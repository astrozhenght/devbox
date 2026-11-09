#include "ui_home.h"
#include <stdio.h>   // snprintf()、sprintf()
#include "wifi_service_port.h"
#include "ui_calendar.h"
#include "ui_weather.h"
#include "weather_service.h"
#include "ui_uart_terminal.h"
#include "ui_file_manager.h"
#include "ui_calculator.h"
#include "system_monitor.h"
#include "ui_images.h"
#include "ui_fonts.h"

#define HOME_UI_BG_COLOR          0xF5F7FA
#define HOME_UI_CARD_COLOR        0xFFFFFF
#define HOME_UI_TEXT_COLOR        0x243247
#define HOME_UI_MUTED_COLOR       0x69788B
#define HOME_UI_BORDER_COLOR      0xE4EAF1
#define HOME_UI_PRESSED_COLOR     0xE6F0FF
#define HOME_UI_DISABLED_TEXT     0xA5AFBC
#define HOME_UI_PRIMARY_BLUE      0x2196F3

typedef enum
{
    APP_UART_TERMINAL = 0,
    APP_FILE_MANAGER,
    APP_SETTINGS,
    APP_MUSIC,
    APP_DAPLINK,
    APP_WEATHER,
    APP_CALENDAR,
    APP_CALCULATOR,
    APP_COUNT
} ui_home_app_id_t;

typedef struct
{
    const char *name;
    const char *fallback_icon;
    const lv_img_dsc_t *icon;
    bool enabled;
    uint8_t grid_column;
    uint8_t grid_row;
} ui_home_app_descriptor_t;

/*
 * 应用名称、图标和可用状态集中定义，避免每个入口分别设置样式后逐渐失去一致性。
 * 尚未实现页面入口的应用保持可见但禁用，让用户能够区分“预留功能”和“可用功能”。
 */
static const ui_home_app_descriptor_t app_descriptors[APP_COUNT] =
{
    [APP_UART_TERMINAL] = {"串口助手",     "UART",  &ui_img_icon_uart,     true,  0, 0},
    [APP_FILE_MANAGER]  = {"文件管理",     "FILE",  &ui_img_icon_folder48, true,  1, 0},
    [APP_SETTINGS]      = {"设置",         "SET",   &ui_img_icon_setting,  false, 5, 0},
    [APP_MUSIC]         = {"音乐",         "MUSIC", &ui_img_icon_music,    false, 0, 1},
    [APP_DAPLINK]       = {"DAP Link",     "DAP",   &ui_img_icon_debug,    false, 1, 1},
    [APP_WEATHER]       = {"天气",         "WEA",   &ui_img_icon_weather,  true,  2, 0},
    [APP_CALENDAR]      = {"日历",         "CAL",   &ui_img_icon_calendar, true,  3, 0},
    [APP_CALCULATOR]    = {"计算器",       "CALC",  &ui_img_icon_calculator, true, 4, 0},
};

typedef struct
{
    lv_obj_t *button;
    lv_obj_t *button_icon;
    lv_obj_t *button_icon_img;
    lv_obj_t *button_icon_label;
    lv_obj_t *button_label;
} ui_home_app_item_t; // 应用启动器结构体

typedef struct
{
    lv_obj_t* root_obj;                   // 一级根对象
    lv_obj_t* system_status_bar;          // 二级状态栏
    lv_obj_t* datetime_weather;           // 二级时间和天气面板
    lv_obj_t* app_launcher;               // 二级应用启动器
    lv_obj_t* status_bar_cpu_usage_label; // 三级
    lv_obj_t* status_bar_cpu_ram_divider; // CPU与RAM分隔线
    lv_obj_t* status_bar_ram_usage_label; // 三级
    lv_obj_t* status_bar_ram_fps_divider; // RAM与FPS分隔线
    lv_obj_t* status_bar_fps_label;       // 三级
    lv_obj_t* status_bar_wifi_icon;       // 三级
    lv_obj_t* status_bar_time_label;      // 三级
    lv_obj_t* time_group;                 // 左侧时间和日期信息组
    lv_obj_t* datetime_divider;            // 时间与天气之间的分隔线
    lv_obj_t* weather_group;              // 右侧当前天气信息组
    lv_obj_t* tw_time_label;              // 三级
    lv_obj_t* tw_date_label;              // 三级
    lv_obj_t* tw_weather_icon;            // 三级
    lv_obj_t* tw_temperature_label;       // 三级
    lv_timer_t* refresh_timer;            // 定时器
    lv_style_t style_btn_normal;          // 样式，按钮正常样式
    lv_style_t style_btn_pressed;         // 样式，按钮按下样式
    lv_style_t style_btn_disabled;        // 样式，尚未开放的应用
    ui_home_app_item_t app_items[APP_COUNT]; // 三级，App结构体数组
} ui_home_context_t;

static ui_home_context_t home_context = {0};

static void ui_home_create_status_bar(lv_obj_t *parent);
static void ui_home_create_datetime_weather_panel(lv_obj_t *parent);
static void ui_home_create_app_launcher(lv_obj_t *parent);
static void ui_home_app_click_event_cb(lv_event_t *event);
static void ui_home_refresh_timer_cb(lv_timer_t* t);
static void ui_home_format_date(char* out, uint8_t month, uint8_t date, uint8_t weekday);
static void ui_home_format_time(char* out, uint8_t hour, uint8_t minute);
static lv_obj_t *ui_home_create_root(lv_obj_t *parent);
static lv_obj_t *ui_home_create_status_divider(lv_obj_t *parent);
static void ui_home_configure_layout(void);
static void ui_home_style_card(lv_obj_t *card);
static void ui_home_update_system_monitor(void);

