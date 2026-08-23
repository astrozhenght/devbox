#include "lcd.h"

//LCD相关参数结构体
struct
{
    uint32_t Color;                 // LCD当前画笔颜色
    uint32_t BackColor;             // 背景色
    uint32_t ColorMode;             // 颜色格式
    uint32_t LayerMemoryAdd;        // 层显存地址
    uint8_t  Layer;                 // 当前层
    uint8_t  Direction;             // 显示方向
    uint8_t  BytesPerPixel;         // 每个像素所占字节数
    uint8_t  ShowNum_Mode;          // 数字显示模式
}LCD;

LTDC_HandleTypeDef hltdc; // LTDC句柄

// 重写weak函数，打开ltdc时钟和初始化gpio引脚
void HAL_LTDC_MspInit(LTDC_HandleTypeDef *hltdc)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (hltdc->Instance == LTDC)
    {
        /* Peripheral clock enable */
        __HAL_RCC_LTDC_CLK_ENABLE(); // 使能LTDC时钟

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

        // 初始化背光引脚
        GPIO_InitStruct.Pin = LCD_BACKLIGHT_PIN;             // 背光引脚
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;          // 推挽输出模式
        GPIO_InitStruct.Pull = GPIO_NOPULL;                  // 无上下拉
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;         // 速度等级低
        HAL_GPIO_Init(LCD_BACKLIGHT_PORT, &GPIO_InitStruct); // 初始化

        LCD_BACKLIGHT_OFF; // 先关闭背光引脚，初始化LTDC之后再开启
    }
}

/*************************************************************************************************
 *    函 数 名:    MX_LTDC_Init
 *    入口参数:    无
 *    返 回 值:    无
 *    函数功能:    初始化LTDC引脚的IO口、全局参数、层设置等
 *    说    明:    无
 *************************************************************************************************/
void MX_LTDC_Init(void)
{
    LTDC_LayerCfgTypeDef pLayerCfg = {0}; // layer0 相关参数
    __HAL_RCC_DMA2D_CLK_ENABLE();         // 使能DMA2D时钟

    hltdc.Instance = LTDC;
    hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;  // 低电平有效
    hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;  // 低电平有效
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;  // 低电平有效，要注意的是，很多面板都是高电平有效，但是750需要设置成低电平才能正常显示
    hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC; // 像素时钟极性，0: pixel clock polarity is active low. 1: pixel clock is active high.

    hltdc.Init.HorizontalSync = HSW - 1; // 根据屏幕设置参数即可
    hltdc.Init.VerticalSync = VSW - 1;
    hltdc.Init.AccumulatedHBP = HBP + HSW - 1;
    hltdc.Init.AccumulatedVBP = VBP + VSW - 1;
    hltdc.Init.AccumulatedActiveW = LCD_WIDTH + HSW + HBP - 1;
    hltdc.Init.AccumulatedActiveH = LCD_HEIGHT + VSW + VBP - 1;
    hltdc.Init.TotalWidth = LCD_WIDTH + HSW + HBP + HFP - 1;
    hltdc.Init.TotalHeigh = LCD_HEIGHT + VSW + VBP + VFP - 1;

    hltdc.Init.Backcolor.Red = 0;   // 初始背景色，R
    hltdc.Init.Backcolor.Green = 0; // 初始背景色，G
    hltdc.Init.Backcolor.Blue = 255;  // 初始背景色，B

    HAL_LTDC_Init(&hltdc); // 初始化LTDC参数

    /*---------------------------------- layer0 显示配置 --------------------------------*/
    pLayerCfg.WindowX0 = 0;                 // 水平起点
    pLayerCfg.WindowX1 = LCD_WIDTH;         // 水平终点
    pLayerCfg.WindowY0 = 0;                 // 垂直起点
    pLayerCfg.WindowY1 = LCD_HEIGHT;        // 垂直终点
    pLayerCfg.ImageWidth = LCD_WIDTH;       // 显示区域宽度
    pLayerCfg.ImageHeight = LCD_HEIGHT;     // 显示区域高度
    pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;   // 颜色格式
    
    // 配置 layer0 的恒定透明度，最终写入 LTDC_LxCACR 寄存器
    // 需要注意的是，这个参数是直接配置整个 layer0 的透明度，这里设置为255即不透明
    pLayerCfg.Alpha = 255; // 取值范围0~255，255表示不透明，0表示完全透明

    // 设置 layer0 的层混合系数，最终写入 LTDC_LxBFCR 寄存器
    // 该参数用于设置 layer0 和 底层背景 之间的颜色混合系数，计算公式为 ：
    // 混合后的颜色 =  BF1 * layer0的颜色 + BF2 * 底层背景的颜色
    // 如果 layer0 使用了透明色，则必须配置成 LTDC_BLENDING_FACTOR1_PAxCA 和 LTDC_BLENDING_FACTOR2_PAxCA，否则ARGB中的A通道不起作用
    // 更多的介绍可以查阅 参考手册关于 LTDC_LxBFCR 寄存器的介绍
    pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA; // 混合系数1
    pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA; // 混合系数2

    pLayerCfg.FBStartAdress = LCD_MEMORY_ADDR; // 显存地址

    // 配置 layer0 的初始默认颜色，包括A,R,G,B 的值，最终写入 LTDC_LxDCCR 寄存器
    pLayerCfg.Alpha0 = 0;           // 初始颜色，A
    pLayerCfg.Backcolor.Blue = 0;   // 初始颜色，R
    pLayerCfg.Backcolor.Green = 255;  // 初始颜色，G
    pLayerCfg.Backcolor.Red = 0;    // 初始颜色，B

    HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0); // 配置layer0

