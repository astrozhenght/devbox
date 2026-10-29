#include <stdio.h>
#include <string.h>
#include "usart.h"
#include "bsp_wifi.h"
#include "ring_buffer.h"
#include "FreeRTOS.h"
#include "task.h"

static uint8_t wifi_rx_data_ready = 0;     // 已经放入临界区保护
static ring_buffer_t wifi_rx_ring_buffer;  // 已经添加临界区保护
static uint8_t wifi_rx_buffer[WIFI_RX_BUFFER_SIZE];  // 无中断调用，不需要临界区保护

bsp_wifi_status_t bsp_wifi_init(void)
{
    HAL_StatusTypeDef status;
    status = platform_usart6_init(); // USART6用于连接ESP8266
    platform_usart6_disable();       // 禁用UART6
    if (status == HAL_OK)
    {
        return BSP_WIFI_OK;
    }
    else
    {
        return BSP_WIFI_INIT_ERROR;
    }
}

/**
 * @brief 启动Wi-Fi模块的数据接收
 *
 * 使能UART6，初始化接收环形缓冲区，并启动UART空闲中断DMA接收。
 *
 * @return BSP_WIFI_OK              启动成功
 * @return BSP_WIFI_TRANSPORT_ERROR UART DMA接收启动失败
 */
bsp_wifi_status_t bsp_wifi_start_receive(void)
{
    // 使能UART6
    platform_usart6_enable();

    // 初始化串口上位机队列
    ring_buffer_init(&wifi_rx_ring_buffer);

    // 使能UART接收空闲中断DMA模式
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart6, wifi_rx_buffer, WIFI_RX_BUFFER_SIZE) != HAL_OK)
    {
        return BSP_WIFI_TRANSPORT_ERROR;
    }
    return BSP_WIFI_OK;
}

uint8_t *bsp_wifi_get_rx_buffer(void)
{
    return wifi_rx_buffer;
}

void bsp_wifi_set_rx_data_ready(uint8_t ready)
{
    wifi_rx_data_ready = ready;
}

/**
 * @brief 执行ESP8266 AT指令并等待标准响应
 *
 * 通过UART DMA发送AT指令，然后在指定时间内等待ESP8266响应。
 * 收到“OK”时返回成功，收到“ERROR”或“FAIL”时返回响应错误，
 * 超过等待时间仍未收到有效响应时返回超时。
 *
 * @param command             以空字符结尾的AT指令字符串
 * @param response_timeout_ms 等待ESP8266响应的超时时间，单位为毫秒
 *
 * @return BSP_WIFI_OK               指令执行成功
 * @return BSP_WIFI_TRANSPORT_ERROR  UART或DMA发送失败
 * @return BSP_WIFI_RESPONSE_ERROR   ESP8266返回ERROR或FAIL
 * @return BSP_WIFI_RESPONSE_TIMEOUT 等待响应超时
 */
bsp_wifi_status_t bsp_wifi_execute_at_command(const uint8_t *command, uint16_t resp_timeout_ms)
{
    uint8_t times = 0;

    // 发送指令给ESP8266
    if (HAL_OK != HAL_UART_Transmit_DMA(&huart6, command, strlen((const char*)command)))
    {
        return BSP_WIFI_TRANSPORT_ERROR;
    }
    // 等待指令发送完成，若500ms内未发送完成，则强制退出
    while ((huart6.gState != HAL_UART_STATE_READY) && (times < 100))
    {
        vTaskDelay(pdMS_TO_TICKS(5));
        times++;
    }
    if (times >= 100)
    {
        return BSP_WIFI_TRANSPORT_ERROR;
    }

    // 等待ESP8266回复数据
    // 超时处理：若在规定时间内未接收到ESP8266回复的“OK/ERROR/FAIL”，则强制退出
    for (uint8_t i = 0; i < 10; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(resp_timeout_ms / 10)); // 单位ms
        if (wifi_rx_data_ready == 1)
        {
            // 获取ESP8266回复的完整数据，方式是从环形缓冲区中取数据放入指定buffer中
            __disable_irq();
            uint32_t len = ring_buffer_copy(&wifi_rx_ring_buffer, wifi_rx_buffer);
            wifi_rx_buffer[len] = '\0'; // 添加字符串结束符
            wifi_rx_data_ready = 0;
            __enable_irq();
            
            // 判断是否接收到OK/ERROR/FAIL
            if (strstr((const char *)wifi_rx_buffer, "OK") != NULL) // 接收到OK
            {
                ring_buffer_clear(&wifi_rx_ring_buffer); // 清空环形缓冲区数据
                return BSP_WIFI_OK;
            }
            if ((strstr((const char *)wifi_rx_buffer, "ERROR") != NULL) ||
                (strstr((const char *)wifi_rx_buffer, "FAIL")  != NULL)) // 接收到ERROR/FAIL
            {
                ring_buffer_clear(&wifi_rx_ring_buffer); // 清空环形缓冲区数据
                return BSP_WIFI_RESPONSE_ERROR;
            }
        }
    }
    ring_buffer_clear(&wifi_rx_ring_buffer); // 清空环形缓冲区数据
    return BSP_WIFI_RESPONSE_TIMEOUT;
}

