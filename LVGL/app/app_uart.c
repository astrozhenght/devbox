#include <stdio.h>
#include "lvgl/lvgl.h"
#include "app_uart.h"
#include "./utils/comm_queue.h"
#include "./utils/str_convert.h"

#if defined(_WIN32) || defined(WIN32) // Windows平台
#define __disable_irq()   ((void)0)
#define __enable_irq()    ((void)0)
#else // STM32平台
#include "stm32h7xx.h"
#include "usart.h"
#include "dma.h"
#endif

//声明字体
LV_FONT_DECLARE(ceshi_12_1);
LV_FONT_DECLARE(ceshi_16_1);
LV_FONT_DECLARE(ceshi_24_1);
LV_FONT_DECLARE(ceshi_24_4);
LV_FONT_DECLARE(ceshi_32_1);
LV_FONT_DECLARE(ceshi_32_2);
LV_FONT_DECLARE(ceshi_32_4);
LV_FONT_DECLARE(ceshi_32_8);
LV_FONT_DECLARE(ceshi_48_1);

uint8_t  g_app_uart_recv_buff[APP_UART_RECV_SIZE];  // 无中断调用，不需要临界区保护
uint8_t  g_app_uart_send_buff[APP_UART_SEND_SIZE];  // 无中断调用，不需要临界区保护

uint32_t g_app_uart_recv_length = 0;        // 累计接收长度，已经放入临界区保护
uint32_t g_app_uart_send_length = 0;        // 无中断调用，不需要临界区保护
uint8_t  g_app_uart_recv_complete_flag = 0; // 已经放入临界区保护
uint8_t  g_app_uart_recv_error_status = 0;  // 错误状态机，0-接收正常，1-接收出现错误需要刷新错误信息到屏幕上，2-错误信息已刷新并且未检测到接受正常
uint8_t  g_app_uart_recv_hex_disp_flag = 0; // 0-UTF8显示，1-16进制显示，无中断调用，不需要临界区保护
uint8_t  g_app_uart_hex_send_flag = 0;      // 0-发送ASCII，1-发送16进制数，无中断调用，不需要临界区保护
uint8_t  g_app_uart_send_new_line_flag = 0; // 0-不发送回车换行，1-发送回车换行，无中断调用，不需要临界区保护

static lv_obj_t* console;
static lv_obj_t* textview_uart_recv; // 未在中断中调用
static lv_obj_t* textview_uart_send;
static lv_obj_t* dropdown_set_com;
static lv_obj_t* dropdown_set_baudrate;
static lv_obj_t* dropdown_set_stopbit;
static lv_obj_t* dropdown_set_databit;
static lv_obj_t* dropdown_set_parity;
static lv_obj_t* btn_connect;
static lv_obj_t* btn_connect_label;
static lv_obj_t* label_recv;
static lv_obj_t* label_send;
static lv_obj_t* btn_clear_recv;
static lv_obj_t* btn_clear_send;
static lv_obj_t* btn_send;
static lv_obj_t* checkbox_hex_disp;
static lv_obj_t* checkbox_hex_send;
static lv_obj_t* checkbox_send_new_line;
static lv_obj_t* keyboard_all;
static lv_obj_t* title_bar;
static lv_obj_t* btn_minimize;
static lv_obj_t* btn_close;
static lv_obj_t* title;
static lv_timer_t* timer_uart_rx;
static lv_obj_t* label; // 公用名称

Comm_Queue uart1_queue;                                   // 已经添加临界区保护
uint8_t g_uart_textarea_buffer[COMM_QUEUE_SIZE];          // 无中断调用，不需要临界区保护
uint8_t g_uart_connect_status = UART_STATUS_DISCONNECTED; // 无中断调用，不需要临界区保护


