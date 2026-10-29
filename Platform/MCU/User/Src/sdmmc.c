#include "sdmmc.h"
#include "main.h"
#include "stm32h7xx_hal.h"
#include "platform_irq_priority.h"

/* IRQ priorities */
#define BSP_SD_IT_PRIORITY          PLATFORM_IRQ_SDMMC_PREEMPT_PRIORITY
#define BSP_SD_IT_SUB_PRIORITY      PLATFORM_IRQ_SDMMC_SUB_PRIORITY

/* SD high performance usage define */
/* DS: max bus speed 12.5 MB/s, max clock freq 25MHz*/
/* HS: max bus speed 25 MB/s, max clock freq 50MHz*/
#define USE_SD_HIGH_PERFORMANCE     0U

SD_HandleTypeDef hsd_sdmmc[SD_INSTANCES_NBR]; // 定义SDMMC句柄
EXTI_HandleTypeDef hsd_exti[SD_INSTANCES_NBR];

/**
 * @brief  Initializes the SD MSP.
 * @param  hsd  SD handle
 * @retval None
 */
void SD_MspInit(SD_HandleTypeDef *hsd)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (hsd == &hsd_sdmmc[0])
    {
        /** Initializes the peripherals clock
         */
        /* SDMMC1 Clock 200MHz */
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SDMMC;
        PeriphClkInitStruct.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
        {
            Error_Handler();
        }

        /* Enable SDIO clock */
        __HAL_RCC_SDMMC1_CLK_ENABLE();

        /* Enable GPIOs clock */
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();

        /**SDMMC1 GPIO Configuration
        PC8     ------> SDMMC1_D0
        PC9     ------> SDMMC1_D1
        PC10    ------> SDMMC1_D2
        PC11    ------> SDMMC1_D3
        PC12    ------> SDMMC1_CK
        PD2     ------> SDMMC1_CMD
        */
        GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF12_SDIO1;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_12;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_2;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF12_SDIO1;
        HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

        __HAL_RCC_SDMMC1_FORCE_RESET();
        __HAL_RCC_SDMMC1_RELEASE_RESET();

        /* NVIC configuration for SDIO interrupts   */
        HAL_NVIC_SetPriority(SDMMC1_IRQn, BSP_SD_IT_PRIORITY, BSP_SD_IT_SUB_PRIORITY);
        HAL_NVIC_EnableIRQ(SDMMC1_IRQn);
    }

}

/**
 * @brief  DeInitializes the SD MSP.
 * @param  hsd  SD handle
 * @retval None
 */
void SD_MspDeInit(SD_HandleTypeDef *hsd)
{
    GPIO_InitTypeDef gpio_init_structure;

    if (hsd == &hsd_sdmmc[0])
    {
        /* Disable NVIC for SDIO interrupts   */
        HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
        
        /**SDMMC1 GPIO Configuration
        PC10    ------> SDMMC1_D2
        PC11    ------> SDMMC1_D3
        PC12    ------> SDMMC1_CK
        PD2     ------> SDMMC1_CMD
        PC8     ------> SDMMC1_D0
        PC9     ------> SDMMC1_D1
        */
        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12);
        HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);

        /* Disable SDMMC1 clock   */
        __HAL_RCC_SDMMC1_CLK_DISABLE();
    }
}


/**
 * @brief  Initializes the SDMMC1 peripheral.
 * @param  hsd SD handle
 * @retval HAL status
 */
HAL_StatusTypeDef platform_sd_init(uint32_t Instance)
{
    HAL_StatusTypeDef ret = HAL_OK;

    if (Instance == 0)
    {
        /* uSD device interface configuration */
        hsd_sdmmc[0].Instance = SDMMC1;
        hsd_sdmmc[0].Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
        hsd_sdmmc[0].Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
        hsd_sdmmc[0].Init.BusWide = SDMMC_BUS_WIDE_4B;
        hsd_sdmmc[0].Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;

        // SDMMC_CK(输出时钟) = SDMMCCLK(SDMMC 内核时钟) / (2 * ClockDiv)
        // 在本例程中，sdmmc_ker_ck = 200M
        // SDMMC_HSpeed_CLK_DIV 和 SDMMC_NSpeed_CLK_DIV 都是对应200M外设时钟的配置
        if ( USE_SD_HIGH_PERFORMANCE > 0 )
        {
            hsd_sdmmc[0].Init.ClockDiv = SDMMC_HSpeed_CLK_DIV;
        }
        else
        {
            // SDMMC Default Speed Frequency (25Mhz max) for Peripheral CLK 200MHz
            // SDMMC High Speed Frequency (50Mhz max) for Peripheral CLK 200MHz
            hsd_sdmmc[0].Init.ClockDiv = SDMMC_HSpeed_CLK_DIV;  // 200 / (2 * 2) = 50MHz
        }

        /* HAL SD initialization   */
        if (HAL_SD_Init(&hsd_sdmmc[0]) != HAL_OK)
        {
            ret = HAL_ERROR;
        }
    }

    return ret;
}

