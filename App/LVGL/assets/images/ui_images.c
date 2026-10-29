#include "ui_images.h"

#define UI_WEATHER_IMG_CODE_MAX    38U

LV_IMG_DECLARE(ui_img_weather_c0);
LV_IMG_DECLARE(ui_img_weather_c1);
LV_IMG_DECLARE(ui_img_weather_c2);
LV_IMG_DECLARE(ui_img_weather_c3);
LV_IMG_DECLARE(ui_img_weather_c4);
LV_IMG_DECLARE(ui_img_weather_c5);
LV_IMG_DECLARE(ui_img_weather_c6);
LV_IMG_DECLARE(ui_img_weather_c7);
LV_IMG_DECLARE(ui_img_weather_c8);
LV_IMG_DECLARE(ui_img_weather_c9);
LV_IMG_DECLARE(ui_img_weather_c10);
LV_IMG_DECLARE(ui_img_weather_c11);
LV_IMG_DECLARE(ui_img_weather_c12);
LV_IMG_DECLARE(ui_img_weather_c13);
LV_IMG_DECLARE(ui_img_weather_c14);
LV_IMG_DECLARE(ui_img_weather_c15);
LV_IMG_DECLARE(ui_img_weather_c16);
LV_IMG_DECLARE(ui_img_weather_c17);
LV_IMG_DECLARE(ui_img_weather_c18);
LV_IMG_DECLARE(ui_img_weather_c19);
LV_IMG_DECLARE(ui_img_weather_c20);
LV_IMG_DECLARE(ui_img_weather_c21);
LV_IMG_DECLARE(ui_img_weather_c22);
LV_IMG_DECLARE(ui_img_weather_c23);
LV_IMG_DECLARE(ui_img_weather_c24);
LV_IMG_DECLARE(ui_img_weather_c25);
LV_IMG_DECLARE(ui_img_weather_c26);
LV_IMG_DECLARE(ui_img_weather_c27);
LV_IMG_DECLARE(ui_img_weather_c28);
LV_IMG_DECLARE(ui_img_weather_c29);
LV_IMG_DECLARE(ui_img_weather_c30);
LV_IMG_DECLARE(ui_img_weather_c31);
LV_IMG_DECLARE(ui_img_weather_c32);
LV_IMG_DECLARE(ui_img_weather_c33);
LV_IMG_DECLARE(ui_img_weather_c34);
LV_IMG_DECLARE(ui_img_weather_c35);
LV_IMG_DECLARE(ui_img_weather_c36);
LV_IMG_DECLARE(ui_img_weather_c37);
LV_IMG_DECLARE(ui_img_weather_c38);
LV_IMG_DECLARE(ui_img_weather_c99);

static const lv_img_dsc_t *const weather_imgs[] =
{
    &ui_img_weather_c0,  &ui_img_weather_c1,  &ui_img_weather_c2,
    &ui_img_weather_c3,  &ui_img_weather_c4,  &ui_img_weather_c5,
    &ui_img_weather_c6,  &ui_img_weather_c7,  &ui_img_weather_c8,
    &ui_img_weather_c9,  &ui_img_weather_c10, &ui_img_weather_c11,
    &ui_img_weather_c12, &ui_img_weather_c13, &ui_img_weather_c14,
    &ui_img_weather_c15, &ui_img_weather_c16, &ui_img_weather_c17,
    &ui_img_weather_c18, &ui_img_weather_c19, &ui_img_weather_c20,
    &ui_img_weather_c21, &ui_img_weather_c22, &ui_img_weather_c23,
    &ui_img_weather_c24, &ui_img_weather_c25, &ui_img_weather_c26,
    &ui_img_weather_c27, &ui_img_weather_c28, &ui_img_weather_c29,
    &ui_img_weather_c30, &ui_img_weather_c31, &ui_img_weather_c32,
    &ui_img_weather_c33, &ui_img_weather_c34, &ui_img_weather_c35,
    &ui_img_weather_c36, &ui_img_weather_c37, &ui_img_weather_c38
};

const lv_img_dsc_t *ui_weather_img_get(uint8_t weather_code)
{
    if (weather_code > UI_WEATHER_IMG_CODE_MAX)
    {
        return &ui_img_weather_c99;
    }

    return weather_imgs[weather_code];
}