static void uart_rx_timer_cb(lv_timer_t* t)
{
    LV_UNUSED(t);
    char recv_length[9];   // 确保足够大存放字符串

    if (g_app_uart_recv_complete_flag)
    {
        // 把FIFO队列中数据放入LVGL的文本数组
        __disable_irq();
        uint32_t len = Comm_Queue_Memcpy(&uart1_queue, g_uart_textarea_buffer);
        g_app_uart_recv_complete_flag = 0;
        g_app_uart_recv_error_status = 0;  // 错误状态恢复正常
        if (g_app_uart_recv_length > 999999)
        {
            g_app_uart_recv_length -= 999999; // 限制最大值
        }
        __enable_irq();

        if (g_app_uart_recv_hex_disp_flag == 0) // 非十六进制显示
        {
            g_uart_textarea_buffer[len] = '\0'; // 添加字符串结束符
            lv_textarea_set_text(textview_uart_recv, (const char*)g_uart_textarea_buffer); // 刷新显示
        }
        else // 十六进制显示
        {
            uint8_t* hex_str = lv_mem_alloc(len * 3);
            ascii_to_hexstr((const char*)g_uart_textarea_buffer, (char*)hex_str, len * 3);
            lv_textarea_set_text(textview_uart_recv, (const char*)hex_str); // 刷新显示
            lv_mem_free(hex_str);
        }

        sprintf(recv_length, "R:%d", g_app_uart_recv_length);   // 把整数格式化为字符串
        lv_label_set_text(label_recv, recv_length);
    }

    if (g_app_uart_recv_error_status == 1)
    {
        // 显示错误信息
        // 接收到错误数据比如0xE0时，"#FF0000 Receive Error!#"无法正常显示，因此加空格进行处理
        lv_textarea_add_text(textview_uart_recv, "   \n#FF0000 Receive Error!#");
        g_app_uart_recv_error_status = 2; // 标记错误信息已刷新
    }
}

static void drag_event_handler(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target(e);
    lv_indev_t* indev = lv_indev_get_act();
    if (indev == NULL)  return;

    lv_point_t vect;
    lv_indev_get_vect(indev, &vect);

    int32_t x = lv_obj_get_x_aligned(obj) + vect.x;
    int32_t y = lv_obj_get_y_aligned(obj) + vect.y;
    lv_obj_set_pos(obj, x, y);
}

static void textarea_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* textarea = lv_event_get_target(e);

    switch (code)
    {
    case LV_EVENT_CLICKED:
        if (keyboard_all == NULL)
        {
            lv_obj_t* obj;
            lv_obj_t* label;

            obj = lv_obj_create(console);
            lv_obj_set_size(obj, lv_pct(75), lv_pct(60));
            lv_obj_align_to(obj, textview_uart_send, LV_ALIGN_OUT_TOP_LEFT, 0, 0);
            lv_obj_add_event_cb(obj, drag_event_handler, LV_EVENT_PRESSING, NULL);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK | LV_OBJ_FLAG_SCROLLABLE);  // 禁用自动布局，禁止滚动
            lv_obj_add_flag(obj, LV_OBJ_FLAG_FLOATING);  // 允许自由移动
            lv_obj_move_foreground(obj);  // 保证在最上层

            label = lv_label_create(obj);
            lv_label_set_text(label, "按压拖动");
            lv_obj_set_style_text_font(label, &ceshi_24_4, 0);
            lv_obj_align(label, LV_ALIGN_TOP_MID, 0, -5);
            lv_obj_set_style_text_opa(label, LV_OPA_70, 0);

            keyboard_all = lv_keyboard_create(obj);
            lv_obj_set_size(keyboard_all, lv_pct(100), lv_pct(90));
            lv_keyboard_set_popovers(keyboard_all, true);
            //lv_obj_set_style_text_font(keyboard_all, &lv_font_montserrat_24, 0);
            lv_obj_align(keyboard_all, LV_ALIGN_BOTTOM_MID, 0, 5);
            lv_keyboard_set_textarea(keyboard_all, textview_uart_send);
        }
        break;

    case LV_EVENT_CANCEL:
        lv_obj_del(lv_obj_get_parent(keyboard_all));
        keyboard_all = NULL;
        break;

    default: break;
    }
}

