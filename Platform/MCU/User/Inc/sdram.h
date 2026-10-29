#ifndef __SDRAM_H__
#define __SDRAM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "stm32h7xx_hal.h"

#define SDRAM_BANK_ADDR     ((uint32_t)0xC0000000)       // FMC SDRAM 数据基地址

HAL_StatusTypeDef platform_sdram_init(void);


#ifdef __cplusplus
}
#endif

#endif /* __SDRAM_H__ */
