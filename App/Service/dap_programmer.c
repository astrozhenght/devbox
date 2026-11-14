#include "dap_programmer.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "dap_service.h"
#include "firmware_reader.h"
#include "flm_executor.h"
#include "flm_parser.h"
#include "swd_target.h"
#include "target_profile.h"
#include "task.h"

#define DAP_PROGRAMMER_VERIFY_CHUNK  256U
#define DAP_PROGRAMMER_CALL_MARGIN   200U

typedef struct
{
    const flm_executor_t *executor;
    const flm_algorithm_t *algorithm;
    char *error;
    size_t error_size;
    uint32_t completed;
    uint32_t total;
} dap_page_context_t;

static dap_programmer_status_t programmer_status;
static bool request_pending;
static bool cancel_requested;
static uint32_t requested_clock_hz = DAP_PROGRAMMER_SWD_CLOCK_HZ;
static uint8_t requested_erase_mode = DAP_ERASE_SECTOR;
static uint32_t requested_ram_start = 0x20000000U;
static uint32_t requested_ram_size = 20U * 1024U;
static TickType_t programmer_start_tick;
static char requested_firmware[FILE_SERVICE_PATH_CAP];
static char requested_flm[FILE_SERVICE_PATH_CAP];
static uint8_t firmware_touched[FIRMWARE_READER_MAX_MAP_SIZE]
    __attribute__((section(".axisram"), aligned(8)));
static uint8_t verify_data[DAP_PROGRAMMER_VERIFY_CHUNK]
    __attribute__((section(".axisram"), aligned(32)));

static void set_state(dap_programmer_state_t state, uint32_t completed,
                      uint32_t total, const char *message)
{
    taskENTER_CRITICAL();
    programmer_status.state = state;
    if ((state >= DAP_PROGRAMMER_ERASING) && (state <= DAP_PROGRAMMER_RESETTING))
        programmer_status.operation_stage = state;
    programmer_status.completed = completed;
    programmer_status.total = total;
    programmer_status.progress_percent = (total == 0U) ? 0U :
        (uint8_t)(((uint64_t)completed * 100U) / total);
    if (message != NULL)
    {
        strncpy(programmer_status.message, message, sizeof(programmer_status.message) - 1U);
        programmer_status.message[sizeof(programmer_status.message) - 1U] = '\0';
    }
    taskEXIT_CRITICAL();
}

static bool cancelled(void)
{
    bool value;
    taskENTER_CRITICAL();
    value = cancel_requested;
    programmer_status.cancel_requested = value;
    taskEXIT_CRITICAL();
    return value;
}

static void set_error(char *error, size_t error_size,
                      const char *message, uint32_t value)
{
    if ((error == NULL) || (error_size == 0U)) return;
    if (value == UINT32_MAX) snprintf(error, error_size, "%s", message);
    else snprintf(error, error_size, "%s0x%08lX", message, (unsigned long)value);
}

static bool call_flm(const flm_executor_t *executor, uint32_t function,
                     uint32_t arg0, uint32_t arg1, uint32_t arg2,
                     uint32_t timeout_ms, const char *operation,
                     char *error, size_t error_size)
{
    uint32_t result;
    swd_target_status_t status = flm_executor_call(executor, function, arg0, arg1,
                                                   arg2, 0U, timeout_ms, &result);

    if (status != SWD_TARGET_OK)
    {
        snprintf(error, error_size, "%s: %s", operation,
                 swd_target_status_text(status));
        return false;
    }
    if (result != 0U)
    {
        set_error(error, error_size, operation, result);
        return false;
    }
    return true;
}

