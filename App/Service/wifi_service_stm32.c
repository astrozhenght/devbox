#include "wifi_service_port.h"
#include <stdio.h>      // sscanf()
#include <stdlib.h>     // atoi()
#include <string.h>     // strcmp()、strlen()、memcpy()
#include "FreeRTOS.h"
#include "queue.h"      // QueueHandle_t、xQueueSend()、xQueueReceive()
#include "bsp_rtc.h"
#include "bsp_wifi.h"
#include "cJSON.h"


static QueueHandle_t wifi_command_queue;  // 队列存储命令
static QueueHandle_t wifi_message_queue;  // 队列存储消息

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

// 从响应字符串中解析天气信息
// true： 解析天气信息成功
// false：解析天气信息失败
bool weather_service_parse_response(const char *response, weather_service_info_t *weather)
{
    // 解析整段JSON数据
    cJSON *cjson_test = cJSON_Parse(response);
    if (cjson_test == NULL) // 解析失败
    {
        return false;
    }

    // results是数组
    cJSON *results = cJSON_GetObjectItem(cjson_test, "results");
    if ((results == NULL) || !cJSON_IsArray(results)) // results error
    {
        cJSON_Delete(cjson_test);
        return false;
    }

    // 取results[0]
    cJSON *result0 = cJSON_GetArrayItem(results, 0);
    if (result0 == NULL) // results[0] error
    {
        cJSON_Delete(cjson_test);
        return false;
    }

    // 解析 now 对象
    cJSON *now  = cJSON_GetObjectItem(result0, "now");
    cJSON *text = cJSON_GetObjectItem(now, "text");
    cJSON *code = cJSON_GetObjectItem(now, "code");
    cJSON *temp = cJSON_GetObjectItem(now, "temperature");

    // 复制数据到weather结构体变量中，包含'\0'
    uint32_t len_text = strlen(text->valuestring) + 1;
    if (len_text > sizeof(weather->text))
    {
        return false;
    }
    memcpy((void *)(weather->text), (const void *)(text->valuestring), len_text);
    weather->code = atoi(code->valuestring);
    weather->temperature = atoi(temp->valuestring);

    // last_update
    cJSON *last_update = cJSON_GetObjectItem(result0, "last_update");

    // 释放内存
    cJSON_Delete(cjson_test);
    return true;
}

// 向心知天气发送请求，获取天气数据
// 任务B解析队列中的命令含义，命令匹配后，执行本函数，获取网络天气
bool weather_service_request_update(const uint8_t *weather_request)
{
    if (bsp_wifi_send_request_and_wait_for_text(weather_request, "last_update", 600) != BSP_WIFI_OK)
    {
        return false;
    }
    return true;
}

// 任务A调用本函数，发送命令给队列
bool wifi_service_enqueue_command(uint8_t command)
{
    if (xQueueSend(wifi_command_queue, &command, QUEUE_SEND_MAX_DELAY) == pdPASS)
    {
        return true;
    }
    return false;
}

// 任务B调用本函数，读取任务A的命令
bool wifi_service_dequeue_command(uint8_t *p_command)
{
    if (p_command == NULL)
    {
        return false;
    }

    if (xQueueReceive(wifi_command_queue, p_command, 0) == pdPASS)
    {
        return true;
    }
    return false;
}

// 任务B调用本函数，发送信息给任务A
bool wifi_service_send_message(wifi_service_message_t *message)
{
    if (message == NULL)
    {
        return false;
    }

    if (xQueueSend(wifi_message_queue, message, QUEUE_SEND_MAX_DELAY) == pdPASS)
    {
        return true;
    }
    return false;
}

// 任务A调用本函数，读取任务B发送的信息
bool wifi_service_receive_message(wifi_service_message_t *message)
{
    if (message == NULL)
    {
        return false;
    }

    return (xQueueReceive(wifi_message_queue, message, 0) == pdPASS);
}

bool wifi_service_create_command_queue(void)
{
    wifi_command_queue = xQueueCreate(5, sizeof(uint8_t));
    if(wifi_command_queue != NULL)
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool wifi_service_create_message_queue(void)
{
    wifi_message_queue = xQueueCreate(5, sizeof(wifi_service_message_t));
    if(wifi_message_queue != NULL)
    {
        return true;
    }
    else
    {
        return false;
    }
}

