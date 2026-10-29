#include "bsp_lcd.h"
#include "bsp_types.h"
#include "ltdc.h"
#include "dma2d.h"

// 1. LTDC时钟在 main.c 文件中 SystemClock_Config() 函数设置，例程默认为33MHz，
//    即刷新率在60帧左右，过高的时钟会加大对SDRAM的占用，容易造成花屏，并且屏
//    幕本身不支持这么高的刷新率，还会导致屏幕闪烁显示异常等，过低的时钟会导
//    致刷新率太低，屏幕会有明显的闪烁
// 2. 只开单层显示，16位色、RGB888以及ARGB8888模式下，LTDC时钟可以设置为33M
// 3. 开双层显示，RGB565和ARGB1555模式下，LTDC时钟可以设置为33M
// 4. 开双层显示，RGB565和ARGB8888模式下，LTDC时钟最大只能设置为28M（不同的面板会有所差异，需要根据实际情况调整）
// 5. 开双层显示，RGB888和ARGB1555模式下，LTDC时钟最大只能设置为33M（不同的面板会有所差异，需要根据实际情况调整）
// 6. 开双层显示，RGB888和ARGB8888模式下，LTDC时钟最大只能设置为24M（不同的面板会有所差异，需要根据实际情况调整）
// 7. 综上，反客STM32H750XBH6核心板开启双层时，推荐使用 16位色+16位色 或者 24位色+16位色 的组合，这样可以确保屏幕工作在最佳的刷新率

// 板载LCD面板固定配置
// PH6 ------> LCD背光
// 核心板使用的是外部SDRAM作为显存，起始地址0xC0000000
// 显存所需空间=分辨率*每个像素所占字节数，例如800*480的屏，使用16位色（RGB565或者AEGB1555），需要显存 800*480*2B = 768000B = 750KB
#define LCD_HSYNC_WIDTH                 1U
#define LCD_HBACK_PORCH                 46U
#define LCD_HFRONT_PORCH                210U
#define LCD_VSYNC_WIDTH                 3U
#define LCD_VBACK_PORCH                 23U
#define LCD_VFRONT_PORCH                22U
#define LCD_LAYER_PIXEL_FORMAT          PLATFORM_LTDC_PIXEL_FORMAT_RGB565
#define LCD_BYTES_PER_PIXEL(format)     \
        (((format) == PLATFORM_LTDC_PIXEL_FORMAT_RGB888) ? 3U :   \
         ((format) == PLATFORM_LTDC_PIXEL_FORMAT_ARGB8888) ? 4U : 2U)
#define LCD_BACKLIGHT_PORT              GPIOH
#define LCD_BACKLIGHT_PIN               GPIO_PIN_6

// LCD 绘图上下文，仅供本文件内部使用
typedef struct
{
    uint32_t draw_color;          // 当前画笔颜色
    uint32_t background_color;    // 当前背景颜色
    uint32_t pixel_format;        // 当前层像素格式
    uint32_t framebuffer_address; // 当前层显存起始地址
    uint8_t  bytes_per_pixel;     // 每个像素占用的字节数
    bsp_lcd_orientation_t orientation;  // 显示方向
    bsp_lcd_number_fill_mode_t number_fill_mode;  // 数字空位填充方式
} bsp_lcd_context_t;

static bsp_lcd_context_t lcd_context = {
    .draw_color = LCD_COLOR_WHITE,
    .background_color = LCD_COLOR_BLACK,
    .pixel_format = LCD_LAYER_PIXEL_FORMAT,
    .framebuffer_address = LCD_FRAMEBUFFER_BASE_ADDR,
    .orientation = BSP_LCD_ORIENTATION_LANDSCAPE,
    .bytes_per_pixel = LCD_BYTES_PER_PIXEL(LCD_LAYER_PIXEL_FORMAT),
    .number_fill_mode = BSP_LCD_NUMBER_FILL_SPACE,
};

// LCD 面板与 LTDC 的固定硬件配置，初始化后保持只读
static const platform_ltdc_config_t ltdc_config = {
    .width = LCD_WIDTH,
    .height = LCD_HEIGHT,
    .hsync = LCD_HSYNC_WIDTH,
    .hbp = LCD_HBACK_PORCH,
    .hfp = LCD_HFRONT_PORCH,
    .vsync = LCD_VSYNC_WIDTH,
    .vbp = LCD_VBACK_PORCH,
    .vfp = LCD_VFRONT_PORCH,
    .layer_framebuffer_address = LCD_FRAMEBUFFER_BASE_ADDR,
    .layer_pixel_format = (platform_ltdc_pixel_format_t)LCD_LAYER_PIXEL_FORMAT,
};

// 内部函数声明
static void bsp_lcd_backlight_init(void);


