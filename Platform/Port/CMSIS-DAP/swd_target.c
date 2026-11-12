#include "swd_target.h"

#include <string.h>

#include "DAP.h"
#include "FreeRTOS.h"
#include "bsp_dap.h"
#include "task.h"

#define SWD_PACKET_SIZE          512U
#define SWD_BLOCK_WORDS          120U
#define SWD_DP_POWER_REQUEST     0x50000000U
#define SWD_DP_POWER_ACK         0xA0000000U
#define SWD_AP_CSW_WORD_INC      0x23000052U
#define SWD_DHCSR                0xE000EDF0U
#define SWD_DCRSR                0xE000EDF4U
#define SWD_DCRDR                0xE000EDF8U
#define SWD_DEMCR                0xE000EDFCU
#define SWD_AIRCR                0xE000ED0CU
#define SWD_DHCSR_KEY            0xA05F0000U
#define SWD_DHCSR_C_DEBUGEN      (1U << 0)
#define SWD_DHCSR_C_HALT         (1U << 1)
#define SWD_DHCSR_S_REGRDY       (1U << 16)
#define SWD_DHCSR_S_HALT         (1U << 17)
#define SWD_DCRSR_REGWNR         (1U << 16)

static uint8_t swd_request[SWD_PACKET_SIZE];
static uint8_t swd_response[SWD_PACKET_SIZE];

static void put_u32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
    target[2] = (uint8_t)(value >> 16);
    target[3] = (uint8_t)(value >> 24);
}