#if ((COLOR_MODE_0 == LTDC_PIXEL_FORMAT_RGB888) || (COLOR_MODE_0 == LTDC_PIXEL_FORMAT_ARGB8888)) // 判断是否使用24位或者32位色
    // 因为750每个通道的低位都是采用伪随机抖动输出，如果不开启颜色抖动，则无法正常显示24位或者32位色
    HAL_LTDC_EnableDither(&hltdc); // 开启颜色抖动
#endif

    /*---------------------------------- layer1 显示配置 --------------------------------*/
#if (LCD_NUM_LAYERS == 2) // 如果定义了双层

    LTDC_LayerCfgTypeDef pLayerCfg1 = {0};

    pLayerCfg1.WindowX0 = 0;              // 水平起点
    pLayerCfg1.WindowX1 = LCD_WIDTH;      // 水平终点
    pLayerCfg1.WindowY0 = 0;              // 垂直起点
    pLayerCfg1.WindowY1 = LCD_HEIGHT;     // 垂直终点
    pLayerCfg1.ImageWidth = LCD_WIDTH;    // 显示区域宽度
    pLayerCfg1.ImageHeight = LCD_HEIGHT;  // 显示区域高度
    pLayerCfg1.PixelFormat = COLOR_MODE_1; // 颜色格式，layer1 应配置为带有透明色的格式，例如ARGB8888或ARGB1555

    // 配置 layer1 的恒定透明度，最终写入 LTDC_LxCACR 寄存器
    // 需要注意的是，这个参数是直接配置整个 layer1 的透明度，这里设置为255即不透明
    pLayerCfg1.Alpha = 255; // 取值范围0~255，255表示不透明，0表示完全透明

    // 设置 layer1 的层混合系数，最终写入 LTDC_LxBFCR 寄存器
    // 该参数用于设置 layer1 和 (layer0+背景）之间的颜色混合系数，计算公式为 ：
    // 混合后的颜色 =  BF1 * layer1的颜色 + BF2 * (layer0+背景混合后的颜色）
    // 如果 layer1 使用了透明色，则必须配置成 LTDC_BLENDING_FACTOR1_PAxCA 和 LTDC_BLENDING_FACTOR2_PAxCA，否则ARGB中的A通道不起作用
    // 更多的介绍可以查阅 参考手册关于 LTDC_LxBFCR 寄存器的介绍
    pLayerCfg1.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA; // 混合系数1
    pLayerCfg1.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA; // 混合系数2

    pLayerCfg1.FBStartAdress = LCD_MEMORY_ADDR + LCD_MemoryAdd_OFFSET; // 显存地址

    // 配置layer1 的初始默认颜色，包括A,R,G,B 的值 ，最终写入 LTDC_LxDCCR 寄存器
    pLayerCfg1.Alpha0 = 100;          // 初始颜色，A
    pLayerCfg1.Backcolor.Red = 0;   //    初始颜色，R
    pLayerCfg1.Backcolor.Green = 255; //    初始颜色，G
    pLayerCfg1.Backcolor.Blue = 0;  //    初始颜色，B

    HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg1, 1); // 初始化 layer1 的配置

#if ((COLOR_MODE_1 == LTDC_PIXEL_FORMAT_RGB888) || (COLOR_MODE_1 == LTDC_PIXEL_FORMAT_ARGB8888)) // 判断是否使用24位或者32位色
    // 因为750每个通道的低位都是采用伪随机抖动输出，如果不开启颜色抖动，则无法正常显示24位或者32位色
    HAL_LTDC_EnableDither(&hltdc); // 开启颜色抖动
#endif

#endif

#if 0  // 我的测试
    /*---------------------------------- 初始化一些默认配置 --------------------------------*/
    LCD_DisplayDirection(Direction_H);  // 设置横屏显示，LCD.Direction
    LCD_ShowNumMode(Fill_Space);        // 设置数字显示默认填充空格，LCD.ShowNum_Mode
    LCD_SetLayer(0);                    // 切换到 layer0，设置LCD.BytesPerPixel、LCD.ColorMode、LCD.LayerMemoryAddr、LCD.Layer
    LCD_SetBackColor(LCD_YELLOW);       // 设置背景色，LCD.BackColor
    LCD_SetColor(LCD_WHITE);            // 设置画笔颜色，LCD.Color
    LCD_Clear();                        // 清屏，刷背景色，使用了LCD.ColorMode、LCD.LayerMemoryAdd、LCD.BackColor
