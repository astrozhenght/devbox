#include "ui_calendar.h"
#include "ui_fonts.h"
#include "lvgl.h"
#include "wifi_service_port.h"
#include "lunar_calendar.h"
#include <string.h>

#define CALENDAR_COLOR_BG      0xF5F7FA
#define CALENDAR_COLOR_TEXT    0x243247
#define CALENDAR_COLOR_MUTED   0x69788B
#define CALENDAR_COLOR_BORDER  0xE4EAF1

typedef struct
{
    lv_obj_t* container;           // 一级容器
    lv_obj_t* header;              // 二级标题栏
    lv_obj_t* calendar;            // 二级日历
    lv_obj_t* month_header;        // 年月选择栏
    lv_obj_t* previous_month_btn;  // 上一月
    lv_obj_t* next_month_btn;      // 下一月
    lv_obj_t* year_dropdown;       // 年份选择，2099～1970
    lv_obj_t* month_dropdown;      // 月份选择，1～12
    lv_obj_t* header_title_label;  // 三级标题栏-标题
    lv_obj_t* header_home_btn;     // 三级标题栏-返回主页按钮
    lv_obj_t* today_btn;           // 重新读取系统日期并回到今天
    lv_obj_t* gregorian_label;     // 底部左侧公历日期
    lv_obj_t* lunar_label;         // 底部右侧农历日期
    lv_calendar_date_t selected;   // 高亮日期，存储期覆盖日历控件的使用期
} ui_calendar_context_t;

static ui_calendar_context_t calendar_context = {0};
static void calendar_create_container(lv_obj_t* parent);
static void calendar_create_title_bar(void);
static void calendar_create_calendar(void);
static void calendar_apply_layout(void);
static void calendar_shift_month(lv_dir_t direction);

static void calendar_show_time_error(void)
{
    lv_label_set_text(calendar_context.gregorian_label, "未获取到有效系统日期");
    lv_label_set_text(calendar_context.lunar_label, "请选择日期");
}

static void calendar_show_lunar(const lv_calendar_date_t *date)
{
    static const char *months[] = {"正月","二月","三月","四月","五月","六月",
                                   "七月","八月","九月","十月","冬月","腊月"};
    static const char *days[] = {"初一","初二","初三","初四","初五","初六","初七","初八","初九","初十",
        "十一","十二","十三","十四","十五","十六","十七","十八","十九","二十",
        "廿一","廿二","廿三","廿四","廿五","廿六","廿七","廿八","廿九","三十"};
    static const char *stems[] = {"甲","乙","丙","丁","戊","己","庚","辛","壬","癸"};
    static const char *branches[] = {"子","丑","寅","卯","辰","巳","午","未","申","酉","戌","亥"};
    lunar_calendar_date_t lunar;
    lv_obj_set_style_text_color(calendar_context.gregorian_label, lv_color_hex(CALENDAR_COLOR_MUTED), 0);
    lv_label_set_text_fmt(calendar_context.gregorian_label, "公历 %04u-%02u-%02u",
        date->year, date->month, date->day);
    if (!lunar_calendar_convert(date->year, date->month, date->day, &lunar))
    {
        lv_label_set_text(calendar_context.lunar_label, "农历支持1970-2099年的有效日期");
        return;
    }
    calendar_context.selected = *date;
    lv_calendar_set_highlighted_dates(calendar_context.calendar, &calendar_context.selected, 1);
    lv_label_set_text_fmt(calendar_context.lunar_label, "农历 %s%s年 %s%s%s",
        stems[(lunar.year - 4U) % 10U],
        branches[(lunar.year - 4U) % 12U], lunar.is_leap_month ? "闰" : "",
        months[lunar.month - 1U], days[lunar.day - 1U]);
}

static void calendar_date_changed(lv_event_t* event)
{
    (void)event;
    lv_calendar_date_t date;
    if (lv_calendar_get_pressed_date(calendar_context.calendar, &date) == LV_RES_OK)
        calendar_show_lunar(&date);
}

