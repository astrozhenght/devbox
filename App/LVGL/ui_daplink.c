#include "ui_daplink.h"

#include <stdio.h>
#include <string.h>

#include "dap_programmer.h"
#include "dap_service.h"
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
/* 下方操作行距卡片外边缘还包含1px边框，与上方目标卡片共用右侧宽度。 */
#define DAP_UI_TARGET_WIDTH      (DAP_UI_START_WIDTH + DAP_UI_CANCEL_WIDTH + \
                                  DAP_UI_ACTION_GAP + DAP_UI_ACTION_PADDING + 1)

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
    lv_obj_t *local_id_label;
    lv_obj_t *clock_dropdown;
    lv_obj_t *stage_labels[3];
    lv_obj_t *elapsed_label;
    lv_obj_t *firmware_info_label;
    lv_obj_t *path_dialog;
    bool local_selected;
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
        uint16_t selected = lv_dropdown_get_selected(daplink_context.clock_dropdown);
        if (selected >= sizeof(local_clock_rates) / sizeof(local_clock_rates[0])) return;
        if (!dap_programmer_request_with_clock(daplink_context.firmware_path,
                                               daplink_context.flm_path,
                                               local_clock_rates[selected]))
            lv_label_set_text(daplink_context.programmer_message_label,
                              "无法提交：烧录器忙或路径无效");
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

static void ui_daplink_dialog_deleted(lv_event_t *event)
{
    if (lv_event_get_target(event) == daplink_context.path_dialog)
        daplink_context.path_dialog = NULL;
}