#endif
    
    /*---------------------------------- 如果开启了两层显示 --------------------------------*/
#if LCD_NUM_LAYERS == 2
    LCD_SetLayer(1);                    // 切换到前景层 layer1，layer1始终在layer0之上
    LCD_SetBackColor(LCD_BLACK);        // 设置背景色
    LCD_SetColor(LCD_WHITE);            // 设置画笔颜色
    LCD_Clear();                        // 清屏，刷背景色
#endif

    // LTDC在初始化之后，上电的瞬间会有一个短暂的白屏，
    // 即使一开始就将背光引脚拉低并且屏幕背光引脚用电阻下拉还是会有这个现象，
    // 如果需要消除这个现象，可以在初始化完毕之后，进行一个短暂的延时再打开背光
    HAL_Delay(200);    // 延时200ms

    LCD_BACKLIGHT_ON; // 开启背光
}

#if 1
/*************************************************************************************************
 *    函 数 名:    LCD_SetLayer
 *    入口参数:    layer - 要显示和操作的层，可以设置为0或1，即选择 layer0 或 layer1
 *    返 回 值:    无
 *    函数功能:    设置要显示和操作的层，切换相应的显存地址、颜色格式等
 *    说    明:    750的LTDC层顺序是固定的，layer1 在 layer0之上，即开启两层显示时，
 *                layer1 是前景层，通常使用带透明色的颜色格式，layer0 是背景层，
 *                只开启单层时，默认只操作 layer0
 *************************************************************************************************/

void LCD_SetLayer(uint8_t layer)
{
#if LCD_NUM_LAYERS == 2 // 如果开了双层

    if (layer == 0) // 如果设置的是 layer0
    {
        LCD.LayerMemoryAdd = LCD_MEMORY_ADDR;  // 获取 layer0 的显存地址
        LCD.ColorMode = COLOR_MODE_0;         // 获取 layer0 的颜色格式
        LCD.BytesPerPixel = BytesPerPixel_0; // 获取 layer0 的每个像素所需字节数的大小
    }
    else if (layer == 1) // 如果设置的是 layer1
    {
        LCD.LayerMemoryAdd = LCD_MEMORY_ADDR + LCD_MemoryAdd_OFFSET; // 获取 layer1 的显存地址
        LCD.ColorMode = COLOR_MODE_1;                               // 获取 layer1 的颜色格式
        LCD.BytesPerPixel = BytesPerPixel_1;                       // 获取 layer1 的每个像素所需字节数的大小
    }
    LCD.Layer = layer; // 记录当前所在的层

#else // 如果只开启单层，默认操作 layer0

    LCD.LayerMemoryAdd = LCD_MEMORY_ADDR;   // 获取 layer0 的显存地址
    LCD.ColorMode = COLOR_MODE_0;           // 获取 layer0 的颜色格式
    LCD.BytesPerPixel = BytesPerPixel_0;    // 获取 layer0 的每个像素所需字节数的大小
    LCD.Layer = 0;                          // 层标记设置为 layer0

#endif
}

/***************************************************************************************************************
 *    函 数 名:    LCD_SetColor
 *
 *    入口参数:    Color - 要显示的颜色，示例：0xff0000FF 表示不透明的蓝色，0xAA0000FF 表示透明度为66.66%的蓝色
 *
 *    函数功能:    此函数用于设置显示字符、画点画线、绘图的颜色
 *
 *    说    明:    1. 为了方便用户使用自定义颜色，入口参数 Color 使用32位的颜色格式，用户无需关心颜色格式的转换
 *                2. 32位的颜色中，从高位到低位分别对应 A、R、G、B  4个颜色通道
 *                3. 高8位的透明通道中，ff表示不透明，0表示完全透明
 *                4. 除非使用ARGB1555和ARGB8888等支持透明色的颜色格式，不然透明色不起作用，其中ARGB1555仅支持一位
 *                   透明色，即仅有透明和不透明两种状态，ARGB8888支持255级透明度
 *                5. 这里说到的透明，是指 背景层、layer0和layer1 之间的透明
 *
 ***************************************************************************************************************/