static bool validate_algorithm(const flm_algorithm_t *algorithm,
                               char *error, size_t error_size)
{
    uint32_t index;

    if ((algorithm == NULL) || (algorithm->flash_size == 0U) ||
        (algorithm->page_size == 0U) ||
        (algorithm->page_size > FIRMWARE_READER_MAX_PAGE_SIZE) ||
        (algorithm->flash_start > UINT32_MAX - algorithm->flash_size))
    {
        set_error(error, error_size, "FLM设备范围或编程页无效", UINT32_MAX);
        return false;
    }
    if ((algorithm->sector_region_count == 0U) ||
        (algorithm->sector_regions[0].address != 0U))
    {
        set_error(error, error_size, "FLM扇区表未覆盖Flash起始", UINT32_MAX);
        return false;
    }
    for (index = 0U; index < algorithm->sector_region_count; index++)
    {
        uint32_t region_start = algorithm->sector_regions[index].address;
        uint32_t region_end = (index + 1U < algorithm->sector_region_count) ?
                              algorithm->sector_regions[index + 1U].address :
                              algorithm->flash_size;

        if ((region_start >= region_end) || (region_end > algorithm->flash_size) ||
            (algorithm->sector_regions[index].size == 0U))
        {
            set_error(error, error_size, "FLM扇区区域无效", UINT32_MAX);
            return false;
        }
    }
    return true;
}

static uint32_t sector_region_end(const flm_algorithm_t *algorithm, uint32_t index)
{
    return (index + 1U < algorithm->sector_region_count) ?
           algorithm->sector_regions[index + 1U].address : algorithm->flash_size;
}

static bool sector_overlaps_data(const firmware_meta_t *meta,
                                 const uint8_t *touched,
                                 uint32_t sector_offset, uint32_t sector_end)
{
    uint32_t limit = sector_end;
    uint32_t first_page;
    uint32_t last_page;
    uint32_t page;

    if (limit <= sector_offset) return false;
    first_page = sector_offset / meta->page_size;
    last_page = (limit - 1U) / meta->page_size;
    for (page = first_page; (page <= last_page) && (page < meta->page_count); page++)
    {
        if ((touched[page / 8U] & (uint8_t)(1U << (page % 8U))) != 0U) return true;
    }
    return false;
}

static uint32_t count_sectors(const flm_algorithm_t *algorithm,
                              const firmware_meta_t *meta,
                              const uint8_t *touched, bool touched_only)
{
    uint32_t total = 0U;
    uint32_t region;

    for (region = 0U; region < algorithm->sector_region_count; region++)
    {
        uint32_t start = algorithm->sector_regions[region].address;
        uint32_t size = algorithm->sector_regions[region].size;
        uint32_t end = sector_region_end(algorithm, region);
        uint32_t offset;

        for (offset = start; offset < end; offset += size)
        {
            uint64_t sector_end64 = (uint64_t)offset + size;
            uint32_t sector_end = (sector_end64 > end) ? end : (uint32_t)sector_end64;

            if (!touched_only ||
                sector_overlaps_data(meta, touched, offset, sector_end))
                total++;
            if (offset > UINT32_MAX - size) break;
        }
    }
    return total;
}

static bool erase_sectors(const flm_executor_t *executor,
                          const flm_algorithm_t *algorithm,
                          const firmware_meta_t *meta,
                          const uint8_t *touched, bool touched_only,
                          uint32_t total, char *error, size_t error_size)
{
    uint32_t completed = 0U;
    uint32_t region;

    for (region = 0U; region < algorithm->sector_region_count; region++)
    {
        uint32_t start = algorithm->sector_regions[region].address;
        uint32_t size = algorithm->sector_regions[region].size;
        uint32_t end = sector_region_end(algorithm, region);
        uint32_t offset;

        for (offset = start; offset < end; offset += size)
        {
            uint64_t sector_end64 = (uint64_t)offset + size;
            uint32_t sector_end = (sector_end64 > end) ? end : (uint32_t)sector_end64;

            if (cancelled()) return false;
            if (!touched_only ||
                sector_overlaps_data(meta, touched, offset, sector_end))
            {
                if (!call_flm(executor, algorithm->erase_sector,
                              algorithm->flash_start + offset, 0U, 0U,
                              algorithm->erase_timeout_ms + DAP_PROGRAMMER_CALL_MARGIN,
                              "FLM EraseSector", error, error_size))
                    return false;
                set_state(DAP_PROGRAMMER_ERASING, ++completed, total,
                          touched_only ? "正在擦除固件覆盖扇区" : "正在逐扇区擦除");
            }
            if (offset > UINT32_MAX - size) break;
        }
    }
    return true;
}

