#ifndef WIFI_SERVICE_PORT_H
#define WIFI_SERVICE_PORT_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "weather_service.h"

typedef struct {
    uint16_t year;
    uint8_t month, day, weekday, hour, minute, second;
} time_service_datetime_t;
typedef enum {
    WIFI_SERVICE_CMD_NONE=0, WIFI_SERVICE_CMD_CONNECT,
    WIFI_SERVICE_CMD_GET_WEATHER, WIFI_SERVICE_CMD_GET_SNTPTIME
} wifi_service_command_t;
typedef enum {
    WIFI_SERVICE_STATUS_OK=0, WIFI_SERVICE_STATUS_ERROR,
    WIFI_SERVICE_STATUS_NO_MEMORY, WIFI_SERVICE_STATUS_WEATHER_PARSE_ERROR,
    WIFI_SERVICE_STATUS_INVALID_SNTP_TIME
} wifi_service_status_t;
typedef struct {
    wifi_service_command_t command;
    uint32_t request_id;
    weather_endpoint_t endpoint;
    char location[48];
} wifi_service_request_t;
typedef struct {
    wifi_service_command_t command;
    wifi_service_status_t status;
    bool is_connected;
    union {weather_result_t weather;char sntp_time[26];} data;
} wifi_service_message_t;
typedef struct {
    char firmware[128];
    uint32_t requests, received_bytes, transport_status, http_status, weather_status;
    uint32_t connected, stage;
    char connect_response[128]; /* 仅CIPSTART回复，不记录请求或凭据。 */
} wifi_service_diagnostics_t;
extern volatile wifi_service_diagnostics_t wifi_diagnostics;
#define QUEUE_SEND_MAX_DELAY (pdMS_TO_TICKS(3000))

bool time_service_parse_sntp_response(const char *response,time_service_datetime_t *time);
bool time_service_get_datetime(time_service_datetime_t *datetime);
bool time_service_set_datetime(const time_service_datetime_t *datetime);
bool wifi_service_enqueue_command(uint8_t command);
bool wifi_service_enqueue_request(const wifi_service_request_t *request);
bool wifi_service_dequeue_request(wifi_service_request_t *request);
bool wifi_service_send_message(wifi_service_message_t *message);
bool wifi_service_create_command_queue(void);
bool wifi_service_create_message_queue(void);
void wifi_service_dispatch_messages(uint32_t now_ms);
/* 仅网络任务调用；返回完整HTTP正文，缓冲区由适配层持有，下次请求前有效。 */
weather_status_t wifi_service_http_get(const char *target,const char **body,size_t *length,unsigned *http_status);
#endif