static void dropdown_event_cb(lv_event_t* e)
{
    lv_obj_t* dropdown_obj = lv_event_get_target(e);
    uint8_t option_id = lv_dropdown_get_selected(dropdown_obj);

#if !defined(_WIN32) && !defined(WIN32) // STM32平台
    HAL_UART_DMAStop(&huart1);   // 停止DMA接收
    __HAL_UART_DISABLE(&huart1); // 禁用UART

    if (dropdown_obj == dropdown_set_baudrate)
    {
        switch (option_id)
        {
            case 0: huart1.Init.BaudRate = 4800;   break;
            case 1: huart1.Init.BaudRate = 9600;   break;
            case 2: huart1.Init.BaudRate = 19200;  break;
            case 3: huart1.Init.BaudRate = 38400;  break;
            case 4: huart1.Init.BaudRate = 115200; break;
            case 5: huart1.Init.BaudRate = 128000; break;
            case 6: huart1.Init.BaudRate = 230400; break;
            case 7: huart1.Init.BaudRate = 460800; break;
            case 8: huart1.Init.BaudRate = 921600; break;
            default: break;
        }
    }
    else if (dropdown_obj == dropdown_set_stopbit)
    {
        switch (option_id)
        {
            case 0: huart1.Init.StopBits = UART_STOPBITS_1;   break;
            case 1: huart1.Init.StopBits = UART_STOPBITS_2;   break;
            case 2: huart1.Init.StopBits = UART_STOPBITS_0_5; break;
            case 3: huart1.Init.StopBits = UART_STOPBITS_1_5; break;
            default: break;
        }
    }
    else if (dropdown_obj == dropdown_set_databit)
    {
        switch (option_id)
        {
            case 0: huart1.Init.WordLength = UART_WORDLENGTH_7B; break;
            case 1: huart1.Init.WordLength = UART_WORDLENGTH_8B; break;
            case 2: huart1.Init.WordLength = UART_WORDLENGTH_9B; break;
            default: break;
        }
    }
    else if (dropdown_obj == dropdown_set_parity)
    {
        switch (option_id)
        {
            case 0: huart1.Init.Parity = UART_PARITY_NONE; break;
            case 1: huart1.Init.Parity = UART_PARITY_ODD;  break;
            case 2: huart1.Init.Parity = UART_PARITY_EVEN; break;
            default: break;
        }
    }

    HAL_UART_Init(&huart1);    // 重新初始化UART，内部包含使能UART
    if (g_uart_connect_status == UART_STATUS_DISCONNECTED)
    {
        __HAL_UART_DISABLE(&huart1);
    }
    else
    {
        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, g_app_uart_recv_buff, APP_UART_RECV_SIZE); // 重新初始化接收参数
    }
#endif
}

static void checkbox_event_cb(lv_event_t* e)
{
    lv_obj_t* target = lv_event_get_target(e);
    bool checked = lv_obj_has_state(target, LV_STATE_CHECKED);  // 判断是否被选中

    if (target == checkbox_hex_disp)
    {
        if (checked)
        {
            g_app_uart_recv_hex_disp_flag = 1; // 十六进制显示
        }
        else
        {
            g_app_uart_recv_hex_disp_flag = 0; // UTF8显示
        }
        g_app_uart_recv_complete_flag = 1; // 触发刷新接收相关内容
    }
    else if (target == checkbox_send_new_line) // 发送新行
    {
        if (checked)
        {
            g_app_uart_send_new_line_flag = 1; // 发送新行
        }
        else
        {
            g_app_uart_send_new_line_flag = 0; // 不发送新行
        }
    }
    else if (target == checkbox_hex_send) // 16进制发送
    {
        char* hex_buf = lv_mem_alloc(APP_UART_SEND_SIZE);
        if (checked)
        {
            g_app_uart_hex_send_flag = 1; // 选中16进制发送

            // 限制textarea输入字符，仅允许0-9,a-z,A-Z
            lv_textarea_set_accepted_chars(textview_uart_send, "0123456789abcdefABCDEF ");

            // 更新发送窗口的值
            const char* send_text = lv_textarea_get_text(textview_uart_send);
            if ((send_text == NULL) || (send_text[0] == '\0'))
            {
                return;
            }
            ascii_to_hexstr(send_text, hex_buf, APP_UART_SEND_SIZE);
            lv_textarea_set_text(textview_uart_send, hex_buf);  // 将转换结果显示回文本框
        }
        else
        {
            g_app_uart_hex_send_flag = 0; // 选中发送ASCII码

            // 取消textarea输入字符限制
            lv_textarea_set_accepted_chars(textview_uart_send, NULL);

            // 更新发送窗口的值
            const char* send_text = lv_textarea_get_text(textview_uart_send);
            if ((send_text == NULL) || (send_text[0] == '\0'))
            {
                return;
            }
            hexstr_to_ascii(send_text, hex_buf, APP_UART_SEND_SIZE);
            lv_textarea_set_text(textview_uart_send, hex_buf);  // 将转换结果显示回文本框
        }
        lv_mem_free(hex_buf);
    }
}

