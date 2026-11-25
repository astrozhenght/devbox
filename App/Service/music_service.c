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
static MUSIC_RAM uint8_t stream_buffer[2][16384];
static unsigned stream_index;
static bool read_pending;
static MUSIC_RAM StackType_t stack[1536];
static StaticTask_t task_control;
static music_snapshot_t state;
static music_snapshot_t published;
static volatile bool programming;
static volatile bool programming_ack;
static volatile bool programming_ok;
static bool initialized;
static bool file_open;
/* 仅供调试器读取；记录连续播放的供数耗时，不增加SPI访问。单位为RTOS节拍。 */
static volatile struct
{
    uint32_t bytes_sent;
    TickType_t max_feed_gap;
    TickType_t max_read_wait;
    TickType_t max_clock_wait;
} playback_timing;

static void publish(void)
{
    taskENTER_CRITICAL(); published = state; taskEXIT_CRITICAL();
}

/**
 * music_service_snapshot() - 复制最近一次发布的播放状态
 * @snapshot: 调用方提供的非空输出缓冲区
 *
 * Context: 任务上下文。通过短临界区保证整份快照一致，不等待音乐任务。
 */
void music_service_snapshot(music_snapshot_t *snapshot)
{
    taskENTER_CRITICAL(); *snapshot = published; taskEXIT_CRITICAL();
}

/**
 * music_service_track_name() - 按已发布的曲目索引复制文件名
 * @index: 从 0 开始的曲目索引
 * @name: 用于接收名称的缓冲区
 * @capacity: 缓冲区容量，单位为字节，包含字符串结束符
 *
 * Context: 任务上下文。短临界区内校验索引并复制名称。
 * Return: 已复制名称返回 %true，容量不足时按字节截断并补结束符。
 * 索引无效、@name 为空或 @capacity 为 0 时返回 %false。
 */
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

/**
 * music_service_command() - 向音乐任务提交控制命令
 * @kind: 命令类型
 * @value: 播放命令的曲目索引、音量命令的百分比或模式命令的枚举值
 *
 * Context: 任务上下文。不等待队列空位或命令执行完成。
 * Return: 入队成功返回 %true，队列未创建、队列已满或烧录暂停期间返回 %false。
 */
bool music_service_command(music_command_t kind, uint32_t value)
{
    command_t cmd = {kind, value};
    if (commands == NULL || programming) return false;
    return xQueueSend(commands, &cmd, 0) == pdPASS;
}

/**
 * stop() - 结束当前解码并关闭播放文件
 * @cancel: %true 表示主动中断，%false 表示已读到文件尾
 *
 * 先收回未完成的预读结果，再关闭文件或复用缓冲区。
 * 解码结束失败时清除初始化标记，下次播放重新初始化芯片。
 *
 * Context: 仅音乐任务调用。可能等待文件任务和解码器。
 * Return: 清理成功返回 %true 并发布就绪状态，失败返回 %false 并发布错误状态。
 */
