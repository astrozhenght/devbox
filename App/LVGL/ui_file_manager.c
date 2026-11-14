#include "ui_file_manager.h"
#include "file_task.h"
#include "ui_fonts.h"
#include "ui_images.h"

#include <stdio.h>
#include <string.h>

#define FILE_MANAGER_MEDIA_RETRY_MS 2000U
#define FILE_MANAGER_ICON_SIZE      32U
#define FILE_MANAGER_ICON_LEFT      12
#define FILE_MANAGER_TEXT_INSET     10
#define FILE_MANAGER_COLOR_BG       0xF5F7FA
#define FILE_MANAGER_COLOR_TEXT     0x243247
#define FILE_MANAGER_COLOR_MUTED    0x69788B
#define FILE_MANAGER_COLOR_BORDER   0xE4EAF1
#define FILE_MANAGER_COLOR_SELECTED 0xE6F0FF

static const lv_coord_t file_columns[] = {LV_GRID_FR(1), 220, 190, LV_GRID_TEMPLATE_LAST};
static const lv_coord_t file_row[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

/* 文件管理页面上下文，由UI任务维护，集中保存控件、当前页数据和异步请求状态。 */
typedef struct
{
    lv_obj_t *root;         /* 页面根容器，NULL表示页面尚未创建或已关闭。 */
    lv_obj_t *title_label;  /* 左上角粉红色App名称标签。 */
    lv_obj_t *path_label;   /* 当前目录路径标签。 */
    lv_obj_t *status_label; /* 操作进度、错误及统计提示标签。 */
    lv_obj_t *page_label;   /* 页码标签，显示时将内部从0开始的页码加1。 */
    lv_obj_t *list;         /* 可滚动的文件列表。 */
    lv_obj_t *placeholder;  /* 空目录、无卡和错误提示容器。 */
    lv_obj_t *hint_label;   /* 列表中央提示文本。 */
    lv_obj_t *up;           /* 返回上一级目录的按钮。 */
    lv_obj_t *refresh;      /* 刷新当前目录的按钮。 */
    lv_obj_t *previous;     /* 上一页按钮。 */
    lv_obj_t *next;         /* 下一页按钮。 */
    lv_obj_t *remove;       /* 删除选中项的按钮。 */
    lv_obj_t *choose;       /* 选择模式下确认选中文件的按钮。 */
    lv_obj_t *close;        /* 关闭文件管理并返回主页的按钮。 */
    lv_obj_t *rows[FILE_SERVICE_PAGE_SIZE];    /* 当前页各条目的可点击行，未使用的行隐藏。 */
    lv_obj_t *icons[FILE_SERVICE_PAGE_SIZE];   /* 各行的文件夹或普通文件图片。 */
    lv_obj_t *names[FILE_SERVICE_PAGE_SIZE];   /* 各行的文件或目录名称标签。 */
    lv_obj_t *dates[FILE_SERVICE_PAGE_SIZE];   /* 各行的创建时间标签，无有效时间时显示破折号。 */
    lv_obj_t *details[FILE_SERVICE_PAGE_SIZE]; /* 各行的文件大小或目录直属数量、统计状态标签。 */
    lv_obj_t *dialog;       /* 删除确认框，无确认框时为NULL。 */
    lv_timer_t *timer;      /* 50 ms周期处理结果队列和介质重试，页面关闭时删除。 */
    file_service_entry_t entries[FILE_SERVICE_PAGE_SIZE]; /* 当前页条目元数据，与行数组下标对应。 */
    char path[FILE_SERVICE_PATH_CAP];        /* 当前浏览或请求的目录完整路径，以空字符结束。 */
    char delete_path[FILE_SERVICE_PATH_CAP]; /* 待确认或已提交删除的目标完整路径，以空字符结束。 */
    char extension[16];     /* 选择模式允许的扩展名，以|分隔；空字符串表示不过滤。 */
    ui_file_picker_callback_t picker_callback;
    void *picker_user;
    uint32_t request_id;    /* 当前请求编号，用于过滤旧请求的结果。 */
    uint32_t page;          /* 当前页码，从0开始。 */
    uint32_t count;         /* 当前页已接收的条目数。 */
    uint32_t files;         /* 当前目录的可见文件总数，不是仅当前页的数量。 */
    uint32_t directories;   /* 当前目录的可见子目录总数，不是仅当前页的数量。 */
    uint32_t media_retry_tick; /* 最近安排或尝试介质重试时的LVGL毫秒节拍，用于计算间隔。 */
    int32_t selected;       /* 选中条目在当前页的下标，-1表示未选中。 */
    bool busy;              /* 已提交读取或删除，正在等待流程结束。 */
    bool deleting;          /* 当前等待的是删除操作。 */
    bool has_next;          /* PAGE事件报告后面还有可见条目。 */
    bool suppress_click;    /* 长按后忽略下一次点击。 */
    bool retry_media;       /* 介质未就绪后安排自动重试读取；不用于自动重试删除。 */
    bool picker_mode;       /* true表示只选择文件，不允许删除。 */
} ui_file_manager_context_t;

static ui_file_manager_context_t manager __attribute__((section(".axisram")));
static file_service_event_t message __attribute__((section(".axisram")));

/* 重复控件共用样式，避免20行各自分配相同属性；页面关闭后保留供下次复用。 */
static lv_style_t button_style;
static lv_style_t button_pressed_style;
static lv_style_t button_disabled_style;
static lv_style_t row_style;
static lv_style_t row_pressed_style;
static lv_style_t row_checked_style;
static lv_style_t row_checked_pressed_style;
static lv_style_t cell_styles[3];
static bool styles_ready;

static void request_page(const char *path, uint32_t page, bool background);
static void button_event(lv_event_t *event);

static bool entry_selectable(int32_t index)
{
    const char *name;
    size_t name_length;
    const char *extension;

    if ((index < 0) || ((uint32_t)index >= manager.count) ||
        manager.entries[index].is_directory) return false;
    if (manager.extension[0] == '\0') return true;
    name = manager.entries[index].name;
    name_length = strlen(name);
    extension = manager.extension;
    while (*extension != '\0')
    {
        const char *separator = strchr(extension, '|');
        size_t extension_length = (separator != NULL) ?
                                  (size_t)(separator - extension) : strlen(extension);
        if (name_length >= extension_length)
        {
            size_t offset = name_length - extension_length;
            size_t i;
            for (i = 0U; i < extension_length; i++)
            {
                char left = name[offset + i];
                char right = extension[i];
                if ((left >= 'A') && (left <= 'Z')) left = (char)(left - 'A' + 'a');
                if ((right >= 'A') && (right <= 'Z')) right = (char)(right - 'A' + 'a');
                if (left != right) break;
            }
            if (i == extension_length) return true;
        }
        if (separator == NULL) break;
        extension = separator + 1U;
    }
    return false;
}

static void init_styles(void)
{
    if (styles_ready) return;
    lv_style_init(&button_style);
    lv_style_set_pad_all(&button_style, 3);
    lv_style_set_radius(&button_style, 6);
    lv_style_set_border_width(&button_style, 1);
    lv_style_set_border_color(&button_style, lv_color_hex(FILE_MANAGER_COLOR_BORDER));
    lv_style_set_bg_opa(&button_style, LV_OPA_COVER);
    lv_style_set_bg_color(&button_style, lv_color_white());
    lv_style_set_text_color(&button_style, lv_color_hex(FILE_MANAGER_COLOR_TEXT));
    lv_style_set_text_font(&button_style, &ui_font_chinese_24_4);
    lv_style_init(&button_pressed_style);
    lv_style_set_bg_color(&button_pressed_style, lv_color_hex(0xEAF0F7));
    lv_style_init(&button_disabled_style);
    lv_style_set_bg_color(&button_disabled_style, lv_color_hex(0xEDF1F5));
    lv_style_set_text_color(&button_disabled_style, lv_color_hex(0xA5AFBC));

    lv_style_init(&row_style);
    lv_style_set_width(&row_style, lv_pct(100));
    lv_style_set_height(&row_style, 45);
    lv_style_set_pad_all(&row_style, 0);
    lv_style_set_border_width(&row_style, 1);
    lv_style_set_border_side(&row_style, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_color(&row_style, lv_color_hex(FILE_MANAGER_COLOR_BORDER));
    lv_style_set_bg_opa(&row_style, LV_OPA_COVER);
    lv_style_set_bg_color(&row_style, lv_color_white());
    lv_style_set_text_color(&row_style, lv_color_hex(FILE_MANAGER_COLOR_TEXT));
    lv_style_set_layout(&row_style, LV_LAYOUT_GRID);
    lv_style_set_grid_column_dsc_array(&row_style, file_columns);
    lv_style_set_grid_row_dsc_array(&row_style, file_row);
    lv_style_init(&row_pressed_style);
    lv_style_set_bg_color(&row_pressed_style, lv_color_hex(0xF0F4FA));
    lv_style_init(&row_checked_style);
    lv_style_set_bg_color(&row_checked_style, lv_color_hex(FILE_MANAGER_COLOR_SELECTED));
    lv_style_init(&row_checked_pressed_style);
    lv_style_set_bg_color(&row_checked_pressed_style, lv_color_hex(0xD7E8FA));

    for (uint32_t i = 0; i < 3U; i++)
    {
        lv_style_init(&cell_styles[i]);
        lv_style_set_grid_cell_column_pos(&cell_styles[i], i);
        lv_style_set_grid_cell_column_span(&cell_styles[i], 1);
        lv_style_set_grid_cell_row_pos(&cell_styles[i], 0);
        lv_style_set_grid_cell_row_span(&cell_styles[i], 1);
        lv_style_set_grid_cell_x_align(&cell_styles[i], LV_GRID_ALIGN_STRETCH);
        lv_style_set_grid_cell_y_align(&cell_styles[i], LV_GRID_ALIGN_CENTER);
        lv_style_set_pad_left(&cell_styles[i], i == 0U ? 56 : 8);
        lv_style_set_pad_right(&cell_styles[i], 12);
        lv_style_set_text_align(&cell_styles[i], i == 2U ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT);
        if (i != 0U) lv_style_set_text_color(&cell_styles[i], lv_color_hex(FILE_MANAGER_COLOR_MUTED));
    }
    styles_ready = true;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_coord_t width)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_add_style(button, &button_style, 0);
    lv_obj_add_style(button, &button_pressed_style, LV_STATE_PRESSED);
    lv_obj_add_style(button, &button_disabled_style, LV_STATE_DISABLED);
    lv_obj_set_size(button, width, 38);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, button_event, LV_EVENT_CLICKED, NULL);
    return button;
}

