#include "file_service.h"
#include "file_service_port.h"

#include <limits.h>
#include <string.h>

#if defined(__arm__) || defined(__ARMCC_VERSION)
#define FILE_WORK_RAM __attribute__((section(".axisram")))
#else
#define FILE_WORK_RAM
#endif

/* 列举、统计和删除共用静态工作区，仅由文件任务串行调用，不可重入。 */
/* ARM目标放在AXI SRAM中，不从LVGL堆或任务栈分配这些工作区。 */
static FILE_WORK_RAM file_service_entry_t page_entries[FILE_SERVICE_PAGE_SIZE];
static FILE_WORK_RAM file_service_event_t event;
static FILE_WORK_RAM file_service_entry_t work_entry;
static FILE_WORK_RAM char work_path[FILE_SERVICE_PATH_CAP];
/* 下降到子项前保存父路径字节长度，回退时用于截断work_path，避免递归。 */
static FILE_WORK_RAM uint16_t parent_lengths[FILE_SERVICE_MAX_DEPTH];

/**
 * file_service_path_valid() - 检查路径是否符合文件服务的访问规则
 * @path: 待检查路径；允许NULL，字符串必须在可读缓冲区内以空字符结束
 *
 * 只接受大小写匹配的SD:/根路径及其子路径，长度必须小于
 * FILE_SERVICE_PATH_CAP字节，不含结束符。拒绝空路径分量、点或双点分量、
 * 非根路径尾部分隔符、反斜杠、根前缀以外的冒号、控制字符及以点或空格结尾的分量。
 * 不修改路径，不规范化路径，也不检查目标是否存在或完整的FatFS名称合法性。
 *
 * Context: 仅检查内存中的字符串，不访问文件系统或共享服务工作区。
 * Return: 符合服务路径规则返回%true，NULL、超长或不符合规则时返回%false。
 */
bool file_service_path_valid(const char *path)
{
    const char *part;
    const char *cursor;
    size_t length = 0;

    if (path == NULL) return false;
    while (length < FILE_SERVICE_PATH_CAP && path[length] != '\0') length++;
    if (length < 4U || length >= FILE_SERVICE_PATH_CAP ||
        strncmp(path, FILE_SERVICE_ROOT, 4U) != 0) return false;
    if (length == 4U) return true;

    part = path + 4;
    for (cursor = part; ; cursor++)
    {
        if (*cursor == '\\' || *cursor == ':' ||
            (*cursor != '\0' && (unsigned char)*cursor < 32U)) return false;
        if (*cursor == '/' || *cursor == '\0')
        {
            size_t size = (size_t)(cursor - part);
            if (size == 0U || (size == 1U && part[0] == '.') ||
                (size == 2U && part[0] == '.' && part[1] == '.') ||
                part[size - 1U] == '.' || part[size - 1U] == ' ') return false;
            if (*cursor == '\0') break;
            part = cursor + 1;
        }
    }
    return true;
}

/**
 * file_service_join_path() - 拼接父路径和单个名称分量
 * @parent: 符合服务规则、以空字符结束的父路径
 * @name: 非空且以空字符结束的名称，不得包含斜杠或反斜杠
 * @dest: 非空输出缓冲区，允许与@parent相同，不得与@name存储区重叠
 * @capacity: 输出缓冲区容量，单位为字节，包含结束符所需空间
 *
 * 根路径后直接附加名称，其他父路径先补分隔符。不截断结果；容量不足时
 * 在写入之前返回。拼接完成后再次校验服务路径规则，因此返回路径无效时，
 * 输出缓冲区可能已经被修改，调用方不能将失败结果用于文件操作。
 *
 * Context: 只操作调用方缓冲区，不访问文件系统。
 * Return:
 * * %FILE_SERVICE_OK - 拼接成功，输出以空字符结束。
 * * %FILE_SERVICE_INVALID_PATH - 参数或拼接结果不符合路径规则。
 * * %FILE_SERVICE_PATH_TOO_LONG - 超出输出容量或服务路径长度上限。
 */
file_service_status_t file_service_join_path(const char *parent, const char *name,
                                             char *dest, size_t capacity)
{
    size_t parent_len, name_len, separator;
    if (!file_service_path_valid(parent) || name == NULL || dest == NULL ||
        name[0] == '\0' || strchr(name, '/') != NULL || strchr(name, '\\') != NULL)
        return FILE_SERVICE_INVALID_PATH;
    parent_len = strlen(parent);
    name_len = strlen(name);
    separator = parent_len == 4U ? 0U : 1U;
    if (parent_len + separator + name_len + 1U > capacity ||
        parent_len + separator + name_len >= FILE_SERVICE_PATH_CAP)
        return FILE_SERVICE_PATH_TOO_LONG;
    memmove(dest, parent, parent_len);
    if (separator) dest[parent_len] = '/';
    memcpy(dest + parent_len + separator, name, name_len + 1U);
    return file_service_path_valid(dest) ? FILE_SERVICE_OK : FILE_SERVICE_INVALID_PATH;
}

