#ifndef __BSP_TOUCH_H__
#define __BSP_TOUCH_H__

#include <stdint.h>
#include <stdbool.h>
#include "bsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_TOUCH_MAX_POINTS         1  // 最大触摸点数

typedef struct {
    uint8_t flag;   // 触摸标志位，为1时表示有触摸操作
    uint8_t num;    // 触摸点数
    uint16_t x[BSP_TOUCH_MAX_POINTS];   // x坐标
    uint16_t y[BSP_TOUCH_MAX_POINTS];   // y坐标
} bsp_touch_info_t;

bsp_status_t bsp_touch_init(void);
bsp_status_t bsp_touch_read_data(bsp_touch_info_t *touch_info);
bool bsp_touch_is_pressed(void);
void bsp_touch_get_position(uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_TOUCH_H__ */

