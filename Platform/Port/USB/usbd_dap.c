#include "usbd_dap.h"

#include <string.h>

#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"

#define USBD_DAP_CONFIG_DESC_SIZE 32U
#define USBD_DAP_OS_STRING_SIZE   18U

typedef struct
{
    uint8_t alternate_setting;
} usbd_dap_handle_t;

typedef struct
{
    uint8_t data[USBD_DAP_PACKET_SIZE];
    uint32_t length;
} usbd_dap_packet_t;

#define USBD_DAP_RAM __attribute__((section(".axisram"), aligned(32)))

static uint8_t usbd_dap_init(USBD_HandleTypeDef *device, uint8_t config_index);
static uint8_t usbd_dap_deinit(USBD_HandleTypeDef *device, uint8_t config_index);
static uint8_t usbd_dap_setup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request);
static uint8_t usbd_dap_data_in(USBD_HandleTypeDef *device, uint8_t endpoint);
static uint8_t usbd_dap_data_out(USBD_HandleTypeDef *device, uint8_t endpoint);
static uint8_t *usbd_dap_get_config_descriptor(uint16_t *length);
static uint8_t *usbd_dap_get_qualifier_descriptor(uint16_t *length);
static uint8_t *usbd_dap_get_os_string_descriptor(uint16_t *length);
static void usbd_dap_arm_receive(void);
static void usbd_dap_start_transmit(void);

static USBD_HandleTypeDef *dap_device;
static TaskHandle_t dap_consumer_task;
/* USB 缓冲固定放在 MPU 已配置为 non-cacheable 的 AXI SRAM。 */
static USBD_DAP_RAM usbd_dap_packet_t rx_queue[USBD_DAP_PACKET_COUNT];
static USBD_DAP_RAM usbd_dap_packet_t tx_queue[USBD_DAP_PACKET_COUNT];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;
static volatile uint8_t rx_count;
static volatile uint8_t rx_armed;
static volatile uint8_t tx_head;
static volatile uint8_t tx_tail;
static volatile uint8_t tx_count;
static volatile uint8_t tx_active;
static volatile uint8_t tx_zlp_pending; /* 零长度结束包（ZLP）待发送或正在发送。 */

__ALIGN_BEGIN static uint8_t dap_config_descriptor[USBD_DAP_CONFIG_DESC_SIZE] __ALIGN_END =
{
    0x09U, USB_DESC_TYPE_CONFIGURATION,
    LOBYTE(USBD_DAP_CONFIG_DESC_SIZE), HIBYTE(USBD_DAP_CONFIG_DESC_SIZE),
    0x01U, 0x01U, USBD_IDX_CONFIG_STR, 0x80U, 0xFAU,

    0x09U, USB_DESC_TYPE_INTERFACE,
    0x00U, 0x00U, 0x02U,
    0xFFU, 0x00U, 0x00U, USBD_IDX_INTERFACE_STR,

    0x07U, USB_DESC_TYPE_ENDPOINT, USBD_DAP_OUT_EP, 0x02U,
    LOBYTE(USBD_DAP_FS_MAX_PACKET_SIZE), HIBYTE(USBD_DAP_FS_MAX_PACKET_SIZE), 0x00U,

    0x07U, USB_DESC_TYPE_ENDPOINT, USBD_DAP_IN_EP, 0x02U,
    LOBYTE(USBD_DAP_FS_MAX_PACKET_SIZE), HIBYTE(USBD_DAP_FS_MAX_PACKET_SIZE), 0x00U
};

__ALIGN_BEGIN static uint8_t dap_qualifier_descriptor[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
    USB_LEN_DEV_QUALIFIER_DESC, USB_DESC_TYPE_DEVICE_QUALIFIER,
    0x00U, 0x02U, 0x00U, 0x00U, 0x00U, USB_MAX_EP0_SIZE, 0x01U, 0x00U
};

/* Microsoft OS 1.0 string descriptor. Vendor code 0xA0 requests WinUSB data. */
__ALIGN_BEGIN static uint8_t dap_os_string_descriptor[USBD_DAP_OS_STRING_SIZE] __ALIGN_END =
{
    USBD_DAP_OS_STRING_SIZE, USB_DESC_TYPE_STRING,
    'M', 0U, 'S', 0U, 'F', 0U, 'T', 0U, '1', 0U, '0', 0U,
    0xA0U, 0x00U
};

USBD_ClassTypeDef USBD_DAP =
{
    usbd_dap_init,
    usbd_dap_deinit,
    usbd_dap_setup,
    NULL,
    NULL,
    usbd_dap_data_in,
    usbd_dap_data_out,
    NULL,
    NULL,
    NULL,
    usbd_dap_get_config_descriptor,
    usbd_dap_get_config_descriptor,
    usbd_dap_get_config_descriptor,
    usbd_dap_get_qualifier_descriptor,
    usbd_dap_get_os_string_descriptor
};

