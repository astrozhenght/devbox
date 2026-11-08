#ifndef __BSP_WIFI_H__
#define __BSP_WIFI_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

// 注意：WIFI模块回复的数据个数不应该超过环形缓冲区的长度
#define WIFI_RX_BUFFER_SIZE            512U

typedef enum {
    BSP_WIFI_OK = 0,           // 成功，给ESP8266发送指令成功，并且接收到ESP8266回复的"OK"，有效数据已经存到g_uart_textarea_buffer数组当中
    BSP_WIFI_TRANSPORT_ERROR,  // UART/DMA 等底层通信错误，给ESP8266发送指令失败，500ms内未发送完成
    BSP_WIFI_RESPONSE_ERROR,   // ESP8266回复"ERROR/FAIL"
    BSP_WIFI_RESPONSE_TIMEOUT, // ESP8266回复数据超时
    BSP_WIFI_INIT_ERROR,       // UART初始化错误
    BSP_WIFI_BUFFER_FULL
} bsp_wifi_status_t;

bsp_wifi_status_t bsp_wifi_init(void);
bsp_wifi_status_t bsp_wifi_execute_at_command(const uint8_t *command, uint16_t resp_timeout_ms);
bsp_wifi_status_t bsp_wifi_send_request_and_wait_for_text(const uint8_t *request, const char *expected_response, uint16_t resp_timeout_ms);
bsp_wifi_status_t bsp_wifi_exit_passthrough(void);
bsp_wifi_status_t bsp_wifi_start_receive(void);
uint8_t *bsp_wifi_get_rx_buffer(void);
void bsp_wifi_set_rx_data_ready(uint8_t ready);
void bsp_wifi_copy_buffer_to_ring(uint16_t length);
void bsp_wifi_on_rx_event(uint16_t position);
void bsp_wifi_on_rx_error(void);
void bsp_wifi_flush_rx(void);
bsp_wifi_status_t bsp_wifi_read(uint8_t *dest, size_t capacity, size_t *length);
bsp_wifi_status_t bsp_wifi_send_bytes(const uint8_t *data, size_t length);
bsp_wifi_status_t bsp_wifi_wait_text(const char *expected, uint32_t timeout_ms);
const char *bsp_wifi_get_response(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_WIFI_H__ */

