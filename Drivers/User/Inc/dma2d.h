#ifndef __DMA2D_H__
#define __DMA2D_H__

#include <stdint.h>
#include "stm32h7xx.h"

/* DMA2D颜色填充功能 */
// 打开了 TC interrupt enable
void _DMA2D_Fill(void *pDst,
                 uint32_t xSize,
                 uint32_t ySize,
                 uint32_t OffLine,
                 uint32_t ColorIndex,
                 uint32_t PixelFormat);

/* 通过DMA2D从前景层复制指定区域的颜色数据到目标区域 */
// 打开了 TC interrupt enable
// 前景层和输出区域都采用的RGB565颜色格式
void _DMA2D_Copy(void *pSrc,
                 void *pDst,
                 uint32_t xSize,
                 uint32_t ySize,
                 uint32_t OffLineSrc,
                 uint32_t OffLineDst);

/* 前景层和目标区域的颜色混合 */
// 打开了 TC interrupt enable
// 前景层，背景层和输出区都是用的RGB565格式
void _DMA2D_MixColorsBulk(uint32_t *pColorFG,
                          uint32_t OffLineSrcFG,
                          uint32_t *pColorDst,
                          uint32_t OffLineDst,
                          uint32_t xSize,
                          uint32_t ySize,
                          uint8_t Intens);

/* 前景层和背景层的颜色混合 */
// 打开了 TC interrupt enable
// 前景层和背景层采用ARGB8888格式，输出区采用RGB565格式
void _DMA2D_AlphaBlendingBulk(uint32_t *pColorFG,
                              uint32_t OffLineSrcFG,
                              uint32_t *pColorBG,
                              uint32_t OffLineSrcBG,
                              uint32_t *pColorDst,
                              uint32_t OffLineDst,
                              uint32_t xSize,
                              uint32_t ySize);
/* ARGB8888格式位图显示 */
// 前景层颜色格式是LTDC_PIXEL_FORMAT_ARGB8888，即位图的颜色格式，背景层和输出区颜色格式可配置
void _DMA2D_DrawAlphaBitmap(void *pDst,
                            void *pSrc,
                            uint32_t xSize,
                            uint32_t ySize,
                            uint32_t OffLineSrc,
                            uint32_t OffLineDst,
                            uint32_t PixelFormat);

#endif
