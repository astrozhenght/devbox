#include "stm32h7xx_hal.h"
#include "platform_mcu.h"
#include "dma.h"
#include "gpio.h"
#include "sdram.h"
#include "usart.h"

// 只初始化MCU级且未被具体板载器件使用的资源，主要包括 DMA、GPIO、SDRAM、USART1
HAL_StatusTypeDef platform_mcu_init(void)
{
    platform_dma_init();    // DMA初始化，usart1和usart6需要使用DMA
    platform_gpio_init();   // GPIO初始化，仅调试使用
    if (platform_sdram_init() != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (platform_usart1_init() != HAL_OK)
    {
        return HAL_ERROR;
    }
    __HAL_UART_DISABLE(&huart1); // 禁用UART1，等待用户使用串口调试助手时使能

    return HAL_OK;
}
