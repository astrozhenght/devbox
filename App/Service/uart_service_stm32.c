#include <stdint.h>
#include "uart_service_port.h"
#include "usart.h"

// 初始化并使能UART，重新配置接收参数
uart_service_status_t uart_service_port_open(const uart_service_config_t *config)
{
    huart1.Init.BaudRate = config->baud_rate;
    switch (config->data_bits)
    {
        case UART_SERVICE_DATA_BITS_7:
            huart1.Init.WordLength = UART_WORDLENGTH_7B;
            break;
        case UART_SERVICE_DATA_BITS_8:
            huart1.Init.WordLength = UART_WORDLENGTH_8B;
            break;
        case UART_SERVICE_DATA_BITS_9:
            huart1.Init.WordLength = UART_WORDLENGTH_9B;
            break;
        default:
            return UART_SERVICE_ERROR_INVALID_CONFIG;
    }
    switch (config->stop_bits)
    {
        case UART_SERVICE_STOP_BITS_1:
            huart1.Init.StopBits = UART_STOPBITS_1;
            break;
        case UART_SERVICE_STOP_BITS_2:
            huart1.Init.StopBits = UART_STOPBITS_2;
            break;
        case UART_SERVICE_STOP_BITS_0_5:
            huart1.Init.StopBits = UART_STOPBITS_0_5;
            break;
        case UART_SERVICE_STOP_BITS_1_5:
            huart1.Init.StopBits = UART_STOPBITS_1_5;
            break;
        default:
            return UART_SERVICE_ERROR_INVALID_CONFIG;
    }
    switch (config->parity)
    {
        case UART_SERVICE_PARITY_NONE:
            huart1.Init.Parity = UART_PARITY_NONE;
            break;
        case UART_SERVICE_PARITY_ODD:
            huart1.Init.Parity = UART_PARITY_ODD;
            break;
        case UART_SERVICE_PARITY_EVEN:
            huart1.Init.Parity = UART_PARITY_EVEN;
            break;
        default:
            return UART_SERVICE_ERROR_INVALID_CONFIG;
    }
    if (HAL_UART_Init(&huart1) != HAL_OK) // 重新初始化UART，内部包含使能UART
    {
        return UART_SERVICE_ERROR_INIT;
    }
    return uart_service_port_reconfigure_rx_params(); // 重新配置接收参数
}

uart_service_status_t uart_service_port_close(void)
{
    HAL_UART_DMAStop(&huart1);   // 停止DMA接收
    __HAL_UART_DISABLE(&huart1); // 禁用UART
    return UART_SERVICE_OK;
}

uart_service_status_t uart_service_port_reconfigure_rx_params(void)
{
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart1, uart_service_get_rx_buffer(), UART_APP_RX_BUFFER_SIZE) != HAL_OK) // 重新初始化接收参数
    {
        return UART_SERVICE_ERROR_INIT;
    }
    return UART_SERVICE_OK;
}

uart_service_status_t uart_service_port_send(const uint8_t *data, size_t length)
{
    if (HAL_UART_Transmit_DMA(&huart1, data, length) != HAL_OK)
    {
        return UART_SERVICE_ERROR_TRANSPORT;
    }
    return UART_SERVICE_OK;
}