static void ui_daplink_path_cb(lv_event_t *event)
{
    const char *path = lv_event_get_current_target(event) == daplink_context.firmware_label ?
                       daplink_context.firmware_path : daplink_context.flm_path;
    if (path[0] == '\0' || daplink_context.path_dialog != NULL) return;
    daplink_context.path_dialog = lv_msgbox_create(daplink_context.root,
                                                   "文件路径", path, NULL, true);
    lv_obj_set_width(daplink_context.path_dialog, 680);
    lv_obj_set_style_text_font(daplink_context.path_dialog, &ui_font_chinese_24_4, 0);
    lv_obj_set_width(lv_msgbox_get_text(daplink_context.path_dialog), 620);
    lv_label_set_long_mode(lv_msgbox_get_text(daplink_context.path_dialog), LV_LABEL_LONG_WRAP);
    lv_obj_add_event_cb(daplink_context.path_dialog, ui_daplink_dialog_deleted,
                        LV_EVENT_DELETE, NULL);
    lv_obj_center(daplink_context.path_dialog);
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
    if (programmer->firmware_bytes != 0U &&
        strcmp(daplink_context.firmware_path, programmer->firmware_path) == 0)
        lv_label_set_text_fmt(daplink_context.firmware_info_label,
            "%lu B  0x%08lX-0x%08lX",
            (unsigned long)programmer->firmware_bytes,
            (unsigned long)programmer->firmware_start,
            (unsigned long)(programmer->firmware_end - 1U));
    else
        lv_label_set_text(daplink_context.firmware_info_label, "有效大小 / 地址：待解析");
    for (uint32_t i = 0U; i < 3U; i++)
    {
        dap_programmer_state_t stage = (dap_programmer_state_t)(DAP_PROGRAMMER_ERASING + i);
        uint32_t stage_color = DAP_UI_MUTED_COLOR;
        if (!programmer->pending)
        {
            if (programmer->state == DAP_PROGRAMMER_SUCCESS || programmer->operation_stage > stage)
                stage_color = DAP_UI_SUCCESS_COLOR;
            else if (programmer->operation_stage == stage)
                stage_color = (programmer->state == DAP_PROGRAMMER_FAILED ||
                               programmer->state == DAP_PROGRAMMER_CANCELLED) ?
                               DAP_UI_DANGER_COLOR : DAP_UI_PRIMARY_COLOR;
        }
        lv_obj_set_style_text_color(daplink_context.stage_labels[i], lv_color_hex(stage_color), 0);
    }

    if (programmer->state == DAP_PROGRAMMER_SUCCESS)
    {
        color = DAP_UI_SUCCESS_COLOR;
        percent = 100U;
    }
    else if (programmer->state == DAP_PROGRAMMER_FAILED ||
             programmer->state == DAP_PROGRAMMER_CANCELLED) color = DAP_UI_DANGER_COLOR;
    else if (busy) color = DAP_UI_WARNING_COLOR;

    if (programmer->pending)
    {
        percent = 0U;
        lv_label_set_text(daplink_context.programmer_state_label, "等待烧录");
        message = "正在准备烧录";
    }
    else if (programmer->cancel_requested && busy)
        lv_label_set_text(daplink_context.programmer_state_label, "正在取消");
    else if (programmer->total != 0U && programmer->active)
        lv_label_set_text_fmt(daplink_context.programmer_state_label,
                              "%s %lu/%lu %lu%%",
                              dap_programmer_state_text(programmer->state),
                              (unsigned long)programmer->completed,
                              (unsigned long)programmer->total, (unsigned long)percent);
    else if (programmer->state == DAP_PROGRAMMER_SUCCESS)
        lv_label_set_text(daplink_context.programmer_state_label, "烧录成功    100%");
    else
        lv_label_set_text(daplink_context.programmer_state_label,
                          dap_programmer_state_text(programmer->state));

    if (programmer->state == DAP_PROGRAMMER_IDLE && !busy)
        message = ready ? "可以开始烧录" : "请选择 HEX/BIN 固件和 FLM 算法";
    lv_label_set_text(daplink_context.programmer_message_label, message);
    lv_obj_set_style_text_color(daplink_context.programmer_state_label, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(daplink_context.local_bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_bar_set_value(daplink_context.local_bar, percent, LV_ANIM_OFF);
    ui_daplink_disable(daplink_context.firmware_button, busy);
    ui_daplink_disable(daplink_context.flm_button, busy);
    ui_daplink_disable(daplink_context.clock_dropdown, busy);
    ui_daplink_disable(daplink_context.start_button, busy || !ready);
    ui_daplink_disable(daplink_context.cancel_button, !busy || programmer->cancel_requested);

    lv_label_set_text(daplink_context.local_target_label,
        programmer->dpidr != 0U ? (busy ? "目标已识别" : "上次识别") :
        (busy ? "正在连接目标" : "目标未连接"));
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
    lv_obj_t *body;
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 6, 0);
    (void)ui_daplink_create_label(card, title, DAP_UI_PRIMARY_COLOR);
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
    static const lv_coord_t rows[] = {34, LV_GRID_TEMPLATE_LAST};
    lv_obj_t *row = ui_daplink_create_panel(parent, false);
    lv_obj_t *label;
    lv_obj_t *button;
    lv_obj_set_size(row, lv_pct(100), 34);
    lv_obj_set_grid_dsc_array(row, columns, rows);
    label = ui_daplink_create_label(row, firmware ? "固件" : "算法", DAP_UI_MUTED_COLOR);
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    label = ui_daplink_create_label(row,
        firmware ? "请选择 HEX/BIN 固件" : "请选择 FLM 算法", DAP_UI_MUTED_COLOR);
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, ui_daplink_path_cb, LV_EVENT_CLICKED, NULL);
    button = ui_daplink_create_action_button(row, "选择", DAP_UI_PRIMARY_COLOR);
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
    static const lv_coord_t root_rows[] = {42, 40, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t usb_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t usb_rows[] = {102, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t local_cols[] = {LV_GRID_FR(1), DAP_UI_TARGET_WIDTH, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t local_rows[] = {LV_GRID_FR(1), 144, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t progress_rows[] = {29, 29, 42, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t overview_cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), 270,
                                               LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t overview_rows[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t action_cols[] = {LV_GRID_FR(1), DAP_UI_START_WIDTH,
                                             DAP_UI_CANCEL_WIDTH, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t action_rows[] = {42, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t stage_cols[] = {200, LV_GRID_FR(1), 156, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t stage_rows[] = {29, LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t field_cols[] = {70, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t field_rows[] = {34, LV_GRID_TEMPLATE_LAST};
    lv_obj_t *header, *spacer, *home, *tabs, *overview, *card, *row;
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
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    (void)ui_daplink_create_label(header, "DAP Link", DAP_UI_TITLE_COLOR);
    spacer = ui_daplink_create_panel(header, false);
    lv_obj_set_size(spacer, 0, 1);
    lv_obj_set_flex_grow(spacer, 1);
    home = ui_daplink_create_home_button(header);
    lv_obj_add_event_cb(home, ui_daplink_home_button_cb, LV_EVENT_CLICKED, NULL);
    tabs = ui_daplink_create_panel(daplink_context.root, false);
    ui_daplink_grid_cell(tabs, 0, 1, 1);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    daplink_context.usb_tab = ui_daplink_create_action_button(tabs, "USB 调试", DAP_UI_PRIMARY_COLOR);
    daplink_context.local_tab = ui_daplink_create_action_button(tabs, "本地烧录", DAP_UI_PRIMARY_COLOR);
    lv_obj_t *tab_buttons[] = {daplink_context.usb_tab, daplink_context.local_tab};
    for (uint32_t i = 0U; i < 2U; i++)
    {
        lv_obj_set_width(tab_buttons[i], 0);
        lv_obj_set_height(tab_buttons[i], 40);
        lv_obj_set_flex_grow(tab_buttons[i], 1);
        lv_obj_set_style_bg_color(tab_buttons[i], lv_color_hex(DAP_UI_PRIMARY_COLOR), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(tab_buttons[i], lv_color_white(), LV_STATE_CHECKED);
        lv_obj_add_event_cb(tab_buttons[i], ui_daplink_tab_cb, LV_EVENT_CLICKED, NULL);
    }

    daplink_context.usb_page = ui_daplink_create_panel(daplink_context.root, false);
    ui_daplink_grid_cell(daplink_context.usb_page, 0, 2, 1);
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
    ui_daplink_grid_cell(daplink_context.local_page, 0, 2, 1);
    lv_obj_set_style_pad_gap(daplink_context.local_page, 12, 0);
    lv_obj_set_grid_dsc_array(daplink_context.local_page, local_cols, local_rows);
    card = ui_daplink_create_panel(daplink_context.local_page, true);
    ui_daplink_grid_cell(card, 0, 0, 1);
    row = ui_daplink_card_body(card, "烧录文件");
    ui_daplink_file_row(row, true);
    ui_daplink_file_row(row, false);
    daplink_context.firmware_info_label = ui_daplink_create_label(row,
        "有效大小 / 地址：待解析", DAP_UI_MUTED_COLOR);
    lv_obj_set_width(daplink_context.firmware_info_label, lv_pct(100));
    lv_label_set_long_mode(daplink_context.firmware_info_label, LV_LABEL_LONG_DOT);
    card = ui_daplink_create_panel(daplink_context.local_page, true);
    ui_daplink_grid_cell(card, 1, 0, 1);
    row = ui_daplink_card_body(card, "目标信息");
    daplink_context.local_target_label = ui_daplink_create_label(row, "目标未连接", DAP_UI_MUTED_COLOR);
    {
        lv_obj_t *id_row = ui_daplink_create_panel(row, false);
        lv_obj_set_size(id_row, lv_pct(100), 34);
        lv_obj_set_grid_dsc_array(id_row, field_cols, field_rows);
        lv_obj_t *caption = ui_daplink_create_label(id_row, "IDCODE", DAP_UI_MUTED_COLOR);
        lv_obj_set_grid_cell(caption, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        daplink_context.local_id_label = ui_daplink_create_label(id_row, "--", DAP_UI_TEXT_COLOR);
        lv_obj_set_grid_cell(daplink_context.local_id_label, LV_GRID_ALIGN_STRETCH,
                             1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_style_pad_left(daplink_context.local_id_label, 8, 0);
        lv_label_set_long_mode(daplink_context.local_id_label, LV_LABEL_LONG_DOT);
    }
    {
        lv_obj_t *clock_row = ui_daplink_create_panel(row, false);
        lv_obj_set_size(clock_row, lv_pct(100), 34);
        lv_obj_set_grid_dsc_array(clock_row, field_cols, field_rows);
        lv_obj_t *caption = ui_daplink_create_label(clock_row, "SWD", DAP_UI_MUTED_COLOR);
        lv_obj_set_grid_cell(caption, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        daplink_context.clock_dropdown = lv_dropdown_create(clock_row);
        lv_obj_set_grid_cell(daplink_context.clock_dropdown, LV_GRID_ALIGN_STRETCH,
                             1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_style_pad_ver(daplink_context.clock_dropdown, 1, 0);
        lv_obj_set_style_pad_hor(daplink_context.clock_dropdown, 8, 0);
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
    lv_obj_set_grid_dsc_array(card, one_col, progress_rows);
    lv_obj_set_style_pad_row(card, 8, 0);
    row = ui_daplink_create_panel(card, false);
    ui_daplink_grid_cell(row, 0, 0, 1);
    lv_obj_set_grid_dsc_array(row, stage_cols, stage_rows);
    {
        lv_obj_t *stages = ui_daplink_create_panel(row, false);
        ui_daplink_grid_cell(stages, 0, 0, 1);
        lv_obj_set_flex_flow(stages, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(stages, 6, 0);
        static const char *names[] = {"擦除", "编程", "校验"};
        for (uint32_t i = 0U; i < 3U; i++)
        {
            if (i != 0U) (void)ui_daplink_create_label(stages, ">", DAP_UI_MUTED_COLOR);
            daplink_context.stage_labels[i] = ui_daplink_create_label(stages, names[i], DAP_UI_MUTED_COLOR);
        }
    }
    daplink_context.programmer_state_label = ui_daplink_create_label(row, "等待本地烧录", DAP_UI_TEXT_COLOR);
    ui_daplink_grid_cell(daplink_context.programmer_state_label, 1, 0, 1);
    lv_label_set_long_mode(daplink_context.programmer_state_label, LV_LABEL_LONG_DOT);
    daplink_context.elapsed_label = ui_daplink_create_label(row, "耗时 00:00.0", DAP_UI_MUTED_COLOR);
    ui_daplink_grid_cell(daplink_context.elapsed_label, 2, 0, 1);
    lv_obj_set_style_text_align(daplink_context.elapsed_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(daplink_context.elapsed_label, LV_LABEL_LONG_CLIP);
    daplink_context.programmer_message_label = ui_daplink_create_label(card, "", DAP_UI_MUTED_COLOR);
    ui_daplink_grid_cell(daplink_context.programmer_message_label, 0, 1, 1);
    lv_label_set_long_mode(daplink_context.programmer_message_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    row = ui_daplink_create_panel(card, false);
    ui_daplink_grid_cell(row, 0, 2, 1);
    lv_obj_set_style_pad_column(row, DAP_UI_ACTION_GAP, 0);
    lv_obj_set_grid_dsc_array(row, action_cols, action_rows);
    daplink_context.local_bar = ui_daplink_bar(row, false);
    lv_obj_set_grid_cell(daplink_context.local_bar, LV_GRID_ALIGN_STRETCH,
                         0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    daplink_context.start_button = ui_daplink_create_action_button(row, "开始烧录", DAP_UI_SUCCESS_COLOR);
    lv_obj_set_size(daplink_context.start_button, DAP_UI_START_WIDTH, 42);
    daplink_context.cancel_button = ui_daplink_create_action_button(row, "取消烧录", DAP_UI_DANGER_COLOR);
    lv_obj_set_size(daplink_context.cancel_button, DAP_UI_CANCEL_WIDTH, 42);
    ui_daplink_grid_cell(daplink_context.start_button, 1, 0, 1);
    ui_daplink_grid_cell(daplink_context.cancel_button, 2, 0, 1);
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
