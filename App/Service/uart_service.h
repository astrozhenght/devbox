#ifndef UART_SERVICE_H
#define UART_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_APP_RX_BUFFER_SIZE      255  // 接收缓冲区大小
#define UART_APP_TX_BUFFER_SIZE      255  // 发送缓冲区大小

typedef enum
{
    UART_SERVICE_OK = 0,
    UART_SERVICE_ERROR_INVALID_ARGUMENT,
    UART_SERVICE_ERROR_INVALID_CONFIG,
    UART_SERVICE_ERROR_NOT_OPEN,
    UART_SERVICE_ERROR_BUSY,
    UART_SERVICE_ERROR_BUFFER_FULL,
    UART_SERVICE_ERROR_TRANSPORT, // UART传输错误
    UART_SERVICE_ERROR_INIT,      // UART初始化失败
} uart_service_status_t;

typedef enum
{
    UART_SERVICE_STATE_CLOSED = 0,
    UART_SERVICE_STATE_OPEN,
    UART_SERVICE_STATE_ERROR
} uart_service_state_t;

typedef enum
{
    UART_SERVICE_DATA_BITS_7 = 7,
    UART_SERVICE_DATA_BITS_8 = 8,
    UART_SERVICE_DATA_BITS_9 = 9
} uart_service_data_bits_t;

typedef enum
{
    UART_SERVICE_STOP_BITS_1 = 0,
    UART_SERVICE_STOP_BITS_2,
    UART_SERVICE_STOP_BITS_0_5,
    UART_SERVICE_STOP_BITS_1_5
} uart_service_stop_bits_t;

typedef enum
{
    UART_SERVICE_PARITY_NONE = 0,
    UART_SERVICE_PARITY_ODD,
    UART_SERVICE_PARITY_EVEN
} uart_service_parity_t;

typedef struct
{
    uint32_t baud_rate;
    uart_service_data_bits_t data_bits;
    uart_service_stop_bits_t stop_bits;
    uart_service_parity_t parity;
} uart_service_config_t;

typedef enum
{
    UART_SERVICE_RX_ERROR_NONE = 0,   // 接收正常，没有待处理错误
    UART_SERVICE_RX_ERROR_PENDING,    // 检测到接收错误，等待通知UI
    UART_SERVICE_RX_ERROR_REPORTED    // 错误已通知UI
} uart_service_rx_error_status_t;

void uart_service_set_rx_cplt_flag(bool val);
bool uart_service_get_rx_cplt_flag(void);
void uart_service_set_error_status(uart_service_rx_error_status_t val);
uart_service_rx_error_status_t uart_service_get_error_status(void);
void uart_service_set_rx_total_bytes(uint32_t val);
uint32_t uart_service_get_rx_total_bytes(void);
void uart_service_clear_rx_ring_buffer(void);
void uart_service_init_rx_ring_buffer(void);
ring_buffer_t* uart_service_get_ring_buffer(void);
uint8_t* uart_service_get_rx_buffer(void);

uint32_t uart_service_copy_ring_to_disp_buffer(uint8_t* disp_buffer);
void uart_service_copy_buffer_to_ring(uint16_t length);


#ifdef __cplusplus
}
#endif

#endif
