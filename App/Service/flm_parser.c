#include "flm_parser.h"

#include <limits.h>
#include <string.h>

#include "FreeRTOS.h"
#include "file_task.h"

#define FLM_FILE_TIMEOUT       pdMS_TO_TICKS(5000U)
#define ELFCLASS32             1U
#define ELFDATA2LSB            1U
#define ET_EXEC                2U
#define EM_ARM                 40U
#define SHT_PROGBITS           1U
#define SHT_SYMTAB             2U
#define SHT_STRTAB             3U
#define SHT_RELA               4U
#define SHT_NOBITS             8U
#define SHT_REL                9U
#define SHF_WRITE              0x1U
#define SHF_ALLOC              0x2U
#define SHF_EXECINSTR          0x4U
#define SHN_UNDEF              0U
#define FLM_DEVICE_HEADER_SIZE 160U

typedef struct __attribute__((packed))
{
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} elf32_header_t;

typedef struct __attribute__((packed))
{
    uint32_t name;
    uint32_t type;
    uint32_t flags;
    uint32_t address;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t alignment;
    uint32_t entry_size;
} elf32_section_t;

typedef struct __attribute__((packed))
{
    uint32_t name;
    uint32_t value;
    uint32_t size;
    uint8_t info;
    uint8_t other;
    uint16_t section_index;
} elf32_symbol_t;

static uint8_t flm_file[FLM_MAX_FILE_SIZE]
    __attribute__((section(".axisram"), aligned(32)));

static bool range_valid(uint32_t offset, uint32_t size, uint32_t limit)
{
    return (offset <= limit) && (size <= (limit - offset));
}

static const char *table_string(const uint8_t *table, uint32_t table_size, uint32_t offset)
{
    if (offset >= table_size) return NULL;
    if (memchr(table + offset, '\0', table_size - offset) == NULL) return NULL;
    return (const char *)(table + offset);
}

static bool section_at(const elf32_header_t *header, uint32_t index,
                       const elf32_section_t **section)
{
    uint32_t offset;

    if ((index >= header->shnum) || (header->shentsize != sizeof(elf32_section_t))) return false;
    if (index > ((UINT32_MAX - header->shoff) / header->shentsize)) return false;
    offset = header->shoff + index * header->shentsize;
    if (!range_valid(offset, sizeof(elf32_section_t), sizeof(flm_file))) return false;
    *section = (const elf32_section_t *)(flm_file + offset);
    return true;
}

static const uint8_t *address_to_file(const elf32_header_t *header,
                                      uint32_t address, uint32_t size)
{
    uint32_t index;

    for (index = 0U; index < header->shnum; index++)
    {
        const elf32_section_t *section;
        uint32_t delta;
        if (!section_at(header, index, &section) || (section->type != SHT_PROGBITS) ||
            (address < section->address)) continue;
        delta = address - section->address;
        if ((delta <= section->size) && (size <= (section->size - delta)) &&
            range_valid(section->offset + delta, size, FLM_MAX_FILE_SIZE))
            return flm_file + section->offset + delta;
    }
    return NULL;
}