// GPIOH6作为LCD背光控制引脚，低电平关闭背光，高电平开启背光
static void bsp_lcd_backlight_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();

    gpio_init.Pin   = LCD_BACKLIGHT_PIN;
    gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull  = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LCD_BACKLIGHT_PORT, &gpio_init);

    bsp_lcd_set_backlight(false); // 默认关闭背光，初始化LTDC之后再开启
}

bsp_status_t bsp_lcd_init(void)
{
    HAL_StatusTypeDef status;

    bsp_lcd_backlight_init(); // 关闭背光
    status = platform_ltdc_init(&ltdc_config);
    platform_dma2d_init();

    // LTDC在初始化之后，上电的瞬间会有一个短暂的白屏，
    // 即使一开始就将背光引脚拉低并且屏幕背光引脚用电阻下拉还是会有这个现象，
    // 如果需要消除这个现象，可以在初始化完毕之后，进行一个短暂的延时再打开背光
    HAL_Delay(200);    // 延时200ms
    bsp_lcd_set_backlight(true); // 打开背光

    if (status != HAL_OK)
    {
        return BSP_ERROR;
    }
    return BSP_OK;
}

// 设置LCD背光，enabled为true表示开启背光，false表示关闭背光
void bsp_lcd_set_backlight(bool enabled)
{
    HAL_GPIO_WritePin(LCD_BACKLIGHT_PORT, LCD_BACKLIGHT_PIN, (enabled ? GPIO_PIN_SET : GPIO_PIN_RESET));
}

/***************************************************************************************************************
 *    函 数 名:    bsp_lcd_set_draw_color
 *
 *    入口参数:    color - 要显示的颜色，示例：0xff0000FF 表示不透明的蓝色，0xAA0000FF 表示透明度为66.66%的蓝色
 *
 *    函数功能:    此函数用于设置显示字符、画点画线、绘图的颜色
 *
 *    说    明:    1. 为了方便用户使用自定义颜色，入口参数 color 使用32位的颜色格式，用户无需关心颜色格式的转换
 *                2. 32位的颜色中，从高位到低位分别对应 A、R、G、B  4个颜色通道
 *                3. 高8位的透明通道中，ff表示不透明，0表示完全透明
 *                4. 除非使用ARGB1555和ARGB8888等支持透明色的颜色格式，不然透明色不起作用，其中ARGB1555仅支持一位
 *                   透明色，即仅有透明和不透明两种状态，ARGB8888支持255级透明度
 *                5. 这里说到的透明，是指 背景层、layer0和layer1 之间的透明
 *
 ***************************************************************************************************************/
void bsp_lcd_set_draw_color(uint32_t color)
{
    uint16_t alpha_value = 0, red_value = 0, green_value = 0, blue_value = 0; // 各个颜色通道的值
    
    if (lcd_context.pixel_format == PLATFORM_LTDC_PIXEL_FORMAT_RGB565) // 将32位色转换为16位色
    {
        red_value = (uint16_t)((color & 0x00F80000) >> 8);
        green_value = (uint16_t)((color & 0x0000FC00) >> 5);
        blue_value = (uint16_t)((color & 0x000000F8) >> 3);
        lcd_context.draw_color = (uint16_t)(red_value | green_value | blue_value);
    }
    else if (lcd_context.pixel_format == PLATFORM_LTDC_PIXEL_FORMAT_ARGB1555) // 将32位色转换为ARGB1555颜色
    {
        if ((color & 0xFF000000) == 0) // 判断是否使用透明色
        {
            alpha_value = 0x0000;
        }
        else
        {
            alpha_value = 0x8000;
        }
        red_value = (uint16_t)((color & 0x00F80000) >> 9);
        green_value = (uint16_t)((color & 0x0000F800) >> 6);
        blue_value = (uint16_t)((color & 0x000000F8) >> 3);
        lcd_context.draw_color = (uint16_t)(alpha_value | red_value | green_value | blue_value);
    }
    else if (lcd_context.pixel_format == PLATFORM_LTDC_PIXEL_FORMAT_ARGB4444) // 将32位色转换为ARGB4444颜色
    {
        alpha_value = (uint16_t)((color & 0xf0000000) >> 16);
        red_value = (uint16_t)((color & 0x00F00000) >> 12);
        green_value = (uint16_t)((color & 0x0000F000) >> 8);
        blue_value = (uint16_t)((color & 0x000000F8) >> 4);
        lcd_context.draw_color = (uint16_t)(alpha_value | red_value | green_value | blue_value);
    }
    else
    {
        lcd_context.draw_color = color; // 24位色或32位色不需要转换
    }
}