static bool program_page(uint32_t address, const uint8_t *data,
                         uint32_t size, void *user)
{
    dap_page_context_t *context = (dap_page_context_t *)user;
    swd_target_status_t status;

    if (context == NULL) return false;
    if (size != context->algorithm->page_size)
    {
        set_error(context->error, context->error_size,
                  "固件编程页大小与FLM不一致", UINT32_MAX);
        return false;
    }
    if (cancelled()) return false;
    status = swd_target_write_memory(context->executor->page_buffer, data, size);
    if (status != SWD_TARGET_OK)
    {
        snprintf(context->error, context->error_size,
                 "写入FLM页缓冲区: %s", swd_target_status_text(status));
        return false;
    }
    if (!call_flm(context->executor, context->algorithm->program_page,
                  address, size, context->executor->page_buffer,
                  context->algorithm->program_timeout_ms + DAP_PROGRAMMER_CALL_MARGIN,
                  "FLM ProgramPage", context->error, context->error_size))
        return false;
    set_state(DAP_PROGRAMMER_PROGRAMMING, ++context->completed, context->total,
              "正在流式写入固件");
    return true;
}

static bool verify_page_with_flm(uint32_t address, const uint8_t *data,
                                 uint32_t size, void *user)
{
    dap_page_context_t *context = (dap_page_context_t *)user;
    swd_target_status_t status;
    uint32_t result;

    if (cancelled()) return false;
    status = swd_target_write_memory(context->executor->page_buffer, data, size);
    if (status != SWD_TARGET_OK)
    {
        snprintf(context->error, context->error_size,
                 "写入FLM校验缓冲区: %s", swd_target_status_text(status));
        return false;
    }
    status = flm_executor_call(context->executor, context->algorithm->verify,
                               address, size, context->executor->page_buffer, 0U,
                               context->algorithm->program_timeout_ms +
                               DAP_PROGRAMMER_CALL_MARGIN, &result);
    if (status != SWD_TARGET_OK)
    {
        snprintf(context->error, context->error_size,
                 "FLM Verify: %s", swd_target_status_text(status));
        return false;
    }
    if (result != address + size)
    {
        set_error(context->error, context->error_size, "FLM校验失败", result);
        return false;
    }
    set_state(DAP_PROGRAMMER_VERIFYING, ++context->completed, context->total,
              "正在执行FLM校验");
    return true;
}

static bool verify_page_with_swd(uint32_t address, const uint8_t *data,
                                 uint32_t size, void *user)
{
    dap_page_context_t *context = (dap_page_context_t *)user;
    uint32_t offset;

    if (cancelled()) return false;
    for (offset = 0U; offset < size; offset += sizeof(verify_data))
    {
        uint32_t amount = size - offset;
        swd_target_status_t status;

        if (amount > sizeof(verify_data)) amount = sizeof(verify_data);
        status = swd_target_read_memory(address + offset, verify_data, amount);
        if (status != SWD_TARGET_OK)
        {
            snprintf(context->error, context->error_size,
                     "SWD回读校验: %s", swd_target_status_text(status));
            return false;
        }
        if (memcmp(verify_data, data + offset, amount) != 0)
        {
            set_error(context->error, context->error_size,
                      "SWD回读校验失败", address + offset);
            return false;
        }
    }
    set_state(DAP_PROGRAMMER_VERIFYING, ++context->completed, context->total,
              "正在回读校验");
    return true;
}

