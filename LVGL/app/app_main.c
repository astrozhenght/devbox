#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl/lvgl.h"
#include "app_main.h"
#include "app_uart.h"
#include "app_calendar.h"
#include "./utils/comm_queue.h"
#include "./utils/str_convert.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#if defined(_WIN32) || defined(WIN32) // Windows平台
#define __disable_irq()   ((void)0)
#define __enable_irq()    ((void)0)
#include <windows.h>
#else // STM32平台
#include "stm32h7xx.h"
#include "usart.h"
#include "dma.h"
#include "rtc.h"

RTC_TimeTypeDef sTime = {0};
RTC_DateTypeDef sDate = {0};
#endif

//声明字体
LV_FONT_DECLARE(ceshi_12_1);
LV_FONT_DECLARE(ceshi_16_1);
LV_FONT_DECLARE(ceshi_24_1);
LV_FONT_DECLARE(ceshi_24_4);
LV_FONT_DECLARE(ceshi_32_1);
LV_FONT_DECLARE(ceshi_32_2);
LV_FONT_DECLARE(ceshi_32_4);
LV_FONT_DECLARE(ceshi_32_8);
LV_FONT_DECLARE(ceshi_48_1);
LV_FONT_DECLARE(ceshi_72_4);

LV_IMG_DECLARE(folder48);
LV_IMG_DECLARE(debug);
LV_IMG_DECLARE(music);
LV_IMG_DECLARE(setting);
LV_IMG_DECLARE(uart);
LV_IMG_DECLARE(weather);
LV_IMG_DECLARE(calendar);

LV_IMG_DECLARE(c0);
LV_IMG_DECLARE(c1);
LV_IMG_DECLARE(c2);
LV_IMG_DECLARE(c3);
LV_IMG_DECLARE(c4);
LV_IMG_DECLARE(c5);
LV_IMG_DECLARE(c6);
LV_IMG_DECLARE(c7);
LV_IMG_DECLARE(c8);
LV_IMG_DECLARE(c9);
LV_IMG_DECLARE(c10);
LV_IMG_DECLARE(c11);
LV_IMG_DECLARE(c12);
LV_IMG_DECLARE(c13);
LV_IMG_DECLARE(c14);
LV_IMG_DECLARE(c15);
LV_IMG_DECLARE(c16);
LV_IMG_DECLARE(c17);
LV_IMG_DECLARE(c18);
LV_IMG_DECLARE(c19);
LV_IMG_DECLARE(c20);
LV_IMG_DECLARE(c21);
LV_IMG_DECLARE(c22);
LV_IMG_DECLARE(c23);
LV_IMG_DECLARE(c24);
LV_IMG_DECLARE(c25);
LV_IMG_DECLARE(c26);
LV_IMG_DECLARE(c27);
LV_IMG_DECLARE(c28);
LV_IMG_DECLARE(c29);
LV_IMG_DECLARE(c30);
LV_IMG_DECLARE(c31);
LV_IMG_DECLARE(c32);
LV_IMG_DECLARE(c33);
LV_IMG_DECLARE(c34);
LV_IMG_DECLARE(c35);
LV_IMG_DECLARE(c36);
LV_IMG_DECLARE(c37);
LV_IMG_DECLARE(c38);
LV_IMG_DECLARE(c99);

lv_obj_t* main_obj;

lv_obj_t* status_bar;
lv_obj_t* time_weather;
lv_obj_t* app_launcher;

lv_obj_t* status_bar_cpu_pct;
lv_obj_t* status_bar_ram_pct;
lv_obj_t* status_bar_wifi_icon;
lv_obj_t* status_bar_time;
lv_obj_t* tw_time;
lv_obj_t* tw_weather;
lv_obj_t* tw_date;
lv_obj_t* tw_temp;

lv_obj_t* app1, *app1_icon;
lv_obj_t* app2, *app2_icon;
lv_obj_t* app3, *app3_icon;
lv_obj_t* app4, *app4_icon;
lv_obj_t* app5, *app5_icon;
lv_obj_t* app6, *app6_icon;
lv_obj_t* app7, *app7_icon;

