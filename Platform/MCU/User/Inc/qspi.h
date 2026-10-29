#ifndef __QSPI_H__
#define __QSPI_H__

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern QSPI_HandleTypeDef hqspi;

HAL_StatusTypeDef platform_qspi_init(void);


#ifdef __cplusplus
}
#endif

#endif /* __QSPI_H__ */
