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

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
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

/* 只读取文件头和少量尾部数据，避免为获取时长阻塞文件任务全盘扫描。 */
static FRESULT read_duration(music_duration_t *duration)
{
    uint8_t header[24];
    UINT amount;
    uint32_t size = (uint32_t)f_size(&audio_file);
    *duration = (music_duration_t){0};
    FRESULT fr = read_at(0, header, 12, &amount);
    if (fr != FR_OK || amount != 12) return fr;
    if (!memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVE", 4))
    {
        uint32_t rate = 0, data = 0, offset = 12;
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
            }
            else if (!memcmp(header, "data", 4)) data = bytes;
            if (rate && data)
            {
                duration->seconds = (uint32_t)(((uint64_t)data + rate / 2U) / rate);
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
        }
        return FR_OK;
    }
    return FR_OK;
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

bool music_storage_create(void)
{
    requests = xQueueCreate(1, sizeof(storage_request_t));
    results = xQueueCreate(1, sizeof(storage_result_t));
    return requests != NULL && results != NULL;
}

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

bool music_storage_active(void)
{
    return opened; /* 仅文件任务调用，与FIL处于同一所有者。 */
}

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
                fr = f_read(&audio_file, work.buffer, (UINT)work.capacity, &transferred);
                result.amount = transferred;
                break;
            }
            case MUSIC_STORAGE_CLOSE:
                if (opened) fr = f_close(&audio_file);
                opened = false;
                break;
        }
    }
    if (fr != FR_OK) result.status = fr == FR_NOT_READY ? FILE_SERVICE_NOT_READY : FILE_SERVICE_IO_ERROR;
    (void)xQueueSend(results, &result, portMAX_DELAY);
}
