#include "bsp_vs1053.h"
#include "music_audio.h"
#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/**
 * bsp_vs1053_ready() - 采样解码器的 DREQ 就绪引脚
 *
 * Context: GPIO 配置完成后调用，不等待引脚变化。
 * Return: DREQ 为高返回 %true，为低返回 %false。
 */
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

/**
 * sci() - 通过 XCS 执行一次 SCI 寄存器读写
 * @reg: SCI 寄存器号
 * @value: 非空指针，写操作提供寄存器值，读操作用于接收寄存器值
 * @write: %true 写寄存器，%false 读寄存器
 *
 * Context: 音乐任务独占 SPI，传输前及成功传输后均可能等待 DREQ。
 * 每次就绪等待的超时为 250 ms。
 * Return: SPI 传输及两次就绪检查均成功返回 %true，否则返回 %false。
 */
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

/**
 * bsp_vs1053_volume() - 设置左右声道的输出音量
 * @percent: 0 到 100 的音量百分比，0 对应 SCI_VOL 的 0xFEFE 静音值
 *
 * Context: 初始化过程中或初始化完成后，由音乐任务调用，可能等待 DREQ。
 * Return: 音量写入成功返回 %true，参数越界或 SCI 访问失败返回 %false。
 */
bool bsp_vs1053_volume(uint8_t percent)
{
    if (percent > 100U) return false;
    uint16_t attenuation = percent == 0 ? 254U : (100U - percent) / 2U;
    return write_reg(11, (attenuation << 8) | attenuation);
}

/**
 * bsp_vs1053_i2s_enable() - 配置解码器的 I²S 引脚和数字音频输出
 *
 * Context: 解码器初始化后由独占 SPI 的任务调用，可能等待 DREQ。
 * Return: 两项配置均写入成功返回 %true，否则返回 %false，已写入的配置可能保留。
 */
bool bsp_vs1053_i2s_enable(void)
{
    return write_memory(0xC017U, 0x00F0U) && write_memory(0xC040U, 0x000CU);
}

/**
 * bsp_vs1053_init() - 复位 VS1053 并配置耳机播放所需的时钟和音量
 *
 * 先用低速 SPI 确认芯片型号和软复位后的模式，再请求切换到工作速率。
 *
 * Context: 仅音乐任务调用，初始化过程中会延时并等待 DREQ。
 * Return: 硬件未确认、低速 SPI 初始化失败或芯片配置检查失败时返回 %false，
 * 完成配置并请求切换工作速率后返回 %true。
 */
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
    /* 硬复位可能进入GPIO选择的启动模式，软复位后进入正常音频解码。 */
    if (!write_reg(0, 0x0804U)) return false;
    vTaskDelay(pdMS_TO_TICKS(2)); /* 软复位最长需要22000个XTALI周期。 */
    uint16_t mode = 0;
    if (!sci(0, &mode, false) || mode != 0x0800U) return false;
    /* 12.288MHz晶振、3倍CLKI；使用芯片自身的DAC输出到耳机。 */
    if (!write_reg(0, 0x0800U) || !write_reg(3, 0x6000U) ||
        !bsp_vs1053_volume(50)) return false;
    platform_music_spi_fast();
    return true;
}

/**
 * bsp_vs1053_send() - 通过 XDCS 向解码器发送一块音频数据
 * @data: 非空源缓冲区，至少包含 @size 字节
 * @size: 本次传输字节数，范围为 1 到 32
 *
 * Context: 初始化完成后由音乐任务独占 SPI 发送。不等待 DREQ 拉高，SPI 传输会阻塞。
 * Return: 传输成功返回 %true，长度无效、DREQ 为低或 SPI 传输失败返回 %false。
 */
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

/**
 * bsp_vs1053_seconds() - 读取解码器的整秒计时
 * @seconds: 非空输出。仅在返回成功时使用其值
 *
 * Context: 解码器初始化后由音乐任务调用，可能等待 DREQ。
 * Return: SCI_DECODE_TIME 读取成功返回 %true，否则返回 %false。
 */
bool bsp_vs1053_seconds(uint16_t *seconds)
{
    *seconds = 0;
    return sci(4, seconds, false);
}

/**
 * bsp_vs1053_finish() - 结束当前音频流并清零解码计时
 * @cancel: %true 表示主动取消，%false 表示已经送完文件数据
 *
 * 使用芯片的 endFillByte 填充，正常结束时先填充再设置 SM_CANCEL。
 * 主动取消时在 SM_CANCEL 清除后补发剩余的结束填充。
 *
 * Context: 仅音乐任务调用。过程中多次等待 DREQ 并同步传输，
 * 各次等待分别计时，整个函数没有共用的超时。
 * Return: 芯片清除 SM_CANCEL 且计时清零成功返回 %true。
 * 传输失败、DREQ 超时或 64 轮内取消未完成返回 %false。
 */
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
