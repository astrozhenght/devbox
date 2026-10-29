#ifndef __BSP_BOARD_H__
#define __BSP_BOARD_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_BOARD_NAME                  "DevBox"
#define BSP_BOARD_VERSION_MAJOR         1U
#define BSP_BOARD_VERSION_MINOR         0U

bsp_status_t bsp_board_init(void);


#ifdef __cplusplus
}
#endif

#endif
