/**
 * @file lv_port_disp_templ.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>  // bool
#include <stdint.h>   // uint32_t
#include <string.h>   // memset()
#include "bsp_lcd.h"  // LCD尺寸和显存地址
#include "ltdc.h"

/*********************
 *      DEFINES
 *********************/
#ifndef MY_DISP_HOR_RES
    #warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen width, default value 320 is used for now.
    #define MY_DISP_HOR_RES    800
#endif

#ifndef MY_DISP_VER_RES
    #warning Please define or replace the macro MY_DISP_HOR_RES with the actual screen height, default value 240 is used for now.
    #define MY_DISP_VER_RES    480
#endif

#if LV_COLOR_DEPTH != 16
    #error "The LTDC layer uses RGB565, so LV_COLOR_DEPTH must be 16."
#endif

#define DISPLAY_PIXEL_COUNT          ((uint32_t)MY_DISP_HOR_RES * MY_DISP_VER_RES)
#define DISPLAY_FRAMEBUFFER_SIZE     (DISPLAY_PIXEL_COUNT * sizeof(lv_color_t))
#define DISPLAY_FRAMEBUFFER_0_ADDR   ((uintptr_t)LCD_FRAMEBUFFER_BASE_ADDR)
#define DISPLAY_FRAMEBUFFER_1_ADDR   (DISPLAY_FRAMEBUFFER_0_ADDR + DISPLAY_FRAMEBUFFER_SIZE)
/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//        const lv_area_t * fill_area, lv_color_t color);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_disp_draw_buf_t display_draw_buffer;
static lv_color_t * const framebuffer_0 = (lv_color_t *)DISPLAY_FRAMEBUFFER_0_ADDR;
static lv_color_t * const framebuffer_1 = (lv_color_t *)DISPLAY_FRAMEBUFFER_1_ADDR;
static lv_disp_drv_t * volatile pending_flush_driver;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /* direct mode 要求两块显存初始内容一致，后续由 LVGL 同步差异区域。 */
    memset(framebuffer_0, 0, DISPLAY_FRAMEBUFFER_SIZE);
    memset(framebuffer_1, 0, DISPLAY_FRAMEBUFFER_SIZE);

    /* LTDC 初始显示 framebuffer_0，因此 LVGL 先在 framebuffer_1 中绘制。 */
    lv_disp_draw_buf_init(&display_draw_buffer,
                          framebuffer_1,
                          framebuffer_0,
                          DISPLAY_PIXEL_COUNT);

    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/

    static lv_disp_drv_t disp_drv;     /*Descriptor of a display driver*/
    lv_disp_drv_init(&disp_drv);       /*Basic initialization*/

    /*Set up the functions to access to your display*/

    /*Set the resolution of the display*/
    disp_drv.hor_res = MY_DISP_HOR_RES;
    disp_drv.ver_res = MY_DISP_VER_RES;

    /*Used to copy the buffer's content to the display*/
    disp_drv.flush_cb = disp_flush;

    /*Set a display buffer*/
    disp_drv.draw_buf = &display_draw_buffer;

    // disp_drv.rotated = LV_DISP_ROT_90;
    // disp_drv.sw_rotate = 1;

    /* 整帧双缓冲 direct mode：只绘制变化区域，最后在垂直消隐期间切换显存。 */
    disp_drv.full_refresh = 0;
    disp_drv.direct_mode = 1;

    /* Fill a memory array with a color if you have GPU.
     * Note that, in lv_conf.h you can enable GPUs that has built-in support in LVGL.
     * But if you have a different GPU you can use with this callback.*/
    // disp_drv.gpu_fill_cb = gpu_fill;

    /*Finally register the driver*/
    lv_disp_drv_register(&disp_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /*You code here*/
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/*Flush the content of the internal buffer the specific area on the display
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_disp_flush_ready()' has to be called when finished.*/
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    (void)area;

    if (!disp_flush_enabled)
    {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    /* direct mode 可能一次刷新多个无效区域，只在最后一个区域完成后换帧。 */
    if (!lv_disp_flush_is_last(disp_drv))
    {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    pending_flush_driver = disp_drv;
    __DSB(); // 确保当前帧的显存写入在提交给 LTDC 前完成

    /* 只更新 LTDC 影子寄存器，等待下一次垂直消隐再生效。 */
    if ((HAL_LTDC_SetAddress_NoReload(&hltdc, (uint32_t)color_p, LTDC_LAYER_1) != HAL_OK) ||
        (HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK))
    {
        pending_flush_driver = NULL;
        lv_disp_flush_ready(disp_drv);
    }
}

/* LTDC 在垂直消隐期间完成显存切换后，通知 LVGL 旧显存可以继续绘制。 */
void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *ltdc_handle)
{
    lv_disp_drv_t *completed_driver;

    if (ltdc_handle != &hltdc)
    {
        return;
    }

    completed_driver = pending_flush_driver;
    pending_flush_driver = NULL;

    if (completed_driver != NULL)
    {
        lv_disp_flush_ready(completed_driver);
    }
}

/*OPTIONAL: GPU INTERFACE*/

/*If your MCU has hardware accelerator (GPU) then you can use it to fill a memory with a color*/
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//                    const lv_area_t * fill_area, lv_color_t color)
//{
//    /*It's an example code which should be done by your GPU*/
//    int32_t x, y;
//    dest_buf += dest_width * fill_area->y1; /*Go to the first line*/
//
//    for(y = fill_area->y1; y <= fill_area->y2; y++) {
//        for(x = fill_area->x1; x <= fill_area->x2; x++) {
//            dest_buf[x] = color;
//        }
//        dest_buf+=dest_width;    /*Go to the next line*/
//    }
//}


#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
