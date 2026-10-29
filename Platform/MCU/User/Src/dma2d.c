#include "dma2d.h"
#include "platform_irq_priority.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_ll_dma2d.h"

void platform_dma2d_init(void)
{
    __HAL_RCC_DMA2D_CLK_ENABLE();
    
    /* NVIC configuration for DMA2D interrupts   */
    HAL_NVIC_SetPriority(DMA2D_IRQn,
                         PLATFORM_IRQ_DMA2D_PREEMPT_PRIORITY,
                         PLATFORM_IRQ_DMA2D_SUB_PRIORITY);
    HAL_NVIC_EnableIRQ(DMA2D_IRQn);

    /* Enable the transfer complete interrupt */
    LL_DMA2D_EnableIT_TC(DMA2D);
}

