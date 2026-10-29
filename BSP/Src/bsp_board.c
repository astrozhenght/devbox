#include "bsp_types.h"
#include "platform_mcu.h"
#include "bsp_board.h"
#include "bsp_flash.h"
#include "bsp_lcd.h"
#include "bsp_rtc.h"
#include "bsp_sd.h"
#include "bsp_touch.h"
#include "bsp_wifi.h"


bsp_status_t bsp_board_init(void)
{
    if (platform_mcu_init() != HAL_OK) // 初始化MCU独立外设的资源，包括DMA、GPIO、RTC、SDRAM、USART1
    {
        return BSP_ERROR;
    }

    if (bsp_rtc_init() != BSP_OK)
    {
        return BSP_ERROR;
    }

    if (bsp_lcd_init() != BSP_OK)
    {
        return BSP_ERROR;
    }

    if (bsp_touch_init() != BSP_OK)
    {
        return BSP_ERROR;
    }

    if (bsp_flash_init() != BSP_OK)
    {
        return BSP_ERROR;
    }
    
    if (bsp_wifi_init() != BSP_WIFI_OK)
    {
        return BSP_ERROR;
    }
    
    // if (BSP_SD_Init(SD_Instance) != BSP_SD_ERROR_NONE)
    // {
    //     return BSP_ERROR;
    // }

    return BSP_OK;
}



