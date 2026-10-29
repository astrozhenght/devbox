#ifndef __WIFI_SERVICE_PORT_H__
#define __WIFI_SERVICE_PORT_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    char text[25];      // 最长数据：雷阵雨伴有冰雹，21个字节
    int8_t temperature; // 温度
    uint8_t code;       // 天气现象代码
} weather_service_info_t;

typedef struct
{
    uint16_t year;
    uint8_t month;   // 1~12
    uint8_t day;     // 1~31
    uint8_t weekday; // 1~7 (星期一到星期日)
    uint8_t hour;    // 0~23
    uint8_t minute;  // 0~59
    uint8_t second;  // 0~59
} time_service_datetime_t;

// typedef enum
// {
//     WIFI_SERVICE_STATE_DISCONNECTED = 0,
//     WIFI_SERVICE_STATE_CONNECTING,
//     WIFI_SERVICE_STATE_CONNECTED,
//     WIFI_SERVICE_STATE_ERROR
// } wifi_service_state_t;

typedef enum
{
    WIFI_SERVICE_CMD_NONE = 0,
    WIFI_SERVICE_CMD_CONNECT,
    WIFI_SERVICE_CMD_GET_WEATHER,
    WIFI_SERVICE_CMD_GET_SNTPTIME,
} wifi_service_command_t;

// typedef struct
// {
//     wifi_service_state_t state;
//     char ssid[33];
//     char ip_address[16];
//     int8_t rssi;
// } wifi_service_info_t;

typedef enum
{
    WIFI_SERVICE_STATUS_OK = 0,                  /* 操作成功 */
    WIFI_SERVICE_STATUS_ERROR = 1,               /* 通用操作失败 */
    WIFI_SERVICE_STATUS_NO_MEMORY = 2,           /* 内存分配失败 */
    WIFI_SERVICE_STATUS_WEATHER_PARSE_ERROR = 3, /* 天气数据解析失败 */
    WIFI_SERVICE_STATUS_INVALID_SNTP_TIME = 4    /* SNTP时间无效，例如返回1970年 */
} wifi_service_status_t;

typedef struct
{
    wifi_service_command_t command; // 命令
    wifi_service_status_t status;   // 当前通知值对应的处理状态
    bool is_connected; // 当前wifi连接状态
    union
    {
        weather_service_info_t weather;
        char sntp_time[26];
    } data;
} wifi_service_message_t;

#define QUEUE_SEND_MAX_DELAY                (pdMS_TO_TICKS(3000)) // 队列发送最大等待时间

// 解析SNTP数据，获取日期和时间
bool time_service_parse_sntp_response(const char *response, time_service_datetime_t *time);

// 从当前平台读取日期和时间
bool time_service_get_datetime(time_service_datetime_t *datetime);

// 向当前平台写入日期和时间
bool time_service_set_datetime(const time_service_datetime_t *datetime);

// 解析天气数据
bool weather_service_parse_response(const char *response, weather_service_info_t *weather);

// 向心知天气发送请求，获取天气数据
bool weather_service_request_update(const uint8_t *weather_request);

// 任务A调用本函数，发送命令给队列
bool wifi_service_enqueue_command(uint8_t command);

// 任务B调用本函数，读取任务A的命令
bool wifi_service_dequeue_command(uint8_t *p_command);

// 任务B调用本函数，发送信息给任务A
bool wifi_service_send_message(wifi_service_message_t *message);

// 任务A调用本函数，读取任务B发送的信息
bool wifi_service_receive_message(wifi_service_message_t *message);

bool wifi_service_create_command_queue(void);
bool wifi_service_create_message_queue(void);

#ifdef __cplusplus
}
#endif

#endif
