#include "firmware_reader.h"

#include <ctype.h>
#include <string.h>

#include "FreeRTOS.h"
#include "file_task.h"

#define FIRMWARE_IO_CHUNK       512U
#define FIRMWARE_HEX_LINE_CAP   600U
#define FIRMWARE_FILE_TIMEOUT   pdMS_TO_TICKS(5000U)

typedef firmware_reader_status_t (*firmware_data_consume_t)(
    uint32_t address, const uint8_t *data, uint32_t size, void *user);

typedef struct
{
    firmware_meta_t *meta;
    uint8_t *touched;
} firmware_scan_context_t;

typedef struct
{
    uint32_t flash_start;
    uint32_t flash_end;
    uint32_t page_size;
    uint8_t fill_byte;
    uint32_t page_index;
    bool page_valid;
    firmware_page_emit_t emit;
    void *user;
    firmware_reader_status_t status;
} firmware_stream_context_t;

static char hex_line[FIRMWARE_HEX_LINE_CAP]
    __attribute__((section(".axisram")));
static uint8_t stream_page[FIRMWARE_READER_MAX_PAGE_SIZE]
    __attribute__((section(".axisram"), aligned(32)));

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

static bool flash_range_valid(uint32_t address, uint32_t size,
                              uint32_t flash_start, uint32_t flash_size)
{
    uint64_t address_end = (uint64_t)address + size;
    uint64_t flash_end = (uint64_t)flash_start + flash_size;

    return (size != 0U) && (address >= flash_start) && (address_end <= flash_end);
}

static bool page_map_test(const uint8_t *map, uint32_t page)
{
    return (map[page / 8U] & (uint8_t)(1U << (page % 8U))) != 0U;
}

static void page_map_set(uint8_t *map, uint32_t page)
{
    map[page / 8U] |= (uint8_t)(1U << (page % 8U));
}

static firmware_reader_status_t scan_data(uint32_t address, const uint8_t *data,
                                          uint32_t size, void *user)
{
    firmware_scan_context_t *context = (firmware_scan_context_t *)user;
    firmware_meta_t *meta;
    uint32_t offset;
    uint32_t first_page;
    uint32_t last_page;
    uint32_t page;

    (void)data;
    if ((context == NULL) || (context->meta == NULL) || (context->touched == NULL))
        return FIRMWARE_READER_RANGE_ERROR;
    meta = context->meta;
    if (!flash_range_valid(address, size, meta->flash_start, meta->flash_size))
        return FIRMWARE_READER_RANGE_ERROR;
    if (meta->data_bytes > UINT32_MAX - size) return FIRMWARE_READER_RANGE_ERROR;

    offset = address - meta->flash_start;
    first_page = offset / meta->page_size;
    last_page = (offset + size - 1U) / meta->page_size;
    /* 与流式写入共用页顺序约束：已离开的页不能返回，擦除前就拒绝。 */
    if ((meta->data_bytes != 0U) &&
        (first_page < (meta->max_address - 1U - meta->flash_start) / meta->page_size))
        return FIRMWARE_READER_FORMAT_ERROR;
    for (page = first_page; page <= last_page; page++)
    {
        if (!page_map_test(context->touched, page))
        {
            page_map_set(context->touched, page);
            meta->data_pages++;
        }
    }
    if (meta->data_bytes == 0U)
    {
        meta->min_address = address;
        meta->max_address = address + size;
    }
    else
    {
        if (address < meta->min_address) meta->min_address = address;
        if ((address + size) > meta->max_address) meta->max_address = address + size;
    }
    meta->data_bytes += size;
    return FIRMWARE_READER_OK;
}

