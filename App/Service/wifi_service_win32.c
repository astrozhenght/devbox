#include <windows.h>


void get_system_time_str_win32(char* buf, size_t len)
{
    SYSTEMTIME st;
    GetLocalTime(&st);  // 获取本地时间（含毫秒）

    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

bool time_service_parse_sntp_response(const char *response, time_service_datetime_t *time)
{
    (void)response;
    (void)response;
    return true;
}


// 读取本地日期和时间
bool time_service_get_datetime(time_service_datetime_t *datetime)
{
    if (bsp_rtc_get_datetime((bsp_rtc_datetime_t *)datetime) == BSP_OK)
    {
        return true;
    }
    return false;
}

uint8_t time_service_get_weekday(uint16_t y, uint8_t m, uint8_t d)
{
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

    if (m < 3)
    {
        y--;
    }

    /* w: 0=Sunday, 1=Monday, ..., 6=Saturday */
    uint8_t w = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;

    /* convert to: 1~7 => Monday~Sunday */
    return w == 0 ? 7 : w;
}

