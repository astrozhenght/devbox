#include "bsp_wifi.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define WIFI_FIFO_SIZE 4096U
static uint8_t wifi_dma_buffer[WIFI_RX_BUFFER_SIZE] __attribute__((section(".sram123"),aligned(32)));
static uint8_t wifi_fifo[WIFI_FIFO_SIZE] __attribute__((section(".axisram")));
static char response[2048];
static volatile uint16_t fifo_head, fifo_tail;
static volatile bool fifo_overflow;
static uint16_t dma_position;

bsp_wifi_status_t bsp_wifi_init(void)
{
    HAL_StatusTypeDef status = platform_usart6_init();
    platform_usart6_disable();
    return status == HAL_OK ? BSP_WIFI_OK : BSP_WIFI_INIT_ERROR;
}
void bsp_wifi_flush_rx(void)
{
    uint32_t mask = __get_PRIMASK(); __disable_irq();
    fifo_tail = fifo_head; fifo_overflow = false;
    __set_PRIMASK(mask);
}
bsp_wifi_status_t bsp_wifi_start_receive(void)
{
    platform_usart6_enable(); bsp_wifi_flush_rx(); dma_position = 0;
    return HAL_UARTEx_ReceiveToIdle_DMA(&huart6,wifi_dma_buffer,WIFI_RX_BUFFER_SIZE) == HAL_OK ?
        BSP_WIFI_OK : BSP_WIFI_TRANSPORT_ERROR;
}
uint8_t *bsp_wifi_get_rx_buffer(void) { return wifi_dma_buffer; }
const char *bsp_wifi_get_response(void) { return response; }
void bsp_wifi_set_rx_data_ready(uint8_t ready) { (void)ready; }
static void fifo_write(const uint8_t *data,size_t length)
{
    for (size_t i = 0; i < length; i++) {
        uint16_t next = (fifo_head + 1U) % WIFI_FIFO_SIZE;
        if (next == fifo_tail) {fifo_overflow = true;return;}
        wifi_fifo[fifo_head] = data[i]; __DMB(); fifo_head = next;
    }
}
/* HT、TC、IDLE的position均为本轮DMA末端位置，只搬新增部分。 */
void bsp_wifi_on_rx_event(uint16_t position)
{
    if (!position || position > WIFI_RX_BUFFER_SIZE || position == dma_position) return;
    uint32_t mask = __get_PRIMASK(); __disable_irq();
    if (position < dma_position) {
        fifo_write(wifi_dma_buffer + dma_position,WIFI_RX_BUFFER_SIZE - dma_position);
        dma_position = 0;
    }
    fifo_write(wifi_dma_buffer + dma_position,position - dma_position);
    dma_position = position;
    __set_PRIMASK(mask);
}
void bsp_wifi_copy_buffer_to_ring(uint16_t length) { bsp_wifi_on_rx_event(length); }
void bsp_wifi_on_rx_error(void)
{
    fifo_overflow = true; dma_position = 0;
    (void)HAL_UART_AbortReceive(&huart6);
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart6,wifi_dma_buffer,WIFI_RX_BUFFER_SIZE);
}
bsp_wifi_status_t bsp_wifi_read(uint8_t *dest,size_t capacity,size_t *length)
{
    if (!dest || !length) return BSP_WIFI_TRANSPORT_ERROR;
    uint32_t mask = __get_PRIMASK(); __disable_irq();
    *length = 0;
    while (*length < capacity && fifo_tail != fifo_head) {
        dest[(*length)++] = wifi_fifo[fifo_tail]; fifo_tail = (fifo_tail + 1U) % WIFI_FIFO_SIZE;
    }
    bool overflow = fifo_overflow; __set_PRIMASK(mask);
    return overflow ? BSP_WIFI_BUFFER_FULL : BSP_WIFI_OK;
}
bsp_wifi_status_t bsp_wifi_send_bytes(const uint8_t *data,size_t length)
{
    if (!data || !length || length > UINT16_MAX ||
        HAL_UART_Transmit_DMA(&huart6,data,(uint16_t)length) != HAL_OK) return BSP_WIFI_TRANSPORT_ERROR;
    TickType_t start = xTaskGetTickCount();
    while (huart6.gState != HAL_UART_STATE_READY) {
        if ((TickType_t)(xTaskGetTickCount()-start) >= pdMS_TO_TICKS(1000)) {
            (void)HAL_UART_AbortTransmit(&huart6);return BSP_WIFI_TRANSPORT_ERROR;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return BSP_WIFI_OK;
}
bsp_wifi_status_t bsp_wifi_wait_text(const char *expected,uint32_t timeout_ms)
{
    size_t used = 0; response[0] = 0;
    TickType_t start = xTaskGetTickCount();
    do {
        size_t n;
        bsp_wifi_status_t status = bsp_wifi_read((uint8_t*)response+used,sizeof(response)-used-1U,&n);
        if (status != BSP_WIFI_OK) return status;
        used += n;response[used] = 0;
        if (strstr(response,"\r\nERROR\r\n") || strstr(response,"\r\nFAIL\r\n") ||
            strstr(response,"SEND FAIL")) return BSP_WIFI_RESPONSE_ERROR;
        if (strstr(response,expected)) return BSP_WIFI_OK;
        if (used + 1U == sizeof(response)) return BSP_WIFI_BUFFER_FULL;
        vTaskDelay(pdMS_TO_TICKS(2));
    } while ((TickType_t)(xTaskGetTickCount()-start) < pdMS_TO_TICKS(timeout_ms));
    return BSP_WIFI_RESPONSE_TIMEOUT;
}
bsp_wifi_status_t bsp_wifi_execute_at_command(const uint8_t *command,uint16_t timeout_ms)
{
    bsp_wifi_flush_rx();
    bsp_wifi_status_t status = bsp_wifi_send_bytes(command,strlen((const char*)command));
    return status == BSP_WIFI_OK ? bsp_wifi_wait_text("\r\nOK\r\n",timeout_ms) : status;
}
bsp_wifi_status_t bsp_wifi_send_request_and_wait_for_text(const uint8_t *request,
                                                       const char *expected,uint16_t timeout_ms)
{
    bsp_wifi_status_t status = bsp_wifi_send_bytes(request,strlen((const char*)request));
    return status == BSP_WIFI_OK ? bsp_wifi_wait_text(expected,timeout_ms) : status;
}
bsp_wifi_status_t bsp_wifi_exit_passthrough(void)
{
    bsp_wifi_status_t status = bsp_wifi_send_bytes((const uint8_t*)"+++",3);
    if (status == BSP_WIFI_OK) vTaskDelay(pdMS_TO_TICKS(1200));
    return status;
}
