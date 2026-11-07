#include "file_service_port.h"
#include "ff.h"
#include "ff_gen_drv.h"
#include "sd_diskio_dma.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* FatFS端口共用工作区，仅由文件任务串行访问，不支持并发或回调重入。 */
static FATFS filesystem __attribute__((section(".axisram"), aligned(32))); /* 卷挂载对象。 */
static DIR directory __attribute__((section(".axisram"))); /* 唯一目录枚举句柄。 */
static FILINFO info __attribute__((section(".axisram"))); /* 目录枚举和属性查询共用的元数据。 */
/* linked: 驱动已注册到物理盘0；mounted: 最近挂载成功；directory_open: 枚举句柄已打开。 */
static bool linked, mounted, directory_open;
static char drive_path[4]; /* ST驱动注册接口生成的数字盘符，例如"0:/"，包含结束符。 */
static FRESULT last_error; /* 最近记录的FatFS错误，不会被普通操作成功自动清除。 */

_Static_assert(sizeof(info.fname) <= FILE_SERVICE_NAME_CAP, "File service filename buffer is too small");


/**
 * translate() - 将FatFS返回值转换为文件服务状态
 * @result: FatFS操作返回值
 *
 * 仅失败时更新last_error，成功时保留之前的错误。未单独映射的FatFS错误
 * 统一转换为FILE_SERVICE_IO_ERROR，调用方可另外获取原始错误辅助定位。
 *
 * Context: 文件任务串行调用，访问共享错误记录，不可重入。
 * Return: 对应的文件服务状态，不是原始FRESULT数值。
 */
static file_service_status_t translate(FRESULT result)
{
    if (result != FR_OK) last_error = result;
    switch (result)
    {
        case FR_OK: return FILE_SERVICE_OK;
        case FR_NOT_READY: return FILE_SERVICE_NOT_READY;
        case FR_NO_FILESYSTEM: return FILE_SERVICE_NO_FILESYSTEM;
        case FR_NO_FILE:
        case FR_NO_PATH: return FILE_SERVICE_NOT_FOUND;
        case FR_DENIED:
        case FR_WRITE_PROTECTED:
        case FR_LOCKED: return FILE_SERVICE_DENIED;
        case FR_NOT_ENOUGH_CORE: return FILE_SERVICE_NO_MEMORY;
        case FR_INVALID_NAME:
        case FR_INVALID_DRIVE: return FILE_SERVICE_INVALID_PATH;
        default: return FILE_SERVICE_IO_ERROR;
    }
}

/**
 * convert_info() - 将共享FatFS元数据转换为文件服务条目
 * @entry: 非空输出指针，写入前先清零整个条目
 *
 * 使用最近一次枚举或属性查询写入的info，复制名称、大小、目录和只读属性。
 * 当前UTF-8名称缓冲容量由静态断言约束；隐藏属性同时包含AM_HID和AM_SYS。
 * 创建时间取自crdate/crtime，不使用修改时间；解码后检查实际月份天数、闰年
 * 和时分秒，缺失或非法时将created.valid置为false。秒字段的编码精度为2秒。
 *
 * Context: 文件任务串行调用，要求info已由成功的FatFS操作填充，期间不能被覆盖。
 */
static void convert_info(file_service_entry_t *entry)
{
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, info.fname, strlen(info.fname) + 1U);
    entry->size_bytes = (uint32_t)info.fsize;
    entry->is_directory = (info.fattrib & AM_DIR) != 0U;
    entry->hidden = (info.fattrib & (AM_HID | AM_SYS)) != 0U;
    entry->read_only = (info.fattrib & AM_RDO) != 0U;
    entry->created.year = 1980U + (info.crdate >> 9);
    entry->created.month = (info.crdate >> 5) & 15U;
    entry->created.day = info.crdate & 31U;
    entry->created.hour = info.crtime >> 11;
    entry->created.minute = (info.crtime >> 5) & 63U;
    entry->created.second = (info.crtime & 31U) * 2U;
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint16_t year = entry->created.year;
    uint8_t month = entry->created.month;
    uint8_t max_day = month >= 1U && month <= 12U ? days[month - 1U] : 0U;
    if (month == 2U && ((year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U)) max_day++;
    entry->created.valid = info.crdate != 0U && entry->created.day >= 1U &&
        entry->created.day <= max_day && entry->created.hour < 24U &&
        entry->created.minute < 60U && entry->created.second < 60U;
}

