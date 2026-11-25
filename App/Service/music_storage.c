#include "music_storage.h"
#include "file_service_port.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "ff.h"
#include <string.h>
#include <ctype.h>

typedef struct
{
    music_storage_op_t op;
    char path[FILE_SERVICE_PATH_CAP];
    void *buffer;
    size_t capacity;
} storage_request_t;
typedef struct { file_service_status_t status; size_t amount; } storage_result_t;
static QueueHandle_t requests;
static QueueHandle_t results;
static MUSIC_RAM FIL audio_file;
static MUSIC_RAM DIR scan_directory;
static MUSIC_RAM FILINFO scan_info;
static MUSIC_RAM storage_request_t work;
static bool opened;
static MUSIC_RAM uint8_t metadata[4096];
/* 定位元数据、WAV 头和文件句柄均由文件任务独占。 */
static struct
{
    uint32_t start, length, bytes_per_second, duration;
    uint8_t pcm_format[16];
    unsigned pcm_align;
} seek_info;
static uint8_t wave_header[44];
static unsigned wave_header_left;
static bool wave_segment;

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* WAV 头中的长度字段采用小端编码，p 须至少有 4 字节空间。 */
static void write_le32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (8U * i));
}

static FRESULT read_at(uint32_t offset, void *buffer, UINT size, UINT *amount)
{
    FRESULT fr = f_lseek(&audio_file, offset);
    return fr == FR_OK ? f_read(&audio_file, buffer, size, amount) : fr;
}

typedef struct
{
    uint32_t rate, bitrate, bytes, samples;
    uint8_t version, mono, crc;
} mp3_frame_t;

