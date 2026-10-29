#ifndef __BSP_RTC_H__
#define __BSP_RTC_H__

#include <stdint.h>
#include <stdbool.h>
#include "bsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t year;   // 202x
    uint8_t month;   // 1~12
    uint8_t day;     // 1~31
    uint8_t weekday; // 1~7 (星期一到星期日)
    uint8_t hour;    // 0~23
    uint8_t minute;  // 0~59
    uint8_t second;  // 0~59
} bsp_rtc_datetime_t;

bsp_status_t bsp_rtc_init(void);

// 获取RTC时间和日期
bsp_status_t bsp_rtc_get_datetime(bsp_rtc_datetime_t* datetime);

// 校准RTC时间和日期
bsp_status_t bsp_rtc_set_datetime(bsp_rtc_datetime_t* datetime);

// 判断RTC是否需要初始化
bool bsp_rtc_require_init(void);

// 写RTC初始化完成标志
bool bsp_rtc_mark_init(void);


#ifdef __cplusplus
}
#endif


#endif
