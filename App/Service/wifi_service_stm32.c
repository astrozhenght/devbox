#include "wifi_service_port.h"
#include <stdio.h>      // sscanf()
#include <stdlib.h>     // atoi()
#include <string.h>     // strcmp()、strlen()、memcpy()
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"      // QueueHandle_t、xQueueSend()、xQueueReceive()
#include "bsp_rtc.h"
#include "bsp_wifi.h"
#include "weather_http.h"
#include "weather_config.h"


static QueueHandle_t wifi_command_queue;  // 队列存储命令
static QueueHandle_t wifi_message_queue;  // 队列存储消息
static StaticQueue_t command_control, message_control;
static uint8_t command_memory[5*sizeof(wifi_service_request_t)] __attribute__((section(".axisram")));
static uint8_t message_memory[4*sizeof(wifi_service_message_t)] __attribute__((section(".axisram")));
static weather_http_t http __attribute__((section(".axisram")));
static char http_request[512] __attribute__((section(".axisram")));
volatile wifi_service_diagnostics_t wifi_diagnostics;

// SNTP的月份定义
static const char *month_table[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// SNTP的星期定义
static const char *week_table[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

// true:  解析SNTP时间成功
// false: 解析SNTP时间失败
bool time_service_parse_sntp_response(const char *response, time_service_datetime_t *time)
{
    char week_str[4] = {0};
    char mon_str[4]  = {0};
    int  day, hour, min, sec, year;

    if ((response == NULL) || (time == NULL))
    {
        return false;
    }

    // 解析字符串
    if (sscanf(response, "%3s %3s %d %d:%d:%d %d",
               week_str, mon_str,
               &day, &hour, &min, &sec, &year) != 7)
    {
        return false;
    }

    // 星期转换，1~7对应周一至周日
    for (uint8_t i = 0; i < 7; i++)
    {
        if (strcmp(week_str, week_table[i]) == 0)
        {
            time->weekday = (i ? i : 7);
            break;
        }
    }

    // 月份转换
    for (uint8_t i = 0; i < 12; i++)
    {
        if (strcmp(mon_str, month_table[i]) == 0)
        {
            time->month = i + 1;
            break;
        }
    }

    time->year   = year;
    time->day    = day;
    time->hour   = hour;
    time->minute = min;
    time->second = sec;
    return true;   // 成功
}

// 从RTC读取日期和时间
bool time_service_get_datetime(time_service_datetime_t *datetime)
{
    if (bsp_rtc_get_datetime((bsp_rtc_datetime_t *)datetime) == BSP_OK)
    {
        return true;
    }
    return false;
}

// 向RTC写入日期和时间
bool time_service_set_datetime(const time_service_datetime_t *datetime)
{
    if (bsp_rtc_set_datetime((bsp_rtc_datetime_t*)datetime) == BSP_OK)
    {
        return true;
    }
    return false;
}

bool wifi_service_enqueue_request(const wifi_service_request_t *request)
{
    return request && wifi_command_queue && xQueueSend(wifi_command_queue,request,0) == pdPASS;
}
bool wifi_service_enqueue_command(uint8_t command)
{
    wifi_service_request_t request = {0}; request.command = (wifi_service_command_t)command;
    return wifi_service_enqueue_request(&request);
}
bool wifi_service_dequeue_request(wifi_service_request_t *request)
{
    return request && xQueueReceive(wifi_command_queue,request,pdMS_TO_TICKS(50)) == pdPASS;
}
bool wifi_service_send_message(wifi_service_message_t *message)
{
    return message && xQueueSend(wifi_message_queue,message,pdMS_TO_TICKS(500)) == pdPASS;
}
bool wifi_service_create_command_queue(void)
{
    wifi_command_queue = xQueueCreateStatic(5,sizeof(wifi_service_request_t),command_memory,&command_control);
    return wifi_command_queue != NULL;
}
bool wifi_service_create_message_queue(void)
{
    wifi_message_queue = xQueueCreateStatic(4,sizeof(wifi_service_message_t),message_memory,&message_control);
    return wifi_message_queue != NULL;
}
void wifi_service_dispatch_messages(uint32_t now_ms)
{
    wifi_service_message_t message;
    for (unsigned i = 0; i < 4 && xQueueReceive(wifi_message_queue,&message,0) == pdPASS; i++) {
        if (message.command == WIFI_SERVICE_CMD_GET_WEATHER) {
            weather_service_set_connected(message.is_connected,now_ms);
            weather_service_accept_result(&message.data.weather,now_ms);
        } else if (message.command == WIFI_SERVICE_CMD_CONNECT)
            weather_service_set_connected(message.status == WIFI_SERVICE_STATUS_OK,now_ms);
        else if (message.command == WIFI_SERVICE_CMD_GET_SNTPTIME && message.status == WIFI_SERVICE_STATUS_OK) {
            time_service_datetime_t time = {0};
            if (time_service_parse_sntp_response(message.data.sntp_time,&time) && time.year >= 2000 && time.year <= 2099)
                (void)time_service_set_datetime(&time);
        }
    }
}

/* 普通模式+IPD帧解包，AT提示仅在负载之外解释；HTTP消息完整性由独立解码器判断。 */
weather_status_t wifi_service_http_get(const char *target,const char **body,size_t *length,unsigned *http_status)
{
    weather_status_t status = WEATHER_NETWORK_ERROR;
    char command[96], line[128]; size_t line_size = 0, payload_left = 0;
    int count = snprintf(http_request,sizeof(http_request),
        "GET %s HTTP/1.1\r\nHost: api.seniverse.com\r\nConnection: close\r\nAccept-Encoding: identity\r\n\r\n",target);
    if (count < 0 || count >= sizeof(http_request)) return WEATHER_TOO_LARGE;
    weather_http_init(&http);*body = NULL;*length = 0;*http_status = 0;
    wifi_diagnostics.requests++; wifi_diagnostics.stage = 3;
    (void)bsp_wifi_execute_at_command((const uint8_t*)"AT+CIPCLOSE\r\n",500);
    snprintf(command,sizeof(command),"AT+CIPSTART=\"%s\",\"api.seniverse.com\",%u\r\n",
        WEATHER_USE_TLS ? "SSL" : "TCP",WEATHER_USE_TLS ? 443U : 80U);
    bsp_wifi_status_t transport = bsp_wifi_execute_at_command((const uint8_t*)command,10000);
    wifi_diagnostics.transport_status = transport;
    snprintf((char*)wifi_diagnostics.connect_response,sizeof(wifi_diagnostics.connect_response),
        "%.127s",bsp_wifi_get_response());
    if (transport != BSP_WIFI_OK) goto cleanup;
    snprintf(command,sizeof(command),"AT+CIPSEND=%u\r\n",(unsigned)count);
    bsp_wifi_flush_rx();
    if (bsp_wifi_send_bytes((const uint8_t*)command,strlen(command)) != BSP_WIFI_OK ||
        bsp_wifi_wait_text(">",2000) != BSP_WIFI_OK) goto cleanup;
    if (bsp_wifi_send_bytes((const uint8_t*)http_request,(size_t)count) != BSP_WIFI_OK) goto cleanup;
    wifi_diagnostics.stage = 4;
    TickType_t start = xTaskGetTickCount();
    while ((TickType_t)(xTaskGetTickCount()-start) < pdMS_TO_TICKS(WEATHER_HTTP_TIMEOUT_MS)) {
        uint8_t bytes[128];size_t n;
        transport = bsp_wifi_read(bytes,sizeof(bytes),&n);
        if (transport != BSP_WIFI_OK) {status = WEATHER_TOO_LARGE;goto cleanup;}
        for (size_t i = 0; i < n; i++) {
            if (payload_left) {
                wifi_diagnostics.received_bytes++;
                if (!weather_http_feed(&http,&bytes[i],1)) {
                    status = http.too_large ? WEATHER_TOO_LARGE : WEATHER_PARSE_ERROR;goto cleanup;
                }
                payload_left--;
                if (!payload_left && http.done) goto complete;
            } else {
                if (line_size + 1 >= sizeof(line)) {status = WEATHER_PARSE_ERROR;goto cleanup;}
                line[line_size++] = (char)bytes[i];line[line_size] = 0;
                if (bytes[i] == ':' && !strncmp(line,"+IPD,",5)) {
                    char *end; unsigned long size = strtoul(line+5,&end,10);
                    if (end == line+5 || strcmp(end,":") || !size || size > 65535) {
                        status = WEATHER_PARSE_ERROR;goto cleanup;
                    }
                    payload_left = size;line_size = 0;
                } else if (bytes[i] == '\n') {
                    if (strstr(line,"CLOSED")) {
                        if (weather_http_close(&http)) goto complete;
                        status = WEATHER_PARSE_ERROR;goto cleanup;
                    }
                    if (strstr(line,"ERROR") || strstr(line,"FAIL") || strstr(line,"WIFI DISCONNECT")) goto cleanup;
                    line_size = 0;
                }
            }
        }
        if (!n) vTaskDelay(pdMS_TO_TICKS(2));
    }
    status = WEATHER_TIMEOUT;goto cleanup;
complete:
    *http_status = http.status;wifi_diagnostics.http_status = http.status;
    *body = http.body;*length = http.body_size;
    status = WEATHER_OK;
cleanup:
    wifi_diagnostics.transport_status = transport;
    /* 每条退出路径均收尾，不再依赖+++及固定保护延时。 */
    (void)bsp_wifi_execute_at_command((const uint8_t*)"AT+CIPCLOSE\r\n",500);
    wifi_diagnostics.stage = 5;
    return status;
}
