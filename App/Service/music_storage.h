#ifndef MUSIC_STORAGE_H
#define MUSIC_STORAGE_H
#include "file_service.h"
#include "music_service.h"
typedef enum { MUSIC_STORAGE_SCAN, MUSIC_STORAGE_OPEN, MUSIC_STORAGE_READ, MUSIC_STORAGE_CLOSE } music_storage_op_t;
typedef struct
{
    char name[MUSIC_NAME_CAP];
    char path[FILE_SERVICE_PATH_CAP];
} music_track_t;
typedef struct
{
    uint32_t seconds;
    bool estimated;
} music_duration_t;
bool music_storage_create(void);
/* 音乐任务单一调用者；读请求完成前必须保持buffer有效，并在关闭文件前取回结果。 */
bool music_storage_read_begin(void *buffer, size_t capacity);
file_service_status_t music_storage_read_end(size_t *amount);
/* OPEN可通过buffer/capacity返回music_duration_t；READ仍返回原始字节。
 * 只能由音乐任务调用；返回前一定等待文件任务完成，保证buffer生命周期。 */
file_service_status_t music_storage_request(music_storage_op_t op, const char *path,
                                           void *buffer, size_t capacity, size_t *amount);
/* 仅文件任务及其遍历让出点调用，所有FatFS访问仍在同一任务中。 */
void music_storage_poll(void);
bool music_storage_active(void);
#endif
