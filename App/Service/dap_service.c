#include "dap_service.h"

#include <stdint.h>
#include <string.h>

#include "DAP.h"
#include "DAP_config.h"
#include "bsp_dap.h"
#include "usbd_core.h"
#include "usbd_dap.h"
#include "usbd_desc.h"

static USBD_HandleTypeDef usb_device_fs;
static uint8_t dap_request[USBD_DAP_PACKET_SIZE];
static uint8_t dap_response[USBD_DAP_PACKET_SIZE];
static bool dap_connection_active;
static TickType_t dap_connection_start_tick;
static dap_service_status_t dap_status =
{
    .swd_clock_hz = DAP_DEFAULT_SWJ_CLOCK
};

static uint32_t dap_service_read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint32_t dap_service_ticks_to_ms(TickType_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000U) / configTICK_RATE_HZ);
}

static void dap_service_update_connection_state(void)
{
    TickType_t now = xTaskGetTickCount();
    bool connected = (DAP_Data.debug_port == DAP_PORT_SWD);

    taskENTER_CRITICAL();
    if (connected && !dap_connection_active)
    {
        dap_connection_active = true;
        dap_connection_start_tick = now;
        dap_status.connection_duration_ms = 0U;
    }
    else if (!connected && dap_connection_active)
    {
        dap_status.connection_duration_ms =
            dap_service_ticks_to_ms(now - dap_connection_start_tick);
        dap_connection_active = false;
        dap_connection_start_tick = 0U;
    }
    taskEXIT_CRITICAL();
}

static void dap_service_record_error(void)
{
    taskENTER_CRITICAL();
    dap_status.error_count++;
    taskEXIT_CRITICAL();
}

/* 检查命令的协议结果。USB 响应入队成功并不代表 SWD 访问成功。
   原子命令按整个包统计，包内任一命令失败即计为失败；迭代遍历避免递归。 */
