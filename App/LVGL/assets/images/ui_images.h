#ifndef UI_IMAGES_H
#define UI_IMAGES_H

#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_IMG_DECLARE(ui_img_icon_folder48);
LV_IMG_DECLARE(ui_img_icon_debug);
LV_IMG_DECLARE(ui_img_icon_music);
LV_IMG_DECLARE(ui_img_icon_setting);
LV_IMG_DECLARE(ui_img_icon_uart);
LV_IMG_DECLARE(ui_img_icon_weather);
LV_IMG_DECLARE(ui_img_icon_calendar);

/**
 * @brief Get the image descriptor for a weather condition code.
 *
 * Codes 0 through 38 map to c0 through c38. Any other code maps to
 * the unknown-weather image c99.
 */
const lv_img_dsc_t *ui_weather_img_get(uint8_t weather_code);

#ifdef __cplusplus
}
#endif

#endif
