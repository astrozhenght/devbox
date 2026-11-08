#ifndef WEATHER_SERVICE_H
#define WEATHER_SERVICE_H
#include <stdbool.h>
#include <stdint.h>

#define WEATHER_SERVICE_DAYS 3U
#define WEATHER_TEXT_CAP 48U
typedef enum {
    WEATHER_CURRENT = 1,
    WEATHER_FORECAST = 2
} weather_endpoint_t;
typedef enum {
    WEATHER_OK = 0, WEATHER_NETWORK_ERROR, WEATHER_TIMEOUT, WEATHER_PARSE_ERROR,
    WEATHER_TOO_LARGE, WEATHER_KEY_ERROR, WEATHER_PERMISSION_ERROR,
    WEATHER_LOCATION_ERROR, WEATHER_RATE_LIMIT, WEATHER_QUOTA_ERROR,
    WEATHER_NO_MEMORY
} weather_status_t;
typedef struct {
    char text[WEATHER_TEXT_CAP];
    int16_t temperature;
    uint8_t code;
    char city[48];
    char location_id[24];
    char updated[36];
} weather_current_t;
typedef struct {
    char date[11];
    char text_day[WEATHER_TEXT_CAP];
    char text_night[WEATHER_TEXT_CAP];
    char wind_direction[32];
    char wind_speed[16];
    char wind_scale[12];
    int16_t high;
    int16_t low;
    uint8_t code_day;
    uint8_t code_night;
} weather_daily_t;
typedef struct {
    weather_daily_t days[WEATHER_SERVICE_DAYS];
    uint8_t count;
    char city[48];
    char location_id[24];
    char updated[36];
} weather_forecast_t;
typedef struct {
    uint32_t request_id;
    weather_endpoint_t endpoint;
    weather_status_t status;
    union { weather_current_t current; weather_forecast_t forecast; } data;
} weather_result_t;
typedef struct {
    weather_current_t current;
    weather_forecast_t forecast;
    bool current_valid;
    bool forecast_valid;
    bool connected;
    uint8_t pending;
    weather_status_t current_status;
    weather_status_t forecast_status;
    uint32_t version;
    uint32_t current_received_ms;
    uint32_t forecast_received_ms;
} weather_snapshot_t;

/* 以下接口由UI任务调用，网络任务仅通过队列提交weather_result_t。 */
void weather_service_init(void);
void weather_service_poll(uint32_t now_ms);
bool weather_service_request_refresh(uint32_t now_ms);
void weather_service_accept_result(const weather_result_t *result, uint32_t now_ms);
void weather_service_set_connected(bool connected, uint32_t now_ms);
const weather_snapshot_t *weather_service_get_snapshot(void);
const char *weather_service_status_text(weather_status_t status);
#endif
