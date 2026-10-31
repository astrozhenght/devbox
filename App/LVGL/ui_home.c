#include "ui_home.h"
#include <stdio.h>   // snprintf()、sprintf()
#include "wifi_service_port.h"
#include "ui_calendar.h"
#include "ui_uart_terminal.h"
#include "ui_images.h"
#include "ui_fonts.h"

typedef enum
{
    APP_UART_TERMINAL = 0,
    APP_FILE_MANAGER,
    APP_SETTINGS,
    APP_MUSIC,
    APP_DAPLINK,
    APP_WEATHER,
    APP_CALENDAR,
    APP_COUNT
} ui_home_app_id_t;

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
    lv_obj_t* status_bar_ram_usage_label; // 三级
    lv_obj_t* status_bar_wifi_icon;       // 三级
    lv_obj_t* status_bar_time_label;      // 三级
    lv_obj_t* tw_time_label;              // 三级
    lv_obj_t* tw_date_label;              // 三级
    lv_obj_t* tw_weather_icon;            // 三级
    lv_obj_t* tw_temperature_label;       // 三级
    lv_timer_t* refresh_timer;            // 定时器
    lv_style_t style_btn_normal;          // 样式，按钮正常样式
    lv_style_t style_btn_pressed;         // 样式，按钮按下样式
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
static void ui_home_configure_layout(void);

