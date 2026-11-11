#include "ui_daplink.h"

#include <stdio.h>
#include <string.h>

#include "dap_service.h"
#include "ui_fonts.h"

#define DAP_UI_BG_COLOR          0xF5F7FA
#define DAP_UI_CARD_COLOR        0xFFFFFF
#define DAP_UI_TEXT_COLOR        0x243247
#define DAP_UI_MUTED_COLOR       0x69788B
#define DAP_UI_BORDER_COLOR      0xE4EAF1
#define DAP_UI_PROGRESS_BG_COLOR 0xB0BEC5
#define DAP_UI_PRIMARY_COLOR     0x2196F3
#define DAP_UI_SUCCESS_COLOR     0x2E7D32
#define DAP_UI_WARNING_COLOR     0xEF6C00
#define DAP_UI_DANGER_COLOR      0xE53935
#define DAP_UI_TITLE_COLOR       0xE91E63
#define DAP_UI_ACTIVITY_HOLD_MS  600U
#define DAP_UI_RATE_SAMPLE_MS    500U

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
    lv_obj_t *payload_label;
    lv_obj_t *activity_label;
    lv_obj_t *progress_bar;
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
} ui_daplink_context_t;

static ui_daplink_context_t daplink_context = {0};

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

static void ui_daplink_set_payload_text(uint32_t bytes)
{
    uint32_t value_tenths;

    if (bytes >= (1024U * 1024U))
    {
        value_tenths = (uint32_t)(((uint64_t)bytes * 10U) / (1024U * 1024U));
        lv_label_set_text_fmt(daplink_context.payload_label,
                              "累计载荷 %lu.%lu MiB",
                              (unsigned long)(value_tenths / 10U),
                              (unsigned long)(value_tenths % 10U));
    }
    else if (bytes >= 1024U)
    {
        value_tenths = (uint32_t)(((uint64_t)bytes * 10U) / 1024U);
        lv_label_set_text_fmt(daplink_context.payload_label,
                              "累计载荷 %lu.%lu KiB",
                              (unsigned long)(value_tenths / 10U),
                              (unsigned long)(value_tenths % 10U));
    }
    else
    {
        lv_label_set_text_fmt(daplink_context.payload_label,
                              "累计载荷 %lu B",
                              (unsigned long)bytes);
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

static void ui_daplink_update(lv_timer_t *timer)
{
    dap_service_status_t status;
    uint32_t now;
    uint32_t status_color;

    (void)timer;
    if (!dap_service_get_status(&status))
    {
        return;
    }
    now = lv_tick_get();
    ui_daplink_update_rate(&status, now);
    if (status.command_active ||
        (daplink_context.status_valid &&
         (status.command_count != daplink_context.status.command_count)))
    {
        daplink_context.last_activity_tick = now;
        daplink_context.activity_seen = true;
    }
    /* 主机没有提供烧录总量：活动条只表示通信忙碌，不显示虚构百分比。 */
    ui_daplink_set_activity(status.usb_configured && daplink_context.activity_seen &&
        (status.command_active ||
         (lv_tick_elaps(daplink_context.last_activity_tick) < DAP_UI_ACTIVITY_HOLD_MS)));
    if (daplink_context.status_valid &&
        (memcmp(&status, &daplink_context.status, sizeof(status)) == 0))
    {
        return;
    }

    daplink_context.status = status;
    daplink_context.status_valid = true;

    if (!status.initialized)
    {
        lv_label_set_text(daplink_context.usb_status_label, "USB 初始化失败");
        status_color = DAP_UI_DANGER_COLOR;
    }
    else if (status.usb_configured)
    {
        lv_label_set_text(daplink_context.usb_status_label, "USB 已连接");
        status_color = DAP_UI_SUCCESS_COLOR;
    }
    else
    {
        lv_label_set_text(daplink_context.usb_status_label, "USB 等待连接");
        status_color = DAP_UI_WARNING_COLOR;
    }
    lv_obj_set_style_text_color(daplink_context.usb_status_label,
                                lv_color_hex(status_color), 0);

    if (status.debug_port == 1U)
    {
        lv_label_set_text(daplink_context.port_status_label, "SWD 已连接");
        status_color = DAP_UI_SUCCESS_COLOR;
    }
    else
    {
        lv_label_set_text(daplink_context.port_status_label, "SWD 未连接");
        status_color = DAP_UI_MUTED_COLOR;
    }
    lv_obj_set_style_text_color(daplink_context.port_status_label,
                                lv_color_hex(status_color), 0);

    lv_label_set_text_fmt(daplink_context.clock_label,
                          "SWD 时钟 %lu kHz",
                          (unsigned long)(status.swd_clock_hz / 1000U));
    lv_label_set_text_fmt(daplink_context.duration_label,
                          "连接时长 %02lu:%02lu",
                          (unsigned long)(status.connection_duration_ms / 60000U),
                          (unsigned long)((status.connection_duration_ms / 1000U) % 60U));
    if (status.dpidr_valid)
    {
        lv_label_set_text_fmt(daplink_context.dpidr_label,
                              "0x%08lX", (unsigned long)status.dpidr);
    }
    else
    {
        lv_label_set_text(daplink_context.dpidr_label, "尚未读取");
    }
    lv_label_set_text_fmt(daplink_context.success_count_label,
                          "成功命令数 %lu",
                          (unsigned long)status.success_count);
    lv_label_set_text_fmt(daplink_context.failure_count_label,
                          "失败命令数 %lu",
                          (unsigned long)status.failure_count);
    ui_daplink_set_payload_text(status.communication_bytes);
}

static void ui_daplink_root_deleted_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) != daplink_context.root)
    {
        return;
    }
    if (daplink_context.refresh_timer != NULL)
    {
        lv_timer_del(daplink_context.refresh_timer);
    }
    lv_anim_del(daplink_context.progress_bar, ui_daplink_activity_anim);
    memset(&daplink_context, 0, sizeof(daplink_context));
}

