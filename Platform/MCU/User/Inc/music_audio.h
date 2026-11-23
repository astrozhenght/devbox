#ifndef MUSIC_AUDIO_H
#define MUSIC_AUDIO_H
#include <stdbool.h>
#include <stdint.h>
#include "music_config.h"
bool platform_music_spi_init(void);
bool platform_music_spi_transfer(uint8_t *tx, uint8_t *rx, uint16_t size);
void platform_music_spi_fast(void);
bool platform_music_sai_start(void);
void platform_music_sai_stop(void);
bool platform_music_pcm_copy(int16_t *stereo, uint32_t *sequence);
uint32_t platform_music_pcm_errors(void);
void platform_music_dma_irq(void);
void platform_music_sai_irq(void);
#endif
