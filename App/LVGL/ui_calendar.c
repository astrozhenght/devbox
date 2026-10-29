#include "ui_fonts.h"
#include "lvgl.h"
#include "wifi_service_port.h"

typedef struct
{
    lv_obj_t* container;           // 一级容器
    lv_obj_t* header;              // 二级标题栏
    lv_obj_t* calendar;            // 二级日历
    lv_obj_t* header_title_label;  // 三级标题栏-标题
    lv_obj_t* header_minimize_btn; // 三级标题栏-最小化按钮
    lv_obj_t* header_close_btn;    // 三级标题栏-关闭按钮
} ui_calendar_context_t;

static ui_calendar_context_t calendar_context = {0};
static void calendar_create_container(lv_obj_t* parent);
static void calendar_create_title_bar(void);
static void calendar_create_calendar(void);
static void calendar_apply_layout(void);

static void ui_calendar_btn_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);

    if (btn == calendar_context.header_close_btn)
    {
        lv_obj_del(calendar_context.container);
        memset(&calendar_context, 0, sizeof(calendar_context));
    }
}

static void calendar_create_container(lv_obj_t* parent)
{
    calendar_context.container = lv_obj_create(parent);
    lv_obj_set_size(calendar_context.container, lv_pct(100), lv_pct(92));
    lv_obj_set_style_border_width(calendar_context.container, 0, 0);
    lv_obj_set_style_pad_all(calendar_context.container, 0, 0);
    lv_obj_set_y(calendar_context.container, lv_pct(8));
}

// 创建日历App-标题栏
static void calendar_create_title_bar(void)
{
    calendar_context.header = lv_obj_create(calendar_context.container);
    lv_obj_set_style_pad_gap(calendar_context.header, 0, 0);   // 去掉容器与子项的外边距
    //lv_obj_set_style_pad_all(calendar_context.header, 0, 0); // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_pad_right(calendar_context.header, 0, 0);
    lv_obj_set_style_pad_left(calendar_context.header, 25, 0);
    lv_obj_clear_flag(calendar_context.header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(calendar_context.header, 0, 0);  // 设置圆角为0，变成矩形
    static const lv_coord_t top_grid_cols[] = { LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t top_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(calendar_context.header, top_grid_cols, top_grid_rows);
    lv_obj_set_style_grid_row_align(calendar_context.header, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    calendar_context.header_title_label = lv_label_create(calendar_context.header);
    lv_obj_set_style_text_font(calendar_context.header_title_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(calendar_context.header_title_label, lv_palette_main(LV_PALETTE_PINK), 0);
    lv_label_set_text(calendar_context.header_title_label, "日历");
    //lv_obj_set_height(calendar_context.header, 50);

    calendar_context.header_minimize_btn = lv_btn_create(calendar_context.header);
    lv_obj_set_style_bg_opa(calendar_context.header_minimize_btn, LV_OPA_TRANSP, LV_PART_MAIN);      // 背景透明
    lv_obj_set_style_border_opa(calendar_context.header_minimize_btn, LV_OPA_TRANSP, LV_PART_MAIN);  // 边框透明
    lv_obj_set_style_shadow_opa(calendar_context.header_minimize_btn, LV_OPA_TRANSP, LV_PART_MAIN);  // 阴影透明
    lv_obj_set_style_radius(calendar_context.header_minimize_btn, 0, LV_PART_MAIN);                  // 直角
    lv_obj_set_style_bg_opa(calendar_context.header_minimize_btn, LV_OPA_COVER, LV_STATE_PRESSED);   // 设置按下状态：灰色背景
    lv_obj_set_style_bg_color(calendar_context.header_minimize_btn, lv_color_hex(0xAAAAAA), LV_STATE_PRESSED);
    lv_obj_t *label = lv_label_create(calendar_context.header_minimize_btn);
    lv_obj_set_style_text_color(label, lv_color_hex(0), 0); // 黑色字体颜色
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_label_set_text(label, LV_SYMBOL_MINUS);
    lv_obj_center(label);

    calendar_context.header_close_btn = lv_btn_create(calendar_context.header);
    lv_obj_set_style_bg_opa(calendar_context.header_close_btn, LV_OPA_TRANSP, LV_PART_MAIN);      // 背景透明
    lv_obj_set_style_border_opa(calendar_context.header_close_btn, LV_OPA_TRANSP, LV_PART_MAIN);  // 边框透明
    lv_obj_set_style_shadow_opa(calendar_context.header_close_btn, LV_OPA_TRANSP, LV_PART_MAIN);  // 阴影透明
    lv_obj_set_style_radius(calendar_context.header_close_btn, 0, LV_PART_MAIN);                  // 直角
    lv_obj_set_style_bg_opa(calendar_context.header_close_btn, LV_OPA_COVER, LV_STATE_PRESSED);   // 设置按下状态：红色背景
    lv_obj_set_style_bg_color(calendar_context.header_close_btn, lv_color_hex(0xFF0000), LV_STATE_PRESSED);
    lv_obj_add_event_cb(calendar_context.header_close_btn, ui_calendar_btn_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(calendar_context.header_close_btn);
    lv_obj_set_style_text_color(label, lv_color_hex(0), 0); // 黑色字体颜色
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_label_set_text(label, LV_SYMBOL_CLOSE);
    lv_obj_center(label);

    lv_obj_set_grid_cell(calendar_context.header_title_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(calendar_context.header_minimize_btn, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(calendar_context.header_close_btn, LV_GRID_ALIGN_END, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
}

// 创建日历App-日历主体
static void calendar_create_calendar(void)
{
    time_service_datetime_t datetime = {0};
    time_service_get_datetime(&datetime);

    // 定义并初始化日历
    calendar_context.calendar = lv_calendar_create(calendar_context.container);
    lv_obj_set_style_text_font(calendar_context.calendar, &ui_font_chinese_24_4, 0);
    lv_calendar_set_today_date(calendar_context.calendar, datetime.year, datetime.month, datetime.day); // 设置日历的日期
    lv_calendar_set_showed_date(calendar_context.calendar, datetime.year, datetime.month); // 设置日历显示的月份
    lv_calendar_header_dropdown_create(calendar_context.calendar); // 设置日历头
    lv_obj_update_layout(calendar_context.calendar); // 更新日历参数

    /*Highlight a few days*/
    //static lv_calendar_date_t highlighted_days[1];  /*Only its pointer will be saved so should be static*/
    //highlighted_days[0].year = 2025;
    //highlighted_days[0].month = 11;
    //highlighted_days[0].day = 8;
    //lv_calendar_set_highlighted_dates(calendar, highlighted_days, 1);
}

static void calendar_apply_layout(void)
{
    lv_obj_set_style_pad_gap(calendar_context.container, 0, 0); // 去掉容器与子项的外边距
    static const lv_coord_t bg_grid_cols[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t bg_grid_rows[] = { LV_GRID_FR(10), LV_GRID_FR(90), LV_GRID_TEMPLATE_LAST };
    //static const lv_coord_t console_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(calendar_context.container, bg_grid_cols, bg_grid_rows);
    lv_obj_set_style_grid_row_align(calendar_context.container, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    lv_obj_set_grid_cell(calendar_context.header, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(calendar_context.calendar, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
}

void ui_calendar_create(lv_obj_t* parent)
{
    calendar_create_container(parent);
    calendar_create_title_bar();
    calendar_create_calendar();
    calendar_apply_layout();
}
