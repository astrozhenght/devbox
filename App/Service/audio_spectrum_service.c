#include "audio_spectrum_service.h"
#include "music_audio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "dsp/transform_functions.h"
#include <math.h>
#include <string.h>

static MUSIC_RAM float input[MUSIC_FFT_SIZE];
static MUSIC_RAM float output[MUSIC_FFT_SIZE];
static MUSIC_RAM float window[MUSIC_FFT_SIZE];
static MUSIC_RAM int16_t stereo[MUSIC_FFT_SIZE * 2U];
static MUSIC_RAM float smooth[MUSIC_SPECTRUM_BANDS];
static MUSIC_RAM arm_rfft_fast_instance_f32 fft;
static MUSIC_RAM StackType_t stack[1024];
static StaticTask_t task_control;
static audio_spectrum_snapshot_t published;
static volatile bool enabled;

void audio_spectrum_enable(bool value)
{
    taskENTER_CRITICAL();
    enabled = value;
    taskEXIT_CRITICAL();
}

void audio_spectrum_snapshot(audio_spectrum_snapshot_t *snapshot)
{
    taskENTER_CRITICAL();
    *snapshot = published;
    taskEXIT_CRITICAL();
}

static void spectrum_entry(void *argument)
{
    (void)argument;
    uint32_t sequence = 0;
    TickType_t last_pcm = 0;
    audio_spectrum_snapshot_t result = {0};
    for (unsigned i = 0; i < MUSIC_FFT_SIZE; i++)
        window[i] = 0.5f - 0.5f * cosf(6.28318530718f * i / (MUSIC_FFT_SIZE - 1U));
    if (arm_rfft_fast_init_f32(&fft, MUSIC_FFT_SIZE) != ARM_MATH_SUCCESS)
    {
        result.errors = 1;
        taskENTER_CRITICAL(); published = result; taskEXIT_CRITICAL();
        vTaskDelete(NULL);
    }
    for (;;)
    {
        uint32_t previous = sequence;
        bool fresh = enabled && platform_music_pcm_copy(stereo, &sequence);
        if (fresh)
        {
            if (sequence > previous + 1U) result.dropped += sequence - previous - 1U;
            last_pcm = xTaskGetTickCount();
            float mean = 0;
            for (unsigned i = 0; i < MUSIC_FFT_SIZE; i++) mean += stereo[i * 2U] / 32768.0f;
            mean /= MUSIC_FFT_SIZE;
            for (unsigned i = 0; i < MUSIC_FFT_SIZE; i++)
                input[i] = (stereo[i * 2U] / 32768.0f - mean) * window[i];
            arm_rfft_fast_f32(&fft, input, output, 0);
            for (unsigned band = 0; band < MUSIC_SPECTRUM_BANDS; band++)
            {
                /* 对数频带覆盖约47Hz～20kHz；至少包含一个有效FFT bin。 */
                unsigned begin = (unsigned)powf(426.0f, (float)band / MUSIC_SPECTRUM_BANDS);
                unsigned end = (unsigned)powf(426.0f, (float)(band + 1U) / MUSIC_SPECTRUM_BANDS);
                if (end <= begin) end = begin + 1U;
                float peak = 0;
                for (unsigned bin = begin; bin < end && bin < MUSIC_FFT_SIZE / 2U; bin++)
                {
                    /* 排除特殊打包的DC/Nyquist，普通bin从output[2]开始。 */
                    float re = output[2U * bin];
                    float im = output[2U * bin + 1U];
                    float power = re * re + im * im;
                    if (power > peak) peak = power;
                }
                float db = 10.0f * log10f(peak * 16.0f / (MUSIC_FFT_SIZE * MUSIC_FFT_SIZE) + 1e-12f);
                float level = (db + 60.0f) * (100.0f / 60.0f);
                if (level < 0) level = 0;
                if (level > 100) level = 100;
                smooth[band] += (level - smooth[band]) * (level > smooth[band] ? 0.7f : 0.15f);
            }
            result.frames++;
        }
        if (!enabled || xTaskGetTickCount() - last_pcm > pdMS_TO_TICKS(150))
            for (unsigned band = 0; band < MUSIC_SPECTRUM_BANDS; band++) smooth[band] *= 0.8f;
        for (unsigned band = 0; band < MUSIC_SPECTRUM_BANDS; band++) result.levels[band] = (uint8_t)smooth[band];
        result.errors = platform_music_pcm_errors();
        taskENTER_CRITICAL(); published = result; taskEXIT_CRITICAL();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

bool audio_spectrum_create(void)
{
    return xTaskCreateStatic(spectrum_entry, "spectrum", 1024, NULL, 1,
                             stack, &task_control) != NULL;
}