static bool dap_service_command_succeeded(const uint8_t *request,
                                           uint32_t request_length,
                                           const uint8_t *response,
                                           uint32_t response_length,
                                           bool *dpidr_valid,
                                           uint32_t *dpidr)
{
    uint32_t pending = 1U;
    uint32_t request_offset = 0U;
    uint32_t response_offset = 0U;

    *dpidr_valid = false;

    while (pending-- != 0U)
    {
        const uint8_t *command;
        const uint8_t *reply;
        uint32_t request_available;
        uint32_t response_available;
        uint32_t request_size = 1U;
        uint32_t response_size = 2U;
        uint32_t count;
        bool has_status = true;

        if ((request_offset >= request_length) || (response_offset >= response_length))
        {
            return false;
        }
        command = request + request_offset;
        reply = response + response_offset;
        request_available = request_length - request_offset;
        response_available = response_length - response_offset;
        if (command[0] != reply[0])
        {
            return false;
        }
        /* 当前厂商命令模板只回显命令字，未定义额外参数或状态。 */
        if ((command[0] >= ID_DAP_Vendor0) && (command[0] <= ID_DAP_Vendor31))
        {
            request_offset++;
            response_offset++;
            continue;
        }
        if (response_available < 2U)
        {
            return false;
        }

        switch (command[0])
        {
            case ID_DAP_ExecuteCommands:
                if ((request_available < 2U) || (command[1] != reply[1]))
                {
                    return false;
                }
                pending += command[1];
                request_size = 2U;
                has_status = false;
                break;
            case ID_DAP_Info:
                request_size = 2U;
                response_size += reply[1];
                has_status = false;
                break;
            case ID_DAP_Connect:
                request_size = 2U;
                if (reply[1] != DAP_PORT_SWD)
                {
                    return false;
                }
                has_status = false;
                break;
            case ID_DAP_Transfer:
            {
                uint32_t response_data_offset = 3U;

                if ((request_available < 3U) || (response_available < 3U) ||
                    (command[2] != reply[1]) ||
                    ((reply[2] != DAP_TRANSFER_OK) &&
                     !((command[2] == 0U) && (reply[2] == 0U))))
                {
                    return false;
                }
                request_size = 3U;
                response_size = 3U;
                for (count = 0U; count < command[2]; count++)
                {
                    uint8_t flags;
                    if (request_size >= request_available)
                    {
                        return false;
                    }
                    flags = command[request_size++];
                    if (((flags & DAP_TRANSFER_RnW) == 0U) ||
                        ((flags & DAP_TRANSFER_MATCH_VALUE) != 0U))
                    {
                        request_size += 4U;
                    }
                    if (((flags & DAP_TRANSFER_RnW) != 0U) &&
                        ((flags & DAP_TRANSFER_MATCH_VALUE) == 0U))
                    {
                        if ((flags & DAP_TRANSFER_TIMESTAMP) != 0U)
                        {
                            response_data_offset += 4U;
                            response_size += 4U;
                        }
                        if ((response_data_offset + 4U) > response_available)
                        {
                            return false;
                        }
                        if ((flags & (DAP_TRANSFER_APnDP | DAP_TRANSFER_RnW |
                                      DAP_TRANSFER_A2 | DAP_TRANSFER_A3 |
                                      DAP_TRANSFER_MATCH_VALUE)) == DAP_TRANSFER_RnW)
                        {
                            *dpidr = dap_service_read_u32(reply + response_data_offset);
                            *dpidr_valid = true;
                        }
                        response_data_offset += 4U;
                        response_size += 4U;
                    }
                    else if ((flags & DAP_TRANSFER_TIMESTAMP) != 0U)
                    {
                        response_data_offset += 4U;
                        response_size += 4U;
                    }
                }
                has_status = false;
                break;
            }
            case ID_DAP_TransferBlock:
                if ((request_available < 5U) || (response_available < 4U))
                {
                    return false;
                }
                count = (uint32_t)command[2] | ((uint32_t)command[3] << 8);
                if ((reply[1] != command[2]) || (reply[2] != command[3]) ||
                    ((reply[3] != DAP_TRANSFER_OK) && !((count == 0U) && (reply[3] == 0U))))
                {
                    return false;
                }
                request_size = 5U;
                response_size = 4U;
                if ((command[4] & DAP_TRANSFER_RnW) != 0U)
                {
                    response_size += count * 4U;
                    if ((count != 0U) &&
                        ((command[4] & (DAP_TRANSFER_APnDP | DAP_TRANSFER_RnW |
                                        DAP_TRANSFER_A2 | DAP_TRANSFER_A3)) == DAP_TRANSFER_RnW) &&
                        (response_available >= 8U))
                    {
                        *dpidr = dap_service_read_u32(reply + 4U);
                        *dpidr_valid = true;
                    }
                }
                else
                {
                    request_size += count * 4U;
                }
                has_status = false;
                break;
            case ID_DAP_SWD_Sequence:
                if (request_available < 2U)
                {
                    return false;
                }
                request_size = 2U;
                for (count = 0U; count < command[1]; count++)
                {
                    uint8_t info;
                    uint32_t bytes;
                    if (request_size >= request_available)
                    {
                        return false;
                    }
                    info = command[request_size++];
                    bytes = info & SWD_SEQUENCE_CLK;
                    bytes = (bytes == 0U) ? 8U : ((bytes + 7U) / 8U);
                    if ((info & SWD_SEQUENCE_DIN) != 0U)
                    {
                        response_size += bytes;
                    }
                    else
                    {
                        request_size += bytes;
                    }
                }
                break;
            case ID_DAP_SWJ_Sequence:
                if (request_available < 2U)
                {
                    return false;
                }
                request_size = 2U + ((command[1] == 0U) ? 32U : ((command[1] + 7U) / 8U));
                break;
            case ID_DAP_SWJ_Pins:
                request_size = 7U;
                has_status = false;
                break;
            case ID_DAP_HostStatus:
            case ID_DAP_Delay:
                request_size = 3U;
                break;
            case ID_DAP_TransferConfigure:
            case ID_DAP_WriteABORT:
                request_size = 6U;
                break;
            case ID_DAP_SWJ_Clock:
                request_size = 5U;
                break;
            case ID_DAP_SWD_Configure:
                request_size = 2U;
                break;
            case ID_DAP_ResetTarget:
                response_size = 3U;
                break;
            case ID_DAP_Disconnect:
                break;
            default:
                /* 当前固件只启用 SWD；未支持的命令不能算作成功。 */
                return false;
        }

        if ((request_size > request_available) || (response_size > response_available) ||
            (has_status && (reply[1] != DAP_OK)))
        {
            return false;
        }
        request_offset += request_size;
        response_offset += response_size;
    }
    return response_offset == response_length;
}