static bool find_symbols(const elf32_header_t *header, flm_algorithm_t *algorithm,
                         uint32_t *device_address, uint32_t *device_size)
{
    uint32_t section_index;
    bool found_init = false;
    bool found_uninit = false;
    bool found_erase = false;
    bool found_program = false;
    bool found_device = false;

    for (section_index = 0U; section_index < header->shnum; section_index++)
    {
        const elf32_section_t *symbols;
        const elf32_section_t *strings;
        const uint8_t *string_data;
        uint32_t count;
        uint32_t symbol_index;

        if (!section_at(header, section_index, &symbols)) return false;
        if (symbols->type != SHT_SYMTAB) continue;
        if ((symbols->entry_size != sizeof(elf32_symbol_t)) ||
            !range_valid(symbols->offset, symbols->size, algorithm->file_size) ||
            !section_at(header, symbols->link, &strings) || (strings->type != SHT_STRTAB) ||
            !range_valid(strings->offset, strings->size, algorithm->file_size)) return false;
        string_data = flm_file + strings->offset;
        count = symbols->size / symbols->entry_size;
        for (symbol_index = 0U; symbol_index < count; symbol_index++)
        {
            const elf32_symbol_t *symbol = (const elf32_symbol_t *)(flm_file +
                symbols->offset + symbol_index * symbols->entry_size);
            const char *name;
            if (symbol->section_index == SHN_UNDEF) continue;
            name = table_string(string_data, strings->size, symbol->name);
            if (name == NULL) return false;
            if (strcmp(name, "Init") == 0)
            {
                algorithm->init = symbol->value;
                found_init = true;
            }
            else if (strcmp(name, "UnInit") == 0)
            {
                algorithm->uninit = symbol->value;
                found_uninit = true;
            }
            else if (strcmp(name, "EraseSector") == 0)
            {
                algorithm->erase_sector = symbol->value;
                found_erase = true;
            }
            else if (strcmp(name, "ProgramPage") == 0)
            {
                algorithm->program_page = symbol->value;
                found_program = true;
            }
            else if (strcmp(name, "EraseChip") == 0)
            {
                algorithm->erase_chip = symbol->value;
                algorithm->has_erase_chip = true;
            }
            else if (strcmp(name, "BlankCheck") == 0)
            {
                algorithm->blank_check = symbol->value;
                algorithm->has_blank_check = true;
            }
            else if (strcmp(name, "Verify") == 0)
            {
                algorithm->verify = symbol->value;
                algorithm->has_verify = true;
            }
            else if (strcmp(name, "FlashDevice") == 0)
            {
                *device_address = symbol->value;
                *device_size = symbol->size;
                found_device = true;
            }
        }
    }
    return found_init && found_uninit && found_erase && found_program && found_device;
}

static bool parse_device(const elf32_header_t *header, flm_algorithm_t *algorithm,
                         uint32_t address, uint32_t symbol_size)
{
    const uint8_t *device;
    uint32_t offset;
    uint32_t index;

    if (symbol_size < (FLM_DEVICE_HEADER_SIZE + 8U)) return false;
    device = address_to_file(header, address, symbol_size);
    if (device == NULL) return false;
    memcpy(algorithm->device_name, device + 2U, 128U);
    algorithm->device_name[128] = '\0';
    if (memchr(algorithm->device_name, '\0', 128U) == NULL) return false;
    memcpy(&algorithm->device_type, device + 130U, sizeof(algorithm->device_type));
    memcpy(&algorithm->flash_start, device + 132U, sizeof(uint32_t));
    memcpy(&algorithm->flash_size, device + 136U, sizeof(uint32_t));
    memcpy(&algorithm->page_size, device + 140U, sizeof(uint32_t));
    algorithm->erased_value = device[148U];
    memcpy(&algorithm->program_timeout_ms, device + 152U, sizeof(uint32_t));
    memcpy(&algorithm->erase_timeout_ms, device + 156U, sizeof(uint32_t));
    if ((algorithm->flash_size == 0U) || (algorithm->page_size == 0U) ||
        (algorithm->page_size > 65536U) ||
        (algorithm->flash_start > UINT32_MAX - algorithm->flash_size)) return false;
    offset = FLM_DEVICE_HEADER_SIZE;
    for (index = 0U; index < FLM_MAX_SECTOR_REGIONS; index++, offset += 8U)
    {
        flm_sector_region_t region;
        if ((offset + 8U) > symbol_size) return false;
        memcpy(&region.size, device + offset, sizeof(uint32_t));
        memcpy(&region.address, device + offset + 4U, sizeof(uint32_t));
        if ((region.size == UINT32_MAX) && (region.address == UINT32_MAX)) break;
        if ((region.size == 0U) || (region.address >= algorithm->flash_size) ||
            ((index != 0U) &&
             (region.address <= algorithm->sector_regions[index - 1U].address))) return false;
        algorithm->sector_regions[index] = region;
        algorithm->sector_region_count++;
    }
    return (algorithm->sector_region_count != 0U) && (index < FLM_MAX_SECTOR_REGIONS);
}