static firmware_reader_status_t parse_hex_line(
    const char *line, size_t length, uint32_t *upper_address, bool *eof_seen,
    firmware_data_consume_t consume, void *user)
{
    uint8_t record[260];
    uint8_t count;
    uint8_t type;
    uint16_t offset_address;
    uint32_t index;
    uint32_t absolute_address;
    uint8_t sum = 0U;

    if ((line == NULL) || (upper_address == NULL) || (eof_seen == NULL) ||
        (consume == NULL) || (length < 11U) || (line[0] != ':') ||
        (((length - 1U) & 1U) != 0U))
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

    offset_address = (uint16_t)(((uint16_t)record[1] << 8) | record[2]);
    type = record[3];
    switch (type)
    {
        case 0x00U:
            if (*eof_seen || (offset_address > UINT32_MAX - *upper_address))
                return FIRMWARE_READER_FORMAT_ERROR;
            absolute_address = *upper_address + offset_address;
            return consume(absolute_address, record + 4U, count, user);

        case 0x01U:
            if ((count != 0U) || (offset_address != 0U))
                return FIRMWARE_READER_FORMAT_ERROR;
            *eof_seen = true;
            return FIRMWARE_READER_OK;

        case 0x02U:
            if ((count != 2U) || (offset_address != 0U) || *eof_seen)
                return FIRMWARE_READER_FORMAT_ERROR;
            *upper_address = (((uint32_t)record[4] << 8) | record[5]) << 4;
            return FIRMWARE_READER_OK;

        case 0x04U:
            if ((count != 2U) || (offset_address != 0U) || *eof_seen)
                return FIRMWARE_READER_FORMAT_ERROR;
            *upper_address = (((uint32_t)record[4] << 8) | record[5]) << 16;
            return FIRMWARE_READER_OK;

        case 0x03U:
        case 0x05U:
            return (*eof_seen || (count != 4U) || (offset_address != 0U)) ?
                   FIRMWARE_READER_FORMAT_ERROR : FIRMWARE_READER_OK;

        default:
            return FIRMWARE_READER_FORMAT_ERROR;
    }
}