static lv_obj_t *ui_home_create_status_divider(lv_obj_t *parent)
{
    lv_obj_t *divider = lv_obj_create(parent);

    /* 分隔线与24px文字等高，适度加粗以保证在LCD上清晰可见。 */
    lv_obj_set_size(divider, 2, 24);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(divider, lv_color_hex(HOME_UI_MUTED_COLOR), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_70, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_radius(divider, 1, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);

    return divider;
}

// 创建主页-状态栏
static void ui_home_create_status_bar(lv_obj_t *parent)
{
    char time_str[6] = {0};
    time_service_datetime_t datetime = {0};
    time_service_get_datetime(&datetime); // 从RTC读取日期和时间

    home_context.system_status_bar = lv_obj_create(parent);
    lv_obj_clear_flag(home_context.system_status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(home_context.system_status_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(home_context.system_status_bar, 0, 0);
    lv_obj_set_style_radius(home_context.system_status_bar, 0, 0);
    /* 左侧与各App标题统一从屏幕18px处起始，右侧间距保持不变。 */
    lv_obj_set_style_pad_left(home_context.system_status_bar, 10, 0);
    lv_obj_set_style_pad_right(home_context.system_status_bar, 12, 0);
    lv_obj_set_style_pad_ver(home_context.system_status_bar, 0, 0);
    lv_obj_set_style_pad_column(home_context.system_status_bar, 6, 0);

    home_context.status_bar_cpu_usage_label = lv_label_create(home_context.system_status_bar);
    lv_obj_set_style_text_font(home_context.status_bar_cpu_usage_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.status_bar_cpu_usage_label,
                                lv_color_hex(HOME_UI_MUTED_COLOR), 0);
    /* C比中文标题少2px左侧字形留白，整组右移后按可见笔画对齐。 */
    lv_obj_set_style_translate_x(home_context.status_bar_cpu_usage_label, 2, 0);
    lv_label_set_text(home_context.status_bar_cpu_usage_label, "CPU:--%");

    home_context.status_bar_cpu_ram_divider =
        ui_home_create_status_divider(home_context.system_status_bar);
    lv_obj_set_style_translate_x(home_context.status_bar_cpu_ram_divider, -10, 0);

    home_context.status_bar_ram_usage_label = lv_label_create(home_context.system_status_bar);
    lv_obj_set_style_text_font(home_context.status_bar_ram_usage_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.status_bar_ram_usage_label,
                                lv_color_hex(HOME_UI_MUTED_COLOR), 0);
    lv_obj_set_style_translate_x(home_context.status_bar_ram_usage_label, -10, 0);
    lv_label_set_text(home_context.status_bar_ram_usage_label, "RAM:--%");

    home_context.status_bar_ram_fps_divider =
        ui_home_create_status_divider(home_context.system_status_bar);
    lv_obj_set_style_translate_x(home_context.status_bar_ram_fps_divider, -10, 0);

    home_context.status_bar_fps_label = lv_label_create(home_context.system_status_bar);
    lv_obj_set_style_text_font(home_context.status_bar_fps_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.status_bar_fps_label,
                                lv_color_hex(HOME_UI_MUTED_COLOR), 0);
    /* FPS保留相对整组额外左移12px的视觉调整。 */
    lv_obj_set_style_translate_x(home_context.status_bar_fps_label, -22, 0);
    lv_label_set_text(home_context.status_bar_fps_label, "FPS:--");

    home_context.status_bar_wifi_icon = lv_label_create(home_context.system_status_bar);
    lv_obj_set_style_text_font(home_context.status_bar_wifi_icon, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(home_context.status_bar_wifi_icon, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(home_context.status_bar_wifi_icon, LV_SYMBOL_WIFI);

    home_context.status_bar_time_label = lv_label_create(home_context.system_status_bar);
    lv_obj_set_style_text_font(home_context.status_bar_time_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.status_bar_time_label,
                                lv_color_hex(HOME_UI_PRIMARY_BLUE), 0);
    ui_home_format_time(time_str, datetime.hour, datetime.minute); // 格式化时间，例如: "23:09"
    time_str[5] = '\0'; // 手动补上结束符
    lv_label_set_text(home_context.status_bar_time_label, time_str); // 把读取到的RTC时间，刷新显示到主界面

    /* 三个指标占用相同宽度，使两条分隔线左右间距完全一致。 */
    static const lv_coord_t status_grid_cols[] = {
        96, 2, 96, 2, 96, LV_GRID_FR(1),
        LV_GRID_CONTENT, LV_GRID_CONTENT,
        LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t status_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(home_context.system_status_bar, status_grid_cols, status_grid_rows);
    lv_obj_set_grid_cell(home_context.status_bar_cpu_usage_label,
                         LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.status_bar_cpu_ram_divider,
                         LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.status_bar_ram_usage_label,
                         LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.status_bar_ram_fps_divider,
                         LV_GRID_ALIGN_CENTER, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.status_bar_fps_label,
                         LV_GRID_ALIGN_CENTER, 4, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.status_bar_wifi_icon,
                         LV_GRID_ALIGN_END, 6, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.status_bar_time_label,
                         LV_GRID_ALIGN_END, 7, 1, LV_GRID_ALIGN_CENTER, 0, 1);
}

// 为主页信息区域提供统一的白色圆角卡片样式
static void ui_home_style_card(lv_obj_t *card)
{
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(HOME_UI_CARD_COLOR), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(HOME_UI_BORDER_COLOR), 0);
    lv_obj_set_style_radius(card, 8, 0);
}

// 创建主页-时间和天气面板
static void ui_home_create_datetime_weather_panel(lv_obj_t *parent)
{
    char time_str[6] = {0};
    char date_str[32] = {0};
    time_service_datetime_t datetime = {0};
    time_service_get_datetime(&datetime); // 从RTC读取日期和时间

    home_context.datetime_weather = lv_obj_create(parent);
    ui_home_style_card(home_context.datetime_weather);
    lv_obj_set_style_pad_all(home_context.datetime_weather, 0, 0);

    home_context.time_group = lv_obj_create(home_context.datetime_weather);
    lv_obj_clear_flag(home_context.time_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(home_context.time_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(home_context.time_group, 0, 0);
    lv_obj_set_style_pad_left(home_context.time_group, 32, 0);
    lv_obj_set_style_pad_right(home_context.time_group, 12, 0);
    lv_obj_set_style_pad_ver(home_context.time_group, 0, 0);
    lv_obj_set_style_pad_row(home_context.time_group, 0, 0);
    lv_obj_set_flex_flow(home_context.time_group, LV_FLEX_FLOW_COLUMN);
    /* 时间、日期以及上下留白采用等间距分布，避免文字集中在卡片中央。 */
    lv_obj_set_flex_align(home_context.time_group,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);

    home_context.tw_time_label = lv_label_create(home_context.time_group);
    lv_obj_set_style_text_font(home_context.tw_time_label, &ui_font_chinese_72_4, 0);
    lv_obj_set_style_text_color(home_context.tw_time_label,
                                lv_color_hex(HOME_UI_PRIMARY_BLUE), 0);
    time_str[5] = 0; // 结束符
    ui_home_format_time(time_str, datetime.hour, datetime.minute); // 格式化时间，例如: "23:09"
    lv_label_set_text(home_context.tw_time_label, time_str);

    home_context.tw_date_label = lv_label_create(home_context.time_group);
    lv_obj_set_style_text_font(home_context.tw_date_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.tw_date_label,
                                lv_color_hex(HOME_UI_PRIMARY_BLUE), 0);
    ui_home_format_date(date_str, datetime.month, datetime.day, datetime.weekday); // 格式化日期，例如："9月22日 周二"
    lv_label_set_text(home_context.tw_date_label, date_str);

    home_context.datetime_divider = lv_obj_create(home_context.datetime_weather);
    lv_obj_clear_flag(home_context.datetime_divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(home_context.datetime_divider, 1, lv_pct(68));
    lv_obj_set_style_bg_color(home_context.datetime_divider,
                              lv_color_hex(HOME_UI_BORDER_COLOR), 0);
    lv_obj_set_style_bg_opa(home_context.datetime_divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_context.datetime_divider, 0, 0);
    lv_obj_set_style_pad_all(home_context.datetime_divider, 0, 0);
    lv_obj_set_style_radius(home_context.datetime_divider, 0, 0);

    home_context.weather_group = lv_obj_create(home_context.datetime_weather);
    lv_obj_clear_flag(home_context.weather_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(home_context.weather_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(home_context.weather_group, 0, 0);
    lv_obj_set_style_pad_all(home_context.weather_group, 0, 0);
    lv_obj_set_style_pad_row(home_context.weather_group, 0, 0);
    lv_obj_set_flex_flow(home_context.weather_group, LV_FLEX_FLOW_COLUMN);
    /* 天气图标、文字以及上下留白采用等间距分布，与左侧时间区域保持一致。 */
    lv_obj_set_flex_align(home_context.weather_group,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    home_context.tw_weather_icon = lv_img_create(home_context.weather_group);
    lv_img_set_src(home_context.tw_weather_icon, ui_weather_img_get(99U)); // 默认显示未知图标N/A

    home_context.tw_temperature_label = lv_label_create(home_context.weather_group);
    lv_obj_set_style_text_font(home_context.tw_temperature_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.tw_temperature_label,
                                lv_color_hex(HOME_UI_PRIMARY_BLUE), 0);
    lv_label_set_text(home_context.tw_temperature_label, "晴 16℃");

    /*
     * 使用一张完整卡片承载时间和天气：左侧文字保持同一基线，右侧图标与温度上下居中。
     * 中间的 1px 分隔线只负责建立视觉分区，避免两张独立卡片产生割裂感。
     */
    static const lv_coord_t time_weather_grid_cols[] = {
        LV_GRID_FR(5), 1, LV_GRID_FR(3), LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t time_weather_grid_rows[] = {
        LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    lv_obj_set_grid_dsc_array(home_context.datetime_weather, time_weather_grid_cols, time_weather_grid_rows);
    lv_obj_set_grid_cell(home_context.time_group,
                         LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(home_context.datetime_divider,
                         LV_GRID_ALIGN_CENTER, 1, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.weather_group,
                         LV_GRID_ALIGN_STRETCH, 2, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
}

// 创建主页-应用启动器
static void ui_home_create_app_launcher(lv_obj_t *parent)
{
    home_context.app_launcher = lv_obj_create(parent);
    lv_obj_clear_flag(home_context.app_launcher, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(home_context.app_launcher, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(home_context.app_launcher, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(home_context.app_launcher, 0, 0);
    lv_obj_set_style_pad_all(home_context.app_launcher, 0, 0);
    lv_obj_set_style_pad_gap(home_context.app_launcher, 8, 0);

    /* 800×480 屏幕每行显示 6 个入口，第二行继续按从左到右的手机桌面顺序排列。 */
    static const lv_coord_t app_grid_cols[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t app_grid_rows[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    lv_obj_set_grid_dsc_array(home_context.app_launcher, app_grid_cols, app_grid_rows);

    lv_style_init(&home_context.style_btn_normal);
    /* 手机桌面式入口：网格单元只负责排列图标与名称，不承担点击。 */
    lv_style_set_bg_opa(&home_context.style_btn_normal, LV_OPA_TRANSP);
    lv_style_set_border_width(&home_context.style_btn_normal, 0);
    lv_style_set_radius(&home_context.style_btn_normal, 12);
    lv_style_set_flex_flow(&home_context.style_btn_normal, LV_FLEX_FLOW_COLUMN);
    lv_style_set_pad_all(&home_context.style_btn_normal, 8);
    lv_style_set_pad_row(&home_context.style_btn_normal, 6); // 图标和文字之间的垂直间距
    lv_style_set_layout(&home_context.style_btn_normal, LV_LAYOUT_FLEX); /* 设置 Flex 布局 */
    lv_style_set_flex_main_place(&home_context.style_btn_normal, LV_FLEX_ALIGN_CENTER);
    lv_style_set_flex_cross_place(&home_context.style_btn_normal, LV_FLEX_ALIGN_CENTER);

    lv_style_init(&home_context.style_btn_pressed);
    lv_style_set_bg_opa(&home_context.style_btn_pressed, LV_OPA_COVER);
    lv_style_set_bg_color(&home_context.style_btn_pressed, lv_color_hex(HOME_UI_PRESSED_COLOR));
    lv_style_set_border_color(&home_context.style_btn_pressed,
                              lv_color_hex(HOME_UI_PRIMARY_BLUE));

    lv_style_init(&home_context.style_btn_disabled);
    lv_style_set_bg_opa(&home_context.style_btn_disabled, LV_OPA_COVER);
    lv_style_set_bg_color(&home_context.style_btn_disabled,
                          lv_color_hex(HOME_UI_CARD_COLOR));
    lv_style_set_border_color(&home_context.style_btn_disabled,
                              lv_color_hex(HOME_UI_BORDER_COLOR));

    //****************************************************************************
    home_context.app_items[APP_UART_TERMINAL].button = lv_btn_create(home_context.app_launcher);
    lv_obj_set_size(home_context.app_items[APP_UART_TERMINAL].button, 200, 123);
    lv_obj_clear_flag(home_context.app_items[APP_UART_TERMINAL].button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(home_context.app_items[APP_UART_TERMINAL].button, &home_context.style_btn_normal, 0);
    lv_obj_add_event_cb(home_context.app_items[APP_UART_TERMINAL].button, ui_home_app_click_event_cb, LV_EVENT_CLICKED, NULL);
    home_context.app_items[APP_UART_TERMINAL].button_icon = lv_obj_create(home_context.app_items[APP_UART_TERMINAL].button);
    lv_obj_set_size(home_context.app_items[APP_UART_TERMINAL].button_icon, 60, 60);
    //lv_obj_set_style_bg_color(home_context.app_items[APP_UART_TERMINAL].button_icon, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_set_style_pad_all(home_context.app_items[APP_UART_TERMINAL].button_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(home_context.app_items[APP_UART_TERMINAL].button_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(home_context.app_items[APP_UART_TERMINAL].button_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    home_context.app_items[APP_UART_TERMINAL].button_icon_label = lv_label_create(home_context.app_items[APP_UART_TERMINAL].button_icon);
    lv_obj_set_style_text_font(home_context.app_items[APP_UART_TERMINAL].button_icon_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_UART_TERMINAL].button_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(home_context.app_items[APP_UART_TERMINAL].button_icon_label);
    lv_label_set_text(home_context.app_items[APP_UART_TERMINAL].button_icon_label, "UART");
#else
    home_context.app_items[APP_UART_TERMINAL].button_icon_img = lv_img_create(home_context.app_items[APP_UART_TERMINAL].button_icon);
    lv_img_set_src(home_context.app_items[APP_UART_TERMINAL].button_icon_img, &ui_img_icon_uart);
    lv_obj_center(home_context.app_items[APP_UART_TERMINAL].button_icon_img);
#endif
    home_context.app_items[APP_UART_TERMINAL].button_label = lv_label_create(home_context.app_items[APP_UART_TERMINAL].button);
    lv_obj_set_style_text_font(home_context.app_items[APP_UART_TERMINAL].button_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_UART_TERMINAL].button_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(home_context.app_items[APP_UART_TERMINAL].button_label, "串口调试助手");

    //****************************************************************************
    home_context.app_items[APP_FILE_MANAGER].button = lv_btn_create(home_context.app_launcher);
    lv_obj_set_size(home_context.app_items[APP_FILE_MANAGER].button, 200, 123);
    lv_obj_clear_flag(home_context.app_items[APP_FILE_MANAGER].button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(home_context.app_items[APP_FILE_MANAGER].button, &home_context.style_btn_normal, 0);
    lv_obj_add_event_cb(home_context.app_items[APP_FILE_MANAGER].button, ui_home_app_click_event_cb, LV_EVENT_CLICKED, NULL);
    home_context.app_items[APP_FILE_MANAGER].button_icon = lv_obj_create(home_context.app_items[APP_FILE_MANAGER].button);
    lv_obj_set_size(home_context.app_items[APP_FILE_MANAGER].button_icon, 60, 60);
    //lv_obj_set_style_bg_color(home_context.app_items[APP_FILE_MANAGER].button_icon, lv_palette_main(LV_PALETTE_PINK), 0);
    lv_obj_set_style_pad_all(home_context.app_items[APP_FILE_MANAGER].button_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(home_context.app_items[APP_FILE_MANAGER].button_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(home_context.app_items[APP_FILE_MANAGER].button_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    home_context.app_items[APP_FILE_MANAGER].button_icon_label = lv_label_create(home_context.app_items[APP_FILE_MANAGER].button_icon);
    lv_obj_set_style_text_font(home_context.app_items[APP_FILE_MANAGER].button_icon_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_FILE_MANAGER].button_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(home_context.app_items[APP_FILE_MANAGER].button_icon_label);
    lv_label_set_text(home_context.app_items[APP_FILE_MANAGER].button_icon_label, "FILE");
#else
    home_context.app_items[APP_FILE_MANAGER].button_icon_img = lv_img_create(home_context.app_items[APP_FILE_MANAGER].button_icon);
    lv_img_set_src(home_context.app_items[APP_FILE_MANAGER].button_icon_img, &ui_img_icon_folder48);
    lv_obj_center(home_context.app_items[APP_FILE_MANAGER].button_icon_img);
#endif
    home_context.app_items[APP_FILE_MANAGER].button_label = lv_label_create(home_context.app_items[APP_FILE_MANAGER].button);
    lv_obj_set_style_text_font(home_context.app_items[APP_FILE_MANAGER].button_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_FILE_MANAGER].button_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(home_context.app_items[APP_FILE_MANAGER].button_label, "文件管理");

    //****************************************************************************
    home_context.app_items[APP_SETTINGS].button = lv_btn_create(home_context.app_launcher);
    lv_obj_set_size(home_context.app_items[APP_SETTINGS].button, 200, 123);
    lv_obj_clear_flag(home_context.app_items[APP_SETTINGS].button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(home_context.app_items[APP_SETTINGS].button, &home_context.style_btn_normal, 0);
    lv_obj_add_event_cb(home_context.app_items[APP_SETTINGS].button, ui_home_app_click_event_cb, LV_EVENT_CLICKED, NULL);
    home_context.app_items[APP_SETTINGS].button_icon = lv_obj_create(home_context.app_items[APP_SETTINGS].button);
    lv_obj_set_size(home_context.app_items[APP_SETTINGS].button_icon, 60, 60);
    //lv_obj_set_style_bg_color(home_context.app_items[APP_SETTINGS].button_icon, lv_color_hex(ColorGold), 0);
    lv_obj_set_style_pad_all(home_context.app_items[APP_SETTINGS].button_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(home_context.app_items[APP_SETTINGS].button_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(home_context.app_items[APP_SETTINGS].button_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    home_context.app_items[APP_SETTINGS].button_icon_label = lv_label_create(home_context.app_items[APP_SETTINGS].button_icon);
    lv_obj_set_style_text_font(home_context.app_items[APP_SETTINGS].button_icon_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_SETTINGS].button_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(home_context.app_items[APP_SETTINGS].button_icon_label);
    lv_label_set_text(home_context.app_items[APP_SETTINGS].button_icon_label, "SET");
#else
    home_context.app_items[APP_SETTINGS].button_icon_img = lv_img_create(home_context.app_items[APP_SETTINGS].button_icon);
    lv_img_set_src(home_context.app_items[APP_SETTINGS].button_icon_img, &ui_img_icon_setting);
    lv_obj_center(home_context.app_items[APP_SETTINGS].button_icon_img);
#endif
    home_context.app_items[APP_SETTINGS].button_label = lv_label_create(home_context.app_items[APP_SETTINGS].button);
    lv_obj_set_style_text_font(home_context.app_items[APP_SETTINGS].button_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_SETTINGS].button_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(home_context.app_items[APP_SETTINGS].button_label, "设    置");

    //****************************************************************************
    home_context.app_items[APP_MUSIC].button = lv_btn_create(home_context.app_launcher);
    lv_obj_set_size(home_context.app_items[APP_MUSIC].button, 200, 123);
    lv_obj_clear_flag(home_context.app_items[APP_MUSIC].button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(home_context.app_items[APP_MUSIC].button, &home_context.style_btn_normal, 0);
    lv_obj_add_event_cb(home_context.app_items[APP_MUSIC].button, ui_home_app_click_event_cb, LV_EVENT_CLICKED, NULL);
    home_context.app_items[APP_MUSIC].button_icon = lv_obj_create(home_context.app_items[APP_MUSIC].button);
    lv_obj_set_size(home_context.app_items[APP_MUSIC].button_icon, 60, 60);
    //lv_obj_set_style_bg_color(home_context.app_items[APP_MUSIC].button_icon, lv_color_hex(ColorMediumPurple), 0);
    lv_obj_set_style_pad_all(home_context.app_items[APP_MUSIC].button_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(home_context.app_items[APP_MUSIC].button_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(home_context.app_items[APP_MUSIC].button_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    home_context.app_items[APP_MUSIC].button_icon_label = lv_label_create(home_context.app_items[APP_MUSIC].button_icon);
    lv_obj_set_style_text_font(home_context.app_items[APP_MUSIC].button_icon_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_MUSIC].button_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(home_context.app_items[APP_MUSIC].button_icon_label);
    lv_label_set_text(home_context.app_items[APP_MUSIC].button_icon_label, "MUSIC");
#else
    home_context.app_items[APP_MUSIC].button_icon_img = lv_img_create(home_context.app_items[APP_MUSIC].button_icon);
    lv_img_set_src(home_context.app_items[APP_MUSIC].button_icon_img, &ui_img_icon_music);
    lv_obj_center(home_context.app_items[APP_MUSIC].button_icon_img);
#endif
    home_context.app_items[APP_MUSIC].button_label = lv_label_create(home_context.app_items[APP_MUSIC].button);
    lv_obj_set_style_text_font(home_context.app_items[APP_MUSIC].button_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_MUSIC].button_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(home_context.app_items[APP_MUSIC].button_label, "音乐播放器");

    //****************************************************************************
    home_context.app_items[APP_DAPLINK].button = lv_btn_create(home_context.app_launcher);
    lv_obj_set_size(home_context.app_items[APP_DAPLINK].button, 200, 123);
    lv_obj_clear_flag(home_context.app_items[APP_DAPLINK].button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(home_context.app_items[APP_DAPLINK].button, &home_context.style_btn_normal, 0);
    lv_obj_add_event_cb(home_context.app_items[APP_DAPLINK].button, ui_home_app_click_event_cb, LV_EVENT_CLICKED, NULL);
    home_context.app_items[APP_DAPLINK].button_icon = lv_obj_create(home_context.app_items[APP_DAPLINK].button);
    lv_obj_set_size(home_context.app_items[APP_DAPLINK].button_icon, 60, 60);
    //lv_obj_set_style_bg_color(home_context.app_items[APP_DAPLINK].button_icon, lv_color_hex(ColorLightGray), 0);
    lv_obj_set_style_pad_all(home_context.app_items[APP_DAPLINK].button_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(home_context.app_items[APP_DAPLINK].button_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(home_context.app_items[APP_DAPLINK].button_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    home_context.app_items[APP_DAPLINK].button_icon_label = lv_label_create(home_context.app_items[APP_DAPLINK].button_icon);
    lv_obj_set_style_text_font(home_context.app_items[APP_DAPLINK].button_icon_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_DAPLINK].button_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(home_context.app_items[APP_DAPLINK].button_icon_label);
    lv_label_set_text(home_context.app_items[APP_DAPLINK].button_icon_label, "DAP");
#else
    home_context.app_items[APP_DAPLINK].button_icon_img = lv_img_create(home_context.app_items[APP_DAPLINK].button_icon);
    lv_img_set_src(home_context.app_items[APP_DAPLINK].button_icon_img, &ui_img_icon_debug);
    lv_obj_center(home_context.app_items[APP_DAPLINK].button_icon_img);
#endif
    home_context.app_items[APP_DAPLINK].button_label = lv_label_create(home_context.app_items[APP_DAPLINK].button);
    lv_obj_set_style_text_font(home_context.app_items[APP_DAPLINK].button_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_DAPLINK].button_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(home_context.app_items[APP_DAPLINK].button_label, "DAP Link");

    //****************************************************************************
    home_context.app_items[APP_WEATHER].button = lv_btn_create(home_context.app_launcher);
    lv_obj_set_size(home_context.app_items[APP_WEATHER].button, 200, 123);
    lv_obj_clear_flag(home_context.app_items[APP_WEATHER].button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(home_context.app_items[APP_WEATHER].button, &home_context.style_btn_normal, 0);
    lv_obj_add_event_cb(home_context.app_items[APP_WEATHER].button, ui_home_app_click_event_cb, LV_EVENT_CLICKED, NULL);
    home_context.app_items[APP_WEATHER].button_icon = lv_obj_create(home_context.app_items[APP_WEATHER].button);
    lv_obj_set_size(home_context.app_items[APP_WEATHER].button_icon, 60, 60);
    //lv_obj_set_style_bg_color(home_context.app_items[APP_WEATHER].button_icon, lv_color_hex(ColorAuqamarin), 0);
    lv_obj_set_style_pad_all(home_context.app_items[APP_WEATHER].button_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(home_context.app_items[APP_WEATHER].button_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(home_context.app_items[APP_WEATHER].button_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    home_context.app_items[APP_WEATHER].button_icon_label = lv_label_create(home_context.app_items[APP_WEATHER].button_icon);
    lv_obj_set_style_text_font(home_context.app_items[APP_WEATHER].button_icon_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_WEATHER].button_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(home_context.app_items[APP_WEATHER].button_icon_label);
    lv_label_set_text(home_context.app_items[APP_WEATHER].button_icon_label, "WEA");
#else
    home_context.app_items[APP_WEATHER].button_icon_img = lv_img_create(home_context.app_items[APP_WEATHER].button_icon);
    lv_img_set_src(home_context.app_items[APP_WEATHER].button_icon_img, &ui_img_icon_weather);
    lv_obj_center(home_context.app_items[APP_WEATHER].button_icon_img);
#endif
    home_context.app_items[APP_WEATHER].button_label = lv_label_create(home_context.app_items[APP_WEATHER].button);
    lv_obj_set_style_text_font(home_context.app_items[APP_WEATHER].button_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_WEATHER].button_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(home_context.app_items[APP_WEATHER].button_label, "天    气");

    //****************************************************************************
    home_context.app_items[APP_CALENDAR].button = lv_btn_create(home_context.app_launcher);
    lv_obj_set_size(home_context.app_items[APP_CALENDAR].button, 200, 123);
    lv_obj_clear_flag(home_context.app_items[APP_CALENDAR].button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(home_context.app_items[APP_CALENDAR].button, &home_context.style_btn_normal, 0);
    lv_obj_add_event_cb(home_context.app_items[APP_CALENDAR].button, ui_home_app_click_event_cb, LV_EVENT_CLICKED, NULL);
    home_context.app_items[APP_CALENDAR].button_icon = lv_obj_create(home_context.app_items[APP_CALENDAR].button);
    lv_obj_set_size(home_context.app_items[APP_CALENDAR].button_icon, 60, 60);
    //lv_obj_set_style_bg_color(home_context.app_items[APP_CALENDAR].button_icon, lv_color_hex(ColorAuqamarin), 0);
    lv_obj_set_style_pad_all(home_context.app_items[APP_CALENDAR].button_icon, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_border_width(home_context.app_items[APP_CALENDAR].button_icon, 0, 0); // 不显示边框
    lv_obj_add_flag(home_context.app_items[APP_CALENDAR].button_icon, LV_OBJ_FLAG_EVENT_BUBBLE);  // 让事件冒泡，点击 img 时也能触发 obj 的点击事件
#ifdef APP_USE_TEXT
    home_context.app_items[APP_CALENDAR].button_icon_label = lv_label_create(home_context.app_items[APP_CALENDAR].button_icon);
    lv_obj_set_style_text_font(home_context.app_items[APP_CALENDAR].button_icon_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_CALENDAR].button_icon_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_center(home_context.app_items[APP_CALENDAR].button_icon_label);
    lv_label_set_text(home_context.app_items[APP_CALENDAR].button_icon_label, "CAL");
#else
    home_context.app_items[APP_CALENDAR].button_icon_img = lv_img_create(home_context.app_items[APP_CALENDAR].button_icon);
    lv_img_set_src(home_context.app_items[APP_CALENDAR].button_icon_img, &ui_img_icon_calendar);
    lv_obj_center(home_context.app_items[APP_CALENDAR].button_icon_img);
#endif
    home_context.app_items[APP_CALENDAR].button_label = lv_label_create(home_context.app_items[APP_CALENDAR].button);
    lv_obj_set_style_text_font(home_context.app_items[APP_CALENDAR].button_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.app_items[APP_CALENDAR].button_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(home_context.app_items[APP_CALENDAR].button_label, "日    历");

    home_context.app_items[APP_CALCULATOR].button = lv_btn_create(home_context.app_launcher);
    lv_obj_set_size(home_context.app_items[APP_CALCULATOR].button, 200, 123);
    lv_obj_clear_flag(home_context.app_items[APP_CALCULATOR].button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(home_context.app_items[APP_CALCULATOR].button,
                     &home_context.style_btn_normal, 0);
    home_context.app_items[APP_CALCULATOR].button_icon = lv_obj_create(
        home_context.app_items[APP_CALCULATOR].button);
    lv_obj_set_style_pad_all(home_context.app_items[APP_CALCULATOR].button_icon, 0, 0);
#ifdef APP_USE_TEXT
    home_context.app_items[APP_CALCULATOR].button_icon_label = lv_label_create(
        home_context.app_items[APP_CALCULATOR].button_icon);
    lv_obj_set_style_text_font(home_context.app_items[APP_CALCULATOR].button_icon_label,
                               &ui_font_chinese_24_4, 0);
    lv_obj_center(home_context.app_items[APP_CALCULATOR].button_icon_label);
#else
    home_context.app_items[APP_CALCULATOR].button_icon_img = lv_img_create(
        home_context.app_items[APP_CALCULATOR].button_icon);
    lv_img_set_src(home_context.app_items[APP_CALCULATOR].button_icon_img,
                   &ui_img_icon_calculator);
    lv_obj_center(home_context.app_items[APP_CALCULATOR].button_icon_img);
#endif
    home_context.app_items[APP_CALCULATOR].button_label = lv_label_create(
        home_context.app_items[APP_CALCULATOR].button);
    lv_obj_set_style_text_font(home_context.app_items[APP_CALCULATOR].button_label,
                               &ui_font_chinese_24_4, 0);

    /*
     * 在所有对象创建完成后统一应用网格位置和状态样式。
     * 这样新增应用时只需补充描述表和对象，不会遗漏文字颜色、按下反馈或禁用状态。
     */
    for (uint32_t app_id = 0; app_id < APP_COUNT; app_id++)
    {
        ui_home_app_item_t *item = &home_context.app_items[app_id];
        const ui_home_app_descriptor_t *descriptor = &app_descriptors[app_id];

        /* 外层网格对象只负责布局，清除点击能力，避免用户点到图标外部时误打开应用。 */
        lv_obj_remove_style_all(item->button);
        lv_obj_add_style(item->button, &home_context.style_btn_normal, 0);
        lv_obj_clear_flag(item->button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_event_cb(item->button, ui_home_app_click_event_cb);
        lv_obj_set_grid_cell(item->button,
                             LV_GRID_ALIGN_STRETCH, descriptor->grid_column, 1,
                             LV_GRID_ALIGN_STRETCH, descriptor->grid_row, 1);

        /* 白色圆角小框既是图标背景，也是该应用唯一的触摸热区。 */
        lv_obj_set_size(item->button_icon, 72, 72);
        lv_obj_clear_flag(item->button_icon, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_style_bg_color(item->button_icon, lv_color_hex(HOME_UI_CARD_COLOR), 0);
        lv_obj_set_style_bg_opa(item->button_icon, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(item->button_icon, 1, 0);
        lv_obj_set_style_border_color(item->button_icon,
                                      lv_color_hex(HOME_UI_BORDER_COLOR), 0);
        lv_obj_set_style_radius(item->button_icon, 16, 0);
        lv_obj_set_style_shadow_width(item->button_icon, 6, 0);
        lv_obj_set_style_shadow_opa(item->button_icon, LV_OPA_10, 0);
        lv_obj_add_style(item->button_icon,
                         &home_context.style_btn_pressed, LV_STATE_PRESSED);
        lv_obj_add_style(item->button_icon,
                         &home_context.style_btn_disabled, LV_STATE_DISABLED);

        lv_label_set_text(item->button_label, descriptor->name);
        lv_obj_set_width(item->button_label, lv_pct(100));
        lv_obj_clear_flag(item->button_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_align(item->button_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(item->button_label,
                                    lv_color_hex(descriptor->enabled
                                                     ? HOME_UI_TEXT_COLOR
                                                     : HOME_UI_DISABLED_TEXT),
                                    0);

#ifdef APP_USE_TEXT
        lv_label_set_text(item->button_icon_label, descriptor->fallback_icon);
        lv_obj_set_style_text_color(item->button_icon_label,
                                    lv_color_hex(descriptor->enabled
                                                     ? 0x2563A6
                                                     : HOME_UI_DISABLED_TEXT),
                                    0);
#else
        if (descriptor->icon != NULL)
        {
            lv_img_set_src(item->button_icon_img, descriptor->icon);
            lv_obj_set_style_img_opa(item->button_icon_img,
                                     descriptor->enabled ? LV_OPA_COVER : LV_OPA_50,
                                     0);
        }
        else
        {
            lv_label_set_text(item->button_icon_label, descriptor->fallback_icon);
            lv_obj_set_style_text_color(item->button_icon_label,
                                        lv_color_hex(HOME_UI_PRIMARY_BLUE), 0);
        }
#endif

        if (!descriptor->enabled)
        {
            lv_obj_clear_flag(item->button_icon, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_state(item->button_icon, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_add_flag(item->button_icon, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(item->button_icon,
                                ui_home_app_click_event_cb,
                                LV_EVENT_CLICKED,
                                NULL);
        }
    }
}

static void ui_home_app_click_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_target(event);

    if (code != LV_EVENT_CLICKED)
    {
        return;
    }

    if (target == home_context.app_items[APP_UART_TERMINAL].button_icon) // 打开串口调试助手
    {
        ui_uart_terminal_create(lv_scr_act());
    }
    else if (target == home_context.app_items[APP_FILE_MANAGER].button_icon)
    {
        ui_file_manager_create(lv_scr_act());
    }
    else if (target == home_context.app_items[APP_WEATHER].button_icon)
    {
        ui_weather_create(lv_scr_act());
    }
    else if (target == home_context.app_items[APP_CALENDAR].button_icon) // 打开日历
    {
        ui_calendar_create(lv_scr_act());
    }
    else if (target == home_context.app_items[APP_CALCULATOR].button_icon)
    {
        ui_calculator_create(lv_scr_act());
    }
}

static void ui_home_update_system_monitor(void)
{
    const system_monitor_snapshot_t *snapshot;

    system_monitor_sample(lv_tick_get());
    snapshot = system_monitor_get_snapshot();
    if (!snapshot->valid)
    {
        return;
    }

    lv_label_set_text_fmt(home_context.status_bar_cpu_usage_label,
                          "CPU:%u%%", snapshot->cpu_usage_percent);
    lv_label_set_text_fmt(home_context.status_bar_ram_usage_label,
                          "RAM:%u%%", snapshot->ram_usage_percent);
    lv_label_set_text_fmt(home_context.status_bar_fps_label,
                          "FPS:%u", snapshot->fps);
}

// 定时调度入口，定时器1s
static void ui_home_refresh_timer_cb(lv_timer_t* t)
{
    LV_UNUSED(t);
    ui_home_update_system_monitor();
    static uint32_t last_time_year   = 0xFF;  // 年
    static uint8_t  last_time_month  = 0xFF;  // 月
    static uint8_t  last_time_day    = 0xFF;  // 日
    static uint8_t  last_time_hour   = 0xFF;  // 时
    static uint8_t  last_time_minute = 0xFF;  // 分
    time_service_datetime_t datetime = {0};

    if (time_service_get_datetime(&datetime)) // 从RTC读取日期和时间
    {
        // 如果分钟/小时更新了，则刷新重绘到屏幕上
        if ((datetime.minute != last_time_minute) || (datetime.hour != last_time_hour))
        {
            // 把读取到的RTC时间，刷新显示到主界面
            char time_str[6];   // 用于存储时间，例如: "23:09"
            ui_home_format_time(time_str, datetime.hour, datetime.minute); // 格式化时间
            time_str[5] = '\0'; // 手动补上结束符
            lv_label_set_text(home_context.status_bar_time_label, time_str);
            lv_label_set_text(home_context.tw_time_label, time_str);
            last_time_minute = datetime.minute;
            last_time_hour = datetime.hour;
            if ((datetime.day != last_time_day) ||
                (datetime.month != last_time_month) ||
                (datetime.year != last_time_year))
            {
                char date_str[32]; // 用于存储日期，例如："9月22日 周二"
                ui_home_format_date(date_str, datetime.month, datetime.day, datetime.weekday); // 格式化日期
                lv_label_set_text(home_context.tw_date_label, date_str);
                last_time_day = datetime.day;
                last_time_month = datetime.month;
                last_time_year = datetime.year;
            }

            // 向队列发送命令，0点0分请求同步SNTP时间
            if ((datetime.hour == 0) && (datetime.minute == 0))
            {
                if (wifi_service_enqueue_command(WIFI_SERVICE_CMD_GET_SNTPTIME) == false)
                {
                    // printf("队列发送失败\r\n");
                    // 队列繁忙，下一个周期重新请求
                }
            }
        }
    }

    static uint32_t weather_version = UINT32_MAX;
    const weather_snapshot_t *weather = weather_service_get_snapshot();
    if (weather_version != weather->version)
    {
        weather_version = weather->version;
        if (weather->current_valid)
        {
            lv_label_set_text_fmt(home_context.tw_temperature_label, "%s %d\xE2\x84\x83",
                weather->current.text, weather->current.temperature);
            lv_img_set_src(home_context.tw_weather_icon, ui_weather_img_get(weather->current.code));
        }
        lv_obj_set_style_text_color(home_context.status_bar_wifi_icon,
            weather->connected ? lv_palette_main(LV_PALETTE_BLUE) : lv_color_hex(0xA5AFBC), 0);
    }

}

// 格式化日期，例如: "9月16日 周三"
static void ui_home_format_date(char* out, uint8_t month, uint8_t date, uint8_t weekday)
{
    const char* week_str[] = {"禁止", "周一", "周二", "周三", "周四", "周五", "周六", "周日"}; // weekday: 1~7对应周一至周日
    sprintf(out, "%d月%d日 %s", month, date, week_str[weekday]);
}

// 格式化时间，例如: "23:09"
static void ui_home_format_time(char* out, uint8_t hour, uint8_t minute)
{
    sprintf(out, "%02d:%02d", hour, minute);
}

static lv_obj_t *ui_home_create_root(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(HOME_UI_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 8, 0);
    lv_obj_set_style_pad_gap(root, 8, 0);
    return root;
}

static void ui_home_configure_layout(void)
{
    static const lv_coord_t main_grid_cols[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t main_grid_rows[] = {
        42, 136, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    lv_obj_set_grid_dsc_array(home_context.root_obj, main_grid_cols, main_grid_rows);
    lv_obj_set_grid_cell(home_context.system_status_bar, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(home_context.datetime_weather, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(home_context.app_launcher, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
}

void ui_home_create(lv_obj_t* parent)
{
    system_monitor_init();
    home_context.refresh_timer = lv_timer_create(ui_home_refresh_timer_cb, 1000, NULL); // 创建定时器，周期1秒
    home_context.root_obj = ui_home_create_root(parent);
    ui_home_create_status_bar(home_context.root_obj);              // 创建状态栏
    ui_home_create_datetime_weather_panel(home_context.root_obj);  // 创建时间和天气面板
    ui_home_create_app_launcher(home_context.root_obj);            // 创建应用启动器
    ui_home_configure_layout(); // 配置整体布局
}

