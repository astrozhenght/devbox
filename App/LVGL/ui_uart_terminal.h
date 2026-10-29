#ifndef __LV_APP_UART_H
#define __LV_APP_UART_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    UI_UART_FORMAT_TEXT = 0, // 文本显示/发送
    UI_UART_FORMAT_HEX       // 16进制显示/发送
} ui_uart_format_t;

typedef enum
{
    UI_UART_LINE_ENDING_NONE = 0, // 不追加换行符
    UI_UART_LINE_ENDING_CRLF      // 追加\r\n
} ui_uart_line_ending_t;

typedef enum
{
    UI_UART_DISCONNECTED = 0,
    UI_UART_CONNECTED
} ui_uart_connect_status_t;

void ui_uart_terminal_create(lv_obj_t* parent);


#ifdef __cplusplus
}
#endif

#endif /* __LV_APP_UART_H */