/***************************************************************************************************************
 *    函 数 名:    bsp_lcd_set_background_color
 *
 *    入口参数:    color - 要显示的颜色，示例：0xff0000FF 表示不透明的蓝色，0xAA0000FF 表示透明度为66.66%的蓝色
 *
 *    函数功能:    设置背景色,此函数用于清屏以及显示字符的背景色
 *
 *    说    明:    1. 为了方便用户使用自定义颜色，入口参数 color 使用32位的颜色格式，用户无需关心颜色格式的转换
 *                    2. 32位的颜色中，从高位到低位分别对应 A、R、G、B  4个颜色通道
 *                    3. 高8位的透明通道中，ff表示不透明，0表示完全透明
 *                    4. 除非使用ARGB1555和ARGB8888等支持透明色的颜色格式，不然透明色不起作用，其中ARGB1555仅支持一位
 *                        透明色，即仅有透明和不透明两种状态，ARGB8888支持255级透明度
 *                    5. 这里说到的透明，是指 背景层、layer0和layer1之间的透明
 *
 ***************************************************************************************************************/

void bsp_lcd_set_background_color(uint32_t color)
{
    uint16_t alpha_value = 0, red_value = 0, green_value = 0, blue_value = 0; // 各个颜色通道的值

    if (lcd_context.pixel_format == PLATFORM_LTDC_PIXEL_FORMAT_RGB565) // 将32位色转换为16位色
    {
        red_value = (uint16_t)((color & 0x00F80000) >> 8);
        green_value = (uint16_t)((color & 0x0000FC00) >> 5);
        blue_value = (uint16_t)((color & 0x000000F8) >> 3);
        lcd_context.background_color = (uint16_t)(red_value | green_value | blue_value);
    }
    else if (lcd_context.pixel_format == PLATFORM_LTDC_PIXEL_FORMAT_ARGB1555) // 将32位色转换为ARGB1555颜色
    {
        if ((color & 0xFF000000) == 0) // 判断是否使用透明色
        {
            alpha_value = 0x0000;
        }
        else
        {
            alpha_value = 0x8000;
        }
        red_value = (uint16_t)((color & 0x00F80000) >> 9);
        green_value = (uint16_t)((color & 0x0000F800) >> 6);
        blue_value = (uint16_t)((color & 0x000000F8) >> 3);
        lcd_context.background_color = (uint16_t)(alpha_value | red_value | green_value | blue_value);
    }
    else if (lcd_context.pixel_format == PLATFORM_LTDC_PIXEL_FORMAT_ARGB4444) // 将32位色转换为ARGB4444颜色
    {
        alpha_value = (uint16_t)((color & 0xf0000000) >> 16);
        red_value = (uint16_t)((color & 0x00F00000) >> 12);
        green_value = (uint16_t)((color & 0x0000F000) >> 8);
        blue_value = (uint16_t)((color & 0x000000F8) >> 4);
        lcd_context.background_color = (uint16_t)(alpha_value | red_value | green_value | blue_value);
    }
    else
    {
        lcd_context.background_color = color; // 24位色或32位色不需要转换
    }
}

/***************************************************************************************************************
 *    函 数 名:    bsp_lcd_set_orientation
 *
 *    入口参数:    direction - 要显示的方向
 *
 *    函数功能:    设置要显示的方向，可输入参数 BSP_LCD_ORIENTATION_LANDSCAPE 代表横屏显示，BSP_LCD_ORIENTATION_PORTRAIT 代表竖直显示
 *
 *    说    明:   使用示例 bsp_lcd_set_orientation(BSP_LCD_ORIENTATION_LANDSCAPE)，即设置屏幕横屏显示
 *
 ***************************************************************************************************************/
void bsp_lcd_set_orientation(bsp_lcd_orientation_t orientation)
{
    lcd_context.orientation = orientation;
}

/***************************************************************************************************************
 *    函 数 名:    bsp_lcd_clear_dma2d
 *
 *    函数功能:    使用 DMA2D 将整个 LCD 清除为当前背景颜色
 *
 *    说    明:    先用 bsp_lcd_set_background_color() 设置背景色，再调用该函数清屏
 *
 ***************************************************************************************************************/
