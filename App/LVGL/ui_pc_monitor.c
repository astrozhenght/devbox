#include "ui_pc_monitor.h"

#include <stdio.h>
#include <string.h>

#include "pc_monitor_service.h"
#include "ui_fonts.h"

#define PC_UI_BG_COLOR       0xF5F7FA
#define PC_UI_CARD_COLOR     0xFFFFFF
#define PC_UI_TEXT_COLOR     0x243247
#define PC_UI_MUTED_COLOR    0x69788B
#define PC_UI_BORDER_COLOR   0xE4EAF1
#define PC_UI_CPU_COLOR      0x2196F3
#define PC_UI_RAM_COLOR      0x7E57C2
#define PC_UI_GPU_COLOR      0x2E9D67
#define PC_UI_WARNING_COLOR  0xD97706
#define PC_UI_DANGER_COLOR   0xD94757
#define PC_UI_TITLE_COLOR    0xE91E63

typedef struct
{
    lv_obj_t *arc;
    lv_obj_t *value_label;
    lv_obj_t *status_label;
    lv_obj_t *model_label;
    lv_obj_t *detail_label;
    uint32_t accent_color;
} pc_ui_metric_t;

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *connection_label;
    pc_ui_metric_t cpu;
    pc_ui_metric_t ram;
    pc_ui_metric_t gpu;
    lv_obj_t *network_value_label;
    lv_obj_t *disk_value_label;
    lv_obj_t *history_chart;
    lv_chart_series_t *cpu_series;
    lv_chart_series_t *vram_series;
    lv_timer_t *refresh_timer;
    uint32_t rendered_version;
    uint32_t rendered_sequence;
} ui_pc_monitor_context_t;

static ui_pc_monitor_context_t monitor_ui;

static lv_obj_t *pc_ui_panel(lv_obj_t *parent, bool card)
{
    lv_obj_t *object = lv_obj_create(parent);

    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    if (card)
    {
        lv_obj_set_style_bg_color(object, lv_color_hex(PC_UI_CARD_COLOR), 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(object, 1, 0);
        lv_obj_set_style_border_color(object, lv_color_hex(PC_UI_BORDER_COLOR), 0);
        lv_obj_set_style_radius(object, 8, 0);
        lv_obj_set_style_pad_all(object, 8, 0);
    }
    return object;
}

static lv_obj_t *pc_ui_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
    return label;
}

static lv_obj_t *pc_ui_small_label(lv_obj_t *parent, const char *text,
                                   uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, &ui_font_chinese_20, 0);
    return label;
}