void LCD_SetColor(uint32_t Color)
{
    uint16_t Alpha_Value = 0, Red_Value = 0, Green_Value = 0, Blue_Value = 0; // 各个颜色通道的值

    if (LCD.ColorMode == LTDC_PIXEL_FORMAT_RGB565) // 将32位色转换为16位色
    {
        Red_Value = (uint16_t)((Color & 0x00F80000) >> 8);
        Green_Value = (uint16_t)((Color & 0x0000FC00) >> 5);
        Blue_Value = (uint16_t)((Color & 0x000000F8) >> 3);
        LCD.Color = (uint16_t)(Red_Value | Green_Value | Blue_Value);
    }
    else if (LCD.ColorMode == LTDC_PIXEL_FORMAT_ARGB1555) // 将32位色转换为ARGB1555颜色
    {
        if ((Color & 0xFF000000) == 0) // 判断是否使用透明色
            Alpha_Value = 0x0000;
        else
            Alpha_Value = 0x8000;

        Red_Value = (uint16_t)((Color & 0x00F80000) >> 9);
        Green_Value = (uint16_t)((Color & 0x0000F800) >> 6);
        Blue_Value = (uint16_t)((Color & 0x000000F8) >> 3);
        LCD.Color = (uint16_t)(Alpha_Value | Red_Value | Green_Value | Blue_Value);
    }
    else if (LCD.ColorMode == LTDC_PIXEL_FORMAT_ARGB4444) // 将32位色转换为ARGB4444颜色
    {

        Alpha_Value = (uint16_t)((Color & 0xf0000000) >> 16);
        Red_Value = (uint16_t)((Color & 0x00F00000) >> 12);
        Green_Value = (uint16_t)((Color & 0x0000F000) >> 8);
        Blue_Value = (uint16_t)((Color & 0x000000F8) >> 4);
        LCD.Color = (uint16_t)(Alpha_Value | Red_Value | Green_Value | Blue_Value);
    }
    else
        LCD.Color = Color; // 24位色或32位色不需要转换
}

/***************************************************************************************************************
 *    函 数 名:    LCD_SetBackColor
 *
 *    入口参数:    Color - 要显示的颜色，示例：0xff0000FF 表示不透明的蓝色，0xAA0000FF 表示透明度为66.66%的蓝色
 *
 *    函数功能:    设置背景色,此函数用于清屏以及显示字符的背景色
 *
 *    说    明:    1. 为了方便用户使用自定义颜色，入口参数 Color 使用32位的颜色格式，用户无需关心颜色格式的转换
 *                    2. 32位的颜色中，从高位到低位分别对应 A、R、G、B  4个颜色通道
 *                    3. 高8位的透明通道中，ff表示不透明，0表示完全透明
 *                    4. 除非使用ARGB1555和ARGB8888等支持透明色的颜色格式，不然透明色不起作用，其中ARGB1555仅支持一位
 *                        透明色，即仅有透明和不透明两种状态，ARGB8888支持255级透明度
 *                    5. 这里说到的透明，是指 背景层、layer0和layer1之间的透明
 *
 ***************************************************************************************************************/

void LCD_SetBackColor(uint32_t Color)
{
    uint16_t Alpha_Value = 0, Red_Value = 0, Green_Value = 0, Blue_Value = 0; // 各个颜色通道的值

    if (LCD.ColorMode == LTDC_PIXEL_FORMAT_RGB565) // 将32位色转换为16位色
    {
        Red_Value = (uint16_t)((Color & 0x00F80000) >> 8);
        Green_Value = (uint16_t)((Color & 0x0000FC00) >> 5);
        Blue_Value = (uint16_t)((Color & 0x000000F8) >> 3);
        LCD.BackColor = (uint16_t)(Red_Value | Green_Value | Blue_Value);
    }
    else if (LCD.ColorMode == LTDC_PIXEL_FORMAT_ARGB1555) // 将32位色转换为ARGB1555颜色
    {
        if ((Color & 0xFF000000) == 0) // 判断是否使用透明色
            Alpha_Value = 0x0000;
        else
            Alpha_Value = 0x8000;

        Red_Value = (uint16_t)((Color & 0x00F80000) >> 9);
        Green_Value = (uint16_t)((Color & 0x0000F800) >> 6);
        Blue_Value = (uint16_t)((Color & 0x000000F8) >> 3);
        LCD.BackColor = (uint16_t)(Alpha_Value | Red_Value | Green_Value | Blue_Value);
    }
    else if (LCD.ColorMode == LTDC_PIXEL_FORMAT_ARGB4444) // 将32位色转换为ARGB4444颜色
    {
        Alpha_Value = (uint16_t)((Color & 0xf0000000) >> 16);
        Red_Value = (uint16_t)((Color & 0x00F00000) >> 12);
        Green_Value = (uint16_t)((Color & 0x0000F000) >> 8);
        Blue_Value = (uint16_t)((Color & 0x000000F8) >> 4);
        LCD.BackColor = (uint16_t)(Alpha_Value | Red_Value | Green_Value | Blue_Value);
    }

    else
        LCD.BackColor = Color; // 24位色或32位色不需要转换
}

/***************************************************************************************************************
 *    函 数 名:    LCD_DisplayDirection
 *
 *    入口参数:    direction - 要显示的方向
 *
 *    函数功能:    设置要显示的方向，可输入参数 Direction_H 代表横屏显示，Direction_V 代表竖直显示
 *
 *    说    明:   使用示例 LCD_DisplayDirection(Direction_H) ，即设置屏幕横屏显示
 *
 ***************************************************************************************************************/

void LCD_DisplayDirection(uint8_t direction)
{
    LCD.Direction = direction;
}

/***************************************************************************************************************
 *    函 数 名:    LCD_Clear
 *
 *    函数功能:    清屏函数，将LCD清除为 LCD.BackColor 的颜色，使用DMA2D实现
 *
 *    说    明:    先用 LCD_SetBackColor() 设置要清除的背景色，再调用该函数清屏即可
 *
 ***************************************************************************************************************/