void bsp_lcd_clear_dma2d(void)
{
    DMA2D->CR &= ~(DMA2D_CR_START);                // 停止DMA2D
    DMA2D->CR = DMA2D_R2M;                         // 寄存器到SDRAM
    DMA2D->OPFCCR = lcd_context.pixel_format;      // 设置颜色格式
    DMA2D->OOR = 0;                                // 设置行偏移
    DMA2D->OMAR = lcd_context.framebuffer_address; // 地址
    DMA2D->NLR = (LCD_WIDTH << 16) | (LCD_HEIGHT); // 设定长度和宽度
    DMA2D->OCOLR = lcd_context.background_color;   // 颜色

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
 *    函 数 名:    bsp_lcd_clear_rect_dma2d
 *
 *    入口参数:    x - 起始水平坐标，取值范围0~799
 *                    y - 起始垂直坐标，取值范围0~479
 *                    width  - 要清除区域的横向长度
 *                    height - 要清除区域的纵向宽度
 *
 *    函数功能:    使用当前背景颜色清除指定区域
 *
 *    说    明:    1. 先用 bsp_lcd_set_background_color() 设置背景色，再调用该函数
 *                 2. 使用示例 bsp_lcd_clear_rect_dma2d(10, 10, 100, 50)
 *
 ***************************************************************************************************************/

void bsp_lcd_clear_rect_dma2d(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    DMA2D->CR &= ~(DMA2D_CR_START); // 停止DMA2D
    DMA2D->CR = DMA2D_R2M;          // 寄存器到SDRAM
    DMA2D->OPFCCR = lcd_context.pixel_format;    // 设置颜色格式
    DMA2D->OCOLR = lcd_context.background_color; // 颜色

    if (lcd_context.orientation == BSP_LCD_ORIENTATION_LANDSCAPE)  // 横屏填充
    {
        DMA2D->OOR = LCD_WIDTH - width;                                             //    设置行偏移
        DMA2D->OMAR = lcd_context.framebuffer_address + lcd_context.bytes_per_pixel * (LCD_WIDTH * y + x); // 地址
        DMA2D->NLR = (width << 16) | (height);                                      //    设定长度和宽度
    }
    else // 竖屏填充
    {
        DMA2D->OOR = LCD_WIDTH - height;                                                                       //    设置行偏移
        DMA2D->OMAR = lcd_context.framebuffer_address + lcd_context.bytes_per_pixel * ((LCD_HEIGHT - x - 1 - width) * LCD_WIDTH + y); // 地址
        DMA2D->NLR = (width) | (height << 16);                                                                 //    设定长度和宽度
    }

    DMA2D->CR |= DMA2D_CR_START; // 启动DMA2D
    while (DMA2D->CR & DMA2D_CR_START); // 等待传输完成
}

/***************************************************************************************************************
 *    函 数 名:    bsp_lcd_draw_pixel
 *
 *    入口参数:    x - 起始水平坐标，取值范围0~799
 *                    y - 起始垂直坐标，取值范围0~479
 *                    color  - 要绘制的颜色，使用32位的颜色格式，用户无需关心颜色格式的转换
 *
 *    函数功能:    在指定坐标绘制指定颜色的点
 *
 *    说    明:    1. 直接在对应的显存位置写入颜色值，即可实现画点的功能
 *                    2. 使用示例 bsp_lcd_draw_pixel(10, 10, 0xff0000FF)
 *
 ***************************************************************************************************************/

void bsp_lcd_draw_pixel(uint16_t x, uint16_t y, uint32_t color)
{
    if (lcd_context.pixel_format == PLATFORM_LTDC_PIXEL_FORMAT_ARGB8888)
    {
        if (lcd_context.orientation == BSP_LCD_ORIENTATION_LANDSCAPE)
        {
            BSP_REG32(lcd_context.framebuffer_address + 4 * (x + y * LCD_WIDTH)) = color;
        }
        else
        {
            BSP_REG32(lcd_context.framebuffer_address + 4 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y)) = color;
        }
    }
    else if (lcd_context.pixel_format == PLATFORM_LTDC_PIXEL_FORMAT_RGB888)
    {
        if (lcd_context.orientation == BSP_LCD_ORIENTATION_LANDSCAPE)
        {
            BSP_REG16(lcd_context.framebuffer_address + 3 * (x + y * LCD_WIDTH)) = color;
            BSP_REG08(lcd_context.framebuffer_address + 3 * (x + y * LCD_WIDTH) + 2) = color >> 16;
        }
        else
        {
            BSP_REG16(lcd_context.framebuffer_address + 3 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y)) = color;
            BSP_REG08(lcd_context.framebuffer_address + 3 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y) + 2) = color >> 16;
        }
    }
    else
    {
        if (lcd_context.orientation == BSP_LCD_ORIENTATION_LANDSCAPE)
        {
            BSP_REG16(lcd_context.framebuffer_address + 2 * (x + y * LCD_WIDTH)) = color;
        }
        else
        {
            BSP_REG16(lcd_context.framebuffer_address + 2 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y)) = color;
        }
    }
}

/***************************************************************************************************************
 *    函 数 名:    bsp_lcd_read_pixel
 *
 *    入口参数:    x - 起始水平坐标，取值范围0~799
 *                    y - 起始垂直坐标，取值范围0~479
 *
 *    返 回 值：  读取到的颜色
 *
 *    函数功能:    读取指定坐标点的颜色，在使用16或24位色模式时，读出来的颜色数据对应为16位或24位
 *
 *    说    明:    1. 直接读取对应的显存值，即可实现读点的功能
 *                    2. 使用示例 color = bsp_lcd_read_pixel(10, 10)
 *
 ***************************************************************************************************************/

