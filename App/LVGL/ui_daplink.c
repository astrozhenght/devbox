#include "ui_daplink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_programmer.h"
#include "dap_service.h"
#include "target_profile.h"
#include "ui_file_manager.h"
#include "ui_fonts.h"

#define DAP_UI_BG_COLOR          0xF5F7FA
#define DAP_UI_CARD_COLOR        0xFFFFFF
#define DAP_UI_TEXT_COLOR        0x243247
#define DAP_UI_MUTED_COLOR       0x69788B
#define DAP_UI_BORDER_COLOR      0xE4EAF1
#define DAP_UI_PROGRESS_BG_COLOR 0xB0BEC5
#define DAP_UI_PRIMARY_COLOR     0x2196F3
#define DAP_UI_SUCCESS_COLOR     0x4CAF50
#define DAP_UI_WARNING_COLOR     0xEF6C00
#define DAP_UI_DANGER_COLOR      0xE53935
#define DAP_UI_TITLE_COLOR       0xE91E63
#define DAP_UI_ACTIVITY_HOLD_MS  600U
#define DAP_UI_RATE_SAMPLE_MS    500U
#define DAP_UI_START_WIDTH       112
#define DAP_UI_CANCEL_WIDTH      100
#define DAP_UI_ACTION_GAP        12
#define DAP_UI_ACTION_PADDING    10
/* 为完整IDCODE和SWD参数留足宽度，左侧仍优先显示文件名。 */
#define DAP_UI_TARGET_WIDTH      280

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *usb_status_label;
    lv_obj_t *port_status_label;
    lv_obj_t *clock_label;
    lv_obj_t *duration_label;
    lv_obj_t *dpidr_label;
    lv_obj_t *success_count_label;
    lv_obj_t *failure_count_label;
    lv_obj_t *rate_label;
    lv_obj_t *activity_label;
    lv_obj_t *progress_bar;
    lv_obj_t *firmware_label;
    lv_obj_t *flm_label;
    lv_obj_t *programmer_state_label;
    lv_obj_t *programmer_message_label;
    lv_obj_t *firmware_button;
    lv_obj_t *flm_button;
    lv_obj_t *start_button;
    lv_obj_t *cancel_button;
    lv_obj_t *details_button;
    lv_timer_t *refresh_timer;
    dap_service_status_t status;
    uint32_t last_activity_tick;
    uint32_t rate_sample_tick;
    uint32_t rate_sample_bytes;
    uint32_t rate_bytes_per_second;
    bool status_valid;
    bool activity_seen;
    bool activity_active;
    bool rate_sample_valid;
    lv_obj_t *usb_page;
    lv_obj_t *local_page;
    lv_obj_t *usb_tab;
    lv_obj_t *local_tab;
    lv_obj_t *local_bar;
    lv_obj_t *local_target_label;
    lv_obj_t *local_target_dot;
    lv_obj_t *local_id_label;
    lv_obj_t *clock_dropdown;
    lv_obj_t *erase_dropdown;
    lv_obj_t *stage_labels[3];
    lv_obj_t *elapsed_label;
    lv_obj_t *ram_start_ta;
    lv_obj_t *ram_size_ta;
    lv_obj_t *hex_keyboard_box;
    lv_obj_t *hex_keyboard;
    lv_obj_t *hex_keyboard_title;
    lv_obj_t *result_dialog;
    bool local_selected;
    const char *input_error;
    char firmware_path[FILE_SERVICE_PATH_CAP];
    char flm_path[FILE_SERVICE_PATH_CAP];
} ui_daplink_context_t;

static ui_daplink_context_t daplink_context = {0};

/* 下拉显示顺序与实际传给DAP引擎的频率一一对应，从高到低。 */
static const uint32_t local_clock_rates[] =
{
    10000000U, 5000000U, 2000000U, 1000000U, 500000U, 200000U,
    100000U, 50000U, 20000U, 10000U, 5000U
};

static lv_obj_t *ui_daplink_create_panel(lv_obj_t *parent, bool card)
{
    lv_obj_t *panel = lv_obj_create(parent);

    lv_obj_remove_style_all(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(panel, card ? 12 : 0, 0);
    lv_obj_set_style_pad_gap(panel, 8, 0);
    if (card)
    {
        lv_obj_set_style_bg_color(panel, lv_color_hex(DAP_UI_CARD_COLOR), 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(panel, 1, 0);
        lv_obj_set_style_border_color(panel, lv_color_hex(DAP_UI_BORDER_COLOR), 0);
        lv_obj_set_style_radius(panel, 8, 0);
    }
    return panel;
}

static lv_obj_t *ui_daplink_create_label(lv_obj_t *parent,
                                          const char *text,
                                          uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *ui_daplink_create_home_button(lv_obj_t *parent)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label;

    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 112, 38);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(DAP_UI_BORDER_COLOR), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(DAP_UI_TEXT_COLOR), 0);
    lv_obj_set_style_bg_color(button,
                              lv_color_hex(DAP_UI_DANGER_COLOR),
                              LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_white(), LV_STATE_PRESSED);

    label = lv_label_create(button);
    lv_label_set_text(label, "返回主页");
    lv_obj_center(label);
    return button;
}

static lv_obj_t *ui_daplink_create_action_button(lv_obj_t *parent, const char *text,
                                                  uint32_t color)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label;
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 100, 34);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xF7FAFC), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xECEFF1), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, lv_color_hex(DAP_UI_MUTED_COLOR), LV_STATE_DISABLED);
    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static const char *ui_daplink_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return (slash != NULL) ? slash + 1 : path;
}

static void ui_daplink_file_selected(const char *path, void *user)
{
    char *destination;
    lv_obj_t *label;
    if ((path == NULL) || (daplink_context.root == NULL)) return;
    if ((uintptr_t)user == 1U)
    {
        destination = daplink_context.firmware_path;
        label = daplink_context.firmware_label;
    }
    else
    {
        destination = daplink_context.flm_path;
        label = daplink_context.flm_label;
    }
    strncpy(destination, path, FILE_SERVICE_PATH_CAP - 1U);
    destination[FILE_SERVICE_PATH_CAP - 1U] = '\0';
    lv_label_set_text(label, ui_daplink_basename(destination));
    lv_obj_set_style_text_color(label, lv_color_hex(DAP_UI_TEXT_COLOR), 0);
    daplink_context.input_error = NULL;
}

/* 本地输入错误持续显示，直到用户修改参数或重新提交。 */
static void ui_daplink_set_input_error(const char *message)
{
    daplink_context.input_error = message;
    lv_label_set_text(daplink_context.programmer_message_label, message);
    lv_obj_set_style_text_color(daplink_context.programmer_message_label,
                                lv_color_hex(DAP_UI_DANGER_COLOR), 0);
}

static void ui_daplink_ram_changed_cb(lv_event_t *event)
{
    (void)event;
    daplink_context.input_error = NULL;
}

/* 逐位解析，拒绝空值、内部 x 和超过32位的数值。 */
static bool ui_daplink_parse_hex(const lv_obj_t *ta, uint32_t *value)
{
    const char *s = lv_textarea_get_text(ta);
    uint32_t result = 0U;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (*s == '\0') return false;
    for (; *s != '\0'; s++)
    {
        uint32_t digit;
        if (*s >= '0' && *s <= '9') digit = (uint32_t)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') digit = (uint32_t)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') digit = (uint32_t)(*s - 'A' + 10);
        else return false;
        if (result > (UINT32_MAX - digit) / 16U) return false;
        result = result * 16U + digit;
    }
    *value = result;
    return true;
}

static void ui_daplink_close_hex_keyboard(void)
{
    if (daplink_context.hex_keyboard_box != NULL)
    {
        lv_obj_t *ta = lv_keyboard_get_textarea(daplink_context.hex_keyboard);
        lv_obj_set_style_border_color(ta, lv_color_hex(DAP_UI_BORDER_COLOR), 0);
        lv_obj_del(daplink_context.hex_keyboard_box);
        daplink_context.hex_keyboard_box = NULL;
        daplink_context.hex_keyboard = NULL;
        daplink_context.hex_keyboard_title = NULL;
    }
}