static void btn_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);

    if (btn == btn_connect) // 打开串口/关闭串口
    {
        if (g_uart_connect_status == UART_STATUS_DISCONNECTED)
        {
            g_uart_connect_status = UART_STATUS_CONNECTED;
            lv_obj_set_style_bg_color(btn_connect, lv_palette_main(LV_PALETTE_RED), 0);
            lv_label_set_text(btn_connect_label, "关闭串口");
            timer_uart_rx = lv_timer_create(uart_rx_timer_cb, 50, NULL);

#if !defined(_WIN32) && !defined(WIN32)  // STM32平台
            __HAL_UART_ENABLE(&huart1);
            HAL_UARTEx_ReceiveToIdle_DMA(&huart1, g_app_uart_recv_buff, APP_UART_RECV_SIZE);
#endif
        }
        else
        {
#if !defined(_WIN32) && !defined(WIN32)  // STM32平台
            HAL_UART_DMAStop(&huart1);   // 停止DMA接收
            __HAL_UART_DISABLE(&huart1); // 禁用UART
#endif
            g_uart_connect_status = UART_STATUS_DISCONNECTED;
            lv_obj_set_style_bg_color(btn_connect, lv_palette_main(LV_PALETTE_BLUE), 0);
            lv_label_set_text(btn_connect_label, "打开串口");
            lv_timer_del(timer_uart_rx);
        }
    }
    if (btn == btn_clear_recv) // 清除接收
    {
        __disable_irq();
        g_app_uart_recv_length = 0;
        Comm_Queue_Clear(&uart1_queue);
        __enable_irq();
        lv_label_set_text(label_recv, "R:0");
        lv_textarea_set_text(textview_uart_recv, ""); // 清除显示
    }
    if (btn == btn_send) // 发送数据
    {
        if (g_uart_connect_status == UART_STATUS_CONNECTED)
        {
            // 注意，DMA访问不到TCM内存，send_text位于DTCM
            const char* send_text = lv_textarea_get_text(textview_uart_send);
            if ((send_text == NULL) || (send_text[0] == '\0'))
            {
                return;
            }

            uint32_t len = strlen(send_text);
            if (len >= (APP_UART_SEND_SIZE - 2))
            {
                len = (APP_UART_SEND_SIZE - 2);
            }

            // 针对16进制发送功能，处理字符串
            if (g_app_uart_hex_send_flag == 0)
            {
                memcpy(g_app_uart_send_buff, send_text, len);
            }
            else
            {
                const char* send_text = lv_textarea_get_text(textview_uart_send);
                hexstr_to_ascii(send_text, (char*)g_app_uart_send_buff, APP_UART_SEND_SIZE);
                len = strlen((char*)g_app_uart_send_buff);
            }

            // 针对发送新行功能，处理换行
            if (g_app_uart_send_new_line_flag == 1)
            {
                g_app_uart_send_buff[len++] = '\r';
                g_app_uart_send_buff[len++] = '\n';
            }

#if !defined(_WIN32) && !defined(WIN32)
            if (HAL_OK == HAL_UART_Transmit_DMA(&huart1, (const uint8_t*)g_app_uart_send_buff, len))
            {
                // 显示已发送数据个数
                char send_length[9];   // 确保足够大存放字符串，最长S:999999
                g_app_uart_send_length += len;
                if (g_app_uart_send_length > 999999)
                {
                    g_app_uart_send_length -= 999999; // 限制最大值
                }
                sprintf(send_length, "S:%d", g_app_uart_send_length);   // 把整数格式化为字符串
                lv_label_set_text(label_send, send_length);
            }
#else
            // 向Windows控制台显示数据
            g_app_uart_send_buff[len] = 0;
            printf(g_app_uart_send_buff);

            // 显示已发送数据个数
            char send_length[9];   // 确保足够大存放字符串，最长S:999999
            g_app_uart_send_length += len;
            if (g_app_uart_send_length > 999999)
            {
                g_app_uart_send_length -= 999999; // 限制最大值
            }
            sprintf(send_length, "S:%d", g_app_uart_send_length);   // 把整数格式化为字符串
            lv_label_set_text(label_send, send_length);
#endif
        }
    }
    if (btn == btn_clear_send)
    {
        g_app_uart_send_length = 0;
        lv_label_set_text(label_send, "S:0");
        lv_textarea_set_text(textview_uart_send, ""); // 清除显示
    }
    if (btn == btn_close)
    {
#if !defined(_WIN32) && !defined(WIN32)  // STM32平台
        HAL_UART_DMAStop(&huart1);   // 停止DMA接收
        __HAL_UART_DISABLE(&huart1); // 禁用UART
#endif
        g_uart_connect_status = UART_STATUS_DISCONNECTED;
        // lv_timer_del(timer_uart_rx); // 关闭窗口时删除定时器会导致页面卡死
        lv_obj_del(console);
    }
}

