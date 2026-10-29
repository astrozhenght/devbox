/*******************************************************************************
 * Size: 16 px
 * Bpp: 1
 * Opts: --bpp 1 --size 16 --no-compress --stride 1 --align 1 --font SmileySans-Oblique.ttf --symbols 串口调试助手测试 --format lvgl -o ui_font_chinese_16_1.c
 ******************************************************************************/

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif



#ifndef CESHI_16_1
#define CESHI_16_1 1
#endif

#if CESHI_16_1

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+4E32 "串" */
    0x2, 0x0, 0x40, 0xff, 0x93, 0x32, 0x64, 0x7f,
    0x81, 0x80, 0x20, 0x7f, 0xc8, 0x8d, 0x11, 0x7f,
    0xe0, 0xc0, 0x18, 0x3, 0x0,

    /* U+52A9 "助" */
    0x38, 0x85, 0x91, 0xaf, 0xb5, 0xf7, 0x9a, 0x92,
    0x52, 0x4a, 0x4b, 0x79, 0x6b, 0x6d, 0x69, 0xad,
    0x2f, 0xc5, 0xda, 0x82, 0x70,

    /* U+53E3 "口" */
    0x7f, 0xd0, 0x34, 0xd, 0x3, 0x40, 0x90, 0x2c,
    0xb, 0x2, 0xc1, 0xb0, 0x6c, 0x1b, 0x6, 0x81,
    0xa0, 0x6f, 0xf0,

    /* U+624B "手" */
    0x0, 0x67, 0xfc, 0x8, 0x1, 0x0, 0x20, 0x7f,
    0x81, 0x80, 0x30, 0x6, 0x1f, 0xfc, 0x18, 0x3,
    0x0, 0x40, 0x48, 0x7, 0x0,

    /* U+6D4B "测" */
    0x27, 0x99, 0xa7, 0xcf, 0x34, 0x35, 0xa2, 0xbd,
    0x15, 0xa8, 0xad, 0x47, 0x6e, 0xf, 0x71, 0x6b,
    0x8b, 0x7c, 0x4c, 0x46, 0xa2, 0x2d, 0xd1, 0x47,
    0x80,

    /* U+8BD5 "试" */
    0x20, 0xa6, 0x14, 0x42, 0x7, 0xf0, 0x8, 0xc1,
    0xb, 0xe1, 0x34, 0x24, 0x84, 0x90, 0xd2, 0x3a,
    0x56, 0x6f, 0xb9, 0x80, 0x30,

    /* U+8C03 "调" */
    0x27, 0xe2, 0xc2, 0x2d, 0x20, 0xd2, 0xb, 0xa6,
    0x92, 0x2b, 0xa6, 0x86, 0x6b, 0xe4, 0xee, 0x5f,
    0xe7, 0xfc, 0xd0, 0x4b, 0x24, 0x33, 0x80
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 205, .box_w = 11, .box_h = 15, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 21, .adv_w = 205, .box_w = 11, .box_h = 15, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 42, .adv_w = 205, .box_w = 10, .box_h = 15, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 61, .adv_w = 205, .box_w = 11, .box_h = 15, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 82, .adv_w = 205, .box_w = 13, .box_h = 15, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 107, .adv_w = 205, .box_w = 11, .box_h = 15, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 128, .adv_w = 205, .box_w = 12, .box_h = 15, .ofs_x = 1, .ofs_y = -1}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x477, 0x5b1, 0x1419, 0x1f19, 0x3da3, 0x3dd1
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 20018, .range_length = 15826, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 7, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif

};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t ui_font_chinese_16_1 = {
#else
lv_font_t ui_font_chinese_16_1 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 15,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if CESHI_16_1*/
