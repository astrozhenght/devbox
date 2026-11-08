#include "lunar_calendar.h"
#include <stddef.h>
#include "lunar_calendar_data.h"

#define LUNAR_DATA_FIRST_YEAR 1969U

static bool gregorian_leap(uint16_t year)
{
    return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

static uint16_t gregorian_year_days(uint16_t year)
{
    return gregorian_leap(year) ? 366U : 365U;
}

bool lunar_calendar_convert(uint16_t year, uint8_t month, uint8_t day,
                            lunar_calendar_date_t *lunar)
{
    static const uint8_t month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (lunar == NULL || year < LUNAR_CALENDAR_MIN_YEAR || year > LUNAR_CALENDAR_MAX_YEAR ||
        month < 1U || month > 12U) return false;
    uint8_t length = month_days[month - 1U] + (month == 2U && gregorian_leap(year) ? 1U : 0U);
    if (day < 1U || day > length) return false;
    uint16_t offset = day - 1U;
    for (uint8_t i = 1U; i < month; i++)
        offset += month_days[i - 1U] + (i == 2U && gregorian_leap(year) ? 1U : 0U);

    uint16_t lunar_year = year;
    uint32_t data = lunar_year_data[year - LUNAR_DATA_FIRST_YEAR];
    uint16_t new_year = (uint16_t)((data >> 17) & 0x3FU);
    if (offset < new_year)
    {
        lunar_year--;
        data = lunar_year_data[lunar_year - LUNAR_DATA_FIRST_YEAR];
        offset += gregorian_year_days(lunar_year);
        new_year = (uint16_t)((data >> 17) & 0x3FU);
    }
    offset -= new_year;
    uint8_t leap_month = (uint8_t)(data & 0x0FU);
    uint8_t months = leap_month != 0U ? 13U : 12U;
    for (uint8_t ordinal = 1U; ordinal <= months; ordinal++)
    {
        uint8_t days = (data & (1UL << (ordinal + 3U))) != 0U ? 30U : 29U;
        if (offset < days)
        {
            lunar_calendar_date_t result;
            result.year = lunar_year;
            result.is_leap_month = leap_month != 0U && ordinal == leap_month + 1U;
            result.month = leap_month != 0U && ordinal > leap_month ? ordinal - 1U : ordinal;
            result.day = (uint8_t)(offset + 1U);
            *lunar = result;
            return true;
        }
        offset -= days;
    }
    return false;
}
