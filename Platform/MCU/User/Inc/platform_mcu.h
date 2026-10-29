#ifndef __PLATFORM_MCU_H__
#define __PLATFORM_MCU_H__

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif


HAL_StatusTypeDef platform_mcu_init(void);


#ifdef __cplusplus
}
#endif

#endif  // __PLATFORM_MCU_H__