static bool stop(bool cancel)
{
    bool ok = true;
    /* 先收回文件任务的读结果，再关闭文件或重用缓冲区。 */
    if (read_pending)
    {
        ok = music_storage_read_end(NULL) == FILE_SERVICE_OK;
        read_pending = false;
    }
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

/**
 * music_service_suspend_for_programming() - 请求停播并等待本地烧录许可
 *
 * 音乐任务会清理播放资源并丢弃已排队命令。超时或清理失败时，
 * 暂停标记仍保持置位，调用方结束烧录流程时应调用
 * music_service_resume_after_programming()。
 *
 * Context: 仅烧录任务串行调用。每 5 ms 检查确认标记，约 3 秒超时。
 * Return: 收到停播成功的确认返回 %true，超时或停播失败返回 %false。
 */
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

/**
 * music_service_resume_after_programming() - 解除本地烧录期间的播放限制
 *
 * 解除后可提交新命令。先前的曲目不会自动恢复。
 *
 * Context: 由烧录任务在流程退出时调用，不等待音乐任务。
 */
void music_service_resume_after_programming(void) { programming = false; }

/**
 * music_entry() - 处理播放命令、音频供数和状态发布
 * @argument: FreeRTOS 任务参数，未使用
 *
 * 音乐任务负责播放状态和 VS1053 访问，FatFS 由文件任务操作。
 * 播放时交替使用两块缓冲区，只有在取回预读结果后才切换到下一块。
 *
 * Context: FreeRTOS 音乐任务入口，可等待命令、文件结果或 DREQ，不返回。
 */
static void music_entry(void *argument)
{
    (void)argument;
    size_t amount = 0;
    size_t position = 0;
    TickType_t last_ready = 0;
    TickType_t last_clock = 0;
    TickType_t last_send = 0;
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
                /* 暂停期间只更新用户音量，保持解码器输出静音。 */
                if (initialized && !bsp_vs1053_volume(state.state == MUSIC_PAUSED ? 0 : state.volume))
                    fail("音量设置失败");
            }
            else if (cmd.kind == MUSIC_MODE && cmd.value <= MUSIC_PLAY_MODE_SHUFFLE)
                state.mode = (uint8_t)cmd.value;
            else if (cmd.kind == MUSIC_STOP) stop(true);
            else if (cmd.kind == MUSIC_TOGGLE && file_open)
            {
                bool pause = state.state == MUSIC_PLAYING;
                /* 停止供数不会关闭DAC输出；暂停静音，继续时恢复用户音量。 */
                if (!bsp_vs1053_volume(pause ? 0 : state.volume))
                { fail("音量设置失败"); continue; }
                state.state = pause ? MUSIC_PAUSED : MUSIC_PLAYING;
                last_ready = xTaskGetTickCount();
                last_send = last_ready;
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
                stream_index = 0;
                if (!bsp_vs1053_volume(state.volume)) { fail("音量设置失败"); continue; }
                strcpy(state.message, "正在播放");
                last_ready = last_clock = xTaskGetTickCount();
                last_send = last_ready;
                playback_timing.bytes_sent = 0;
                playback_timing.max_feed_gap = 0;
                playback_timing.max_read_wait = 0;
                playback_timing.max_clock_wait = 0;
                publish();
            }
            publish();
        }
        if (state.state != MUSIC_PLAYING) continue;
        if (position == amount)
        {
            TickType_t read_start = xTaskGetTickCount();
            file_service_status_t read_status;
            if (read_pending)
            {
                read_status = music_storage_read_end(&amount);
                read_pending = false;
                stream_index ^= 1U;
            }
            else
                read_status = music_storage_request(MUSIC_STORAGE_READ, NULL, stream_buffer[stream_index],
                                                   sizeof(stream_buffer[0]), &amount);
            if (read_status != FILE_SERVICE_OK)
            { fail("音乐读取失败，请检查SD卡"); continue; }
            TickType_t read_wait = xTaskGetTickCount() - read_start;
            if (playback_timing.bytes_sent != 0 && read_wait > playback_timing.max_read_wait)
                playback_timing.max_read_wait = read_wait;
            position = 0;
            last_ready = xTaskGetTickCount();
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
            read_pending = music_storage_read_begin(stream_buffer[stream_index ^ 1U],
                                                    sizeof(stream_buffer[0]));
            if (!read_pending) { fail("音乐预读请求失败"); continue; }
        }
        if (bsp_vs1053_ready())
        {
            uint16_t bytes = amount - position > 32U ? 32U : (uint16_t)(amount - position);
            if (!bsp_vs1053_send(stream_buffer[stream_index] + position, bytes)) { fail("SPI音频传输失败"); continue; }
            position += bytes;
            last_ready = xTaskGetTickCount();
            TickType_t gap = last_ready - last_send;
            if (playback_timing.bytes_sent != 0 && gap > playback_timing.max_feed_gap)
                playback_timing.max_feed_gap = gap;
            playback_timing.bytes_sent += bytes;
            last_send = last_ready;
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
            TickType_t clock_start = xTaskGetTickCount();
            if (!bsp_vs1053_seconds(&seconds)) { fail("解码时间读取失败"); continue; }
            TickType_t clock_wait = xTaskGetTickCount() - clock_start;
            if (clock_wait > playback_timing.max_clock_wait) playback_timing.max_clock_wait = clock_wait;
            state.seconds = seconds;
            last_clock = xTaskGetTickCount();
            publish();
        }
    }
}

/**
 * music_service_create() - 建立音乐任务和所需队列
 *
 * Context: 在系统初始化阶段调用一次。
 * Return: 命令队列、存储和频谱服务及音乐任务均创建成功时返回 %true，
 * 任一创建失败返回 %false。
 */
bool music_service_create(void)
{
    commands = xQueueCreate(8, sizeof(command_t));
    return commands != NULL && music_storage_create() && audio_spectrum_create() &&
        xTaskCreateStatic(music_entry, "music", 1536, NULL, 4, stack, &task_control) != NULL;
}