static bool executable_symbol(const flm_algorithm_t *algorithm, uint32_t value)
{
    uint32_t address = value & ~1U;
    uint32_t index;
    for (index = 0U; index < algorithm->section_count; index++)
    {
        const flm_load_section_t *section = &algorithm->sections[index];
        if (((section->flags & SHF_EXECINSTR) != 0U) &&
            (address >= section->address) &&
            ((address - section->address) < section->size)) return true;
    }
    return false;
}

static flm_parser_status_t parse_elf(flm_algorithm_t *algorithm)
{
    const elf32_header_t *header = (const elf32_header_t *)flm_file;
    const elf32_section_t *name_section;
    const uint8_t *name_data;
    uint32_t index;
    uint32_t minimum = UINT32_MAX;
    uint32_t maximum = 0U;
    uint32_t device_address = 0U;
    uint32_t device_size = 0U;

    if ((algorithm->file_size < sizeof(*header)) ||
        (memcmp(header->ident, "\x7F" "ELF", 4U) != 0) ||
        (header->ident[4] != ELFCLASS32) || (header->ident[5] != ELFDATA2LSB) ||
        (header->type != ET_EXEC) || (header->machine != EM_ARM) ||
        (header->ehsize != sizeof(*header)) || (header->shnum == 0U) ||
        (header->shentsize != sizeof(elf32_section_t)) ||
        !range_valid(header->shoff, (uint32_t)header->shnum * header->shentsize,
                     algorithm->file_size)) return FLM_PARSER_BAD_ELF;
    if (!section_at(header, header->shstrndx, &name_section) ||
        (name_section->type != SHT_STRTAB) ||
        !range_valid(name_section->offset, name_section->size, algorithm->file_size))
        return FLM_PARSER_BAD_ELF;
    name_data = flm_file + name_section->offset;
    for (index = 0U; index < header->shnum; index++)
    {
        const elf32_section_t *section;
        const char *name;
        uint32_t end;
        if (!section_at(header, index, &section)) return FLM_PARSER_BAD_ELF;
        name = table_string(name_data, name_section->size, section->name);
        if (name == NULL) return FLM_PARSER_BAD_ELF;
        if (((section->type == SHT_REL) || (section->type == SHT_RELA)) &&
            (section->size != 0U)) return FLM_PARSER_UNSUPPORTED;
        if ((section->flags & SHF_ALLOC) == 0U) continue;
        if (strcmp(name, "DevDscr") == 0) continue;
        if ((section->flags & (SHF_EXECINSTR | SHF_WRITE)) == 0U) continue;
        if (section->size == 0U) continue;
        if ((section->type != SHT_PROGBITS) && (section->type != SHT_NOBITS))
            return FLM_PARSER_UNSUPPORTED;
        if ((section->type == SHT_PROGBITS) &&
            !range_valid(section->offset, section->size, algorithm->file_size))
            return FLM_PARSER_BAD_ELF;
        if ((section->address > UINT32_MAX - section->size) ||
            (algorithm->section_count >= FLM_MAX_LOAD_SECTIONS))
            return FLM_PARSER_UNSUPPORTED;
        algorithm->sections[algorithm->section_count].address = section->address;
        algorithm->sections[algorithm->section_count].size = section->size;
        algorithm->sections[algorithm->section_count].file_offset = section->offset;
        algorithm->sections[algorithm->section_count].type = section->type;
        algorithm->sections[algorithm->section_count].flags = section->flags;
        algorithm->section_count++;
        end = section->address + section->size;
        if (section->address < minimum) minimum = section->address;
        if (end > maximum) maximum = end;
        if (((section->flags & SHF_WRITE) != 0U) &&
            (algorithm->static_base_offset == UINT32_MAX))
            algorithm->static_base_offset = section->address;
    }
    if ((algorithm->section_count == 0U) || (minimum != 0U) || (maximum == 0U) ||
        (maximum > 32U * 1024U)) return FLM_PARSER_UNSUPPORTED;
    algorithm->image_base = minimum;
    algorithm->image_size = maximum - minimum;
    if (algorithm->static_base_offset == UINT32_MAX)
        algorithm->static_base_offset = 0U;
    if (!find_symbols(header, algorithm, &device_address, &device_size))
        return FLM_PARSER_MISSING_SYMBOL;
    if (!executable_symbol(algorithm, algorithm->init) ||
        !executable_symbol(algorithm, algorithm->uninit) ||
        !executable_symbol(algorithm, algorithm->erase_sector) ||
        !executable_symbol(algorithm, algorithm->program_page) ||
        (algorithm->has_erase_chip && !executable_symbol(algorithm, algorithm->erase_chip)) ||
        (algorithm->has_blank_check && !executable_symbol(algorithm, algorithm->blank_check)) ||
        (algorithm->has_verify && !executable_symbol(algorithm, algorithm->verify)))
        return FLM_PARSER_MISSING_SYMBOL;
    if (!parse_device(header, algorithm, device_address, device_size))
        return FLM_PARSER_BAD_DEVICE;
    return FLM_PARSER_OK;
}

