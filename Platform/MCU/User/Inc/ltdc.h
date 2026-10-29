#ifndef __LTDC_H__
#define __LTDC_H__

#include <stdint.h>
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern LTDC_HandleTypeDef hltdc;

typedef enum {
    PLATFORM_LTDC_PIXEL_FORMAT_ARGB8888 = LTDC_PIXEL_FORMAT_ARGB8888,
    PLATFORM_LTDC_PIXEL_FORMAT_RGB888   = LTDC_PIXEL_FORMAT_RGB888,
    PLATFORM_LTDC_PIXEL_FORMAT_RGB565   = LTDC_PIXEL_FORMAT_RGB565,
    PLATFORM_LTDC_PIXEL_FORMAT_ARGB1555 = LTDC_PIXEL_FORMAT_ARGB1555,
    PLATFORM_LTDC_PIXEL_FORMAT_ARGB4444 = LTDC_PIXEL_FORMAT_ARGB4444,
} platform_ltdc_pixel_format_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t hsync;
    uint16_t hbp;
    uint16_t hfp;
    uint16_t vsync;
    uint16_t vbp;
    uint16_t vfp;
    uint32_t layer_framebuffer_address;
    platform_ltdc_pixel_format_t layer_pixel_format;
} platform_ltdc_config_t;

HAL_StatusTypeDef platform_ltdc_init(const platform_ltdc_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* __LTDC_H__ */
