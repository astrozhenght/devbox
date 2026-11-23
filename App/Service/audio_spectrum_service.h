#ifndef AUDIO_SPECTRUM_SERVICE_H
#define AUDIO_SPECTRUM_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
#include "music_config.h"
typedef struct
{
    uint8_t levels[MUSIC_SPECTRUM_BANDS];
    uint32_t frames;
    uint32_t dropped;
    uint32_t errors;
} audio_spectrum_snapshot_t;
bool audio_spectrum_create(void);
void audio_spectrum_enable(bool enabled);
void audio_spectrum_snapshot(audio_spectrum_snapshot_t *snapshot);
#endif
