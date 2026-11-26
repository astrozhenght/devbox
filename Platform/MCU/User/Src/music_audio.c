#include "music_audio.h"
#include "platform_irq_priority.h"
#include "stm32h7xx_hal.h"
#include <string.h>

static SPI_HandleTypeDef audio_spi;
static SAI_HandleTypeDef audio_sai;
static DMA_HandleTypeDef audio_dma;
static MUSIC_RAM int16_t pcm_dma[MUSIC_FFT_SIZE * 4U];
static volatile uint32_t pcm_sequence;
static volatile uint32_t pcm_errors;
static volatile unsigned pcm_completed_half;
static volatile bool pcm_valid;
static volatile bool sai_running;

bool platform_music_spi_init(void)
{
    if (!MUSIC_HARDWARE_CONFIRMED) return false;
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &gpio);
    audio_spi.Instance = SPI2;
    audio_spi.Init.Mode = SPI_MODE_MASTER;
    audio_spi.Init.Direction = SPI_DIRECTION_2LINES;
    audio_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    audio_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    audio_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    audio_spi.Init.NSS = SPI_NSS_SOFT;
    audio_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    audio_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    audio_spi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    audio_spi.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    audio_spi.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    return HAL_SPI_Init(&audio_spi) == HAL_OK;
}

void platform_music_spi_fast(void)
{
    /* 200 MHz / 128 = 1.5625 MHz。为模块排线保留时序余量，仍高于音频码率。 */
    audio_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
    (void)HAL_SPI_Init(&audio_spi);
}

bool platform_music_spi_transfer(uint8_t *tx, uint8_t *rx, uint16_t size)
{
    return HAL_SPI_TransmitReceive(&audio_spi, tx, rx, size, 100U) == HAL_OK;
}

bool platform_music_sai_start(void)
{
    GPIO_InitTypeDef gpio = {0};
    if (!MUSIC_HARDWARE_CONFIRMED) return false;
    if (sai_running) return true;
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_SAI1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOE, &gpio);
    audio_sai.Instance = SAI1_Block_A;
    audio_sai.Init.AudioMode = SAI_MODESLAVE_RX;
    audio_sai.Init.Synchro = SAI_ASYNCHRONOUS;
    audio_sai.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
    audio_sai.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
    audio_sai.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_1QF;
    audio_sai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
    audio_sai.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
    audio_sai.Init.MonoStereoMode = SAI_STEREOMODE;
    audio_sai.Init.CompandingMode = SAI_NOCOMPANDING;
    audio_sai.Init.TriState = SAI_OUTPUT_NOTRELEASED;
    if (HAL_SAI_InitProtocol(&audio_sai, SAI_I2S_STANDARD, SAI_PROTOCOL_DATASIZE_16BIT, 2) != HAL_OK)
        return false;
    audio_dma.Instance = DMA2_Stream0;
    audio_dma.Init.Request = DMA_REQUEST_SAI1_A;
    audio_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    audio_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    audio_dma.Init.MemInc = DMA_MINC_ENABLE;
    audio_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    audio_dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    audio_dma.Init.Mode = DMA_CIRCULAR;
    audio_dma.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&audio_dma) != HAL_OK) return false;
    __HAL_LINKDMA(&audio_sai, hdmarx, audio_dma);
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, PLATFORM_IRQ_AUDIO_PREEMPT_PRIORITY,
                         PLATFORM_IRQ_AUDIO_SUB_PRIORITY);
    HAL_NVIC_SetPriority(SAI1_IRQn, PLATFORM_IRQ_AUDIO_PREEMPT_PRIORITY,
                         PLATFORM_IRQ_AUDIO_SUB_PRIORITY);
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_ClearPendingIRQ(SAI1_IRQn);
    pcm_valid = false;
    pcm_errors = 0;
    sai_running = HAL_SAI_Receive_DMA(&audio_sai, (uint8_t *)pcm_dma,
                                     MUSIC_FFT_SIZE * 4U) == HAL_OK;
    if (!sai_running)
    {
        (void)HAL_SAI_DMAStop(&audio_sai);
        return false;
    }
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_EnableIRQ(SAI1_IRQn);
    return sai_running;
}

void platform_music_sai_stop(void)
{
    if (!sai_running) return;
    sai_running = false;
    pcm_valid = false;
    HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_DisableIRQ(SAI1_IRQn);
    audio_sai.Instance->IMR = 0;
    if (HAL_SAI_DMAStop(&audio_sai) != HAL_OK) pcm_errors++;
}

void platform_music_dma_irq(void) { HAL_DMA_IRQHandler(&audio_dma); }
void platform_music_sai_irq(void) { HAL_SAI_IRQHandler(&audio_sai); }
static void pcm_complete(unsigned half)
{
    if (!sai_running) return;
    pcm_completed_half = half;
    __DMB();
    pcm_sequence++;
    pcm_valid = true;
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *sai)
{
    if (sai == &audio_sai) pcm_complete(0);
}
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *sai)
{
    if (sai == &audio_sai) pcm_complete(1);
}
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *sai)
{
    if (sai == &audio_sai) pcm_errors++;
}

bool platform_music_pcm_copy(int16_t *stereo, uint32_t *sequence)
{
    uint32_t before = pcm_sequence;
    if (!sai_running || !pcm_valid || pcm_errors != 0 || before == *sequence) return false;
    /* 记录具体完成的半区，序号在换曲或暂停重启 DMA 时仍连续。 */
    unsigned half = pcm_completed_half;
    unsigned samples = MUSIC_FFT_SIZE * 2U;
    __DMB();
    memcpy(stereo, pcm_dma + half * samples, samples * sizeof(int16_t));
    __DMB();
    /* 除回调序号外还检查 DMA 位置，覆盖事件未进入中断时也丢弃该块。 */
    uint32_t remaining = __HAL_DMA_GET_COUNTER(&audio_dma);
    if (!sai_running || !pcm_valid || pcm_errors != 0 || before != pcm_sequence ||
        remaining == 0 || (half == 0 ? remaining > samples : remaining <= samples)) return false;
    *sequence = before;
    return true;
}

uint32_t platform_music_pcm_errors(void) { return pcm_errors; }
