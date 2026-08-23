#include <stdio.h>
#include <stdlib.h>
#include "lvgl/lvgl.h"
#include "app_main.h"
#include "app_calendar.h"

#if !defined(_WIN32) && !defined(WIN32)
#include "rtc.h"
#endif

static lv_obj_t* background;
static lv_obj_t* title_bar;
static lv_obj_t* calendar_obj;
static lv_obj_t* title;
static lv_obj_t* btn_minimize;
static lv_obj_t* btn_close;


static void btn_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);

    if (btn == btn_close)
    {
        lv_obj_del(background);
    }
}


/**
 * @brief  例
 * @param  无
 * @return 无
 */
void lv_example_calendar(void)
{
    //声明字体
    LV_FONT_DECLARE(ceshi_24_4);

    background = lv_obj_create(lv_scr_act());
    lv_obj_set_size(background, lv_pct(100), lv_pct(92));
    lv_obj_set_style_border_width(background, 0, 0);
    lv_obj_set_style_pad_all(background, 0, 0);
    lv_obj_set_y(background, lv_pct(8));

    // 标题栏 ===========================================================================================================
    title_bar = lv_obj_create(background);
    lv_obj_set_style_pad_gap(title_bar, 0, 0);   // 去掉容器与子项的外边距
    //lv_obj_set_style_pad_all(title_bar, 0, 0); // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_pad_right(title_bar, 0, 0);
    lv_obj_set_style_pad_left(title_bar, 25, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_bar, 0, 0);  // 设置圆角为0，变成矩形
    static const lv_coord_t top_grid_cols[] = { LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t top_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(title_bar, top_grid_cols, top_grid_rows);
    lv_obj_set_style_grid_row_align(title_bar, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    title = lv_label_create(title_bar);
    lv_obj_set_style_text_font(title, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_PINK), 0);
    lv_label_set_text(title, "日历");
    //lv_obj_set_height(title_bar, 50);

    btn_minimize = lv_btn_create(title_bar);
    lv_obj_set_style_bg_opa(btn_minimize, LV_OPA_TRANSP, LV_PART_MAIN);      // 背景透明
    lv_obj_set_style_border_opa(btn_minimize, LV_OPA_TRANSP, LV_PART_MAIN);  // 边框透明
    lv_obj_set_style_shadow_opa(btn_minimize, LV_OPA_TRANSP, LV_PART_MAIN);  // 阴影透明
    lv_obj_set_style_radius(btn_minimize, 0, LV_PART_MAIN);                  // 直角
    lv_obj_set_style_bg_opa(btn_minimize, LV_OPA_COVER, LV_STATE_PRESSED);   // 设置按下状态：灰色背景
    lv_obj_set_style_bg_color(btn_minimize, lv_color_hex(0xAAAAAA), LV_STATE_PRESSED);
    lv_obj_t *label = lv_label_create(btn_minimize);
    lv_obj_set_style_text_color(label, lv_color_hex(0), 0); // 黑色字体颜色
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_label_set_text(label, LV_SYMBOL_MINUS);
    lv_obj_center(label);

    btn_close = lv_btn_create(title_bar);
    lv_obj_set_style_bg_opa(btn_close, LV_OPA_TRANSP, LV_PART_MAIN);      // 背景透明
    lv_obj_set_style_border_opa(btn_close, LV_OPA_TRANSP, LV_PART_MAIN);  // 边框透明
    lv_obj_set_style_shadow_opa(btn_close, LV_OPA_TRANSP, LV_PART_MAIN);  // 阴影透明
    lv_obj_set_style_radius(btn_close, 0, LV_PART_MAIN);                  // 直角
    lv_obj_set_style_bg_opa(btn_close, LV_OPA_COVER, LV_STATE_PRESSED);   // 设置按下状态：红色背景
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0xFF0000), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_close, btn_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_close);
    lv_obj_set_style_text_color(label, lv_color_hex(0), 0); // 黑色字体颜色
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_label_set_text(label, LV_SYMBOL_CLOSE);
    lv_obj_center(label);

    lv_obj_set_grid_cell(title, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(btn_minimize, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(btn_close, LV_GRID_ALIGN_END, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    // 读取RTC获取当前日期
#if !defined(_WIN32) && !defined(WIN32) // STM32平台
    RTC_DateTypeDef sDate;
    RTC_TimeTypeDef sTime;
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    // 定义并初始化日历
    calendar_obj = lv_calendar_create(background);
    lv_obj_set_style_text_font(calendar_obj, &ceshi_24_4, 0);
    lv_calendar_set_today_date(calendar_obj, sDate.Year + 2000, sDate.Month, sDate.Date); // 设置日历的日期
    lv_calendar_set_showed_date(calendar_obj, sDate.Year + 2000, sDate.Month); // 设置日历显示的月份
    lv_calendar_header_dropdown_create(calendar_obj); // 设置日历头
    lv_obj_update_layout(calendar_obj); // 更新日历参数
#elif
    // 定义并初始化日历
    calendar_obj = lv_calendar_create(background);
    lv_obj_set_style_text_font(calendar_obj, &ceshi_24_4, 0);
    lv_calendar_set_today_date(calendar_obj, 2025, 11, 8); // 设置日历的日期
    lv_calendar_set_showed_date(calendar_obj, 2025, 11); // 设置日历显示的月份
    lv_calendar_header_dropdown_create(calendar_obj); // 设置日历头
    lv_obj_update_layout(calendar_obj); // 更新日历参数
#endif

    /*Highlight a few days*/
    //static lv_calendar_date_t highlighted_days[1];  /*Only its pointer will be saved so should be static*/
    //highlighted_days[0].year = 2025;
    //highlighted_days[0].month = 11;
    //highlighted_days[0].day = 8;
    //lv_calendar_set_highlighted_dates(calendar, highlighted_days, 1);

    // 整体布局 ===========================================================================================================
    lv_obj_set_style_pad_gap(background, 0, 0); // 去掉容器与子项的外边距
    static const lv_coord_t bg_grid_cols[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t bg_grid_rows[] = { LV_GRID_FR(10), LV_GRID_FR(90), LV_GRID_TEMPLATE_LAST };
    //static const lv_coord_t console_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(background, bg_grid_cols, bg_grid_rows);
    lv_obj_set_style_grid_row_align(background, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    lv_obj_set_grid_cell(title_bar, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(calendar_obj, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
}