static firmware_reader_status_t read_hex(
    const char *path, firmware_data_consume_t consume, void *user)
{
    uint8_t chunk[FIRMWARE_IO_CHUNK];
    uint32_t file_size = 0U;
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
        if (received == 0U) break;
        for (index = 0U; index < received; index++)
        {
            char value = (char)chunk[index];

            if ((value == '\r') || (value == '\n'))
            {
                if (line_length != 0U)
                {
                    result = parse_hex_line(hex_line, line_length, &upper_address,
                                            &eof_seen, consume, user);
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
        if (result != FIRMWARE_READER_OK) break;
    }
    if ((result == FIRMWARE_READER_OK) && (line_length != 0U))
        result = parse_hex_line(hex_line, line_length, &upper_address,
                                &eof_seen, consume, user);
    if ((result == FIRMWARE_READER_OK) && !eof_seen)
        result = FIRMWARE_READER_FORMAT_ERROR;
    if ((file_task_stream_close(FIRMWARE_FILE_TIMEOUT) != FILE_SERVICE_OK) &&
        (result == FIRMWARE_READER_OK))
        result = FIRMWARE_READER_FILE_ERROR;
    return result;
}

static firmware_reader_status_t scan_bin(const char *path, firmware_scan_context_t *context)
{
    uint8_t chunk[FIRMWARE_IO_CHUNK];
    uint32_t file_size = 0U;
    uint32_t offset = 0U;
    firmware_reader_status_t result = FIRMWARE_READER_OK;
    file_service_status_t status;

    status = file_task_stream_open(path, &file_size, FIRMWARE_FILE_TIMEOUT);
    if (status != FILE_SERVICE_OK) return FIRMWARE_READER_FILE_ERROR;
    if ((file_size == 0U) || (file_size > context->meta->flash_size))
        result = (file_size == 0U) ? FIRMWARE_READER_EMPTY :
                 FIRMWARE_READER_RANGE_ERROR;
    while ((result == FIRMWARE_READER_OK) && (offset < file_size))
    {
        size_t received = 0U;
        size_t expected = file_size - offset;

        if (expected > sizeof(chunk)) expected = sizeof(chunk);
        status = file_task_stream_read(chunk, expected, &received,
                                       FIRMWARE_FILE_TIMEOUT);
        if ((status != FILE_SERVICE_OK) || (received == 0U))
        {
            result = FIRMWARE_READER_FILE_ERROR;
            break;
        }
        offset += (uint32_t)received;
    }
    if ((file_task_stream_close(FIRMWARE_FILE_TIMEOUT) != FILE_SERVICE_OK) &&
        (result == FIRMWARE_READER_OK))
        result = FIRMWARE_READER_FILE_ERROR;
    if (result == FIRMWARE_READER_OK)
        result = scan_data(context->meta->flash_start, NULL, file_size, context);
    return result;
}

static firmware_reader_status_t prepare_scan(
    uint32_t flash_start, uint32_t flash_size, uint32_t page_size,
    firmware_meta_t *meta, uint8_t *touched, size_t touched_capacity)
{
    uint32_t page_count;

    if ((meta == NULL) || (touched == NULL) || (flash_size == 0U) ||
        (page_size == 0U) || (page_size > FIRMWARE_READER_MAX_PAGE_SIZE) ||
        (flash_start > UINT32_MAX - flash_size))
        return FIRMWARE_READER_RANGE_ERROR;
    page_count = 1U + (flash_size - 1U) / page_size;
    if (page_count > FIRMWARE_READER_MAX_PAGES)
        return FIRMWARE_READER_RANGE_ERROR;
    memset(meta, 0, sizeof(*meta));
    meta->flash_start = flash_start;
    meta->flash_size = flash_size;
    meta->page_size = page_size;
    meta->page_count = page_count;
    meta->touched_bytes = (page_count + 7U) / 8U;
    if (meta->touched_bytes > touched_capacity) return FIRMWARE_READER_RANGE_ERROR;
    memset(touched, 0, meta->touched_bytes);
    return FIRMWARE_READER_OK;
}

firmware_reader_status_t firmware_scan(const char *path,
                                       uint32_t flash_start, uint32_t flash_size,
                                       uint32_t page_size,
                                       firmware_meta_t *meta,
                                       uint8_t *touched, size_t touched_capacity)
{
    firmware_scan_context_t context;
    firmware_reader_status_t result;

    if (path == NULL) return FIRMWARE_READER_FILE_ERROR;
    result = prepare_scan(flash_start, flash_size, page_size, meta,
                          touched, touched_capacity);
    if (result != FIRMWARE_READER_OK) return result;
    context.meta = meta;
    context.touched = touched;
    if (has_suffix(path, ".hex")) result = read_hex(path, scan_data, &context);
    else if (has_suffix(path, ".bin")) result = scan_bin(path, &context);
    else result = FIRMWARE_READER_FORMAT_ERROR;
    if ((result == FIRMWARE_READER_OK) && (meta->data_bytes == 0U))
        result = FIRMWARE_READER_EMPTY;
    return result;
}

static firmware_reader_status_t stream_flush(firmware_stream_context_t *context)
{
    uint32_t address;

    if (!context->page_valid) return FIRMWARE_READER_OK;
    address = context->flash_start + context->page_index * context->page_size;
    if (!context->emit(address, stream_page, context->page_size, context->user))
        return FIRMWARE_READER_CANCELLED;
    context->page_valid = false;
    return FIRMWARE_READER_OK;
}

static firmware_reader_status_t stream_data(uint32_t address, const uint8_t *data,
                                            uint32_t size, void *user)
{
    firmware_stream_context_t *context = (firmware_stream_context_t *)user;
    uint32_t offset;

    if ((context == NULL) || !flash_range_valid(address, size,
        context->flash_start, context->flash_end - context->flash_start))
        return FIRMWARE_READER_RANGE_ERROR;
    offset = address - context->flash_start;
    while (size != 0U)
    {
        uint32_t page_index = offset / context->page_size;
        uint32_t page_offset = offset % context->page_size;
        uint32_t amount = context->page_size - page_offset;
        firmware_reader_status_t result;

        if (amount > size) amount = size;
        if (!context->page_valid || (page_index != context->page_index))
        {
            if (context->page_valid)
            {
                if (page_index <= context->page_index)
                    return FIRMWARE_READER_FORMAT_ERROR;
                result = stream_flush(context);
                if (result != FIRMWARE_READER_OK) return result;
            }
            context->page_index = page_index;
            context->page_valid = true;
            memset(stream_page, context->fill_byte, context->page_size);
        }
        memcpy(stream_page + page_offset, data, amount);
        data += amount;
        offset += amount;
        size -= amount;
    }
    return FIRMWARE_READER_OK;
}

static firmware_reader_status_t stream_bin(const char *path,
                                           firmware_stream_context_t *context)
{
    uint32_t file_size = 0U;
    uint32_t offset = 0U;
    firmware_reader_status_t result = FIRMWARE_READER_OK;
    file_service_status_t status;

    status = file_task_stream_open(path, &file_size, FIRMWARE_FILE_TIMEOUT);
    if (status != FILE_SERVICE_OK) return FIRMWARE_READER_FILE_ERROR;
    if ((file_size == 0U) || (file_size > (context->flash_end - context->flash_start)))
    {
        (void)file_task_stream_close(FIRMWARE_FILE_TIMEOUT);
        return file_size == 0U ? FIRMWARE_READER_EMPTY : FIRMWARE_READER_RANGE_ERROR;
    }
    while (offset < file_size)
    {
        size_t received = 0U;
        size_t expected = file_size - offset;

        if (expected > context->page_size) expected = context->page_size;
        status = file_task_stream_read(stream_page, expected, &received,
                                       FIRMWARE_FILE_TIMEOUT);
        if ((status != FILE_SERVICE_OK) || (received == 0U))
        {
            result = FIRMWARE_READER_FILE_ERROR;
            break;
        }
        if (received < context->page_size)
            memset(stream_page + received, context->fill_byte,
                   context->page_size - (uint32_t)received);
        if (!context->emit(context->flash_start + offset, stream_page,
                           context->page_size, context->user))
        {
            result = FIRMWARE_READER_CANCELLED;
            break;
        }
        offset += (uint32_t)received;
    }
    if ((file_task_stream_close(FIRMWARE_FILE_TIMEOUT) != FILE_SERVICE_OK) &&
        (result == FIRMWARE_READER_OK))
        result = FIRMWARE_READER_FILE_ERROR;
    return result;
}

firmware_reader_status_t firmware_stream(const char *path,
                                         uint32_t flash_start, uint32_t flash_size,
                                         uint32_t page_size, uint8_t fill_byte,
                                         firmware_page_emit_t emit, void *user)
{
    firmware_stream_context_t context;
    firmware_reader_status_t result;

    if ((path == NULL) || (emit == NULL) || (flash_size == 0U) ||
        (page_size == 0U) || (page_size > FIRMWARE_READER_MAX_PAGE_SIZE) ||
        (flash_start > UINT32_MAX - flash_size))
        return FIRMWARE_READER_RANGE_ERROR;
    memset(&context, 0, sizeof(context));
    context.flash_start = flash_start;
    context.flash_end = flash_start + flash_size;
    context.page_size = page_size;
    context.fill_byte = fill_byte;
    context.emit = emit;
    context.user = user;
    context.status = FIRMWARE_READER_OK;
    if (has_suffix(path, ".hex")) result = read_hex(path, stream_data, &context);
    else if (has_suffix(path, ".bin")) result = stream_bin(path, &context);
    else result = FIRMWARE_READER_FORMAT_ERROR;
    if ((result == FIRMWARE_READER_OK) && context.page_valid)
        result = stream_flush(&context);
    return result;
}

const char *firmware_reader_status_text(firmware_reader_status_t status)
{
    switch (status)
    {
        case FIRMWARE_READER_OK: return "固件有效";
        case FIRMWARE_READER_FILE_ERROR: return "固件文件读取失败";
        case FIRMWARE_READER_FORMAT_ERROR: return "固件格式不受支持";
        case FIRMWARE_READER_CHECKSUM_ERROR: return "HEX校验和错误";
        case FIRMWARE_READER_RANGE_ERROR: return "固件地址超出FLM范围";
        case FIRMWARE_READER_EMPTY: return "固件没有有效数据";
        case FIRMWARE_READER_CANCELLED: return "固件处理已取消";
        default: return "未知固件错误";
    }
}