lv_timer_t* timer_time_update;

lv_style_t style_btn;
lv_style_t style_btn_pr;

// 天气图标数组
const lv_img_dsc_t *weather_img_arr[] =
{
    &c0, &c1, &c2, &c3, &c4, &c5, &c6, &c7, &c8, &c9,
    &c10, &c11, &c12, &c13, &c14, &c15, &c16, &c17, &c18, &c19,
    &c20, &c21, &c22, &c23, &c24, &c25, &c26, &c27, &c28, &c29,
    &c30, &c31, &c32, &c33, &c34, &c35, &c36, &c37, &c38, &c99
};

//#define APP_USE_TEXT

static void btn_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* target = lv_event_get_target(e);

    if ((target == app1) || (target == app1_icon)) // 打开串口调试助手
    {
        lv_uart_create(lv_scr_act());
    }
    else if ((target == app7) || (target == app7_icon)) // 打开日历
    {
        //lv_calendar_create(lv_scr_act());
        lv_example_calendar();
    }
    else if (target == app3)
    {
    }
    else
    {

    }
}

#if defined(_WIN32) || defined(WIN32)
void get_system_time_str_win32(char* buf, size_t len)
{
    SYSTEMTIME st;
    GetLocalTime(&st);  // 获取本地时间（含毫秒）

    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}
#endif

typedef struct
{
    uint16_t year;
    uint8_t  month; // 1~12
    uint8_t  day;   // 1~31
    uint8_t  hour;
    uint8_t  min;
    uint8_t  sec;
    uint8_t  week;  // 0~6 (Sun~Sat)
} time_info_t;