void dap_programmer_init(void)
{
    const target_profile_t *profile = target_profile_stm32f103c8();

    memset(&programmer_status, 0, sizeof(programmer_status));
    programmer_status.state = DAP_PROGRAMMER_IDLE;
    programmer_status.swd_clock_hz = DAP_PROGRAMMER_SWD_CLOCK_HZ;
    requested_ram_start = profile->ram_start;
    requested_ram_size = profile->ram_size;
    programmer_status.ram_start = requested_ram_start;
    programmer_status.ram_size = requested_ram_size;
    strcpy(programmer_status.message, "请选择固件并开始烧录");
}

bool dap_programmer_request(const char *firmware_path, const char *flm_path)
{
    return dap_programmer_request_with_clock(firmware_path, flm_path,
                                             DAP_PROGRAMMER_SWD_CLOCK_HZ);
}

bool dap_programmer_request_with_clock(const char *firmware_path, const char *flm_path,
                                        uint32_t swd_clock_hz)
{
    return dap_programmer_request_with_options(firmware_path, flm_path, swd_clock_hz,
                                               DAP_ERASE_SECTOR);
}

bool dap_programmer_request_with_options(const char *firmware_path, const char *flm_path,
                                          uint32_t swd_clock_hz, uint8_t erase_mode)
{
    const target_profile_t *profile = target_profile_stm32f103c8();
    return dap_programmer_request_with_ram(firmware_path, flm_path, swd_clock_hz,
                                           erase_mode, profile->ram_start,
                                           profile->ram_size);
}