void LCD_Clear(void)
{
    DMA2D->CR &= ~(DMA2D_CR_START);                // 停止DMA2D
    DMA2D->CR = DMA2D_R2M;                         // 寄存器到SDRAM
    DMA2D->OPFCCR = LCD.ColorMode;                 // 设置颜色格式
    DMA2D->OOR = 0;                                // 设置行偏移
    DMA2D->OMAR = LCD.LayerMemoryAdd;              // 地址
    DMA2D->NLR = (LCD_WIDTH << 16) | (LCD_HEIGHT); // 设定长度和宽度
    DMA2D->OCOLR = LCD.BackColor;                  // 颜色

    /******
    等待 垂直数据使能显示状态 ，即LTDC即将刷完一整屏数据的时候
    因为在屏幕没有刷完一帧时进行刷屏，会有撕裂的现象
    用户也可以使用 寄存器重载中断 进行判断，不过为了保证例程的简洁以及移植的方便性，这里直接使用判断寄存器的方法

    如果不做判断，DMA2D刷屏速度如下

    颜色格式    RGB565     RGB888     ARGB888
    耗时       4.3ms        7.5ms      11.9ms

    加了之后，不管哪种格式，都需要17.6ms刷一屏，不过屏幕本身的刷新率只有60帧左右（LTDC时钟33MHz），
    17.6ms的速度已经足够了，除非是对速度要求特别高的场合，不然建议加上判断垂直等待的语句，可以避免撕裂效应

    ******/
    while (LTDC->CDSR != 0X00000001);   // 判断 显示状态寄存器LTDC_CDSR 的第0位 VDES：垂直数据使能显示状态
    DMA2D->CR |= DMA2D_CR_START;        // 启动DMA2D
    while (DMA2D->CR & DMA2D_CR_START); // 等待传输完成
}

/***************************************************************************************************************
 *    函 数 名:    LCD_ClearRect
 *
 *    入口参数:    x - 起始水平坐标，取值范围0~799
 *                    y - 起始垂直坐标，取值范围0~479
 *                    width  - 要清除区域的横向长度
 *                    height - 要清除区域的纵向宽度
 *
 *    函数功能:    局部清屏函数，将指定位置对应的区域清除为 LCD.BackColor 的颜色
 *
 *    说    明:    1. 先用 LCD_SetBackColor() 设置要清除的背景色，再调用该函数清屏即可
 *                    2. 使用示例 LCD_ClearRect( 10, 10, 100, 50) ，清除坐标(10,10)开始的长100宽50的区域
 *
 ***************************************************************************************************************/

void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{

    DMA2D->CR &= ~(DMA2D_CR_START); //    停止DMA2D
    DMA2D->CR = DMA2D_R2M;          //    寄存器到SDRAM
    DMA2D->OPFCCR = LCD.ColorMode;  //    设置颜色格式
    DMA2D->OCOLR = LCD.BackColor;   //    颜色

    if (LCD.Direction == Direction_H) // 横屏填充
    {
        DMA2D->OOR = LCD_WIDTH - width;                                             //    设置行偏移
        DMA2D->OMAR = LCD.LayerMemoryAdd + LCD.BytesPerPixel * (LCD_WIDTH * y + x); // 地址;
        DMA2D->NLR = (width << 16) | (height);                                      //    设定长度和宽度
    }
    else // 竖屏填充
    {
        DMA2D->OOR = LCD_WIDTH - height;                                                                       //    设置行偏移
        DMA2D->OMAR = LCD.LayerMemoryAdd + LCD.BytesPerPixel * ((LCD_HEIGHT - x - 1 - width) * LCD_WIDTH + y); // 地址
        DMA2D->NLR = (width) | (height << 16);                                                                 //    设定长度和宽度
    }

    DMA2D->CR |= DMA2D_CR_START; //    启动DMA2D

    while (DMA2D->CR & DMA2D_CR_START)
        ; //    等待传输完成
}

/***************************************************************************************************************
 *    函 数 名:    LCD_DrawPoint
 *
 *    入口参数:    x - 起始水平坐标，取值范围0~799
 *                    y - 起始垂直坐标，取值范围0~479
 *                    color  - 要绘制的颜色，使用32位的颜色格式，用户无需关心颜色格式的转换
 *
 *    函数功能:    在指定坐标绘制指定颜色的点
 *
 *    说    明:    1. 直接在对应的显存位置写入颜色值，即可实现画点的功能
 *                    2. 使用示例 LCD_DrawPoint( 10, 10, 0xff0000FF) ，在坐标(10,10)绘制蓝色的点
 *
 ***************************************************************************************************************/