/**
 * file_service_parent_path() - 将路径原地改为其父路径
 * @path: 可写且以空字符结束的路径缓冲区
 *
 * 删除最后一个路径分量；根目录的直接子项回退到SD:/。路径无效或已是根目录
 * 时不修改缓冲区。不访问文件系统，也不检查父目录是否存在。
 *
 * Context: 只操作调用方缓冲区，调用期间不得由其他执行方修改该缓冲区。
 * Return: 成功回退一级返回%true，路径无效、NULL或已是根目录时返回%false。
 */
bool file_service_parent_path(char *path)
{
    char *slash;
    if (!file_service_path_valid(path) || strlen(path) == 4U) return false;
    slash = strrchr(path, '/');
    if (slash == path + 3) path[4] = '\0';
    else *slash = '\0';
    return true;
}

/**
 * file_service_status_text() - 获取文件服务状态的中文提示
 * @status: 文件服务状态码
 *
 * 未单独匹配的状态码统一显示为“路径无效”，不查询文件系统的原始错误。
 *
 * Context: 无文件系统访问，不使用可变共享工作区。
 * Return: 指向静态字符串的非空指针，调用方不得修改或释放。
 */
const char *file_service_status_text(file_service_status_t status)
{
    switch (status)
    {
        case FILE_SERVICE_OK: return "完成";
        case FILE_SERVICE_NOT_READY: return "SD卡未就绪，请插卡后刷新";
        case FILE_SERVICE_NO_FILESYSTEM: return "文件系统不支持，请使用FAT/FAT32";
        case FILE_SERVICE_IO_ERROR: return "SD卡读写失败或超时，请刷新重试";
        case FILE_SERVICE_DENIED: return "只读、对象占用或访问被拒绝";
        case FILE_SERVICE_NOT_FOUND: return "文件或目录不存在";
        case FILE_SERVICE_PATH_TOO_LONG: return "路径过长，无法操作";
        case FILE_SERVICE_DEPTH_LIMIT: return "目录层级超过删除上限";
        case FILE_SERVICE_NO_MEMORY: return "文件系统内存不足";
        case FILE_SERVICE_CANCELLED: return "操作已取消";
        default: return "路径无效";
    }
}

/**
 * publish() - 设置共享事件类型并同步调用结果回调
 * @kind: 本次事件类型
 * @emit: 非空事件回调
 * @user: 透传给回调的上下文指针，可以为NULL
 *
 * 除事件类型外不清理其他字段。回调收到共享事件的地址，如需保留内容，
 * 必须在返回前复制，不能依赖后续内容不变；回调不得重入列举或删除服务。
 *
 * Context: 文件任务上下文，与当前服务调用同步执行，可能因回调等待而阻塞。
 * Return: 原样返回回调结果，其取消含义由列举、统计或删除调用方决定。
 */
static bool publish(file_service_event_kind_t kind, file_service_emit_t emit, void *user)
{
    event.kind = kind;
    return emit(&event, user);
}

/**
 * finish() - 填写最终状态并尝试投递操作结束事件
 * @status: 本次操作的最终服务状态
 * @emit: 非空事件回调
 * @user: 透传给回调的上下文指针，可以为NULL
 *
 * native_error记录端口最近一次错误，不保证与路径校验等服务层错误一一对应。
 * 其他事件字段保持现值；忽略回调返回值，不保证接收方一定收到DONE事件。
 * 本函数不关闭目录，也不清理调用方持有的其他资源。
 *
 * Context: 文件任务上下文，可能因回调等待而阻塞，不可重入文件服务。
 */
static void finish(file_service_status_t status, file_service_emit_t emit, void *user)
{
    event.status = status;
    event.native_error = file_port_last_error();
    (void)publish(FILE_SERVICE_EVENT_DONE, emit, user);
}