static bool mp3_frame(const uint8_t *p, mp3_frame_t *frame)
{
    unsigned version = (p[1] >> 3) & 3U;
    unsigned bitrate = p[2] >> 4;
    unsigned rate = (p[2] >> 2) & 3U;
    if (p[0] != 0xFF || (p[1] & 0xE6) != 0xE2 || version == 1 ||
        bitrate == 0 || bitrate == 15 || rate == 3) return false;
    static const uint16_t rates[] = {44100, 48000, 32000};
    static const uint16_t bitrates[2][14] = {
        {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
        {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}
    };
    frame->version = version;
    frame->rate = rates[rate] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
    frame->bitrate = bitrates[version == 3 ? 0 : 1][bitrate - 1] * 1000U;
    frame->samples = version == 3 ? 1152 : 576;
    frame->bytes = (version == 3 ? 144U : 72U) * frame->bitrate / frame->rate + ((p[2] >> 1) & 1U);
    frame->mono = (p[3] >> 6) == 3;
    frame->crc = (p[1] & 1U) == 0;
    return true;
}

/**
 * read_duration() - 探测播放时长并建立定位元数据
 * @duration: 非空输出，接收秒数、时长估算标志和定位支持标志
 *
 * 只读取 WAV 区块、MP3 头部窗口和必要的尾部标签。
 * 线性 PCM WAV 可保存数据区及帧对齐信息，MP3 保存估算定位所需的信息。
 * 函数会改变文件位置，播放前由打开流程将文件定位回起点。
 *
 * Context: 仅文件任务调用，要求音频文件已打开，可能阻塞于文件读取。
 * Return: 探测结束返回 %FR_OK，无法识别时秒数为 0。读取或定位失败时返回 FatFS 错误。
 */
static FRESULT read_duration(music_duration_t *duration)
{
    uint8_t header[24];
    UINT amount;
    uint32_t size = (uint32_t)f_size(&audio_file);
    *duration = (music_duration_t){0};
    memset(&seek_info, 0, sizeof(seek_info));
    FRESULT fr = read_at(0, header, 12, &amount);
    if (fr != FR_OK || amount != 12) return fr;
    if (!memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVE", 4))
    {
        uint32_t rate = 0, data = 0, data_start = 0, offset = 12;
        for (unsigned chunks = 0; chunks < 64 && offset <= size - 8U; chunks++)
        {
            fr = read_at(offset, header, 8, &amount);
            if (fr != FR_OK || amount != 8) return fr;
            uint32_t bytes = read_le32(header + 4);
            if (bytes > size - offset - 8U) break;
            if (!memcmp(header, "fmt ", 4) && bytes >= 16)
            {
                fr = read_at(offset + 8U, header, 16, &amount);
                if (fr != FR_OK || amount != 16) return fr;
                if ((header[0] == 1 || header[0] == 3) && header[1] == 0) rate = read_le32(header + 8);
                unsigned channels = header[2];
                unsigned bits = header[14];
                unsigned align = channels * bits / 8U;
                if (header[0] == 1 && header[1] == 0 && header[3] == 0 &&
                    (channels == 1 || channels == 2) && header[15] == 0 &&
                    (bits == 8 || bits == 16) && header[12] == align && header[13] == 0 &&
                    rate != 0 && (uint64_t)read_le32(header + 4) * align == rate)
                {
                    memcpy(seek_info.pcm_format, header, sizeof(seek_info.pcm_format));
                    seek_info.pcm_align = align;
                }
                else seek_info.pcm_align = 0;
            }
            else if (!memcmp(header, "data", 4)) { data = bytes; data_start = offset + 8U; }
            if (rate && data)
            {
                duration->seconds = (uint32_t)(((uint64_t)data + rate / 2U) / rate);
                if (seek_info.pcm_align != 0 && data >= seek_info.pcm_align)
                {
                    seek_info.start = data_start;
                    seek_info.length = data - data % seek_info.pcm_align;
                    seek_info.bytes_per_second = rate;
                    seek_info.duration = duration->seconds;
                    duration->seekable = duration->seconds != 0;
                }
                return FR_OK;
            }
            uint64_t next = (uint64_t)offset + 8U + bytes + (bytes & 1U);
            if (next > size) break;
            offset = (uint32_t)next;
        }
        return FR_OK;
    }
    uint32_t start = 0;
    if (!memcmp(header, "ID3", 3))
    {
        if ((header[6] | header[7] | header[8] | header[9]) & 0x80) return FR_OK;
        start = 10U + ((uint32_t)header[6] << 21) + ((uint32_t)header[7] << 14) +
                ((uint32_t)header[8] << 7) + header[9];
        if (header[3] == 4 && (header[5] & 0x10)) start += 10U;
    }
    if (start >= size) return FR_OK;
    fr = read_at(start, metadata, sizeof(metadata), &amount);
    if (fr != FR_OK) return fr;
    for (uint32_t i = 0; i + 4U <= amount; i++)
    {
        mp3_frame_t frame, next;
        if (!mp3_frame(metadata + i, &frame)) continue;
        if (frame.bytes > amount - i || amount - i - frame.bytes < 4U ||
            !mp3_frame(metadata + i + frame.bytes, &next) || next.rate != frame.rate ||
            next.version != frame.version) continue;
        uint32_t xing = 4U + (frame.crc ? 2U : 0U) +
                        (frame.version == 3 ? (frame.mono ? 17U : 32U) : (frame.mono ? 9U : 17U));
        uint32_t frames = 0;
        bool indexed = false;
        if (xing + 12U <= frame.bytes &&
            (!memcmp(metadata + i + xing, "Xing", 4) || !memcmp(metadata + i + xing, "Info", 4)))
        {
            indexed = true;
            if (read_be32(metadata + i + xing + 4U) & 1U)
                frames = read_be32(metadata + i + xing + 8U);
        }
        else if (frame.bytes >= 54U && !memcmp(metadata + i + 36U, "VBRI", 4))
        {
            indexed = true;
            frames = read_be32(metadata + i + 50U);
        }
        if (indexed)
        {
            /* 帧数不能超过文件按最短有效帧所能容纳的上限。 */
            if (frames <= (size - start - i) / 24U)
                duration->seconds = (uint32_t)(((uint64_t)frames * frame.samples + frame.rate / 2U) / frame.rate);
            if (duration->seconds != 0)
            {
                seek_info.start = start + i;
                seek_info.length = size - seek_info.start;
                seek_info.duration = duration->seconds;
                duration->seekable = true;
            }
            return FR_OK;
        }
        /* 头部码率已变化却没有帧数表时，不用CBR公式伪造VBR总时长。 */
        for (uint32_t at = i; at + 4U <= amount; at += next.bytes)
        {
            if (!mp3_frame(metadata + at, &next) || next.rate != frame.rate ||
                next.version != frame.version || next.bitrate != frame.bitrate) return FR_OK;
            if (next.bytes > amount - at) break;
        }
        uint32_t end = size;
        if (size >= 128U)
        {
            fr = read_at(size - 128U, header, 3, &amount);
            if (fr != FR_OK) return fr;
            if (amount == 3 && !memcmp(header, "TAG", 3)) end -= 128U;
        }
        if (end > start + i)
        {
            duration->seconds = (uint32_t)(((uint64_t)(end - start - i) * 8U + frame.bitrate / 2U) / frame.bitrate);
            duration->estimated = duration->seconds != 0;
            seek_info.start = start + i;
            seek_info.length = end - seek_info.start;
            seek_info.bytes_per_second = frame.bitrate / 8U;
            seek_info.duration = duration->seconds;
            duration->seekable = duration->seconds != 0;
        }
        return FR_OK;
    }
    return FR_OK;
}

/**
 * seek_audio() - 移动当前音乐文件的读位置
 * @seconds: 目标整秒数，须小于探测到的总时长
 *
 * PCM WAV 对齐到采样帧，并准备剩余数据的文件头供下一次读取。
 * MP3 使用码率或文件字节比例估算位置，交由解码器重新同步帧。
 *
 * Context: 仅文件任务调用，要求文件已打开，可能阻塞于文件定位。
 * Return: 定位成功返回 %FR_OK，文件未打开或位置不可用返回
 * %FR_INVALID_PARAMETER，FatFS 定位失败返回其错误。
 */
static FRESULT seek_audio(uint32_t seconds)
{
    if (!opened || seek_info.duration == 0 || seconds >= seek_info.duration)
        return FR_INVALID_PARAMETER;
    uint64_t offset = seek_info.bytes_per_second ? (uint64_t)seconds * seek_info.bytes_per_second :
                      (uint64_t)seek_info.length * seconds / seek_info.duration;
    if (offset >= seek_info.length) return FR_INVALID_PARAMETER;

    unsigned align = seek_info.pcm_align;
    if (align != 0) offset -= offset % align;
    FRESULT fr = f_lseek(&audio_file, seek_info.start + (uint32_t)offset);
    if (fr != FR_OK) return fr;
    wave_segment = align != 0;
    wave_header_left = 0;
    if (wave_segment)
    {
        uint32_t remaining = seek_info.length - (uint32_t)offset;
        memcpy(wave_header, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0", 20);
        memcpy(wave_header + 20, seek_info.pcm_format, 16);
        memcpy(wave_header + 36, "data", 4);
        write_le32(wave_header + 4, 36U + remaining + (remaining & 1U));
        write_le32(wave_header + 40, remaining);
        wave_header_left = sizeof(wave_header);
    }
    return FR_OK;
}

/**
 * read_audio() - 读取下一块送给解码器的字节
 * @buffer: 非空目标缓冲区
 * @capacity: 目标缓冲区容量，单位为字节
 * @amount: 非空输出，实际字节数包含已复制的 WAV 头
 *
 * WAV 定位后先输出合成的文件头，可分多次读取；后续读取止于
 * PCM 数据区末尾。其他情况直接读取原文件字节。
 *
 * Context: 仅文件任务调用，要求文件已打开，可能阻塞于文件读取。
 * Return: 读取成功或到达文件尾返回 %FR_OK，否则返回 FatFS 错误。
 */
static FRESULT read_audio(uint8_t *buffer, UINT capacity, UINT *amount)
{
    UINT header_bytes = capacity < wave_header_left ? capacity : wave_header_left;
    if (header_bytes != 0)
    {
        memcpy(buffer, wave_header + sizeof(wave_header) - wave_header_left, header_bytes);
        wave_header_left -= header_bytes;
    }
    UINT bytes = capacity - header_bytes;
    if (wave_segment)
    {
        uint32_t remaining = seek_info.start + seek_info.length - f_tell(&audio_file);
        if (bytes > remaining) bytes = remaining;
    }
    UINT transferred = 0;
    FRESULT fr = bytes ? f_read(&audio_file, buffer + header_bytes, bytes, &transferred) : FR_OK;
    *amount = header_bytes + transferred;
    return fr;
}

static bool supported(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (ext == NULL || strlen(ext) != 4U) return false;
    return (tolower((unsigned char)ext[1]) == 'm' &&
            tolower((unsigned char)ext[2]) == 'p' && ext[3] == '3') ||
           (tolower((unsigned char)ext[1]) == 'w' &&
            tolower((unsigned char)ext[2]) == 'a' && tolower((unsigned char)ext[3]) == 'v');
}

/**
 * music_storage_create() - 创建文件请求及结果队列
 *
 * Context: 系统初始化阶段调用一次，先于音乐任务使用存储接口。
 * Return: 两个队列均创建成功返回 %true，否则返回 %false。
 */
bool music_storage_create(void)
{
    requests = xQueueCreate(1, sizeof(storage_request_t));
    results = xQueueCreate(1, sizeof(storage_result_t));
    return requests != NULL && results != NULL;
}

/**
 * music_storage_request() - 提交文件操作并等待文件任务返回
 * @op: 扫描、打开、读取、关闭或定位操作
 * @path: 打开时为文件路径，其他操作可传 %NULL，扫描使用 %MUSIC_DIRECTORY
 * @buffer: 扫描输出曲目数组，读取输出解码流字节，打开可输出 music_duration_t，
 * 定位时指向 uint32_t 目标秒数，关闭时传 %NULL
 * @capacity: 扫描时为数组元素数，读取时为字节数（最多 32768），
 * 打开或定位时为对应参数的字节数，关闭时为 0
 * @amount: 可选输出，扫描时为曲目数，读取时为实际字节数
 *
 * 请求读取非零字节时，成功返回 0 字节表示已到文件尾。
 *
 * Context: 仅音乐任务调用，可能阻塞。提交前须收回异步读结果，
 * 且 @buffer 必须保持有效直到函数返回。
 * Return: 文件操作结果，或队列未就绪、路径无效、队列操作失败对应的状态。
 */
file_service_status_t music_storage_request(music_storage_op_t op, const char *path,
                                           void *buffer, size_t capacity, size_t *amount)
{
    storage_request_t request = {0};
    storage_result_t result;
    if (requests == NULL || results == NULL) return FILE_SERVICE_NOT_READY;
    if (path != NULL && !file_service_path_valid(path)) return FILE_SERVICE_INVALID_PATH;
    request.op = op;
    request.buffer = buffer;
    request.capacity = capacity;
    if (path != NULL) strcpy(request.path, path);
    /* 入队后不超时返回，文件任务仍可能持有buffer；避免异步写入已释放的内存。 */
    if (xQueueSend(requests, &request, portMAX_DELAY) != pdPASS ||
        xQueueReceive(results, &result, portMAX_DELAY) != pdPASS) return FILE_SERVICE_IO_ERROR;
    if (amount != NULL) *amount = result.amount;
    return result.status;
}

/**
 * music_storage_active() - 查询音乐文件的打开标记
 *
 * Context: 仅文件任务调用，与音频文件句柄由同一任务管理。
 * Return: 文件处于打开状态返回 %true，否则返回 %false。
 */
bool music_storage_active(void)
{
    return opened; /* 仅文件任务调用，与FIL处于同一所有者。 */
}

/**
 * music_storage_read_begin() - 提交下一块音频的异步读请求
 * @buffer: 非空目标缓冲区
 * @capacity: 目标缓冲区字节数，范围为 1 到 32768
 *
 * WAV 定位后的读结果可包含合成文件头。入队后须保持 @buffer 有效且不得复用，
 * 直到调用 music_storage_read_end()
 * 取回结果。发起下一项文件操作或关闭文件前，也必须取回该结果。
 *
 * Context: 仅音乐任务调用，要求文件已打开且没有未收回的请求。不等待队列或读取。
 * Return: 成功入队返回 %true，参数或队列检查失败返回 %false。
 */
bool music_storage_read_begin(void *buffer, size_t capacity)
{
    if (requests == NULL || results == NULL || buffer == NULL || capacity == 0 || capacity > 32768U)
        return false;
    storage_request_t request = {0};
    request.op = MUSIC_STORAGE_READ;
    request.buffer = buffer;
    request.capacity = capacity;
    return xQueueSend(requests, &request, 0) == pdPASS;
}

/**
 * music_storage_read_end() - 收回已提交的异步读请求结果
 * @amount: 可选输出，接收实际输出字节数（含可能的 WAV 头），成功时为 0 表示文件尾
 *
 * Context: 仅音乐任务在成功提交异步读后调用，每个请求调用一次。
 * 可能阻塞至文件任务发布结果。
 * Return: 异步读取结果，或接收失败时返回 %FILE_SERVICE_IO_ERROR。
 */
file_service_status_t music_storage_read_end(size_t *amount)
{
    storage_result_t result;
    if (results == NULL || xQueueReceive(results, &result, portMAX_DELAY) != pdPASS)
        return FILE_SERVICE_IO_ERROR;
    if (amount != NULL) *amount = result.amount;
    return result.status;
}

/**
 * music_storage_poll() - 在文件任务中处理至多一项音乐存储请求
 *
 * 串行访问 FatFS 句柄和工作区。处理完成后通过结果队列交还状态和数据长度。
 *
 * Context: 仅文件任务及其让出点调用。不等待新请求，处理已有请求时可能阻塞。
 */
void music_storage_poll(void)
{
    storage_result_t result = {FILE_SERVICE_OK, 0};
    FRESULT fr = FR_OK;
    if (requests == NULL || xQueueReceive(requests, &work, 0) != pdPASS) return;
    if (work.op == MUSIC_STORAGE_SCAN || work.op == MUSIC_STORAGE_OPEN)
        result.status = file_port_mount();
    if (result.status == FILE_SERVICE_OK)
    {
        switch (work.op)
        {
            case MUSIC_STORAGE_SCAN:
            {
                music_track_t *tracks = work.buffer;
                if (tracks == NULL || work.capacity == 0U) { fr = FR_INVALID_PARAMETER; break; }
                fr = f_opendir(&scan_directory, MUSIC_DIRECTORY);
                if (fr != FR_OK) break;
                while (result.amount < work.capacity)
                {
                    fr = f_readdir(&scan_directory, &scan_info);
                    if (fr != FR_OK || scan_info.fname[0] == '\0') break;
                    if ((scan_info.fattrib & (AM_DIR | AM_HID | AM_SYS)) || !supported(scan_info.fname)) continue;
                    /* 只接受完整可显示且路径可保存的名称，不截断后打开错误文件。 */
                    if (strlen(scan_info.fname) >= MUSIC_NAME_CAP) continue;
                    music_track_t *track = &tracks[result.amount];
                    if (file_service_join_path(MUSIC_DIRECTORY, scan_info.fname, track->path,
                                               sizeof(track->path)) != FILE_SERVICE_OK) continue;
                    strcpy(track->name, scan_info.fname);
                    result.amount++;
                }
                FRESULT close_result = f_closedir(&scan_directory);
                if (fr == FR_OK) fr = close_result;
                break;
            }
            case MUSIC_STORAGE_OPEN:
                if (opened) { fr = FR_LOCKED; break; }
                memset(&seek_info, 0, sizeof(seek_info));
                wave_segment = false;
                wave_header_left = 0;
                fr = f_open(&audio_file, work.path, FA_READ);
                opened = fr == FR_OK;
                if (opened && work.buffer != NULL && work.capacity == sizeof(music_duration_t))
                {
                    fr = read_duration(work.buffer);
                    /* 探测时长后必须回到文件起点，不能把ID3/音频头从解码数据中丢掉。 */
                    FRESULT rewind = f_lseek(&audio_file, 0);
                    if (fr == FR_OK) fr = rewind;
                    if (fr != FR_OK)
                    {
                        (void)f_close(&audio_file);
                        opened = false;
                    }
                }
                break;
            case MUSIC_STORAGE_READ:
            {
                UINT transferred = 0;
                if (!opened || work.buffer == NULL || work.capacity > 32768U) { fr = FR_INVALID_OBJECT; break; }
                fr = read_audio(work.buffer, (UINT)work.capacity, &transferred);
                result.amount = transferred;
                break;
            }
            case MUSIC_STORAGE_CLOSE:
                if (opened) fr = f_close(&audio_file);
                opened = false;
                break;
            case MUSIC_STORAGE_SEEK:
                if (work.buffer == NULL || work.capacity != sizeof(uint32_t))
                    fr = FR_INVALID_PARAMETER;
                else fr = seek_audio(*(const uint32_t *)work.buffer);
                break;
        }
    }
    if (fr != FR_OK) result.status = fr == FR_NOT_READY ? FILE_SERVICE_NOT_READY : FILE_SERVICE_IO_ERROR;
    (void)xQueueSend(results, &result, portMAX_DELAY);
}
