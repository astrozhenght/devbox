// 本文件仅供串口终端App UART1使用
// 操作ring_buffer的函数封装到本文件中
#include "uart_service.h"
#include "stm32h7xx.h"  // __disable_irq()、__enable_irq()

// 变量说明：
// 1.接收相关变量放本文件，uart接收中断函数会调用
// 2.uart接收到的数据先放入rx_buffer，uart接收完成后追加到ring_buffer，UI使用的时候
//  从ring_buffer拷贝到供显示专用的display_buffer中
static bool s_uart_rx_cplt_flag = false;
static uint32_t s_uart_rx_total_bytes = 0;  // uart累计接收长度，需要放入临界区保护
static uart_service_rx_error_status_t s_uart_rx_error_status = UART_SERVICE_RX_ERROR_NONE;
static ring_buffer_t s_uart_rx_ring_buffer; // 已经添加临界区保护
static uint8_t s_uart_rx_buffer[UART_APP_RX_BUFFER_SIZE]; // 无中断调用，不需要临界区保护

void uart_service_set_rx_cplt_flag(bool val)
{
    __disable_irq();
    s_uart_rx_cplt_flag = val;
    __enable_irq();
}

bool uart_service_get_rx_cplt_flag(void)
{
    return s_uart_rx_cplt_flag;
}

void uart_service_set_error_status(uart_service_rx_error_status_t val)
{
    __disable_irq();
    s_uart_rx_error_status = val;
    __enable_irq();
}

uart_service_rx_error_status_t uart_service_get_error_status(void)
{
    return s_uart_rx_error_status;
}

void uart_service_set_rx_total_bytes(uint32_t val)
{
    __disable_irq();
    s_uart_rx_total_bytes = val;
    __enable_irq();
}

uint32_t uart_service_get_rx_total_bytes(void)
{
    return s_uart_rx_total_bytes;
}

// 把环形缓冲区中接收到的数据放入buffer中，供LVGL显示
uint32_t uart_service_copy_ring_to_disp_buffer(uint8_t* disp_buffer)
{
    __disable_irq();
    uint32_t len = ring_buffer_copy(&s_uart_rx_ring_buffer, disp_buffer);
    __enable_irq();
    return len;
}

// uart1接收完成后，把接收到的数据放入环形缓冲区
void uart_service_copy_buffer_to_ring(uint16_t length)
{
    __disable_irq();
    ring_buffer_write(&s_uart_rx_ring_buffer, s_uart_rx_buffer, length);
    __enable_irq();
}

// 把环形缓冲区中接收到的数据清除
void uart_service_clear_rx_ring_buffer(void)
{
    __disable_irq();
    ring_buffer_clear(&s_uart_rx_ring_buffer);
    __enable_irq();
}

// 初始化环形缓冲区
void uart_service_init_rx_ring_buffer(void)
{
    ring_buffer_init(&s_uart_rx_ring_buffer);
}

ring_buffer_t* uart_service_get_ring_buffer(void)
{
    return (ring_buffer_t*)(&s_uart_rx_ring_buffer);
}

uint8_t* uart_service_get_rx_buffer(void)
{
    return (uint8_t *)(&s_uart_rx_buffer);
}