static void ui_daplink_home_button_cb(lv_event_t *event)
{
    (void)event;
    if (daplink_context.root != NULL)
    {
        lv_obj_del(daplink_context.root);
    }
}

void ui_daplink_create(lv_obj_t *parent)
{
    static const lv_coord_t root_columns[] =
    {
        LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t root_rows[] =
    {
        42, 112, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t detail_columns[] =
    {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t detail_rows[] =
    {
        LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    lv_obj_t *header;
    lv_obj_t *title;
    lv_obj_t *spacer;
    lv_obj_t *home_button;
    lv_obj_t *overview;
    lv_obj_t *overview_status;
    lv_obj_t *target_status;
    lv_obj_t *identity_status;
    lv_obj_t *details;
    lv_obj_t *interface_card;
    lv_obj_t *statistics_card;

    if (daplink_context.root != NULL)
    {
        lv_obj_move_foreground(daplink_context.root);
        return;
    }

    daplink_context.root = ui_daplink_create_panel(parent, false);
    lv_obj_set_size(daplink_context.root, lv_pct(100), lv_pct(92));
    lv_obj_set_y(daplink_context.root, lv_pct(8));
    lv_obj_set_style_pad_all(daplink_context.root, 8, 0);
    lv_obj_set_style_pad_gap(daplink_context.root, 8, 0);
    lv_obj_set_style_bg_color(daplink_context.root,
                              lv_color_hex(DAP_UI_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(daplink_context.root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(daplink_context.root,
                               &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(daplink_context.root,
                                lv_color_hex(DAP_UI_TEXT_COLOR), 0);
    lv_obj_set_grid_dsc_array(daplink_context.root, root_columns, root_rows);
    lv_obj_add_event_cb(daplink_context.root,
                        ui_daplink_root_deleted_cb,
                        LV_EVENT_DELETE,
                        NULL);

    header = ui_daplink_create_panel(daplink_context.root, false);
    lv_obj_set_grid_cell(header,
                         LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_pad_left(header, 10, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    title = ui_daplink_create_label(header, "DAP Link", DAP_UI_TITLE_COLOR);
    spacer = ui_daplink_create_panel(header, false);
    lv_obj_set_size(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);
    home_button = ui_daplink_create_home_button(header);
    lv_obj_add_event_cb(home_button,
                        ui_daplink_home_button_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    overview = ui_daplink_create_panel(daplink_context.root, true);
    lv_obj_set_grid_cell(overview,
                         LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_flex_flow(overview, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(overview,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(overview, 12, 0);

    overview_status = ui_daplink_create_panel(overview, false);
    lv_obj_set_flex_flow(overview_status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(overview_status,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_height(overview_status, lv_pct(100));
    lv_obj_set_flex_grow(overview_status, 1);
    (void)ui_daplink_create_label(overview_status,
                                  "CMSIS-DAP v2",
                                  DAP_UI_PRIMARY_COLOR);
    daplink_context.usb_status_label = ui_daplink_create_label(
        overview_status, "USB 等待连接", DAP_UI_WARNING_COLOR);

    target_status = ui_daplink_create_panel(overview, false);
    lv_obj_set_height(target_status, lv_pct(100));
    lv_obj_set_flex_grow(target_status, 1);
    lv_obj_set_flex_flow(target_status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(target_status,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    (void)ui_daplink_create_label(target_status,
                                  "目标连接",
                                  DAP_UI_PRIMARY_COLOR);
    daplink_context.port_status_label = ui_daplink_create_label(
        target_status, "SWD 未连接", DAP_UI_MUTED_COLOR);

    identity_status = ui_daplink_create_panel(overview, false);
    lv_obj_set_height(identity_status, lv_pct(100));
    lv_obj_set_flex_grow(identity_status, 1);
    lv_obj_set_flex_flow(identity_status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(identity_status,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    (void)ui_daplink_create_label(identity_status,
                                  "IDCODE",
                                  DAP_UI_PRIMARY_COLOR);
    daplink_context.dpidr_label = ui_daplink_create_label(
        identity_status, "尚未读取", DAP_UI_MUTED_COLOR);

    details = ui_daplink_create_panel(daplink_context.root, false);
    lv_obj_set_grid_cell(details,
                         LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_set_style_pad_column(details, 8, 0);
    lv_obj_set_grid_dsc_array(details, detail_columns, detail_rows);

    interface_card = ui_daplink_create_panel(details, true);
    lv_obj_set_grid_cell(interface_card,
                         LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_flex_flow(interface_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(interface_card,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    (void)ui_daplink_create_label(interface_card,
                                   "链路状态",
                                  DAP_UI_PRIMARY_COLOR);
    daplink_context.clock_label = ui_daplink_create_label(
        interface_card, "SWD 时钟 100 kHz", DAP_UI_TEXT_COLOR);
    daplink_context.duration_label = ui_daplink_create_label(
        interface_card, "连接时长 00:00", DAP_UI_MUTED_COLOR);
    daplink_context.activity_label = ui_daplink_create_label(
        interface_card, "通信状态 等待命令", DAP_UI_MUTED_COLOR);
    daplink_context.progress_bar = lv_bar_create(interface_card);
    lv_obj_set_size(daplink_context.progress_bar, lv_pct(100), 12);
    lv_bar_set_range(daplink_context.progress_bar, 0, 100);
    lv_bar_set_mode(daplink_context.progress_bar, LV_BAR_MODE_RANGE);
    lv_obj_set_style_bg_color(daplink_context.progress_bar,
                              lv_color_hex(DAP_UI_PROGRESS_BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(daplink_context.progress_bar,
                            LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(daplink_context.progress_bar,
                              lv_color_hex(DAP_UI_PRIMARY_COLOR), LV_PART_INDICATOR);
    lv_bar_set_value(daplink_context.progress_bar, 0, LV_ANIM_OFF);

    statistics_card = ui_daplink_create_panel(details, true);
    lv_obj_set_grid_cell(statistics_card,
                         LV_GRID_ALIGN_STRETCH, 1, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_flex_flow(statistics_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(statistics_card,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    (void)ui_daplink_create_label(statistics_card,
                                  "运行统计",
                                  DAP_UI_PRIMARY_COLOR);
    daplink_context.success_count_label = ui_daplink_create_label(
        statistics_card, "成功命令数 0", DAP_UI_SUCCESS_COLOR);
    daplink_context.failure_count_label = ui_daplink_create_label(
        statistics_card, "失败命令数 0", DAP_UI_DANGER_COLOR);
    daplink_context.rate_label = ui_daplink_create_label(
        statistics_card, "实时速率 0 B/s", DAP_UI_TEXT_COLOR);
    daplink_context.payload_label = ui_daplink_create_label(
        statistics_card, "累计载荷 0 B", DAP_UI_MUTED_COLOR);

    /* UI 仅轮询服务快照，不参与 USB/DAP 协议处理。 */
    daplink_context.refresh_timer = lv_timer_create(ui_daplink_update, 250, NULL);
    ui_daplink_update(NULL);
}
