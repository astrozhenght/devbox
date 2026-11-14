#include "flm_executor.h"

#include <string.h>

#define FLM_STACK_SIZE       1024U
#define FLM_VERIFY_CHUNK     128U
#define FLM_REGISTER_R0      0U
#define FLM_REGISTER_R1      1U
#define FLM_REGISTER_R2      2U
#define FLM_REGISTER_R3      3U
#define FLM_REGISTER_R9      9U
#define FLM_REGISTER_SP      13U
#define FLM_REGISTER_LR      14U
#define FLM_REGISTER_PC      15U
#define FLM_REGISTER_XPSR    16U
#define FLM_REGISTER_CFBP    20U

static uint8_t verify_buffer[FLM_VERIFY_CHUNK]
    __attribute__((section(".axisram"), aligned(32)));
static const uint8_t zero_buffer[FLM_VERIFY_CHUNK] = {0};

static swd_target_status_t write_and_verify(uint32_t address, const uint8_t *data,
                                            uint32_t size)
{
    uint32_t offset = 0U;
    swd_target_status_t status;

    status = swd_target_write_memory(address, data, size);
    if (status != SWD_TARGET_OK) return status;
    while (offset < size)
    {
        uint32_t amount = size - offset;
        if (amount > sizeof(verify_buffer)) amount = sizeof(verify_buffer);
        status = swd_target_read_memory(address + offset, verify_buffer, amount);
        if (status != SWD_TARGET_OK) return status;
        if (memcmp(verify_buffer, data + offset, amount) != 0)
            return SWD_TARGET_VERIFY_ERROR;
        offset += amount;
    }
    return SWD_TARGET_OK;
}

swd_target_status_t flm_executor_load(flm_executor_t *executor,
                                      const flm_algorithm_t *algorithm,
                                      const target_profile_t *profile)
{
    if (profile == NULL) return SWD_TARGET_ARGUMENT;
    return flm_executor_load_at(executor, algorithm, profile->ram_start, profile->ram_size);
}

swd_target_status_t flm_executor_load_at(flm_executor_t *executor,
                                         const flm_algorithm_t *algorithm,
                                         uint32_t ram_start, uint32_t ram_size)
{
    uint64_t code_end;
    uint64_t page_buffer;
    uint64_t stack_pointer;
    uint64_t stack_base;
    uint32_t index;
    swd_target_status_t status;
    const uint32_t return_stub = 0xE7FEBE00U; /* BKPT 0; B . */

    if ((executor == NULL) || (algorithm == NULL) || (algorithm->page_size == 0U) ||
        (algorithm->image_size == 0U) || (ram_size <= FLM_STACK_SIZE) ||
        ((ram_start & 7U) != 0U) || (ram_start > UINT32_MAX - ram_size) ||
        (algorithm->static_base_offset >= algorithm->image_size) ||
        (algorithm->section_count > FLM_MAX_LOAD_SECTIONS))
        return SWD_TARGET_ARGUMENT;

    /* 先用64位计算完整布局，验证后再转成目标地址，防止回绕绕过容量检查。 */
    code_end = ((uint64_t)ram_start + algorithm->image_size + 3U) & ~UINT64_C(3);
    page_buffer = (code_end + sizeof(return_stub) + 7U) & ~UINT64_C(7);
    stack_pointer = ((uint64_t)ram_start + ram_size) & ~UINT64_C(7);
    stack_base = stack_pointer - FLM_STACK_SIZE;
    if ((stack_base < ram_start) ||
        ((page_buffer + algorithm->page_size) > stack_base))
        return SWD_TARGET_ARGUMENT;
    for (index = 0U; index < algorithm->section_count; index++)
    {
        const flm_load_section_t *section = &algorithm->sections[index];
        if ((section->address > algorithm->image_size) ||
            (section->size > algorithm->image_size - section->address))
            return SWD_TARGET_ARGUMENT;
    }
    memset(executor, 0, sizeof(*executor));
    executor->algorithm = algorithm;
    executor->load_address = ram_start;
    executor->static_base = executor->load_address + algorithm->static_base_offset;
    executor->return_address = (uint32_t)code_end;
    executor->page_buffer = (uint32_t)page_buffer;
    executor->stack_pointer = (uint32_t)stack_pointer;

    status = swd_target_halt(1000U);
    if (status != SWD_TARGET_OK) return status;
    for (index = 0U; index < algorithm->section_count; index++)
    {
        const flm_load_section_t *section = &algorithm->sections[index];
        uint32_t target_address = executor->load_address + section->address;
        if (section->type == 1U)
        {
            status = write_and_verify(target_address,
                                      algorithm->file_data + section->file_offset,
                                      section->size);
            if (status != SWD_TARGET_OK) return status;
        }
        else
        {
            uint32_t remaining = section->size;
            while (remaining != 0U)
            {
                uint32_t amount = remaining;
                if (amount > sizeof(zero_buffer)) amount = sizeof(zero_buffer);
                status = swd_target_write_memory(target_address, zero_buffer, amount);
                if (status != SWD_TARGET_OK) return status;
                target_address += amount;
                remaining -= amount;
            }
        }
    }
    return swd_target_write32(executor->return_address, return_stub);
}

swd_target_status_t flm_executor_call(const flm_executor_t *executor,
                                      uint32_t function_offset,
                                      uint32_t argument0,
                                      uint32_t argument1,
                                      uint32_t argument2,
                                      uint32_t argument3,
                                      uint32_t timeout_ms,
                                      uint32_t *return_value)
{
    swd_target_status_t status;
    uint32_t pc;
    uint32_t function_address;

    if ((executor == NULL) || (executor->algorithm == NULL) ||
        (return_value == NULL) || (function_offset >= executor->algorithm->image_size))
        return SWD_TARGET_ARGUMENT;
    function_address = executor->load_address + (function_offset & ~1U);
    status = swd_target_halt(1000U);
    if (status != SWD_TARGET_OK) return status;
#define WRITE_REGISTER(selector, value) do { \
    status = swd_target_write_core_register((selector), (value), 100U); \
    if (status != SWD_TARGET_OK) return status; \
} while (0)
    WRITE_REGISTER(FLM_REGISTER_R0, argument0);
    WRITE_REGISTER(FLM_REGISTER_R1, argument1);
    WRITE_REGISTER(FLM_REGISTER_R2, argument2);
    WRITE_REGISTER(FLM_REGISTER_R3, argument3);
    WRITE_REGISTER(FLM_REGISTER_R9, executor->static_base);
    WRITE_REGISTER(FLM_REGISTER_SP, executor->stack_pointer);
    WRITE_REGISTER(FLM_REGISTER_LR, executor->return_address | 1U);
    WRITE_REGISTER(FLM_REGISTER_PC, function_address);
    WRITE_REGISTER(FLM_REGISTER_XPSR, 0x01000000U);
    WRITE_REGISTER(FLM_REGISTER_CFBP, 0U);
#undef WRITE_REGISTER
    status = swd_target_resume();
    if (status != SWD_TARGET_OK) return status;
    status = swd_target_wait_halted(timeout_ms);
    if (status != SWD_TARGET_OK)
    {
        (void)swd_target_halt(100U);
        return status;
    }
    status = swd_target_read_core_register(FLM_REGISTER_PC, &pc, 100U);
    if (status != SWD_TARGET_OK) return status;
    if ((pc < executor->return_address) || (pc > (executor->return_address + 4U)))
        return SWD_TARGET_CORE_FAULT;
    return swd_target_read_core_register(FLM_REGISTER_R0, return_value, 100U);
}
