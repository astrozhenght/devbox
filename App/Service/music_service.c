#include "music_service.h"
#include "music_storage.h"
#include "bsp_vs1053.h"
#include "music_audio.h"
#include "audio_spectrum_service.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>

typedef struct { music_command_t kind; uint32_t value; } command_t;
static QueueHandle_t commands;
static MUSIC_RAM music_track_t tracks[MUSIC_MAX_TRACKS];
static MUSIC_RAM music_track_t scan_tracks[MUSIC_MAX_TRACKS];
static MUSIC_RAM uint8_t stream_buffer[16384];
static MUSIC_RAM StackType_t stack[1536];
static StaticTask_t task_control;
static music_snapshot_t state;
static music_snapshot_t published;
static volatile bool programming;
static volatile bool programming_ack;
static volatile bool programming_ok;
static bool initialized;
static bool file_open;

static void publish(void)
{
    taskENTER_CRITICAL(); published = state; taskEXIT_CRITICAL();
}

void music_service_snapshot(music_snapshot_t *snapshot)
{
    taskENTER_CRITICAL(); *snapshot = published; taskEXIT_CRITICAL();
}

bool music_service_track_name(uint16_t index, char *name, unsigned capacity)
{
    bool ok = false;
    if (name == NULL || capacity == 0) return false;
    taskENTER_CRITICAL();
    if (index < published.count)
    {
        strncpy(name, tracks[index].name, capacity - 1U);
        name[capacity - 1U] = '\0';
        ok = true;
    }
    taskEXIT_CRITICAL();
    return ok;
}

bool music_service_command(music_command_t kind, uint32_t value)
{
    command_t cmd = {kind, value};
    if (commands == NULL || programming) return false;
    return xQueueSend(commands, &cmd, 0) == pdPASS;
}

static bool stop(bool cancel)
{
    bool ok = true;
    audio_spectrum_enable(false);
    platform_music_sai_stop();
    if (file_open)
    {
        if (initialized && !bsp_vs1053_finish(cancel))
        {
            initialized = false;
            ok = false;
        }
        if (music_storage_request(MUSIC_STORAGE_CLOSE, NULL, NULL, 0, NULL) != FILE_SERVICE_OK)
            ok = false;
        file_open = false;
    }
    state.state = ok ? MUSIC_READY : MUSIC_ERROR;
    strcpy(state.message, ok ? "已停止" : "停止或关闭文件失败");
    publish();
    return ok;
}

static void fail(const char *message)
{
    stop(true);
    state.state = MUSIC_ERROR;
    strncpy(state.message, message, sizeof(state.message) - 1U);
    publish();
}

