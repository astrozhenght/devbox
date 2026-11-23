#ifndef MUSIC_SERVICE_H
#define MUSIC_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
#include "music_config.h"
#define MUSIC_NAME_CAP 128U
typedef enum
{
    MUSIC_IDLE, MUSIC_SCANNING, MUSIC_READY, MUSIC_PLAYING,
    MUSIC_PAUSED, MUSIC_ERROR
} music_state_t;
typedef enum
{
    MUSIC_SCAN, MUSIC_PLAY, MUSIC_TOGGLE, MUSIC_PREVIOUS,
    MUSIC_NEXT, MUSIC_STOP, MUSIC_VOLUME, MUSIC_MODE
} music_command_t;
typedef enum
{
    MUSIC_PLAY_MODE_ORDER = 0,
    MUSIC_PLAY_MODE_REPEAT_ONE,
    MUSIC_PLAY_MODE_SHUFFLE
} music_play_mode_t;
typedef struct
{
    music_state_t state;
    uint16_t track;
    uint16_t count;
    uint8_t volume;
    uint8_t mode; /* music_play_mode_t。 */
    uint32_t seconds;
    uint32_t duration_seconds; /* 文件头时长；0表示无法识别。 */
    bool duration_estimated; /* 无帧数信息的MP3按码率估算，UI加~区分。 */
    uint32_t playlist_version;
    char title[MUSIC_NAME_CAP];
    char message[96];
} music_snapshot_t;
bool music_service_create(void);
bool music_service_command(music_command_t command, uint32_t value);
void music_service_snapshot(music_snapshot_t *snapshot);
bool music_service_track_name(uint16_t index, char *name, unsigned capacity);
/* 本地烧录前请求停播并等待文件关闭；只在任务上下文调用。 */
bool music_service_suspend_for_programming(void);
void music_service_resume_after_programming(void);
#endif