// 返回值0：解析SNTP时间成功
// 返回值1：解析SNTP时间失败，指针空
// 返回值2：解析SNTP时间失败，字符串不符合需求
uint8_t parse_sntp(const char *str, time_info_t *time)
{
    char week_str[4] = {0};
    char mon_str[4]  = {0};
    int  day, hour, min, sec, year;
    static const char *month_table[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static const char *week_table[] = {  // SNTP的星期定义
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };

    if ((str == NULL) || (time == NULL))
    {
        return 1;
    }

    // 解析字符串
    if (sscanf(str, "%3s %3s %d %d:%d:%d %d",
               week_str, mon_str,
               &day, &hour, &min, &sec, &year) != 7)
    {
        return 2;
    }

    // 星期转换，1~7对应周一至周日
    for (uint8_t i = 0; i < 7; i++)
    {
        if (strcmp(week_str, week_table[i]) == 0)
        {
            time->week = (i ? i : 7);
            break;
        }
    }

    // 月份转换
    for (uint8_t i = 0; i < 12; i++)
    {
        if (strcmp(mon_str, month_table[i]) == 0)
        {
            time->month = i + 1;
            break;
        }
    }

    time->year = year;
    time->day  = day;
    time->hour = hour;
    time->min  = min;
    time->sec  = sec;
    return 0;   // 成功
}

uint8_t Win_GetWeekDay(uint16_t y, uint8_t m, uint8_t d)
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

// weekday: 1~7对应周一至周日
void FormatDate(char* out, uint8_t month, uint8_t date, uint8_t weekday)
{
    const char* week_str[] = {"禁止", "周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    sprintf(out, "%d月%d日 %s", month, date, week_str[weekday]);
}

void FormatTime(char* out, uint8_t hour, uint8_t minute)
{
    sprintf(out, "%02d:%02d", hour, minute);
}

// 间隔1s，读取一次RTC时间和日期，刷新显示
static void timer_time_update_cb(lv_timer_t* t)
{
    LV_UNUSED(t);
    static uint8_t  last_time_hour = 0;
    static uint8_t  last_time_minute = 0;
    static uint32_t last_time_year = 1970;
    static uint8_t  last_time_month = 1;
    static uint8_t  last_time_date = 1;

#if !defined(_WIN32) && !defined(WIN32) // STM32平台
    // 获取当前时间和日期
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    // 刷新时间到屏幕上
    if ((sTime.Minutes != last_time_minute) || (sTime.Hours != last_time_hour))
    {
        // 按周期请求更新天气
        if ((sTime.Minutes % WEATHER_REFRESH_PERIOD_MINUTES) == 0)
        {
            uint8_t wifi_action = WIFI_ACTION_GET_WEATHER;
            if (xQueueSend(wifi_action_queue, &wifi_action, QUEUE_SEND_MAX_DELAY) != pdTRUE)
            {
                // printf("队列发送失败\r\n");
                while(1);
            }
        }

        // 0点0分，请求同步SNTP时间
        if ((sTime.Hours == 0) && (sTime.Minutes == 0))
        {
            uint8_t wifi_action = WIFI_ACTION_GET_SNTPTIME;
            if (xQueueSend(wifi_action_queue, &wifi_action, QUEUE_SEND_MAX_DELAY) != pdTRUE)
            {
                // printf("队列发送失败\r\n");
                while(1);
            }
        }

        // 读取RTC时间，刷新显示到主界面
        char time_str[6];   // 5个字符 + '\0'
        FormatTime(time_str, sTime.Hours, sTime.Minutes);
        time_str[5] = '\0'; // 手动补上结束符
        lv_label_set_text(status_bar_time, time_str);
        lv_label_set_text(tw_time, time_str);
        last_time_minute = sTime.Minutes;
        last_time_hour = sTime.Hours;
        if ((sDate.Date != last_time_date) ||
            (sDate.Month != last_time_month) ||
            (sDate.Year + 2000 != last_time_year))
        {
            char date_str[32];
            FormatDate(date_str, sDate.Month, sDate.Date, sDate.WeekDay);
            lv_label_set_text(tw_date, date_str);
            last_time_date = sDate.Date;
            last_time_month = sDate.Month;
            last_time_year = sDate.Year + 2000;
        }
    }

    wifi_info_t wifi_info;
    time_info_t time_info;
    uint32_t text_len;
    uint32_t temp_len;
    uint8_t* weather_str;

    if (xQueueReceive(wifi_info_queue, &wifi_info, 0) == pdPASS)
    {
        if (wifi_info.status == WIFI_STATUS_OK)
        {
            switch (wifi_info.action_val)
            {
                case WIFI_ACTION_GET_WEATHER:
                    // 更新天气数据
                    // 拼接字符串形式为"小雨 16℃"，然后显示到屏幕上
                    text_len = strlen(wifi_info.weather_text);
                    temp_len = strlen(wifi_info.weather_temp);
                    weather_str = lv_mem_alloc(text_len + temp_len + 5); // 多留5个字节存放空格、℃符号和结束符
                    memcpy(weather_str, wifi_info.weather_text, text_len);
                    weather_str[text_len] = ' ';
                    memcpy(&weather_str[text_len + 1], wifi_info.weather_temp, temp_len);
                    weather_str[text_len + temp_len + 1] = 0xE2;
                    weather_str[text_len + temp_len + 2] = 0x84;
                    weather_str[text_len + temp_len + 3] = 0x83; // ℃ UTF-8编码
                    weather_str[text_len + temp_len + 4] = 0;
                    lv_label_set_text(tw_temp, (const char *)weather_str);
                    lv_mem_free(weather_str);

                    // 更新天气图标
                    uint8_t weather_code = atoi(wifi_info.weather_code);
                    if ((weather_code >= 0) && (weather_code <= 38)) // 0~38: 有效图标
                    {
                        lv_img_set_src(tw_weather, weather_img_arr[weather_code]);
                    }
                    else // 其他: 显示未知图标N/A
                    {
                        lv_img_set_src(tw_weather, weather_img_arr[39]);
                    }
                    break;
                case WIFI_ACTION_GET_SNTPTIME:
                    if (parse_sntp(wifi_info.sntp_time, &time_info) == 0)
                    {
                        // 校准RTC时间和日期
                        sTime.Hours = time_info.hour;
                        sTime.Minutes = time_info.min;
                        sTime.Seconds = time_info.sec;
                        sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
                        sTime.StoreOperation = RTC_STOREOPERATION_RESET;
                        if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
                        {
                            Error_Handler();
                        }
                        sDate.WeekDay = time_info.week;
                        sDate.Month = time_info.month;
                        sDate.Date = time_info.day;
                        sDate.Year = time_info.year - 2000;
                        if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
                        {
                            Error_Handler();
                        }
                    }
                    break;
                
                default:
                    break;
            }
        }
    }
#else // Windows平台
    char time_str[32];
    get_system_time_str_win32(time_str, sizeof(time_str));
    time_str[31] = 0;
    printf(time_str);
    printf("\n");
    //2025-11-07 23:14:43.170
    //int hour, minute;
    //sscanf(time_str, "%*d-%*d-%*d %d:%d", &hour, &minute);
    int hour = atoi(time_str + 11); // 速度比sscanf()快
    int minute = atoi(time_str + 14);

    if ((minute != last_time_minute) || (hour != last_time_hour))
    {
        printf("hour = %d, minute = %d\n", hour, minute);
        char timecpy[6];  // 5个字符 + '\0'
        strncpy(timecpy, time_str + 11, 5);  // 从第11个字符开始复制5个
        timecpy[5] = '\0';  // 手动补上结束符
        lv_label_set_text(status_bar_time, timecpy);
        lv_label_set_text(tw_time, timecpy);
        last_time_minute = minute;
        last_time_hour = hour;

        int year = atoi(time_str + 0);
        int month = atoi(time_str + 5);
        int date = atoi(time_str + 8);
        if ((last_time_date != date) || (last_time_month != month))
        {
            char date_str[32];
            FormatDate(date_str, month, date, Win_GetWeekDay(year, month, date));
            lv_label_set_text(tw_date, date_str);
            last_time_date = date;
            last_time_month = month;
        }
    }
#endif
}

void lv_main_create(lv_obj_t* parent)
{
    // 获取当前时间和日期
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    // 进入主界面立即读取RTC时间和日期，刷新显示
    char time_str[6];  // 5个字符 + '\0'
    FormatTime(time_str, sTime.Hours, sTime.Minutes);
    time_str[5] = '\0';  // 手动补上结束符
    char date_str[32];
    FormatDate(date_str, sDate.Month, sDate.Date, sDate.WeekDay);

    timer_time_update = lv_timer_create(timer_time_update_cb, 1000, NULL);

    main_obj = lv_obj_create(parent);
    lv_obj_set_size(main_obj, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_gap(main_obj, 0, 0);  // 去掉容器与子项的外边距
    lv_obj_set_style_pad_all(main_obj, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_pad_right(main_obj, 0, 0);
    lv_obj_set_style_pad_left(main_obj, 0, 0);

    status_bar = lv_obj_create(main_obj);
    lv_obj_set_style_pad_left(status_bar, 25, 0);
    lv_obj_set_style_pad_right(status_bar, 25, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    
    status_bar_cpu_pct = lv_label_create(status_bar);
    lv_obj_set_style_text_font(status_bar_cpu_pct, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(status_bar_cpu_pct, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(status_bar_cpu_pct, "CPU:27%");

    status_bar_ram_pct = lv_label_create(status_bar);
    lv_obj_set_style_text_font(status_bar_ram_pct, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(status_bar_ram_pct, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(status_bar_ram_pct, "RAM:27%");

    status_bar_wifi_icon = lv_label_create(status_bar);
    lv_obj_set_style_text_font(status_bar_wifi_icon, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(status_bar_wifi_icon, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(status_bar_wifi_icon, LV_SYMBOL_WIFI);

    status_bar_time = lv_label_create(status_bar);
    lv_obj_set_style_text_font(status_bar_time, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(status_bar_time, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(status_bar_time, time_str);

    lv_obj_set_style_radius(status_bar, 0, 0);  // 设置圆角为0，变成矩形
    //static const lv_coord_t status_grid_cols[] = { LV_GRID_FR(20), LV_GRID_FR(20), LV_GRID_FR(86), LV_GRID_FR(12), LV_GRID_FR(12), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t status_grid_cols[] = { LV_GRID_FR(13), LV_GRID_FR(13), LV_GRID_FR(58), LV_GRID_FR(8), LV_GRID_FR(8), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t status_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(status_bar, status_grid_cols, status_grid_rows);
    lv_obj_set_style_grid_row_align(status_bar, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    lv_obj_set_grid_cell(status_bar_cpu_pct, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(status_bar_ram_pct, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(status_bar_wifi_icon, LV_GRID_ALIGN_END, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(status_bar_time, LV_GRID_ALIGN_END, 4, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    // 天气布局 ===========================================================================================================
    time_weather = lv_obj_create(main_obj);
    lv_obj_set_style_pad_left(time_weather, 25, 0);
    lv_obj_set_style_pad_right(time_weather, 25, 0);
    lv_obj_clear_flag(time_weather, LV_OBJ_FLAG_SCROLLABLE);

    tw_time = lv_label_create(time_weather);
    lv_obj_set_style_text_font(tw_time, &ceshi_72_4, 0);
    lv_obj_set_style_text_color(tw_time, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(tw_time, time_str);

    tw_weather = lv_img_create(time_weather);
    lv_img_set_src(tw_weather, weather_img_arr[39]); // 默认显示未知图标N/A
    lv_obj_center(tw_weather);

    tw_date = lv_label_create(time_weather);
    lv_obj_set_style_text_font(tw_date, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(tw_date, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(tw_date, date_str);

    tw_temp = lv_label_create(time_weather);
    lv_obj_set_style_text_font(tw_temp, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(tw_temp, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(tw_temp, "晴 16℃");

    lv_obj_set_style_radius(time_weather, 0, 0);  // 设置圆角为0，变成矩形
    static const lv_coord_t time_weather_grid_cols[] = { LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t time_weather_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(time_weather, time_weather_grid_cols, time_weather_grid_rows);
    lv_obj_set_style_grid_row_align(time_weather, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间

    lv_obj_set_grid_cell(tw_time, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(tw_weather, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(tw_date, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);
    lv_obj_set_grid_cell(tw_temp, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 1, 1);

    // APP图标布局 ===========================================================================================================
    app_launcher = lv_obj_create(main_obj);
    lv_obj_set_flex_flow(app_launcher, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(app_launcher, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(app_launcher, LV_SCROLLBAR_MODE_OFF);

    lv_style_init(&style_btn);
    //lv_style_set_bg_opa(&style_btn, LV_OPA_50);
    lv_style_set_flex_flow(&style_btn, LV_FLEX_FLOW_COLUMN);
    lv_style_set_pad_top(&style_btn, 17);
    lv_style_set_pad_row(&style_btn, 10); // 图标和文字之间的垂直间距
    lv_style_set_layout(&style_btn, LV_LAYOUT_FLEX); /* 设置 Flex 布局 */
    lv_style_set_flex_main_place(&style_btn, LV_FLEX_ALIGN_START);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x6495ED)); //矢车菊的蓝色

    lv_style_init(&style_btn_pr);
    lv_style_set_bg_opa(&style_btn_pr, LV_OPA_COVER);
    lv_style_set_bg_color(&style_btn_pr, lv_color_hex(0x557FC8));

    //****************************************************************************
    app1 = lv_btn_create(app_launcher);
    lv_obj_set_size(app1, 200, 123);
    lv_obj_clear_flag(app1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(app1, &style_btn, 0);
    lv_obj_add_event_cb(app1, btn_event_cb, LV_EVENT_CLICKED, NULL);

    app1_icon = lv_obj_create(app1);
    lv_obj_set_size(app1_icon, 60, 60);
    //lv_obj_set_style_bg_color(app1_icon, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_set_style_pad_all(app1_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(app1_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(app1_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    lv_obj_t* app1_icon_label = lv_label_create(app1_icon);
    lv_obj_set_style_text_font(app1_icon_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app1_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(app1_icon_label);
    lv_label_set_text(app1_icon_label, "UART");
#else
    lv_obj_t* app1_icon_img = lv_img_create(app1_icon);
    lv_img_set_src(app1_icon_img, &uart);
    lv_obj_center(app1_icon_img);
#endif
    lv_obj_t* app1_label = lv_label_create(app1);
    lv_obj_set_style_text_font(app1_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app1_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(app1_label, "串口调试助手");

    //****************************************************************************
    app2 = lv_btn_create(app_launcher);
    lv_obj_set_size(app2, 200, 123);
    lv_obj_clear_flag(app2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(app2, &style_btn, 0);
    lv_obj_add_event_cb(app2, btn_event_cb, LV_EVENT_CLICKED, NULL);

    app2_icon = lv_obj_create(app2);
    lv_obj_set_size(app2_icon, 60, 60);
    //lv_obj_set_style_bg_color(app2_icon, lv_palette_main(LV_PALETTE_PINK), 0);
    lv_obj_set_style_pad_all(app2_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(app2_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(app2_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    lv_obj_t* app2_icon_label = lv_label_create(app2_icon);
    lv_obj_set_style_text_font(app2_icon_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app2_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(app2_icon_label);
    lv_label_set_text(app2_icon_label, "FILE");
#else
    lv_obj_t* app2_icon_img = lv_img_create(app2_icon);
    lv_img_set_src(app2_icon_img, &folder48);
    lv_obj_center(app2_icon_img);
#endif
    lv_obj_t* app2_label = lv_label_create(app2);
    lv_obj_set_style_text_font(app2_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app2_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(app2_label, "文件管理");

    //****************************************************************************

    app3 = lv_btn_create(app_launcher);
    lv_obj_set_size(app3, 200, 123);
    lv_obj_clear_flag(app3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(app3, &style_btn, 0);
    lv_obj_add_event_cb(app3, btn_event_cb, LV_EVENT_CLICKED, NULL);

    app3_icon = lv_obj_create(app3);
    lv_obj_set_size(app3_icon, 60, 60);
    //lv_obj_set_style_bg_color(app3_icon, lv_color_hex(ColorGold), 0);
    lv_obj_set_style_pad_all(app3_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(app3_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(app3_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    lv_obj_t* app3_icon_label = lv_label_create(app3_icon);
    lv_obj_set_style_text_font(app3_icon_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app3_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(app3_icon_label);
    lv_label_set_text(app3_icon_label, "SET");
#else
    lv_obj_t* app3_icon_img = lv_img_create(app3_icon);
    lv_img_set_src(app3_icon_img, &setting);
    lv_obj_center(app3_icon_img);
#endif
    lv_obj_t* app3_label = lv_label_create(app3);
    lv_obj_set_style_text_font(app3_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app3_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(app3_label, "设    置");
    //****************************************************************************

    app4 = lv_btn_create(app_launcher);
    lv_obj_set_size(app4, 200, 123);
    lv_obj_clear_flag(app4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(app4, &style_btn, 0);
    lv_obj_add_event_cb(app4, btn_event_cb, LV_EVENT_CLICKED, NULL);

    app4_icon = lv_obj_create(app4);
    lv_obj_set_size(app4_icon, 60, 60);
    //lv_obj_set_style_bg_color(app4_icon, lv_color_hex(ColorMediumPurple), 0);
    lv_obj_set_style_pad_all(app4_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(app4_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(app4_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    lv_obj_t* app4_icon_label = lv_label_create(app4_icon);
    lv_obj_set_style_text_font(app4_icon_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app4_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(app4_icon_label);
    lv_label_set_text(app4_icon_label, "MUSIC");
#else
    lv_obj_t* app4_icon_img = lv_img_create(app4_icon);
    lv_img_set_src(app4_icon_img, &music);
    lv_obj_center(app4_icon_img);
#endif
    lv_obj_t* app4_label = lv_label_create(app4);
    lv_obj_set_style_text_font(app4_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app4_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(app4_label, "音乐播放器");

    //****************************************************************************

    app5 = lv_btn_create(app_launcher);
    lv_obj_set_size(app5, 200, 123);
    lv_obj_clear_flag(app5, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(app5, &style_btn, 0);
    lv_obj_add_event_cb(app5, btn_event_cb, LV_EVENT_CLICKED, NULL);

    app5_icon = lv_obj_create(app5);
    lv_obj_set_size(app5_icon, 60, 60);
    //lv_obj_set_style_bg_color(app5_icon, lv_color_hex(ColorLightGray), 0);
    lv_obj_set_style_pad_all(app5_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(app5_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(app5_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    lv_obj_t* app5_icon_label = lv_label_create(app5_icon);
    lv_obj_set_style_text_font(app5_icon_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app5_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(app5_icon_label);
    lv_label_set_text(app5_icon_label, "DAP");
#else
    lv_obj_t* app5_icon_img = lv_img_create(app5_icon);
    lv_img_set_src(app5_icon_img, &debug);
    lv_obj_center(app5_icon_img);
#endif
    lv_obj_t* app5_label = lv_label_create(app5);
    lv_obj_set_style_text_font(app5_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app5_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(app5_label, "DAP Link");

    //****************************************************************************
    app6 = lv_btn_create(app_launcher);
    lv_obj_set_size(app6, 200, 123);
    lv_obj_clear_flag(app6, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(app6, &style_btn, 0);
    lv_obj_add_event_cb(app6, btn_event_cb, LV_EVENT_CLICKED, NULL);

    app6_icon = lv_obj_create(app6);
    lv_obj_set_size(app6_icon, 60, 60);
    //lv_obj_set_style_bg_color(app6_icon, lv_color_hex(ColorAuqamarin), 0);
    lv_obj_set_style_pad_all(app6_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(app6_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(app6_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    lv_obj_t* app6_icon_label = lv_label_create(app6_icon);
    lv_obj_set_style_text_font(app6_icon_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app6_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(app6_icon_label);
    lv_label_set_text(app6_icon_label, "WEA");
#else
    lv_obj_t* app6_icon_img = lv_img_create(app6_icon);
    lv_img_set_src(app6_icon_img, &weather);
    lv_obj_center(app6_icon_img);
#endif
    lv_obj_t* app6_label = lv_label_create(app6);
    lv_obj_set_style_text_font(app6_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app6_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(app6_label, "天    气");

    //****************************************************************************
    app7 = lv_btn_create(app_launcher);
    lv_obj_set_size(app7, 200, 123);
    lv_obj_clear_flag(app7, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(app7, &style_btn, 0);
    lv_obj_add_event_cb(app7, btn_event_cb, LV_EVENT_CLICKED, NULL);

    app7_icon = lv_obj_create(app7);
    lv_obj_set_size(app7_icon, 60, 60);
    //lv_obj_set_style_bg_color(app7_icon, lv_color_hex(ColorAuqamarin), 0);
    lv_obj_set_style_pad_all(app7_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(app7_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(app7_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    lv_obj_t* app7_icon_label = lv_label_create(app7_icon);
    lv_obj_set_style_text_font(app7_icon_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app7_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(app7_icon_label);
    lv_label_set_text(app7_icon_label, "CAL");
#else
    lv_obj_t* app7_icon_img = lv_img_create(app7_icon);
    lv_img_set_src(app7_icon_img, &calendar);
    lv_obj_center(app7_icon_img);
#endif
    lv_obj_t* app7_label = lv_label_create(app7);
    lv_obj_set_style_text_font(app7_label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(app7_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(app7_label, "日    历");
    //****************************************************************************


    // 整体布局 ===========================================================================================================
    //lv_obj_set_style_border_width(main_obj, 0, 0);
    //lv_obj_set_style_pad_all(main_obj, 0, 0);
    static const lv_coord_t main_grid_cols[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t main_grid_rows[] = { LV_GRID_FR(8), LV_GRID_FR(32), LV_GRID_FR(60), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(main_obj, main_grid_cols, main_grid_rows);
    lv_obj_set_style_grid_row_align(main_obj, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    lv_obj_set_grid_cell(status_bar, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(time_weather, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(app_launcher, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);

}