// 创建主页-状态栏
static void ui_home_create_status_bar(lv_obj_t *parent)
{
    char time_str[6];
    time_service_datetime_t datetime = {0};
    time_service_get_datetime(&datetime); // 从RTC读取日期和时间

    home_context.system_status_bar = lv_obj_create(parent);
    lv_obj_set_style_pad_left(home_context.system_status_bar, 25, 0);
    lv_obj_set_style_pad_right(home_context.system_status_bar, 25, 0);
    lv_obj_clear_flag(home_context.system_status_bar, LV_OBJ_FLAG_SCROLLABLE);

    home_context.status_bar_cpu_usage_label = lv_label_create(home_context.system_status_bar);
    lv_obj_set_style_text_font(home_context.status_bar_cpu_usage_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.status_bar_cpu_usage_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(home_context.status_bar_cpu_usage_label, "CPU:27%");

    home_context.status_bar_ram_usage_label = lv_label_create(home_context.system_status_bar);
    lv_obj_set_style_text_font(home_context.status_bar_ram_usage_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.status_bar_ram_usage_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(home_context.status_bar_ram_usage_label, "RAM:27%");

    home_context.status_bar_wifi_icon = lv_label_create(home_context.system_status_bar);
    lv_obj_set_style_text_font(home_context.status_bar_wifi_icon, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(home_context.status_bar_wifi_icon, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(home_context.status_bar_wifi_icon, LV_SYMBOL_WIFI);

    home_context.status_bar_time_label = lv_label_create(home_context.system_status_bar);
    lv_obj_set_style_text_font(home_context.status_bar_time_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.status_bar_time_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    ui_home_format_time(time_str, datetime.hour, datetime.minute); // 格式化时间，例如: "23:09"
    time_str[5] = '\0'; // 手动补上结束符
    lv_label_set_text(home_context.status_bar_time_label, time_str); // 把读取到的RTC时间，刷新显示到主界面

    lv_obj_set_style_radius(home_context.system_status_bar, 0, 0);  // 设置圆角为0，变成矩形
    //static const lv_coord_t status_grid_cols[] = { LV_GRID_FR(20), LV_GRID_FR(20), LV_GRID_FR(86), LV_GRID_FR(12), LV_GRID_FR(12), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t status_grid_cols[] = { LV_GRID_FR(13), LV_GRID_FR(13), LV_GRID_FR(58), LV_GRID_FR(8), LV_GRID_FR(8), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t status_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(home_context.system_status_bar, status_grid_cols, status_grid_rows);
    lv_obj_set_style_grid_row_align(home_context.system_status_bar, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    lv_obj_set_grid_cell(home_context.status_bar_cpu_usage_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.status_bar_ram_usage_label, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.status_bar_wifi_icon, LV_GRID_ALIGN_END, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.status_bar_time_label, LV_GRID_ALIGN_END, 4, 1, LV_GRID_ALIGN_CENTER, 0, 1);
}

// 创建主页-时间和天气面板
static void ui_home_create_datetime_weather_panel(lv_obj_t *parent)
{
    char time_str[6];
    char date_str[32];
    time_service_datetime_t datetime = {0};
    time_service_get_datetime(&datetime); // 从RTC读取日期和时间

    home_context.datetime_weather = lv_obj_create(parent);
    lv_obj_set_style_pad_left(home_context.datetime_weather, 25, 0);
    lv_obj_set_style_pad_right(home_context.datetime_weather, 25, 0);
    lv_obj_clear_flag(home_context.datetime_weather, LV_OBJ_FLAG_SCROLLABLE);

    home_context.tw_time_label = lv_label_create(home_context.datetime_weather);
    lv_obj_set_style_text_font(home_context.tw_time_label, &ui_font_chinese_72_4, 0);
    lv_obj_set_style_text_color(home_context.tw_time_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    time_str[5] = 0; // 结束符
    ui_home_format_time(time_str, datetime.hour, datetime.minute); // 格式化时间，例如: "23:09"
    lv_label_set_text(home_context.tw_time_label, time_str);

    home_context.tw_weather_icon = lv_img_create(home_context.datetime_weather);
    lv_img_set_src(home_context.tw_weather_icon, ui_weather_img_get(99U)); // 默认显示未知图标N/A
    lv_obj_center(home_context.tw_weather_icon);

    home_context.tw_date_label = lv_label_create(home_context.datetime_weather);
    lv_obj_set_style_text_font(home_context.tw_date_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.tw_date_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    ui_home_format_date(date_str, datetime.month, datetime.day, datetime.weekday); // 格式化日期，例如："9月22日 周二"
    lv_label_set_text(home_context.tw_date_label, date_str);

    home_context.tw_temperature_label = lv_label_create(home_context.datetime_weather);
    lv_obj_set_style_text_font(home_context.tw_temperature_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(home_context.tw_temperature_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(home_context.tw_temperature_label, "晴 16℃");

    lv_obj_set_style_radius(home_context.datetime_weather, 0, 0);  // 设置圆角为0，变成矩形
    static const lv_coord_t time_weather_grid_cols[] = { LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t time_weather_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(home_context.datetime_weather, time_weather_grid_cols, time_weather_grid_rows);
    lv_obj_set_style_grid_row_align(home_context.datetime_weather, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间

    lv_obj_set_grid_cell(home_context.tw_time_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.tw_weather_icon, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(home_context.tw_date_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);
    lv_obj_set_grid_cell(home_context.tw_temperature_label, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 1, 1);
}

// 创建主页-应用启动器
static void ui_home_create_app_launcher(lv_obj_t *parent)
{
    home_context.app_launcher = lv_obj_create(parent);
    lv_obj_set_flex_flow(home_context.app_launcher, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(home_context.app_launcher, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(home_context.app_launcher, LV_SCROLLBAR_MODE_OFF);

    lv_style_init(&home_context.style_btn_normal);
    //lv_style_set_bg_opa(&home_context.style_btn_normal, LV_OPA_50);
    lv_style_set_flex_flow(&home_context.style_btn_normal, LV_FLEX_FLOW_COLUMN);
    lv_style_set_pad_top(&home_context.style_btn_normal, 17);
    lv_style_set_pad_row(&home_context.style_btn_normal, 10); // 图标和文字之间的垂直间距
    lv_style_set_layout(&home_context.style_btn_normal, LV_LAYOUT_FLEX); /* 设置 Flex 布局 */
    lv_style_set_flex_main_place(&home_context.style_btn_normal, LV_FLEX_ALIGN_START);
    lv_style_set_bg_color(&home_context.style_btn_normal, lv_color_hex(0x6495ED)); //矢车菊的蓝色

    lv_style_init(&home_context.style_btn_pressed);
    lv_style_set_bg_opa(&home_context.style_btn_pressed, LV_OPA_COVER);
    lv_style_set_bg_color(&home_context.style_btn_pressed, lv_color_hex(0x557FC8));

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
}

static void ui_home_app_click_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_target(event);

    if (code != LV_EVENT_CLICKED)
    {
        return;
    }

    if ((target == home_context.app_items[APP_UART_TERMINAL].button) || (target == home_context.app_items[APP_UART_TERMINAL].button_icon)) // 打开串口调试助手
    {
        ui_uart_terminal_create(lv_scr_act());
    }
    else if ((target == home_context.app_items[APP_CALENDAR].button) || (target == home_context.app_items[APP_CALENDAR].button_icon)) // 打开日历
    {
        ui_calendar_create(lv_scr_act());
    }
}

// 定时调度入口，定时器1s
static void ui_home_refresh_timer_cb(lv_timer_t* t)
{
    LV_UNUSED(t);
    static uint32_t last_time_year   = 0xFF;  // 年
    static uint8_t  last_time_month  = 0xFF;  // 月
    static uint8_t  last_time_day    = 0xFF;  // 日
    static uint8_t  last_time_hour   = 0xFF;  // 时
    static uint8_t  last_time_minute = 0xFF;  // 分
    uint32_t text_len; // 天气字符串长度
    uint32_t temp_len; // 温度字符串长度
    uint8_t weather_str[64]; // 处理后的天气字符串，比如："小雨 16℃"
    time_service_datetime_t datetime = {0};
    wifi_service_message_t response = {0};

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

            // 向队列发送命令，周期请求更新天气
            if ((datetime.minute % WEATHER_REFRESH_PERIOD_MINUTES) == 0)
            {
                if (wifi_service_enqueue_command(WIFI_SERVICE_CMD_GET_WEATHER) == false)
                {
                    // printf("队列发送失败\r\n");
                    // 队列繁忙，下一个周期重新请求
                }
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

    if (wifi_service_receive_message(&response) == true)
    {
        if (response.status != WIFI_SERVICE_STATUS_OK)
        {
            /* 显示对应错误 */
            return;
        }

        switch (response.command)
        {
            case WIFI_SERVICE_CMD_GET_WEATHER: // 已成功获取天气，更新天气图标和对应文字
                // 更新天气数据，拼接字符串形式为"小雨 16℃"，然后显示到屏幕上
                #if 0
                text_len = strlen(response.data.weather.text);
                temp_len = strlen(response.data.weather.temperature);
                memcpy(weather_str, response.data.weather.text, text_len);
                weather_str[text_len] = ' ';
                memcpy(&weather_str[text_len + 1], response.data.weather.temperature, temp_len);
                weather_str[text_len + temp_len + 1] = 0xE2;
                weather_str[text_len + temp_len + 2] = 0x84;
                weather_str[text_len + temp_len + 3] = 0x83; // ℃ UTF-8编码
                weather_str[text_len + temp_len + 4] = 0;
                #else
                snprintf((char *)weather_str,
                         sizeof(weather_str),
                         "%s %d℃",
                         response.data.weather.text,
                         response.data.weather.temperature);
                // weather_str[text_len + temp_len + 4] = 0;
                #endif
                lv_label_set_text(home_context.tw_temperature_label, (const char *)weather_str);

                // 更新天气图标
                lv_img_set_src(home_context.tw_weather_icon, ui_weather_img_get(response.data.weather.code));
                break;
            case WIFI_SERVICE_CMD_GET_SNTPTIME: // 已成功获取SNTP时间
                if (time_service_parse_sntp_response(response.data.sntp_time, &datetime))
                {
                    // 校准RTC时间和日期
                    if (time_service_set_datetime(&datetime) == false)
                    {
                        // 校准失败
                    }
                }
                break;
            default:
                break;
        }
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
    lv_obj_set_style_pad_gap(root, 0, 0);  // 去掉容器与子项的外边距
    lv_obj_set_style_pad_all(root, 0, 0);  // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_pad_right(root, 0, 0);
    lv_obj_set_style_pad_left(root, 0, 0);
    return root;
}

static void ui_home_configure_layout(void)
{
    //lv_obj_set_style_border_width(root, 0, 0);
    //lv_obj_set_style_pad_all(root, 0, 0);
    static const lv_coord_t main_grid_cols[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t main_grid_rows[] = { LV_GRID_FR(8), LV_GRID_FR(32), LV_GRID_FR(60), LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(home_context.root_obj, main_grid_cols, main_grid_rows);
    lv_obj_set_style_grid_row_align(home_context.root_obj, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    lv_obj_set_grid_cell(home_context.system_status_bar, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(home_context.datetime_weather, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(home_context.app_launcher, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
}

void ui_home_create(lv_obj_t* parent)
{
    // 是否有必要加这段代码？？
    // char time_str[6] = {0};
    // char date_str[32] = {0};
    // // 从RTC读取日期和时间
    // if (time_service_get_datetime(&datetime))
    // {
    //     // 把读取到的RTC时间，刷新显示到主界面
    //     ui_home_format_time(time_str, datetime.hour, datetime.minute); // 格式化时间
    //     ui_home_format_date(date_str, datetime.month, datetime.day, datetime.weekday); // 格式化日期
    // }

    home_context.refresh_timer = lv_timer_create(ui_home_refresh_timer_cb, 1000, NULL); // 创建定时器，周期1秒
    home_context.root_obj = ui_home_create_root(parent);
    ui_home_create_status_bar(home_context.root_obj);              // 创建状态栏
    ui_home_create_datetime_weather_panel(home_context.root_obj);  // 创建时间和天气面板
    ui_home_create_app_launcher(home_context.root_obj);            // 创建应用启动器
    ui_home_configure_layout(); // 配置整体布局
}

