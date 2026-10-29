#ifndef __BSP_LCD_H__
#define __BSP_LCD_H__

#include <stdbool.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"
#include "bsp_types.h"
#include "sdram.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_WIDTH                       800    // LCD的像素长度
#define LCD_HEIGHT                      480    // LCD的像素宽度
#define LCD_FRAMEBUFFER_BASE_ADDR       SDRAM_BANK_ADDR   // 显存的起始地址

#define RGB888_TO_RGB565(R,G,B)         (((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)) /* 将8位R,G,B转化为 16位RGB565格式 */

typedef enum {
    BSP_LCD_ORIENTATION_LANDSCAPE = 0,  // 横屏显示
    BSP_LCD_ORIENTATION_PORTRAIT,       // 竖屏显示
} bsp_lcd_orientation_t;

typedef enum {
    BSP_LCD_NUMBER_FILL_ZERO = 0,   // 数字显示时多余位补零
    BSP_LCD_NUMBER_FILL_SPACE,      // 数字显示时多余位补空格
} bsp_lcd_number_fill_mode_t;

/*---------------------------------------- 常用颜色 ------------------------------------------------------
 1. 这里为了方便用户使用，定义的是32位颜色，然后再通过代码自动转换成对应颜色格式所需要的的颜色
 2. 32位的颜色中，从高位到低位分别对应 A、R、G、B  4个颜色通道，其中A表示透明通道
 3. 最多可设置255级透明色，ff表示不透明，0表示完全透明
 4. 除非使用ARGB1555和ARGB8888等支持透明色的颜色格式，不然透明色不起作用，其中ARGB1555仅支持一位
     透明色，即仅有透明和不透明两种状态，ARGB4444有16级透明度，ARGB8888支持255级透明度
 5. 用户可以在电脑用调色板获取24位RGB颜色，然后再补充透明通道得到32位的颜色，再将此32位颜色输入
     bsp_lcd_set_draw_color() 或 bsp_lcd_set_background_color() 就可以显示出相应的颜色。
 6. 使用示例：纯蓝色的RGB值为0x0000FF，如果不需要透明色，则对应的32位颜色值为 0xff0000FF
 7. 以下定义的颜色都设置为不透明，用户可根据需求自行定义对应的颜色                                */
#define LCD_COLOR_WHITE                 0xFFFFFFFF      // 纯白色
#define LCD_COLOR_BLACK                 0xFF000000      // 纯黑色
#define LCD_COLOR_BLUE                  0xFF0000FF      // 纯蓝色
#define LCD_COLOR_GREEN                 0xFF00FF00      // 纯绿色
#define LCD_COLOR_RED                   0xFFFF0000      // 纯红色
#define LCD_COLOR_CYAN                  0xFF00FFFF      // 蓝绿色
#define LCD_COLOR_MAGENTA               0xFFFF00FF      // 紫红色
#define LCD_COLOR_YELLOW                0xFFFFFF00      // 黄色
#define LCD_COLOR_GREY                  0xFF2C2C2C      // 灰色
#define LCD_COLOR_LIGHT_BLUE            0xFF8080FF      // 亮蓝色
#define LCD_COLOR_LIGHT_GREEN           0xFF80FF80      // 亮绿色
#define LCD_COLOR_LIGHT_RED             0xFFFF8080      // 亮红色
#define LCD_COLOR_LIGHT_CYAN            0xFF80FFFF      // 亮蓝绿色
#define LCD_COLOR_LIGHT_MAGENTA         0xFFFF80FF      // 亮紫红色
#define LCD_COLOR_LIGHT_YELLOW          0xFFFFFF80      // 亮黄色
#define LCD_COLOR_LIGHT_GREY            0xFFA3A3A3      // 亮灰色
#define LCD_COLOR_DARK_BLUE             0xFF000080      // 暗蓝色
#define LCD_COLOR_DARK_GREEN            0xFF008000      // 暗绿色
#define LCD_COLOR_DARK_RED              0xFF800000      // 暗红色
#define LCD_COLOR_DARK_CYAN             0xFF008080      // 暗蓝绿色
#define LCD_COLOR_DARK_MAGENTA          0xFF800080      // 暗紫红色
#define LCD_COLOR_DARK_YELLOW           0xFF808000      // 暗黄色
#define LCD_COLOR_DARK_GREY             0xFF404040      // 暗灰色

bsp_status_t bsp_lcd_init(void);
void bsp_lcd_set_backlight(bool enabled);
void bsp_lcd_set_draw_color(uint32_t color);  // 设置画笔颜色
void bsp_lcd_set_background_color(uint32_t color);  // 设置背景颜色
void bsp_lcd_set_orientation(bsp_lcd_orientation_t orientation);  // 设置显示方向
void bsp_lcd_clear_dma2d(void);  // 清屏函数
void bsp_lcd_clear_rect_dma2d(uint16_t x, uint16_t y, uint16_t width, uint16_t height); // 局部清屏函数
void bsp_lcd_draw_pixel(uint16_t x, uint16_t y, uint32_t color);  // 画点
uint32_t bsp_lcd_read_pixel(uint16_t x, uint16_t y);  // 读点
void bsp_lcd_set_number_fill_mode(bsp_lcd_number_fill_mode_t mode);  // 设置数字空位填充方式

// 绘制图片
void bsp_lcd_draw_image(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *image_data);

// 2D图形绘制函数
void bsp_lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);  // 画线
void bsp_lcd_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);  // 画矩形
void bsp_lcd_draw_circle(uint16_t x, uint16_t y, uint16_t radius);  // 画圆

// 区域填充函数
void bsp_lcd_fill_rect_dma2d(uint16_t x, uint16_t y, uint16_t width, uint16_t height); // 填充矩形
void bsp_lcd_fill_circle(uint16_t x, uint16_t y, uint16_t radius);  // 填充圆

void bsp_lcd_fill_dma2d(void *dest, uint32_t x_size, uint32_t y_size, uint32_t off_line, uint32_t color, uint32_t pixel_format);
void bsp_lcd_copy_dma2d(void *src, void *dest, uint32_t x_size, uint32_t y_size, uint32_t off_line_src, uint32_t dest_off_line);
void bsp_lcd_mix_colors_bulk_dma2d(uint32_t *p_color_fg, uint32_t off_line_src_fg, uint32_t *p_color_dst, uint32_t off_line_dest, uint32_t x_size, uint32_t y_size, uint8_t intens);
void bsp_lcd_alpha_blending_bulk_dma2d(uint32_t *p_color_fg, uint32_t off_line_src_fg, uint32_t *p_color_bg, uint32_t off_line_src_bg, uint32_t *p_color_dst, uint32_t dest_off_line, uint32_t x_size, uint32_t y_size);
void bsp_lcd_draw_alpha_bitmap_dma2d(void *dest, void *src, uint32_t x_size, uint32_t y_size, uint32_t off_line_src, uint32_t dest_off_line, uint32_t pixel_format);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LCD_H__ */
