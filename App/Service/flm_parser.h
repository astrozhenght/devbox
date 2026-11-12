#ifndef FLM_PARSER_H
#define FLM_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#define FLM_MAX_FILE_SIZE       (64U * 1024U)
#define FLM_MAX_LOAD_SECTIONS   8U
#define FLM_MAX_SECTOR_REGIONS  32U

typedef enum
{
    FLM_PARSER_OK = 0,
    FLM_PARSER_FILE_ERROR,
    FLM_PARSER_TOO_LARGE,
    FLM_PARSER_BAD_ELF,
    FLM_PARSER_UNSUPPORTED,
    FLM_PARSER_MISSING_SYMBOL,
    FLM_PARSER_BAD_DEVICE
} flm_parser_status_t;

typedef struct
{
    uint32_t address;
    uint32_t size;
    uint32_t file_offset;
    uint32_t type;
    uint32_t flags;
} flm_load_section_t;

typedef struct
{
    uint32_t size;
    uint32_t address;
} flm_sector_region_t;

typedef struct
{
    const uint8_t *file_data;
    uint32_t file_size;
    flm_load_section_t sections[FLM_MAX_LOAD_SECTIONS];
    uint32_t section_count;
    uint32_t image_base;
    uint32_t image_size;
    uint32_t static_base_offset;
    uint32_t init;
    uint32_t uninit;
    uint32_t erase_sector;
    uint32_t program_page;
    uint32_t erase_chip;
    uint32_t blank_check;
    uint32_t verify;
    bool has_erase_chip;
    bool has_blank_check;
    bool has_verify;
    char device_name[129];
    uint16_t device_type;
    uint32_t flash_start;
    uint32_t flash_size;
    uint32_t page_size;
    uint8_t erased_value;
    uint32_t program_timeout_ms;
    uint32_t erase_timeout_ms;
    flm_sector_region_t sector_regions[FLM_MAX_SECTOR_REGIONS];
    uint32_t sector_region_count;
} flm_algorithm_t;

flm_parser_status_t flm_parser_load(const char *path, flm_algorithm_t *algorithm);
uint32_t flm_parser_sector_size(const flm_algorithm_t *algorithm, uint32_t address);
const char *flm_parser_status_text(flm_parser_status_t status);

#endif /* FLM_PARSER_H */