bool dap_service_init(void)
{
    usbd_dap_bind_consumer_task(xTaskGetCurrentTaskHandle());

    if (bsp_dap_init() != BSP_OK)
    {
        dap_service_record_error();
        return false;
    }
    if (USBD_Init(&usb_device_fs, &FS_Desc, DEVICE_FS) != USBD_OK)
    {
        dap_service_record_error();
        return false;
    }
    if (USBD_RegisterClass(&usb_device_fs, &USBD_DAP) != USBD_OK)
    {
        dap_service_record_error();
        return false;
    }
    if (USBD_Start(&usb_device_fs) != USBD_OK)
    {
        dap_service_record_error();
        return false;
    }

    taskENTER_CRITICAL();
    dap_status.initialized = true;
    taskEXIT_CRITICAL();
    return true;
}

void dap_service_process(TickType_t timeout)
{
    uint32_t request_length;
    uint32_t result;
    uint32_t response_length;
    bool succeeded;
    bool dpidr_valid;
    uint32_t dpidr;

    if (!usbd_dap_receive(dap_request,
                          sizeof(dap_request),
                          &request_length,
                          timeout))
    {
        return;
    }

    taskENTER_CRITICAL();
    dap_status.command_count++;
    dap_status.communication_bytes += request_length;
    dap_status.last_command = dap_request[0];
    dap_status.command_active = true;
    taskEXIT_CRITICAL();

    result = bsp_dap_execute_command(dap_request, dap_response);
    response_length = result & 0xFFFFU;
    dap_service_update_connection_state();

    taskENTER_CRITICAL();
    if ((dap_request[0] == ID_DAP_SWJ_Clock) &&
        (response_length >= 2U) &&
        (dap_response[1] == DAP_OK))
    {
        dap_status.swd_clock_hz = (uint32_t)dap_request[1] |
                                  ((uint32_t)dap_request[2] << 8) |
                                  ((uint32_t)dap_request[3] << 16) |
                                  ((uint32_t)dap_request[4] << 24);
    }
    taskEXIT_CRITICAL();

    if ((response_length == 0U) || (response_length > sizeof(dap_response)))
    {
        dap_service_record_error();
        taskENTER_CRITICAL();
        dap_status.failure_count++;
        dap_status.command_active = false;
        taskEXIT_CRITICAL();
        return;
    }

    succeeded = dap_service_command_succeeded(dap_request, request_length,
                                               dap_response, response_length,
                                               &dpidr_valid, &dpidr);

    /* IN 队列短暂占满时主动让出 CPU，避免忙等拖慢 LVGL。 */
    while (!usbd_dap_transmit(dap_response, response_length))
    {
        vTaskDelay(pdMS_TO_TICKS(1U));
    }

    taskENTER_CRITICAL();
    dap_status.response_count++;
    dap_status.communication_bytes += response_length;
    if (succeeded)
    {
        dap_status.success_count++;
        if (dpidr_valid)
        {
            dap_status.dpidr = dpidr;
            dap_status.dpidr_valid = true;
        }
    }
    else
    {
        dap_status.failure_count++;
    }
    dap_status.command_active = false;
    taskEXIT_CRITICAL();
}

bool dap_service_get_status(dap_service_status_t *status)
{
    TickType_t now;

    if (status == NULL)
    {
        return false;
    }

    /* 同步当前 DAP 端口状态，避免连接状态在命令统计之外变化时漏记起点。 */
    dap_service_update_connection_state();
    now = xTaskGetTickCount();
    /* USB 中断和 DAP 任务都会改变状态，短临界区保证 UI 得到一致快照。 */
    taskENTER_CRITICAL();
    memcpy(status, &dap_status, sizeof(*status));
    status->usb_configured = (usb_device_fs.dev_state == USBD_STATE_CONFIGURED);
    status->debug_port = DAP_Data.debug_port;
    if (dap_connection_active)
    {
        TickType_t elapsed = now - dap_connection_start_tick;
        status->connection_duration_ms = dap_service_ticks_to_ms(elapsed);
    }
    taskEXIT_CRITICAL();

    return true;
}

void dap_service_set_local_active(bool active)
{
    taskENTER_CRITICAL();
    dap_status.local_active = active;
    dap_status.command_active = false;
    taskEXIT_CRITICAL();
}

void dap_service_flush_requests(void)
{
    uint32_t request_length;

    while (usbd_dap_receive(dap_request, sizeof(dap_request), &request_length, 0U))
    {
        taskENTER_CRITICAL();
        dap_status.failure_count++;
        dap_status.communication_bytes += request_length;
        taskEXIT_CRITICAL();
    }
    /* 丢弃队列时一并清除对应通知计数，避免退出本地模式后空唤醒。 */
    (void)ulTaskNotifyTake(pdTRUE, 0U);
}