static void ui_daplink_hex_keyboard_close_cb(lv_event_t *event)
{
    (void)event;
    ui_daplink_close_hex_keyboard();
}

static void ui_daplink_style_close_button(lv_obj_t *button)
{
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 36, 38);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(DAP_UI_BORDER_COLOR), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(DAP_UI_TEXT_COLOR), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(DAP_UI_DANGER_COLOR),
                              LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_white(), LV_STATE_PRESSED);
}

static void ui_daplink_hex_keyboard_drag_cb(lv_event_t *event)
{
    lv_obj_t *box = lv_event_get_target(event);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t vector;

    if (indev == NULL) return;
    lv_indev_get_vect(indev, &vector);
    lv_coord_t x = lv_obj_get_x(box) + vector.x;
    lv_coord_t y = lv_obj_get_y(box) + vector.y;
    lv_coord_t max_x = lv_obj_get_content_width(lv_obj_get_parent(box)) - lv_obj_get_width(box);
    lv_coord_t max_y = lv_obj_get_content_height(lv_obj_get_parent(box)) - lv_obj_get_height(box);
    if (x > max_x) x = max_x;
    if (y > max_y) y = max_y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    lv_obj_set_pos(box, x, y);
}

static void ui_daplink_create_hex_keyboard(lv_obj_t *ta)
{
    static const char *hex_map[] =
    {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "\n",
        "x", "A", "B", "C", "D", "E", "F",
        LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_BACKSPACE, ""
    };
    static const lv_btnmatrix_ctrl_t hex_ctrl[] =
    {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1,
        LV_KEYBOARD_CTRL_BTN_FLAGS | 1,
        LV_KEYBOARD_CTRL_BTN_FLAGS | 1,
        LV_KEYBOARD_CTRL_BTN_FLAGS | 2
    };
    lv_obj_t *box;
    lv_obj_t *kb;
    lv_obj_t *title;
    lv_obj_t *drag_label;
    lv_obj_t *close_button;
    lv_obj_t *close_label;
    lv_area_t screen_area;
    lv_area_t ta_area;
    lv_coord_t box_width;
    lv_coord_t box_height = 180;
    lv_coord_t box_x;
    lv_coord_t box_y;

    lv_obj_get_coords(lv_scr_act(), &screen_area);
    lv_obj_get_coords(ta, &ta_area);
    box_width = lv_obj_get_width(lv_scr_act()) - 24;
    if (box_width > 460) box_width = 460;
    box_x = (ta_area.x1 + ta_area.x2 - box_width) / 2;
    box_y = ta_area.y1 - box_height - 8;
    if (box_x < screen_area.x1 + 12) box_x = screen_area.x1 + 12;
    if ((box_x + box_width) > (screen_area.x2 - 12))
        box_x = screen_area.x2 - 12 - box_width;
    if (box_y < screen_area.y1 + 12) box_y = screen_area.y1 + 12;

    box = lv_obj_create(lv_scr_act());
    lv_obj_add_flag(box, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(box, box_width, box_height);
    lv_obj_set_pos(box, box_x - screen_area.x1, box_y - screen_area.y1);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(box, &ui_font_chinese_24_4, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(box, ui_daplink_hex_keyboard_drag_cb, LV_EVENT_PRESSING, NULL);
    title = lv_label_create(box);
    lv_label_set_text(title, "");
    daplink_context.hex_keyboard_title = title;
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 6);
    drag_label = lv_label_create(box);
    lv_label_set_text(drag_label, "按压拖动");
    lv_obj_set_style_text_color(drag_label, lv_color_hex(DAP_UI_MUTED_COLOR), 0);
    lv_obj_set_style_text_opa(drag_label, LV_OPA_70, 0);
    lv_obj_align(drag_label, LV_ALIGN_TOP_MID, 0, 6);
    close_button = lv_btn_create(box);
    ui_daplink_style_close_button(close_button);
    lv_obj_align(close_button, LV_ALIGN_TOP_RIGHT, 0, 1);
    lv_obj_add_event_cb(close_button, ui_daplink_hex_keyboard_close_cb,
                        LV_EVENT_CLICKED, NULL);
    close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);
    kb = lv_keyboard_create(box);
    lv_obj_set_size(kb, lv_pct(100), 112);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_font(kb, &ui_font_chinese_24_4, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(kb, 2, 0);
    lv_obj_set_style_pad_row(kb, 3, 0);
    lv_obj_set_style_pad_column(kb, 3, 0);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, hex_map, hex_ctrl);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
    lv_keyboard_set_textarea(kb, ta);
    /* 字符键与编辑键分色，保留 LVGL 默认的输入和光标处理。 */
    lv_obj_set_style_bg_color(kb, lv_color_hex(DAP_UI_CARD_COLOR), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0xEAF0F7), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, lv_color_hex(DAP_UI_PRIMARY_COLOR), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS | LV_STATE_PRESSED);
    daplink_context.hex_keyboard_box = box;
    daplink_context.hex_keyboard = kb;
    lv_obj_move_foreground(box);
}

/* 已打开时只切换绑定，输入实时保留，关闭按钮仅收起键盘。 */
static void ui_daplink_ram_field_cb(lv_event_t *event)
{
    lv_obj_t *ta = lv_event_get_target(event);
    if (daplink_context.hex_keyboard == NULL) ui_daplink_create_hex_keyboard(ta);
    else
    {
        lv_obj_t *old = lv_keyboard_get_textarea(daplink_context.hex_keyboard);
        lv_obj_set_style_border_color(old, lv_color_hex(DAP_UI_BORDER_COLOR), 0);
        lv_keyboard_set_textarea(daplink_context.hex_keyboard, ta);
    }
    lv_label_set_text(daplink_context.hex_keyboard_title,
        ta == daplink_context.ram_start_ta ? "RAM 起始地址" : "RAM 大小");
    lv_obj_set_style_border_color(ta, lv_color_hex(DAP_UI_PRIMARY_COLOR), 0);
}

static lv_obj_t *ui_daplink_create_ram_field(lv_obj_t *parent, uint8_t column,
                                            uint32_t value)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    char initial[16];
    lv_obj_set_grid_cell(ta, LV_GRID_ALIGN_STRETCH, column, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_style_pad_all(ta, 2, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(DAP_UI_BORDER_COLOR), 0);
    lv_obj_set_style_radius(ta, 6, 0);
    lv_obj_set_style_bg_color(ta, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(DAP_UI_TEXT_COLOR), 0);
    lv_textarea_set_one_line(ta, true);
    /* 单行模式会重设高度，因此统一尺寸要在它之后指定。 */
    lv_obj_set_height(ta, 36);
    lv_textarea_set_max_length(ta, 10);
    lv_textarea_set_accepted_chars(ta, "0123456789abcdefABCDEFxX");
    snprintf(initial, sizeof(initial), "0x%08lX", (unsigned long)value);
    lv_textarea_set_text(ta, initial);
    lv_obj_add_event_cb(ta, ui_daplink_ram_field_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ta, ui_daplink_ram_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return ta;
}

static void ui_daplink_result_deleted_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) == daplink_context.result_dialog)
        daplink_context.result_dialog = NULL;
}

static void ui_daplink_close_result(void)
{
    if (daplink_context.result_dialog != NULL)
        lv_obj_del(daplink_context.result_dialog);
}

static void ui_daplink_result_close_cb(lv_event_t *event)
{
    (void)event;
    ui_daplink_close_result();
}

/* 标签和数值各占固定一列，不用空格拼排版。 */
static void ui_daplink_result_field(lv_obj_t *table, uint8_t row,
                                    const char *caption, const char *value,
                                    uint32_t value_color)
{
    lv_obj_t *label = ui_daplink_create_label(table, caption, DAP_UI_MUTED_COLOR);
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1,
                         LV_GRID_ALIGN_CENTER, row, 1);
    label = ui_daplink_create_label(table, value, value_color);
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_STRETCH, 1, 1,
                         LV_GRID_ALIGN_CENTER, row, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