static lv_obj_t *pc_ui_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label;

    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 112, 38);
    lv_obj_set_style_pad_all(button, 3, 0);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_white(), 0);
    lv_obj_set_style_bg_color(button, lv_palette_main(LV_PALETTE_RED),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(PC_UI_BORDER_COLOR), 0);
    lv_obj_set_style_text_font(button, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(button, lv_color_hex(PC_UI_TEXT_COLOR), 0);
    lv_obj_set_style_text_color(button, lv_color_white(), LV_STATE_PRESSED);

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

static void pc_ui_home_clicked(lv_event_t *event)
{
    (void)event;
    if (monitor_ui.root != NULL) lv_obj_del(monitor_ui.root);
}

static void pc_ui_root_deleted(lv_event_t *event)
{
    if (lv_event_get_target(event) != monitor_ui.root) return;
    if (monitor_ui.refresh_timer != NULL) lv_timer_del(monitor_ui.refresh_timer);
    memset(&monitor_ui, 0, sizeof(monitor_ui));
}

static pc_ui_metric_t pc_ui_create_metric(lv_obj_t *parent, const char *title,
                                          uint32_t accent_color)
{
    pc_ui_metric_t metric = {0};
    lv_obj_t *title_label = pc_ui_label(parent, title, PC_UI_TEXT_COLOR);

    metric.accent_color = accent_color;
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, -3);

    metric.model_label = pc_ui_small_label(parent, "--", PC_UI_MUTED_COLOR);
    lv_label_set_long_mode(metric.model_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(metric.model_label, lv_pct(100));
    lv_obj_set_style_text_align(metric.model_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(metric.model_label, LV_ALIGN_TOP_LEFT, 0, 24);

    metric.arc = lv_arc_create(parent);
    lv_obj_set_size(metric.arc, 100, 100);
    lv_obj_align(metric.arc, LV_ALIGN_BOTTOM_LEFT, 0, 4);
    lv_arc_set_range(metric.arc, 0, 100);
    lv_arc_set_bg_angles(metric.arc, 150, 30);
    lv_arc_set_value(metric.arc, 0);
    lv_obj_clear_flag(metric.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(metric.arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(metric.arc, lv_color_hex(0xE6EBF1), LV_PART_MAIN);
    lv_obj_set_style_arc_width(metric.arc, 11, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(metric.arc, lv_color_hex(accent_color),
                               LV_PART_INDICATOR);
    lv_obj_remove_style(metric.arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_rounded(metric.arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(metric.arc, true, LV_PART_INDICATOR);
    /* 原生端点随进度移动：彩色圆点、白边和淡色光晕。 */
    lv_obj_set_style_radius(metric.arc, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(metric.arc, -1, LV_PART_KNOB);
    lv_obj_set_style_bg_color(metric.arc, lv_color_hex(accent_color), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(metric.arc, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(metric.arc, 2, LV_PART_KNOB);
    lv_obj_set_style_border_color(metric.arc, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_border_opa(metric.arc, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_outline_width(metric.arc, 3, LV_PART_KNOB);
    lv_obj_set_style_outline_pad(metric.arc, 0, LV_PART_KNOB);
    lv_obj_set_style_outline_color(metric.arc, lv_color_hex(accent_color), LV_PART_KNOB);
    lv_obj_set_style_outline_opa(metric.arc, LV_OPA_20, LV_PART_KNOB);
    lv_obj_set_style_opa(metric.arc, LV_OPA_TRANSP, LV_PART_KNOB);

    metric.value_label = pc_ui_small_label(parent, "--%", accent_color);
    lv_obj_set_style_text_font(metric.value_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_width(metric.value_label, 100);
    lv_obj_set_style_text_align(metric.value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(metric.value_label, metric.arc, LV_ALIGN_CENTER, 0, -7);
    metric.status_label = pc_ui_small_label(parent, "NO DATA", PC_UI_MUTED_COLOR);
    lv_obj_set_width(metric.status_label, 100);
    lv_obj_set_style_text_align(metric.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(metric.status_label, metric.arc, LV_ALIGN_CENTER, 0, 29);

    metric.detail_label = pc_ui_small_label(parent, "--", PC_UI_TEXT_COLOR);
    lv_obj_set_width(metric.detail_label, 125);
    lv_obj_set_style_text_line_space(metric.detail_label, 5, 0);
    lv_obj_align(metric.detail_label, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
    return metric;
}

static uint8_t pc_ui_vram_percent(const pc_monitor_snapshot_t *snapshot)
{
    uint32_t percent;

    if (snapshot->vram_total_mib == 0U) return 0U;
    percent = (snapshot->vram_used_mib * 100U) / snapshot->vram_total_mib;
    return (uint8_t)((percent > 100U) ? 100U : percent);
}

static void pc_ui_format_capacity(char *buffer, size_t capacity,
                                  uint32_t used_mib, uint32_t total_mib)
{
    uint32_t used_hundredths = (used_mib * 100U) / 1024U;
    uint32_t total_hundredths = (total_mib * 100U) / 1024U;

    (void)snprintf(buffer, capacity, "%lu.%02lu/%lu.%02luGB",
                   (unsigned long)(used_hundredths / 100U),
                   (unsigned long)(used_hundredths % 100U),
                   (unsigned long)(total_hundredths / 100U),
                   (unsigned long)(total_hundredths % 100U));
}

static void pc_ui_format_clock(char *buffer, size_t capacity, uint16_t mhz)
{
    if (mhz == 0U)
        (void)snprintf(buffer, capacity, "--");
    else if (mhz >= 1000U)
        (void)snprintf(buffer, capacity, "%u.%02u GHz",
                       mhz / 1000U, (mhz % 1000U) / 10U);
    else
        (void)snprintf(buffer, capacity, "%u MHz", mhz);
}

static void pc_ui_format_rate(char *buffer, size_t capacity, uint32_t kib_s)
{
    uint64_t bytes = (uint64_t)kib_s * 1024ULL;
    uint64_t hundredths;

    if (bytes < 1000ULL)
    {
        (void)snprintf(buffer, capacity, "%lu B/s", (unsigned long)bytes);
    }
    else if (bytes < 1000000ULL)
    {
        hundredths = (bytes * 100ULL) / 1000ULL;
        (void)snprintf(buffer, capacity, "%lu.%02lu KB/s",
                       (unsigned long)(hundredths / 100ULL),
                       (unsigned long)(hundredths % 100ULL));
    }
    else
    {
        hundredths = (bytes * 100ULL) / 1000000ULL;
        (void)snprintf(buffer, capacity, "%lu.%02lu MB/s",
                       (unsigned long)(hundredths / 100ULL),
                       (unsigned long)(hundredths % 100ULL));
    }
}

static void pc_ui_update_metric(pc_ui_metric_t *metric, int value)
{
    uint32_t color = metric->accent_color;
    const char *status = "正常";

    if (value < 0)
    {
        lv_arc_set_value(metric->arc, 0);
        lv_label_set_text(metric->value_label, "--%");
        lv_label_set_text(metric->status_label, "无数据");
        color = PC_UI_MUTED_COLOR;
    }
    else
    {
        if (value >= 85)
        {
            color = PC_UI_DANGER_COLOR;
            status = "繁忙";
        }
        else if (value >= 70)
        {
            color = PC_UI_WARNING_COLOR;
            status = "较高";
        }
        lv_arc_set_value(metric->arc, value);
        lv_label_set_text_fmt(metric->value_label, "%d%%", value);
        lv_label_set_text(metric->status_label, status);
    }
    lv_obj_set_style_arc_color(metric->arc, lv_color_hex(color),
                               LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(metric->arc, lv_color_hex(color), LV_PART_KNOB);
    lv_obj_set_style_outline_color(metric->arc, lv_color_hex(color), LV_PART_KNOB);
    lv_obj_set_style_opa(metric->arc,
                         value > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_KNOB);
    /* 数值跟随负载颜色，状态文字保持灰色，减少视觉干扰。 */
    lv_obj_set_style_text_color(metric->value_label, lv_color_hex(color), 0);
}

static void pc_ui_update_history(const pc_monitor_snapshot_t *snapshot)
{
    pc_monitor_history_t history;
    uint8_t index;

    if (monitor_ui.rendered_sequence == UINT32_MAX)
    {
        pc_monitor_service_get_history(&history);
        for (index = 0U; index < history.count; index++)
        {
            uint8_t point = (uint8_t)((history.head + PC_MONITOR_HISTORY_POINTS -
                                      history.count + index) % PC_MONITOR_HISTORY_POINTS);
            lv_chart_set_next_value(monitor_ui.history_chart,
                                    monitor_ui.cpu_series,
                                    (history.cpu_usage[point] >= 0) ?
                                    history.cpu_usage[point] : 0);
            lv_chart_set_next_value(monitor_ui.history_chart,
                                    monitor_ui.vram_series,
                                    history.vram_usage[point]);
        }
        monitor_ui.rendered_sequence = snapshot->sequence;
        return;
    }
    if (snapshot->sequence == monitor_ui.rendered_sequence) return;
    monitor_ui.rendered_sequence = snapshot->sequence;
    lv_chart_set_next_value(monitor_ui.history_chart, monitor_ui.cpu_series,
                            (snapshot->cpu_usage >= 0) ? snapshot->cpu_usage : 0);
    lv_chart_set_next_value(monitor_ui.history_chart, monitor_ui.vram_series,
                            pc_ui_vram_percent(snapshot));
}

static void pc_ui_update_models(const pc_monitor_snapshot_t *snapshot)
{
    /* 两个最大长度字段、两个空格和字符串结束符。 */
    char ram_description[2U * (PC_MONITOR_RAM_INFO_CAPACITY - 1U) + 3U];

    if (!snapshot->info_valid) return;
    lv_label_set_text(monitor_ui.cpu.model_label, snapshot->cpu_model);
    lv_label_set_text(monitor_ui.gpu.model_label, snapshot->gpu_model);
    if ((snapshot->ram_type[0] != '\0') ||
        (snapshot->ram_configuration[0] != '\0'))
    {
        (void)snprintf(ram_description, sizeof(ram_description), "%s  %s",
                       snapshot->ram_type, snapshot->ram_configuration);
        lv_label_set_text(monitor_ui.ram.model_label, ram_description);
    }
    else
    {
        lv_label_set_text(monitor_ui.ram.model_label, snapshot->ram_model);
    }
}

static void pc_ui_update_values(const pc_monitor_snapshot_t *snapshot)
{
    char first[40];
    char second[40];
    char third[40];
    uint8_t vram_percent = pc_ui_vram_percent(snapshot);
    uint32_t total_network = snapshot->net_upload_kib_s +
                             snapshot->net_download_kib_s;

    pc_ui_update_metric(&monitor_ui.cpu, snapshot->cpu_usage);
    pc_ui_update_metric(&monitor_ui.ram, snapshot->ram_usage);
    pc_ui_update_metric(&monitor_ui.gpu,
                        (snapshot->vram_total_mib != 0U) ? vram_percent : -1);

    pc_ui_format_clock(first, sizeof(first), snapshot->cpu_frequency_mhz);
    pc_ui_format_clock(second, sizeof(second), snapshot->cpu_base_frequency_mhz);
    lv_label_set_text_fmt(monitor_ui.cpu.detail_label,
                          "Current / Base\n%s\n%s", first, second);

    pc_ui_format_capacity(first, sizeof(first), snapshot->ram_used_mib,
                          snapshot->ram_total_mib);
    if (snapshot->ram_data_rate_mts != 0U)
        (void)snprintf(second, sizeof(second), "%u MT/s",
                       snapshot->ram_data_rate_mts);
    else
        (void)snprintf(second, sizeof(second), "-- MT/s");
    lv_label_set_text_fmt(monitor_ui.ram.detail_label,
                          "Used / Total\n%s\n%s", first, second);

    pc_ui_format_capacity(first, sizeof(first), snapshot->vram_used_mib,
                          snapshot->vram_total_mib);
    pc_ui_format_clock(second, sizeof(second), snapshot->gpu_frequency_mhz);
    pc_ui_format_clock(third, sizeof(third), snapshot->gpu_max_frequency_mhz);
    lv_label_set_text_fmt(monitor_ui.gpu.detail_label,
                          "Used / Total\n%s\n%s\n%s", first, second, third);

    pc_ui_format_rate(first, sizeof(first), total_network);
    pc_ui_format_rate(second, sizeof(second), snapshot->net_upload_kib_s);
    pc_ui_format_rate(third, sizeof(third), snapshot->net_download_kib_s);
    lv_label_set_text_fmt(monitor_ui.network_value_label,
                          "%s\n%s\n%s",
                          first, second, third);

    pc_ui_format_rate(first, sizeof(first), snapshot->disk_read_kib_s);
    pc_ui_format_rate(second, sizeof(second), snapshot->disk_write_kib_s);
    if (snapshot->disk_usage >= 0)
        lv_label_set_text_fmt(monitor_ui.disk_value_label,
                              "%d%%\n%s\n%s",
                              snapshot->disk_usage, first, second);
    else
        lv_label_set_text_fmt(monitor_ui.disk_value_label,
                              "--\n%s\n%s",
                              first, second);

    pc_ui_update_models(snapshot);
    pc_ui_update_history(snapshot);
}

static void pc_ui_refresh(lv_timer_t *timer)
{
    pc_monitor_snapshot_t snapshot;

    (void)timer;
    (void)pc_monitor_service_get_snapshot(&snapshot);
    if (snapshot.version == monitor_ui.rendered_version) return;
    monitor_ui.rendered_version = snapshot.version;

    if (!snapshot.valid)
    {
        lv_label_set_text(monitor_ui.connection_label, "等待电脑数据");
        lv_obj_set_style_text_color(monitor_ui.connection_label,
                                    lv_color_hex(PC_UI_WARNING_COLOR), 0);
        return;
    }
    if (snapshot.connected)
    {
        lv_label_set_text(monitor_ui.connection_label, "已连接");
        lv_obj_set_style_text_color(monitor_ui.connection_label,
                                    lv_color_hex(PC_UI_GPU_COLOR), 0);
    }
    else
    {
        lv_label_set_text(monitor_ui.connection_label, "数据已断开");
        lv_obj_set_style_text_color(monitor_ui.connection_label,
                                    lv_color_hex(PC_UI_WARNING_COLOR), 0);
    }
    pc_ui_update_values(&snapshot);
}

static void pc_ui_create_header(lv_obj_t *root)
{
    lv_obj_t *header = pc_ui_panel(root, false);
    lv_obj_t *title = pc_ui_label(header, "性能监视器", PC_UI_TITLE_COLOR);
    lv_obj_t *home_button;

    lv_obj_set_grid_cell(header, LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);
    monitor_ui.connection_label = pc_ui_label(header, "等待电脑数据",
                                               PC_UI_WARNING_COLOR);
    lv_obj_align(monitor_ui.connection_label, LV_ALIGN_CENTER, 0, 0);
    home_button = pc_ui_button(header, "返回主页");
    lv_obj_align(home_button, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(home_button, pc_ui_home_clicked, LV_EVENT_CLICKED, NULL);
}

static void pc_ui_create_metrics(lv_obj_t *root)
{
    static const lv_coord_t columns[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t rows[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_t *area = pc_ui_panel(root, false);
    lv_obj_t *card;

    lv_obj_set_grid_cell(area, LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_dsc_array(area, columns, rows);
    lv_obj_set_style_pad_gap(area, 8, 0);

    card = pc_ui_panel(area, true);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    monitor_ui.cpu = pc_ui_create_metric(card, "CPU", PC_UI_CPU_COLOR);
    card = pc_ui_panel(area, true);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 1, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    monitor_ui.ram = pc_ui_create_metric(card, "RAM", PC_UI_RAM_COLOR);
    card = pc_ui_panel(area, true);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, 2, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    monitor_ui.gpu = pc_ui_create_metric(card, "GPU", PC_UI_GPU_COLOR);
    /* 四行参数从型号下方开始，每行占固定高度，避免向上挤到型号。 */
    lv_obj_set_style_text_line_space(monitor_ui.gpu.detail_label, 0, 0);
    lv_obj_set_height(monitor_ui.gpu.detail_label, 100);
    lv_obj_align(monitor_ui.gpu.detail_label, LV_ALIGN_TOP_RIGHT, 0, 53);
}

static void pc_ui_chart_tick_label(lv_event_t *event)
{
    lv_obj_draw_part_dsc_t *part = lv_event_get_draw_part_dsc(event);

    if (part->type != LV_CHART_DRAW_PART_TICK_LABEL || part->text == NULL)
        return;
    if (part->id == LV_CHART_AXIS_PRIMARY_Y)
        (void)snprintf(part->text, part->text_length, "%ld%%", (long)part->value);
    else if (part->id == LV_CHART_AXIS_PRIMARY_X)
    {
        /* 折线图的横轴回调值是刻度序号，不是设定的时间范围。 */
        static const char *const labels[] = {"60s", "30s", "0"};
        if (part->value >= 0 && part->value < 3)
            (void)snprintf(part->text, part->text_length, "%s", labels[part->value]);
    }
}

static void pc_ui_create_overview(lv_obj_t *root)
{
    static const lv_coord_t columns[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
    };
    static const lv_coord_t rows[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_t *area = pc_ui_panel(root, false);
    lv_obj_t *chart_card;
    lv_obj_t *side;
    lv_obj_t *network_card;
    lv_obj_t *disk_card;
    lv_obj_t *legend;
    lv_obj_t *field_label;

    lv_obj_set_grid_cell(area, LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_set_grid_dsc_array(area, columns, rows);
    lv_obj_set_style_pad_gap(area, 8, 0);

    chart_card = pc_ui_panel(area, true);
    lv_obj_set_grid_cell(chart_card, LV_GRID_ALIGN_STRETCH, 0, 2,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    (void)pc_ui_label(chart_card, "60 秒性能趋势", PC_UI_TEXT_COLOR);
    legend = pc_ui_small_label(chart_card, "GPU 显存", PC_UI_GPU_COLOR);
    lv_obj_align(legend, LV_ALIGN_TOP_RIGHT, -20, 5);
    legend = pc_ui_small_label(chart_card, "CPU", PC_UI_CPU_COLOR);
    lv_obj_align(legend, LV_ALIGN_TOP_RIGHT, -130, 5);
    monitor_ui.history_chart = lv_chart_create(chart_card);
    lv_obj_set_size(monitor_ui.history_chart, lv_pct(86), 118);
    lv_obj_align(monitor_ui.history_chart, LV_ALIGN_BOTTOM_MID, 14, -20);
    lv_chart_set_type(monitor_ui.history_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(monitor_ui.history_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_range(monitor_ui.history_chart, LV_CHART_AXIS_PRIMARY_X, -60, 0);
    lv_chart_set_point_count(monitor_ui.history_chart, PC_MONITOR_HISTORY_POINTS);
    lv_chart_set_div_line_count(monitor_ui.history_chart, 5, 7);
    lv_chart_set_axis_tick(monitor_ui.history_chart, LV_CHART_AXIS_PRIMARY_Y,
                           4, 2, 5, 1, true, 52);
    lv_chart_set_axis_tick(monitor_ui.history_chart, LV_CHART_AXIS_PRIMARY_X,
                           4, 2, 3, 1, true, 28);
    lv_obj_add_event_cb(monitor_ui.history_chart, pc_ui_chart_tick_label,
                        LV_EVENT_DRAW_PART_BEGIN, NULL);
    lv_obj_set_style_text_font(monitor_ui.history_chart,
                               &ui_font_chinese_20, LV_PART_TICKS);
    lv_obj_set_style_width(monitor_ui.history_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(monitor_ui.history_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(monitor_ui.history_chart, 2, LV_PART_ITEMS);
    monitor_ui.cpu_series = lv_chart_add_series(monitor_ui.history_chart,
                                                 lv_color_hex(PC_UI_CPU_COLOR),
                                                 LV_CHART_AXIS_PRIMARY_Y);
    monitor_ui.vram_series = lv_chart_add_series(monitor_ui.history_chart,
                                                  lv_color_hex(PC_UI_GPU_COLOR),
                                                  LV_CHART_AXIS_PRIMARY_Y);

    side = pc_ui_panel(area, false);
    lv_obj_set_grid_cell(side, LV_GRID_ALIGN_STRETCH, 2, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, 8, 0);

    network_card = pc_ui_panel(side, true);
    lv_obj_set_width(network_card, lv_pct(100));
    lv_obj_set_flex_grow(network_card, 1);
    (void)pc_ui_label(network_card, "网络", PC_UI_TEXT_COLOR);
    field_label = pc_ui_small_label(network_card, "总网速\n上传\n下载",
                                    PC_UI_MUTED_COLOR);
    lv_obj_set_style_text_line_space(field_label, 0, 0);
    /* 三列固定起点：标题、字段、数值，数值变长时起点不移动。 */
    lv_obj_set_pos(field_label, 64, 0);
    monitor_ui.network_value_label = pc_ui_small_label(network_card,
                                                        "--\n--\n--",
                                                        PC_UI_TEXT_COLOR);
    lv_obj_set_width(monitor_ui.network_value_label, lv_pct(45));
    lv_obj_set_style_text_align(monitor_ui.network_value_label,
                                LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(monitor_ui.network_value_label, 0, 0);
    lv_obj_set_pos(monitor_ui.network_value_label, 130, 0);
    lv_label_set_long_mode(monitor_ui.network_value_label, LV_LABEL_LONG_CLIP);

    disk_card = pc_ui_panel(side, true);
    lv_obj_set_width(disk_card, lv_pct(100));
    lv_obj_set_flex_grow(disk_card, 1);
    (void)pc_ui_label(disk_card, "磁盘", PC_UI_TEXT_COLOR);
    field_label = pc_ui_small_label(disk_card, "使用率\n读取\n写入",
                                    PC_UI_MUTED_COLOR);
    lv_obj_set_style_text_line_space(field_label, 0, 0);
    lv_obj_set_pos(field_label, 64, 0);
    monitor_ui.disk_value_label = pc_ui_small_label(disk_card,
                                                     "--\n--\n--",
                                                     PC_UI_TEXT_COLOR);
    lv_obj_set_width(monitor_ui.disk_value_label, lv_pct(45));
    lv_obj_set_style_text_align(monitor_ui.disk_value_label,
                                LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(monitor_ui.disk_value_label, 0, 0);
    lv_obj_set_pos(monitor_ui.disk_value_label, 130, 0);
    lv_label_set_long_mode(monitor_ui.disk_value_label, LV_LABEL_LONG_CLIP);
}

void ui_pc_monitor_create(lv_obj_t *parent)
{
    static const lv_coord_t columns[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t rows[] = {42, 174, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    if (monitor_ui.root != NULL)
    {
        lv_obj_move_foreground(monitor_ui.root);
        return;
    }

    memset(&monitor_ui, 0, sizeof(monitor_ui));
    monitor_ui.rendered_version = UINT32_MAX;
    monitor_ui.rendered_sequence = UINT32_MAX;
    monitor_ui.root = pc_ui_panel(parent, false);
    lv_obj_set_size(monitor_ui.root, lv_pct(100), lv_pct(92));
    lv_obj_set_y(monitor_ui.root, lv_pct(8));
    lv_obj_set_style_bg_color(monitor_ui.root, lv_color_hex(PC_UI_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(monitor_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(monitor_ui.root, 8, 0);
    lv_obj_set_style_pad_gap(monitor_ui.root, 6, 0);
    lv_obj_set_grid_dsc_array(monitor_ui.root, columns, rows);
    lv_obj_add_event_cb(monitor_ui.root, pc_ui_root_deleted, LV_EVENT_DELETE, NULL);

    pc_ui_create_header(monitor_ui.root);
    pc_ui_create_metrics(monitor_ui.root);
    pc_ui_create_overview(monitor_ui.root);
    monitor_ui.refresh_timer = lv_timer_create(pc_ui_refresh, 500U, NULL);
    pc_ui_refresh(NULL);
}
