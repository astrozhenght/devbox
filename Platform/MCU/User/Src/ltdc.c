#include "ltdc.h"
#include "platform_irq_priority.h"

LTDC_HandleTypeDef hltdc; // LTDC句柄

// 重写weak函数，打开ltdc时钟和初始化gpio引脚
void HAL_LTDC_MspInit(LTDC_HandleTypeDef *hltdc)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (hltdc->Instance == LTDC)
    {
        /* Peripheral clock enable */
        __HAL_RCC_LTDC_CLK_ENABLE(); // 使能LTDC时钟

        HAL_NVIC_SetPriority(LTDC_IRQn,
                             PLATFORM_IRQ_LTDC_PREEMPT_PRIORITY,
                             PLATFORM_IRQ_LTDC_SUB_PRIORITY);
        HAL_NVIC_EnableIRQ(LTDC_IRQn);

        /* GPIO Ports Clock Enable */
        __HAL_RCC_GPIOG_CLK_ENABLE();
        __HAL_RCC_GPIOH_CLK_ENABLE();
        __HAL_RCC_GPIOI_CLK_ENABLE();
        __HAL_RCC_GPIOJ_CLK_ENABLE();
        __HAL_RCC_GPIOK_CLK_ENABLE(); // 使能IO口时钟

        /*------------------------------LTDC GPIO Configuration--------------------------
            PI15 ------> LTDC_R0     PJ7  ------> LTDC_G0     PJ12 ------> LTDC_B0
            PJ0  ------> LTDC_R1     PJ8  ------> LTDC_G1     PJ13 ------> LTDC_B1
            PJ1  ------> LTDC_R2     PJ9  ------> LTDC_G2     PJ14 ------> LTDC_B2
            PJ2  ------> LTDC_R3     PG10 ------> LTDC_G3     PJ15 ------> LTDC_B3
            PJ3  ------> LTDC_R4     PH15 ------> LTDC_G4     PK3  ------> LTDC_B4
            PJ4  ------> LTDC_R5     PH4  ------> LTDC_G5     PK4  ------> LTDC_B5
            PJ5  ------> LTDC_R6     PK1  ------> LTDC_G6     PK5  ------> LTDC_B6
            PJ6  ------> LTDC_R7     PK2  ------> LTDC_G7     PK6  ------> LTDC_B7

            PI12 ------> LTDC_HSYNC
            PI13 ------> LTDC_VSYNC
            PK7  ------> LTDC_DE
            PI14 ------> LTDC_CLK
        */
        GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_3 | GPIO_PIN_7 | GPIO_PIN_2 | GPIO_PIN_1;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
        HAL_GPIO_Init(GPIOK, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_10;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF9_LTDC;
        HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_15 | GPIO_PIN_14 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_9 | GPIO_PIN_0 | GPIO_PIN_8 | GPIO_PIN_7 | GPIO_PIN_6 | GPIO_PIN_1 | GPIO_PIN_5 | GPIO_PIN_3 | GPIO_PIN_2 | GPIO_PIN_4;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
        HAL_GPIO_Init(GPIOJ, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_15;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
        HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
        HAL_GPIO_Init(GPIOI, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_4;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF9_LTDC;
        HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_6;                    // 背光引脚
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;          // 推挽输出模式
        GPIO_InitStruct.Pull = GPIO_NOPULL;                  // 无上下拉
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;         // 速度等级低
        HAL_GPIO_Init(GPIOH, &GPIO_InitStruct); // 初始化
        HAL_GPIO_WritePin(GPIOH, GPIO_PIN_6, GPIO_PIN_RESET);// 关闭背光
    }
}

/*************************************************************************************************
 *    函 数 名:    platform_ltdc_init
 *    入口参数:    无
 *    返 回 值:    无
 *    函数功能:    初始化LTDC引脚的IO口、全局参数、层设置等
 *    说    明:    无
 *************************************************************************************************/
// 仅支持配置Layer0，Layer1暂不支持
HAL_StatusTypeDef platform_ltdc_init(const platform_ltdc_config_t *config)
{
    LTDC_LayerCfgTypeDef layer_config = {0}; // layer 相关参数

    if (config == NULL)
    {
        return HAL_ERROR;
    }

    hltdc.Instance = LTDC;
    hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;    // 低电平有效
    hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;    // 低电平有效
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;    // 低电平有效，要注意的是，很多面板都是高电平有效，但是750需要设置成低电平才能正常显示
    hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;   // 像素时钟极性，0: pixel clock polarity is active low. 1: pixel clock is active high.
    hltdc.Init.HorizontalSync = config->hsync - 1; // 根据屏幕设置参数即可
    hltdc.Init.VerticalSync = config->vsync - 1;
    hltdc.Init.AccumulatedHBP = config->hbp + config->hsync - 1;
    hltdc.Init.AccumulatedVBP = config->vbp + config->vsync - 1;
    hltdc.Init.AccumulatedActiveW = config->width + config->hsync + config->hbp - 1;
    hltdc.Init.AccumulatedActiveH = config->height + config->vsync + config->vbp - 1;
    hltdc.Init.TotalWidth = config->width + config->hsync + config->hbp + config->hfp - 1;
    hltdc.Init.TotalHeigh = config->height + config->vsync + config->vbp + config->vfp - 1;
    hltdc.Init.Backcolor.Red = 0;    // 初始背景色，R
    hltdc.Init.Backcolor.Green = 0;  // 初始背景色，G
    hltdc.Init.Backcolor.Blue = 255; // 初始背景色，B

    if (HAL_LTDC_Init(&hltdc) != HAL_OK) // 初始化LTDC参数
    {
        return HAL_ERROR;
    }

    // layer0 显示配置
    layer_config.WindowX0 = 0;                 // 水平起点
    layer_config.WindowX1 = config->width;         // 水平终点
    layer_config.WindowY0 = 0;                 // 垂直起点
    layer_config.WindowY1 = config->height;        // 垂直终点
    layer_config.ImageWidth = config->width;       // 显示区域宽度
    layer_config.ImageHeight = config->height;     // 显示区域高度
    layer_config.PixelFormat = (uint32_t)config->layer_pixel_format; // 颜色格式

    // 配置 layer0 的恒定透明度，最终写入 LTDC_LxCACR 寄存器
    // 需要注意的是，这个参数是直接配置整个 layer0 的透明度，这里设置为255即不透明
    layer_config.Alpha = 255; // 取值范围0~255，255表示不透明，0表示完全透明

    // 设置 layer0 的层混合系数，最终写入 LTDC_LxBFCR 寄存器
    // 该参数用于设置 layer0 和 底层背景 之间的颜色混合系数，计算公式为 ：
    // 混合后的颜色 =  BF1 * layer0的颜色 + BF2 * 底层背景的颜色
    // 如果 layer0 使用了透明色，则必须配置成 LTDC_BLENDING_FACTOR1_PAxCA 和 LTDC_BLENDING_FACTOR2_PAxCA，否则ARGB中的A通道不起作用
    // 更多的介绍可以查阅 参考手册关于 LTDC_LxBFCR 寄存器的介绍
    layer_config.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA; // 混合系数1
    layer_config.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA; // 混合系数2

    layer_config.FBStartAdress = config->layer_framebuffer_address; // 显存地址

    // 配置 layer0 的初始默认颜色，包括A,R,G,B 的值，最终写入 LTDC_LxDCCR 寄存器
    layer_config.Alpha0 = 0;            // 初始颜色，A
    layer_config.Backcolor.Blue = 0;    // 初始颜色，R
    layer_config.Backcolor.Green = 255; // 初始颜色，G
    layer_config.Backcolor.Red = 0;     // 初始颜色，B

    HAL_LTDC_ConfigLayer(&hltdc, &layer_config, 0); // 配置layer0

    // 因为750每个通道的低位都是采用伪随机抖动输出，如果不开启颜色抖动，则无法正常显示24位或者32位色
    if ((config->layer_pixel_format == LTDC_PIXEL_FORMAT_RGB888) ||
        (config->layer_pixel_format == LTDC_PIXEL_FORMAT_ARGB8888))
    {
        HAL_LTDC_EnableDither(&hltdc); // 开启颜色抖动
    }

    return HAL_OK;
}