/**
 * file_port_mount() - 注册SD驱动并按需挂载或重新挂载文件系统
 *
 * 每次调用先清除原始错误记录。首次注册要求SD驱动占用物理盘0，应在其他
 * 磁盘驱动注册前完成。已有挂载且disk_status(0)就绪时直接复用；否则解除旧
 * 挂载，清除ST驱动的初始化缓存，再立即挂载以触发底层重新初始化。
 * 不格式化介质，也不提供插卡定时检测；重新尝试由上层请求触发。
 *
 * Context: 文件任务串行调用，重挂载前不得持有目录或文件句柄；可能等待SD卡，
 *          不可在中断或UI事件回调中直接调用。
 * Return: 已有可用挂载或本次挂载成功返回%FILE_SERVICE_OK，
 *         注册盘号不符合要求或挂载失败时返回转换后的服务状态。
 */
file_service_status_t file_port_mount(void)
{
    last_error = FR_OK;
    if (!linked)
    {
        if (FATFS_LinkDriver(&SD_Driver, drive_path) != 0U || drive_path[0] != '0')
            return translate(FR_INVALID_DRIVE);
        linked = true;
    }
    if (mounted && disk_status(0) == 0U) return FILE_SERVICE_OK;
    (void)f_mount(NULL, FILE_SERVICE_ROOT, 0);
    disk.is_initialized[0] = 0U;
    FRESULT result = f_mount(&filesystem, FILE_SERVICE_ROOT, 1);
    mounted = result == FR_OK;
    return translate(result);
}

/**
 * file_port_open_directory() - 打开唯一的目录枚举句柄
 * @path: 非空且以空字符结束的目录路径，服务层应先检查访问范围
 *
 * 仅成功打开时设置directory_open。已有枚举句柄打开时拒绝第二次打开，
 * 记录FR_LOCKED，并保持原句柄；完成或取消遍历后必须调用file_port_close_directory()。
 *
 * Context: 文件任务串行调用，通常在file_port_mount()成功后使用，可能访问磁盘。
 * Return: 成功返回%FILE_SERVICE_OK；已有打开句柄返回%FILE_SERVICE_DENIED，
 *         其他错误转换为相应服务状态。
 */
file_service_status_t file_port_open_directory(const char *path)
{
    if (directory_open) return translate(FR_LOCKED);
    FRESULT result = f_opendir(&directory, path);
    directory_open = result == FR_OK;
    return translate(result);
}

/**
 * file_port_read_entry() - 从当前目录读取下一个实际条目
 * @entry: 非空条目输出指针，仅成功且@end为false时包含本次有效条目
 * @end: 非空输出指针，成功时表示是否已到目录末尾
 *
 * 跳过点和双点目录项，保留隐藏及系统项，供浏览或删除服务分别处理。
 * 名称超出条目缓冲区或FatFS返回问号占位名称时报告路径无效，不截断名称。
 * 到达末尾时设置@end为true并返回成功，不修改@entry；返回错误时不应使用
 * 输出字段判断遍历结果。此函数不检查directory_open标志，调用方须保证句柄有效。
 *
 * Context: 文件任务串行调用，要求目录已成功打开，可能等待磁盘，不可重入。
 * Return: 读到条目或正常到达末尾均返回%FILE_SERVICE_OK，
 *         名称不可用或读取失败时返回转换后的服务状态。
 */