/**
 * count_directory() - 统计目录直属的可见文件和子目录数量
 * @path: 要统计的目录路径
 * @files: 非空输出指针，保存直属普通文件数
 * @directories: 非空输出指针，保存直属子目录数
 * @emit: 非空事件回调，BEGIN事件用作取消检查
 * @user: 透传给回调的上下文指针，可以为NULL
 *
 * 不递归进入子目录，跳过端口标记为hidden的条目；当前FatFS端口将隐藏和
 * 系统条目均标记为hidden。输出计数先置零，出错或取消时可能保留部分计数。
 * 成功打开目录后在退出前关闭；已有遍历错误时优先返回该错误，否则返回关闭结果。
 *
 * Context: 文件任务串行调用，要求没有其他枚举句柄打开；可能等待磁盘、
 *          回调和端口让出CPU，不可在中断中调用。
 * Return: 成功返回%FILE_SERVICE_OK，取消检查失败返回%FILE_SERVICE_CANCELLED，
 *         其他失败返回相应的文件系统端口状态。
 */
static file_service_status_t count_directory(const char *path, uint32_t *files,
                                               uint32_t *directories,
                                               file_service_emit_t emit, void *user)
{
    file_service_status_t status = file_port_open_directory(path);
    bool end = false;
    *files = *directories = 0U;
    if (status != FILE_SERVICE_OK) return status;
    while ((status = file_port_read_entry(&work_entry, &end)) == FILE_SERVICE_OK && !end)
    {
        /* 心跳使页面关闭/切换后的取消能够在大目录中及时生效。 */
        if (!publish(FILE_SERVICE_EVENT_BEGIN, emit, user))
        {
            status = FILE_SERVICE_CANCELLED;
            break;
        }
        if (!work_entry.hidden)
        {
            if (work_entry.is_directory) (*directories)++;
            else (*files)++;
        }
        file_port_yield();
    }
    file_service_status_t close_status = file_port_close_directory();
    return status == FILE_SERVICE_OK ? close_status : status;
}

/**
 * file_service_list() - 枚举指定页目录条目并统计可见子目录
 * @path: 要枚举的SD:/目录路径，非法路径通过DONE事件报告
 * @page: 从0开始的页码，每页最多FILE_SERVICE_PAGE_SIZE条
 * @request_id: 写入所有事件的请求编号，供上层过滤过期结果
 * @emit: 同步事件回调，NULL时不执行操作；应在返回前复制需要保留的事件
 * @user: 透传给回调的上下文指针，可以为NULL
 *
 * 按端口枚举顺序分页，不排序，跳过hidden条目。即使只请求一页，也会扫描
 * 完整目录，以PAGE事件报告当前目录的可见文件、子目录总数及是否存在下一页。
 * 所选页条目通过ENTRY事件逐条报告，随后为该页每个子目录发送直属数量COUNT。
 * 单项统计失败记录在COUNT中，不会自动将最终DONE改为失败；DONE中的数量字段
 * 也不能代替PAGE中的全目录总数。
 *
 * BEGIN等回调检查点允许停止浏览，取消时不保证发送DONE。读取出错前已发出的
 * 条目不会撤回，接收方应结合最终状态判断。同步执行整个流程，不创建后台任务；
 * 回调不得再次调用列举或删除服务。
 *
 * Context: 仅由文件任务串行调用，共用静态工作区和端口枚举句柄，可能阻塞，
 *          不可在中断中调用；UI应通过文件任务提交请求。
 */
void file_service_list(const char *path, uint32_t page, uint32_t request_id,
                       file_service_emit_t emit, void *user)
{
    uint32_t position = 0U, stored = 0U, start;
    bool end = false;
    file_service_status_t status;
    if (emit == NULL) return;
    memset(&event, 0, sizeof(event));
    event.request_id = request_id;
    if (!file_service_path_valid(path) || page > UINT32_MAX / FILE_SERVICE_PAGE_SIZE)
    {
        finish(FILE_SERVICE_INVALID_PATH, emit, user);
        return;
    }
    strcpy(event.path, path);
    if (!publish(FILE_SERVICE_EVENT_BEGIN, emit, user)) return;
    status = file_port_mount();
    if (status != FILE_SERVICE_OK) { finish(status, emit, user); return; }
    status = file_port_open_directory(path);
    if (status != FILE_SERVICE_OK) { finish(status, emit, user); return; }
    start = page * FILE_SERVICE_PAGE_SIZE;
    while ((status = file_port_read_entry(&work_entry, &end)) == FILE_SERVICE_OK && !end)
    {
        if (!publish(FILE_SERVICE_EVENT_BEGIN, emit, user))
        {
            (void)file_port_close_directory();
            return;
        }
        file_port_yield();
        if (work_entry.hidden) continue;
        if (work_entry.is_directory) event.directories++;
        else event.files++;
        if (position >= start && stored < FILE_SERVICE_PAGE_SIZE)
        {
            page_entries[stored] = work_entry;
            event.entry = work_entry;
            event.index = stored++;
            if (!publish(FILE_SERVICE_EVENT_ENTRY, emit, user))
            {
                (void)file_port_close_directory();
                return;
            }
        }
        position++;
        file_port_yield();
    }
    file_service_status_t close_status = file_port_close_directory();
    if (status == FILE_SERVICE_OK) status = close_status;
    if (status != FILE_SERVICE_OK) { finish(status, emit, user); return; }
    event.has_next = position > start && position - start > stored;
    if (!publish(FILE_SERVICE_EVENT_PAGE, emit, user)) return;

    for (uint32_t i = 0; i < stored; i++)
    {
        if (!page_entries[i].is_directory) continue;
        status = file_service_join_path(path, page_entries[i].name, work_path, sizeof(work_path));
        event.files = event.directories = 0U;
        if (status == FILE_SERVICE_OK)
            status = count_directory(work_path, &event.files, &event.directories, emit, user);
        event.index = i;
        event.status = status;
        event.native_error = file_port_last_error();
        if (!publish(FILE_SERVICE_EVENT_COUNT, emit, user)) return;
    }
    finish(FILE_SERVICE_OK, emit, user);
}

