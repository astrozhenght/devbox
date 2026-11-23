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
    /* 200MHz内核时钟/64=3.125MHz，低于36.864MHz CLKI/7的SCI读取上限。 */
    audio_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
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
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_SetPriority(SAI1_IRQn, PLATFORM_IRQ_AUDIO_PREEMPT_PRIORITY,
                         PLATFORM_IRQ_AUDIO_SUB_PRIORITY);
    HAL_NVIC_EnableIRQ(SAI1_IRQn);
    pcm_sequence = 0;
    pcm_errors = 0;
    sai_running = HAL_SAI_Receive_DMA(&audio_sai, (uint8_t *)pcm_dma,
                                     MUSIC_FFT_SIZE * 4U) == HAL_OK;
    return sai_running;
}

void platform_music_sai_stop(void)
{
    if (sai_running) (void)HAL_SAI_DMAStop(&audio_sai);
    sai_running = false;
}

void platform_music_dma_irq(void) { HAL_DMA_IRQHandler(&audio_dma); }
void platform_music_sai_irq(void) { HAL_SAI_IRQHandler(&audio_sai); }
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *sai)
{
    if (sai == &audio_sai) { __DMB(); pcm_sequence++; }
}
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *sai)
{
    if (sai == &audio_sai) { __DMB(); pcm_sequence++; }
}
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *sai)
{
    if (sai == &audio_sai) pcm_errors++;
}

bool platform_music_pcm_copy(int16_t *stereo, uint32_t *sequence)
{
    uint32_t before = pcm_sequence;
    if (!sai_running || before == 0 || before == *sequence) return false;
    /* 完成奇数次时前半块稳定，偶数次时后半块稳定。DMA不可写入DTCM。 */
    uint32_t offset = (before & 1U) ? 0U : MUSIC_FFT_SIZE * 2U;
    __DMB();
    memcpy(stereo, pcm_dma + offset, MUSIC_FFT_SIZE * 2U * sizeof(int16_t));
    __DMB();
    if (before != pcm_sequence) return false; /* 复制时被DMA追上则整块丢弃。 */
    *sequence = before;
    return true;
}

uint32_t platform_music_pcm_errors(void) { return pcm_errors; }