bool music_service_suspend_for_programming(void)
{
    programming_ack = false;
    programming_ok = false;
    programming = true;
    TickType_t start = xTaskGetTickCount();
    while (!programming_ack)
    {
        if (xTaskGetTickCount() - start > pdMS_TO_TICKS(3000)) return false;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return programming_ok;
}

void music_service_resume_after_programming(void) { programming = false; }

static void music_entry(void *argument)
{
    (void)argument;
    size_t amount = 0;
    size_t position = 0;
    TickType_t last_ready = 0;
    TickType_t last_clock = 0;
    state.volume = 50;
    state.state = MUSIC_IDLE;
    strcpy(state.message, MUSIC_HARDWARE_CONFIRMED ? "请选择曲目" : "模块接线待确认");
    publish();
    for (;;)
    {
        command_t cmd;
        if (programming)
        {
            if (!programming_ack)
            {
                programming_ok = stop(true);
                /* 清除烧录前排队的播放命令，避免作业结束后意外恢复。 */
                while (xQueueReceive(commands, &cmd, 0) == pdPASS) {}
                programming_ack = true; /* 文件关闭后才允许本地烧录开始。 */
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (xQueueReceive(commands, &cmd, state.state == MUSIC_PLAYING ? 0 : pdMS_TO_TICKS(20)) == pdPASS)
        {
            if (cmd.kind == MUSIC_SCAN)
            {
                stop(true);
                state.state = MUSIC_SCANNING;
                publish();
                size_t count = 0;
                if (music_storage_request(MUSIC_STORAGE_SCAN, MUSIC_DIRECTORY, scan_tracks,
                                          MUSIC_MAX_TRACKS, &count) != FILE_SERVICE_OK)
                    fail("无法读取SD:/Music");
                else
                {
                    taskENTER_CRITICAL(); published.count = 0; taskEXIT_CRITICAL();
                    memcpy(tracks, scan_tracks, count * sizeof(music_track_t));
                    state.count = (uint16_t)count;
                    state.playlist_version++;
                    state.state = MUSIC_READY;
                    strcpy(state.message, count ? "请选择曲目" : "目录没有MP3/WAV");
                    publish();
                }
            }
            else if (cmd.kind == MUSIC_VOLUME)
            {
                state.volume = cmd.value > 100 ? 100 : cmd.value;
                if (initialized && !bsp_vs1053_volume(state.volume)) fail("音量设置失败");
            }
            else if (cmd.kind == MUSIC_MODE && cmd.value <= MUSIC_PLAY_MODE_SHUFFLE)
                state.mode = (uint8_t)cmd.value;
            else if (cmd.kind == MUSIC_STOP) stop(true);
            else if (cmd.kind == MUSIC_TOGGLE && file_open)
            {
                state.state = state.state == MUSIC_PLAYING ? MUSIC_PAUSED : MUSIC_PLAYING;
                audio_spectrum_enable(state.state == MUSIC_PLAYING);
                last_ready = xTaskGetTickCount();
            }
            else if (state.count > 0)
            {
                uint16_t next = state.track;
                if (cmd.kind == MUSIC_PLAY) next = cmd.value < state.count ? cmd.value : state.count;
                else if (cmd.kind == MUSIC_NEXT) next = (next + 1U) % state.count;
                else if (cmd.kind == MUSIC_PREVIOUS) next = (next + state.count - 1U) % state.count;
                if (next >= state.count) continue;
                stop(true);
                if (!initialized) initialized = bsp_vs1053_init();
                if (!initialized) { fail(MUSIC_HARDWARE_CONFIRMED ? "VS1053初始化失败" : "模块接线待确认"); continue; }
                music_duration_t duration = {0};
                if (music_storage_request(MUSIC_STORAGE_OPEN, tracks[next].path, &duration, sizeof(duration), NULL) != FILE_SERVICE_OK)
                { fail("音乐文件打开失败"); continue; }
                file_open = true;
                state.track = next;
                state.seconds = 0;
                state.duration_seconds = duration.seconds;
                state.duration_estimated = duration.estimated;
                strcpy(state.title, tracks[next].name);
                state.state = MUSIC_PLAYING;
                amount = position = 0;
                if (!bsp_vs1053_volume(state.volume)) { fail("音量设置失败"); continue; }
                bool capture = platform_music_sai_start();
                audio_spectrum_enable(capture);
                strcpy(state.message, capture ? "正在播放" : "播放中，频谱采集失败");
                last_ready = last_clock = xTaskGetTickCount();
                publish();
            }
            publish();
        }
        if (state.state != MUSIC_PLAYING) continue;
        if (position == amount)
        {
            if (music_storage_request(MUSIC_STORAGE_READ, NULL, stream_buffer,
                                      sizeof(stream_buffer), &amount) != FILE_SERVICE_OK)
            { fail("音乐读取失败，请检查SD卡"); continue; }
            position = 0;
            if (amount == 0)
            {
                uint16_t next = state.track;
                if (state.mode == 2 && state.count > 1)
                    next = (next + 1U + xTaskGetTickCount() % (state.count - 1U)) % state.count;
                else if (state.mode == 0) next = (next + 1U) % state.count;
                if (stop(false) && !music_service_command(MUSIC_PLAY, next))
                    fail("播放请求队列已满");
                continue;
            }
        }
        if (bsp_vs1053_ready())
        {
            uint16_t bytes = amount - position > 32U ? 32U : (uint16_t)(amount - position);
            if (!bsp_vs1053_send(stream_buffer + position, bytes)) { fail("SPI音频传输失败"); continue; }
            position += bytes;
            last_ready = xTaskGetTickCount();
        }
        else
        {
            if (xTaskGetTickCount() - last_ready > pdMS_TO_TICKS(2000))
            { fail("DREQ等待超时"); continue; }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (xTaskGetTickCount() - last_clock >= pdMS_TO_TICKS(500))
        {
            uint16_t seconds;
            if (!bsp_vs1053_seconds(&seconds)) { fail("解码时间读取失败"); continue; }
            state.seconds = seconds;
            last_clock = xTaskGetTickCount();
            publish();
        }
    }
}

bool music_service_create(void)
{
    commands = xQueueCreate(8, sizeof(command_t));
    return commands != NULL && music_storage_create() && audio_spectrum_create() &&
        xTaskCreateStatic(music_entry, "music", 1536, NULL, 2, stack, &task_control) != NULL;
}