static uint32_t get_u32(const uint8_t *source)
{
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static bool execute(uint32_t request_size, uint32_t minimum_response)
{
    uint32_t result;
    uint32_t response_size;

    (void)request_size;
    result = bsp_dap_execute_command(swd_request, swd_response);
    response_size = result & 0xFFFFU;
    return (response_size >= minimum_response) &&
           (response_size <= sizeof(swd_response)) &&
           (swd_response[0] == swd_request[0]);
}

static swd_target_status_t simple_command(uint8_t command, const uint8_t *data,
                                          uint32_t data_size)
{
    swd_request[0] = command;
    if (data_size != 0U) memcpy(swd_request + 1U, data, data_size);
    if (!execute(data_size + 1U, 2U) || (swd_response[1] != DAP_OK))
        return SWD_TARGET_DAP_ERROR;
    return SWD_TARGET_OK;
}

static swd_target_status_t transfer_register(bool ap, bool read, uint8_t address,
                                             uint32_t *value)
{
    uint8_t request_value = (uint8_t)(address & (DAP_TRANSFER_A2 | DAP_TRANSFER_A3));

    if (ap) request_value |= DAP_TRANSFER_APnDP;
    if (read) request_value |= DAP_TRANSFER_RnW;
    swd_request[0] = ID_DAP_Transfer;
    swd_request[1] = 0U;
    swd_request[2] = 1U;
    swd_request[3] = request_value;
    if (!read)
    {
        if (value == NULL) return SWD_TARGET_ARGUMENT;
        put_u32(swd_request + 4U, *value);
    }
    if (!execute(read ? 4U : 8U, read ? 7U : 3U)) return SWD_TARGET_DAP_ERROR;
    if ((swd_response[1] != 1U) || (swd_response[2] != DAP_TRANSFER_OK))
        return (swd_response[2] == 7U) ? SWD_TARGET_NO_ACK : SWD_TARGET_DAP_ERROR;
    if (read)
    {
        if (value == NULL) return SWD_TARGET_ARGUMENT;
        *value = get_u32(swd_response + 3U);
    }
    return SWD_TARGET_OK;
}

static swd_target_status_t dp_write(uint8_t address, uint32_t value)
{
    return transfer_register(false, false, address, &value);
}

static swd_target_status_t dp_read(uint8_t address, uint32_t *value)
{
    return transfer_register(false, true, address, value);
}

static swd_target_status_t ap_write(uint8_t address, uint32_t value)
{
    return transfer_register(true, false, address, &value);
}

static swd_target_status_t configure_memory_access(uint32_t address)
{
    swd_target_status_t status;

    status = dp_write(DP_SELECT, 0U);
    if (status != SWD_TARGET_OK) return status;
    status = ap_write(0x00U, SWD_AP_CSW_WORD_INC);
    if (status != SWD_TARGET_OK) return status;
    return ap_write(0x04U, address);
}

static swd_target_status_t block_words(uint32_t address, uint32_t *words,
                                       uint32_t count, bool read)
{
    swd_target_status_t status;
    uint32_t index;

    if ((words == NULL) || (count == 0U) || (count > SWD_BLOCK_WORDS))
        return SWD_TARGET_ARGUMENT;
    status = configure_memory_access(address);
    if (status != SWD_TARGET_OK) return status;
    swd_request[0] = ID_DAP_TransferBlock;
    swd_request[1] = 0U;
    swd_request[2] = (uint8_t)count;
    swd_request[3] = (uint8_t)(count >> 8);
    swd_request[4] = DAP_TRANSFER_APnDP | DAP_TRANSFER_A2 | DAP_TRANSFER_A3;
    if (read) swd_request[4] |= DAP_TRANSFER_RnW;
    if (!read)
    {
        for (index = 0U; index < count; index++) put_u32(swd_request + 5U + index * 4U,
                                                         words[index]);
    }
    if (!execute(read ? 5U : (5U + count * 4U), read ? (4U + count * 4U) : 4U))
        return SWD_TARGET_DAP_ERROR;
    if ((((uint32_t)swd_response[1] | ((uint32_t)swd_response[2] << 8)) != count) ||
        (swd_response[3] != DAP_TRANSFER_OK))
        return (swd_response[3] == 7U) ? SWD_TARGET_NO_ACK : SWD_TARGET_DAP_ERROR;
    if (read)
    {
        for (index = 0U; index < count; index++) words[index] =
            get_u32(swd_response + 4U + index * 4U);
    }
    return SWD_TARGET_OK;
}

static swd_target_status_t aligned_transfer(uint32_t address, uint8_t *data,
                                            size_t size, bool read)
{
    uint32_t words[SWD_BLOCK_WORDS];

    while (size != 0U)
    {
        uint32_t count = (uint32_t)(size / 4U);
        uint32_t boundary = (0x400U - (address & 0x3FFU)) / 4U;
        uint32_t index;
        swd_target_status_t status;
        if (count > SWD_BLOCK_WORDS) count = SWD_BLOCK_WORDS;
        if (count > boundary) count = boundary;
        if (!read)
        {
            for (index = 0U; index < count; index++)
                memcpy(&words[index], data + index * 4U, sizeof(uint32_t));
        }
        status = block_words(address, words, count, read);
        if (status != SWD_TARGET_OK) return status;
        if (read)
        {
            for (index = 0U; index < count; index++)
                memcpy(data + index * 4U, &words[index], sizeof(uint32_t));
        }
        address += count * 4U;
        data += count * 4U;
        size -= count * 4U;
    }
    return SWD_TARGET_OK;
}

static swd_target_status_t wait_dhcsr(uint32_t mask, uint32_t expected, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);

    for (;;)
    {
        uint32_t value;
        swd_target_status_t status = swd_target_read32(SWD_DHCSR, &value);
        if (status != SWD_TARGET_OK) return status;
        if ((value & mask) == expected) return SWD_TARGET_OK;
        if ((xTaskGetTickCount() - start) >= timeout) return SWD_TARGET_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

swd_target_status_t swd_target_connect(uint32_t clock_hz, uint32_t *dpidr)
{
    uint8_t data[5];
    uint32_t value;
    swd_target_status_t status;
    uint32_t retry;
    static const uint8_t ones[7] = {0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU};
    static const uint8_t switch_sequence[2] = {0x9EU, 0xE7U};
    static const uint8_t zeros[1] = {0x00U};

    if ((clock_hz == 0U) || (dpidr == NULL)) return SWD_TARGET_ARGUMENT;
    data[0] = DAP_PORT_SWD;
    swd_request[0] = ID_DAP_Connect;
    swd_request[1] = DAP_PORT_SWD;
    if (!execute(2U, 2U) || (swd_response[1] != DAP_PORT_SWD))
        return SWD_TARGET_DAP_ERROR;
    put_u32(data, clock_hz);
    status = simple_command(ID_DAP_SWJ_Clock, data, 4U);
    if (status != SWD_TARGET_OK) return status;
    data[0] = 0U;
    status = simple_command(ID_DAP_SWD_Configure, data, 1U);
    if (status != SWD_TARGET_OK) return status;
    data[0] = 0U;
    data[1] = 100U;
    data[2] = 0U;
    data[3] = 0U;
    data[4] = 0U;
    status = simple_command(ID_DAP_TransferConfigure, data, 5U);
    if (status != SWD_TARGET_OK) return status;

    swd_request[0] = ID_DAP_SWJ_Sequence;
    swd_request[1] = 56U;
    memcpy(swd_request + 2U, ones, sizeof(ones));
    if (!execute(9U, 2U) || (swd_response[1] != DAP_OK)) return SWD_TARGET_DAP_ERROR;
    swd_request[1] = 16U;
    memcpy(swd_request + 2U, switch_sequence, sizeof(switch_sequence));
    if (!execute(4U, 2U) || (swd_response[1] != DAP_OK)) return SWD_TARGET_DAP_ERROR;
    swd_request[1] = 56U;
    memcpy(swd_request + 2U, ones, sizeof(ones));
    if (!execute(9U, 2U) || (swd_response[1] != DAP_OK)) return SWD_TARGET_DAP_ERROR;
    swd_request[1] = 8U;
    memcpy(swd_request + 2U, zeros, sizeof(zeros));
    if (!execute(3U, 2U) || (swd_response[1] != DAP_OK)) return SWD_TARGET_DAP_ERROR;

    status = dp_read(DP_IDCODE, dpidr);
    if (status != SWD_TARGET_OK) return status;
    status = dp_write(DP_ABORT, 0x1EU);
    if (status != SWD_TARGET_OK) return status;
    status = dp_write(DP_CTRL_STAT, SWD_DP_POWER_REQUEST);
    if (status != SWD_TARGET_OK) return status;
    for (retry = 0U; retry < 100U; retry++)
    {
        status = dp_read(DP_CTRL_STAT, &value);
        if (status != SWD_TARGET_OK) return status;
        if ((value & SWD_DP_POWER_ACK) == SWD_DP_POWER_ACK) return SWD_TARGET_OK;
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    return SWD_TARGET_TIMEOUT;
}

void swd_target_disconnect(void)
{
    uint8_t unused = 0U;
    (void)simple_command(ID_DAP_Disconnect, &unused, 0U);
}

swd_target_status_t swd_target_read_memory(uint32_t address, void *data, size_t size)
{
    uint8_t *output = (uint8_t *)data;
    uint32_t aligned;
    uint32_t word;
    size_t part;
    swd_target_status_t status;

    if ((data == NULL) || (size == 0U)) return SWD_TARGET_ARGUMENT;
    while ((size != 0U) && ((address & 3U) != 0U))
    {
        aligned = address & ~3U;
        status = swd_target_read32(aligned, &word);
        if (status != SWD_TARGET_OK) return status;
        part = 4U - (address & 3U);
        if (part > size) part = size;
        memcpy(output, ((uint8_t *)&word) + (address & 3U), part);
        address += (uint32_t)part;
        output += part;
        size -= part;
    }
    part = size & ~(size_t)3U;
    if (part != 0U)
    {
        status = aligned_transfer(address, output, part, true);
        if (status != SWD_TARGET_OK) return status;
        address += (uint32_t)part;
        output += part;
        size -= part;
    }
    if (size != 0U)
    {
        status = swd_target_read32(address, &word);
        if (status != SWD_TARGET_OK) return status;
        memcpy(output, &word, size);
    }
    return SWD_TARGET_OK;
}

swd_target_status_t swd_target_write_memory(uint32_t address, const void *data, size_t size)
{
    const uint8_t *input = (const uint8_t *)data;
    uint32_t aligned;
    uint32_t word;
    size_t part;
    swd_target_status_t status;

    if ((data == NULL) || (size == 0U)) return SWD_TARGET_ARGUMENT;
    while ((size != 0U) && ((address & 3U) != 0U))
    {
        aligned = address & ~3U;
        status = swd_target_read32(aligned, &word);
        if (status != SWD_TARGET_OK) return status;
        part = 4U - (address & 3U);
        if (part > size) part = size;
        memcpy(((uint8_t *)&word) + (address & 3U), input, part);
        status = swd_target_write32(aligned, word);
        if (status != SWD_TARGET_OK) return status;
        address += (uint32_t)part;
        input += part;
        size -= part;
    }
    part = size & ~(size_t)3U;
    if (part != 0U)
    {
        status = aligned_transfer(address, (uint8_t *)(uintptr_t)input, part, false);
        if (status != SWD_TARGET_OK) return status;
        address += (uint32_t)part;
        input += part;
        size -= part;
    }
    if (size != 0U)
    {
        status = swd_target_read32(address, &word);
        if (status != SWD_TARGET_OK) return status;
        memcpy(&word, input, size);
        status = swd_target_write32(address, word);
        if (status != SWD_TARGET_OK) return status;
    }
    return SWD_TARGET_OK;
}

swd_target_status_t swd_target_read32(uint32_t address, uint32_t *value)
{
    if ((value == NULL) || ((address & 3U) != 0U)) return SWD_TARGET_ARGUMENT;
    return block_words(address, value, 1U, true);
}

swd_target_status_t swd_target_write32(uint32_t address, uint32_t value)
{
    if ((address & 3U) != 0U) return SWD_TARGET_ARGUMENT;
    return block_words(address, &value, 1U, false);
}

swd_target_status_t swd_target_halt(uint32_t timeout_ms)
{
    swd_target_status_t status = swd_target_write32(SWD_DHCSR,
        SWD_DHCSR_KEY | SWD_DHCSR_C_DEBUGEN | SWD_DHCSR_C_HALT);
    if (status != SWD_TARGET_OK) return status;
    return wait_dhcsr(SWD_DHCSR_S_HALT, SWD_DHCSR_S_HALT, timeout_ms);
}

swd_target_status_t swd_target_wait_halted(uint32_t timeout_ms)
{
    return wait_dhcsr(SWD_DHCSR_S_HALT, SWD_DHCSR_S_HALT, timeout_ms);
}

swd_target_status_t swd_target_resume(void)
{
    return swd_target_write32(SWD_DHCSR, SWD_DHCSR_KEY | SWD_DHCSR_C_DEBUGEN);
}

swd_target_status_t swd_target_reset(void)
{
    return swd_target_write32(SWD_AIRCR, 0x05FA0004U);
}

swd_target_status_t swd_target_reset_and_halt(uint32_t timeout_ms)
{
    swd_target_status_t status;

    status = swd_target_resume();
    if (status != SWD_TARGET_OK) return status;
    status = swd_target_write32(SWD_DEMCR, 1U);
    if (status != SWD_TARGET_OK) return status;
    status = swd_target_reset();
    if (status == SWD_TARGET_OK)
        status = swd_target_wait_halted(timeout_ms);
    (void)swd_target_write32(SWD_DEMCR, 0U);
    return status;
}

swd_target_status_t swd_target_read_core_register(uint32_t selector, uint32_t *value,
                                                  uint32_t timeout_ms)
{
    swd_target_status_t status;
    if (value == NULL) return SWD_TARGET_ARGUMENT;
    status = swd_target_write32(SWD_DCRSR, selector & 0x7FU);
    if (status != SWD_TARGET_OK) return status;
    status = wait_dhcsr(SWD_DHCSR_S_REGRDY, SWD_DHCSR_S_REGRDY, timeout_ms);
    if (status != SWD_TARGET_OK) return status;
    return swd_target_read32(SWD_DCRDR, value);
}

swd_target_status_t swd_target_write_core_register(uint32_t selector, uint32_t value,
                                                   uint32_t timeout_ms)
{
    swd_target_status_t status;
    status = swd_target_write32(SWD_DCRDR, value);
    if (status != SWD_TARGET_OK) return status;
    status = swd_target_write32(SWD_DCRSR, (selector & 0x7FU) | SWD_DCRSR_REGWNR);
    if (status != SWD_TARGET_OK) return status;
    return wait_dhcsr(SWD_DHCSR_S_REGRDY, SWD_DHCSR_S_REGRDY, timeout_ms);
}

const char *swd_target_status_text(swd_target_status_t status)
{
    switch (status)
    {
        case SWD_TARGET_OK: return "SWD正常";
        case SWD_TARGET_ARGUMENT: return "SWD参数无效";
        case SWD_TARGET_DAP_ERROR: return "SWD传输失败";
        case SWD_TARGET_NO_ACK: return "目标未响应";
        case SWD_TARGET_TIMEOUT: return "目标操作超时";
        case SWD_TARGET_CORE_FAULT: return "目标算法异常";
        case SWD_TARGET_VERIFY_ERROR: return "目标校验失败";
        default: return "未知SWD错误";
    }
}
