#include "wifi_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_wifi.h"
#include "wifi_credentials.h"
#include "wifi_service_port.h"
#include "weather_service_seniverse.h"
#include "weather_config.h"
#include <stdio.h>
#include <string.h>

#define WIFI_TASK_STACK_DEPTH 1280U
static TaskHandle_t wifi_task_handle;
static bool connected;
static TickType_t last_api_tick;
static bool api_sent;
static char unsolicited[160];
static char target[256] __attribute__((section(".axisram")));
static bool command(const char *text,uint16_t timeout)
{
    return bsp_wifi_execute_at_command((const uint8_t*)text,timeout)==BSP_WIFI_OK;
}
static bool connect_network(void)
{
    wifi_diagnostics.stage=1;
    if (!command("AT\r\n",500)) {
        /* 上一版固件可能仍处于透传；先满足+++保护时间，再重新探测AT模式。 */
        vTaskDelay(pdMS_TO_TICKS(1200));
        if(bsp_wifi_exit_passthrough()!=BSP_WIFI_OK || !command("AT\r\n",1000)) return false;
    }
    (void)command("ATE0\r\n",500);
    if (command("AT+GMR\r\n",1000)) {
        const char *s=bsp_wifi_get_response();size_t n=strlen(s);
        if(n>=sizeof(wifi_diagnostics.firmware))n=sizeof(wifi_diagnostics.firmware)-1;
        memcpy((void*)wifi_diagnostics.firmware,s,n);wifi_diagnostics.firmware[n]=0;
    }
    if (!command("AT+CWMODE=1\r\n",1000) ||
        !command("AT+CWJAP=\"" APP_WIFI_SSID "\",\"" APP_WIFI_PASSWORD "\"\r\n",20000) ||
        !command("AT+CIPMUX=0\r\n",500) || !command("AT+CIPMODE=0\r\n",500)) return false;
    (void)command("AT+CIPDINFO=0\r\n",500);
    /* 固件支持时设置SNI；旧固件不支持该命令仍由SSL连接本身报告结果。 */
    if (WEATHER_USE_TLS) {
        (void)command("AT+CIPSSLSIZE=4096\r\n",500); /* Non-OS AT握手缓冲；新固件可不支持。 */
        (void)command("AT+CIPSSLCSNI=\"api.seniverse.com\"\r\n",500);
    }
    wifi_diagnostics.stage=2;return true;
}
static void get_weather(const wifi_service_request_t *request,weather_result_t *result)
{
    result->request_id=request->request_id;result->endpoint=request->endpoint;
    TickType_t now=xTaskGetTickCount();
    if(api_sent && (TickType_t)(now-last_api_tick)<pdMS_TO_TICKS(WEATHER_API_INTERVAL_MS))
        vTaskDelay(pdMS_TO_TICKS(WEATHER_API_INTERVAL_MS)-(TickType_t)(now-last_api_tick));
    last_api_tick=xTaskGetTickCount();api_sent=true;
    int n=snprintf(target,sizeof(target),"/v3/weather/%s.json?key=%s&location=%s&language=zh-Hans&unit=c%s",
        request->endpoint==WEATHER_CURRENT?"now":"daily",APP_WEATHER_API_KEY,request->location,
        request->endpoint==WEATHER_FORECAST?"&start=0&days=3":"");
    if(n<0 || n>=sizeof(target)){result->status=WEATHER_TOO_LARGE;return;}
    const char *body;size_t length;unsigned http_status;
    result->status=wifi_service_http_get(target,&body,&length,&http_status);
    if(result->status==WEATHER_OK) {
        result->status=weather_seniverse_parse(body,length,result);
        if(http_status!=200 && (result->status==WEATHER_OK || result->status==WEATHER_PARSE_ERROR))
            result->status=WEATHER_NETWORK_ERROR;
    }
    wifi_diagnostics.weather_status=result->status;
}
static void get_sntp(wifi_service_message_t *message)
{
    message->status=WIFI_SERVICE_STATUS_INVALID_SNTP_TIME;
    if(!command("AT+CIPSNTPCFG=1,8\r\n",500))return;
    for(unsigned i=0;i<10;i++) {
        if(command("AT+CIPSNTPTIME?\r\n",500)) {
            const char *begin=strstr(bsp_wifi_get_response(),"+CIPSNTPTIME:");
            if(begin) {
                begin+=strlen("+CIPSNTPTIME:");const char *end=strpbrk(begin,"\r\n");
                size_t n=end?(size_t)(end-begin):strlen(begin);
                if(n<sizeof(message->data.sntp_time)) {
                    char text[26];memcpy(text,begin,n);text[n]=0;
                    time_service_datetime_t dt={0};
                    if(time_service_parse_sntp_response(text,&dt) && dt.year>=2000 && dt.year<=2099) {
                        memcpy(message->data.sntp_time,text,n+1);message->status=WIFI_SERVICE_STATUS_OK;return;
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
static void wifi_task_entry(void *argument)
{
    (void)argument;
    wifi_service_request_t request;wifi_service_message_t message;
    vTaskDelay(pdMS_TO_TICKS(500));
    bool receiving=bsp_wifi_start_receive()==BSP_WIFI_OK;
    TickType_t reconnect_at=0;
    for(;;) {
        if(!wifi_service_dequeue_request(&request)) {
            if(connected) {
                size_t n,keep=strlen(unsolicited);
                bsp_wifi_status_t rx=bsp_wifi_read((uint8_t*)unsolicited+keep,sizeof(unsolicited)-keep-1U,&n);
                unsolicited[keep+n]=0;
                if(rx!=BSP_WIFI_OK || strstr(unsolicited,"WIFI DISCONNECT")) {
                    connected=false;wifi_diagnostics.connected=0;
                    memset(&message,0,sizeof(message));message.command=WIFI_SERVICE_CMD_CONNECT;
                    message.status=WIFI_SERVICE_STATUS_ERROR;(void)wifi_service_send_message(&message);
                    reconnect_at=xTaskGetTickCount()+pdMS_TO_TICKS(5000);
                }
                keep=strlen(unsolicited);
                if(keep>24)memmove(unsolicited,unsolicited+keep-24,25);
            }
            if(!connected && (int32_t)(xTaskGetTickCount()-reconnect_at)>=0) {
                receiving=receiving || bsp_wifi_start_receive()==BSP_WIFI_OK;
                connected=receiving && connect_network();
                unsolicited[0]=0;
                memset(&message,0,sizeof(message));message.command=WIFI_SERVICE_CMD_CONNECT;
                message.status=connected?WIFI_SERVICE_STATUS_OK:WIFI_SERVICE_STATUS_ERROR;
                message.is_connected=connected;(void)wifi_service_send_message(&message);
                reconnect_at=xTaskGetTickCount()+pdMS_TO_TICKS(30000);
            }
            continue;
        }
        memset(&message,0,sizeof(message));message.command=request.command;
        if(request.command==WIFI_SERVICE_CMD_CONNECT) {
            connected=receiving && connect_network();
            unsolicited[0]=0;
            message.status=connected?WIFI_SERVICE_STATUS_OK:WIFI_SERVICE_STATUS_ERROR;
            reconnect_at=xTaskGetTickCount()+pdMS_TO_TICKS(30000);
        } else if(request.command==WIFI_SERVICE_CMD_GET_WEATHER) {
            get_weather(&request,&message.data.weather);
            message.status=message.data.weather.status==WEATHER_OK?WIFI_SERVICE_STATUS_OK:WIFI_SERVICE_STATUS_ERROR;
            if(message.data.weather.status==WEATHER_NETWORK_ERROR) {
                /* 仅真实掉线才重连，API拒绝/SSL错误不反复重入CWJAP。 */
                if(!command("AT+CWJAP?\r\n",1000) || !strstr(bsp_wifi_get_response(),"+CWJAP:")) connected=false;
            }
        } else if(request.command==WIFI_SERVICE_CMD_GET_SNTPTIME) get_sntp(&message);
        message.is_connected=connected;wifi_diagnostics.connected=connected;
        if(!wifi_service_send_message(&message)) {
            /* UI恢复后服务的60秒请求超时会结束繁忙态，不在这里死循环。 */
            wifi_diagnostics.stage=90;
        }
    }
}
bool wifi_task_create(void)
{
    return xTaskCreate(wifi_task_entry,"wifi_service",WIFI_TASK_STACK_DEPTH,NULL,2,&wifi_task_handle)==pdPASS;
}