static lv_obj_t *ui_daplink_result_table(lv_obj_t *parent, bool ram)
{
    static const lv_coord_t cols[] = {128, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t firmware_rows[] = {32, 32, 32, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t ram_rows[] = {32, 32, LV_GRID_TEMPLATE_LAST};
    lv_obj_t *table = ui_daplink_create_panel(parent, false);
    lv_obj_set_size(table, lv_pct(100), ram ? 68 : 104);
    lv_obj_set_style_pad_row(table, 4, 0);
    lv_obj_set_style_pad_column(table, 12, 0);
    lv_obj_set_grid_dsc_array(table, cols, ram ? ram_rows : firmware_rows);
    return table;
}

static void ui_daplink_format_size(char *text, size_t capacity, uint32_t bytes,
                                   bool hexadecimal)
{
    /* 整数运算显示一位小数，同时保留精确字节数或十六进制值。 */
    uint32_t tenths = (uint32_t)(((uint64_t)bytes * 10U + 512U) / 1024U);
    if (hexadecimal)
        snprintf(text, capacity, "%lu.%lu KiB (0x%lX)",
                 (unsigned long)(tenths / 10U), (unsigned long)(tenths % 10U),
                 (unsigned long)bytes);
    else
        snprintf(text, capacity, "%lu.%lu KiB (%lu B)",
                 (unsigned long)(tenths / 10U), (unsigned long)(tenths % 10U),
                 (unsigned long)bytes);
}

/* 所有详情共用同一模态框架，保证标题、遮罩和关闭按钮保持一致。 */
static lv_obj_t *ui_daplink_create_info_dialog(const char *title, bool success)
{
    lv_obj_t *overlay;
    lv_obj_t *box;
    lv_obj_t *header;
    lv_obj_t *table;
    lv_obj_t *label;
    lv_obj_t *button;

    overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_40, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    daplink_context.result_dialog = overlay;
    lv_obj_add_event_cb(overlay, ui_daplink_result_deleted_cb, LV_EVENT_DELETE, NULL);
    ui_daplink_close_hex_keyboard();

    box = ui_daplink_create_panel(overlay, true);
    lv_obj_set_size(box, 560, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(box, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_pad_all(box, 16, 0);
    lv_obj_set_style_pad_row(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

    header = ui_daplink_create_panel(box, false);
    lv_obj_set_size(header, lv_pct(100), 38);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    label = ui_daplink_create_label(header, title, DAP_UI_TITLE_COLOR);
    lv_obj_set_flex_grow(label, 1);
    if (success)
        (void)ui_daplink_create_label(header, "烧录成功", DAP_UI_SUCCESS_COLOR);
    button = lv_btn_create(header);
    ui_daplink_style_close_button(button);
    lv_obj_add_event_cb(button, ui_daplink_result_close_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, LV_SYMBOL_CLOSE);
    lv_obj_center(label);

    table = ui_daplink_create_panel(box, false);
    lv_obj_set_size(table, lv_pct(100), 1);
    lv_obj_set_style_bg_color(table, lv_color_hex(DAP_UI_BORDER_COLOR), 0);
    lv_obj_set_style_bg_opa(table, LV_OPA_COVER, 0);
    lv_obj_center(box);
    return box;
}

static void ui_daplink_finish_info_dialog(lv_obj_t *box)
{
    lv_obj_t *footer = ui_daplink_create_panel(box, false);
    lv_obj_t *button;
    lv_obj_t *label;
    lv_obj_set_size(footer, lv_pct(100), 38);
    button = lv_btn_create(footer);
    ui_daplink_style_close_button(button);
    lv_obj_set_width(button, 112);
    lv_obj_align(button, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(button, ui_daplink_result_close_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(button);
    lv_label_set_text(label, "关闭");
    lv_obj_center(label);
}

/* 显示点击时的烧录快照；遮罩独立于页面，退出页面时必须一并释放。 */
static void ui_daplink_result_clicked(lv_event_t *event)
{
    dap_programmer_status_t st;
    char text[64];
    lv_obj_t *box;
    lv_obj_t *table;
    (void)event;
    if (daplink_context.result_dialog != NULL) return;
    if (!dap_programmer_get_status(&st) || (st.firmware_bytes == 0U)) return;
    box = ui_daplink_create_info_dialog("烧录信息", st.state == DAP_PROGRAMMER_SUCCESS);

    table = ui_daplink_result_table(box, false);
    ui_daplink_format_size(text, sizeof(text), st.firmware_bytes, false);
    ui_daplink_result_field(table, 0, "固件大小", text, DAP_UI_TEXT_COLOR);
    snprintf(text, sizeof(text), "0x%08lX", (unsigned long)st.firmware_start);
    ui_daplink_result_field(table, 1, "起始地址", text, DAP_UI_PRIMARY_COLOR);
    snprintf(text, sizeof(text), "0x%08lX",
             (unsigned long)(st.firmware_end != 0U ? st.firmware_end - 1U : 0U));
    ui_daplink_result_field(table, 2, "结束地址", text, DAP_UI_PRIMARY_COLOR);

    (void)ui_daplink_create_label(box, "算法 RAM", DAP_UI_TEXT_COLOR);
    table = ui_daplink_result_table(box, true);
    snprintf(text, sizeof(text), "0x%08lX", (unsigned long)st.ram_start);
    ui_daplink_result_field(table, 0, "起始地址", text, DAP_UI_PRIMARY_COLOR);
    ui_daplink_format_size(text, sizeof(text), st.ram_size, true);
    ui_daplink_result_field(table, 1, "可用大小", text, DAP_UI_TEXT_COLOR);

    ui_daplink_finish_info_dialog(box);
}

static void ui_daplink_local_button_cb(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_current_target(event);
    if (target == daplink_context.firmware_button)
        ui_file_manager_create_picker(lv_scr_act(), "SD:/DAP/Firmware", ".hex|.bin",
                                      ui_daplink_file_selected, (void *)(uintptr_t)1U);
    else if (target == daplink_context.flm_button)
        ui_file_manager_create_picker(lv_scr_act(), "SD:/DAP/Algorithms", ".flm",
                                      ui_daplink_file_selected, (void *)(uintptr_t)2U);
    else if (target == daplink_context.start_button)
    {
        daplink_context.input_error = NULL;
        uint16_t selected = lv_dropdown_get_selected(daplink_context.clock_dropdown);
        if (selected >= sizeof(local_clock_rates) / sizeof(local_clock_rates[0])) return;
        uint8_t erase_mode = (lv_dropdown_get_selected(daplink_context.erase_dropdown) == 0U) ?
                             DAP_ERASE_SECTOR : DAP_ERASE_CHIP;
        uint32_t ram_start;
        uint32_t ram_size;
        if (!ui_daplink_parse_hex(daplink_context.ram_start_ta, &ram_start) ||
            (ram_start & 7U) != 0U)
        {
            ui_daplink_set_input_error("RAM 起始地址无效 (8 B)");
            return;
        }
        if (!ui_daplink_parse_hex(daplink_context.ram_size_ta, &ram_size))
        {
            ui_daplink_set_input_error("RAM 大小无效");
            return;
        }
        if (ram_size < 256U)
        {
            ui_daplink_set_input_error("RAM Size 至少 0x100");
            return;
        }
        if (ram_start > UINT32_MAX - ram_size)
        {
            ui_daplink_set_input_error("RAM 地址范围无效");
            return;
        }
        ui_daplink_close_hex_keyboard();
        if (!dap_programmer_request_with_ram(daplink_context.firmware_path,
                                             daplink_context.flm_path,
                                             local_clock_rates[selected],
                                             erase_mode, ram_start, ram_size))
            ui_daplink_set_input_error("无法提交：烧录器忙或路径无效");
    }
    else if (target == daplink_context.cancel_button)
        dap_programmer_cancel();
}

static void ui_daplink_activity_anim(void *bar, int32_t value)
{
    int32_t start = (value < 0) ? 0 : value;
    int32_t end = (value + 25 > 100) ? 100 : (value + 25);

    lv_bar_set_value(bar, end, LV_ANIM_OFF);
    lv_bar_set_start_value(bar, start, LV_ANIM_OFF);
}

static void ui_daplink_set_activity(bool active)
{
    lv_anim_t animation;

    if (daplink_context.activity_active == active)
    {
        return;
    }
    daplink_context.activity_active = active;
    if (active)
    {
        lv_label_set_text(daplink_context.activity_label, "通信状态 通信中");
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, daplink_context.progress_bar);
        lv_anim_set_exec_cb(&animation, ui_daplink_activity_anim);
        lv_anim_set_values(&animation, -25, 100);
        lv_anim_set_time(&animation, 1000);
        lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&animation);
    }
    else
    {
        lv_anim_del(daplink_context.progress_bar, ui_daplink_activity_anim);
        lv_bar_set_start_value(daplink_context.progress_bar, 0, LV_ANIM_OFF);
        lv_bar_set_value(daplink_context.progress_bar, 0, LV_ANIM_OFF);
        lv_label_set_text(daplink_context.activity_label, "通信状态 等待命令");
    }
}

static void ui_daplink_set_rate_text(uint32_t bytes_per_second)
{
    if (bytes_per_second >= 1024U)
    {
        uint32_t value_tenths = (bytes_per_second * 10U) / 1024U;
        lv_label_set_text_fmt(daplink_context.rate_label,
                              "实时速率 %lu.%lu KiB/s",
                              (unsigned long)(value_tenths / 10U),
                              (unsigned long)(value_tenths % 10U));
    }
    else
    {
        lv_label_set_text_fmt(daplink_context.rate_label,
                              "实时速率 %lu B/s",
                              (unsigned long)bytes_per_second);
    }
}

static void ui_daplink_update_rate(const dap_service_status_t *status, uint32_t now)
{
    uint32_t elapsed;
    uint32_t transferred;

    if (!daplink_context.rate_sample_valid)
    {
        daplink_context.rate_sample_tick = now;
        daplink_context.rate_sample_bytes = status->communication_bytes;
        daplink_context.rate_sample_valid = true;
        ui_daplink_set_rate_text(0U);
        return;
    }
    if (!status->usb_configured)
    {
        daplink_context.rate_sample_tick = now;
        daplink_context.rate_sample_bytes = status->communication_bytes;
        daplink_context.rate_bytes_per_second = 0U;
        ui_daplink_set_rate_text(0U);
        return;
    }

    elapsed = lv_tick_elaps(daplink_context.rate_sample_tick);
    if (elapsed < DAP_UI_RATE_SAMPLE_MS)
    {
        return;
    }
    transferred = status->communication_bytes - daplink_context.rate_sample_bytes;
    daplink_context.rate_bytes_per_second =
        (uint32_t)(((uint64_t)transferred * 1000U) / elapsed);
    daplink_context.rate_sample_tick = now;
    daplink_context.rate_sample_bytes = status->communication_bytes;
    ui_daplink_set_rate_text(daplink_context.rate_bytes_per_second);
}


/* 两个页签只改变可见性，后台任务和状态快照始终独立于页面。 */
static void ui_daplink_select_tab(bool local)
{
    daplink_context.local_selected = local;
    ui_daplink_close_hex_keyboard();
    if (local)
    {
        lv_obj_add_flag(daplink_context.usb_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(daplink_context.local_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(daplink_context.local_tab, LV_STATE_CHECKED);
        lv_obj_clear_state(daplink_context.usb_tab, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_add_flag(daplink_context.local_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(daplink_context.usb_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(daplink_context.usb_tab, LV_STATE_CHECKED);
        lv_obj_clear_state(daplink_context.local_tab, LV_STATE_CHECKED);
    }
}

static void ui_daplink_tab_cb(lv_event_t *event)
{
    ui_daplink_select_tab(lv_event_get_current_target(event) == daplink_context.local_tab);
}

static void ui_daplink_path_cb(lv_event_t *event)
{
    bool firmware = lv_event_get_current_target(event) == daplink_context.firmware_label;
    const char *path = firmware ? daplink_context.firmware_path : daplink_context.flm_path;
    lv_obj_t *box;
    lv_obj_t *body;
    lv_obj_t *label;
    if (path[0] == '\0' || daplink_context.result_dialog != NULL) return;
    box = ui_daplink_create_info_dialog(firmware ? "固件路径" : "算法路径", false);
    (void)ui_daplink_create_label(box, "文件路径", DAP_UI_MUTED_COLOR);

    /* 长路径自动换行，超过正文高度时仅正文滚动，关闭入口始终可见。 */
    body = ui_daplink_create_panel(box, false);
    lv_obj_set_size(body, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(body, 192, 0);
    lv_obj_set_style_pad_all(body, 12, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0xF0F4F8), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(body, 6, 0);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    label = ui_daplink_create_label(body, path, DAP_UI_TEXT_COLOR);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    ui_daplink_finish_info_dialog(box);
}

static void ui_daplink_disable(lv_obj_t *object, bool disabled)
{
    if (disabled) lv_obj_add_state(object, LV_STATE_DISABLED);
    else lv_obj_clear_state(object, LV_STATE_DISABLED);
}

static void ui_daplink_update_local(const dap_programmer_status_t *programmer)
{
    bool busy = programmer->active || programmer->pending;
    bool ready = daplink_context.firmware_path[0] != '\0' &&
                 daplink_context.flm_path[0] != '\0';
    uint32_t color = DAP_UI_MUTED_COLOR;
    uint32_t percent = programmer->progress_percent;
    const char *message = programmer->message;

    lv_label_set_text_fmt(daplink_context.elapsed_label, "耗时 %02lu:%02lu.%lu",
        (unsigned long)(programmer->elapsed_ms / 60000U),
        (unsigned long)((programmer->elapsed_ms / 1000U) % 60U),
        (unsigned long)((programmer->elapsed_ms / 100U) % 10U));
    for (uint32_t i = 0U; i < 3U; i++)
    {
        dap_programmer_state_t stage = (dap_programmer_state_t)(DAP_PROGRAMMER_ERASING + i);
        uint32_t stage_color = DAP_UI_MUTED_COLOR;
        uint32_t stage_background = 0xF0F4F8;
        if (!programmer->pending)
        {
            if (programmer->state == DAP_PROGRAMMER_SUCCESS || programmer->operation_stage > stage)
            {
                stage_color = DAP_UI_SUCCESS_COLOR;
                stage_background = 0xEAF7EE;
            }
            else if (programmer->operation_stage == stage)
            {
                if (programmer->state == DAP_PROGRAMMER_FAILED)
                {
                    stage_color = DAP_UI_DANGER_COLOR;
                    stage_background = 0xFFF4F5;
                }
                else if (programmer->state == DAP_PROGRAMMER_CANCELLED)
                {
                    stage_color = DAP_UI_WARNING_COLOR;
                    stage_background = 0xFFF3E6;
                }
                else
                {
                    stage_color = DAP_UI_PRIMARY_COLOR;
                    stage_background = 0xE6F0FF;
                }
            }
        }
        lv_obj_set_style_text_color(daplink_context.stage_labels[i], lv_color_hex(stage_color), 0);
        lv_obj_set_style_bg_color(daplink_context.stage_labels[i], lv_color_hex(stage_background), 0);
    }

    if (programmer->state == DAP_PROGRAMMER_SUCCESS)
    {
        color = DAP_UI_SUCCESS_COLOR;
        percent = 100U;
    }
    else if (programmer->state == DAP_PROGRAMMER_FAILED) color = DAP_UI_DANGER_COLOR;
    else if (programmer->state == DAP_PROGRAMMER_CANCELLED) color = DAP_UI_WARNING_COLOR;
    else if (busy) color = DAP_UI_PRIMARY_COLOR;

    if (programmer->pending)
    {
        percent = 0U;
        lv_label_set_text(daplink_context.programmer_state_label, "等待烧录");
        message = "正在准备烧录";
    }
    else if (programmer->cancel_requested && busy)
        lv_label_set_text(daplink_context.programmer_state_label, "正在取消");
    /* 百分比属于当前阶段，切换阶段时归零不代表整体进度倒退。 */
    else if (programmer->total != 0U && programmer->active &&
             programmer->state >= DAP_PROGRAMMER_ERASING &&
             programmer->state <= DAP_PROGRAMMER_VERIFYING)
        lv_label_set_text_fmt(daplink_context.programmer_state_label,
                              "%s %lu%%",
                              dap_programmer_state_text(programmer->state),
                              (unsigned long)percent);
    else if (programmer->state == DAP_PROGRAMMER_SUCCESS)
        lv_label_set_text(daplink_context.programmer_state_label, "烧录成功 100%");
    else
        lv_label_set_text(daplink_context.programmer_state_label,
                          dap_programmer_state_text(programmer->state));

    if (programmer->state == DAP_PROGRAMMER_IDLE && !busy)
        message = ready ? "可以开始烧录" : "请选择 HEX/BIN 固件和 FLM 算法";
    if (busy) daplink_context.input_error = NULL;
    if (daplink_context.input_error != NULL) message = daplink_context.input_error;
    lv_label_set_text(daplink_context.programmer_message_label, message);
    lv_obj_set_style_text_color(daplink_context.programmer_message_label,
        lv_color_hex((daplink_context.input_error != NULL ||
                      programmer->state == DAP_PROGRAMMER_FAILED) ? DAP_UI_DANGER_COLOR :
                     DAP_UI_MUTED_COLOR), 0);
    lv_obj_set_style_text_color(daplink_context.programmer_state_label, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(daplink_context.local_bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_bar_set_value(daplink_context.local_bar, percent, LV_ANIM_OFF);
    ui_daplink_disable(daplink_context.firmware_button, busy);
    ui_daplink_disable(daplink_context.flm_button, busy);
    ui_daplink_disable(daplink_context.clock_dropdown, busy);
    ui_daplink_disable(daplink_context.erase_dropdown, busy);
    if (busy) ui_daplink_close_hex_keyboard();
    ui_daplink_disable(daplink_context.ram_start_ta, busy);
    ui_daplink_disable(daplink_context.ram_size_ta, busy);
    ui_daplink_disable(daplink_context.start_button, busy || !ready);
    ui_daplink_disable(daplink_context.cancel_button, !busy || programmer->cancel_requested);
    ui_daplink_disable(daplink_context.details_button, programmer->firmware_bytes == 0U);

    /* 只呈现已有快照；历史识别结果不等同于当前连接，不能持续显示绿色。 */
    uint32_t target_color = DAP_UI_MUTED_COLOR;
    const char *target_text = "目标未连接";
    if (programmer->dpidr != 0U)
    {
        target_text = busy ? "目标已识别" : "上次识别";
        if (busy) target_color = DAP_UI_SUCCESS_COLOR;
    }
    else if (programmer->active && programmer->state >= DAP_PROGRAMMER_CONNECTING &&
             programmer->state <= DAP_PROGRAMMER_RESETTING)
    {
        target_text = "正在连接目标";
        target_color = DAP_UI_PRIMARY_COLOR;
    }
    else if (programmer->state == DAP_PROGRAMMER_FAILED && programmer->firmware_bytes != 0U)
    {
        target_text = "目标识别失败";
        target_color = DAP_UI_DANGER_COLOR;
    }
    lv_label_set_text(daplink_context.local_target_label, target_text);
    lv_obj_set_style_text_color(daplink_context.local_target_label, lv_color_hex(target_color), 0);
    lv_obj_set_style_bg_color(daplink_context.local_target_dot, lv_color_hex(target_color), 0);
    if (programmer->dpidr != 0U)
    {
        lv_label_set_text_fmt(daplink_context.local_id_label, "0x%08lX",
                              (unsigned long)programmer->dpidr);
        lv_obj_set_style_text_color(daplink_context.local_id_label,
                                    lv_color_hex(DAP_UI_TEXT_COLOR), 0);
    }
    else
    {
        lv_label_set_text(daplink_context.local_id_label, "--");
        lv_obj_set_style_text_color(daplink_context.local_id_label,
                                    lv_color_hex(DAP_UI_MUTED_COLOR), 0);
    }
}

static void ui_daplink_set_clock(lv_obj_t *label, uint32_t hz)
{
    if (hz >= 1000000U)
    {
        uint32_t fraction = (hz % 1000000U) / 1000U;
        if (fraction == 0U)
            lv_label_set_text_fmt(label, "SWD 时钟 %lu MHz",
                                  (unsigned long)(hz / 1000000U));
        else
        {
            char decimals[4];
            int end = 2;
            (void)snprintf(decimals, sizeof(decimals), "%03lu", (unsigned long)fraction);
            while (end > 0 && decimals[end] == '0') decimals[end--] = '\0';
            lv_label_set_text_fmt(label, "SWD 时钟 %lu.%s MHz",
                                  (unsigned long)(hz / 1000000U), decimals);
        }
    }
    else
        lv_label_set_text_fmt(label, "SWD 时钟 %lu kHz",
                              (unsigned long)(hz / 1000U));
}

static void ui_daplink_update(lv_timer_t *timer)
{
    dap_service_status_t status;
    dap_programmer_status_t programmer;
    uint32_t now;
    (void)timer;
    if (!dap_service_get_status(&status) || !dap_programmer_get_status(&programmer)) return;
    ui_daplink_update_local(&programmer);
    now = lv_tick_get();
    ui_daplink_update_rate(&status, now);
    if (status.command_active || (daplink_context.status_valid &&
        status.command_count != daplink_context.status.command_count))
    {
        daplink_context.last_activity_tick = now;
        daplink_context.activity_seen = true;
    }
    ui_daplink_set_activity(!status.local_active && !daplink_context.local_selected &&
        status.usb_configured && daplink_context.activity_seen &&
        (status.command_active ||
         lv_tick_elaps(daplink_context.last_activity_tick) < DAP_UI_ACTIVITY_HOLD_MS));
    daplink_context.status = status;
    daplink_context.status_valid = true;

    lv_label_set_text(daplink_context.usb_status_label, !status.initialized ?
        "USB 初始化失败" : (status.usb_configured ? "USB 已连接" : "USB 等待连接"));
    lv_obj_set_style_text_color(daplink_context.usb_status_label,
        lv_color_hex(status.usb_configured ? DAP_UI_SUCCESS_COLOR : DAP_UI_WARNING_COLOR), 0);
    lv_label_set_text(daplink_context.port_status_label, status.local_active ?
        "本地烧录模式" : (status.debug_port == 1U ? "SWD 已连接" : "SWD 未连接"));
    ui_daplink_set_clock(daplink_context.clock_label, status.swd_clock_hz);
    lv_label_set_text_fmt(daplink_context.duration_label, "连接时长 %02lu:%02lu.%lu",
        (unsigned long)(status.connection_duration_ms / 60000U),
        (unsigned long)((status.connection_duration_ms / 1000U) % 60U),
        (unsigned long)((status.connection_duration_ms / 100U) % 10U));
    if (status.dpidr_valid)
        lv_label_set_text_fmt(daplink_context.dpidr_label, "#000000 IDCODE 0x%08lX#",
                              (unsigned long)status.dpidr);
    else lv_label_set_text(daplink_context.dpidr_label, "#000000 IDCODE# #69788B --#");
    lv_label_set_text_fmt(daplink_context.success_count_label, "成功命令数 %lu",
                          (unsigned long)status.success_count);
    lv_label_set_text_fmt(daplink_context.failure_count_label, "失败命令数 %lu",
                          (unsigned long)status.failure_count);
}

static void ui_daplink_root_deleted_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) != daplink_context.root) return;
    ui_daplink_close_result();
    ui_daplink_close_hex_keyboard();
    if (daplink_context.refresh_timer != NULL) lv_timer_del(daplink_context.refresh_timer);
    lv_anim_del(daplink_context.progress_bar, ui_daplink_activity_anim);
    memset(&daplink_context, 0, sizeof(daplink_context));
}

static void ui_daplink_home_button_cb(lv_event_t *event)
{
    (void)event;
    if (daplink_context.root != NULL) lv_obj_del(daplink_context.root);
}

static void ui_daplink_grid_cell(lv_obj_t *object, uint8_t col, uint8_t row, uint8_t span)
{
    lv_obj_set_grid_cell(object, LV_GRID_ALIGN_STRETCH, col, span,
                         LV_GRID_ALIGN_STRETCH, row, 1);
}

static void ui_daplink_column(lv_obj_t *object)
{
    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(object, 10, 0);
    lv_obj_set_flex_align(object, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

/* 标题留在卡片顶端，仅内容区参与剩余空间的均匀分布。 */
static lv_obj_t *ui_daplink_card_body(lv_obj_t *card, const char *title)
{
    lv_obj_t *heading;
    lv_obj_t *accent;
    lv_obj_t *body;

    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 8, 0);

    /* 深色标题配一条短蓝色引导线，突出分区但不抢占应用标题的视觉层级。 */
    heading = ui_daplink_create_panel(card, false);
    lv_obj_set_size(heading, lv_pct(100), 30);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(heading, 8, 0);
    accent = ui_daplink_create_panel(heading, false);
    lv_obj_set_size(accent, 4, 22);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(DAP_UI_PRIMARY_COLOR), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    (void)ui_daplink_create_label(heading, title, DAP_UI_TEXT_COLOR);

    body = ui_daplink_create_panel(card, false);
    lv_obj_set_size(body, lv_pct(100), 0);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_row(body, 2, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return body;
}

static lv_obj_t *ui_daplink_bar(lv_obj_t *parent, bool activity)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, lv_pct(100), 12);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_mode(bar, activity ? LV_BAR_MODE_RANGE : LV_BAR_MODE_NORMAL);
    lv_obj_set_style_bg_color(bar, lv_color_hex(DAP_UI_PROGRESS_BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(DAP_UI_PRIMARY_COLOR), LV_PART_INDICATOR);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    return bar;
}

static void ui_daplink_file_row(lv_obj_t *parent, bool firmware)
{
    static const lv_coord_t columns[] = {56, LV_GRID_FR(1), 88, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t rows[] = {36, LV_GRID_TEMPLATE_LAST};
    lv_obj_t *row = ui_daplink_create_panel(parent, false);
    lv_obj_t *label;
    lv_obj_t *button;
    lv_obj_set_size(row, lv_pct(100), 36);
    lv_obj_set_grid_dsc_array(row, columns, rows);
    label = ui_daplink_create_label(row, firmware ? "固件" : "算法", DAP_UI_MUTED_COLOR);
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    label = ui_daplink_create_label(row,
        firmware ? "请选择 HEX/BIN 固件" : "请选择 FLM 算法", DAP_UI_MUTED_COLOR);
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(label, 36);
    lv_obj_set_style_bg_color(label, lv_color_hex(0xF0F4F8), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(label, 6, 0);
    lv_obj_set_style_pad_hor(label, 8, 0);
    lv_obj_set_style_pad_ver(label, 3, 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(0xE6F0FF), LV_STATE_PRESSED);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, ui_daplink_path_cb, LV_EVENT_CLICKED, NULL);
    button = ui_daplink_create_action_button(row, "选择", DAP_UI_PRIMARY_COLOR);
    lv_obj_set_height(button, 36);
    lv_obj_set_style_bg_color(button, lv_color_white(), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0xBBDDFC), 0);
    ui_daplink_grid_cell(button, 2, 0, 1);
    lv_obj_add_event_cb(button, ui_daplink_local_button_cb, LV_EVENT_CLICKED, NULL);
    if (firmware)
    {
        daplink_context.firmware_label = label;
        daplink_context.firmware_button = button;
    }
    else
    {
        daplink_context.flm_label = label;
        daplink_context.flm_button = button;
    }
}

void ui_daplink_create(lv_obj_t *parent)
{
    static const lv_coord_t one_col[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t root_rows[] = {42, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t header_cols[] = {180, LV_GRID_FR(1), 180,
                                              LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t header_rows[] = {42, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t usb_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t usb_rows[] = {102, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t local_cols[] = {LV_GRID_FR(1), DAP_UI_TARGET_WIDTH, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t local_rows[] = {LV_GRID_FR(1), 152, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t progress_rows[] = {30, 8, 30, 38, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t overview_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), 270,
                                               LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t overview_rows[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t action_cols[] = {128, LV_GRID_FR(1), DAP_UI_START_WIDTH,
                                             DAP_UI_CANCEL_WIDTH, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t action_rows[] = {38, LV_GRID_TEMPLATE_LAST};
    /* 详情64px + 间距 + 耗时148px，与下方112px + 间距 + 100px等宽。 */
    static const lv_coord_t stage_cols[] = {240, LV_GRID_FR(1), 64,
        DAP_UI_START_WIDTH + DAP_UI_CANCEL_WIDTH - 64, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t stage_rows[] = {30, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t field_cols[] = {70, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t field_rows[] = {36, LV_GRID_TEMPLATE_LAST};
    lv_obj_t *header, *home, *tabs, *overview, *card, *row;
    dap_programmer_status_t programmer;
    bool restore;

    if (daplink_context.root != NULL)
    {
        lv_obj_move_foreground(daplink_context.root);
        return;
    }
    restore = dap_programmer_get_status(&programmer) &&
              programmer.firmware_path[0] != '\0';
    daplink_context.root = ui_daplink_create_panel(parent, false);
    lv_obj_set_size(daplink_context.root, lv_pct(100), lv_pct(92));
    lv_obj_set_y(daplink_context.root, lv_pct(8));
    lv_obj_set_style_pad_all(daplink_context.root, 8, 0);
    lv_obj_set_style_bg_color(daplink_context.root, lv_color_hex(DAP_UI_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(daplink_context.root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(daplink_context.root, &ui_font_chinese_24_4, 0);
    lv_obj_set_grid_dsc_array(daplink_context.root, one_col, root_rows);
    lv_obj_add_event_cb(daplink_context.root, ui_daplink_root_deleted_cb, LV_EVENT_DELETE, NULL);

    header = ui_daplink_create_panel(daplink_context.root, false);
    ui_daplink_grid_cell(header, 0, 0, 1);
    lv_obj_set_style_pad_left(header, 10, 0);
    lv_obj_set_grid_dsc_array(header, header_cols, header_rows);
    {
        lv_obj_t *title = ui_daplink_create_label(header, "DAP Link", DAP_UI_TITLE_COLOR);
        lv_obj_set_grid_cell(title, LV_GRID_ALIGN_START, 0, 1,
                             LV_GRID_ALIGN_CENTER, 0, 1);
    }

    /* 模式切换并入标题栏，减少一整行导航占用，让烧录内容获得更多纵向空间。 */
    tabs = ui_daplink_create_panel(header, false);
    lv_obj_set_size(tabs, 280, 38);
    lv_obj_set_grid_cell(tabs, LV_GRID_ALIGN_CENTER, 1, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_color(tabs, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tabs, 1, 0);
    lv_obj_set_style_border_color(tabs, lv_color_hex(DAP_UI_BORDER_COLOR), 0);
    lv_obj_set_style_radius(tabs, 7, 0);
    lv_obj_set_style_pad_all(tabs, 1, 0);
    lv_obj_set_style_pad_column(tabs, 2, 0);
    daplink_context.usb_tab = ui_daplink_create_action_button(tabs, "USB 调试", DAP_UI_PRIMARY_COLOR);
    daplink_context.local_tab = ui_daplink_create_action_button(tabs, "本地烧录", DAP_UI_PRIMARY_COLOR);
    lv_obj_t *tab_buttons[] = {daplink_context.usb_tab, daplink_context.local_tab};
    for (uint32_t i = 0U; i < 2U; i++)
    {
        lv_obj_set_width(tab_buttons[i], 0);
        lv_obj_set_height(tab_buttons[i], 36);
        lv_obj_set_flex_grow(tab_buttons[i], 1);
        lv_obj_set_style_border_width(tab_buttons[i], 0, 0);
        lv_obj_set_style_bg_opa(tab_buttons[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(tab_buttons[i], LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(tab_buttons[i], lv_color_hex(DAP_UI_PRIMARY_COLOR),
                                  LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(tab_buttons[i], LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(tab_buttons[i], lv_color_white(), LV_STATE_CHECKED);
        lv_obj_add_event_cb(tab_buttons[i], ui_daplink_tab_cb, LV_EVENT_CLICKED, NULL);
    }

    home = ui_daplink_create_home_button(header);
    lv_obj_set_grid_cell(home, LV_GRID_ALIGN_END, 2, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_add_event_cb(home, ui_daplink_home_button_cb, LV_EVENT_CLICKED, NULL);

    daplink_context.usb_page = ui_daplink_create_panel(daplink_context.root, false);
    ui_daplink_grid_cell(daplink_context.usb_page, 0, 1, 1);
    lv_obj_set_grid_dsc_array(daplink_context.usb_page, usb_cols, usb_rows);
    overview = ui_daplink_create_panel(daplink_context.usb_page, true);
    ui_daplink_grid_cell(overview, 0, 0, 2);
    /* 固定各状态所在的列，文本长度变化时IDCODE的左边缘保持不变。 */
    lv_obj_set_grid_dsc_array(overview, overview_cols, overview_rows);
    daplink_context.usb_status_label = ui_daplink_create_label(overview, "USB 等待连接", DAP_UI_PRIMARY_COLOR);
    daplink_context.port_status_label = ui_daplink_create_label(overview, "SWD 未连接", DAP_UI_TEXT_COLOR);
    daplink_context.dpidr_label = ui_daplink_create_label(overview, "IDCODE --", DAP_UI_TEXT_COLOR);
    lv_label_set_recolor(daplink_context.dpidr_label, true);
    lv_obj_set_grid_cell(daplink_context.usb_status_label, LV_GRID_ALIGN_STRETCH,
                         0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(daplink_context.port_status_label, LV_GRID_ALIGN_STRETCH,
                         1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(daplink_context.dpidr_label, LV_GRID_ALIGN_STRETCH,
                         2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_style_text_align(daplink_context.dpidr_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(daplink_context.dpidr_label, LV_LABEL_LONG_CLIP);
    card = ui_daplink_create_panel(daplink_context.usb_page, true);
    ui_daplink_grid_cell(card, 0, 1, 1);
    ui_daplink_column(card);
    (void)ui_daplink_create_label(card, "链路状态", DAP_UI_PRIMARY_COLOR);
    daplink_context.clock_label = ui_daplink_create_label(card, "SWD 时钟 --", DAP_UI_TEXT_COLOR);
    daplink_context.duration_label = ui_daplink_create_label(card, "连接时长 00:00.0", DAP_UI_TEXT_COLOR);
    daplink_context.activity_label = ui_daplink_create_label(card, "通信状态 等待命令", DAP_UI_MUTED_COLOR);
    daplink_context.progress_bar = ui_daplink_bar(card, true);
    card = ui_daplink_create_panel(daplink_context.usb_page, true);
    ui_daplink_grid_cell(card, 1, 1, 1);
    ui_daplink_column(card);
    (void)ui_daplink_create_label(card, "运行统计", DAP_UI_PRIMARY_COLOR);
    daplink_context.success_count_label = ui_daplink_create_label(card, "成功命令数 0", DAP_UI_SUCCESS_COLOR);
    daplink_context.failure_count_label = ui_daplink_create_label(card, "失败命令数 0", DAP_UI_DANGER_COLOR);
    daplink_context.rate_label = ui_daplink_create_label(card, "实时速率 0 B/s", DAP_UI_TEXT_COLOR);

    daplink_context.local_page = ui_daplink_create_panel(daplink_context.root, false);
    ui_daplink_grid_cell(daplink_context.local_page, 0, 1, 1);
    lv_obj_set_style_pad_gap(daplink_context.local_page, 12, 0);
    lv_obj_set_grid_dsc_array(daplink_context.local_page, local_cols, local_rows);
    card = ui_daplink_create_panel(daplink_context.local_page, true);
    ui_daplink_grid_cell(card, 0, 0, 1);
    row = ui_daplink_card_body(card, "烧录文件");
    ui_daplink_file_row(row, true);
    ui_daplink_file_row(row, false);
    {
        /* 算法 RAM 起始地址和大小放在同一行，由用户自定义，默认取目标配置。 */
        /* 首列与“固件/算法”标签列同为56px，确保三个输入框左边缘对齐。 */
        static const lv_coord_t ram_cols[] = {56, LV_GRID_FR(1), 48,
                                              LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
        static const lv_coord_t ram_rows[] = {36, LV_GRID_TEMPLATE_LAST};
        const target_profile_t *profile = target_profile_stm32f103c8();
        lv_obj_t *ram_row;

        ram_row = ui_daplink_create_panel(row, false);
        lv_obj_set_size(ram_row, lv_pct(100), 36);
        lv_obj_set_grid_dsc_array(ram_row, ram_cols, ram_rows);
        {
            lv_obj_t *cap = ui_daplink_create_label(ram_row, "起始", DAP_UI_MUTED_COLOR);
            lv_obj_set_grid_cell(cap, LV_GRID_ALIGN_START, 0, 1,
                                 LV_GRID_ALIGN_CENTER, 0, 1);
        }
        daplink_context.ram_start_ta = ui_daplink_create_ram_field(ram_row, 1, profile->ram_start);
        {
            lv_obj_t *cap = ui_daplink_create_label(ram_row, "大小", DAP_UI_MUTED_COLOR);
            lv_obj_set_grid_cell(cap, LV_GRID_ALIGN_START, 2, 1,
                                 LV_GRID_ALIGN_CENTER, 0, 1);
        }
        daplink_context.ram_size_ta = ui_daplink_create_ram_field(ram_row, 3, profile->ram_size);
    }
    card = ui_daplink_create_panel(daplink_context.local_page, true);
    ui_daplink_grid_cell(card, 1, 0, 1);
    row = ui_daplink_card_body(card, "目标信息");
    {
        static const lv_coord_t status_cols[] = {8, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
        lv_obj_t *status_row = ui_daplink_create_panel(row, false);
        lv_obj_set_size(status_row, lv_pct(100), 36);
        lv_obj_set_style_pad_column(status_row, 10, 0);
        lv_obj_set_grid_dsc_array(status_row, status_cols, field_rows);
        daplink_context.local_target_dot = ui_daplink_create_panel(status_row, false);
        lv_obj_set_size(daplink_context.local_target_dot, 8, 8);
        lv_obj_set_style_radius(daplink_context.local_target_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(daplink_context.local_target_dot, LV_OPA_COVER, 0);
        lv_obj_set_grid_cell(daplink_context.local_target_dot, LV_GRID_ALIGN_CENTER, 0, 1,
                             LV_GRID_ALIGN_CENTER, 0, 1);
        daplink_context.local_target_label = ui_daplink_create_label(status_row, "目标未连接", DAP_UI_MUTED_COLOR);
        lv_obj_set_grid_cell(daplink_context.local_target_label, LV_GRID_ALIGN_STRETCH, 1, 1,
                             LV_GRID_ALIGN_CENTER, 0, 1);
        lv_label_set_long_mode(daplink_context.local_target_label, LV_LABEL_LONG_CLIP);
    }
    {
        lv_obj_t *id_row = ui_daplink_create_panel(row, false);
        lv_obj_set_size(id_row, lv_pct(100), 36);
        lv_obj_set_grid_dsc_array(id_row, field_cols, field_rows);
        lv_obj_t *caption = ui_daplink_create_label(id_row, "IDCODE", DAP_UI_MUTED_COLOR);
        lv_obj_set_grid_cell(caption, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        daplink_context.local_id_label = ui_daplink_create_label(id_row, "--", DAP_UI_TEXT_COLOR);
        lv_obj_set_grid_cell(daplink_context.local_id_label, LV_GRID_ALIGN_STRETCH,
                             1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_label_set_long_mode(daplink_context.local_id_label, LV_LABEL_LONG_CLIP);
    }
    {
        lv_obj_t *clock_row = ui_daplink_create_panel(row, false);
        lv_obj_set_size(clock_row, lv_pct(100), 36);
        lv_obj_set_grid_dsc_array(clock_row, field_cols, field_rows);
        lv_obj_t *caption = ui_daplink_create_label(clock_row, "SWD", DAP_UI_MUTED_COLOR);
        lv_obj_set_grid_cell(caption, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        daplink_context.clock_dropdown = lv_dropdown_create(clock_row);
        lv_obj_set_height(daplink_context.clock_dropdown, 36);
        lv_obj_set_grid_cell(daplink_context.clock_dropdown, LV_GRID_ALIGN_STRETCH,
                             1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_style_pad_ver(daplink_context.clock_dropdown, 1, 0);
        lv_obj_set_style_pad_hor(daplink_context.clock_dropdown, 8, 0);
        lv_obj_set_style_radius(daplink_context.clock_dropdown, 6, 0);
        lv_obj_set_style_border_width(daplink_context.clock_dropdown, 1, 0);
        lv_obj_set_style_border_color(daplink_context.clock_dropdown, lv_color_hex(DAP_UI_BORDER_COLOR), 0);
        lv_obj_set_style_bg_color(daplink_context.clock_dropdown, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(daplink_context.clock_dropdown, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(daplink_context.clock_dropdown, lv_color_hex(DAP_UI_TEXT_COLOR), 0);
        lv_dropdown_set_options(daplink_context.clock_dropdown,
            "10 MHz\n5 MHz\n2 MHz\n1 MHz\n500 kHz\n200 kHz\n100 kHz\n50 kHz\n20 kHz\n10 kHz\n5 kHz");
        lv_dropdown_set_selected(daplink_context.clock_dropdown, 0U);
        for (uint32_t i = 0U; restore && i < sizeof(local_clock_rates) / sizeof(local_clock_rates[0]); i++)
        {
            if (local_clock_rates[i] == programmer.swd_clock_hz)
                lv_dropdown_set_selected(daplink_context.clock_dropdown, (uint16_t)i);
        }
        lv_obj_t *list = lv_dropdown_get_list(daplink_context.clock_dropdown);
        lv_obj_set_style_max_height(list, 220, 0);
        lv_obj_set_style_text_font(list, &ui_font_chinese_24_4, 0);
    }

    card = ui_daplink_create_panel(daplink_context.local_page, true);
    ui_daplink_grid_cell(card, 0, 1, 2);
    lv_obj_set_style_pad_all(card, DAP_UI_ACTION_PADDING, 0);
    /* 字库实际行高29px，文字行保留30px；缩小间距以免挤压上方RAM输入行。 */
    lv_obj_set_style_pad_ver(card, 9, 0);
    lv_obj_set_grid_dsc_array(card, one_col, progress_rows);
    lv_obj_set_style_pad_row(card, 6, 0);
    row = ui_daplink_create_panel(card, false);
    ui_daplink_grid_cell(row, 0, 0, 1);
    lv_obj_set_style_pad_column(row, DAP_UI_ACTION_GAP, 0);
    lv_obj_set_grid_dsc_array(row, stage_cols, stage_rows);
    {
        lv_obj_t *stages = ui_daplink_create_panel(row, false);
        ui_daplink_grid_cell(stages, 0, 0, 1);
        lv_obj_set_flex_flow(stages, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(stages, 6, 0);
        static const char *names[] = {"擦除", "编程", "校验"};
        for (uint32_t i = 0U; i < 3U; i++)
        {
            if (i != 0U)
            {
                lv_obj_t *arrow = ui_daplink_create_label(stages, ">", DAP_UI_MUTED_COLOR);
                lv_obj_set_width(arrow, 12);
                lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, 0);
            }
            daplink_context.stage_labels[i] = ui_daplink_create_label(stages, names[i], DAP_UI_MUTED_COLOR);
            lv_obj_set_size(daplink_context.stage_labels[i], 64, 30);
            lv_obj_set_style_radius(daplink_context.stage_labels[i], 6, 0);
            lv_obj_set_style_bg_opa(daplink_context.stage_labels[i], LV_OPA_COVER, 0);
            lv_obj_set_style_text_align(daplink_context.stage_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        }
    }
    daplink_context.programmer_state_label = ui_daplink_create_label(row, "等待本地烧录", DAP_UI_TEXT_COLOR);
    ui_daplink_grid_cell(daplink_context.programmer_state_label, 1, 0, 1);
    lv_label_set_long_mode(daplink_context.programmer_state_label, LV_LABEL_LONG_DOT);
    daplink_context.details_button = ui_daplink_create_action_button(row, "详情", DAP_UI_PRIMARY_COLOR);
    lv_obj_set_size(daplink_context.details_button, 64, 30);
    ui_daplink_grid_cell(daplink_context.details_button, 2, 0, 1);
    lv_obj_add_event_cb(daplink_context.details_button, ui_daplink_result_clicked,
                        LV_EVENT_CLICKED, NULL);
    daplink_context.elapsed_label = ui_daplink_create_label(row, "耗时 00:00.0", DAP_UI_MUTED_COLOR);
    ui_daplink_grid_cell(daplink_context.elapsed_label, 3, 0, 1);
    lv_obj_set_style_text_align(daplink_context.elapsed_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(daplink_context.elapsed_label, LV_LABEL_LONG_CLIP);
    daplink_context.local_bar = ui_daplink_bar(card, false);
    ui_daplink_grid_cell(daplink_context.local_bar, 0, 1, 1);
    lv_obj_set_style_bg_color(daplink_context.local_bar, lv_color_hex(DAP_UI_BORDER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_radius(daplink_context.local_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(daplink_context.local_bar, 4, LV_PART_INDICATOR);
    daplink_context.programmer_message_label = ui_daplink_create_label(card, "", DAP_UI_MUTED_COLOR);
    ui_daplink_grid_cell(daplink_context.programmer_message_label, 0, 2, 1);
    lv_label_set_long_mode(daplink_context.programmer_message_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(daplink_context.programmer_message_label,
                                LV_TEXT_ALIGN_LEFT, 0);
    row = ui_daplink_create_panel(card, false);
    ui_daplink_grid_cell(row, 0, 3, 1);
    lv_obj_set_style_pad_column(row, DAP_UI_ACTION_GAP, 0);
    lv_obj_set_grid_dsc_array(row, action_cols, action_rows);
    daplink_context.erase_dropdown = lv_dropdown_create(row);
    lv_obj_set_height(daplink_context.erase_dropdown, 38);
    lv_obj_set_grid_cell(daplink_context.erase_dropdown, LV_GRID_ALIGN_STRETCH,
                         0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_dropdown_set_options(daplink_context.erase_dropdown, "扇区擦除\n整片擦除");
    lv_dropdown_set_selected(daplink_context.erase_dropdown, 0U);
    lv_obj_set_style_pad_ver(daplink_context.erase_dropdown, 1, 0);
    lv_obj_set_style_pad_hor(daplink_context.erase_dropdown, 6, 0);
    lv_obj_set_style_text_font(daplink_context.erase_dropdown, &ui_font_chinese_24_4, 0);
    lv_obj_t *erase_list = lv_dropdown_get_list(daplink_context.erase_dropdown);
    lv_obj_set_style_text_font(erase_list, &ui_font_chinese_24_4, 0);
    daplink_context.start_button = ui_daplink_create_action_button(row, "开始烧录", DAP_UI_SUCCESS_COLOR);
    lv_obj_set_size(daplink_context.start_button, DAP_UI_START_WIDTH, 38);
    daplink_context.cancel_button = ui_daplink_create_action_button(row, "取消烧录", DAP_UI_DANGER_COLOR);
    lv_obj_set_size(daplink_context.cancel_button, DAP_UI_CANCEL_WIDTH, 38);
    ui_daplink_grid_cell(daplink_context.start_button, 2, 0, 1);
    ui_daplink_grid_cell(daplink_context.cancel_button, 3, 0, 1);
    lv_obj_add_event_cb(daplink_context.start_button, ui_daplink_local_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(daplink_context.cancel_button, ui_daplink_local_button_cb, LV_EVENT_CLICKED, NULL);

    if (restore)
    {
        ui_daplink_file_selected(programmer.firmware_path, (void *)(uintptr_t)1U);
        if (programmer.flm_path[0] != '\0')
            ui_daplink_file_selected(programmer.flm_path, (void *)(uintptr_t)2U);
    }
    ui_daplink_select_tab(restore && (programmer.active || programmer.pending));
    daplink_context.refresh_timer = lv_timer_create(ui_daplink_update, 100U, NULL);
    ui_daplink_update(NULL);
}
