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

static dap_programmer_status_t programmer_status;
static bool request_pending;
static bool cancel_requested;
static uint32_t requested_clock_hz = DAP_PROGRAMMER_SWD_CLOCK_HZ;
static TickType_t programmer_start_tick;
static char requested_firmware[FILE_SERVICE_PATH_CAP];
static char requested_flm[FILE_SERVICE_PATH_CAP];
static uint8_t verify_data[DAP_PROGRAMMER_VERIFY_CHUNK]
    __attribute__((section(".axisram"), aligned(32)));

static void set_state(dap_programmer_state_t state, uint32_t completed,
                      uint32_t total, const char *message)
{
    taskENTER_CRITICAL();
    programmer_status.state = state;
    if (state >= DAP_PROGRAMMER_ERASING && state <= DAP_PROGRAMMER_RESETTING)
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
        (void)snprintf(error, error_size, "%s: %s", operation,
                       swd_target_status_text(status));
        return false;
    }
    if (result != 0U)
    {
        (void)snprintf(error, error_size, "%s返回0x%08lX", operation,
                       (unsigned long)result);
        return false;
    }
    return true;
}

static uint32_t touched_page_count(uint64_t pages)
{
    uint32_t count = 0U;
    while (pages != 0U)
    {
        count += (uint32_t)(pages & 1ULL);
        pages >>= 1;
    }
    return count;
}

static bool validate_compatibility(const target_profile_t *profile,
                                   const flm_algorithm_t *algorithm,
                                   char *error, size_t error_size)
{
    uint64_t flm_end = (uint64_t)algorithm->flash_start + algorithm->flash_size;
    uint64_t target_end = (uint64_t)profile->flash_start + profile->flash_size;
    if ((algorithm->flash_start > profile->flash_start) || (flm_end < target_end))
    {
        strncpy(error, "FLM不覆盖目标Flash范围", error_size - 1U);
        error[error_size - 1U] = '\0';
        return false;
    }
    if (algorithm->page_size != FIRMWARE_READER_PAGE_SIZE)
    {
        strncpy(error, "FLM页大小与F103不匹配", error_size - 1U);
        error[error_size - 1U] = '\0';
        return false;
    }
    return true;
}

