#include "ui_music.h"
#include "music_service.h"
#include "audio_spectrum_service.h"
#include "ui_fonts.h"
#include "ui_images.h"
#include <math.h>
#include <string.h>

/* 视觉参考LVGL music demo：唱片、镜像环形频谱和曲目列表；数据均来自真实服务。 */
typedef struct
{
    lv_obj_t *root;
    lv_obj_t *disc;
    lv_obj_t *cover;
    lv_obj_t *title;
    lv_obj_t *status;
    lv_obj_t *time;
    lv_obj_t *duration;
    lv_obj_t *list;
    lv_obj_t *play_text;
    lv_obj_t *mode_icon;
    lv_obj_t *mode_overlay;
    lv_obj_t *volume;
    lv_obj_t *volume_text;
    lv_obj_t *volume_speaker;
    lv_obj_t *volume_button;
    lv_obj_t *volume_overlay;
    lv_obj_t *track_count;
    lv_obj_t *source;
    lv_obj_t *progress;
    lv_timer_t *timer;
    uint32_t list_version;
    uint16_t rotation;
    uint8_t mode;
    uint8_t volume_value;
    bool request_failed;
    audio_spectrum_snapshot_t spectrum;
} music_ui_t;
static music_ui_t ui;
/* 退出播放器页面后仍保留可恢复的音量。 */
static uint8_t last_audible_volume = 50;

static lv_obj_t *panel(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_style_bg_color(obj, lv_color_white(), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xE4EAF1), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_pad_all(obj, 8, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void command_cb(lv_event_t *event)
{
    music_command_t command = (music_command_t)(uintptr_t)lv_event_get_user_data(event);
    uint32_t value = command == MUSIC_PLAY ? (uint32_t)lv_obj_get_index(lv_event_get_target(event)) : 0;
    ui.request_failed = !music_service_command(command, value);
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, music_command_t command)
{
    lv_obj_t *obj = lv_btn_create(parent);
    lv_obj_set_size(obj, 54, 44);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xE6F0FF), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x2196F3), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(obj, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_text_font(obj, &ui_font_chinese_24_4, 0);
    lv_obj_add_event_cb(obj, command_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)command);
    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return label;
}

/* 播放键突出主操作，其余按键用轻底色分组；统一圆角和按下反馈。 */
static lv_obj_t *playback_button(lv_obj_t *parent, const char *text, music_command_t command)
{
    lv_obj_t *label = button(parent, text, command);
    lv_obj_t *obj = lv_obj_get_parent(label);
    lv_obj_set_size(obj, 48, 48);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, lv_color_white(), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0x3C5875), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xE0E8F2), 0);
    lv_obj_set_style_translate_y(obj, 1, LV_STATE_PRESSED);

    if (command == MUSIC_TOGGLE)
    {
        lv_obj_set_size(obj, 60, 60);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x2196F3), 0);
        lv_obj_set_style_text_color(obj, lv_color_white(), 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x1976D2), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_color(obj, lv_color_hex(0x2196F3), 0);
        lv_obj_set_style_shadow_width(obj, 8, 0);
        lv_obj_set_style_shadow_opa(obj, LV_OPA_20, 0);
        lv_obj_set_style_shadow_width(obj, 2, LV_STATE_PRESSED);
    }
    return label;
}

/* 播放模式只切换图片，按钮和菜单使用同一组资源。 */
static const lv_img_dsc_t *mode_image_get(uint8_t mode)
{
    switch (mode)
    {
    case MUSIC_PLAY_MODE_SHUFFLE: return &ui_img_music_shuffle;
    case MUSIC_PLAY_MODE_REPEAT_ONE: return &ui_img_music_repeat_one;
    default: return &ui_img_music_order;
    }
}

static void mode_overlay_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) != ui.mode_overlay) return;
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) lv_obj_del(ui.mode_overlay);
    else if (lv_event_get_code(event) == LV_EVENT_DELETE)
    {
        ui.mode_overlay = NULL;
        if (ui.mode_icon != NULL) lv_obj_clear_state(ui.mode_icon, LV_STATE_CHECKED);
    }
}

