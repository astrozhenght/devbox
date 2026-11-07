#include "ui_file_manager.h"
#include "file_task.h"
#include "ui_fonts.h"
#include "ui_images.h"

#include <stdio.h>
#include <string.h>

#define FILE_MANAGER_MEDIA_RETRY_MS 2000U
#define FILE_MANAGER_ICON_SIZE      32U

/* 文件管理页面上下文，由UI任务维护，集中保存控件、当前页数据和异步请求状态。 */
typedef struct
{
    lv_obj_t *root;         /* 页面根容器，NULL表示页面尚未创建或已关闭。 */
    lv_obj_t *title_label;  /* 左上角粉红色App名称标签。 */
    lv_obj_t *path_label;   /* 当前目录路径标签。 */
    lv_obj_t *status_label; /* 操作进度、错误及统计提示标签。 */
    lv_obj_t *page_label;   /* 页码标签，显示时将内部从0开始的页码加1。 */
    lv_obj_t *up;           /* 返回上一级目录的按钮。 */
    lv_obj_t *refresh;      /* 刷新当前目录的按钮。 */
    lv_obj_t *previous;     /* 上一页按钮。 */
    lv_obj_t *next;         /* 下一页按钮。 */
    lv_obj_t *remove;       /* 删除选中项的按钮。 */
    lv_obj_t *close;        /* 关闭文件管理并返回主页的按钮。 */
    lv_obj_t *rows[FILE_SERVICE_PAGE_SIZE];    /* 当前页各条目的可点击行，未使用的行隐藏。 */
    lv_obj_t *icons[FILE_SERVICE_PAGE_SIZE];   /* 各行的文件夹或普通文件图片。 */
    lv_obj_t *names[FILE_SERVICE_PAGE_SIZE];   /* 各行的文件或目录名称标签。 */
    lv_obj_t *dates[FILE_SERVICE_PAGE_SIZE];   /* 各行的创建时间标签，无有效时间时显示“未知”。 */
    lv_obj_t *details[FILE_SERVICE_PAGE_SIZE]; /* 各行的文件大小或目录直属数量、统计状态标签。 */
    lv_obj_t *dialog;       /* 删除确认框，无确认框时为NULL。 */
    lv_timer_t *timer;      /* 50 ms周期处理结果队列和介质重试，页面关闭时删除。 */
    file_service_entry_t entries[FILE_SERVICE_PAGE_SIZE]; /* 当前页条目元数据，与行数组下标对应。 */
    char path[FILE_SERVICE_PATH_CAP];        /* 当前浏览或请求的目录完整路径，以空字符结束。 */
    char delete_path[FILE_SERVICE_PATH_CAP]; /* 待确认或已提交删除的目标完整路径，以空字符结束。 */
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
} ui_file_manager_context_t;

static ui_file_manager_context_t manager __attribute__((section(".axisram")));
static file_service_event_t message __attribute__((section(".axisram")));

static void request_page(const char *path, uint32_t page);
static void button_event(lv_event_t *event);

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_coord_t width)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, 38);
    lv_obj_set_style_pad_all(button, 3, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, button_event, LV_EVENT_CLICKED, NULL);
    return button;
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
    set_disabled(manager.remove, manager.busy || manager.selected < 0);
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

static void request_page(const char *path, uint32_t page)
{
    uint32_t id;
    if (!file_task_request_list(path, page, &id))
    {
        lv_label_set_text(manager.status_label, "请求队列忙，请稍后刷新");
        return;
    }
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
    lv_label_set_text(manager.status_label, "正在读取目录...");
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
        lv_label_set_text(manager.status_label, "删除请求未提交，请重试");
        return;
    }
    manager.request_id = id;
    manager.busy = manager.deleting = true;
    lv_label_set_text(manager.status_label, "正在删除，请勿拔卡...");
    update_controls();
}