/**
 * file_service_delete() - 删除文件或遍历删除整个目录树
 * @path: 要删除的SD:/文件或目录路径，禁止根目录及不符合服务规则的路径
 * @request_id: 写入删除进度和结束事件的请求编号
 * @emit: 同步事件回调，NULL时不执行操作；应在返回前复制需要保留的事件
 * @user: 透传给回调的上下文指针，可以为NULL
 *
 * 使用父路径长度栈迭代遍历，不使用C函数递归；下降到子项的深度不超过
 * FILE_SERVICE_MAX_DEPTH。每次关闭目录句柄后再处理其首个子项，先删子项，
 * 再删空目录。与浏览不同，删除包含隐藏和系统条目；遇到只读目标或其他错误
 * 时停止，已删除内容不回滚。
 *
 * 每成功删除一个文件或目录，就增加deleted并发送DELETE_PROGRESS；最后尝试
 * 发送DONE，包含最终状态、累计删除数和当前处理路径。原始端口错误不一定
 * 对应路径过长、层级超限等服务层状态，应优先检查服务状态。
 * 忽略回调返回值，不因页面关闭或结果投递失效而中止删除，仍可能因实际错误停止。
 * 本函数不弹出确认框，用户确认由上层在提交前完成。
 *
 * Context: 仅由文件任务串行调用，操作共用工作区和实际文件系统，可能阻塞；
 *          不可在中断或UI事件回调中直接调用，回调也不得重入列举或删除服务。
 */
void file_service_delete(const char *path, uint32_t request_id,
                         file_service_emit_t emit, void *user)
{
    uint32_t depth = 0U;
    bool end;
    file_service_status_t status;
    if (emit == NULL) return;
    memset(&event, 0, sizeof(event));
    event.request_id = request_id;
    if (!file_service_path_valid(path) || strlen(path) == 4U)
    {
        finish(FILE_SERVICE_INVALID_PATH, emit, user);
        return;
    }
    strcpy(work_path, path);
    strcpy(event.path, path);
    status = file_port_mount();
    if (status != FILE_SERVICE_OK) { finish(status, emit, user); return; }
    for (;;)
    {
        strcpy(event.path, work_path);
        status = file_port_stat(work_path, &work_entry);
        if (status != FILE_SERVICE_OK) break;
        if (work_entry.read_only) { status = FILE_SERVICE_DENIED; break; }
        if (work_entry.is_directory)
        {
            status = file_port_open_directory(work_path);
            if (status != FILE_SERVICE_OK) break;
            /* 每次只取首个子项，关闭句柄后再删除；不持有跨删除的DIR。 */
            status = file_port_read_entry(&work_entry, &end);
            file_service_status_t close_status = file_port_close_directory();
            if (status == FILE_SERVICE_OK) status = close_status;
            if (status != FILE_SERVICE_OK) break;
            if (!end)
            {
                if (depth >= FILE_SERVICE_MAX_DEPTH)
                {
                    status = FILE_SERVICE_DEPTH_LIMIT;
                    break;
                }
                parent_lengths[depth] = (uint16_t)strlen(work_path);
                status = file_service_join_path(work_path, work_entry.name,
                                                 work_path, sizeof(work_path));
                if (status != FILE_SERVICE_OK) break;
                depth++;
                file_port_yield();
                continue;
            }
        }
        status = file_port_remove(work_path);
        if (status != FILE_SERVICE_OK) break;
        event.deleted++;
        (void)publish(FILE_SERVICE_EVENT_DELETE_PROGRESS, emit, user);
        if (depth == 0U) break;
        work_path[parent_lengths[--depth]] = '\0';
        file_port_yield();
    }
    finish(status, emit, user);
}
