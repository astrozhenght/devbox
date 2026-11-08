#ifndef WEATHER_SERVICE_SENIVERSE_H
#define WEATHER_SERVICE_SENIVERSE_H
#include <stddef.h>
#include "weather_service.h"
/* 不读取串口；失败时不改写结果的数据部分。 */
weather_status_t weather_seniverse_parse(const char *json, size_t length,
                                       weather_result_t *result);
#endif