bool dap_programmer_request_with_ram(const char *firmware_path, const char *flm_path,
                                     uint32_t swd_clock_hz, uint8_t erase_mode,
                                     uint32_t ram_start, uint32_t ram_size)
{
    bool accepted = false;

    if ((swd_clock_hz < 5000U) || (swd_clock_hz > 10000000U) ||
        (erase_mode > DAP_ERASE_CHIP) || (ram_size < 256U) ||
        (ram_start > UINT32_MAX - ram_size) ||
        (firmware_path == NULL) || (flm_path == NULL) ||
        !file_service_path_valid(firmware_path) || !file_service_path_valid(flm_path))
        return false;
    taskENTER_CRITICAL();
    if (!programmer_status.active && !request_pending)
    {
        strcpy(requested_firmware, firmware_path);
        strcpy(requested_flm, flm_path);
        requested_clock_hz = swd_clock_hz;
        requested_erase_mode = erase_mode;
        requested_ram_start = ram_start;
        requested_ram_size = ram_size;
        programmer_status.swd_clock_hz = swd_clock_hz;
        programmer_status.erase_mode = erase_mode;
        programmer_status.ram_start = ram_start;
        programmer_status.ram_size = ram_size;
        request_pending = true;
        cancel_requested = false;
        programmer_status.pending = true;
        programmer_start_tick = xTaskGetTickCount();
        programmer_status.elapsed_ms = 0U;
        programmer_status.firmware_bytes = 0U;
        programmer_status.firmware_start = 0U;
        programmer_status.firmware_end = 0U;
        programmer_status.operation_stage = DAP_PROGRAMMER_IDLE;
        programmer_status.cancel_requested = false;
        strcpy(programmer_status.firmware_path, firmware_path);
        strcpy(programmer_status.flm_path, flm_path);
        accepted = true;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

void dap_programmer_cancel(void)
{
    taskENTER_CRITICAL();
    if (programmer_status.active || request_pending) cancel_requested = true;
    taskEXIT_CRITICAL();
}

bool dap_programmer_process(void)
{
    const target_profile_t *profile = target_profile_stm32f103c8();
    flm_algorithm_t algorithm;
    flm_executor_t executor;
    firmware_meta_t firmware;
    firmware_reader_status_t firmware_result;
    flm_parser_status_t flm_result;
    swd_target_status_t swd_result;
    dap_page_context_t page_context;
    uint32_t dpidr = 0U;
    uint32_t device_idcode = 0U;
    uint32_t erase_total = 0U;
    bool connected = false;
    bool initialized = false;
    uint32_t initialized_operation = 0U;
    char error[96] = {0};

    taskENTER_CRITICAL();
    if (!request_pending)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    request_pending = false;
    programmer_status.pending = false;
    programmer_status.active = true;
    programmer_status.state = DAP_PROGRAMMER_VALIDATING;
    programmer_status.progress_percent = 0U;
    programmer_status.completed = 0U;
    programmer_status.total = 0U;
    programmer_status.dpidr = 0U;
    programmer_status.device_idcode = 0U;
    programmer_status.firmware_bytes = 0U;
    programmer_status.algorithm_name[0] = '\0';
    strcpy(programmer_status.message, "正在解析FLM并扫描固件");
    taskEXIT_CRITICAL();

    dap_service_set_local_active(true);
    dap_service_flush_requests();

    flm_result = flm_parser_load(requested_flm, &algorithm);
    if (flm_result != FLM_PARSER_OK)
    {
        strncpy(error, flm_parser_status_text(flm_result), sizeof(error) - 1U);
        goto finish;
    }
    if (!validate_algorithm(&algorithm, error, sizeof(error))) goto finish;
    taskENTER_CRITICAL();
    strncpy(programmer_status.algorithm_name, algorithm.device_name,
            sizeof(programmer_status.algorithm_name) - 1U);
    programmer_status.algorithm_name[sizeof(programmer_status.algorithm_name) - 1U] = '\0';
    taskEXIT_CRITICAL();

    firmware_result = firmware_scan(requested_firmware,
                                    algorithm.flash_start, algorithm.flash_size,
                                    algorithm.page_size, &firmware,
                                    firmware_touched, sizeof(firmware_touched));
    if (firmware_result != FIRMWARE_READER_OK)
    {
        strncpy(error, firmware_reader_status_text(firmware_result),
                sizeof(error) - 1U);
        goto finish;
    }
    taskENTER_CRITICAL();
    programmer_status.firmware_bytes = firmware.data_bytes;
    programmer_status.firmware_start = firmware.min_address;
    programmer_status.firmware_end = firmware.max_address;
    taskEXIT_CRITICAL();
    if (cancelled()) goto finish;

    set_state(DAP_PROGRAMMER_CONNECTING, 0U, 0U, "正在连接目标");
    swd_result = swd_target_connect(requested_clock_hz, &dpidr);
    if (swd_result != SWD_TARGET_OK)
    {
        strncpy(error, swd_target_status_text(swd_result), sizeof(error) - 1U);
        goto finish;
    }
    connected = true;

    /* 不同 Cortex-M 厂商的 DBGMCU 地址不同，ID 仅用于显示，不能阻断烧录。 */
    swd_result = swd_target_read32(0xE0042000U, &device_idcode);
    if (swd_result != SWD_TARGET_OK) device_idcode = 0U;
    taskENTER_CRITICAL();
    programmer_status.dpidr = dpidr;
    programmer_status.device_idcode = device_idcode;
    taskEXIT_CRITICAL();

    swd_result = swd_target_reset_and_halt(1000U);
    if (swd_result != SWD_TARGET_OK)
    {
        strncpy(error, "目标复位暂停失败", sizeof(error) - 1U);
        goto finish;
    }

    set_state(DAP_PROGRAMMER_LOADING_ALGORITHM, 0U, 0U,
              "正在加载FLM到指定RAM");
    if (((requested_ram_start & 7U) != 0U) || (requested_ram_size < 256U))
    {
        strncpy(error, "RAM对齐或大小无效", sizeof(error) - 1U);
        goto finish;
    }
    swd_result = flm_executor_load_at(&executor, &algorithm,
                                      requested_ram_start, requested_ram_size);
    if (swd_result != SWD_TARGET_OK)
    {
        snprintf(error, sizeof(error), "FLM装载失败: %s",
                 swd_target_status_text(swd_result));
        goto finish;
    }
    if (cancelled()) goto finish;

    if (requested_erase_mode == DAP_ERASE_CHIP)
    {
        erase_total = algorithm.has_erase_chip ? 1U :
                      count_sectors(&algorithm, &firmware, firmware_touched, false);
    }
    else
    {
        erase_total = count_sectors(&algorithm, &firmware, firmware_touched, true);
    }
    if (erase_total == 0U)
    {
        strncpy(error, "未找到可擦除的Flash扇区", sizeof(error) - 1U);
        goto finish;
    }
    set_state(DAP_PROGRAMMER_ERASING, 0U, erase_total, "正在擦除目标Flash");
    if (!call_flm(&executor, algorithm.init, algorithm.flash_start,
                  profile->algorithm_clock_hz, 1U, 1000U,
                  "FLM Init(Erase)", error, sizeof(error))) goto finish;
    initialized = true;
    initialized_operation = 1U;
    if (requested_erase_mode == DAP_ERASE_CHIP)
    {
        if (algorithm.has_erase_chip)
        {
            if (!call_flm(&executor, algorithm.erase_chip, 0U, 0U, 0U,
                          algorithm.erase_timeout_ms + DAP_PROGRAMMER_CALL_MARGIN,
                          "FLM EraseChip", error, sizeof(error))) goto finish;
            set_state(DAP_PROGRAMMER_ERASING, 1U, erase_total, "正在整片擦除");
        }
        else if (!erase_sectors(&executor, &algorithm, &firmware, firmware_touched,
                                false, erase_total, error, sizeof(error)))
        {
            goto finish;
        }
    }
    else if (!erase_sectors(&executor, &algorithm, &firmware, firmware_touched,
                            true, erase_total, error, sizeof(error)))
    {
        goto finish;
    }
    if (!call_flm(&executor, algorithm.uninit, 1U, 0U, 0U, 1000U,
                  "FLM UnInit(Erase)", error, sizeof(error))) goto finish;
    initialized = false;

    set_state(DAP_PROGRAMMER_PROGRAMMING, 0U, firmware.data_pages,
              "正在流式写入固件");
    if (!call_flm(&executor, algorithm.init, algorithm.flash_start,
                  profile->algorithm_clock_hz, 2U, 1000U,
                  "FLM Init(Program)", error, sizeof(error))) goto finish;
    initialized = true;
    initialized_operation = 2U;
    memset(&page_context, 0, sizeof(page_context));
    page_context.executor = &executor;
    page_context.algorithm = &algorithm;
    page_context.error = error;
    page_context.error_size = sizeof(error);
    page_context.total = firmware.data_pages;
    firmware_result = firmware_stream(requested_firmware,
                                      algorithm.flash_start, algorithm.flash_size,
                                      algorithm.page_size, algorithm.erased_value,
                                      program_page, &page_context);
    if (firmware_result != FIRMWARE_READER_OK)
    {
        if (error[0] == '\0')
            strncpy(error, firmware_reader_status_text(firmware_result),
                    sizeof(error) - 1U);
        goto finish;
    }
    if (!call_flm(&executor, algorithm.uninit, 2U, 0U, 0U, 1000U,
                  "FLM UnInit(Program)", error, sizeof(error))) goto finish;
    initialized = false;

    set_state(DAP_PROGRAMMER_VERIFYING, 0U, firmware.data_pages, "正在校验固件");
    memset(&page_context, 0, sizeof(page_context));
    page_context.executor = &executor;
    page_context.algorithm = &algorithm;
    page_context.error = error;
    page_context.error_size = sizeof(error);
    page_context.total = firmware.data_pages;
    if (algorithm.has_verify)
    {
        if (!call_flm(&executor, algorithm.init, algorithm.flash_start,
                      profile->algorithm_clock_hz, 3U, 1000U,
                      "FLM Init(Verify)", error, sizeof(error))) goto finish;
        initialized = true;
        initialized_operation = 3U;
        firmware_result = firmware_stream(requested_firmware,
                                          algorithm.flash_start, algorithm.flash_size,
                                          algorithm.page_size, algorithm.erased_value,
                                          verify_page_with_flm, &page_context);
        if (firmware_result != FIRMWARE_READER_OK)
        {
            if (error[0] == '\0')
                strncpy(error, firmware_reader_status_text(firmware_result),
                        sizeof(error) - 1U);
            goto finish;
        }
        if (!call_flm(&executor, algorithm.uninit, 3U, 0U, 0U, 1000U,
                      "FLM UnInit(Verify)", error, sizeof(error))) goto finish;
        initialized = false;
    }
    else
    {
        firmware_result = firmware_stream(requested_firmware,
                                          algorithm.flash_start, algorithm.flash_size,
                                          algorithm.page_size, algorithm.erased_value,
                                          verify_page_with_swd, &page_context);
        if (firmware_result != FIRMWARE_READER_OK)
        {
            if (error[0] == '\0')
                strncpy(error, firmware_reader_status_text(firmware_result),
                        sizeof(error) - 1U);
            goto finish;
        }
    }

    set_state(DAP_PROGRAMMER_RESETTING, 0U, 0U, "正在复位目标");
    swd_result = swd_target_reset();
    if (swd_result != SWD_TARGET_OK)
    {
        strncpy(error, swd_target_status_text(swd_result), sizeof(error) - 1U);
        goto finish;
    }
    vTaskDelay(pdMS_TO_TICKS(20U));
    set_state(DAP_PROGRAMMER_SUCCESS, 1U, 1U, "本地烧录和校验完成");

finish:
    if (initialized)
    {
        uint32_t ignored;
        (void)flm_executor_call(&executor, algorithm.uninit,
                                initialized_operation, 0U, 0U, 0U,
                                1000U, &ignored);
    }
    if (connected)
    {
        if ((programmer_status.state != DAP_PROGRAMMER_SUCCESS) && cancelled())
            (void)swd_target_reset();
        swd_target_disconnect();
    }
    if (programmer_status.state != DAP_PROGRAMMER_SUCCESS)
    {
        if (cancelled())
            set_state(DAP_PROGRAMMER_CANCELLED, 0U, 0U, "烧录已取消");
        else
            set_state(DAP_PROGRAMMER_FAILED, 0U, 0U,
                      (error[0] != '\0') ? error : "本地烧录失败");
    }
    dap_service_flush_requests();
    dap_service_set_local_active(false);
    taskENTER_CRITICAL();
    programmer_status.elapsed_ms = (uint32_t)(((uint64_t)
        (xTaskGetTickCount() - programmer_start_tick) * 1000U) / configTICK_RATE_HZ);
    programmer_status.active = false;
    programmer_status.cancel_requested = false;
    cancel_requested = false;
    taskEXIT_CRITICAL();
    return true;
}

bool dap_programmer_get_status(dap_programmer_status_t *status)
{
    if (status == NULL) return false;
    taskENTER_CRITICAL();
    memcpy(status, &programmer_status, sizeof(*status));
    if (status->active || status->pending)
        status->elapsed_ms = (uint32_t)(((uint64_t)
            (xTaskGetTickCount() - programmer_start_tick) * 1000U) / configTICK_RATE_HZ);
    taskEXIT_CRITICAL();
    return true;
}

const char *dap_programmer_state_text(dap_programmer_state_t state)
{
    switch (state)
    {
        case DAP_PROGRAMMER_IDLE: return "等待本地烧录";
        case DAP_PROGRAMMER_VALIDATING: return "解析固件和FLM";
        case DAP_PROGRAMMER_CONNECTING: return "连接目标";
        case DAP_PROGRAMMER_LOADING_ALGORITHM: return "加载FLM";
        case DAP_PROGRAMMER_ERASING: return "擦除";
        case DAP_PROGRAMMER_PROGRAMMING: return "编程";
        case DAP_PROGRAMMER_VERIFYING: return "校验";
        case DAP_PROGRAMMER_RESETTING: return "复位";
        case DAP_PROGRAMMER_SUCCESS: return "烧录成功";
        case DAP_PROGRAMMER_FAILED: return "烧录失败";
        case DAP_PROGRAMMER_CANCELLED: return "烧录取消";
        default: return "未知状态";
    }
}
