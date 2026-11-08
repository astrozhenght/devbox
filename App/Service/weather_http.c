#include "weather_http.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

static bool equal(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {a++; b++;}
    return !*a && !*b;
}
static bool decimal(const char *s, size_t *value)
{
    if (!isdigit((unsigned char)*s)) return false;
    *value = 0;
    while (isdigit((unsigned char)*s)) {
        if (*value > (SIZE_MAX - 9U) / 10U) return false;
        *value = *value * 10U + (unsigned)(*s++ - '0');
    }
    while (*s == ' ' || *s == '\t') s++;
    return !*s;
}
static bool parse_headers(weather_http_t *h)
{
    unsigned major, minor;
    if (sscanf(h->header,"HTTP/%u.%u %u",&major,&minor,&h->status) != 3 ||
        major != 1 || minor > 1 || h->status < 200 || h->status > 599) return false;
    char *p = strstr(h->header,"\r\n");
    if (!p) return false;
    p += 2;
    while (*p && *p != '\r') {
        char *end = strstr(p,"\r\n"), *colon = strchr(p,':');
        if (!end || !colon || colon > end) return false;
        *colon = 0; *end = 0; char *v = colon + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *tail = v + strlen(v);
        while (tail > v && (tail[-1] == ' ' || tail[-1] == '\t')) *--tail = 0;
        if (equal(p,"Content-Length")) {
            size_t length;
            if (!decimal(v,&length) || (h->has_length && h->expected != length)) return false;
            h->has_length = true; h->expected = length;
            if (length > WEATHER_HTTP_BODY_CAP) {h->too_large = true;return false;}
        } else if (equal(p,"Transfer-Encoding")) {
            if (!equal(v,"chunked")) return false;
            h->chunked = true;
        } else if (equal(p,"Content-Encoding") && !equal(v,"identity")) return false;
        p = end + 2;
    }
    if (h->chunked && h->has_length) return false;
    h->headers_done = true;
    if (h->has_length && !h->expected) h->done = true;
    return true;
}
void weather_http_init(weather_http_t *h) { memset(h,0,sizeof(*h)); }
static bool append(weather_http_t *h, uint8_t c)
{
    if (h->body_size == WEATHER_HTTP_BODY_CAP) {h->too_large = true;return false;}
    h->body[h->body_size++] = (char)c; h->body[h->body_size] = 0; return true;
}
static bool chunk_byte(weather_http_t *h,uint8_t c)
{
    if (h->chunk_state == 1) {
        if (!append(h,c)) return false;
        if (--h->chunk_left == 0) h->chunk_state = 2;
        return true;
    }
    if (h->chunk_state == 2) {if (c != '\r') return false;h->chunk_state = 3;return true;}
    if (h->chunk_state == 3) {if (c != '\n') return false;h->chunk_state = 0;return true;}
    if (h->line_size + 1 >= sizeof(h->line)) return false;
    h->line[h->line_size++] = (char)c; h->line[h->line_size] = 0;
    if (h->line_size < 2 || h->line[h->line_size-2] != '\r' || c != '\n') return true;
    h->line[h->line_size-2] = 0;
    if (h->chunk_state == 4) {
        if (h->line_size == 2) h->done = true;
        else if (!strchr(h->line,':')) return false;
    } else {
        if (!isxdigit((unsigned char)h->line[0])) return false;
        char *end; errno = 0; unsigned long count = strtoul(h->line,&end,16);
        if (errno || (*end && *end != ';')) return false;
        if (count > WEATHER_HTTP_BODY_CAP - h->body_size) {h->too_large = true;return false;}
        h->chunk_left = count; h->chunk_state = count ? 1 : 4;
    }
    h->line_size = 0; return true;
}
bool weather_http_feed(weather_http_t *h,const uint8_t *data,size_t length)
{
    if (h->error) return false;
    for (size_t i = 0; i < length; i++) {
        if (h->done) {h->error = true;return false;}
        if (!h->headers_done) {
            if (h->header_size + 1 >= sizeof(h->header)) {h->too_large=true;goto error;}
            h->header[h->header_size++] = (char)data[i]; h->header[h->header_size] = 0;
            if (h->header_size >= 4 && !memcmp(h->header+h->header_size-4,"\r\n\r\n",4))
                if (!parse_headers(h)) goto error;
        } else if (h->chunked) { if (!chunk_byte(h,data[i])) goto error; }
        else {
            if (!append(h,data[i])) goto error;
            if (h->has_length && h->body_size == h->expected) h->done = true;
        }
    }
    return true;
error:
    h->error = true; return false;
}
bool weather_http_close(weather_http_t *h)
{
    if (h->headers_done && !h->chunked && !h->has_length && !h->error) h->done = true;
    if (!h->done) h->error = true;
    return h->done && !h->error;
}
