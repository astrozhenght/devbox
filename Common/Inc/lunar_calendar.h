#ifndef LUNAR_CALENDAR_H
#define LUNAR_CALENDAR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUNAR_CALENDAR_MIN_YEAR 1970U
#define LUNAR_CALENDAR_MAX_YEAR 2099U

/* 农历日期；闰月沿用原月份数字，例如闰六月为month=6、is_leap_month=true。 */
typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    bool is_leap_month;
} lunar_calendar_date_t;

/* 公历转农历，仅计算日期，不读取RTC、不访问网络、不分配动态内存。
 * 公历范围1970-01-01至2099-12-31；无效日期或NULL输出返回false，输出保持不变。
 */
bool lunar_calendar_convert(uint16_t year, uint8_t month, uint8_t day,
                            lunar_calendar_date_t *lunar);

#ifdef __cplusplus
}
#endif
#endif
