#include "stm32h7xx_hal.h"
#include "qspi.h"
#include "main.h"

QSPI_HandleTypeDef hqspi;

/*************************************************************************************************
 *    函 数 名: HAL_QSPI_MspInit
 *    入口参数: hqspi - QSPI_HandleTypeDef定义的变量，即表示定义的QSPI句柄
 *    返 回 值: 无
 *    函数功能: QSPI引脚初始化函数
 *    说    明: 该函数会被    MX_QUADSPI_Init 函数调用
 *************************************************************************************************/
void HAL_QSPI_MspInit(QSPI_HandleTypeDef *hqspi)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    if (hqspi->Instance == QUADSPI)
    {
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
        PeriphClkInitStruct.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
        {
            Error_Handler();
        }

        __HAL_RCC_QSPI_CLK_ENABLE();    // 使能QSPI时钟
        __HAL_RCC_QSPI_FORCE_RESET();   // 复位QUADSPI 和 QUADSPI延迟模块
        __HAL_RCC_QSPI_RELEASE_RESET();

        __HAL_RCC_GPIOF_CLK_ENABLE();   // 使能QUADSPI IO口时钟
        __HAL_RCC_GPIOG_CLK_ENABLE();   // 使能QUADSPI IO口时钟

        /******************************************************
         PG6     ------> QUADSPI_BK1_NCS
         PF6     ------> QUADSPI_BK1_IO3
         PF7     ------> QUADSPI_BK1_IO2
         PF8     ------> QUADSPI_BK1_IO0
         PF10    ------> QUADSPI_CLK
         PF9     ------> QUADSPI_BK1_IO1
        *******************************************************/
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;            // 复用推挽输出模式
        GPIO_InitStruct.Pull = GPIO_NOPULL;                // 无上下拉
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH; // 超高速IO口速度

        GPIO_InitStruct.Pin = GPIO_PIN_10;                 // QUADSPI_CLK 引脚
        GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;      // QUADSPI_CLK 复用
        HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);            // 初始化 QUADSPI_CLK 引脚

        GPIO_InitStruct.Pin = GPIO_PIN_6;                  // QUADSPI_BK1_NCS 引脚
        GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;     // QUADSPI_BK1_NCS 复用
        HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);            // 初始化 QUADSPI_BK1_NCS 引脚

        GPIO_InitStruct.Pin = GPIO_PIN_8;                  // QUADSPI_BK1_IO0 引脚
        GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;     // QUADSPI_BK1_IO0 复用
        HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);            // 初始化 QUADSPI_BK1_IO0 引脚

        GPIO_InitStruct.Pin = GPIO_PIN_9;                  // QUADSPI_BK1_IO1 引脚
        GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;     // QUADSPI_BK1_IO1 复用
        HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);            // 初始化 QUADSPI_BK1_IO1 引脚

        GPIO_InitStruct.Pin = GPIO_PIN_7;                  // QUADSPI_BK1_IO2 引脚
        GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;      // QUADSPI_BK1_IO2 复用
        HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);            // 初始化 QUADSPI_BK1_IO2 引脚

        GPIO_InitStruct.Pin = GPIO_PIN_6;                  // QUADSPI_BK1_IO3 引脚
        GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;      // QUADSPI_BK1_IO3 复用
        HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);            // 初始化 QUADSPI_BK1_IO3 引脚
    }
}

HAL_StatusTypeDef platform_qspi_init(void)
{
    /*在某些场合，例如用作下载算法时，需要手动清零句柄等参数，否则会工作不正常*/
    uint32_t i;
    char *p;

    /* 此处参考安富莱的代码，大家可以去学习安富莱的教程，非常专业 */
    p = (char *)&hqspi;
    for (i = 0; i < sizeof(hqspi); i++)
    {
        *p++ = 0;
    }
    hqspi.Instance = QUADSPI;

    if (HAL_QSPI_DeInit(&hqspi) != HAL_OK) // 复位QSPI外设
    {
        return HAL_ERROR;
    }

    hqspi.Instance = QUADSPI; // QSPI外设

    /*本例程选择 HCLK 作为QSPI的内核时钟，速度为200M，再经过2分频得到100M驱动时钟，
      关于 QSPI内核时钟 的设置，请参考 main.c文件里的 SystemClock_Config 函数 */
    // 需要注意的是，当使用内存映射模式时，这里的分频系数不能设置为0！！否则会读取错误
    hqspi.Init.ClockPrescaler = 1;                              // 时钟分频值，将QSPI内核时钟进行 1+1 分频得到QSPI通信驱动时钟
    hqspi.Init.FifoThreshold = 32;                              // FIFO阈值
    hqspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_NONE;      // 采样不发生位移，寄存器默认值
    hqspi.Init.FlashSize = 22;                                  // flash大小，FLASH 中的字节数 = 2^[FSIZE+1]，核心板采用是8M字节的W25Q64，这里设置为22
    hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_1_CYCLE;  // 片选保持高电平的时间
    hqspi.Init.ClockMode = QSPI_CLOCK_MODE_3;                   // 模式3
    hqspi.Init.FlashID = QSPI_FLASH_ID_1;                       // 使用QSPI1
    hqspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE;              // 禁止双闪存模式

    if (HAL_QSPI_Init(&hqspi) != HAL_OK) // 初始化配置
    {
        return HAL_ERROR;
    }
    return HAL_OK;
}

