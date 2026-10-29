#include "bsp_rtc.h"
#include "rtc.h"

bsp_status_t bsp_rtc_init(void)
{
    if (platform_rtc_init() == HAL_OK)
    {
        return BSP_OK;
    }
    return BSP_ERROR;
}

// 获取RTC时间和日期
bsp_status_t bsp_rtc_get_datetime(bsp_rtc_datetime_t* datetime)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    if (platform_rtc_get_datetime(&time, &date) != HAL_OK)
    {
        return BSP_ERROR;
    }
    datetime->year    = date.Year + 2000;
    datetime->month   = date.Month;
    datetime->day     = date.Date;
    datetime->weekday = date.WeekDay;
    datetime->hour    = time.Hours;
    datetime->minute  = time.Minutes;
    datetime->second  = time.Seconds;
    return BSP_OK;
}

// 校准RTC时间和日期
bsp_status_t bsp_rtc_set_datetime(bsp_rtc_datetime_t* datetime)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};

    time.Hours   = datetime->hour;
    time.Minutes = datetime->minute;
    time.Seconds = datetime->second;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;
    date.WeekDay = datetime->weekday;
    date.Month   = datetime->month;
    date.Date    = datetime->day;
    date.Year    = datetime->year - 2000;
    if (platform_rtc_set_datetime(&time, &date) != HAL_OK)
    {
        return BSP_ERROR;
    }
    return BSP_OK;
}

// 判断RTC是否需要初始化
bool bsp_rtc_require_init(void)
{
    return platform_rtc_require_init();
}

// 写RTC初始化完成标志
bool bsp_rtc_mark_init(void)
{
    return platform_rtc_mark_init();
}
