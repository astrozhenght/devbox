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
static volatile uint32_t generation;

void audio_spectrum_enable(bool value)
{
    taskENTER_CRITICAL();
    if (enabled != value)
    {
        enabled = value;
        generation++;
        if (value) memset(published.levels, 0, sizeof(published.levels));
    }
    taskEXIT_CRITICAL();
}

void audio_spectrum_snapshot(audio_spectrum_snapshot_t *snapshot)
{
    taskENTER_CRITICAL();
    *snapshot = published;
    taskEXIT_CRITICAL();
}

/* 两个声道分别加窗变换，各频带取两者中较强的分量，保留反相和单侧信号。 */
static void accumulate_channel_peaks(unsigned channel, const unsigned *edges, float *peaks)
{
    float mean = 0;
    for (unsigned i = 0; i < MUSIC_FFT_SIZE; i++) mean += stereo[2U * i + channel];
    mean /= MUSIC_FFT_SIZE;
    for (unsigned i = 0; i < MUSIC_FFT_SIZE; i++)
        input[i] = (stereo[2U * i + channel] - mean) * (1.0f / 32768.0f) * window[i];
    arm_rfft_fast_f32(&fft, input, output, 0);

    for (unsigned band = 0; band < MUSIC_SPECTRUM_BANDS; band++)
    {
        unsigned begin = edges[band];
        unsigned end = edges[band + 1U];
        if (end <= begin) end = begin + 1U;
        for (unsigned bin = begin; bin < end && bin < MUSIC_FFT_SIZE / 2U; bin++)
        {
            float re = output[2U * bin];
            float im = output[2U * bin + 1U];
            float power = re * re + im * im;
            if (power > peaks[band]) peaks[band] = power;
        }
    }
}

static void spectrum_entry(void *argument)
{
    (void)argument;
    uint32_t sequence = 0;
    uint32_t active_generation = 0;
    bool first_block = true;
    TickType_t last_pcm = 0;
    audio_spectrum_snapshot_t result = {0};
    unsigned edges[MUSIC_SPECTRUM_BANDS + 1U];
    for (unsigned i = 0; i <= MUSIC_SPECTRUM_BANDS; i++)
        edges[i] = (unsigned)powf(426.0f, (float)i / MUSIC_SPECTRUM_BANDS);
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
        taskENTER_CRITICAL();
        bool capture = enabled;
        uint32_t current_generation = generation;
        taskEXIT_CRITICAL();
        if (active_generation != current_generation)
        {
            active_generation = current_generation;
            first_block = true;
            if (capture) memset(smooth, 0, sizeof(smooth));
        }
        uint32_t previous = sequence;
        bool fresh = capture && platform_music_pcm_copy(stereo, &sequence);
        if (fresh)
        {
            if (!first_block) result.dropped += sequence - previous - 1U;
            first_block = false;
            last_pcm = xTaskGetTickCount();
            float peaks[MUSIC_SPECTRUM_BANDS] = {0};
            accumulate_channel_peaks(0, edges, peaks);
            accumulate_channel_peaks(1, edges, peaks);
            for (unsigned band = 0; band < MUSIC_SPECTRUM_BANDS; band++)
            {
                float db = 10.0f * log10f(peaks[band] * 16.0f / (MUSIC_FFT_SIZE * MUSIC_FFT_SIZE) + 1e-12f);
                float level = (db + 60.0f) * (100.0f / 60.0f);
                if (level < 0) level = 0;
                if (level > 100) level = 100;
                /* 快速跟随重音并及时回落，避免连续音乐被过度平滑。 */
                smooth[band] += (level - smooth[band]) * (level > smooth[band] ? 0.90f : 0.40f);
            }
            result.frames++;
        }
        if (!capture || xTaskGetTickCount() - last_pcm > pdMS_TO_TICKS(150))
            for (unsigned band = 0; band < MUSIC_SPECTRUM_BANDS; band++) smooth[band] *= 0.8f;
        for (unsigned band = 0; band < MUSIC_SPECTRUM_BANDS; band++) result.levels[band] = (uint8_t)smooth[band];
        result.errors = platform_music_pcm_errors();
        taskENTER_CRITICAL();
        /* 切歌或跳转可能抢占 FFT，旧采样的结果不能覆盖新会话。 */
        if (generation == current_generation) published = result;
        taskEXIT_CRITICAL();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

bool audio_spectrum_create(void)
{
    return xTaskCreateStatic(spectrum_entry, "spectrum", 1024, NULL, 1,
                             stack, &task_control) != NULL;
}
