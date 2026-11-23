#include "bsp_vs1053.h"
#include "music_audio.h"
#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

bool bsp_vs1053_ready(void)
{
    return HAL_GPIO_ReadPin(MUSIC_DREQ_PORT, MUSIC_DREQ_PIN) == GPIO_PIN_SET;
}

static bool wait_ready(void)
{
    uint32_t start = HAL_GetTick();
    while (!bsp_vs1053_ready())
    {
        if (HAL_GetTick() - start >= 250U) return false;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
}

static bool sci(uint8_t reg, uint16_t *value, bool write)
{
    uint8_t tx[4] = {write ? 2U : 3U, reg, (uint8_t)(*value >> 8), (uint8_t)*value};
    uint8_t rx[4];
    if (!wait_ready()) return false;
    HAL_GPIO_WritePin(MUSIC_CS_PORT, MUSIC_CS_PIN, GPIO_PIN_RESET);
    bool ok = platform_music_spi_transfer(tx, rx, 4);
    HAL_GPIO_WritePin(MUSIC_CS_PORT, MUSIC_CS_PIN, GPIO_PIN_SET);
    if (!write && ok) *value = ((uint16_t)rx[2] << 8) | rx[3];
    return ok && wait_ready();
}

static bool write_reg(uint8_t reg, uint16_t value) { return sci(reg, &value, true); }
static bool write_memory(uint16_t address, uint16_t value)
{
    return write_reg(7, address) && write_reg(6, value);
}

bool bsp_vs1053_volume(uint8_t percent)
{
    if (percent > 100U) return false;
    uint16_t attenuation = percent == 0 ? 254U : (100U - percent) / 2U;
    return write_reg(11, (attenuation << 8) | attenuation);
}

bool bsp_vs1053_i2s_enable(void)
{
    return write_memory(0xC017U, 0x00F0U) && write_memory(0xC040U, 0x000CU);
}

bool bsp_vs1053_init(void)
{
    if (!MUSIC_HARDWARE_CONFIRMED) return false;
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    HAL_GPIO_WritePin(MUSIC_CS_PORT, MUSIC_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MUSIC_DCS_PORT, MUSIC_DCS_PIN, GPIO_PIN_SET);
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pin = MUSIC_CS_PIN | MUSIC_DCS_PIN;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = MUSIC_RESET_PIN;
    HAL_GPIO_Init(MUSIC_RESET_PORT, &gpio);
    gpio.Pin = MUSIC_DREQ_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(MUSIC_DREQ_PORT, &gpio);
    if (!platform_music_spi_init()) return false;
    HAL_GPIO_WritePin(MUSIC_RESET_PORT, MUSIC_RESET_PIN, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(2));
    HAL_GPIO_WritePin(MUSIC_RESET_PORT, MUSIC_RESET_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(10));
    uint16_t status = 0;
    if (!sci(1, &status, false) || ((status >> 4) & 15U) != 4U) return false;
    /* 12.288MHz晶振、3倍CLKI；先设置安全音量，再开放数字音频输出。 */
    if (!write_reg(0, 0x0800U) || !write_reg(3, 0x6000U) ||
        !bsp_vs1053_volume(50)) return false;
    platform_music_spi_fast();
    return bsp_vs1053_i2s_enable();
}

bool bsp_vs1053_send(const uint8_t *data, uint16_t size)
{
    uint8_t tx[32] = {0};
    uint8_t rx[32];
    if (size == 0 || size > 32 || !bsp_vs1053_ready()) return false;
    memcpy(tx, data, size);
    HAL_GPIO_WritePin(MUSIC_DCS_PORT, MUSIC_DCS_PIN, GPIO_PIN_RESET);
    bool ok = platform_music_spi_transfer(tx, rx, size);
    HAL_GPIO_WritePin(MUSIC_DCS_PORT, MUSIC_DCS_PIN, GPIO_PIN_SET);
    return ok;
}

bool bsp_vs1053_seconds(uint16_t *seconds)
{
    *seconds = 0;
    return sci(4, seconds, false);
}

bool bsp_vs1053_finish(bool cancel)
{
    uint16_t fill = 0;
    uint16_t mode = 0;
    if (!write_reg(7, 0x1E06U) || !sci(6, &fill, false)) return false;
    uint8_t bytes[32];
    memset(bytes, (uint8_t)fill, sizeof(bytes));
    if (!cancel)
        for (unsigned n = 0; n < 66; n++)
            if (!wait_ready() || !bsp_vs1053_send(bytes, 32)) return false;
    if (!sci(0, &mode, false) || !write_reg(0, mode | 8U)) return false;
    for (unsigned n = 0; n < 64; n++)
    {
        if (!wait_ready() || !bsp_vs1053_send(bytes, 32) || !sci(0, &mode, false)) return false;
        if ((mode & 8U) == 0)
        {
            if (cancel)
                for (unsigned j = 0; j < 66; j++)
                    if (!wait_ready() || !bsp_vs1053_send(bytes, 32)) return false;
            return write_reg(4, 0);
        }
    }
    return false; /* CANCEL未完成，交由上层报告并复位，不吞掉故障。 */
}
