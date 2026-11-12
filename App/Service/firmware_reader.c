#include "firmware_reader.h"

#include <ctype.h>
#include <string.h>

#include "FreeRTOS.h"
#include "file_task.h"

#define FIRMWARE_IO_CHUNK       512U
#define FIRMWARE_HEX_LINE_CAP   600U
#define FIRMWARE_FILE_TIMEOUT   pdMS_TO_TICKS(5000U)

static uint8_t firmware_data[FIRMWARE_READER_MAX_SIZE]
    __attribute__((section(".axisram"), aligned(32)));
static char hex_line[FIRMWARE_HEX_LINE_CAP] __attribute__((section(".axisram")));

static bool has_suffix(const char *path, const char *suffix)
{
    size_t path_length;
    size_t suffix_length;
    size_t index;

    if ((path == NULL) || (suffix == NULL)) return false;
    path_length = strlen(path);
    suffix_length = strlen(suffix);
    if (path_length < suffix_length) return false;
    for (index = 0U; index < suffix_length; index++)
    {
        if (tolower((unsigned char)path[path_length - suffix_length + index]) !=
            tolower((unsigned char)suffix[index])) return false;
    }
    return true;
}

static int hex_nibble(char value)
{
    if ((value >= '0') && (value <= '9')) return value - '0';
    if ((value >= 'A') && (value <= 'F')) return value - 'A' + 10;
    if ((value >= 'a') && (value <= 'f')) return value - 'a' + 10;
    return -1;
}

static bool hex_byte(const char *text, uint8_t *value)
{
    int high = hex_nibble(text[0]);
    int low = hex_nibble(text[1]);

    if ((high < 0) || (low < 0)) return false;
    *value = (uint8_t)((high << 4) | low);
    return true;
}

static firmware_reader_status_t store_data(const target_profile_t *profile,
                                           firmware_image_t *image,
                                           uint32_t address,
                                           const uint8_t *data,
                                           uint32_t size)
{
    uint32_t offset;
    uint32_t end;
    uint32_t first_page;
    uint32_t last_page;
    uint32_t page;

    if ((size == 0U) || (address < profile->flash_start))
        return size == 0U ? FIRMWARE_READER_OK : FIRMWARE_READER_RANGE_ERROR;
    if ((address > UINT32_MAX - size) ||
        ((address + size) > (profile->flash_start + profile->flash_size)))
        return FIRMWARE_READER_RANGE_ERROR;
    offset = address - profile->flash_start;
    end = address + size;
    if (image->data_bytes > (UINT32_MAX - size)) return FIRMWARE_READER_RANGE_ERROR;
    memcpy(firmware_data + offset, data, size);
    if (image->data_bytes == 0U)
    {
        image->min_address = address;
        image->max_address = end;
    }
    else
    {
        if (address < image->min_address) image->min_address = address;
        if (end > image->max_address) image->max_address = end;
    }
    image->data_bytes += size;
    first_page = offset / FIRMWARE_READER_PAGE_SIZE;
    last_page = (offset + size - 1U) / FIRMWARE_READER_PAGE_SIZE;
    for (page = first_page; page <= last_page; page++) image->touched_pages |= 1ULL << page;
    return FIRMWARE_READER_OK;
}

static firmware_reader_status_t parse_hex_line(const target_profile_t *profile,
                                               firmware_image_t *image,
                                               const char *line,
                                               size_t length,
                                               uint32_t *upper_address,
                                               bool *eof_seen)
{
    uint8_t record[260];
    uint8_t count;
    uint8_t type;
    uint32_t address;
    uint32_t index;
    uint8_t sum = 0U;

    if ((length < 11U) || (line[0] != ':') || (((length - 1U) & 1U) != 0U))
        return FIRMWARE_READER_FORMAT_ERROR;
    if (!hex_byte(line + 1U, &count) || (length != (size_t)(11U + count * 2U)))
        return FIRMWARE_READER_FORMAT_ERROR;
    for (index = 0U; index < (uint32_t)count + 5U; index++)
    {
        if (!hex_byte(line + 1U + index * 2U, &record[index]))
            return FIRMWARE_READER_FORMAT_ERROR;
        sum = (uint8_t)(sum + record[index]);
    }
    if (sum != 0U) return FIRMWARE_READER_CHECKSUM_ERROR;
    address = ((uint32_t)record[1] << 8) | record[2];
    type = record[3];
    switch (type)
    {
        case 0x00U:
            if (*eof_seen) return FIRMWARE_READER_FORMAT_ERROR;
            return store_data(profile, image, *upper_address + address, record + 4U, count);
        case 0x01U:
            if ((count != 0U) || (address != 0U)) return FIRMWARE_READER_FORMAT_ERROR;
            *eof_seen = true;
            return FIRMWARE_READER_OK;
        case 0x04U:
            if ((count != 2U) || (address != 0U) || *eof_seen)
                return FIRMWARE_READER_FORMAT_ERROR;
            *upper_address = (((uint32_t)record[4] << 8) | record[5]) << 16;
            return FIRMWARE_READER_OK;
        case 0x03U:
        case 0x05U:
            return (*eof_seen || (count != 4U) || (address != 0U)) ?
                   FIRMWARE_READER_FORMAT_ERROR : FIRMWARE_READER_OK;
        default:
            return FIRMWARE_READER_FORMAT_ERROR;
    }
}