void lv_uart_create(lv_obj_t* parent)
{
    // 全局变量初始化
    g_app_uart_recv_length = 0;        // 累计接收长度，已经放入临界区保护
    g_app_uart_send_length = 0;        // 无中断调用，不需要临界区保护
    g_app_uart_recv_complete_flag = 0; // 已经放入临界区保护
    g_app_uart_recv_error_status = 0;  // 错误状态机，0-接收正常，1-接收出现错误需要刷新错误信息到屏幕上，2-错误信息已刷新并且未检测到接受正常

    console = lv_obj_create(parent);
    lv_obj_set_size(console, lv_pct(100), lv_pct(92));
    lv_obj_set_style_border_width(console, 0, 0);
    lv_obj_set_style_pad_all(console, 0, 0);
    lv_obj_set_y(console, lv_pct(8));

    // 标题栏 ===========================================================================================================
    title_bar = lv_obj_create(console);
    lv_obj_set_style_pad_gap(title_bar, 0, 0);   // 去掉容器与子项的外边距
    //lv_obj_set_style_pad_all(title_bar, 0, 0); // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_pad_right(title_bar, 0, 0);
    lv_obj_set_style_pad_left(title_bar, 25, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_bar, 0, 0);  // 设置圆角为0，变成矩形
    static const lv_coord_t top_grid_cols[] = { LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t top_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(title_bar, top_grid_cols, top_grid_rows);
    lv_obj_set_style_grid_row_align(title_bar, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    title = lv_label_create(title_bar);
    lv_obj_set_style_text_font(title, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_PINK), 0);
    lv_label_set_text(title, "串口调试助手");
    //lv_obj_set_height(title_bar, 50);

    btn_minimize = lv_btn_create(title_bar);
    lv_obj_set_style_bg_opa(btn_minimize, LV_OPA_TRANSP, LV_PART_MAIN);      // 背景透明
    lv_obj_set_style_border_opa(btn_minimize, LV_OPA_TRANSP, LV_PART_MAIN);  // 边框透明
    lv_obj_set_style_shadow_opa(btn_minimize, LV_OPA_TRANSP, LV_PART_MAIN);  // 阴影透明
    lv_obj_set_style_radius(btn_minimize, 0, LV_PART_MAIN);                  // 直角
    lv_obj_set_style_bg_opa(btn_minimize, LV_OPA_COVER, LV_STATE_PRESSED);   // 设置按下状态：灰色背景
    lv_obj_set_style_bg_color(btn_minimize, lv_color_hex(0xAAAAAA), LV_STATE_PRESSED);
    label = lv_label_create(btn_minimize);
    lv_obj_set_style_text_color(label, lv_color_hex(0), 0); // 黑色字体颜色
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_label_set_text(label, LV_SYMBOL_MINUS);
    lv_obj_center(label);

    btn_close = lv_btn_create(title_bar);
    lv_obj_set_style_bg_opa(btn_close, LV_OPA_TRANSP, LV_PART_MAIN);      // 背景透明
    lv_obj_set_style_border_opa(btn_close, LV_OPA_TRANSP, LV_PART_MAIN);  // 边框透明
    lv_obj_set_style_shadow_opa(btn_close, LV_OPA_TRANSP, LV_PART_MAIN);  // 阴影透明
    lv_obj_set_style_radius(btn_close, 0, LV_PART_MAIN);                  // 直角
    lv_obj_set_style_bg_opa(btn_close, LV_OPA_COVER, LV_STATE_PRESSED);   // 设置按下状态：红色背景
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0xFF0000), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn_close, btn_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_close);
    lv_obj_set_style_text_color(label, lv_color_hex(0), 0); // 黑色字体颜色
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_label_set_text(label, LV_SYMBOL_CLOSE);
    lv_obj_center(label);

    lv_obj_set_grid_cell(title, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(btn_minimize, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(btn_close, LV_GRID_ALIGN_END, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    // 控制栏 ===========================================================================================================
    lv_obj_t* ctrl = lv_obj_create(console);
    lv_obj_clear_flag(ctrl, LV_OBJ_FLAG_SCROLLABLE);
    //lv_obj_remove_style_all(ctrl);
    //lv_obj_set_height(ctrl, LV_SIZE_CONTENT);
    //lv_obj_set_style_pad_bottom(ctrl, 8, 0);
    static const lv_coord_t ctrl_grid_col[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t ctrl_grid_row[] = { LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST }; // LV_GRID_FR(1)作为弹性占位
    //static const lv_coord_t ctrl_grid_row[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(ctrl, ctrl_grid_col, ctrl_grid_row);
    lv_obj_set_style_grid_row_align(ctrl, LV_GRID_ALIGN_CENTER, 0);
    lv_obj_set_style_grid_column_align(ctrl, LV_GRID_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_scrollbar_mode(ctrl, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(ctrl, 10, 0);

    label = lv_label_create(ctrl);
    lv_obj_set_style_text_font(label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(label, "串口选择");
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    dropdown_set_com = lv_dropdown_create(ctrl);
    lv_dropdown_set_options(dropdown_set_com, "COM1\nCOM2");
    lv_dropdown_set_dir(dropdown_set_com, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(dropdown_set_com, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected(dropdown_set_com, 0); // 默认显示的选项
    lv_obj_set_height(dropdown_set_com, 31);
    lv_obj_set_grid_cell(dropdown_set_com, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    
    label = lv_label_create(ctrl);
    lv_obj_set_style_text_font(label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(label, "波特率");
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);

    dropdown_set_baudrate = lv_dropdown_create(ctrl);
    lv_dropdown_set_options(dropdown_set_baudrate, "4800\n9600\n19200\n38400\n115200\n128000\n230400\n460800\n921600");
    lv_dropdown_set_dir(dropdown_set_baudrate, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(dropdown_set_baudrate, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected(dropdown_set_baudrate, 4); // 默认显示的选项
    lv_obj_add_event_cb(dropdown_set_baudrate, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_height(dropdown_set_baudrate, 31);
    lv_obj_set_grid_cell(dropdown_set_baudrate, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);

    label = lv_label_create(ctrl);
    lv_obj_set_style_text_font(label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(label, "停止位");
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 2, 1);

    dropdown_set_stopbit = lv_dropdown_create(ctrl);
    lv_dropdown_set_options(dropdown_set_stopbit, "1\n2\n0.5\n1.5");
    lv_dropdown_set_dir(dropdown_set_stopbit, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(dropdown_set_stopbit, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected(dropdown_set_stopbit, 0); // 默认显示的选项
    lv_obj_add_event_cb(dropdown_set_stopbit, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_height(dropdown_set_stopbit, 31);
    lv_obj_set_grid_cell(dropdown_set_stopbit, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 2, 1);

    label = lv_label_create(ctrl);
    lv_obj_set_style_text_font(label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(label, "数据位");
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 3, 1);

    dropdown_set_databit = lv_dropdown_create(ctrl);
    lv_dropdown_set_options(dropdown_set_databit, "7 (+parity)\n8 (+parity)\n9 (+parity)");
    lv_dropdown_set_dir(dropdown_set_databit, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(dropdown_set_databit, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected(dropdown_set_databit, 1); // 默认显示的选项
    lv_obj_add_event_cb(dropdown_set_databit, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_height(dropdown_set_databit, 31);
    lv_obj_set_grid_cell(dropdown_set_databit, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 3, 1);

    label = lv_label_create(ctrl);
    lv_obj_set_style_text_font(label, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(label, "校验位");
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 4, 1);

    dropdown_set_parity = lv_dropdown_create(ctrl);
    lv_dropdown_set_options(dropdown_set_parity, "None\nOdd\nEven");
    lv_dropdown_set_dir(dropdown_set_parity, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(dropdown_set_parity, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected(dropdown_set_parity, 0); // 默认显示的选项
    lv_obj_add_event_cb(dropdown_set_parity, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_height(dropdown_set_parity, 31);
    lv_obj_set_grid_cell(dropdown_set_parity, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 4, 1);

    btn_connect = lv_btn_create(ctrl);
    lv_obj_set_style_bg_color(btn_connect, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(btn_connect, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_set_height(btn_connect, 40);
    lv_obj_add_event_cb(btn_connect, btn_event_cb, LV_EVENT_CLICKED, NULL);
    btn_connect_label = lv_label_create(btn_connect);
    lv_obj_set_style_text_font(btn_connect_label, &ceshi_24_4, 0);
    lv_label_set_text(btn_connect_label, "打开串口");


    lv_obj_center(btn_connect_label);
    lv_obj_set_grid_cell(btn_connect, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 6, 1);

    btn_clear_recv = lv_btn_create(ctrl);
    lv_obj_set_style_bg_color(btn_clear_recv, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(btn_clear_recv, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_set_height(btn_clear_recv, 40);
    lv_obj_add_event_cb(btn_clear_recv, btn_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_clear_recv);
    lv_obj_set_style_text_font(label, &ceshi_24_4, 0);
    lv_label_set_text(label, "清除接收");
    lv_obj_center(label);
    lv_obj_set_grid_cell(btn_clear_recv, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 6, 1);

    btn_send = lv_btn_create(ctrl);
    lv_obj_set_style_bg_color(btn_send, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(btn_send, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_set_height(btn_send, 40);
    lv_obj_add_event_cb(btn_send, btn_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_send);
    lv_obj_set_style_text_font(label, &ceshi_24_4, 0);
    lv_label_set_text(label, "发送");
    lv_obj_center(label);
    lv_obj_set_grid_cell(btn_send, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 7, 1);

    btn_clear_send = lv_btn_create(ctrl);
    lv_obj_set_style_bg_color(btn_clear_send, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(btn_clear_send, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_set_height(btn_clear_send, 40);
    lv_obj_add_event_cb(btn_clear_send, btn_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_clear_send);
    lv_obj_set_style_text_font(label, &ceshi_24_4, 0);
    lv_label_set_text(label, "清除发送");
    lv_obj_center(label);
    lv_obj_set_grid_cell(btn_clear_send, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 7, 1);

    checkbox_hex_disp = lv_checkbox_create(ctrl); /* 创建复选框 */
    lv_obj_set_style_text_font(checkbox_hex_disp, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(checkbox_hex_disp, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(checkbox_hex_disp, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_checkbox_set_text(checkbox_hex_disp, "16进制显示");
    lv_obj_set_grid_cell(checkbox_hex_disp, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 9, 1);

    lv_obj_t* cb_time_stamp = lv_checkbox_create(ctrl); /* 创建复选框 */
    lv_obj_set_style_text_font(cb_time_stamp, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(cb_time_stamp, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_checkbox_set_text(cb_time_stamp, "时间戳");
    lv_obj_set_grid_cell(cb_time_stamp, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 9, 1);
    lv_obj_set_style_pad_left(cb_time_stamp, 10, 0);   // 向右偏移 10 像素


    // 底部状态栏 ===========================================================================================================
    lv_obj_t* bottom = lv_obj_create(console);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(bottom, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW); // 创建flow
    lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    checkbox_hex_send = lv_checkbox_create(bottom); /* 创建复选框 */
    lv_obj_set_style_text_font(checkbox_hex_send, &ceshi_24_4, 0);
    lv_checkbox_set_text(checkbox_hex_send, "16进制发送");
    lv_obj_set_style_text_color(checkbox_hex_send, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(checkbox_hex_send, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(checkbox_hex_send, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    checkbox_send_new_line = lv_checkbox_create(bottom); /* 创建复选框 */
    lv_obj_set_style_text_font(checkbox_send_new_line, &ceshi_24_4, 0);
    lv_checkbox_set_text(checkbox_send_new_line, "发送新行");
    lv_obj_set_style_text_color(checkbox_send_new_line, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(checkbox_send_new_line, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(checkbox_send_new_line, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    label_send = lv_label_create(bottom);
    lv_obj_set_style_text_font(label_send, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(label_send, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_width(label_send, 90);
    lv_label_set_text(label_send, "S:0");

    label_recv = lv_label_create(bottom);
    lv_obj_set_style_text_font(label_recv, &ceshi_24_4, 0);
    lv_obj_set_style_text_color(label_recv, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_width(label_recv, 90);
    lv_label_set_text(label_recv, "R:0");

    // 收发区域 ===========================================================================================================
    textview_uart_recv = lv_textarea_create(console);
    textview_uart_send = lv_textarea_create(console);

    lv_textarea_set_placeholder_text(textview_uart_send, "Please enter text...");
    lv_obj_set_style_text_font(textview_uart_send, &ceshi_24_4, 0);
    //lv_obj_remove_state(lvgl_usart.send_textarea, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(textview_uart_send, textarea_event_handler, LV_EVENT_ALL, NULL);

    // 整体布局 ===========================================================================================================
    lv_obj_set_style_pad_gap(console, 0, 0); // 去掉容器与子项的外边距
    static const lv_coord_t console_grid_cols[] = { LV_GRID_FR(10), LV_GRID_FR(3), LV_GRID_FR(3), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t console_grid_rows[] = { LV_GRID_FR(10), LV_GRID_FR(62), LV_GRID_FR(17), LV_GRID_FR(11), LV_GRID_TEMPLATE_LAST };
    //static const lv_coord_t console_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(console, console_grid_cols, console_grid_rows);
    lv_obj_set_style_grid_row_align(console, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    lv_obj_set_grid_cell(bottom, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 3, 1);
    lv_obj_set_grid_cell(title_bar, LV_GRID_ALIGN_STRETCH, 0, 3, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(ctrl, LV_GRID_ALIGN_STRETCH, 1, 2, LV_GRID_ALIGN_STRETCH, 1, 3);
    lv_obj_set_grid_cell(textview_uart_recv, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(textview_uart_send, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);

    // 初始化串口上位机队列
    Comm_Queue_Init(&uart1_queue);

    // 禁止textarea控件光标显示，降低cpu占用率
    lv_obj_clear_state(textview_uart_recv, LV_STATE_FOCUSED);  // 手动清掉焦点
    lv_obj_clear_flag(textview_uart_recv, LV_OBJ_FLAG_CLICK_FOCUSABLE); // 点击时不会获得焦点
    //lv_obj_add_style(textview_uart_recv, &style_uart, 0);
    lv_obj_set_size(textview_uart_recv, 200/*LV_HOR_RES - 100*/, LV_VER_RES - 100);
    lv_label_set_recolor(lv_textarea_get_label(textview_uart_recv), true); // 启用内嵌颜色解析
    lv_obj_set_style_text_font(textview_uart_recv, &ceshi_32_4, LV_PART_MAIN);
}


