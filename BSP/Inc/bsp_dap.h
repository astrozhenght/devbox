#ifndef BSP_DAP_H
#define BSP_DAP_H

#include <stdint.h>

#include "bsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bsp_status_t bsp_dap_init(void);
uint32_t bsp_dap_execute_command(const uint8_t *request, uint8_t *response);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DAP_H */