static void calendar_update_month_controls(void)
{
    const lv_calendar_date_t *date = lv_calendar_get_showed_date(calendar_context.calendar);
    lv_dropdown_set_selected(calendar_context.year_dropdown, LUNAR_CALENDAR_MAX_YEAR - date->year);
    lv_dropdown_set_selected(calendar_context.month_dropdown, date->month - 1U);
    if (date->year == LUNAR_CALENDAR_MIN_YEAR && date->month == 1U)
        lv_obj_add_state(calendar_context.previous_month_btn, LV_STATE_DISABLED);
    else lv_obj_clear_state(calendar_context.previous_month_btn, LV_STATE_DISABLED);
    if (date->year == LUNAR_CALENDAR_MAX_YEAR && date->month == 12U)
        lv_obj_add_state(calendar_context.next_month_btn, LV_STATE_DISABLED);
    else lv_obj_clear_state(calendar_context.next_month_btn, LV_STATE_DISABLED);
}

/* LVGL在月份变化后通知日历头；此时同步显示该公历月份初一的农历。 */
static void calendar_month_changed(lv_event_t* event)
{
    (void)event;
    calendar_update_month_controls();
    lv_calendar_date_t date = *lv_calendar_get_showed_date(calendar_context.calendar);
    date.day = 1;
    calendar_show_lunar(&date);
}

static void calendar_dropdown_changed(lv_event_t* event)
{
    (void)event;
    uint16_t year = LUNAR_CALENDAR_MAX_YEAR - lv_dropdown_get_selected(calendar_context.year_dropdown);
    uint8_t month = lv_dropdown_get_selected(calendar_context.month_dropdown) + 1U;
    lv_calendar_set_showed_date(calendar_context.calendar, year, month);
}

static void calendar_month_button_cb(lv_event_t* event)
{
    calendar_shift_month(lv_event_get_target(event) == calendar_context.previous_month_btn ?
        LV_DIR_RIGHT : LV_DIR_LEFT);
}

static lv_obj_t* calendar_create_month_button(const char *symbol)
{
    lv_obj_t *button = lv_btn_create(calendar_context.month_header);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 38, 38);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xEAF0F7), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(CALENDAR_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(button, &lv_font_montserrat_22, 0);
    lv_obj_set_style_bg_color(button, lv_palette_main(LV_PALETTE_BLUE), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xEDF1F5), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, lv_color_hex(0xA5AFBC), LV_STATE_DISABLED);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, calendar_month_button_cb, LV_EVENT_CLICKED, NULL);
    return button;
}