static void mode_select_cb(lv_event_t *event)
{
    uint8_t mode = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    ui.request_failed = !music_service_command(MUSIC_MODE, mode);
    if (!ui.request_failed)
    {
        ui.mode = mode;
        lv_obj_set_style_bg_img_src(ui.mode_icon, mode_image_get(ui.mode), 0);
    }
    lv_obj_del(ui.mode_overlay);
}

static lv_obj_t *mode_option(lv_obj_t *parent, const char *text, uint8_t mode)
{
    lv_obj_t *row = lv_btn_create(parent);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 52);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_white(), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xF0F4F8), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xE6F0FF), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(row, lv_color_hex(0x243247), 0);
    lv_obj_set_style_text_color(row, lv_color_hex(0x2196F3), LV_STATE_CHECKED);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *icon = lv_img_create(row);
    lv_img_set_src(icon, mode_image_get(mode));
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, text);
    if (mode == ui.mode) lv_obj_add_state(row, LV_STATE_CHECKED);
    lv_obj_add_event_cb(row, mode_select_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)mode);
    return row;
}

static void mode_popup_cb(lv_event_t *event)
{
    (void)event;
    if (ui.mode_overlay != NULL)
    {
        lv_obj_del(ui.mode_overlay);
        return;
    }
    if (ui.volume_overlay != NULL) lv_obj_del(ui.volume_overlay);
    ui.mode_overlay = lv_obj_create(ui.root);
    lv_obj_remove_style_all(ui.mode_overlay);
    lv_obj_add_flag(ui.mode_overlay, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui.mode_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui.mode_overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(ui.mode_overlay);
    lv_obj_add_event_cb(ui.mode_overlay, mode_overlay_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui.mode_overlay, mode_overlay_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *popup = lv_obj_create(ui.mode_overlay);
    lv_obj_set_size(popup, LV_SIZE_CONTENT, 180);
    lv_obj_set_style_bg_color(popup, lv_color_white(), 0);
    lv_obj_set_style_border_color(popup, lv_color_hex(0xE4EAF1), 0);
    lv_obj_set_style_border_width(popup, 1, 0);
    lv_obj_set_style_radius(popup, 12, 0);
    lv_obj_set_style_shadow_width(popup, 16, 0);
    lv_obj_set_style_shadow_opa(popup, LV_OPA_20, 0);
    lv_obj_set_style_pad_all(popup, 7, 0);
    lv_obj_set_style_pad_row(popup, 4, 0);
    lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
    mode_option(popup, "随机播放", MUSIC_PLAY_MODE_SHUFFLE);
    mode_option(popup, "顺序播放", MUSIC_PLAY_MODE_ORDER);
    mode_option(popup, "单曲循环", MUSIC_PLAY_MODE_REPEAT_ONE);
    lv_obj_add_state(ui.mode_icon, LV_STATE_CHECKED);
    lv_obj_update_layout(ui.root);
    lv_obj_align_to(popup, ui.mode_icon, LV_ALIGN_OUT_TOP_LEFT, 0, -10);
}

static const lv_img_dsc_t *volume_image_get(uint8_t volume)
{
    return volume == 0 ? &ui_img_music_volume_cross : &ui_img_music_volume_high;
}

/* 同步控制栏与音量弹窗的显示。 */
static void volume_update_widgets(uint8_t volume)
{
    bool was_muted = ui.volume_value == 0;
    ui.volume_value = volume > 100 ? 100 : volume;
    if (ui.volume_value != 0) last_audible_volume = ui.volume_value;
    if (was_muted != (ui.volume_value == 0))
    {
        const lv_img_dsc_t *image = volume_image_get(ui.volume_value);
        lv_obj_set_style_bg_img_src(ui.volume_button, image, 0);
        if (ui.volume_speaker != NULL)
            lv_obj_set_style_bg_img_src(ui.volume_speaker, image, 0);
    }
    if (ui.volume != NULL && lv_slider_get_value(ui.volume) != ui.volume_value)
        lv_slider_set_value(ui.volume, ui.volume_value, LV_ANIM_OFF);
    if (ui.volume_text != NULL)
        lv_label_set_text_fmt(ui.volume_text, "%u%%", ui.volume_value);
}

/* 更新界面并把新音量提交给音乐服务。 */
static void volume_request(uint8_t volume)
{
    ui.request_failed = !music_service_command(MUSIC_VOLUME, volume);
    if (!ui.request_failed) volume_update_widgets(volume);
}

static void volume_cb(lv_event_t *event)
{
    volume_request((uint8_t)lv_slider_get_value(lv_event_get_target(event)));
}

static void volume_toggle_cb(lv_event_t *event)
{
    (void)event;
    volume_request(ui.volume_value == 0 ? last_audible_volume : 0);
}

static void volume_overlay_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) != ui.volume_overlay) return;
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) lv_obj_del(ui.volume_overlay);
    else if (lv_event_get_code(event) == LV_EVENT_DELETE)
    {
        /* 弹层销毁后，刷新定时器不能再访问其中的滑条和标签。 */
        ui.volume_overlay = ui.volume = ui.volume_text = ui.volume_speaker = NULL;
        if (ui.volume_button != NULL) lv_obj_clear_state(ui.volume_button, LV_STATE_CHECKED);
    }
}

