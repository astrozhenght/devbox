#ifndef __BSP_TYPES_H__
#define __BSP_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_OK = 0,
    BSP_ERROR,
    BSP_ERROR_PARAM,
    BSP_ERROR_TIMEOUT,
    BSP_ERROR_NOT_READY,
    BSP_ERROR_NOT_SUPPORTED,
} bsp_status_t;

#define BSP_REG08(address)      (*((__IO uint8_t *) (address)))
#define BSP_REG16(address)      (*((__IO uint16_t *)(address)))
#define BSP_REG32(address)      (*((__IO uint32_t *)(address)))

#ifdef __cplusplus
}
#endif

#endif