uint32_t bsp_lcd_read_pixel(uint16_t x, uint16_t y)
{
    uint32_t color = 0;

    if (lcd_context.pixel_format == PLATFORM_LTDC_PIXEL_FORMAT_ARGB8888) // 32位色
    {
        if (lcd_context.orientation == BSP_LCD_ORIENTATION_LANDSCAPE)
        {
            color = BSP_REG32(lcd_context.framebuffer_address + 4 * (x + y * LCD_WIDTH));
        }
        else
        {
            color = BSP_REG32(lcd_context.framebuffer_address + 4 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y));
        }
    }
    else if (lcd_context.pixel_format == PLATFORM_LTDC_PIXEL_FORMAT_RGB888) // 24位色
    {
        if (lcd_context.orientation == BSP_LCD_ORIENTATION_LANDSCAPE)
        {
            color = BSP_REG32(lcd_context.framebuffer_address + 3 * (x + y * LCD_WIDTH)) & 0x00ffffff;
        }
        else
        {
            color = BSP_REG32(lcd_context.framebuffer_address + 3 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y)) & 0x00ffffff;
        }
    }
    else // 16位色
    {
        if (lcd_context.orientation == BSP_LCD_ORIENTATION_LANDSCAPE)
        {
            color = BSP_REG16(lcd_context.framebuffer_address + 2 * (x + y * LCD_WIDTH));
        }
        else
        {
            color = BSP_REG16(lcd_context.framebuffer_address + 2 * ((LCD_HEIGHT - x - 1) * LCD_WIDTH + y));
        }
    }
    return color;
}


/***************************************************************************************************************
 *    函 数 名:    bsp_lcd_set_number_fill_mode
 *
 *    入口参数:    mode - 设置变量的显示模式
 *
 *    函数功能:    设置变量显示时多余位补0还是补空格，可输入参数 Fill_Space 填充空格，Fill_Zero 填充零
 *
 *    说    明:   1. 供整数和小数显示函数使用
 *                2. 使用示例 bsp_lcd_set_number_fill_mode(Fill_Zero)
 *
 ***************************************************************************************************************/
void bsp_lcd_set_number_fill_mode(bsp_lcd_number_fill_mode_t mode)
{
    lcd_context.number_fill_mode = mode;
}

/***************************************************************************************************************************************
 *    函 数 名: bsp_lcd_draw_image
 *
 *    入口参数: x - 起始水平坐标，取值范围0~799
 *                 y - 起始垂直坐标，取值范围0~479
 *                  width  - 图片的水平宽度，最大取值800
 *                 height - 图片的垂直宽度，最大取值480
 *                 image_data - 图片数据存储区的首地址
 *
 *    函数功能: 在指定坐标处显示图片
 *
 *    说    明: 图片需要事先取模，使用 bsp_lcd_set_draw_color() 设置画笔色
 *
 *****************************************************************************************************************************************/
void bsp_lcd_draw_image(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *image_data)
{
    uint8_t dis_char;        // 字模的值
    uint16_t x_address = x; // 水平坐标

    for (uint16_t i = 0; i < height; i++)
    {
        for (uint16_t j = 0; j < (float)width / 8; j++)
        {
            dis_char = *image_data;

            for (uint16_t m = 0; m < 8; m++)
            {
                if (dis_char & 0x01)
                {
                    bsp_lcd_draw_pixel(x_address, y, lcd_context.draw_color); // 当前模值不为0时，使用画笔色绘点
                }
                else
                {
                    bsp_lcd_draw_pixel(x_address, y, lcd_context.background_color); // 否则使用背景色绘制点
                }
                dis_char >>= 1;
                x_address++; // 水平坐标自加

                if ((x_address - x) == width) // 如果水平坐标达到了字符宽度，则退出当前循环，进入下一行的绘制
                {
                    x_address = x;
                    y++;
                    break;
                }
            }
            image_data++;
        }
    }
}

/***************************************************************************************************************************************
 *    函 数 名: bsp_lcd_draw_line
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
void bsp_lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    int16_t deltax = 0, deltay = 0, x = 0, y = 0, xinc1 = 0, xinc2 = 0,
            yinc1 = 0, yinc2 = 0, den = 0, num = 0, numadd = 0, numpixels = 0,
            curpixel = 0;

    deltax = ((x2 - x1) > 0) ? (x2 - x1) : -(x2 - x1); /* The difference between the x's */
    deltay = ((y2 - y1) > 0) ? (y2 - y1) : -(y2 - y1); /* The difference between the y's */
    x = x1;  /* Start x off at the first pixel */
    y = y1;  /* Start y off at the first pixel */

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
        bsp_lcd_draw_pixel(x, y, lcd_context.draw_color); /* Draw the current pixel */
        num += numadd;  /* Increase the numerator by the top of the fraction */
        if (num >= den) /* Check if numerator >= denominator */
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
 *    函 数 名: bsp_lcd_draw_rect
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
void bsp_lcd_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    /* draw horizontal lines */
    bsp_lcd_draw_line(x, y, x + width, y);
    bsp_lcd_draw_line(x, y + height, x + width, y + height);

    /* draw vertical lines */
    bsp_lcd_draw_line(x, y, x, y + height);
    bsp_lcd_draw_line(x + width, y, x + width, y + height);
}