static firmware_reader_status_t load_hex(const char *path,
                                         const target_profile_t *profile,
                                         firmware_image_t *image)
{
    uint8_t chunk[FIRMWARE_IO_CHUNK];
    uint32_t file_size;
    uint32_t upper_address = 0U;
    size_t line_length = 0U;
    bool eof_seen = false;
    firmware_reader_status_t result = FIRMWARE_READER_OK;
    file_service_status_t file_status;

    file_status = file_task_stream_open(path, &file_size, FIRMWARE_FILE_TIMEOUT);
    if (file_status != FILE_SERVICE_OK) return FIRMWARE_READER_FILE_ERROR;
    (void)file_size;
    for (;;)
    {
        size_t received = 0U;
        size_t index;
        file_status = file_task_stream_read(chunk, sizeof(chunk), &received,
                                            FIRMWARE_FILE_TIMEOUT);
        if (file_status != FILE_SERVICE_OK)
        {
            result = FIRMWARE_READER_FILE_ERROR;
            break;
        }
        for (index = 0U; index < received; index++)
        {
            char value = (char)chunk[index];
            if ((value == '\r') || (value == '\n'))
            {
                if (line_length != 0U)
                {
                    result = parse_hex_line(profile, image, hex_line, line_length,
                                            &upper_address, &eof_seen);
                    line_length = 0U;
                    if (result != FIRMWARE_READER_OK) break;
                }
            }
            else
            {
                if (line_length >= sizeof(hex_line))
                {
                    result = FIRMWARE_READER_FORMAT_ERROR;
                    break;
                }
                hex_line[line_length++] = value;
            }
        }
        if ((result != FIRMWARE_READER_OK) || (received == 0U)) break;
    }
    if ((result == FIRMWARE_READER_OK) && (line_length != 0U))
        result = parse_hex_line(profile, image, hex_line, line_length,
                                &upper_address, &eof_seen);
    if ((result == FIRMWARE_READER_OK) && !eof_seen) result = FIRMWARE_READER_FORMAT_ERROR;
    if (file_task_stream_close(FIRMWARE_FILE_TIMEOUT) != FILE_SERVICE_OK &&
        result == FIRMWARE_READER_OK) result = FIRMWARE_READER_FILE_ERROR;
    return result;
}

static firmware_reader_status_t load_bin(const char *path,
                                         const target_profile_t *profile,
                                         firmware_image_t *image)
{
    uint32_t file_size;
    size_t received;
    size_t total = 0U;
    file_service_status_t status;

    status = file_task_stream_open(path, &file_size, FIRMWARE_FILE_TIMEOUT);
    if (status != FILE_SERVICE_OK) return FIRMWARE_READER_FILE_ERROR;
    if ((file_size == 0U) || (file_size > profile->flash_size) ||
        (file_size > sizeof(firmware_data)))
    {
        (void)file_task_stream_close(FIRMWARE_FILE_TIMEOUT);
        return file_size == 0U ? FIRMWARE_READER_EMPTY : FIRMWARE_READER_RANGE_ERROR;
    }
    while (total < file_size)
    {
        status = file_task_stream_read(firmware_data + total, file_size - total,
                                       &received, FIRMWARE_FILE_TIMEOUT);
        if ((status != FILE_SERVICE_OK) || (received == 0U))
        {
            (void)file_task_stream_close(FIRMWARE_FILE_TIMEOUT);
            return FIRMWARE_READER_FILE_ERROR;
        }
        total += received;
    }
    status = file_task_stream_close(FIRMWARE_FILE_TIMEOUT);
    if (status != FILE_SERVICE_OK) return FIRMWARE_READER_FILE_ERROR;
    image->min_address = profile->flash_start;
    image->max_address = profile->flash_start + file_size;
    image->data_bytes = file_size;
    {
        uint32_t pages = (file_size + FIRMWARE_READER_PAGE_SIZE - 1U) /
                         FIRMWARE_READER_PAGE_SIZE;
        image->touched_pages = (pages >= 64U) ? UINT64_MAX : ((1ULL << pages) - 1ULL);
    }
    return FIRMWARE_READER_OK;
}

firmware_reader_status_t firmware_reader_load(const char *path,
                                              const target_profile_t *profile,
                                              firmware_image_t *image)
{
    firmware_reader_status_t status;

    if ((path == NULL) || (profile == NULL) || (image == NULL) ||
        (profile->flash_size > sizeof(firmware_data))) return FIRMWARE_READER_RANGE_ERROR;
    memset(firmware_data, 0xFF, profile->flash_size);
    memset(image, 0, sizeof(*image));
    image->data = firmware_data;
    image->base_address = profile->flash_start;
    if (has_suffix(path, ".hex")) status = load_hex(path, profile, image);
    else if (has_suffix(path, ".bin")) status = load_bin(path, profile, image);
    else status = FIRMWARE_READER_FORMAT_ERROR;
    if ((status == FIRMWARE_READER_OK) && (image->data_bytes == 0U))
        status = FIRMWARE_READER_EMPTY;
    return status;
}

const char *firmware_reader_status_text(firmware_reader_status_t status)
{
    switch (status)
    {
        case FIRMWARE_READER_OK: return "固件有效";
        case FIRMWARE_READER_FILE_ERROR: return "固件文件读取失败";
        case FIRMWARE_READER_FORMAT_ERROR: return "固件格式不受支持";
        case FIRMWARE_READER_CHECKSUM_ERROR: return "HEX校验和错误";
        case FIRMWARE_READER_RANGE_ERROR: return "固件地址超出目标范围";
        case FIRMWARE_READER_EMPTY: return "固件没有有效数据";
        default: return "未知固件错误";
    }
}