static uint8_t usbd_dap_next(uint8_t index)
{
    index++;
    return (index == USBD_DAP_PACKET_COUNT) ? 0U : index;
}

static void usbd_dap_arm_receive(void)
{
    if ((dap_device == NULL) || (rx_armed != 0U) ||
        (rx_count >= USBD_DAP_PACKET_COUNT))
    {
        return;
    }

    rx_armed = 1U;
    if (USBD_LL_PrepareReceive(dap_device,
                               USBD_DAP_OUT_EP,
                               rx_queue[rx_head].data,
                               USBD_DAP_PACKET_SIZE) != USBD_OK)
    {
        rx_armed = 0U;
    }
}

static void usbd_dap_start_transmit(void)
{
    usbd_dap_packet_t *packet;

    if ((dap_device == NULL) || (tx_active != 0U) || (tx_count == 0U))
    {
        return;
    }

    packet = &tx_queue[tx_tail];
    tx_active = 1U;
    /* ZLP 阶段只发送零字节，不重复发送响应数据。 */
    if (USBD_LL_Transmit(dap_device,
                         USBD_DAP_IN_EP,
                         packet->data,
                         (tx_zlp_pending != 0U) ? 0U : packet->length) != USBD_OK)
    {
        /* 保留队列和 ZLP 状态，供后续启动时重试。 */
        tx_active = 0U;
    }
}

static uint8_t usbd_dap_init(USBD_HandleTypeDef *device, uint8_t config_index)
{
    usbd_dap_handle_t *handle;

    (void)config_index;
    handle = (usbd_dap_handle_t *)USBD_malloc(sizeof(usbd_dap_handle_t));
    if (handle == NULL)
    {
        return USBD_FAIL;
    }

    memset(handle, 0, sizeof(*handle));
    device->pClassData = handle;
    dap_device = device;
    rx_head = 0U;
    rx_tail = 0U;
    rx_count = 0U;
    rx_armed = 0U;
    tx_head = 0U;
    tx_tail = 0U;
    tx_count = 0U;
    tx_active = 0U;
    tx_zlp_pending = 0U; /* USB 重新初始化时清除未完成的 ZLP 状态。 */

    if ((USBD_LL_OpenEP(device,
                        USBD_DAP_OUT_EP,
                        USBD_EP_TYPE_BULK,
                        USBD_DAP_FS_MAX_PACKET_SIZE) != USBD_OK) ||
        (USBD_LL_OpenEP(device,
                        USBD_DAP_IN_EP,
                        USBD_EP_TYPE_BULK,
                        USBD_DAP_FS_MAX_PACKET_SIZE) != USBD_OK))
    {
        USBD_free(handle);
        device->pClassData = NULL;
        dap_device = NULL;
        return USBD_FAIL;
    }

    device->ep_out[USBD_DAP_OUT_EP & 0x0FU].is_used = 1U;
    device->ep_in[USBD_DAP_IN_EP & 0x0FU].is_used = 1U;
    usbd_dap_arm_receive();
    return USBD_OK;
}

static uint8_t usbd_dap_deinit(USBD_HandleTypeDef *device, uint8_t config_index)
{
    (void)config_index;
    (void)USBD_LL_CloseEP(device, USBD_DAP_OUT_EP);
    (void)USBD_LL_CloseEP(device, USBD_DAP_IN_EP);
    device->ep_out[USBD_DAP_OUT_EP & 0x0FU].is_used = 0U;
    device->ep_in[USBD_DAP_IN_EP & 0x0FU].is_used = 0U;

    if (device->pClassData != NULL)
    {
        USBD_free(device->pClassData);
        device->pClassData = NULL;
    }
    dap_device = NULL;
    return USBD_OK;
}