static void confirm_delete(void)
{
    if (manager.busy || manager.selected < 0 || manager.dialog != NULL) return;
    const file_service_entry_t *entry = &manager.entries[manager.selected];
    file_service_status_t status = file_service_join_path(manager.path, entry->name,
                                            manager.delete_path, sizeof(manager.delete_path));
    if (status != FILE_SERVICE_OK)
    {
        lv_label_set_text(manager.status_label, file_service_status_text(status));
        return;
    }
    static const char *buttons[] = {"删除", "取消", ""};
    /* LVGL传入空正文时不会创建text对象，后续不能设置其尺寸或内容。 */
    manager.dialog = lv_msgbox_create(NULL, "确认删除", "此操作无法撤销。", buttons, false);
    lv_obj_set_width(manager.dialog, 680);
    lv_obj_set_style_text_font(manager.dialog, &ui_font_chinese_24_4, 0);
    lv_obj_set_width(lv_msgbox_get_text(manager.dialog), 630);
    lv_label_set_text_fmt(lv_msgbox_get_text(manager.dialog), "%s\n%s\n此操作无法撤销。",
        manager.delete_path, entry->is_directory ? "将删除该文件夹及内部全部内容。" : "将删除此文件。");
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
        confirm_delete();
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
        if (status == FILE_SERVICE_OK) request_page(path, 0U);
        else lv_label_set_text(manager.status_label, file_service_status_text(status));
    }
    else
        lv_label_set_text_fmt(manager.status_label, "%s  %lu B%s", manager.entries[index].name,
            (unsigned long)manager.entries[index].size_bytes,
            manager.entries[index].read_only ? "  只读" : "");
}

static void button_event(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_current_target(event);
    if (manager.deleting) return;
    if (target == manager.close) lv_obj_del(manager.root);
    else if (target == manager.refresh) request_page(manager.path, manager.page);
    else if (target == manager.previous && manager.page > 0U) request_page(manager.path, manager.page - 1U);
    else if (target == manager.next && manager.has_next) request_page(manager.path, manager.page + 1U);
    else if (target == manager.remove) confirm_delete();
    else if (target == manager.up)
    {
        char path[FILE_SERVICE_PATH_CAP];
        strcpy(path, manager.path);
        if (file_service_parent_path(path)) request_page(path, 0U);
    }
}

