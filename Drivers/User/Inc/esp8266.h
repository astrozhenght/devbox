#ifndef __ESP8266_H__
#define __ESP8266_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "comm_queue.h"

#define ESP_UART_RECV_SIZE 300

extern Comm_Queue uart6_queue; // 已经添加临界区保护
extern uint8_t g_esp_uart_recv_buff[ESP_UART_RECV_SIZE];  // 无中断调用，不需要临界区保护
extern uint8_t g_esp_uart_recv_complete_flag; // 已经放入临界区保护

typedef struct
{
    char text[25]; // 最长数据：雷阵雨伴有冰雹，21个字节
    char temp[10]; // 温度
    char code[5];  // 天气现象代码
} WeatherInfo_t;

uint8_t esp_send_at_cmd(UART_HandleTypeDef *huart, const uint8_t *p_data, uint16_t timeout_ms);
uint8_t esp_send_req(UART_HandleTypeDef *huart, const uint8_t *p_data, uint16_t timeout_ms);
uint8_t esp_exit_pass_through(UART_HandleTypeDef *huart);
uint8_t weather_get_info(WeatherInfo_t *info, uint8_t *buff);


#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_H__ */

