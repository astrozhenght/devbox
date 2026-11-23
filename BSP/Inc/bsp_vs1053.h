#ifndef BSP_VS1053_H
#define BSP_VS1053_H
#include <stdbool.h>
#include <stdint.h>
bool bsp_vs1053_init(void);
bool bsp_vs1053_ready(void);
bool bsp_vs1053_send(const uint8_t *data, uint16_t size);
bool bsp_vs1053_volume(uint8_t percent);
bool bsp_vs1053_seconds(uint16_t *seconds);
bool bsp_vs1053_finish(bool cancel);
bool bsp_vs1053_i2s_enable(void);
#endif