void LCD_DrawPoint(uint16_t x, uint16_t y, uint32_t color)
{

    /*----------------------- 32位色 ARGB8888 模式 ----------------------*/

    if (LCD.ColorMode == LTDC_PIXEL_FORMAT_ARGB8888)
    {
        if (LCD.Direction == Direction_H) // 水平方向
        {
            *(__IO uint32_t *)(LCD.LayerMemoryAdd + 4 * (x + y * LCD_WIDTH)) = color;
        }
        else if (LCD.Direction == Direction_V) // 垂直方向
        {
            *(__IO uint32_t *)(LCD.LayerMemoryAdd + 4 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y)) = color;
        }
    }
    /*----------------------------- 24位色 RGB888 模式 -------------------------*/

    else if (LCD.ColorMode == LTDC_PIXEL_FORMAT_RGB888)
    {
        if (LCD.Direction == Direction_H) // 水平方向
        {
            *(__IO uint16_t *)(LCD.LayerMemoryAdd + 3 * (x + y * LCD_WIDTH)) = color;
            *(__IO uint8_t *)(LCD.LayerMemoryAdd + 3 * (x + y * LCD_WIDTH) + 2) = color >> 16;
        }
        else if (LCD.Direction == Direction_V) // 垂直方向
        {
            *(__IO uint16_t *)(LCD.LayerMemoryAdd + 3 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y)) = color;
            *(__IO uint8_t *)(LCD.LayerMemoryAdd + 3 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y) + 2) = color >> 16;
        }
    }

    /*----------------------- 16位色 ARGB1555、RGB565或者ARGB4444 模式 ----------------------*/
    else
    {
        if (LCD.Direction == Direction_H) // 水平方向
        {
            *(__IO uint16_t *)(LCD.LayerMemoryAdd + 2 * (x + y * LCD_WIDTH)) = color;
        }
        else if (LCD.Direction == Direction_V) // 垂直方向
        {
            *(__IO uint16_t *)(LCD.LayerMemoryAdd + 2 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y)) = color;
        }
    }
}

/***************************************************************************************************************
 *    函 数 名:    LCD_ReadPoint
 *
 *    入口参数:    x - 起始水平坐标，取值范围0~799
 *                    y - 起始垂直坐标，取值范围0~479
 *
 *    返 回 值：  读取到的颜色
 *
 *    函数功能:    读取指定坐标点的颜色，在使用16或24位色模式时，读出来的颜色数据对应为16位或24位
 *
 *    说    明:    1. 直接读取对应的显存值，即可实现读点的功能
 *                    2. 使用示例 color = LCD_ReadPoint( 10, 10) ，color 为读取到的坐标点(10,10)的颜色
 *
 ***************************************************************************************************************/

uint32_t LCD_ReadPoint(uint16_t x, uint16_t y)
{
    uint32_t color = 0;

    /*----------------------- 32位色 ARGB8888 模式 ----------------------*/
    if (LCD.ColorMode == LTDC_PIXEL_FORMAT_ARGB8888)
    {
        if (LCD.Direction == Direction_H) // 水平方向
        {
            color = *(__IO uint32_t *)(LCD.LayerMemoryAdd + 4 * (x + y * LCD_WIDTH));
        }
        else if (LCD.Direction == Direction_V) // 垂直方向
        {
            color = *(__IO uint32_t *)(LCD.LayerMemoryAdd + 4 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y));
        }
    }

    /*----------------------------- 24位色 RGB888 模式 -------------------------*/
    else if (LCD.ColorMode == LTDC_PIXEL_FORMAT_RGB888)
    {
        if (LCD.Direction == Direction_H) // 水平方向
        {
            color = *(__IO uint32_t *)(LCD.LayerMemoryAdd + 3 * (x + y * LCD_WIDTH)) & 0x00ffffff;
        }
        else if (LCD.Direction == Direction_V) // 垂直方向
        {
            color = *(__IO uint32_t *)(LCD.LayerMemoryAdd + 3 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y)) & 0x00ffffff;
        }
    }

    /*----------------------- 16位色 ARGB1555、RGB565或者ARGB4444 模式 ----------------------*/
    else
    {
        if (LCD.Direction == Direction_H) // 水平方向
        {
            color = *(__IO uint16_t *)(LCD.LayerMemoryAdd + 2 * (x + y * LCD_WIDTH));
        }
        else if (LCD.Direction == Direction_V) // 垂直方向
        {
            color = *(__IO uint16_t *)(LCD.LayerMemoryAdd + 2 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y));
        }
    }
    return color;
}


/***************************************************************************************************************
 *    函 数 名:    LCD_ShowNumMode
 *
 *    入口参数:    mode - 设置变量的显示模式
 *
 *    函数功能:    设置变量显示时多余位补0还是补空格，可输入参数 Fill_Space 填充空格，Fill_Zero 填充零
 *
 *    说    明:   1. 只有 LCD_DisplayNumber() 显示整数 和 LCD_DisplayDecimals()显示小数 这两个函数用到
 *                    2. 使用示例 LCD_ShowNumMode(Fill_Zero) 设置多余位填充0，例如 123 可以显示为 000123
 *
 ***************************************************************************************************************/

void LCD_ShowNumMode(uint8_t mode)
{
    LCD.ShowNum_Mode = mode;
}