static void volume_popup_cb(lv_event_t *event)
{
    (void)event;
    if (ui.volume_overlay != NULL)
    {
        lv_obj_del(ui.volume_overlay);
        return;
    }
    if (ui.mode_overlay != NULL) lv_obj_del(ui.mode_overlay);
    /* 透明遮罩只负责收起弹窗，避免点击外部时误触其他播放操作。 */
    ui.volume_overlay = lv_obj_create(ui.root);
    lv_obj_remove_style_all(ui.volume_overlay);
    lv_obj_add_flag(ui.volume_overlay, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui.volume_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui.volume_overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(ui.volume_overlay);
    lv_obj_add_event_cb(ui.volume_overlay, volume_overlay_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui.volume_overlay, volume_overlay_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *popup = lv_obj_create(ui.volume_overlay);
    lv_obj_remove_style_all(popup);
    lv_obj_add_flag(popup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(popup, 94, 232);
    lv_obj_set_style_radius(popup, 12, 0);
    lv_obj_set_style_bg_color(popup, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(popup, lv_color_hex(0xE4EAF1), 0);
    lv_obj_set_style_border_width(popup, 1, 0);
    lv_obj_set_style_shadow_width(popup, 16, 0);
    lv_obj_set_style_shadow_opa(popup, LV_OPA_20, 0);
    lv_obj_set_style_text_color(popup, lv_color_hex(0x243247), 0);
    ui.volume = lv_slider_create(popup);
    lv_obj_remove_style_all(ui.volume);
    lv_obj_set_size(ui.volume, 4, 126);
    lv_obj_align(ui.volume, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_ext_click_area(ui.volume, 16);
    lv_obj_set_style_bg_opa(ui.volume, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.volume, lv_color_hex(0xE6F0FF), LV_PART_MAIN);
    lv_obj_set_style_radius(ui.volume, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui.volume, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui.volume, lv_color_hex(0x2196F3), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui.volume, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ui.volume, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(ui.volume, lv_color_hex(0x2196F3), LV_PART_KNOB);
    lv_obj_set_style_radius(ui.volume, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(ui.volume, 4, LV_PART_KNOB);
    lv_slider_set_range(ui.volume, 0, 100);
    lv_slider_set_value(ui.volume, ui.volume_value, LV_ANIM_OFF);
    lv_obj_add_event_cb(ui.volume, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ui.volume_text = lv_label_create(popup);
    lv_label_set_text_fmt(ui.volume_text, "%u%%", ui.volume_value);
    lv_obj_align(ui.volume_text, LV_ALIGN_TOP_MID, 0, 154);
    lv_obj_t *divider = lv_obj_create(popup);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, lv_pct(78), 1);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0xE4EAF1), 0);
    ui.volume_speaker = lv_btn_create(popup);
    lv_obj_set_size(ui.volume_speaker, 64, 32);
    lv_obj_set_style_radius(ui.volume_speaker, 8, 0);
    lv_obj_set_style_shadow_width(ui.volume_speaker, 0, 0);
    lv_obj_set_style_bg_color(ui.volume_speaker, lv_color_hex(0xF3F7FD), 0);
    lv_obj_set_style_bg_color(ui.volume_speaker, lv_color_hex(0x2196F3), LV_STATE_PRESSED);
    lv_obj_set_style_bg_img_src(ui.volume_speaker, volume_image_get(ui.volume_value), 0);
    lv_obj_set_style_bg_img_recolor(ui.volume_speaker, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_img_recolor_opa(ui.volume_speaker, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(ui.volume_speaker, volume_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(ui.volume_speaker, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_state(ui.volume_button, LV_STATE_CHECKED);
    lv_obj_update_layout(ui.root);
    lv_obj_align_to(popup, ui.volume_button, LV_ALIGN_OUT_TOP_MID, 0, -10);
}

static void home_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_del(ui.root);
}

static void deleted_cb(lv_event_t *event)
{
    if (lv_event_get_target(event) != ui.root) return;
    /* 页面离开仅停止UI定时器；播放由服务独立拥有。 */
    if (ui.timer != NULL) lv_timer_del(ui.timer);
    memset(&ui, 0, sizeof(ui));
}

static void spectrum_draw(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);
    lv_draw_ctx_t *draw = lv_event_get_draw_ctx(event);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    int cx = (area.x1 + area.x2) / 2;
    int cy = (area.y1 + area.y2) / 2;
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 3;
    line.round_start = 1;
    line.round_end = 1;
    for (unsigned side = 0; side < 2; side++)
        for (unsigned band = 0; band < MUSIC_SPECTRUM_BANDS; band++)
        {
            float angle = ((float)band / MUSIC_SPECTRUM_BANDS * 3.14159265f) * (side ? -1.0f : 1.0f);
            float length = 2.0f + ui.spectrum.levels[band] * 0.20f;
            lv_point_t from = {cx + (lv_coord_t)(sinf(angle) * 72), cy - (lv_coord_t)(cosf(angle) * 72)};
            lv_point_t to = {cx + (lv_coord_t)(sinf(angle) * (72 + length)),
                            cy - (lv_coord_t)(cosf(angle) * (72 + length))};
            line.color = band < 16 ? lv_color_hex(0x2196F3) : lv_color_hex(0x7E57C2);
            lv_draw_line(draw, &line, &from, &to);
        }
}

static void refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    music_snapshot_t snapshot;
    music_service_snapshot(&snapshot);
    audio_spectrum_snapshot(&ui.spectrum);
    if (snapshot.state == MUSIC_PLAYING)
    {
        ui.rotation = (ui.rotation + 8U) % 3600U;
        lv_img_set_angle(ui.cover, ui.rotation);
    }
    lv_obj_invalidate(ui.disc);
    const char *title = snapshot.title[0] ? snapshot.title : "选择喜欢的音乐";
    if (strcmp(lv_label_get_text(ui.title) + 1, title) != 0) lv_label_set_text_fmt(ui.title, " %s", title);
    /* 顶栏保留固定提示区域，错误出现时不挤压播放列表。 */
    bool show_error = ui.request_failed || snapshot.state == MUSIC_ERROR ||
                      strstr(snapshot.message, "失败") != NULL;
    if (show_error)
    {
        const char *status = ui.request_failed ? "请求繁忙，请重试" : snapshot.message;
        if (strcmp(lv_label_get_text(ui.status), status) != 0) lv_label_set_text(ui.status, status);
    }
    else if (lv_label_get_text(ui.status)[0]) lv_label_set_text(ui.status, "");
    lv_label_set_text_fmt(ui.time, "%02lu:%02lu",
                         (unsigned long)(snapshot.seconds / 60), (unsigned long)(snapshot.seconds % 60));
    if (snapshot.duration_seconds)
        lv_label_set_text_fmt(ui.duration, "%s%02lu:%02lu", snapshot.duration_estimated ? "~" : "",
                             (unsigned long)(snapshot.duration_seconds / 60),
                             (unsigned long)(snapshot.duration_seconds % 60));
    else lv_label_set_text(ui.duration, "--:--");
    uint32_t progress = snapshot.duration_seconds ?
        (uint32_t)((uint64_t)snapshot.seconds * 1000U / snapshot.duration_seconds) : 0;
    lv_bar_set_value(ui.progress, progress > 1000U ? 1000 : (int32_t)progress, LV_ANIM_OFF);
    lv_label_set_text(ui.play_text, snapshot.state == MUSIC_PLAYING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (ui.mode != snapshot.mode % 3)
    {
        ui.mode = snapshot.mode % 3;
        lv_obj_set_style_bg_img_src(ui.mode_icon, mode_image_get(ui.mode), 0);
    }
    if (ui.volume == NULL || !lv_obj_has_state(ui.volume, LV_STATE_PRESSED))
    {
        volume_update_widgets(snapshot.volume);
    }
    lv_label_set_text_fmt(ui.track_count, "%u 首", snapshot.count);
    const char *source = snapshot.state == MUSIC_PLAYING ? "正在播放" :
                         snapshot.state == MUSIC_PAUSED ? "已暂停" : "本地音乐";
    if (strcmp(lv_label_get_text(ui.source), source) != 0) lv_label_set_text(ui.source, source);
    if (snapshot.state != MUSIC_SCANNING && snapshot.playlist_version != ui.list_version)
    {
        lv_obj_clean(ui.list);
        for (unsigned i = 0; i < snapshot.count; i++)
        {
            char name[MUSIC_NAME_CAP];
            if (!music_service_track_name(i, name, sizeof(name))) continue;
            lv_obj_t *entry = lv_list_add_btn(ui.list, LV_SYMBOL_AUDIO, name);
            /* J等斜体字形有负左边距；显示前留一个空格，避免滚动标签裁掉左侧笔画。 */
            lv_obj_t *name_label = lv_obj_get_child(entry, 1);
            lv_label_set_text_fmt(name_label, " %s", name);
            lv_obj_set_height(entry, 56);
            lv_obj_set_style_pad_hor(entry, 10, 0);
            lv_obj_set_style_pad_column(entry, 10, 0);
            lv_obj_set_style_radius(entry, 8, 0);
            lv_obj_set_style_text_color(entry, lv_color_hex(0x243247), 0);
            lv_obj_set_style_bg_color(entry, lv_color_hex(0xE6F0FF), LV_STATE_CHECKED);
            lv_obj_set_style_text_color(entry, lv_color_hex(0x2196F3), LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(entry, lv_color_hex(0xF0F4F8), LV_STATE_PRESSED);
            lv_obj_add_event_cb(entry, command_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)MUSIC_PLAY);
        }
        ui.list_version = snapshot.playlist_version;
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(ui.list); i++)
    {
        lv_obj_t *entry = lv_obj_get_child(ui.list, i);
        if (snapshot.title[0] && i == snapshot.track) lv_obj_add_state(entry, LV_STATE_CHECKED);
        else lv_obj_clear_state(entry, LV_STATE_CHECKED);
    }
}

/* 以原生圆形对象绘制默认唱片，避免放大48px启动图标作为整张封面。 */
static void create_record(lv_obj_t *parent)
{
    lv_obj_t *record = panel(parent);
    lv_obj_set_size(record, 132, 132);
    lv_obj_center(record);
    lv_obj_set_style_radius(record, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(record, lv_color_hex(0x243247), 0);
    lv_obj_set_style_border_width(record, 0, 0);
    lv_obj_set_style_shadow_width(record, 12, 0);
    lv_obj_set_style_shadow_opa(record, LV_OPA_20, 0);
    for (unsigned i = 0; i < 4; i++)
    {
        lv_obj_t *ring = lv_obj_create(record);
        lv_obj_remove_style_all(ring);
        lv_obj_set_size(ring, 120 - i * 12, 120 - i * 12);
        lv_obj_center(ring);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(0x3B4A60), 0);
    }
    lv_obj_t *center = panel(record);
    lv_obj_set_size(center, 64, 64);
    lv_obj_center(center);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center, lv_color_hex(0xE6F0FF), 0);
    lv_obj_set_style_border_width(center, 0, 0);
    ui.cover = lv_img_create(center);
    lv_img_set_src(ui.cover, &ui_img_icon_music);
    lv_obj_center(ui.cover);
}

void ui_music_create(lv_obj_t *parent)
{
    if (ui.root != NULL) return;
    ui.root = panel(parent);
    lv_obj_set_size(ui.root, lv_pct(100), lv_pct(92));
    lv_obj_set_y(ui.root, lv_pct(8));
    lv_obj_set_style_bg_color(ui.root, lv_color_hex(0xF5F7FA), 0);
    lv_obj_set_style_text_font(ui.root, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(ui.root, lv_color_hex(0x243247), 0);
    lv_obj_set_flex_flow(ui.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui.root, 8, 0);
    lv_obj_add_event_cb(ui.root, deleted_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *header = panel(ui.root);
    lv_obj_set_size(header, lv_pct(100), 46);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(header, 18, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(header, 16, 0);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "音乐播放器");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE91E63), 0);
    ui.status = lv_label_create(header);
    lv_obj_set_width(ui.status, 0);
    lv_obj_set_flex_grow(ui.status, 1);
    lv_label_set_long_mode(ui.status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(ui.status, lv_color_hex(0xD65A6A), 0);
    lv_label_set_text(ui.status, "");
    lv_obj_t *home = lv_btn_create(header);
    lv_obj_set_size(home, 112, 38);
    lv_obj_set_style_bg_color(home, lv_color_white(), 0);
    lv_obj_set_style_text_color(home, lv_color_hex(0x243247), 0);
    lv_obj_set_style_radius(home, 6, 0);
    lv_obj_set_style_shadow_width(home, 0, 0);
    lv_obj_set_style_border_width(home, 1, 0);
    lv_obj_set_style_border_color(home, lv_color_hex(0xE4EAF1), 0);
    lv_obj_set_style_bg_color(home, lv_color_hex(0xF44336), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(home, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(home, home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *home_text = lv_label_create(home);
    lv_label_set_text(home_text, "返回主页"); lv_obj_center(home_text);
    lv_obj_t *body = panel(ui.root);
    lv_obj_set_width(body, lv_pct(100)); lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_all(body, 0, 0); lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, 10, 0);
    lv_obj_t *player = panel(body);
    lv_obj_set_size(player, lv_pct(62), lv_pct(100));
    lv_obj_set_style_pad_all(player, 12, 0);
    /* 唱片、播放控制、时间进度固定分区；音量通过控制栏弹层设置。 */
    static const lv_coord_t cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const lv_coord_t rows[] = {LV_GRID_FR(1), 68, 34, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(player, cols, rows);
    lv_obj_set_style_pad_row(player, 7, 0);
    lv_obj_t *stage = panel(player);
    lv_obj_set_grid_cell(stage, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_style_bg_color(stage, lv_color_hex(0xF3F7FD), 0);
    lv_obj_set_style_border_width(stage, 0, 0);
    lv_obj_set_style_pad_all(stage, 0, 0);
    ui.disc = lv_obj_create(player);
    lv_obj_remove_style_all(ui.disc);
    /* 唱片舞台是浮动子对象，不参与Grid分配；环形频谱只读取真实FFT快照。 */
    lv_obj_set_parent(ui.disc, stage);
    lv_obj_set_size(ui.disc, 194, 194);
    lv_obj_align(ui.disc, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(ui.disc, spectrum_draw, LV_EVENT_DRAW_MAIN_END, NULL);
    create_record(ui.disc);
    lv_obj_t *info = lv_obj_create(stage);
    lv_obj_remove_style_all(info);
    lv_obj_set_size(info, lv_pct(54), 160);
    lv_obj_align(info, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info, 10, 0);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui.source = lv_label_create(info);
    lv_obj_set_style_text_color(ui.source, lv_color_hex(0x7E57C2), 0);
    ui.title = lv_label_create(player);
    lv_obj_set_parent(ui.title, info);
    lv_obj_set_width(ui.title, lv_pct(100));
    lv_label_set_long_mode(ui.title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(ui.title, " ");
    lv_obj_set_style_text_color(ui.title, lv_color_hex(0x243247), 0);
    lv_obj_t *controls = lv_obj_create(player);
    lv_obj_remove_style_all(controls);
    lv_obj_set_grid_cell(controls, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_style_bg_opa(controls, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(controls, lv_color_hex(0xF3F7FD), 0);
    lv_obj_set_style_radius(controls, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(controls, 1, 0);
    lv_obj_set_style_border_color(controls, lv_color_hex(0xE7EDF5), 0);
    lv_obj_set_style_pad_hor(controls, 8, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *mode_label = playback_button(controls, "", MUSIC_MODE);
    ui.mode_icon = lv_obj_get_parent(mode_label);
    lv_obj_remove_event_cb(ui.mode_icon, command_cb);
    lv_obj_del(mode_label);
    lv_obj_set_style_bg_img_src(ui.mode_icon, mode_image_get(ui.mode), 0);
    lv_obj_set_style_bg_img_recolor(ui.mode_icon, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_img_recolor_opa(ui.mode_icon, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(ui.mode_icon, mode_popup_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(ui.mode_icon, lv_color_hex(0xE6F0FF), LV_STATE_CHECKED);
    playback_button(controls, LV_SYMBOL_PREV, MUSIC_PREVIOUS);
    ui.play_text = playback_button(controls, LV_SYMBOL_PLAY, MUSIC_TOGGLE);
    playback_button(controls, LV_SYMBOL_NEXT, MUSIC_NEXT);
    lv_obj_t *volume_label = playback_button(controls, "", MUSIC_VOLUME);
    ui.volume_button = lv_obj_get_parent(volume_label);
    /* 点击喇叭仅展开滑条，不能触发通用命令回调把音量直接设为0。 */
    lv_obj_remove_event_cb(ui.volume_button, command_cb);
    lv_obj_del(volume_label);
    lv_obj_set_style_bg_img_src(ui.volume_button, volume_image_get(ui.volume_value), 0);
    lv_obj_set_style_bg_img_recolor(ui.volume_button, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_img_recolor_opa(ui.volume_button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(ui.volume_button, volume_popup_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(ui.volume_button, lv_color_hex(0xE6F0FF), LV_STATE_CHECKED);
    lv_obj_t *timeline = lv_obj_create(player);
    lv_obj_remove_style_all(timeline);
    lv_obj_set_grid_cell(timeline, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_set_flex_flow(timeline, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(timeline, 10, 0);
    lv_obj_set_flex_align(timeline, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui.time = lv_label_create(timeline);
    lv_obj_set_width(ui.time, 72);
    lv_obj_set_style_text_color(ui.time, lv_color_hex(0x69788B), 0);
    ui.progress = lv_bar_create(timeline);
    lv_obj_set_size(ui.progress, 0, 6);
    lv_obj_set_flex_grow(ui.progress, 1);
    lv_bar_set_range(ui.progress, 0, 1000);
    lv_obj_set_style_bg_color(ui.progress, lv_color_hex(0xE4EAF1), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.progress, lv_color_hex(0x2196F3), LV_PART_INDICATOR);
    /* 这里只显示播放进度；没有添加尚未实现的拖动定位功能。 */
    ui.duration = lv_label_create(timeline);
    lv_obj_set_width(ui.duration, 84);
    lv_obj_set_style_text_align(ui.duration, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(ui.duration, lv_color_hex(0x69788B), 0);
    lv_obj_t *right = panel(body);
    lv_obj_set_size(right, 0, lv_pct(100)); lv_obj_set_flex_grow(right, 1);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 8, 0);
    lv_obj_t *toolbar = lv_obj_create(right);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_size(toolbar, lv_pct(100), 38);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *list_title = lv_label_create(toolbar);
    lv_label_set_text(list_title, "播放列表");
    lv_obj_set_style_text_color(list_title, lv_color_hex(0x243247), 0);
    ui.track_count = lv_label_create(toolbar);
    lv_obj_set_style_text_color(ui.track_count, lv_color_hex(0x69788B), 0);
    lv_obj_t *scan = button(toolbar, "刷新", MUSIC_SCAN);
    lv_obj_set_size(lv_obj_get_parent(scan), 76, 36);
    lv_obj_set_style_bg_color(lv_obj_get_parent(scan), lv_color_hex(0x4CAF50), LV_STATE_PRESSED);
    ui.list = lv_list_create(right);
    lv_obj_set_width(ui.list, lv_pct(100)); lv_obj_set_flex_grow(ui.list, 1);
    lv_obj_set_style_pad_all(ui.list, 2, 0);
    lv_obj_set_style_pad_row(ui.list, 4, 0);
    lv_obj_set_style_border_width(ui.list, 0, 0);
    lv_obj_set_style_bg_opa(ui.list, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(ui.list, LV_SCROLLBAR_MODE_AUTO);
    ui.list_version = UINT32_MAX;
    ui.timer = lv_timer_create(refresh_cb, 33, NULL);
    music_snapshot_t snapshot;
    music_service_snapshot(&snapshot);
    if (snapshot.count == 0) (void)music_service_command(MUSIC_SCAN, 0);
    refresh_cb(ui.timer);
}