file_service_status_t file_port_read_entry(file_service_entry_t *entry, bool *end)
{
    FRESULT result;
    do
    {
        result = f_readdir(&directory, &info);
        if (result != FR_OK) return translate(result);
        *end = info.fname[0] == '\0';
        if (*end) return FILE_SERVICE_OK;
    } while (strcmp(info.fname, ".") == 0 || strcmp(info.fname, "..") == 0);
    if (strlen(info.fname) >= sizeof(entry->name) || strcmp(info.fname, "?") == 0)
        return translate(FR_INVALID_NAME);
    convert_info(entry);
    return FILE_SERVICE_OK;
}

/**
 * file_port_close_directory() - 关闭当前目录枚举句柄
 *
 * 没有打开句柄时直接成功；否则尝试关闭，并无论关闭是否成功都清除软件
 * 打开标志。调用方仍需检查返回值，不能把标志清除当作FatFS关闭必定成功。
 *
 * Context: 文件任务串行调用，不可与目录枚举或其他端口操作并发执行。
 * Return: 无句柄或关闭成功返回%FILE_SERVICE_OK，否则返回转换后的服务状态。
 */
file_service_status_t file_port_close_directory(void)
{
    if (!directory_open) return FILE_SERVICE_OK;
    FRESULT result = f_closedir(&directory);
    directory_open = false;
    return translate(result);
}

/**
 * file_port_stat() - 查询指定文件或目录的元数据
 * @path: 非空且以空字符结束的目标路径，服务层应先检查访问范围
 * @entry: 非空输出指针，仅查询成功时写入，失败时保持原值
 *
 * 复用共享info缓冲区，再转换为独立的条目副本，不打开新的目录枚举句柄。
 *
 * Context: 文件任务串行调用，通常在挂载成功后使用，可能等待磁盘，不可重入。
 * Return: 查询成功返回%FILE_SERVICE_OK，否则返回转换后的服务状态。
 */
file_service_status_t file_port_stat(const char *path, file_service_entry_t *entry)
{
    FRESULT result = f_stat(path, &info);
    if (result == FR_OK) convert_info(entry);
    return translate(result);
}

/**
 * file_port_remove() - 删除一个文件或空目录
 * @path: 非空且以空字符结束的目标路径，访问范围和删除确认由上层负责
 *
 * 直接调用f_unlink()，不遍历非空目录、不清除只读属性，也不提供回滚。
 * 删除目录树由上层文件服务完成，应在关闭枚举句柄后调用本接口。
 *
 * Context: 文件任务串行调用，可能写入磁盘，不可在中断或UI事件回调中直接调用。
 * Return: 删除成功返回%FILE_SERVICE_OK，否则返回转换后的服务状态。
 */
file_service_status_t file_port_remove(const char *path) { return translate(f_unlink(path)); }

/**
 * file_port_last_error() - 获取端口最近记录的FatFS错误码
 *
 * 获取后不清除；普通操作成功也不清除，file_port_mount()开始时才重置。
 * 服务层路径、深度等错误不一定产生新的FatFS错误，应结合服务状态解释。
 *
 * Context: 文件任务串行调用，只读取共享记录，不访问磁盘。
 * Return: 最近记录的FRESULT数值；初始状态或挂载时重置后为%FR_OK，
 *         不能仅凭本返回值判断刚才的服务操作是否成功。
 */
int32_t file_port_last_error(void) { return (int32_t)last_error; }

/**
 * file_port_yield() - 在长时间遍历期间让出文件任务的CPU执行时间
 *
 * 请求阻塞pdMS_TO_TICKS(1)个节拍，当前1000 Hz配置下为1个节拍。
 * 实际恢复执行的时机由调度器决定，不是精确的1 ms延时。
 *
 * Context: 调度器已运行且允许阻塞的文件任务上下文，不可在中断中调用。
 */
void file_port_yield(void) { vTaskDelay(pdMS_TO_TICKS(1)); }