/***************************************************************************************************************************************
 *    函 数 名: LCD_DrawImage
 *
 *    入口参数: x - 起始水平坐标，取值范围0~799
 *                 y - 起始垂直坐标，取值范围0~479
 *                  width  - 图片的水平宽度，最大取值800
 *                 height - 图片的垂直宽度，最大取值480
 *                *pImage - 图片数据存储区的首地址
 *
 *    函数功能: 在指定坐标处显示图片
 *
 *    说    明: 要显示的图片需要事先进行取模，且只能显示一种颜色，使用 LCD_SetColor() 函数设置画笔色
 *
 *****************************************************************************************************************************************/

void LCD_DrawImage(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *pImage)
{
    uint8_t disChar;       // 字模的值
    uint16_t Xaddress = x; // 水平坐标
    uint16_t i = 0, j = 0, m = 0;

    for (i = 0; i < height; i++)
    {
        for (j = 0; j < (float)width / 8; j++)
        {
            disChar = *pImage;

            for (m = 0; m < 8; m++)
            {
                if (disChar & 0x01)
                {
                    LCD_DrawPoint(Xaddress, y, LCD.Color); // 当前模值不为0时，使用画笔色绘点
                }
                else
                {
                    LCD_DrawPoint(Xaddress, y, LCD.BackColor); // 否则使用背景色绘制点
                }
                disChar >>= 1;
                Xaddress++; // 水平坐标自加

                if ((Xaddress - x) == width) // 如果水平坐标达到了字符宽度，则退出当前循环
                {                            // 进入下一行的绘制
                    Xaddress = x;
                    y++;
                    break;
                }
            }
            pImage++;
        }
    }
}

/***************************************************************************************************************************************
 *    函 数 名: LCD_DrawLine
 *
 *    入口参数: x1 - 起点 水平坐标，取值范围 0~799
 *                  y1 - 起点 垂直坐标，取值范围 0~479
 *
 *                 x2 - 终点 水平坐标，取值范围 0~799
 *            y2 - 终点 垂直坐标，取值范围 0~479
 *
 *    函数功能: 在两点之间画线
 *
 *    说    明: 该函数移植于ST官方评估板的例程
 *
 *****************************************************************************************************************************************/

#define ABS(X) ((X) > 0 ? (X) : -(X))

void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    int16_t deltax = 0, deltay = 0, x = 0, y = 0, xinc1 = 0, xinc2 = 0,
            yinc1 = 0, yinc2 = 0, den = 0, num = 0, numadd = 0, numpixels = 0,
            curpixel = 0;

    deltax = ABS(x2 - x1); /* The difference between the x's */
    deltay = ABS(y2 - y1); /* The difference between the y's */
    x = x1;                /* Start x off at the first pixel */
    y = y1;                /* Start y off at the first pixel */

    if (x2 >= x1) /* The x-values are increasing */
    {
        xinc1 = 1;
        xinc2 = 1;
    }
    else /* The x-values are decreasing */
    {
        xinc1 = -1;
        xinc2 = -1;
    }

    if (y2 >= y1) /* The y-values are increasing */
    {
        yinc1 = 1;
        yinc2 = 1;
    }
    else /* The y-values are decreasing */
    {
        yinc1 = -1;
        yinc2 = -1;
    }

    if (deltax >= deltay) /* There is at least one x-value for every y-value */
    {
        xinc1 = 0; /* Don't change the x when numerator >= denominator */
        yinc2 = 0; /* Don't change the y for every iteration */
        den = deltax;
        num = deltax / 2;
        numadd = deltay;
        numpixels = deltax; /* There are more x-values than y-values */
    }
    else /* There is at least one y-value for every x-value */
    {
        xinc2 = 0; /* Don't change the x for every iteration */
        yinc1 = 0; /* Don't change the y when numerator >= denominator */
        den = deltay;
        num = deltay / 2;
        numadd = deltax;
        numpixels = deltay; /* There are more y-values than x-values */
    }
    for (curpixel = 0; curpixel <= numpixels; curpixel++)
    {
        LCD_DrawPoint(x, y, LCD.Color); /* Draw the current pixel */
        num += numadd;                  /* Increase the numerator by the top of the fraction */
        if (num >= den)                 /* Check if numerator >= denominator */
        {
            num -= den; /* Calculate the new numerator value */
            x += xinc1; /* Change the x as appropriate */
            y += yinc1; /* Change the y as appropriate */
        }
        x += xinc2; /* Change the x as appropriate */
        y += yinc2; /* Change the y as appropriate */
    }
}

/***************************************************************************************************************************************
 *    函 数 名: LCD_DrawRect
 *
 *    入口参数: x - 起始水平坐标，取值范围0~799
 *                 y - 起始垂直坐标，取值范围0~479
 *                  width  - 图片的水平宽度，最大取值800
 *                 height - 图片的垂直宽度，最大取值480
 *
 *    函数功能: 在指点位置绘制指定长宽的矩形线条
 *
 *    说    明: 该函数移植于ST官方评估板的例程
 *
 *****************************************************************************************************************************************/

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    /* draw horizontal lines */
    LCD_DrawLine(x, y, x + width, y);
    LCD_DrawLine(x, y + height, x + width, y + height);

    /* draw vertical lines */
    LCD_DrawLine(x, y, x, y + height);
    LCD_DrawLine(x + width, y, x + width, y + height);
}