static void calendar_create_month_header(void)
{
    calendar_context.month_header = lv_obj_create(calendar_context.calendar);
    lv_obj_move_to_index(calendar_context.month_header, 0);
    lv_obj_set_size(calendar_context.month_header, lv_pct(100), 50);
    lv_obj_set_flex_flow(calendar_context.month_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_hor(calendar_context.month_header, 12, 0);
    lv_obj_set_style_pad_top(calendar_context.month_header, 6, 0);
    lv_obj_set_style_pad_bottom(calendar_context.month_header, 6, 0);
    lv_obj_set_style_pad_gap(calendar_context.month_header, 8, 0);
    lv_obj_set_style_bg_opa(calendar_context.month_header, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(calendar_context.month_header, lv_color_hex(0xF0F4F8), 0);
    lv_obj_set_style_border_width(calendar_context.month_header, 1, 0);
    lv_obj_set_style_border_side(calendar_context.month_header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(calendar_context.month_header, lv_color_hex(CALENDAR_COLOR_BORDER), 0);
    lv_obj_clear_flag(calendar_context.month_header, LV_OBJ_FLAG_SCROLLABLE);
    calendar_context.previous_month_btn = calendar_create_month_button(LV_SYMBOL_LEFT);

    /* 下拉框复制选项，局部缓冲区无需在创建结束后保留。 */
    char year_options[(LUNAR_CALENDAR_MAX_YEAR - LUNAR_CALENDAR_MIN_YEAR + 1U) * 5U];
    unsigned offset = 0;
    for (unsigned year = LUNAR_CALENDAR_MAX_YEAR; year >= LUNAR_CALENDAR_MIN_YEAR; year--)
        offset += lv_snprintf(year_options + offset, sizeof(year_options) - offset,
            "%04u%s", year, year == LUNAR_CALENDAR_MIN_YEAR ? "" : "\n");
    calendar_context.year_dropdown = lv_dropdown_create(calendar_context.month_header);
    lv_dropdown_set_options(calendar_context.year_dropdown, year_options);
    lv_obj_set_flex_grow(calendar_context.year_dropdown, 1);
    calendar_context.month_dropdown = lv_dropdown_create(calendar_context.month_header);
    lv_dropdown_set_options_static(calendar_context.month_dropdown,
        "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12");
    lv_obj_set_flex_grow(calendar_context.month_dropdown, 1);
    calendar_context.next_month_btn = calendar_create_month_button(LV_SYMBOL_RIGHT);
    lv_obj_t *dropdowns[] = {calendar_context.year_dropdown, calendar_context.month_dropdown};
    for (unsigned i = 0; i < 2; i++)
    {
        lv_obj_set_size(dropdowns[i], 0, 38);
        lv_obj_set_style_text_font(dropdowns[i], &ui_font_chinese_24_4, 0);
        lv_obj_set_style_text_color(dropdowns[i], lv_color_hex(CALENDAR_COLOR_TEXT), 0);
        lv_obj_set_style_pad_ver(dropdowns[i], 3, 0);
        lv_obj_set_style_radius(dropdowns[i], 6, 0);
        lv_obj_set_style_border_width(dropdowns[i], 1, 0);
        lv_obj_set_style_border_color(dropdowns[i], lv_color_hex(CALENDAR_COLOR_BORDER), 0);
        lv_obj_set_style_bg_color(dropdowns[i], lv_color_white(), 0);
        lv_obj_set_style_text_font(lv_dropdown_get_list(dropdowns[i]), &ui_font_chinese_24_4, 0);
    }

    calendar_update_month_controls();
    lv_obj_add_event_cb(calendar_context.year_dropdown, calendar_dropdown_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(calendar_context.month_dropdown, calendar_dropdown_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(calendar_context.month_header, calendar_month_changed, LV_EVENT_VALUE_CHANGED, NULL);
}

/* 箭头和手势共用月份推进及边界处理，显示更新统一通知年月栏。 */
static void calendar_shift_month(lv_dir_t direction)
{
    lv_calendar_date_t date = *lv_calendar_get_showed_date(calendar_context.calendar);
    if (direction == LV_DIR_LEFT)
    {
        if (date.month == 12U)
        {
            if (date.year == LUNAR_CALENDAR_MAX_YEAR) return;
            date.year++;
            date.month = 1U;
        }
        else date.month++;
    }
    else
    {
        if (date.month == 1U)
        {
            if (date.year == LUNAR_CALENDAR_MIN_YEAR) return;
            date.year--;
            date.month = 12U;
        }
        else date.month--;
    }
    lv_calendar_set_showed_date(calendar_context.calendar, date.year, date.month);
}

static void calendar_gesture_cb(lv_event_t* event)
{
    lv_indev_t *input = lv_event_get_param(event);
    lv_dir_t direction = lv_indev_get_gesture_dir(input);
    if (direction != LV_DIR_LEFT && direction != LV_DIR_RIGHT) return;

    /* 消费此次滑动，松手时不再触发日期点击；边界处也遵守此规则。 */
    lv_indev_wait_release(input);
    lv_event_stop_bubbling(event);
    calendar_shift_month(direction);
}

static void calendar_deleted(lv_event_t* event)
{
    if (lv_event_get_target(event) == calendar_context.container)
        memset(&calendar_context, 0, sizeof(calendar_context));
}

static void ui_calendar_btn_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);

    if (btn == calendar_context.header_home_btn)
    {
        lv_obj_del(calendar_context.container);
    }
    else if (btn == calendar_context.today_btn)
    {
        time_service_datetime_t datetime = {0};
        lunar_calendar_date_t lunar;
        if (!time_service_get_datetime(&datetime) ||
            !lunar_calendar_convert(datetime.year, datetime.month, datetime.day, &lunar))
        {
            calendar_show_time_error();
            return;
        }
        lv_calendar_set_today_date(calendar_context.calendar, datetime.year, datetime.month, datetime.day);
        lv_calendar_set_showed_date(calendar_context.calendar, datetime.year, datetime.month);
        lv_calendar_date_t date = {datetime.year, datetime.month, datetime.day};
        calendar_show_lunar(&date);
    }
}

static lv_obj_t* calendar_create_title_button(const char *text, lv_palette_t pressed_color)
{
    lv_obj_t *button = lv_btn_create(calendar_context.header);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 112, 38);
    lv_obj_set_style_pad_all(button, 3, 0);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(CALENDAR_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_white(), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(CALENDAR_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(button, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_bg_color(button, lv_palette_main(pressed_color), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, ui_calendar_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static void calendar_create_container(lv_obj_t* parent)
{
    calendar_context.container = lv_obj_create(parent);
    lv_obj_remove_style_all(calendar_context.container);
    lv_obj_set_size(calendar_context.container, lv_pct(100), lv_pct(92));
    lv_obj_set_style_border_width(calendar_context.container, 0, 0);
    lv_obj_set_style_pad_all(calendar_context.container, 8, 0);
    lv_obj_set_style_bg_opa(calendar_context.container, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(calendar_context.container, lv_color_hex(CALENDAR_COLOR_BG), 0);
    lv_obj_set_style_text_font(calendar_context.container, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(calendar_context.container, lv_color_hex(CALENDAR_COLOR_TEXT), 0);
    lv_obj_set_y(calendar_context.container, lv_pct(8));
    lv_obj_clear_flag(calendar_context.container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(calendar_context.container, calendar_deleted, LV_EVENT_DELETE, NULL);
}

// 创建日历App-标题栏
static void calendar_create_title_bar(void)
{
    calendar_context.header = lv_obj_create(calendar_context.container);
    lv_obj_remove_style_all(calendar_context.header);
    lv_obj_set_style_pad_all(calendar_context.header, 0, 0);
    lv_obj_set_style_pad_gap(calendar_context.header, 8, 0);
    lv_obj_set_style_pad_left(calendar_context.header, 10, 0);
    lv_obj_set_style_border_width(calendar_context.header, 0, 0);
    lv_obj_clear_flag(calendar_context.header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(calendar_context.header, 0, 0);  // 设置圆角为0，变成矩形
    static const lv_coord_t top_grid_cols[] = { LV_GRID_CONTENT, LV_GRID_FR(1), 112, 112, LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t top_grid_rows[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(calendar_context.header, top_grid_cols, top_grid_rows);
    lv_obj_set_style_grid_row_align(calendar_context.header, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    calendar_context.header_title_label = lv_label_create(calendar_context.header);
    lv_obj_set_style_text_font(calendar_context.header_title_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(calendar_context.header_title_label, lv_palette_main(LV_PALETTE_PINK), 0);
    lv_label_set_text(calendar_context.header_title_label, "日历");
    calendar_context.today_btn = calendar_create_title_button("回到今天", LV_PALETTE_BLUE);
    lv_obj_set_style_bg_color(calendar_context.today_btn, lv_color_hex(0xE6F0FF), 0);
    lv_obj_set_style_text_color(calendar_context.today_btn, lv_color_hex(0x2767B2), 0);
    calendar_context.header_home_btn = calendar_create_title_button("返回主页", LV_PALETTE_RED);

    lv_obj_set_grid_cell(calendar_context.header_title_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(calendar_context.today_btn, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(calendar_context.header_home_btn, LV_GRID_ALIGN_END, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
}

static void calendar_draw_weekdays(lv_event_t* event)
{
    lv_obj_draw_part_dsc_t *part = lv_event_get_draw_part_dsc(event);
    if (part->part == LV_PART_ITEMS && part->id < 7U)
        part->label_dsc->color = lv_color_hex(part->id >= 5U ? 0xD94757 : CALENDAR_COLOR_MUTED);
}

// 创建日历App-日历主体
static void calendar_create_calendar(void)
{
    time_service_datetime_t datetime = {0};
    bool have_time = time_service_get_datetime(&datetime);
    lunar_calendar_date_t lunar;
    have_time = have_time && lunar_calendar_convert(datetime.year, datetime.month, datetime.day, &lunar);

    // 定义并初始化日历
    calendar_context.calendar = lv_calendar_create(calendar_context.container);
    lv_obj_set_style_bg_color(calendar_context.calendar, lv_color_white(), 0);
    lv_obj_set_style_border_width(calendar_context.calendar, 1, 0);
    lv_obj_set_style_border_color(calendar_context.calendar, lv_color_hex(CALENDAR_COLOR_BORDER), 0);
    lv_obj_set_style_radius(calendar_context.calendar, 8, 0);
    lv_obj_set_style_pad_all(calendar_context.calendar, 0, 0);
    lv_obj_set_style_pad_gap(calendar_context.calendar, 0, 0);
    lv_obj_set_style_text_font(calendar_context.calendar, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_font(lv_calendar_get_btnmatrix(calendar_context.calendar), &ui_font_chinese_24_4, LV_PART_ITEMS);
    static const char *weekdays[] = {"一","二","三","四","五","六","日"};
    lv_calendar_set_day_names(calendar_context.calendar, weekdays);
    if (have_time)
    {
        lv_calendar_set_today_date(calendar_context.calendar, datetime.year, datetime.month, datetime.day);
        lv_calendar_set_showed_date(calendar_context.calendar, datetime.year, datetime.month);
    }
    calendar_create_month_header();
    lv_obj_clear_flag(calendar_context.calendar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *date_grid = lv_calendar_get_btnmatrix(calendar_context.calendar);
    lv_obj_set_style_bg_color(date_grid, lv_color_white(), 0);
    lv_obj_set_style_border_width(date_grid, 0, 0);
    lv_obj_set_style_pad_hor(date_grid, 10, 0);
    lv_obj_set_style_pad_ver(date_grid, 4, 0);
    lv_obj_set_style_pad_row(date_grid, 2, 0);
    lv_obj_set_style_pad_column(date_grid, 6, 0);
    lv_obj_set_style_border_width(date_grid, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(date_grid, 6, LV_PART_ITEMS);
    lv_obj_set_style_text_color(date_grid, lv_color_hex(CALENDAR_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_add_event_cb(date_grid, calendar_draw_weekdays, LV_EVENT_DRAW_PART_BEGIN, NULL);
    lv_obj_clear_flag(date_grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE |
        LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_event_cb(date_grid, calendar_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(calendar_context.calendar, calendar_date_changed, LV_EVENT_VALUE_CHANGED, NULL);

    calendar_context.gregorian_label = lv_label_create(calendar_context.container);
    calendar_context.lunar_label = lv_label_create(calendar_context.container);
    lv_obj_t *date_labels[] = {calendar_context.gregorian_label, calendar_context.lunar_label};
    for (unsigned i = 0; i < 2; i++)
    {
        lv_obj_set_style_text_font(date_labels[i], &ui_font_chinese_24_4, 0);
        lv_obj_set_style_text_color(date_labels[i], lv_color_hex(i == 0 ? CALENDAR_COLOR_MUTED : CALENDAR_COLOR_TEXT), 0);
        lv_obj_set_style_pad_hor(date_labels[i], 12, 0);
        lv_obj_set_style_pad_ver(date_labels[i], 4, 0);
        lv_obj_set_style_bg_color(date_labels[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(date_labels[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(date_labels[i], 8, 0);
        lv_obj_set_style_border_width(date_labels[i], 1, 0);
        lv_obj_set_style_border_color(date_labels[i], lv_color_hex(CALENDAR_COLOR_BORDER), 0);
        lv_label_set_long_mode(date_labels[i], LV_LABEL_LONG_DOT);
    }
    if (have_time)
    {
        lv_calendar_date_t date = {datetime.year, datetime.month, datetime.day};
        calendar_show_lunar(&date);
    }
    else
        calendar_show_time_error();
}

static void calendar_apply_layout(void)
{
    lv_obj_set_style_pad_gap(calendar_context.container, 6, 0);
    static const lv_coord_t bg_grid_cols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t bg_grid_rows[] = { 42, LV_GRID_FR(1), 40, LV_GRID_TEMPLATE_LAST };
    //static const lv_coord_t console_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(calendar_context.container, bg_grid_cols, bg_grid_rows);
    lv_obj_set_style_grid_row_align(calendar_context.container, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    lv_obj_set_grid_cell(calendar_context.header, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(calendar_context.calendar, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 1, 1);
    /* 两列宽度由容器决定，日期文字长短变化不会移动农历起点。 */
    lv_obj_set_grid_cell(calendar_context.gregorian_label, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_set_grid_cell(calendar_context.lunar_label, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
}

void ui_calendar_create(lv_obj_t* parent)
{
    if (calendar_context.container != NULL)
    {
        lv_obj_move_foreground(calendar_context.container);
        return;
    }
    calendar_create_container(parent);
    calendar_create_title_bar();
    calendar_create_calendar();
    calendar_apply_layout();
}