static uint8_t usbd_dap_setup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request)
{
    usbd_dap_handle_t *handle = (usbd_dap_handle_t *)device->pClassData;

    if (handle == NULL)
    {
        return USBD_FAIL;
    }

    if ((request->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_STANDARD)
    {
        switch (request->bRequest)
        {
            case USB_REQ_GET_INTERFACE:
                (void)USBD_CtlSendData(device, &handle->alternate_setting, 1U);
                return USBD_OK;

            case USB_REQ_SET_INTERFACE:
                handle->alternate_setting = (uint8_t)request->wValue;
                return USBD_OK;

            default:
                break;
        }
    }

    USBD_CtlError(device, request);
    return USBD_FAIL;
}

//修复 USB 回包结束处理：响应长度小于 512 字节、且为 64 字节整数倍时，补发零长度包（ZLP），
//让电脑及时判断响应结束，避免等待超时；等 ZLP 完成后再发送下一条响应。
static uint8_t usbd_dap_data_in(USBD_HandleTypeDef *device, uint8_t endpoint)
{
    (void)device;
    if ((endpoint & 0x0FU) != (USBD_DAP_IN_EP & 0x0FU))
    {
        return USBD_FAIL;
    }

    if ((tx_active != 0U) && (tx_count != 0U))
    {
        /* 响应未满逻辑包且长度为 USB 包大小的整数倍时，补发 ZLP，避免接收超时。
           完整逻辑包已填满主机缓冲，无需补发，以免影响下一次接收。 */
        if ((tx_zlp_pending == 0U) &&
            (tx_queue[tx_tail].length < USBD_DAP_PACKET_SIZE) &&
            ((tx_queue[tx_tail].length % USBD_DAP_FS_MAX_PACKET_SIZE) == 0U))
        {
            tx_zlp_pending = 1U;
            tx_active = 0U;
            usbd_dap_start_transmit();
            return (tx_active != 0U) ? USBD_OK : USBD_FAIL;
        }

        /* 无需 ZLP 或 ZLP 已完成后，才能出队并发送下一条响应。 */
        tx_tail = usbd_dap_next(tx_tail);
        tx_count--;
        tx_zlp_pending = 0U;
    }
    tx_active = 0U;
    usbd_dap_start_transmit();
    return USBD_OK;
}

static uint8_t usbd_dap_data_out(USBD_HandleTypeDef *device, uint8_t endpoint)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    uint32_t length;

    if ((endpoint & 0x0FU) != (USBD_DAP_OUT_EP & 0x0FU))
    {
        return USBD_FAIL;
    }

    rx_armed = 0U;
    length = USBD_LL_GetRxDataSize(device, USBD_DAP_OUT_EP);
    if ((length != 0U) && (rx_count < USBD_DAP_PACKET_COUNT))
    {
        if (length > USBD_DAP_PACKET_SIZE)
        {
            length = USBD_DAP_PACKET_SIZE;
        }
        rx_queue[rx_head].length = length;
        rx_head = usbd_dap_next(rx_head);
        rx_count++;

        if (dap_consumer_task != NULL)
        {
            vTaskNotifyGiveFromISR(dap_consumer_task, &higher_priority_task_woken);
        }
    }

    usbd_dap_arm_receive();
    portYIELD_FROM_ISR(higher_priority_task_woken);
    return USBD_OK;
}

static uint8_t *usbd_dap_get_config_descriptor(uint16_t *length)
{
    *length = sizeof(dap_config_descriptor);
    return dap_config_descriptor;
}

static uint8_t *usbd_dap_get_qualifier_descriptor(uint16_t *length)
{
    *length = sizeof(dap_qualifier_descriptor);
    return dap_qualifier_descriptor;
}

static uint8_t *usbd_dap_get_os_string_descriptor(uint16_t *length)
{
    *length = sizeof(dap_os_string_descriptor);
    return dap_os_string_descriptor;
}

void usbd_dap_bind_consumer_task(TaskHandle_t task)
{
    taskENTER_CRITICAL();
    dap_consumer_task = task;
    taskEXIT_CRITICAL();

    if ((task != NULL) && (rx_count != 0U))
    {
        xTaskNotifyGive(task);
    }
}

bool usbd_dap_receive(uint8_t *data,
                      uint32_t capacity,
                      uint32_t *length,
                      TickType_t timeout)
{
    uint32_t copy_length;

    if ((data == NULL) || (length == NULL) || (capacity == 0U))
    {
        return false;
    }

    if ((rx_count == 0U) && (ulTaskNotifyTake(pdTRUE, timeout) == 0U))
    {
        return false;
    }
    if (rx_count == 0U)
    {
        return false;
    }

    copy_length = rx_queue[rx_tail].length;
    if (copy_length > capacity)
    {
        copy_length = capacity;
    }
    memcpy(data, rx_queue[rx_tail].data, copy_length);

    taskENTER_CRITICAL();
    rx_tail = usbd_dap_next(rx_tail);
    rx_count--;
    usbd_dap_arm_receive();
    taskEXIT_CRITICAL();

    *length = copy_length;
    return true;
}

bool usbd_dap_transmit(const uint8_t *data, uint32_t length)
{
    usbd_dap_packet_t *packet;

    if ((data == NULL) || (length == 0U) ||
        (length > USBD_DAP_PACKET_SIZE) ||
        (tx_count >= USBD_DAP_PACKET_COUNT))
    {
        return false;
    }

    packet = &tx_queue[tx_head];
    memcpy(packet->data, data, length);
    packet->length = length;

    taskENTER_CRITICAL();
    tx_head = usbd_dap_next(tx_head);
    tx_count++;
    usbd_dap_start_transmit();
    taskEXIT_CRITICAL();
    return true;
}
