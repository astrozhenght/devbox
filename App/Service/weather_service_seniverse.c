#include "weather_service_seniverse.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

static bool text(const cJSON *obj, const char *key, char *out, size_t capacity, bool required)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!v || cJSON_IsNull(v)) { out[0] = 0; return !required; }
    if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) >= capacity)
        return false;
    strcpy(out, v->valuestring);
    return !required || out[0] != 0;
}
static bool number(const cJSON *obj, const char *key, int low, int high, int *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(v) || !v->valuestring || !v->valuestring[0] ||
        isspace((unsigned char)v->valuestring[0])) return false;
    char *end; errno = 0;
    long n = strtol(v->valuestring, &end, 10);
    if (errno || *end || n < low || n > high) return false;
    *out = (int)n; return true;
}
static bool date_valid(const char *date)
{
    if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') return false;
    for (unsigned i = 0; i < 10; i++)
        if (i != 4 && i != 7 && !isdigit((unsigned char)date[i])) return false;
    int y = atoi(date), m = atoi(date + 5), d = atoi(date + 8);
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (y < 1970 || y > 2099 || m < 1 || m > 12) return false;
    int max = days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 || y % 400 == 0));
    return d >= 1 && d <= max;
}
static weather_status_t api_error(const char *code)
{
    if (!strcmp(code,"AP010003") || !strcmp(code,"AP010004")) return WEATHER_KEY_ERROR;
    if (!strcmp(code,"AP010002") || !strcmp(code,"AP010006") ||
        !strcmp(code,"AP010008") || !strcmp(code,"AP010009")) return WEATHER_PERMISSION_ERROR;
    if (!strcmp(code,"AP010010") || !strcmp(code,"AP010011")) return WEATHER_LOCATION_ERROR;
    if (!strcmp(code,"AP010014")) return WEATHER_RATE_LIMIT;
    if (!strcmp(code,"AP010013") || !strcmp(code,"AP010012")) return WEATHER_QUOTA_ERROR;
    return WEATHER_NETWORK_ERROR;
}
weather_status_t weather_seniverse_parse(const char *json, size_t length, weather_result_t *result)
{
    if (!json || !length || !result) return WEATHER_PARSE_ERROR;
    const char *end;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root) return WEATHER_PARSE_ERROR;
    while (end < json + length && isspace((unsigned char)*end)) end++;
    weather_status_t status = WEATHER_PARSE_ERROR;
    if (end != json + length) goto done;
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root,"status_code");
    if (cJSON_IsString(error) && error->valuestring) { status = api_error(error->valuestring); goto done; }
    const cJSON *rows = cJSON_GetObjectItemCaseSensitive(root,"results");
    const cJSON *item = cJSON_GetArrayItem(rows,0);
    const cJSON *location = cJSON_GetObjectItemCaseSensitive(item,"location");
    if (!cJSON_IsArray(rows) || !cJSON_IsObject(item) || !cJSON_IsObject(location)) goto done;
    if (result->endpoint == WEATHER_CURRENT) {
        weather_current_t value = {0}; int n;
        const cJSON *now = cJSON_GetObjectItemCaseSensitive(item,"now");
        if (!cJSON_IsObject(now) || !text(now,"text",value.text,sizeof(value.text),true) ||
            !number(now,"temperature",-100,100,&n)) goto done;
        value.temperature = (int16_t)n;
        if (!number(now,"code",0,255,&n)) goto done;
        value.code = (uint8_t)n;
        if (!text(location,"name",value.city,sizeof(value.city),true) ||
            !text(location,"id",value.location_id,sizeof(value.location_id),true) ||
            !text(item,"last_update",value.updated,sizeof(value.updated),true)) goto done;
        result->data.current = value;
    } else if (result->endpoint == WEATHER_FORECAST) {
        weather_forecast_t value = {0};
        const cJSON *daily = cJSON_GetObjectItemCaseSensitive(item,"daily");
        int count = cJSON_GetArraySize(daily);
        if (!cJSON_IsArray(daily) || count < 1 || count > (int)WEATHER_SERVICE_DAYS) goto done;
        value.count = (uint8_t)count;
        if (!text(location,"name",value.city,sizeof(value.city),true) ||
            !text(location,"id",value.location_id,sizeof(value.location_id),true) ||
            !text(item,"last_update",value.updated,sizeof(value.updated),true)) goto done;
        for (int i = 0; i < count; i++) {
            const cJSON *day = cJSON_GetArrayItem(daily,i); weather_daily_t *d = &value.days[i]; int n;
            if (!cJSON_IsObject(day) || !text(day,"date",d->date,sizeof(d->date),true) ||
                !date_valid(d->date) || (i && strcmp(value.days[i-1].date,d->date) >= 0) ||
                !text(day,"text_day",d->text_day,sizeof(d->text_day),true) ||
                !text(day,"text_night",d->text_night,sizeof(d->text_night),true) ||
                !number(day,"high",-100,100,&n)) goto done;
            d->high = (int16_t)n;
            if (!number(day,"low",-100,100,&n)) goto done;
            d->low = (int16_t)n;
            if (d->low > d->high || !number(day,"code_day",0,255,&n)) goto done;
            d->code_day = (uint8_t)n;
            if (!number(day,"code_night",0,255,&n)) goto done;
            d->code_night = (uint8_t)n;
            if (!text(day,"wind_direction",d->wind_direction,sizeof(d->wind_direction),false) ||
                !text(day,"wind_speed",d->wind_speed,sizeof(d->wind_speed),false) ||
                !text(day,"wind_scale",d->wind_scale,sizeof(d->wind_scale),false)) goto done;
        }
        result->data.forecast = value;
    } else goto done;
    status = WEATHER_OK;
done:
    cJSON_Delete(root); return status;
}
