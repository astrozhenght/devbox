#ifndef __LV_APP_MAIN_H
#define __LV_APP_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_REFRESH_PERIOD_MINUTES    20  // 更新天气的周期，单位：分钟

#define ColorLightCyan      (0xE1FFFF)  // 淡青色
#define ColorMediumPurple   (0x9370DB)  // 适中的紫色
#define ColorGold           (0xFFD700)  // 金
#define ColorAuqamarin      (0x7FFFAA)  // 绿玉碧绿色
#define ColorLightGray      (0xD3D3D3)  // 浅灰色

void lv_main_create(lv_obj_t* parent);


#ifdef __cplusplus
}
#endif

#endif /* __LV_APP_MAIN_H */