void dap_programmer_init(void)
{
    memset(&programmer_status, 0, sizeof(programmer_status));
    programmer_status.state = DAP_PROGRAMMER_IDLE;
    programmer_status.swd_clock_hz = DAP_PROGRAMMER_SWD_CLOCK_HZ;
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
    bool accepted = false;
    if ((swd_clock_hz < 5000U) || (swd_clock_hz > 10000000U) ||
        (firmware_path == NULL) || (flm_path == NULL) ||
        !file_service_path_valid(firmware_path) || !file_service_path_valid(flm_path))
        return false;
    taskENTER_CRITICAL();
    if (!programmer_status.active && !request_pending)
    {
        strcpy(requested_firmware, firmware_path);
        strcpy(requested_flm, flm_path);
        requested_clock_hz = swd_clock_hz;
        programmer_status.swd_clock_hz = swd_clock_hz;
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
    firmware_image_t image;
    flm_algorithm_t algorithm;
    flm_executor_t executor;
    firmware_reader_status_t firmware_result;
    flm_parser_status_t flm_result;
    swd_target_status_t swd_result;
    uint32_t dpidr = 0U;
    uint32_t device_idcode = 0U;
    uint32_t page;
    uint32_t completed;
    uint32_t total_pages;
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
    strcpy(programmer_status.message, "正在检查固件和FLM");
    taskEXIT_CRITICAL();

    dap_service_set_local_active(true);
    dap_service_flush_requests();
    firmware_result = firmware_reader_load(requested_firmware, profile, &image);
    if (firmware_result != FIRMWARE_READER_OK)
    {
        strncpy(error, firmware_reader_status_text(firmware_result), sizeof(error) - 1U);
        goto finish;
    }
    taskENTER_CRITICAL();
    programmer_status.firmware_bytes = image.data_bytes;
    programmer_status.firmware_start = image.min_address;
    programmer_status.firmware_end = image.max_address;
    taskEXIT_CRITICAL();
    flm_result = flm_parser_load(requested_flm, &algorithm);
    if (flm_result != FLM_PARSER_OK)
    {
        strncpy(error, flm_parser_status_text(flm_result), sizeof(error) - 1U);
        goto finish;
    }
    if (!validate_compatibility(profile, &algorithm, error, sizeof(error))) goto finish;
    taskENTER_CRITICAL();
    strncpy(programmer_status.algorithm_name, algorithm.device_name,
            sizeof(programmer_status.algorithm_name) - 1U);
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
    swd_result = swd_target_read32(0xE0042000U, &device_idcode);
    if (swd_result != SWD_TARGET_OK)
    {
        strncpy(error, "无法读取目标芯片ID", sizeof(error) - 1U);
        goto finish;
    }
    if (!target_profile_matches(profile, device_idcode))
    {
        (void)snprintf(error, sizeof(error), "目标ID不匹配:0x%03lX",
                       (unsigned long)(device_idcode & 0x0FFFU));
        goto finish;
    }
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

    set_state(DAP_PROGRAMMER_LOADING_ALGORITHM, 0U, 0U, "正在加载FLM到目标SRAM");
    swd_result = flm_executor_load(&executor, &algorithm, profile);
    if (swd_result != SWD_TARGET_OK)
    {
        strncpy(error, swd_target_status_text(swd_result), sizeof(error) - 1U);
        goto finish;
    }
    if (cancelled()) goto finish;

    total_pages = touched_page_count(image.touched_pages);
    set_state(DAP_PROGRAMMER_ERASING, 0U, total_pages, "正在擦除目标Flash");
    if (!call_flm(&executor, algorithm.init, profile->flash_start,
                  profile->algorithm_clock_hz, 1U, 1000U,
                  "FLM Init(Erase)", error, sizeof(error))) goto finish;
    initialized = true;
    initialized_operation = 1U;
    completed = 0U;
    for (page = 0U; page < FIRMWARE_READER_PAGE_COUNT; page++)
    {
        uint32_t address;
        if ((image.touched_pages & (1ULL << page)) == 0U) continue;
        if (cancelled()) goto finish;
        address = profile->flash_start + page * FIRMWARE_READER_PAGE_SIZE;
        if (!call_flm(&executor, algorithm.erase_sector, address, 0U, 0U,
                      algorithm.erase_timeout_ms + DAP_PROGRAMMER_CALL_MARGIN,
                      "FLM EraseSector", error, sizeof(error))) goto finish;
        set_state(DAP_PROGRAMMER_ERASING, ++completed, total_pages, "正在擦除目标Flash");
    }
    if (!call_flm(&executor, algorithm.uninit, 1U, 0U, 0U, 1000U,
                  "FLM UnInit(Erase)", error, sizeof(error))) goto finish;
    initialized = false;

    set_state(DAP_PROGRAMMER_PROGRAMMING, 0U, total_pages, "正在写入固件");
    if (!call_flm(&executor, algorithm.init, profile->flash_start,
                  profile->algorithm_clock_hz, 2U, 1000U,
                  "FLM Init(Program)", error, sizeof(error))) goto finish;
    initialized = true;
    initialized_operation = 2U;
    completed = 0U;
    for (page = 0U; page < FIRMWARE_READER_PAGE_COUNT; page++)
    {
        uint32_t address;
        const uint8_t *data;
        if ((image.touched_pages & (1ULL << page)) == 0U) continue;
        if (cancelled()) goto finish;
        address = profile->flash_start + page * FIRMWARE_READER_PAGE_SIZE;
        data = image.data + page * FIRMWARE_READER_PAGE_SIZE;
        swd_result = swd_target_write_memory(executor.page_buffer, data,
                                             FIRMWARE_READER_PAGE_SIZE);
        if (swd_result != SWD_TARGET_OK)
        {
            strncpy(error, swd_target_status_text(swd_result), sizeof(error) - 1U);
            goto finish;
        }
        if (!call_flm(&executor, algorithm.program_page, address,
                      FIRMWARE_READER_PAGE_SIZE, executor.page_buffer,
                      algorithm.program_timeout_ms + DAP_PROGRAMMER_CALL_MARGIN,
                      "FLM ProgramPage", error, sizeof(error))) goto finish;
        set_state(DAP_PROGRAMMER_PROGRAMMING, ++completed, total_pages, "正在写入固件");
    }
    if (!call_flm(&executor, algorithm.uninit, 2U, 0U, 0U, 1000U,
                  "FLM UnInit(Program)", error, sizeof(error))) goto finish;
    initialized = false;

    set_state(DAP_PROGRAMMER_VERIFYING, 0U, total_pages, "正在回读校验");
    completed = 0U;
    if (algorithm.has_verify)
    {
        if (!call_flm(&executor, algorithm.init, profile->flash_start,
                      profile->algorithm_clock_hz, 3U, 1000U,
                      "FLM Init(Verify)", error, sizeof(error))) goto finish;
        initialized = true;
        initialized_operation = 3U;
        for (page = 0U; page < FIRMWARE_READER_PAGE_COUNT; page++)
        {
            uint32_t address;
            uint32_t page_offset;
            uint32_t result;
            if ((image.touched_pages & (1ULL << page)) == 0U) continue;
            if (cancelled()) goto finish;
            page_offset = page * FIRMWARE_READER_PAGE_SIZE;
            address = profile->flash_start + page_offset;
            swd_result = swd_target_write_memory(executor.page_buffer,
                                                 image.data + page_offset,
                                                 FIRMWARE_READER_PAGE_SIZE);
            if (swd_result != SWD_TARGET_OK)
            {
                strncpy(error, swd_target_status_text(swd_result), sizeof(error) - 1U);
                goto finish;
            }
            swd_result = flm_executor_call(&executor, algorithm.verify, address,
                                           FIRMWARE_READER_PAGE_SIZE,
                                           executor.page_buffer, 0U,
                                           algorithm.program_timeout_ms +
                                           DAP_PROGRAMMER_CALL_MARGIN, &result);
            if (swd_result != SWD_TARGET_OK)
            {
                strncpy(error, swd_target_status_text(swd_result), sizeof(error) - 1U);
                goto finish;
            }
            if (result != (address + FIRMWARE_READER_PAGE_SIZE))
            {
                (void)snprintf(error, sizeof(error), "FLM校验失败:0x%08lX",
                               (unsigned long)result);
                goto finish;
            }
            set_state(DAP_PROGRAMMER_VERIFYING, ++completed, total_pages,
                      "正在执行FLM校验");
        }
        if (!call_flm(&executor, algorithm.uninit, 3U, 0U, 0U, 1000U,
                      "FLM UnInit(Verify)", error, sizeof(error))) goto finish;
        initialized = false;
    }
    else
    {
        for (page = 0U; page < FIRMWARE_READER_PAGE_COUNT; page++)
        {
            uint32_t page_offset;
            uint32_t offset;
            if ((image.touched_pages & (1ULL << page)) == 0U) continue;
            if (cancelled()) goto finish;
            page_offset = page * FIRMWARE_READER_PAGE_SIZE;
            for (offset = 0U; offset < FIRMWARE_READER_PAGE_SIZE;
                 offset += sizeof(verify_data))
            {
                swd_result = swd_target_read_memory(profile->flash_start + page_offset + offset,
                                                    verify_data, sizeof(verify_data));
                if (swd_result != SWD_TARGET_OK)
                {
                    strncpy(error, swd_target_status_text(swd_result), sizeof(error) - 1U);
                    goto finish;
                }
                if (memcmp(verify_data, image.data + page_offset + offset,
                           sizeof(verify_data)) != 0)
                {
                    (void)snprintf(error, sizeof(error), "校验失败:0x%08lX",
                                   (unsigned long)(profile->flash_start + page_offset + offset));
                    goto finish;
                }
            }
            set_state(DAP_PROGRAMMER_VERIFYING, ++completed, total_pages, "正在回读校验");
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
        (void)flm_executor_call(&executor, algorithm.uninit, initialized_operation,
                                0U, 0U, 0U, 1000U, &ignored);
    }
    if (connected)
    {
        if ((programmer_status.state != DAP_PROGRAMMER_SUCCESS) && cancelled())
            (void)swd_target_reset();
        swd_target_disconnect();
    }
    if (programmer_status.state != DAP_PROGRAMMER_SUCCESS)
    {
        if (cancelled()) set_state(DAP_PROGRAMMER_CANCELLED, 0U, 0U, "烧录已取消");
        else set_state(DAP_PROGRAMMER_FAILED, 0U, 0U,
                       error[0] != '\0' ? error : "本地烧录失败");
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
