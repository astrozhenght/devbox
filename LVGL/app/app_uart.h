#ifndef __LV_APP_UART_H
#define __LV_APP_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl/lvgl.h"

#define APP_UART_RECV_SIZE          255
#define APP_UART_SEND_SIZE          255

#define UART_STATUS_DISCONNECTED    0
#define UART_STATUS_CONNECTED       1

void lv_uart_create(lv_obj_t* parent);


#ifdef __cplusplus
}
#endif

#endif /* __LV_APP_UART_H */
