#ifndef FIRMWARE_READER_H
#define FIRMWARE_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "target_profile.h"

#define FIRMWARE_READER_MAX_SIZE (64U * 1024U)
#define FIRMWARE_READER_PAGE_SIZE 1024U
#define FIRMWARE_READER_PAGE_COUNT (FIRMWARE_READER_MAX_SIZE / FIRMWARE_READER_PAGE_SIZE)

typedef enum
{
    FIRMWARE_READER_OK = 0,
    FIRMWARE_READER_FILE_ERROR,
    FIRMWARE_READER_FORMAT_ERROR,
    FIRMWARE_READER_CHECKSUM_ERROR,
    FIRMWARE_READER_RANGE_ERROR,
    FIRMWARE_READER_EMPTY
} firmware_reader_status_t;

typedef struct
{
    const uint8_t *data;
    uint32_t base_address;
    uint32_t min_address;
    uint32_t max_address;
    uint32_t data_bytes;
    uint64_t touched_pages;
} firmware_image_t;

firmware_reader_status_t firmware_reader_load(const char *path,
                                              const target_profile_t *profile,
                                              firmware_image_t *image);
const char *firmware_reader_status_text(firmware_reader_status_t status);

#endif /* FIRMWARE_READER_H */
