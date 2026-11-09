#include "runtime_stats.h"

#include "stm32h7xx_hal.h"

#define PLATFORM_RUNTIME_STATS_FREQUENCY_HZ  10000U

void platform_runtime_stats_init(void)
{
    RCC_ClkInitTypeDef clock_config = {0};
    uint32_t flash_latency;
    uint32_t timer_clock;

    __HAL_RCC_TIM5_CLK_ENABLE();
    HAL_RCC_GetClockConfig(&clock_config, &flash_latency);

    /* APB 分频不为 1 时，STM32 定时器时钟是对应 PCLK 的两倍。 */
    timer_clock = HAL_RCC_GetPCLK1Freq();
    if (clock_config.APB1CLKDivider != RCC_HCLK_DIV1)
    {
        timer_clock *= 2U;
    }

    /* TIM5 只提供单调时间基准，不开中断，不参与任何业务定时。 */
    TIM5->CR1 = 0U;
    TIM5->PSC = (timer_clock / PLATFORM_RUNTIME_STATS_FREQUENCY_HZ) - 1U;
    TIM5->ARR = UINT32_MAX;
    TIM5->CNT = 0U;
    TIM5->EGR = TIM_EGR_UG;
    TIM5->SR = 0U;
    TIM5->CR1 = TIM_CR1_CEN;
}

uint32_t platform_runtime_stats_get_counter(void)
{
    return TIM5->CNT;
}