/***************************************************************************************************************************************
 *    函 数 名: LCD_DrawCircle
 *
 *    入口参数: x - 圆心 水平坐标，取值范围 0~799
 *                  y - 圆心 垂直坐标，取值范围 0~479
 *                  r  - 半径
 *
 *    函数功能: 在坐标 (x,y) 绘制半径为 r 的圆形线条
 *
 *    说    明: 1. 该函数移植于ST官方评估板的例程
 *             2. 要绘制的区域不能超过屏幕的显示区域
 *
 *****************************************************************************************************************************************/
void LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r)
{
    int Xadd = -r, Yadd = 0, err = 2 - 2 * r, e2;
    do
    {
        LCD_DrawPoint(x - Xadd, y + Yadd, LCD.Color);
        LCD_DrawPoint(x + Xadd, y + Yadd, LCD.Color);
        LCD_DrawPoint(x + Xadd, y - Yadd, LCD.Color);
        LCD_DrawPoint(x - Xadd, y - Yadd, LCD.Color);

        e2 = err;
        if (e2 <= Yadd)
        {
            err += ++Yadd * 2 + 1;
            if (-Xadd == Yadd && e2 <= Xadd)
                e2 = 0;
        }
        if (e2 > Xadd)
            err += ++Xadd * 2 + 1;
    } while (Xadd <= 0);
}

/***************************************************************************************************************************************
 *    函 数 名: LCD_FillRect
 *
 *    入口参数: x - 水平坐标，取值范围 0~799
 *                  y - 垂直坐标，取值范围 0~479
 *                  width  - 图片的水平宽度，最大取值800
 *                 height - 图片的垂直宽度，最大取值480
 *
 *    函数功能: 在坐标 (x,y) 填充指定长宽的实心矩形
 *
 *    说    明: 1. 使用DMA2D实现
 *                 2. 要绘制的区域不能超过屏幕的显示区域
 *
 *****************************************************************************************************************************************/

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    DMA2D->CR &= ~(DMA2D_CR_START); //    停止DMA2D
    DMA2D->CR = DMA2D_R2M;          //    寄存器到SDRAM
    DMA2D->OPFCCR = LCD.ColorMode;  //    设置颜色格式
    DMA2D->OCOLR = LCD.Color;       //    颜色

    if (LCD.Direction == Direction_H) // 横屏填充
    {
        DMA2D->OOR = LCD_WIDTH - width;                                             //    设置行偏移
        DMA2D->OMAR = LCD.LayerMemoryAdd + LCD.BytesPerPixel * (LCD_WIDTH * y + x); // 地址;
        DMA2D->NLR = (width << 16) | (height);                                      //    设定长度和宽度
    }
    else // 竖屏填充
    {
        DMA2D->OOR = LCD_WIDTH - height;                                                                       //    设置行偏移
        DMA2D->OMAR = LCD.LayerMemoryAdd + LCD.BytesPerPixel * ((LCD_HEIGHT - x - 1 - width) * LCD_WIDTH + y); // 地址
        DMA2D->NLR = (width) | (height << 16);                                                                 //    设定长度和宽度
    }

    DMA2D->CR |= DMA2D_CR_START; //    启动DMA2D

    while (DMA2D->CR & DMA2D_CR_START); //    等待传输完成
}


/***************************************************************************************************************************************
 *    函 数 名: LCD_FillCircle
 *
 *    入口参数: x - 圆心 水平坐标，取值范围 0~799
 *                  y - 圆心 垂直坐标，取值范围 0~479
 *                  r  - 半径
 *
 *    函数功能: 在坐标 (x,y) 填充半径为 r 的圆形区域
 *
 *    说    明: 1. 该函数移植于ST官方评估板的例程
 *                 2. 要绘制的区域不能超过屏幕的显示区域
 *
 *****************************************************************************************************************************************/

void LCD_FillCircle(uint16_t x, uint16_t y, uint16_t r)
{
    int32_t D;     /* Decision Variable */
    uint32_t CurX; /* Current X Value */
    uint32_t CurY; /* Current Y Value */

    D = 3 - (r << 1);

    CurX = 0;
    CurY = r;

    while (CurX <= CurY)
    {
        if (CurY > 0)
        {
            LCD_DrawLine(x - CurX, y - CurY, x - CurX, y - CurY + 2 * CurY);
            LCD_DrawLine(x + CurX, y - CurY, x + CurX, y - CurY + 2 * CurY);
        }

        if (CurX > 0)
        {
            LCD_DrawLine(x - CurY, y - CurX, x - CurY, y - CurX + 2 * CurX);
            LCD_DrawLine(x + CurY, y - CurX, x + CurY, y - CurX + 2 * CurX);
        }
        if (D < 0)
        {
            D += (CurX << 2) + 6;
        }
        else
        {
            D += ((CurX - CurY) << 2) + 10;
            CurY--;
        }
        CurX++;
    }

    LCD_DrawCircle(x, y, r);
}

#endif

