#ifndef FIRMWARE_READER_H
#define FIRMWARE_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 单个编程页上限；超出时直接拒绝，避免目标 RAM 和本地缓冲区失控。 */
#define FIRMWARE_READER_MAX_PAGE_SIZE (8U * 1024U)
#define FIRMWARE_READER_MAX_PAGES     262144U
#define FIRMWARE_READER_MAX_MAP_SIZE  ((FIRMWARE_READER_MAX_PAGES + 7U) / 8U)

typedef enum
{
    FIRMWARE_READER_OK = 0,
    FIRMWARE_READER_FILE_ERROR,
    FIRMWARE_READER_FORMAT_ERROR,
    FIRMWARE_READER_CHECKSUM_ERROR,
    FIRMWARE_READER_RANGE_ERROR,
    FIRMWARE_READER_EMPTY,
    FIRMWARE_READER_CANCELLED
} firmware_reader_status_t;

/* 固件扫描结果，不包含固件数据本体。 */
typedef struct
{
    uint32_t flash_start;
    uint32_t flash_size;
    uint32_t page_size;
    uint32_t min_address;
    uint32_t max_address;
    uint32_t data_bytes;
    uint32_t page_count;
    uint32_t data_pages;
    uint32_t touched_bytes;
} firmware_meta_t;

/* 每个编程页回调；返回 false 可中止流式处理。 */
typedef bool (*firmware_page_emit_t)(uint32_t address, const uint8_t *data,
                                     uint32_t size, void *user);

firmware_reader_status_t firmware_scan(const char *path,
                                       uint32_t flash_start, uint32_t flash_size,
                                       uint32_t page_size,
                                       firmware_meta_t *meta,
                                       uint8_t *touched, size_t touched_capacity);

firmware_reader_status_t firmware_stream(const char *path,
                                         uint32_t flash_start, uint32_t flash_size,
                                         uint32_t page_size, uint8_t fill_byte,
                                         firmware_page_emit_t emit, void *user);

const char *firmware_reader_status_text(firmware_reader_status_t status);

#endif /* FIRMWARE_READER_H */
