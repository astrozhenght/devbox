#include "weather_service.h"
#include "weather_config.h"
#include "wifi_service_port.h"
#include <string.h>

static weather_snapshot_t snapshot;
static struct {uint32_t id, deadline, started;unsigned retries;} jobs[2];
static uint32_t next_id, last_manual;
static bool manual_sent;
static uint32_t last_date_check, calendar_date;
static bool due(uint32_t now,uint32_t time) {return (int32_t)(now-time) >= 0;}
static uint32_t interval(unsigned i) {return i ? WEATHER_DAILY_INTERVAL_MS : WEATHER_NOW_INTERVAL_MS;}
static bool enqueue(unsigned i,uint32_t now)
{
    wifi_service_request_t r = {0};
    r.command = WIFI_SERVICE_CMD_GET_WEATHER;r.endpoint = i ? WEATHER_FORECAST : WEATHER_CURRENT;
    r.request_id = ++next_id;if (!r.request_id) r.request_id = ++next_id;
    if(sizeof(WEATHER_LOCATION)>sizeof(r.location))return false;
    memcpy(r.location,WEATHER_LOCATION,sizeof(WEATHER_LOCATION));
    if (!wifi_service_enqueue_request(&r)) return false;
    jobs[i].id = r.request_id;jobs[i].started = now;
    snapshot.pending |= r.endpoint;snapshot.version++;return true;
}
void weather_service_init(void)
{
    memset(&snapshot,0,sizeof(snapshot));memset(jobs,0,sizeof(jobs));
    next_id = 0;manual_sent = false;last_manual = 0;
    last_date_check=0;calendar_date=0;
}
const weather_snapshot_t *weather_service_get_snapshot(void) {return &snapshot;}
void weather_service_set_connected(bool connected,uint32_t now)
{
    if (snapshot.connected != connected) {
        snapshot.connected = connected;snapshot.version++;
        if (connected) for (unsigned i=0;i<2;i++) {jobs[i].deadline=now;jobs[i].retries=0;}
    }
}
void weather_service_accept_result(const weather_result_t *r,uint32_t now)
{
    if (!r || (r->endpoint != WEATHER_CURRENT && r->endpoint != WEATHER_FORECAST)) return;
    unsigned i = r->endpoint == WEATHER_FORECAST;
    if (!(snapshot.pending & r->endpoint) || jobs[i].id != r->request_id) return;
    snapshot.pending &= (uint8_t)~r->endpoint;
    if (i) snapshot.forecast_status=r->status;else snapshot.current_status=r->status;
    if (r->status == WEATHER_OK) {
        if (i) {snapshot.forecast=r->data.forecast;snapshot.forecast_valid=true;snapshot.forecast_received_ms=now;}
        else {snapshot.current=r->data.current;snapshot.current_valid=true;snapshot.current_received_ms=now;}
        jobs[i].retries=0;jobs[i].deadline=now+interval(i);
    } else {
        static const uint32_t delays[]={5000,15000,60000};
        bool transient=r->status==WEATHER_NETWORK_ERROR || r->status==WEATHER_TIMEOUT || r->status==WEATHER_RATE_LIMIT;
        jobs[i].deadline=now+(transient && jobs[i].retries<3 ? delays[jobs[i].retries++] : interval(i));
    }
    snapshot.version++;
}
void weather_service_poll(uint32_t now)
{
    if((uint32_t)(now-last_date_check)>=1000U) {
        last_date_check=now;time_service_datetime_t time={0};
        if(time_service_get_datetime(&time) && time.year>=1970 && time.year<=2099 &&
            time.month>=1 && time.month<=12 && time.day>=1 && time.day<=31) {
            uint32_t day=time.year*10000U+time.month*100U+time.day;
            if(calendar_date && calendar_date!=day)jobs[1].deadline=now;
            calendar_date=day;
        }
    }
    for (unsigned i=0;i<2;i++) {
        weather_endpoint_t endpoint=i ? WEATHER_FORECAST : WEATHER_CURRENT;
        if ((snapshot.pending & endpoint) && (uint32_t)(now-jobs[i].started)>=60000U) {
            weather_result_t r={0};r.request_id=jobs[i].id;r.endpoint=endpoint;r.status=WEATHER_TIMEOUT;
            weather_service_accept_result(&r,now);
        }
        if (snapshot.connected && !(snapshot.pending & endpoint) && due(now,jobs[i].deadline))
            if (!enqueue(i,now)) jobs[i].deadline=now+1000U;
    }
}
bool weather_service_request_refresh(uint32_t now)
{
    if (!snapshot.connected || snapshot.pending ||
        (manual_sent && (uint32_t)(now-last_manual)<WEATHER_MANUAL_COOLDOWN_MS)) return false;
    bool accepted=false;
    for (unsigned i=0;i<2;i++) {
        if (enqueue(i,now)) accepted=true;
        else jobs[i].deadline=now+1000U;
    }
    if (accepted) {last_manual=now;manual_sent=true;}return accepted;
}
const char *weather_service_status_text(weather_status_t status)
{
    switch(status) {
    case WEATHER_OK:return "已更新";
    case WEATHER_NETWORK_ERROR:return "网络连接失败";
    case WEATHER_TIMEOUT:return "请求超时";
    case WEATHER_PARSE_ERROR:return "天气数据格式错误";
    case WEATHER_TOO_LARGE:return "天气响应过大";
    case WEATHER_KEY_ERROR:return "天气密钥错误";
    case WEATHER_PERMISSION_ERROR:return "天气接口无权限";
    case WEATHER_LOCATION_ERROR:return "天气城市无效";
    case WEATHER_RATE_LIMIT:return "请求过于频繁";
    case WEATHER_QUOTA_ERROR:return "天气服务额度不足";
    case WEATHER_NO_MEMORY:return "天气内存不足";
    default:return "天气更新失败";
    }
}