flm_parser_status_t flm_parser_load(const char *path, flm_algorithm_t *algorithm)
{
    uint32_t file_size;
    size_t total = 0U;
    file_service_status_t file_status;

    if ((path == NULL) || (algorithm == NULL)) return FLM_PARSER_FILE_ERROR;
    memset(algorithm, 0, sizeof(*algorithm));
    algorithm->static_base_offset = UINT32_MAX;
    file_status = file_task_stream_open(path, &file_size, FLM_FILE_TIMEOUT);
    if (file_status != FILE_SERVICE_OK) return FLM_PARSER_FILE_ERROR;
    if ((file_size < sizeof(elf32_header_t)) || (file_size > sizeof(flm_file)))
    {
        (void)file_task_stream_close(FLM_FILE_TIMEOUT);
        return FLM_PARSER_TOO_LARGE;
    }
    while (total < file_size)
    {
        size_t received = 0U;
        file_status = file_task_stream_read(flm_file + total, file_size - total,
                                            &received, FLM_FILE_TIMEOUT);
        if ((file_status != FILE_SERVICE_OK) || (received == 0U))
        {
            (void)file_task_stream_close(FLM_FILE_TIMEOUT);
            return FLM_PARSER_FILE_ERROR;
        }
        total += received;
    }
    file_status = file_task_stream_close(FLM_FILE_TIMEOUT);
    if (file_status != FILE_SERVICE_OK) return FLM_PARSER_FILE_ERROR;
    algorithm->file_data = flm_file;
    algorithm->file_size = file_size;
    return parse_elf(algorithm);
}

uint32_t flm_parser_sector_size(const flm_algorithm_t *algorithm, uint32_t address)
{
    uint32_t offset;
    uint32_t index;
    uint32_t size = 0U;

    if ((algorithm == NULL) || (address < algorithm->flash_start) ||
        (address >= algorithm->flash_start + algorithm->flash_size)) return 0U;
    offset = address - algorithm->flash_start;
    for (index = 0U; index < algorithm->sector_region_count; index++)
    {
        if (offset < algorithm->sector_regions[index].address) break;
        size = algorithm->sector_regions[index].size;
    }
    return size;
}

const char *flm_parser_status_text(flm_parser_status_t status)
{
    switch (status)
    {
        case FLM_PARSER_OK: return "FLM有效";
        case FLM_PARSER_FILE_ERROR: return "FLM文件读取失败";
        case FLM_PARSER_TOO_LARGE: return "FLM文件过大";
        case FLM_PARSER_BAD_ELF: return "FLM的ELF结构损坏";
        case FLM_PARSER_UNSUPPORTED: return "FLM格式暂不支持";
        case FLM_PARSER_MISSING_SYMBOL: return "FLM缺少必要函数";
        case FLM_PARSER_BAD_DEVICE: return "FLM设备描述无效";
        default: return "未知FLM错误";
    }
}