static void show_entry(void)
{
    uint32_t i = message.index;
    if (i >= FILE_SERVICE_PAGE_SIZE) return;
    manager.entries[i] = message.entry;
    if (manager.count <= i) manager.count = i + 1U;
    const lv_img_dsc_t *icon = message.entry.is_directory ? &ui_img_icon_folder48 : &ui_img_icon_file32;
    lv_img_set_src(manager.icons[i], icon);
    lv_img_set_zoom(manager.icons[i], (FILE_MANAGER_ICON_SIZE * 256U + icon->header.w / 2U) / icon->header.w);
    lv_obj_align(manager.icons[i], LV_ALIGN_LEFT_MID, 8, 0);
    lv_label_set_text(manager.names[i], message.entry.name);
    const file_service_time_t *time = &message.entry.created;
    if (time->valid)
        lv_label_set_text_fmt(manager.dates[i], "%04u-%02u-%02u %02u:%02u",
            time->year, time->month, time->day, time->hour, time->minute);
    else lv_label_set_text(manager.dates[i], "未知");
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
                lv_label_set_text_fmt(manager.status_label, "%lu个文件夹，%lu个文件；正在统计...",
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
                lv_label_set_text_fmt(manager.status_label, "已删除%lu项，请勿拔卡...", (unsigned long)message.deleted);
                break;
            case FILE_SERVICE_EVENT_DONE:
            {
                bool was_deleting = manager.deleting;
                manager.busy = manager.deleting = false;
                if (message.status != FILE_SERVICE_OK)
                {
                    lv_label_set_text_fmt(manager.status_label, "%s (FR=%ld)，已删除%lu项：%s",
                        file_service_status_text(message.status), (long)message.native_error,
                        (unsigned long)message.deleted, message.path);
                    /* 部分删除后列表已过期，必须刷新后才能再次操作。 */
                    if (was_deleting) show_selected(-1);
                    else if (message.status == FILE_SERVICE_NOT_READY)
                    {
                        manager.retry_media = true;
                        manager.media_retry_tick = lv_tick_get();
                        lv_label_set_text_fmt(manager.status_label,
                            "SD卡未就绪，请插卡；每2秒自动重试 (FR=%ld)",
                            (long)message.native_error);
                    }
                }
                else if (was_deleting)
                {
                    request_page(manager.path, manager.page);
                }
                else if (manager.count == 0U && manager.page > 0U)
                {
                    request_page(manager.path, manager.page - 1U);
                }
                else
                    lv_label_set_text_fmt(manager.status_label, "%lu个文件夹，%lu个文件%s",
                        (unsigned long)manager.directories, (unsigned long)manager.files,
                        manager.count == 0U ? "；此文件夹为空" : "；长按条目可删除");
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
        request_page(manager.path, manager.page);
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t width, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, 7);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    return label;
}

void ui_file_manager_create(lv_obj_t *parent)
{
    if (manager.root != NULL)
    {
        lv_obj_move_foreground(manager.root);
        return;
    }
    memset(&manager, 0, sizeof(manager));
    manager.selected = -1;
    manager.root = lv_obj_create(parent);
    lv_obj_set_size(manager.root, lv_pct(100), lv_pct(92));
    lv_obj_set_y(manager.root, lv_pct(8));
    lv_obj_set_style_pad_all(manager.root, 8, 0);
    lv_obj_set_style_radius(manager.root, 0, 0);
    lv_obj_set_style_text_font(manager.root, &ui_font_chinese_24_4, 0);
    lv_obj_clear_flag(manager.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(manager.root, root_deleted, LV_EVENT_DELETE, NULL);

    manager.title_label = lv_label_create(manager.root);
    lv_label_set_text(manager.title_label, "文件管理器");
    lv_obj_set_style_text_color(manager.title_label, lv_palette_main(LV_PALETTE_PINK), 0);
    lv_obj_align(manager.title_label, LV_ALIGN_TOP_LEFT, 0, 4);

    /* 右上角从左到右为上一级、刷新、返回主页，按钮之间保留10像素间距。 */
    manager.up = make_button(manager.root, "上一级", 100);
    lv_obj_align(manager.up, LV_ALIGN_TOP_RIGHT, -230, 0);
    manager.refresh = make_button(manager.root, "刷新", 90);
    lv_obj_set_style_bg_color(manager.refresh, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_align(manager.refresh, LV_ALIGN_TOP_RIGHT, -130, 0);
    manager.close = make_button(manager.root, "返回主页", 120);
    lv_obj_set_style_bg_color(manager.close, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_align(manager.close, LV_ALIGN_TOP_RIGHT, 0, 0);
    manager.path_label = lv_label_create(manager.root);
    lv_obj_set_pos(manager.path_label, 0, 46);
    lv_obj_set_width(manager.path_label, lv_pct(100));
    lv_label_set_long_mode(manager.path_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t *head = lv_obj_create(manager.root);
    lv_obj_set_pos(head, 0, 80);
    lv_obj_set_size(head, lv_pct(100), 38);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    make_label(head, 8, 280, "名称");
    make_label(head, 300, 230, "创建时间");
    make_label(head, 540, 210, "直属数量 / 大小");

    lv_obj_t *list = lv_obj_create(manager.root);
    lv_obj_set_pos(list, 0, 120);
    lv_obj_set_size(list, lv_pct(100), 200);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 3, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    for (uint32_t i = 0; i < FILE_SERVICE_PAGE_SIZE; i++)
    {
        lv_obj_t *row = lv_btn_create(list);
        manager.rows[i] = row;
        lv_obj_set_size(row, lv_pct(100), 48);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xE8EDF2), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xA8D3FF), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(row, lv_color_black(), 0);
        lv_obj_add_event_cb(row, row_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, row_event, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)i);
        manager.icons[i] = lv_img_create(row);
        lv_img_set_size_mode(manager.icons[i], LV_IMG_SIZE_MODE_REAL);
        lv_obj_clear_flag(manager.icons[i], LV_OBJ_FLAG_CLICKABLE);
        manager.names[i] = make_label(row, 52, 236, "");
        manager.dates[i] = make_label(row, 300, 230, "");
        manager.details[i] = make_label(row, 540, 210, "");
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
    manager.previous = make_button(manager.root, "上一页", 100);
    lv_obj_set_pos(manager.previous, 0, 326);
    manager.page_label = lv_label_create(manager.root);
    lv_obj_set_pos(manager.page_label, 120, 332);
    manager.next = make_button(manager.root, "下一页", 100);
    lv_obj_set_pos(manager.next, 260, 326);
    manager.remove = make_button(manager.root, "删除", 100);
    lv_obj_set_style_bg_color(manager.remove, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_align(manager.remove, LV_ALIGN_TOP_RIGHT, 0, 326);
    manager.status_label = lv_label_create(manager.root);
    lv_obj_set_pos(manager.status_label, 0, 374);
    lv_obj_set_width(manager.status_label, lv_pct(100));
    lv_label_set_long_mode(manager.status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    manager.timer = lv_timer_create(poll_results, 50U, NULL);
    request_page(FILE_SERVICE_ROOT, 0U);
}
