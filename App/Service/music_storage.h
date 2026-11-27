#ifndef MUSIC_STORAGE_H
#define MUSIC_STORAGE_H
#include "file_service.h"
#include "music_service.h"
typedef enum
{
    MUSIC_STORAGE_SCAN, MUSIC_STORAGE_OPEN, MUSIC_STORAGE_READ, MUSIC_STORAGE_CLOSE, MUSIC_STORAGE_SEEK
} music_storage_op_t;
/* 目录固定为 MUSIC_DIRECTORY，完整路径无需为每首歌保留 512 字节。 */
#define MUSIC_TRACK_PATH_CAP (sizeof(MUSIC_DIRECTORY) + MUSIC_NAME_CAP)
typedef struct
{
    char name[MUSIC_NAME_CAP];
    char path[MUSIC_TRACK_PATH_CAP];
} music_track_t;
typedef struct
{
    uint32_t seconds;
    bool estimated;
    bool seekable; /* 文件任务已建立用于定位的元数据。 */
} music_duration_t;
bool music_storage_create(void);
/* 音乐任务单一调用者；读请求完成前必须保持buffer有效，并在关闭文件前取回结果。 */
bool music_storage_read_begin(void *buffer, size_t capacity);
file_service_status_t music_storage_read_end(size_t *amount);
/* OPEN 返回时长和定位能力；SEEK 的 buffer 指向 uint32_t 目标秒数。
 * 只能由音乐任务调用；返回前一定等待文件任务完成，保证buffer生命周期。 */
file_service_status_t music_storage_request(music_storage_op_t op, const char *path,
                                           void *buffer, size_t capacity, size_t *amount);
/* 仅文件任务及其遍历让出点调用，所有FatFS访问仍在同一任务中。 */
void music_storage_poll(void);
bool music_storage_active(void);
#endif