/***************************************************************************************************************************************
 *    函 数 名: bsp_lcd_draw_circle
 *
 *    入口参数: x - 圆心 水平坐标，取值范围 0~799
 *                  y - 圆心 垂直坐标，取值范围 0~479
 *                  radius - 半径
 *
 *    函数功能: 在坐标 (x,y) 绘制指定半径的圆形线条
 *
 *    说    明: 1. 该函数移植于ST官方评估板的例程
 *             2. 要绘制的区域不能超过屏幕的显示区域
 *
 *****************************************************************************************************************************************/
void bsp_lcd_draw_circle(uint16_t x, uint16_t y, uint16_t radius)
{
    int x_add = -(int)radius, y_add = 0, err = 2 - 2 * (int)radius, e2;
    do
    {
        bsp_lcd_draw_pixel(x - x_add, y + y_add, lcd_context.draw_color);
        bsp_lcd_draw_pixel(x + x_add, y + y_add, lcd_context.draw_color);
        bsp_lcd_draw_pixel(x + x_add, y - y_add, lcd_context.draw_color);
        bsp_lcd_draw_pixel(x - x_add, y - y_add, lcd_context.draw_color);

        e2 = err;
        if (e2 <= y_add)
        {
            err += ++y_add * 2 + 1;
            if (-x_add == y_add && e2 <= x_add)
                e2 = 0;
        }
        if (e2 > x_add)
            err += ++x_add * 2 + 1;
    } while (x_add <= 0);
}

/***************************************************************************************************************************************
 *    函 数 名: bsp_lcd_fill_rect_dma2d
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

void bsp_lcd_fill_rect_dma2d(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    DMA2D->CR &= ~(DMA2D_CR_START); //    停止DMA2D
    DMA2D->CR = DMA2D_R2M;          //    寄存器到SDRAM
    DMA2D->OPFCCR = lcd_context.pixel_format; // 设置颜色格式
    DMA2D->OCOLR = lcd_context.draw_color;    // 颜色

    if (lcd_context.orientation == BSP_LCD_ORIENTATION_LANDSCAPE) // 横屏填充
    {
        DMA2D->OOR = LCD_WIDTH - width;                                             //    设置行偏移
        DMA2D->OMAR = lcd_context.framebuffer_address + lcd_context.bytes_per_pixel * (LCD_WIDTH * y + x); // 地址
        DMA2D->NLR = (width << 16) | (height);                                      //    设定长度和宽度
    }
    else // 竖屏填充
    {
        DMA2D->OOR = LCD_WIDTH - height;                                                                       //    设置行偏移
        DMA2D->OMAR = lcd_context.framebuffer_address + lcd_context.bytes_per_pixel * ((LCD_HEIGHT - x - 1 - width) * LCD_WIDTH + y); // 地址
        DMA2D->NLR = (width) | (height << 16);                                                                 //    设定长度和宽度
    }

    DMA2D->CR |= DMA2D_CR_START; //    启动DMA2D
    while (DMA2D->CR & DMA2D_CR_START); //    等待传输完成
}


/***************************************************************************************************************************************
 *    函 数 名: bsp_lcd_fill_circle
 *
 *    入口参数: x - 圆心 水平坐标，取值范围 0~799
 *                  y - 圆心 垂直坐标，取值范围 0~479
 *                  radius - 半径
 *
 *    函数功能: 在坐标 (x,y) 填充指定半径的圆形区域
 *
 *    说    明: 1. 该函数移植于ST官方评估板的例程
 *                 2. 要绘制的区域不能超过屏幕的显示区域
 *
 *****************************************************************************************************************************************/

void bsp_lcd_fill_circle(uint16_t x, uint16_t y, uint16_t radius)
{
    int32_t D;     /* Decision Variable */
    uint32_t current_x; /* Current X Value */
    uint32_t current_y; /* Current Y Value */

    D = 3 - (radius << 1);
    current_x = 0;
    current_y = radius;

    while (current_x <= current_y)
    {
        if (current_y > 0)
        {
            bsp_lcd_draw_line(x - current_x, y - current_y, x - current_x, y - current_y + 2 * current_y);
            bsp_lcd_draw_line(x + current_x, y - current_y, x + current_x, y - current_y + 2 * current_y);
        }

        if (current_x > 0)
        {
            bsp_lcd_draw_line(x - current_y, y - current_x, x - current_y, y - current_x + 2 * current_x);
            bsp_lcd_draw_line(x + current_y, y - current_x, x + current_y, y - current_x + 2 * current_x);
        }
        if (D < 0)
        {
            D += (current_x << 2) + 6;
        }
        else
        {
            D += ((current_x - current_y) << 2) + 10;
            current_y--;
        }
        current_x++;
    }

    bsp_lcd_draw_circle(x, y, radius);
}


