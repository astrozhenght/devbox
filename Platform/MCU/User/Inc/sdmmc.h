#ifndef __SDMMC_H__
#define __SDMMC_H__

#include <stdint.h>
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SD_INSTANCES_NBR          1UL

extern SD_HandleTypeDef   hsd_sdmmc[];
extern EXTI_HandleTypeDef hsd_exti[];


void SD_MspInit(SD_HandleTypeDef *hsd);
void SD_MspDeInit(SD_HandleTypeDef *hsd);
HAL_StatusTypeDef platform_sd_init(uint32_t Instance);


#ifdef __cplusplus
}
#endif

#endif /* __SDMMC_H__ */

