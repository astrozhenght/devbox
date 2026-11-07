#ifndef FILE_SERVICE_H
#define FILE_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 文件服务允许访问的SD卡根路径；可以浏览，但不允许删除根目录。 */
#define FILE_SERVICE_ROOT         "SD:/"

/* 完整路径缓冲容量，单位为字节，包含结束符；路径内容最多511字节。 */
#define FILE_SERVICE_PATH_CAP     512U

/* 单个文件或目录的名称缓冲容量，单位为字节，包含结束符；UTF-8字节数不等于字符数。 */
#define FILE_SERVICE_NAME_CAP     768U

/* 一页最多返回的可见条目数，文件和目录合计，不包含隐藏或系统条目。 */
#define FILE_SERVICE_PAGE_SIZE    20U

/* 删除遍历的最大下降深度：待删除目标为0，每进入一个子项加1，普通浏览不使用此限制。 */
#define FILE_SERVICE_MAX_DEPTH    16U

/* 文件服务状态：表示单项操作结果，不是位掩码，也不能直接当作FatFS原始错误码。 */
typedef enum
{
    FILE_SERVICE_OK = 0,          /* 本次操作成功。 */
    FILE_SERVICE_NOT_READY,       /* SD卡未就绪或底层初始化失败。 */
    FILE_SERVICE_NO_FILESYSTEM,   /* 无法识别或当前配置不支持的文件系统。 */
    FILE_SERVICE_IO_ERROR,        /* 读写、超时或其他未单独映射的端口错误。 */
    FILE_SERVICE_DENIED,          /* 只读、写保护、对象占用或访问被拒绝。 */
    FILE_SERVICE_NOT_FOUND,       /* 指定文件或目录不存在。 */
    FILE_SERVICE_PATH_TOO_LONG,   /* 路径超过服务上限或输出缓冲区容量不足。 */
    FILE_SERVICE_INVALID_PATH,    /* 路径、名称、卷标无效，或分页计算会溢出。 */
    FILE_SERVICE_DEPTH_LIMIT,     /* 删除时继续进入子项将超过允许的下降深度。 */
    FILE_SERVICE_NO_MEMORY,       /* 文件系统所需工作内存不足。 */
    FILE_SERVICE_CANCELLED        /* 浏览统计在回调检查点取消，不表示删除被取消。 */
} file_service_status_t;

/* 文件系统时间，不含时区信息；只有valid为true时，日期和时间字段才可用于显示。 */
typedef struct
{
    uint16_t year; /* 完整公历年份，当前FatFS时间编码范围为1980～2107年。 */
    /* month: 1～12；day: 1～当月天数；hour: 0～23；minute、second: 0～59。
     * 当前FatFS时间精度为2秒，不提供毫秒；valid为false时不保证字段范围有效。
     */
    uint8_t month, day, hour, minute, second;
    bool valid; /* 时间存在且通过日期、时分秒合法性检查。 */
} file_service_time_t;

/* 单个文件或目录的元数据副本，不包含文件内容、打开句柄或父目录路径。 */
typedef struct
{
    char name[FILE_SERVICE_NAME_CAP]; /* UTF-8名称，以空字符结束，不是完整路径。 */
    uint32_t size_bytes;              /* 普通文件大小，单位为字节；不表示目录内容总大小。 */
    bool is_directory;                /* true为目录，false为普通文件。 */
    bool hidden;                      /* 当前FatFS端口将隐藏或系统条目均标记为true。 */
    bool read_only;                   /* 只读属性；删除服务遇到此属性时拒绝删除。 */
    file_service_time_t created;      /* 文件系统记录的创建时间，不是最近修改时间。 */
} file_service_entry_t;

/* 文件服务回调事件类型，决定file_service_event_t中哪些字段有意义。 */
typedef enum
{
    FILE_SERVICE_EVENT_BEGIN,           /* 浏览或统计的取消检查心跳，可重复；文件任务不将其投递给UI。 */
    FILE_SERVICE_EVENT_ENTRY,           /* 当前页的一条可见条目，使用index和entry。 */
    FILE_SERVICE_EVENT_PAGE,            /* 当前目录的可见总数和分页状态，使用files、directories、has_next。 */
    FILE_SERVICE_EVENT_COUNT,           /* 当前页某个子目录的直属数量，使用index、status及计数字段。 */
    FILE_SERVICE_EVENT_DELETE_PROGRESS, /* 删除进度，deleted为累计成功删除的文件和目录数。 */
    FILE_SERVICE_EVENT_DONE             /* 整个列举或删除流程结束，使用status；取消浏览时不保证收到。 */
} file_service_event_kind_t;

/*
 * 文件服务事件快照。必须先按kind解释字段，不能将所有字段视为同时有效。
 * PAGE中的计数是当前目录总数，COUNT中的计数是指定子目录的直属数量；
 * COUNT失败时计数可能不完整，单项统计失败也不一定令最终DONE失败。
 * 未用于当前事件的字段可能保留先前内容，不能用DONE中的计数代替PAGE总数。
 * 原始回调收到的是服务复用工作区的地址，需要保留时必须复制；文件任务入队时会复制。
 */
typedef struct
{
    file_service_event_kind_t kind;    /* 事件类型。 */
    uint32_t request_id;               /* 所属请求编号，接收方用它过滤过期结果。 */
    file_service_status_t status;      /* COUNT为该子目录统计状态，DONE为流程最终状态。 */
    int32_t native_error;              /* COUNT/DONE记录端口最近错误；服务层错误未必有对应原始码。 */
    uint32_t index;                    /* ENTRY/COUNT对应的当前页条目下标，从0开始。 */
    uint32_t files;                    /* PAGE为目录可见普通文件总数，COUNT为子目录直属可见文件数。 */
    uint32_t directories;              /* PAGE为可见子目录总数，COUNT为子目录直属可见目录数。 */
    uint32_t deleted;                  /* 删除进度及删除DONE中的累计成功删除项数，含文件和目录。 */
    bool has_next;                     /* 仅PAGE使用，表示当前页之后还有可见条目。 */
    char path[FILE_SERVICE_PATH_CAP];  /* 列举为请求目录，删除为当前处理路径；参数无效时可能为空。 */
    file_service_entry_t entry;        /* 仅ENTRY使用，保存该条目的名称及元数据。 */
} file_service_event_t;

/* 返回 false 可取消浏览；删除一旦开始，不因页面关闭而中止在目录遍历中间。 */
typedef bool (*file_service_emit_t)(const file_service_event_t *event, void *user);

bool file_service_path_valid(const char *path);
file_service_status_t file_service_join_path(const char *parent, const char *name,
                                             char *dest, size_t capacity);
bool file_service_parent_path(char *path);
const char *file_service_status_text(file_service_status_t status);
void file_service_list(const char *path, uint32_t page, uint32_t request_id,
                       file_service_emit_t emit, void *user);
void file_service_delete(const char *path, uint32_t request_id,
                         file_service_emit_t emit, void *user);

#endif