/*
*********************************************************************************************************
*	函 数 名: bsp_lcd_fill_dma2d
*	功能说明: DMA2D颜色填充功能
*	形    参: dest           颜色数据目的地址
*             x_size         色块X轴大小，即每行像素数
*             y_size         色块Y轴大小，即行数
*             off_line       前景层图像的行偏移
*             color          色块颜色值
*             pixel_format   目标区颜色格式
*	返 回 值: 无
*********************************************************************************************************
*/
void bsp_lcd_fill_dma2d(void *dest,
                        uint32_t x_size,
                        uint32_t y_size,
                        uint32_t off_line,
                        uint32_t color,
                        uint32_t pixel_format)
{
    /* DMA2D采用寄存器到存储器模式, 这种模式用不到前景层和背景层 */
    DMA2D->CR     = 0x00030000UL | (1 << 9);
    DMA2D->OCOLR  = color;
    DMA2D->OMAR   = (uint32_t)dest;
    DMA2D->OOR    = off_line; // 行偏移量
    DMA2D->OPFCCR = pixel_format;
    DMA2D->NLR    = (uint32_t)(x_size << 16) | (uint16_t)y_size;

    /* 启动传输 */
    DMA2D->CR |= DMA2D_CR_START;

    /* 等待DMA2D传输完成 */
    while (DMA2D->CR & DMA2D_CR_START)
    {
    }
}

/*
*********************************************************************************************************
*	函 数 名: bsp_lcd_copy_dma2d
*	功能说明: 通过DMA2D从前景层复制指定区域的颜色数据到目标区域
*	形    参: src          颜色数据源地址
*             dest          颜色数据目的地址
*             x_size         目的区域的X轴大小，即每行像素数
*             y_size         目的区域的Y轴大小，即行数
*             off_line_src    前景层图像的行偏移
*             off_line_dest    输出的行偏移
*	返 回 值: 无
*********************************************************************************************************
*/
void bsp_lcd_copy_dma2d(void *src,
                        void *dest,
                        uint32_t x_size,
                        uint32_t y_size,
                        uint32_t off_line_src,
                        uint32_t off_line_dest)
{
    /* DMA2D采用存储器到存储器模式, 这种模式是前景层作为DMA2D输入 */
    DMA2D->CR    = (1 << 9);          // 传输完成中断使能
    DMA2D->FGMAR = (uint32_t)src;  // 前景层存储器地址
    DMA2D->OMAR  = (uint32_t)dest; // 输出存储器地址
    DMA2D->FGOR  = off_line_src;   // 前景层偏移
    DMA2D->OOR   = off_line_dest;  // 输出偏移

    /* 前景层和输出区域都采用的RGB565颜色格式 */
    DMA2D->FGPFCCR = LTDC_PIXEL_FORMAT_RGB565;
    DMA2D->OPFCCR  = LTDC_PIXEL_FORMAT_RGB565;
    DMA2D->NLR     = (uint32_t)(x_size << 16) | (uint16_t)y_size;

    /* 启动传输 */
    DMA2D->CR |= DMA2D_CR_START;

    // /* 等待DMA2D传输完成 */
    // while (DMA2D->CR & DMA2D_CR_START)
    // {
    // }
}

/*
*********************************************************************************************************
*	函 数 名: bsp_lcd_mix_colors_bulk_dma2d
*	功能说明: 前景层和目标区域的颜色混合
*	形    参: p_color_fg       前景层数据源地址
*             off_line_src_fg  前景层图像的行偏移
*             p_color_dst      目标区数据地址
*             off_line_dest    目标区的行偏移
*             x_size           目的区域的X轴大小，即每行像素数
*             y_size           目的区域的Y轴大小，即行数
*             intens           设置前景层的透明度，255表示完全不透明，0表示完全透明
*	返 回 值: 无
*********************************************************************************************************
*/
void bsp_lcd_mix_colors_bulk_dma2d(uint32_t *p_color_fg,
                                   uint32_t off_line_src_fg,
                                   uint32_t *p_color_dst,
                                   uint32_t off_line_dest,
                                   uint32_t x_size,
                                   uint32_t y_size,
                                   uint8_t  intens)
{
    /* DMA2D采用存储器到存储器模式, 这种模式前景层和背景层作为DMA2D输入，且支持颜色格式转换和颜色混合 */
    DMA2D->CR    = 0x00020000UL | (1 << 9);
    DMA2D->FGMAR = (uint32_t)p_color_fg;
    DMA2D->BGMAR = (uint32_t)p_color_dst;
    DMA2D->OMAR  = (uint32_t)p_color_dst;
    DMA2D->FGOR  = off_line_src_fg;
    DMA2D->BGOR  = off_line_dest;
    DMA2D->OOR   = off_line_dest;

    /* 前景层，背景层和输出区都是用的RGB565格式 */
    DMA2D->FGPFCCR = LTDC_PIXEL_FORMAT_RGB565 | (1UL << 16) | ((uint32_t)intens << 24);
    DMA2D->BGPFCCR = LTDC_PIXEL_FORMAT_RGB565;
    DMA2D->OPFCCR  = LTDC_PIXEL_FORMAT_RGB565;
    DMA2D->NLR     = (uint32_t)(x_size << 16) | (uint16_t)y_size;

    /* 启动传输 */
    DMA2D->CR |= DMA2D_CR_START;

    /* 等待DMA2D传输完成 */
    while (DMA2D->CR & DMA2D_CR_START)
    {
    }
}