/**
 * @brief 发送请求并等待响应中出现指定文本
 *
 * 通过UART DMA发送请求数据，并在指定时间内接收ESP8266响应。
 * 当响应中包含expected_text时返回成功，等待超时则返回响应超时。
 *
 * @param request             以空字符结尾的请求字符串
 * @param expected_text       期望在响应中出现的文本
 * @param response_timeout_ms 等待响应的超时时间，单位为毫秒
 *
 * @return BSP_WIFI_OK               响应中包含期望文本
 * @return BSP_WIFI_TRANSPORT_ERROR  UART或DMA发送失败
 * @return BSP_WIFI_RESPONSE_TIMEOUT 等待期望文本超时
 */
bsp_wifi_status_t bsp_wifi_send_request_and_wait_for_text(
    const uint8_t *request,
    const char *expected_response,
    uint16_t resp_timeout_ms)
{
    uint8_t times = 0;

    // 发送指令给ESP8266
    if (HAL_OK != HAL_UART_Transmit_DMA(&huart6, request, strlen((const char*)request)))
    {
        return BSP_WIFI_TRANSPORT_ERROR;
    }
    // 等待指令发送完成，若500ms内未发送完成，则强制退出
    while ((huart6.gState != HAL_UART_STATE_READY) && (times < 100))
    {
        vTaskDelay(pdMS_TO_TICKS(5)); // 单位ms
        times++;
    }
    if (times >= 100)
    {
        return BSP_WIFI_TRANSPORT_ERROR;
    }

    // 等待ESP8266回复数据
    // 超时处理：若在规定时间内未接收到ESP8266的回复"expected_response"，则强制退出
    for (uint8_t i = 0; i < 10; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(resp_timeout_ms / 10)); // 单位ms
        if (wifi_rx_data_ready == 1)
        {
            // 获取ESP8266回复的完整数据，方式是从环形缓冲区中取数据放入指定buffer中
            __disable_irq();
            uint32_t len = ring_buffer_copy(&wifi_rx_ring_buffer, wifi_rx_buffer);
            wifi_rx_buffer[len] = '\0'; // 添加字符串结束符
            wifi_rx_data_ready = 0;
            __enable_irq();

            // 判断是否接收到预期回复
            if (strstr((const char *)wifi_rx_buffer, expected_response) != NULL)
            {
                ring_buffer_clear(&wifi_rx_ring_buffer); // 清空环形缓冲区数据
                return BSP_WIFI_OK;
            }
        }
    }
    ring_buffer_clear(&wifi_rx_ring_buffer); // 清空环形缓冲区数据
    return BSP_WIFI_RESPONSE_TIMEOUT;
}

// 退出透传模式
// 当输入单独一包 +++ 时，返回普通AT指令模式。
// 发送 +++ 退出透传时，至少间隔 1 秒再发下一条 AT 指令。
// +++指令必须在开启透传模式以及单连接下使用
bsp_wifi_status_t bsp_wifi_exit_passthrough(void)
{
    uint8_t times = 0;

    // 发送指令给ESP8266
    if (HAL_OK != HAL_UART_Transmit_DMA(&huart6, (const uint8_t *)"+++", 3))
    {
        return BSP_WIFI_TRANSPORT_ERROR;
    }
    // 等待指令发送完成，若500ms内未发送完成，则强制退出
    while ((huart6.gState != HAL_UART_STATE_READY) && (times < 100))
    {
        vTaskDelay(pdMS_TO_TICKS(5)); // 单位ms
        times++;
    }
    if (times >= 100)
    {
        return BSP_WIFI_TRANSPORT_ERROR;
    }

    vTaskDelay(pdMS_TO_TICKS(1200)); // 发送+++后至少间隔1秒再发下一条AT指令
    return BSP_WIFI_OK;
}

bsp_wifi_status_t bsp_wifi_connect(const char *ssid, const char *password)
{
    (void)ssid;
    (void)password;
    return BSP_WIFI_OK;
}

bsp_wifi_status_t bsp_wifi_disconnect(void)
{
    return BSP_WIFI_OK;
}

// 把接收到的数据放入环形缓冲区
void bsp_wifi_copy_buffer_to_ring(uint16_t length)
{
    __disable_irq();
    ring_buffer_write(&wifi_rx_ring_buffer, wifi_rx_buffer, length);
    __enable_irq();
}