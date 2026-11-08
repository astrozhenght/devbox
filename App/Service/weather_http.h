#ifndef WEATHER_HTTP_H
#define WEATHER_HTTP_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "weather_config.h"
typedef struct {
    char header[1024], body[WEATHER_HTTP_BODY_CAP + 1U], line[128];
    size_t header_size, body_size, expected, line_size, chunk_left;
    unsigned status, chunk_state;
    bool headers_done, chunked, has_length, done, error, too_large;
} weather_http_t;
void weather_http_init(weather_http_t *http);
bool weather_http_feed(weather_http_t *http, const uint8_t *data, size_t length);
bool weather_http_close(weather_http_t *http);
#endif