/*
*********************************************************************************************************
*	函 数 名: bsp_lcd_alpha_blending_bulk_dma2d
*	功能说明: 前景层和背景层的颜色混合
*	形    参: p_color_fg      前景层源数据地址
*             off_line_src_fg  前景层源数据行偏移
*             p_color_bg      背景层源数据地址
*             off_line_src_bg  背景层源数据行偏移
*             p_color_dst     目标区地址
*             off_line_dest    目标区行偏移
*             x_size         目标区域的X轴大小，即每行像素数
*             y_size         目标区域的Y轴大小，即行数
*	返 回 值: 无
*********************************************************************************************************
*/
void bsp_lcd_alpha_blending_bulk_dma2d(uint32_t *p_color_fg,
                                       uint32_t off_line_src_fg,
                                       uint32_t *p_color_bg,
                                       uint32_t off_line_src_bg,
                                       uint32_t *p_color_dst,
                                       uint32_t off_line_dest,
                                       uint32_t x_size,
                                       uint32_t y_size)
{
    /* DMA2D采用存储器到存储器模式, 这种模式前景层和背景层作为DMA2D输入，且支持颜色格式转换和颜色混合 */
    DMA2D->CR    = 0x00020000UL | (1 << 9);
    DMA2D->FGMAR = (uint32_t)p_color_fg;
    DMA2D->BGMAR = (uint32_t)p_color_bg;
    DMA2D->OMAR  = (uint32_t)p_color_dst;
    DMA2D->FGOR  = off_line_src_fg;
    DMA2D->BGOR  = off_line_src_bg;
    DMA2D->OOR   = off_line_dest;

    /* 前景层，背景层采用ARGB8888格式，输出区采用RGB565格式 */
    DMA2D->FGPFCCR = PLATFORM_LTDC_PIXEL_FORMAT_ARGB8888 | (2 << 16) | (200 << 24);
    DMA2D->BGPFCCR = PLATFORM_LTDC_PIXEL_FORMAT_ARGB8888 | (2 << 16) | (100 << 24);
    DMA2D->OPFCCR  = PLATFORM_LTDC_PIXEL_FORMAT_RGB565;
    DMA2D->NLR     = (uint32_t)(x_size << 16) | (uint16_t)y_size;

    /* 启动传输 */
    DMA2D->CR |= DMA2D_CR_START;

    /* 等待DMA2D传输完成 */
    while (DMA2D->CR & DMA2D_CR_START)
    {
    }
}

/*
 *********************************************************************************************************
 *	函 数 名: bsp_lcd_draw_alpha_bitmap_dma2d
 *	功能说明: ARGB8888格式位图显示
 *	形    参: dest        目标区地址
 *             src        源数据地址，即位图首地址
 *             x_size       目标区域的X轴大小，即每行像素数
 *             y_size       目标区域的Y轴大小，即行数
 *             off_line_src  源数据行偏移
 *             off_line_dest  目标区行偏移
 *             pixel_format 目标区颜色格式
 *	返 回 值: 无
 *********************************************************************************************************
 */
void bsp_lcd_draw_alpha_bitmap_dma2d(void *dest,
                                     void *src,
                                     uint32_t x_size,
                                     uint32_t y_size,
                                     uint32_t off_line_src,
                                     uint32_t off_line_dest,
                                     uint32_t pixel_format)
{
    /* DMA2D采用存储器到存储器模式, 这种模式前景层和背景层作为DMA2D输入，且支持颜色格式转换和颜色混合 */
    DMA2D->CR    = 0x00020000UL | (1 << 9);
    DMA2D->FGMAR = (uint32_t)src;
    DMA2D->BGMAR = (uint32_t)dest;
    DMA2D->OMAR  = (uint32_t)dest;
    DMA2D->FGOR  = off_line_src;
    DMA2D->BGOR  = off_line_dest;
    DMA2D->OOR   = off_line_dest;

    /* 前景层颜色格式是ARGB8888，即位图的颜色格式，背景层和输出区颜色格式可配置 */
    DMA2D->FGPFCCR = PLATFORM_LTDC_PIXEL_FORMAT_ARGB8888;
    DMA2D->BGPFCCR = pixel_format;
    DMA2D->OPFCCR  = pixel_format;
    DMA2D->NLR     = (uint32_t)(x_size << 16) | (uint16_t)y_size;

    /* 启动传输 */
    DMA2D->CR |= DMA2D_CR_START;

    /* 等待DMA2D传输完成 */
    while (DMA2D->CR & DMA2D_CR_START)
    {
    }
}