/* 提示容器与列表共用内容区，仅显示空目录、无卡及错误提示。 */
static void show_placeholder(const char *text)
{
    if (text == NULL)
    {
        lv_obj_add_flag(manager.placeholder, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(manager.hint_label, text);
    lv_obj_clear_flag(manager.placeholder, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(manager.placeholder);
}

static void set_status_style(bool error)
{
    lv_obj_set_style_text_font(manager.status_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(manager.status_label,
        lv_color_hex(error ? 0xD94757 : FILE_MANAGER_COLOR_MUTED), 0);
    lv_label_set_long_mode(manager.status_label, LV_LABEL_LONG_DOT);
}

static void set_disabled(lv_obj_t *object, bool disabled)
{
    if (disabled) lv_obj_add_state(object, LV_STATE_DISABLED);
    else lv_obj_clear_state(object, LV_STATE_DISABLED);
}

static void update_controls(void)
{
    set_disabled(manager.up, manager.deleting || strcmp(manager.path, FILE_SERVICE_ROOT) == 0);
    set_disabled(manager.refresh, manager.deleting);
    set_disabled(manager.previous, manager.deleting || manager.page == 0U);
    set_disabled(manager.next, manager.busy || !manager.has_next);
    if (manager.remove != NULL)
        set_disabled(manager.remove, manager.busy || manager.selected < 0);
    if (manager.choose != NULL)
        set_disabled(manager.choose, manager.busy || !entry_selectable(manager.selected));
    set_disabled(manager.close, manager.deleting);
}

static void show_selected(int32_t index)
{
    if (manager.selected >= 0)
        lv_obj_clear_state(manager.rows[manager.selected], LV_STATE_CHECKED);
    manager.selected = index;
    if (index >= 0) lv_obj_add_state(manager.rows[index], LV_STATE_CHECKED);
    update_controls();
}

static void root_deleted(lv_event_t *event)
{
    (void)event;
    file_task_cancel(manager.request_id);
    if (manager.timer != NULL) lv_timer_del(manager.timer);
    if (manager.dialog != NULL) lv_msgbox_close(manager.dialog);
    memset(&manager, 0, sizeof(manager));
}

static void request_page(const char *path, uint32_t page, bool background)
{
    uint32_t id;
    if (!file_task_request_list(path, page, &id))
    {
        set_status_style(true);
        lv_label_set_text(manager.status_label, "请求队列忙，请稍后刷新");
        return;
    }
    bool path_changed = strcmp(manager.path, path) != 0;
    lv_obj_scroll_to_y(manager.list, 0, LV_ANIM_OFF);
    memmove(manager.path, path, strlen(path) + 1U);
    manager.request_id = id;
    manager.retry_media = false;
    manager.page = page;
    manager.count = manager.files = manager.directories = 0U;
    manager.selected = -1;
    manager.busy = true;
    manager.has_next = false;
    for (uint32_t i = 0; i < FILE_SERVICE_PAGE_SIZE; i++)
    {
        lv_obj_add_flag(manager.rows[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(manager.rows[i], LV_STATE_CHECKED);
    }
    lv_label_set_text(manager.path_label, manager.path);
    /* 后台重试保留无卡提示；主动读取或切换目录时清除旧提示，不显示加载反馈。 */
    if (!background || path_changed)
    {
        set_status_style(false);
        lv_label_set_text(manager.status_label, "");
        show_placeholder(NULL);
    }
    lv_label_set_text_fmt(manager.page_label, "第%lu页", (unsigned long)(page + 1U));
    update_controls();
}

static void delete_confirmed(lv_event_t *event)
{
    (void)event;
    uint16_t button = lv_msgbox_get_active_btn(manager.dialog);
    if (button == LV_BTNMATRIX_BTN_NONE) return;
    lv_obj_t *dialog = manager.dialog;
    manager.dialog = NULL;
    lv_msgbox_close(dialog);
    if (button != 0U) return;
    uint32_t id;
    if (!file_task_request_delete(manager.delete_path, &id))
    {
        set_status_style(true);
        lv_label_set_text(manager.status_label, "删除请求未提交，请重试");
        return;
    }
    manager.request_id = id;
    manager.busy = manager.deleting = true;
    set_status_style(true);
    lv_label_set_text(manager.status_label, "正在删除,请勿拔卡...");
    update_controls();
}

/* 消息框使用按钮矩阵，仅为第一个“删除”按钮绘制红底白字。 */
static void delete_dialog_button_draw(lv_event_t *event)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(event);
    if (dsc->class_p != &lv_btnmatrix_class ||
        dsc->type != LV_BTNMATRIX_DRAW_PART_BTN || dsc->id != 0U) return;
    lv_obj_t *buttons = lv_event_get_current_target(event);
    bool pressed = lv_obj_has_state(buttons, LV_STATE_PRESSED) &&
                   lv_btnmatrix_get_selected_btn(buttons) == 0U;
    dsc->rect_dsc->bg_color = pressed ? lv_palette_darken(LV_PALETTE_RED, 2) :
                                      lv_palette_main(LV_PALETTE_RED);
    dsc->rect_dsc->bg_opa = LV_OPA_COVER;
    dsc->rect_dsc->border_color = dsc->rect_dsc->bg_color;
    dsc->label_dsc->color = lv_color_white();
}

static void confirm_delete(void)
{
    if (manager.busy || manager.selected < 0 || manager.dialog != NULL) return;
    const file_service_entry_t *entry = &manager.entries[manager.selected];
    file_service_status_t status = file_service_join_path(manager.path, entry->name,
                                            manager.delete_path, sizeof(manager.delete_path));
    if (status != FILE_SERVICE_OK)
    {
        set_status_style(true);
        lv_label_set_text(manager.status_label, file_service_status_text(status));
        return;
    }
    static const char *buttons[] = {"删除", "取消", ""};
    /* LVGL传入空正文时不会创建text对象，后续不能设置其尺寸或内容。 */
    manager.dialog = lv_msgbox_create(NULL, "确认删除", "此操作无法撤销。", buttons, false);
    lv_obj_set_width(manager.dialog, 640);
    lv_obj_set_style_bg_color(manager.dialog, lv_color_white(), 0);
    lv_obj_set_style_radius(manager.dialog, 10, 0);
    lv_obj_set_style_text_color(manager.dialog, lv_color_hex(FILE_MANAGER_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(manager.dialog, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(lv_msgbox_get_title(manager.dialog), lv_color_hex(0xD94757), 0);
    lv_obj_set_width(lv_msgbox_get_text(manager.dialog), 590);
    lv_label_set_text_fmt(lv_msgbox_get_text(manager.dialog), "%s\n\n%s\n\n此操作无法撤销。",
        entry->is_directory ? "将删除该文件夹及内部全部内容。" : "将删除此文件。", manager.delete_path);
    /* 按钮在字体行高之外上下各留3像素，矩阵外侧上下各留4像素。 */
    lv_obj_t *dialog_buttons = lv_msgbox_get_btns(manager.dialog);
    lv_obj_set_height(dialog_buttons, lv_font_get_line_height(&ui_font_chinese_24_4) + 14);
    lv_obj_set_style_pad_ver(dialog_buttons, 4, 0);
    lv_obj_set_style_text_font(dialog_buttons, &ui_font_chinese_24_4, LV_PART_ITEMS);
    lv_obj_add_event_cb(dialog_buttons, delete_dialog_button_draw, LV_EVENT_DRAW_PART_BEGIN, NULL);
    lv_obj_add_event_cb(manager.dialog, delete_confirmed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(manager.dialog);
}

static void row_event(lv_event_t *event)
{
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    if (manager.deleting || index >= manager.count || manager.dialog != NULL) return;
    if (lv_event_get_code(event) == LV_EVENT_LONG_PRESSED)
    {
        show_selected((int32_t)index);
        manager.suppress_click = true;
        if (!manager.picker_mode) confirm_delete();
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (manager.suppress_click) { manager.suppress_click = false; return; }
    show_selected((int32_t)index);
    if (manager.entries[index].is_directory)
    {
        char path[FILE_SERVICE_PATH_CAP];
        file_service_status_t status = file_service_join_path(manager.path,
                                    manager.entries[index].name, path, sizeof(path));
        if (status == FILE_SERVICE_OK) request_page(path, 0U, false);
        else lv_label_set_text(manager.status_label, file_service_status_text(status));
    }
    else
    {
        set_status_style(false);
        lv_obj_set_style_text_font(manager.status_label, &ui_font_chinese_24_4, 0);
        lv_label_set_long_mode(manager.status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text_fmt(manager.status_label, "%s  %lu B%s", manager.entries[index].name,
            (unsigned long)manager.entries[index].size_bytes,
            manager.entries[index].read_only ? "  只读" : "");
    }
}

static void button_event(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_current_target(event);
    if (manager.deleting) return;
    if (target == manager.close) lv_obj_del(manager.root);
    else if (target == manager.refresh) request_page(manager.path, manager.page, false);
    else if (target == manager.previous && manager.page > 0U) request_page(manager.path, manager.page - 1U, false);
    else if (target == manager.next && manager.has_next) request_page(manager.path, manager.page + 1U, false);
    else if (target == manager.remove) confirm_delete();
    else if ((target == manager.choose) && entry_selectable(manager.selected))
    {
        char selected_path[FILE_SERVICE_PATH_CAP];
        if (file_service_join_path(manager.path, manager.entries[manager.selected].name,
                                   selected_path, sizeof(selected_path)) == FILE_SERVICE_OK)
        {
            ui_file_picker_callback_t callback = manager.picker_callback;
            void *user = manager.picker_user;
            if (callback != NULL) callback(selected_path, user);
            lv_obj_del(manager.root);
        }
    }
    else if (target == manager.up)
    {
        char path[FILE_SERVICE_PATH_CAP];
        strcpy(path, manager.path);
        if (file_service_parent_path(path)) request_page(path, 0U, true);
    }
}

static void show_entry(void)
{
    uint32_t i = message.index;
    if (i >= FILE_SERVICE_PAGE_SIZE) return;
    manager.entries[i] = message.entry;
    if (manager.count <= i) manager.count = i + 1U;
    show_placeholder(NULL);
    const lv_img_dsc_t *icon = message.entry.is_directory ? &ui_img_icon_folder48 : &ui_img_icon_file32;
    lv_img_set_src(manager.icons[i], icon);
    /* 将图片等比例缩放到32像素基准，保留REAL模式计算出的实际边界。 */
    lv_img_set_zoom(manager.icons[i], (FILE_MANAGER_ICON_SIZE * 256U + icon->header.w / 2U) / icon->header.w);
    /* 缩放后的边界含抗锯齿余量，按实际宽度居中到统一的32像素图标栏。 */
    lv_obj_set_style_translate_x(manager.icons[i], FILE_MANAGER_ICON_LEFT +
        ((lv_coord_t)FILE_MANAGER_ICON_SIZE - lv_obj_get_self_width(manager.icons[i])) / 2, 0);
    lv_label_set_text(manager.names[i], message.entry.name);
    const file_service_time_t *time = &message.entry.created;
    if (time->valid)
        lv_label_set_text_fmt(manager.dates[i], "%04u-%02u-%02u %02u:%02u",
            time->year, time->month, time->day, time->hour, time->minute);
    else lv_label_set_text(manager.dates[i], "—");
    if (message.entry.is_directory) lv_label_set_text(manager.details[i], "统计中...");
    else if (message.entry.size_bytes >= 1024U * 1024U)
        lv_label_set_text_fmt(manager.details[i], "%lu.%lu MiB",
            (unsigned long)(message.entry.size_bytes / (1024U * 1024U)),
            (unsigned long)((message.entry.size_bytes % (1024U * 1024U)) / 104858U));
    else if (message.entry.size_bytes >= 1024U)
        lv_label_set_text_fmt(manager.details[i], "%lu.%lu KiB",
            (unsigned long)(message.entry.size_bytes / 1024U),
            (unsigned long)((message.entry.size_bytes % 1024U) * 10U / 1024U));
    else lv_label_set_text_fmt(manager.details[i], "%lu B", (unsigned long)message.entry.size_bytes);
    lv_obj_clear_flag(manager.rows[i], LV_OBJ_FLAG_HIDDEN);
}

static void poll_results(lv_timer_t *timer)
{
    (void)timer;
    /* 限制单次消费量，让触摸和显示任务有机会继续执行。 */
    for (unsigned i = 0; i < 4U && file_task_receive(&message); i++)
    {
        if (message.request_id != manager.request_id) continue;
        switch (message.kind)
        {
            case FILE_SERVICE_EVENT_ENTRY: show_entry(); break;
            case FILE_SERVICE_EVENT_PAGE:
                manager.files = message.files;
                manager.directories = message.directories;
                manager.has_next = message.has_next;
                lv_label_set_text_fmt(manager.status_label, "%lu个文件夹 / %lu个文件,统计中...",
                    (unsigned long)manager.directories, (unsigned long)manager.files);
                break;
            case FILE_SERVICE_EVENT_COUNT:
                if (message.index < manager.count)
                {
                    if (message.status == FILE_SERVICE_OK)
                        lv_label_set_text_fmt(manager.details[message.index], "%lu文件 / %lu目录",
                            (unsigned long)message.files, (unsigned long)message.directories);
                    else lv_label_set_text(manager.details[message.index], "统计失败");
                }
                break;
            case FILE_SERVICE_EVENT_DELETE_PROGRESS:
                lv_label_set_text_fmt(manager.status_label, "已删除%lu项,请勿拔卡...", (unsigned long)message.deleted);
                break;
            case FILE_SERVICE_EVENT_DONE:
            {
                bool was_deleting = manager.deleting;
                manager.busy = manager.deleting = false;
                set_status_style(message.status != FILE_SERVICE_OK);
                show_placeholder(NULL);
                if (message.status != FILE_SERVICE_OK)
                {
                    LV_LOG_WARN("File request %lu: status=%d FR=%ld deleted=%lu path=%s",
                        (unsigned long)message.request_id, (int)message.status, (long)message.native_error,
                        (unsigned long)message.deleted, message.path);
                    lv_label_set_text(manager.status_label, file_service_status_text(message.status));
                    /* 部分删除后列表已过期，必须刷新后才能再次操作。 */
                    if (was_deleting)
                    {
                        lv_label_set_text_fmt(manager.status_label, "删除失败,已删除%lu项,请刷新",
                            (unsigned long)message.deleted);
                        show_selected(-1);
                    }
                    else if (message.status == FILE_SERVICE_NOT_READY)
                    {
                        manager.retry_media = true;
                        manager.media_retry_tick = lv_tick_get();
                        lv_label_set_text(manager.status_label, "插卡后将自动重试");
                        if (manager.count == 0U)
                            show_placeholder("SD卡未就绪\n插卡后将自动重试");
                    }
                    else if (manager.count == 0U)
                        show_placeholder(file_service_status_text(message.status));
                }
                else if (was_deleting)
                {
                    request_page(manager.path, manager.page, false);
                }
                else if (manager.count == 0U && manager.page > 0U)
                {
                    request_page(manager.path, manager.page - 1U, false);
                }
                else
                {
                    lv_label_set_text_fmt(manager.status_label, "%lu个文件夹 / %lu个文件",
                        (unsigned long)manager.directories, (unsigned long)manager.files);
                    if (manager.count == 0U) show_placeholder("此文件夹为空");
                }
                update_controls();
                break;
            }
            default: break;
        }
    }
    /* 只重试读取。实际挂载仍由文件任务执行，禁止在UI中等待SDMMC。 */
    if (manager.retry_media && !manager.busy && manager.dialog == NULL &&
        lv_tick_elaps(manager.media_retry_tick) >= FILE_MANAGER_MEDIA_RETRY_MS)
    {
        manager.media_retry_tick = lv_tick_get();
        request_page(manager.path, manager.page, true);
    }
}

/* 创建轻量布局容器，显式去掉主题默认边框、阴影及内边距。 */
static lv_obj_t *make_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

/* 表头与文件行共用列宽；名称留出图标位置，数量和大小靠右对齐。 */
static lv_obj_t *make_cell(lv_obj_t *parent, uint32_t column, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_add_style(label, &cell_styles[column], 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    return label;
}

static void ui_file_manager_create_internal(lv_obj_t *parent, const char *start_path,
                                            const char *extension,
                                            ui_file_picker_callback_t callback,
                                            void *user)
{
    if (manager.root != NULL)
    {
        lv_obj_move_foreground(manager.root);
        return;
    }
    memset(&manager, 0, sizeof(manager));
    manager.selected = -1;
    manager.picker_mode = callback != NULL;
    manager.picker_callback = callback;
    manager.picker_user = user;
    if (extension != NULL)
    {
        strncpy(manager.extension, extension, sizeof(manager.extension) - 1U);
        manager.extension[sizeof(manager.extension) - 1U] = '\0';
    }
    init_styles();
    manager.root = lv_obj_create(parent);
    lv_obj_set_size(manager.root, lv_pct(100), lv_pct(92));
    lv_obj_set_y(manager.root, lv_pct(8));
    lv_obj_set_style_pad_all(manager.root, 8, 0);
    lv_obj_set_style_pad_row(manager.root, 4, 0);
    lv_obj_set_style_border_width(manager.root, 0, 0);
    lv_obj_set_style_radius(manager.root, 0, 0);
    lv_obj_set_style_bg_color(manager.root, lv_color_hex(FILE_MANAGER_COLOR_BG), 0);
    lv_obj_set_style_text_font(manager.root, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(manager.root, lv_color_hex(FILE_MANAGER_COLOR_TEXT), 0);
    lv_obj_clear_flag(manager.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(manager.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(manager.root, root_deleted, LV_EVENT_DELETE, NULL);

    lv_obj_t *toolbar = make_panel(manager.root);
    lv_obj_set_size(toolbar, lv_pct(100), 42);
    lv_obj_set_style_pad_left(toolbar, FILE_MANAGER_TEXT_INSET, 0);
    lv_obj_set_style_pad_column(toolbar, 8, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    manager.title_label = lv_label_create(toolbar);
    lv_label_set_text(manager.title_label, manager.picker_mode ? "选择文件" : "文件管理器");
    lv_obj_set_style_text_color(manager.title_label, lv_palette_main(LV_PALETTE_PINK), 0);
    lv_obj_t *spacer = make_panel(toolbar);
    lv_obj_set_size(spacer, 0, 1);
    lv_obj_set_flex_grow(spacer, 1);

    /* 从左到右为上一级、刷新、返回主页；保留绿色和红色的按下反馈。 */
    manager.up = make_button(toolbar, "上一级", 96);
    manager.refresh = make_button(toolbar, "刷新", 80);
    lv_obj_set_style_bg_color(manager.refresh, lv_palette_main(LV_PALETTE_GREEN), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(manager.refresh, lv_color_white(), LV_STATE_PRESSED);
    manager.close = make_button(toolbar, "返回主页", 112);
    lv_obj_set_style_bg_color(manager.close, lv_palette_main(LV_PALETTE_RED), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(manager.close, lv_color_white(), LV_STATE_PRESSED);

    lv_obj_t *path_bar = make_panel(manager.root);
    lv_obj_set_size(path_bar, lv_pct(100), 34);
    lv_obj_set_style_bg_opa(path_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(path_bar, lv_color_hex(0xEAF0F6), 0);
    lv_obj_set_style_radius(path_bar, 6, 0);
    lv_obj_set_style_pad_hor(path_bar, FILE_MANAGER_TEXT_INSET, 0);
    lv_obj_set_style_pad_column(path_bar, 12, 0);
    lv_obj_set_flex_flow(path_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(path_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *path_caption = lv_label_create(path_bar);
    lv_label_set_text(path_caption, "当前位置");
    lv_obj_set_style_text_color(path_caption, lv_color_hex(FILE_MANAGER_COLOR_MUTED), 0);
    manager.path_label = lv_label_create(path_bar);
    lv_obj_set_width(manager.path_label, 0);
    lv_obj_set_flex_grow(manager.path_label, 1);
    lv_label_set_long_mode(manager.path_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t *browser = make_panel(manager.root);
    lv_obj_set_size(browser, lv_pct(100), 0);
    lv_obj_set_flex_grow(browser, 1);
    lv_obj_set_flex_flow(browser, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(browser, 0, 0);
    lv_obj_set_style_bg_opa(browser, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(browser, lv_color_white(), 0);
    lv_obj_set_style_border_width(browser, 1, 0);
    lv_obj_set_style_border_color(browser, lv_color_hex(FILE_MANAGER_COLOR_BORDER), 0);
    lv_obj_set_style_radius(browser, 8, 0);

    lv_obj_t *head = make_panel(browser);
    lv_obj_set_size(head, lv_pct(100), 32);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(head, lv_color_hex(0xF0F4F8), 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(head, lv_color_hex(FILE_MANAGER_COLOR_BORDER), 0);
    lv_obj_set_style_text_color(head, lv_color_hex(FILE_MANAGER_COLOR_MUTED), 0);
    lv_obj_set_grid_dsc_array(head, file_columns, file_row);
    make_cell(head, 0, "名称");
    /* 表头“创建时间”向右收进，与下方时间文本左边缘对齐。 */
    lv_obj_t *date_header = make_cell(head, 1, "创建时间");
    lv_obj_set_style_pad_left(date_header, 18, 0);
    make_cell(head, 2, "数量 / 大小");

    lv_obj_t *body = make_panel(browser);
    lv_obj_set_size(body, lv_pct(100), 0);
    lv_obj_set_flex_grow(body, 1);
    manager.list = make_panel(body);
    lv_obj_set_size(manager.list, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(manager.list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(manager.list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(manager.list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(manager.list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(manager.list, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(manager.list, lv_color_hex(0xB8C5D4), LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(manager.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(manager.list, 0, 0);
    for (uint32_t i = 0; i < FILE_SERVICE_PAGE_SIZE; i++)
    {
        lv_obj_t *row = lv_btn_create(manager.list);
        manager.rows[i] = row;
        lv_obj_remove_style_all(row);
        lv_obj_add_style(row, &row_style, 0);
        lv_obj_add_style(row, &row_pressed_style, LV_STATE_PRESSED);
        lv_obj_add_style(row, &row_checked_style, LV_STATE_CHECKED);
        lv_obj_add_style(row, &row_checked_pressed_style, LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, row_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, row_event, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)i);
        manager.icons[i] = lv_img_create(row);
        /* 按缩放后的实际边界布局，保证两种图标的中心及垂直位置一致。 */
        lv_img_set_size_mode(manager.icons[i], LV_IMG_SIZE_MODE_REAL);
        lv_obj_set_size(manager.icons[i], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_clear_flag(manager.icons[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_grid_cell(manager.icons[i], LV_GRID_ALIGN_START, 0, 1,
                            LV_GRID_ALIGN_CENTER, 0, 1);
        manager.names[i] = make_cell(row, 0, "");
        manager.dates[i] = make_cell(row, 1, "");
        manager.details[i] = make_cell(row, 2, "");
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    manager.placeholder = make_panel(body);
    lv_obj_set_size(manager.placeholder, lv_pct(90), LV_SIZE_CONTENT);
    lv_obj_clear_flag(manager.placeholder, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(manager.placeholder, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(manager.placeholder, 12, 0);
    lv_obj_set_flex_align(manager.placeholder, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    manager.hint_label = lv_label_create(manager.placeholder);
    lv_obj_set_width(manager.hint_label, lv_pct(100));
    lv_obj_set_style_text_align(manager.hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(manager.hint_label, lv_color_hex(FILE_MANAGER_COLOR_MUTED), 0);
    lv_obj_center(manager.placeholder);

    lv_obj_t *footer = make_panel(manager.root);
    lv_obj_set_size(footer, lv_pct(100), 40);
    lv_obj_set_style_pad_column(footer, 8, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    manager.status_label = lv_label_create(footer);
    lv_obj_set_width(manager.status_label, 0);
    lv_obj_set_flex_grow(manager.status_label, 1);
    set_status_style(false);
    manager.previous = make_button(footer, "上一页", 94);
    manager.page_label = lv_label_create(footer);
    lv_obj_set_width(manager.page_label, 92);
    lv_obj_set_style_text_align(manager.page_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(manager.page_label, LV_LABEL_LONG_DOT);
    manager.next = make_button(footer, "下一页", 94);
    if (manager.picker_mode)
    {
        manager.choose = make_button(footer, "选择", 78);
        lv_obj_set_style_bg_color(manager.choose, lv_color_hex(0xEAF5FF), 0);
        lv_obj_set_style_border_color(manager.choose, lv_color_hex(0x90CAF9), 0);
        lv_obj_set_style_text_color(manager.choose, lv_color_hex(0x1565C0), 0);
    }
    else
    {
        manager.remove = make_button(footer, "删除", 78);
        lv_obj_set_style_bg_color(manager.remove, lv_color_hex(0xFFF4F5), 0);
        lv_obj_set_style_border_color(manager.remove, lv_color_hex(0xF1C8CE), 0);
        lv_obj_set_style_text_color(manager.remove, lv_color_hex(0xD94757), 0);
        lv_obj_set_style_bg_color(manager.remove, lv_palette_main(LV_PALETTE_RED), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(manager.remove, lv_color_white(), LV_STATE_PRESSED);
    }
    manager.timer = lv_timer_create(poll_results, 50U, NULL);
    request_page((start_path != NULL) ? start_path : FILE_SERVICE_ROOT, 0U, false);
}

void ui_file_manager_create(lv_obj_t *parent)
{
    ui_file_manager_create_internal(parent, FILE_SERVICE_ROOT, NULL, NULL, NULL);
}

void ui_file_manager_create_picker(lv_obj_t *parent, const char *start_path,
                                   const char *extension,
                                   ui_file_picker_callback_t callback,
                                   void *user)
{
    ui_file_manager_create_internal(parent, start_path, extension, callback, user);
}
