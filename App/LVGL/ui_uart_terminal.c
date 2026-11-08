#include <stdio.h>
#include <string.h>
#include "ui_uart_terminal.h"
#include "uart_service.h"
#include "uart_service_port.h"
#include "ring_buffer.h"
#include "hex_convert.h"
#include "ui_fonts.h"

#define UART_UI_BG       0xF5F7FA
#define UART_UI_TEXT     0x243247
#define UART_UI_MUTED    0x69788B
#define UART_UI_BORDER   0xE4EAF1

// 对应UI界面的内容
static uint32_t s_tx_total_bytes = 0;       // 对应底部状态栏Label-发送数据个数，无中断调用，不需要临界区保护
static uint8_t s_tx_buffer[UART_APP_TX_BUFFER_SIZE];  // 存储发送区内容；无中断调用，不需要临界区保护
static uint8_t s_rx_display_buffer[RING_BUFFER_SIZE]; // 存储接收区显示的内容；无中断调用，不需要临界区保护
ui_uart_format_t s_send_format = UI_UART_FORMAT_TEXT; // 对应复选框-16进制发送；无中断调用，不需要临界区保护
ui_uart_format_t s_rx_display_format = UI_UART_FORMAT_TEXT; // 对应复选框-16进制显示；无中断调用，不需要临界区保护
uart_service_config_t s_uart_config = {115200, UART_SERVICE_DATA_BITS_8, UART_SERVICE_STOP_BITS_1, UART_SERVICE_PARITY_NONE};
ui_uart_line_ending_t s_send_line_ending = UI_UART_LINE_ENDING_NONE;   // 对应复选框-发送新行；无中断调用，不需要临界区保护
ui_uart_connect_status_t s_uart_connect_status = UI_UART_DISCONNECTED; // 无中断调用，不需要临界区保护

typedef struct
{
    lv_obj_t* container;                     // 一级容器
    lv_obj_t* title_bar;                     // 二级标题栏
    lv_obj_t* receive_area;                  // 二级接收区
    lv_obj_t* send_area;                     // 二级发送区
    lv_obj_t* bottom_status_bar;             // 二级底部状态栏
    lv_obj_t* control_panel;                 // 二级控制面板
    lv_obj_t* body;                          // 收发区与参数面板的横向容器
    lv_obj_t* work_area;                     // 左侧收发面板
    lv_obj_t* receive_panel;                 // 接收卡片
    lv_obj_t* send_panel;                    // 发送卡片
    lv_obj_t* connection_label;              // 底部连接状态与参数摘要
    lv_obj_t* title_bar_title_label;         // 三级
    lv_obj_t* title_bar_home_btn;            // 三级：返回主页按钮
    lv_obj_t* bottom_hex_send_checkbox;      // 三级
    lv_obj_t* bottom_send_new_line_checkbox; // 三级
    lv_obj_t* bottom_rx_label;               // 三级
    lv_obj_t* bottom_tx_label;               // 三级
    lv_obj_t* ctrl_panel_com_dropdown;       // 三级
    lv_obj_t* ctrl_panel_baudrate_dropdown;  // 三级
    lv_obj_t* ctrl_panel_stopbit_dropdown;   // 三级
    lv_obj_t* ctrl_panel_databit_dropdown;   // 三级
    lv_obj_t* ctrl_panel_parity_dropdown;    // 三级
    lv_obj_t* ctrl_panel_open_com_btn;       // 三级
    lv_obj_t* ctrl_panel_open_com_label;     // 三级
    lv_obj_t* ctrl_panel_clr_rx_btn;         // 三级
    lv_obj_t* ctrl_panel_tx_btn;             // 三级
    lv_obj_t* ctrl_panel_clr_tx_btn;         // 三级
    lv_obj_t* ctrl_panel_hex_disp_checkbox;  // 三级
    lv_obj_t* keyboard;                      // 其它
    lv_timer_t* uart_rx_timer;               // 其它
} ui_uart_context_t;

static ui_uart_context_t uart_context = {0};

static void ui_uart_terminal_update_connection(void);

static void ui_uart_terminal_stop_rx_timer(void)
{
    if (uart_context.uart_rx_timer != NULL)
    {
        lv_timer_del(uart_context.uart_rx_timer);
        uart_context.uart_rx_timer = NULL;
    }
}

/* 根对象销毁前停止接收回调，避免回调继续使用已释放的控件。 */
static void ui_uart_terminal_deleted(lv_event_t* event)
{
    (void)event;
    ui_uart_terminal_stop_rx_timer();
    uart_service_port_close();
    s_uart_connect_status = UI_UART_DISCONNECTED;
    memset(&uart_context, 0, sizeof(uart_context));
}


// 函数说明：刷新接收区域的显示内容
// 调用时机：用户点击[16进制显示]checkbox切换显示的时候调用
static void ui_uart_terminal_refresh_receive_display(void)
{
    uart_service_set_rx_cplt_flag(true); // 设置标志位触发刷新
}

// 函数说明：恢复变量的默认值
// 调用时机：用户点击图标进入uart调试助手app时调用
static void ui_uart_terminal_reset_state(void)
{
    // 以下变量在本函数中定义
    s_tx_total_bytes = 0;  // 无中断调用，不需要临界区保护
    s_send_line_ending = UI_UART_LINE_ENDING_NONE;
    s_send_format = UI_UART_FORMAT_TEXT;        // 发送文本
    s_rx_display_format = UI_UART_FORMAT_TEXT;  // 显示文本
    s_uart_config.baud_rate = 115200;
    s_uart_config.data_bits = UART_SERVICE_DATA_BITS_8;
    s_uart_config.stop_bits = UART_SERVICE_STOP_BITS_1;
    s_uart_config.parity = UART_SERVICE_PARITY_NONE;
    s_uart_connect_status = UI_UART_DISCONNECTED;

    // 以下函数修改的变量在uart_service.c中定义
    uart_service_set_rx_total_bytes(0);    // 累计接收长度清零
    uart_service_set_rx_cplt_flag(false);  // 标志位初始化
    uart_service_set_error_status(UART_SERVICE_RX_ERROR_NONE);
}

static void uart_rx_timer_cb(lv_timer_t* t)
{
    LV_UNUSED(t);

    // 接收完成，清除接收完成标志和接收错误标志、更新接收区显示内容、更新接收计数Label显示
    if (uart_service_get_rx_cplt_flag() == true)
    {
        // 把环形缓冲区中接收到的数据放入buffer中，供LVGL显示
        uint32_t len = uart_service_copy_ring_to_disp_buffer(s_rx_display_buffer);
        s_rx_display_buffer[len] = '\0';
        
        // 清除接收完成标志，清除接收错误标志
        uart_service_set_rx_cplt_flag(false);
        uart_service_set_error_status(UART_SERVICE_RX_ERROR_NONE);

        // 限制接收个数的最大值，防止溢出；供底部状态栏显示
        if (uart_service_get_rx_total_bytes() > 999999)
        {
            uart_service_set_rx_total_bytes(uart_service_get_rx_total_bytes() - 999999); // 限制最大值
        }

        // 根据复选框的状态，选择文本或十六进制显示接收区内容
        if (len == 0U || s_rx_display_format == UI_UART_FORMAT_TEXT) // 空接收区或文本显示
        {
            lv_textarea_set_text(uart_context.receive_area, (const char*)s_rx_display_buffer); // 刷新显示
        }
        else // 十六进制显示
        {
            uint8_t* rx_hex_text = lv_mem_alloc(len * 3U + 1U);
            if (rx_hex_text == NULL)
            {
                return;
            }
            bytes_to_hex_string((const char*)s_rx_display_buffer, (char*)rx_hex_text, len * 3U + 1U);
            lv_textarea_set_text(uart_context.receive_area, (const char*)rx_hex_text);
            lv_mem_free(rx_hex_text);
        }

        // 更新接收计数Label显示
        char rx_count_text[9];   // 确保足够大存放字符串
        sprintf(rx_count_text, "R:%d", uart_service_get_rx_total_bytes());
        lv_label_set_text(uart_context.bottom_rx_label, rx_count_text);
    }

    // 检测接收错误状态。如果有错误，让UI显示错误信息
    if (uart_service_get_error_status() == UART_SERVICE_RX_ERROR_PENDING)
    {
        // 接收到错误数据比如0xE0时，"#FF0000 Receive Error!#"无法正常显示，因此加空格进行处理
        lv_textarea_add_text(uart_context.receive_area, "   \n#FF0000 Receive Error!#");
        uart_service_set_error_status(UART_SERVICE_RX_ERROR_REPORTED);
    }
}

// 处理虚拟键盘浮动窗口的拖动事件，该回调注册到LV_EVENT_PRESSING事件
// 在对象持续按压时读取输入设备的移动向量，并根据移动距离更新事件目标对象的位置
static void keyboard_drag_event_cb(lv_event_t* e)
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

// 处理发送区的事件：
// 1.点击发送区文本框时创建并显示虚拟键盘
// 2.收到取消事件时关闭虚拟键盘
static void send_textarea_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* textarea = lv_event_get_target(e);

    switch (code)
    {
        case LV_EVENT_CLICKED:
            if (uart_context.keyboard == NULL)
            {
                lv_obj_t* obj;
                lv_obj_t* label;

                obj = lv_obj_create(uart_context.container);
                /* 先脱离布局，再放到接收区上方，避免相对文本框定位导致越出屏幕。 */
                lv_obj_add_flag(obj, LV_OBJ_FLAG_FLOATING);
                lv_obj_set_size(obj, lv_obj_get_width(uart_context.work_area), lv_pct(60));
                lv_obj_set_style_pad_all(obj, 8, 0);
                lv_obj_set_style_radius(obj, 8, 0);
                lv_obj_set_pos(obj, 0, 48);
                lv_obj_add_event_cb(obj, keyboard_drag_event_cb, LV_EVENT_PRESSING, NULL);
                lv_obj_clear_flag(obj, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK | LV_OBJ_FLAG_SCROLLABLE);  // 禁用自动布局，禁止滚动
                lv_obj_move_foreground(obj);  // 保证在最上层

                label = lv_label_create(obj);
                lv_label_set_text(label, "按压拖动");
                lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
                lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
                lv_obj_set_style_text_opa(label, LV_OPA_70, 0);

                uart_context.keyboard = lv_keyboard_create(obj);
                lv_obj_set_size(uart_context.keyboard, lv_pct(100), lv_pct(85));
                lv_keyboard_set_popovers(uart_context.keyboard, true); // 按下虚拟键盘按键时，在按键上方显示一个放大的按键提示框
                lv_obj_align(uart_context.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
                lv_keyboard_set_textarea(uart_context.keyboard, uart_context.send_area); // LVGL键盘绑定文本框，支持向绑定的文本框发送LV_EVENT_CANCEL事件
            }
            break;
        case LV_EVENT_CANCEL:
            lv_obj_del(lv_obj_get_parent(uart_context.keyboard));
            uart_context.keyboard = NULL;
            break;
        default: break;
    }
}

// 处理串口配置下拉框的选项变更
static void dropdown_event_cb(lv_event_t* e)
{
    lv_obj_t* dropdown_obj = lv_event_get_target(e);
    uint8_t option_id = lv_dropdown_get_selected(dropdown_obj);

    if (dropdown_obj == uart_context.ctrl_panel_baudrate_dropdown)
    {
        switch (option_id)
        {
            case 0: s_uart_config.baud_rate = 4800;   break;
            case 1: s_uart_config.baud_rate = 9600;   break;
            case 2: s_uart_config.baud_rate = 19200;  break;
            case 3: s_uart_config.baud_rate = 38400;  break;
            case 4: s_uart_config.baud_rate = 115200; break;
            case 5: s_uart_config.baud_rate = 128000; break;
            case 6: s_uart_config.baud_rate = 230400; break;
            case 7: s_uart_config.baud_rate = 460800; break;
            case 8: s_uart_config.baud_rate = 921600; break;
            default: break;
        }
    }
    else if (dropdown_obj == uart_context.ctrl_panel_stopbit_dropdown)
    {
        switch (option_id)
        {
            case 0: s_uart_config.stop_bits = UART_SERVICE_STOP_BITS_1;   break;
            case 1: s_uart_config.stop_bits = UART_SERVICE_STOP_BITS_2;   break;
            case 2: s_uart_config.stop_bits = UART_SERVICE_STOP_BITS_0_5; break;
            case 3: s_uart_config.stop_bits = UART_SERVICE_STOP_BITS_1_5; break;
            default: break;
        }
    }
    else if (dropdown_obj == uart_context.ctrl_panel_databit_dropdown)
    {
        switch (option_id)
        {
            case 0: s_uart_config.data_bits = UART_SERVICE_DATA_BITS_7; break;
            case 1: s_uart_config.data_bits = UART_SERVICE_DATA_BITS_8; break;
            case 2: s_uart_config.data_bits = UART_SERVICE_DATA_BITS_9; break;
            default: break;
        }
    }
    else if (dropdown_obj == uart_context.ctrl_panel_parity_dropdown)
    {
        switch (option_id)
        {
            case 0: s_uart_config.parity = UART_SERVICE_PARITY_NONE; break;
            case 1: s_uart_config.parity = UART_SERVICE_PARITY_ODD;  break;
            case 2: s_uart_config.parity = UART_SERVICE_PARITY_EVEN; break;
            default: break;
        }
    }

    if (s_uart_connect_status == UI_UART_CONNECTED)
    {
        if (uart_service_port_open(&s_uart_config) != UART_SERVICE_OK)
        {
            uart_service_port_close();
            ui_uart_terminal_stop_rx_timer();
            s_uart_connect_status = UI_UART_DISCONNECTED;
            ui_uart_terminal_update_connection();
            lv_label_set_text(uart_context.connection_label, "串口配置失败");
            lv_obj_set_style_text_color(uart_context.connection_label, lv_palette_main(LV_PALETTE_RED), 0);
            return;
        }
    }
    ui_uart_terminal_update_connection();
}

// 处理串口终端复选框的状态变更事件
static void checkbox_event_cb(lv_event_t* e)
{
    lv_obj_t* target = lv_event_get_target(e);
    bool checked = lv_obj_has_state(target, LV_STATE_CHECKED);  // 判断是否被选中

    if (target == uart_context.ctrl_panel_hex_disp_checkbox)
    {
        if (checked)
        {
            s_rx_display_format = UI_UART_FORMAT_HEX; // 十六进制显示
        }
        else
        {
            s_rx_display_format = UI_UART_FORMAT_TEXT; // 文本显示
        }
        ui_uart_terminal_refresh_receive_display(); // 刷新接收区显示内容
    }
    else if (target == uart_context.bottom_send_new_line_checkbox) // 发送新行
    {
        if (checked)
        {
            s_send_line_ending = UI_UART_LINE_ENDING_CRLF; // 发送回车换行
        }
        else
        {
            s_send_line_ending = UI_UART_LINE_ENDING_NONE; // 不发送回车换行
        }
    }
    else if (target == uart_context.bottom_hex_send_checkbox) // 16进制发送
    {
        const char* input_text = lv_textarea_get_text(uart_context.send_area);
        char* output_text = NULL;
        if (input_text != NULL && input_text[0] != '\0')
        {
            output_text = lv_mem_alloc(UART_APP_TX_BUFFER_SIZE);
            if (output_text == NULL)
            {
                if (s_send_format == UI_UART_FORMAT_HEX) lv_obj_add_state(target, LV_STATE_CHECKED);
                else lv_obj_clear_state(target, LV_STATE_CHECKED);
                return;
            }
        }
        s_send_format = checked ? UI_UART_FORMAT_HEX : UI_UART_FORMAT_TEXT;
        lv_textarea_set_accepted_chars(uart_context.send_area, checked ? "0123456789abcdefABCDEF " : NULL);
        if ((input_text == NULL) || (input_text[0] == '\0'))
        {
            return;
        }
        if (checked)
        {
            bytes_to_hex_string(input_text, output_text, UART_APP_TX_BUFFER_SIZE);
        }
        else
        {
            hex_string_to_bytes(input_text, output_text, UART_APP_TX_BUFFER_SIZE);
        }
        lv_textarea_set_text(uart_context.send_area, output_text); // 更新发送窗口的值
        lv_mem_free(output_text);
    }
}

// 处理串口终端按钮的点击事件
static void btn_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);

    if (btn == uart_context.ctrl_panel_open_com_btn) // 打开串口/关闭串口
    {
        if (s_uart_connect_status == UI_UART_DISCONNECTED)
        {
            if (uart_service_port_open(&s_uart_config) != UART_SERVICE_OK)
            {
                uart_service_port_close();
                lv_label_set_text(uart_context.connection_label, "串口打开失败");
                lv_obj_set_style_text_color(uart_context.connection_label, lv_palette_main(LV_PALETTE_RED), 0);
                return;
            }
            uart_context.uart_rx_timer = lv_timer_create(uart_rx_timer_cb, 50, NULL);
            if (uart_context.uart_rx_timer == NULL)
            {
                uart_service_port_close();
                lv_label_set_text(uart_context.connection_label, "界面内存不足");
                return;
            }
            s_uart_connect_status = UI_UART_CONNECTED;
        }
        else
        {
            uart_service_port_close(); // 关闭UART
            s_uart_connect_status = UI_UART_DISCONNECTED;
            ui_uart_terminal_stop_rx_timer();
        }
        ui_uart_terminal_update_connection();
    }
    if (btn == uart_context.ctrl_panel_clr_rx_btn) // 清除接收区显示
    {
        uart_service_clear_rx_ring_buffer(); // 清除环形缓冲区中接收到的数据
        uart_service_set_rx_total_bytes(0); // 清零串口接收数据计数值
        lv_label_set_text(uart_context.bottom_rx_label, "R:0");
        lv_textarea_set_text(uart_context.receive_area, "");
    }
    if (btn == uart_context.ctrl_panel_tx_btn) // 发送数据
    {
        if (s_uart_connect_status == UI_UART_CONNECTED)
        {
            // 注意，DMA访问不到TCM内存，send_text位于DTCM
            const char* input_text = lv_textarea_get_text(uart_context.send_area);
            if ((input_text == NULL) || (input_text[0] == '\0'))
            {
                return;
            }

            uint32_t len = strlen(input_text);
            if (len >= (UART_APP_TX_BUFFER_SIZE - 2))
            {
                len = (UART_APP_TX_BUFFER_SIZE - 2);
            }

            // 针对16进制发送功能，处理字符串
            if (s_send_format == UI_UART_FORMAT_TEXT) // 直接发送文本
            {
                memcpy(s_tx_buffer, input_text, len);
            }
            else // 将HEX字符串转换成字节后发送
            {
                hex_string_to_bytes(input_text, (char*)s_tx_buffer, UART_APP_TX_BUFFER_SIZE);
                len = strlen((char*)s_tx_buffer);
            }

            // 添加回车换行
            if (s_send_line_ending == UI_UART_LINE_ENDING_CRLF)
            {
                s_tx_buffer[len++] = '\r';
                s_tx_buffer[len++] = '\n';
            }

            // 发送数据
            if (uart_service_port_send((const uint8_t*)s_tx_buffer, len) == UART_SERVICE_OK)
            {
                char send_length[9];   // 确保足够大存放字符串，最长S:999999
                s_tx_total_bytes += len;
                if (s_tx_total_bytes > 999999)
                {
                    s_tx_total_bytes -= 999999; // 限制最大值
                }
                sprintf(send_length, "S:%d", s_tx_total_bytes);  // 把整数格式化为字符串
                lv_label_set_text(uart_context.bottom_tx_label, send_length);
            }
        }
    }
    if (btn == uart_context.ctrl_panel_clr_tx_btn)
    {
        s_tx_total_bytes = 0;
        lv_label_set_text(uart_context.bottom_tx_label, "S:0");
        lv_textarea_set_text(uart_context.send_area, ""); // 清除显示
    }
    if (btn == uart_context.title_bar_home_btn)
    {
        lv_obj_del(uart_context.container);
    }
}

/* 轻量容器和局部样式，沿用文件管理器的浅色面板与细边框。 */
static lv_obj_t* uart_ui_panel(lv_obj_t* parent, bool card)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, card ? 8 : 0, 0);
    lv_obj_set_style_pad_gap(obj, 6, 0);
    if (card)
    {
        lv_obj_set_style_bg_color(obj, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(UART_UI_BORDER), 0);
        lv_obj_set_style_radius(obj, 8, 0);
    }
    return obj;
}

static lv_obj_t* uart_ui_button(lv_obj_t* parent, const char* text, lv_coord_t width)
{
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, 38);
    lv_obj_set_style_pad_all(button, 3, 0);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(UART_UI_BORDER), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_white(), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(UART_UI_TEXT), 0);
    lv_obj_set_style_text_font(button, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xEAF0F7), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xEDF1F5), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, lv_color_hex(0xA5AFBC), LV_STATE_DISABLED);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, btn_event_cb, LV_EVENT_CLICKED, NULL);
    return button;
}

static void uart_ui_spacer(lv_obj_t* parent)
{
    lv_obj_t* spacer = uart_ui_panel(parent, false);
    lv_obj_set_size(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);
}

static lv_obj_t* uart_ui_checkbox(lv_obj_t* parent, const char* text)
{
    lv_obj_t* checkbox = lv_checkbox_create(parent);
    lv_checkbox_set_text(checkbox, text);
    lv_obj_set_style_text_font(checkbox, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_pad_column(checkbox, 5, 0);
    lv_obj_set_style_size(checkbox, 22, LV_PART_INDICATOR);
    lv_obj_set_style_radius(checkbox, 4, LV_PART_INDICATOR);
    lv_obj_add_event_cb(checkbox, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return checkbox;
}

static void ui_uart_terminal_update_connection(void)
{
    bool connected = s_uart_connect_status == UI_UART_CONNECTED;
    const char* stop = s_uart_config.stop_bits == UART_SERVICE_STOP_BITS_2 ? "2" :
                       s_uart_config.stop_bits == UART_SERVICE_STOP_BITS_0_5 ? "0.5" :
                       s_uart_config.stop_bits == UART_SERVICE_STOP_BITS_1_5 ? "1.5" : "1";
    char parity = s_uart_config.parity == UART_SERVICE_PARITY_ODD ? 'O' :
                  s_uart_config.parity == UART_SERVICE_PARITY_EVEN ? 'E' : 'N';
    lv_label_set_text_fmt(uart_context.connection_label, "%s  COM1 | %lu %u%c%s",
        connected ? "已连接" : "未连接", (unsigned long)s_uart_config.baud_rate,
        (unsigned)s_uart_config.data_bits, parity, stop);
    lv_obj_set_style_text_color(uart_context.connection_label,
        connected ? lv_palette_darken(LV_PALETTE_GREEN, 2) : lv_color_hex(UART_UI_MUTED), 0);
    lv_label_set_text(uart_context.ctrl_panel_open_com_label, connected ? "关闭串口" : "打开串口");
    lv_obj_set_style_bg_color(uart_context.ctrl_panel_open_com_btn,
        lv_palette_main(connected ? LV_PALETTE_RED : LV_PALETTE_GREEN), 0);
    lv_obj_set_style_bg_color(uart_context.ctrl_panel_open_com_btn,
        lv_palette_darken(connected ? LV_PALETTE_RED : LV_PALETTE_GREEN, 2), LV_STATE_PRESSED);
    if (connected) lv_obj_clear_state(uart_context.ctrl_panel_tx_btn, LV_STATE_DISABLED);
    else lv_obj_add_state(uart_context.ctrl_panel_tx_btn, LV_STATE_DISABLED);
}

static void ui_uart_terminal_create_container(lv_obj_t* parent)
{
    uart_context.container = uart_ui_panel(parent, false);
    lv_obj_set_size(uart_context.container, lv_pct(100), lv_pct(92));
    lv_obj_set_y(uart_context.container, lv_pct(8));
    lv_obj_set_style_pad_all(uart_context.container, 8, 0);
    lv_obj_set_style_bg_opa(uart_context.container, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(uart_context.container, lv_color_hex(UART_UI_BG), 0);
    lv_obj_set_style_text_font(uart_context.container, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(uart_context.container, lv_color_hex(UART_UI_TEXT), 0);
    lv_obj_set_flex_flow(uart_context.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(uart_context.container, ui_uart_terminal_deleted, LV_EVENT_DELETE, NULL);
}

static void ui_uart_terminal_create_title_bar(void)
{
    uart_context.title_bar = uart_ui_panel(uart_context.container, false);
    lv_obj_set_size(uart_context.title_bar, lv_pct(100), 42);
    lv_obj_set_style_pad_left(uart_context.title_bar, 10, 0);
    lv_obj_set_flex_flow(uart_context.title_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(uart_context.title_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    uart_context.title_bar_title_label = lv_label_create(uart_context.title_bar);
    lv_label_set_text(uart_context.title_bar_title_label, "串口调试助手");
    lv_obj_set_style_text_color(uart_context.title_bar_title_label, lv_palette_main(LV_PALETTE_PINK), 0);
    uart_ui_spacer(uart_context.title_bar);
    uart_context.title_bar_home_btn = uart_ui_button(uart_context.title_bar, "返回主页", 112);
    lv_obj_set_style_bg_color(uart_context.title_bar_home_btn, lv_palette_main(LV_PALETTE_RED), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(uart_context.title_bar_home_btn, lv_color_white(), LV_STATE_PRESSED);
}

static lv_obj_t* uart_ui_parameter(lv_obj_t* parent, const char* title, const char* options, uint16_t selected)
{
    lv_obj_t* row = uart_ui_panel(parent, false);
    lv_obj_set_size(row, lv_pct(100), 36);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_width(label, 72);
    lv_obj_set_style_text_color(label, lv_color_hex(UART_UI_MUTED), 0);
    lv_obj_t* dropdown = lv_dropdown_create(row);
    lv_obj_set_size(dropdown, 0, 36);
    lv_obj_set_flex_grow(dropdown, 1);
    lv_obj_set_style_pad_ver(dropdown, 1, 0);
    lv_obj_set_style_pad_hor(dropdown, 5, 0);
    lv_obj_set_style_radius(dropdown, 6, 0);
    lv_obj_set_style_border_color(dropdown, lv_color_hex(UART_UI_BORDER), 0);
    lv_obj_set_style_text_font(dropdown, &ui_font_chinese_24_4, 0);
    /* 箭头使用现有图标字体，参数及列表文字仍统一24px。 */
    lv_obj_set_style_text_font(dropdown, &lv_font_montserrat_22, LV_PART_INDICATOR);
    lv_dropdown_set_options(dropdown, options);
    lv_dropdown_set_selected(dropdown, selected);
    lv_obj_t* list = lv_dropdown_get_list(dropdown);
    lv_obj_set_style_text_font(list, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_font(list, &ui_font_chinese_24_4, LV_PART_SELECTED);
    lv_obj_add_event_cb(dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return dropdown;
}

static void ui_uart_terminal_create_control_panel(void)
{
    uart_context.body = uart_ui_panel(uart_context.container, false);
    lv_obj_set_size(uart_context.body, lv_pct(100), 0);
    lv_obj_set_flex_grow(uart_context.body, 1);
    lv_obj_set_style_pad_column(uart_context.body, 8, 0);
    lv_obj_set_flex_flow(uart_context.body, LV_FLEX_FLOW_ROW);
    uart_context.work_area = uart_ui_panel(uart_context.body, false);
    lv_obj_set_size(uart_context.work_area, 0, lv_pct(100));
    lv_obj_set_flex_grow(uart_context.work_area, 1);
    lv_obj_set_style_pad_row(uart_context.work_area, 8, 0);
    lv_obj_set_flex_flow(uart_context.work_area, LV_FLEX_FLOW_COLUMN);
    uart_context.control_panel = uart_ui_panel(uart_context.body, true);
    lv_obj_set_size(uart_context.control_panel, 236, lv_pct(100));
    lv_obj_set_flex_flow(uart_context.control_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* title = lv_label_create(uart_context.control_panel);
    lv_label_set_text(title, "串口设置");
    /* 当前后端为UART1，不提供没有对应硬件切换的COM2入口。 */
    uart_context.ctrl_panel_com_dropdown = uart_ui_parameter(uart_context.control_panel, "串口", "COM1", 0);
    lv_obj_add_state(uart_context.ctrl_panel_com_dropdown, LV_STATE_DISABLED);
    uart_context.ctrl_panel_baudrate_dropdown = uart_ui_parameter(uart_context.control_panel, "波特率",
        "4800\n9600\n19200\n38400\n115200\n128000\n230400\n460800\n921600", 4);
    uart_context.ctrl_panel_databit_dropdown = uart_ui_parameter(uart_context.control_panel, "数据位", "7\n8\n9", 1);
    uart_context.ctrl_panel_stopbit_dropdown = uart_ui_parameter(uart_context.control_panel, "停止位", "1\n2\n0.5\n1.5", 0);
    uart_context.ctrl_panel_parity_dropdown = uart_ui_parameter(uart_context.control_panel, "校验位", "None\nOdd\nEven", 0);
    uart_ui_spacer(uart_context.control_panel);
    uart_context.ctrl_panel_open_com_btn = uart_ui_button(uart_context.control_panel, "打开串口", lv_pct(100));
    lv_obj_set_style_text_color(uart_context.ctrl_panel_open_com_btn, lv_color_white(), 0);
    uart_context.ctrl_panel_open_com_label = lv_obj_get_child(uart_context.ctrl_panel_open_com_btn, 0);
}

static lv_obj_t* uart_ui_area_header(lv_obj_t* panel, const char* title)
{
    lv_obj_t* header = uart_ui_panel(panel, false);
    lv_obj_set_size(header, lv_pct(100), 38);
    lv_obj_set_style_pad_column(header, 8, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* label = lv_label_create(header);
    lv_label_set_text(label, title);
    return header;
}

static lv_obj_t* uart_ui_textarea(lv_obj_t* parent)
{
    lv_obj_t* area = lv_textarea_create(parent);
    lv_obj_set_style_text_font(area, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(area, lv_color_hex(UART_UI_TEXT), 0);
    lv_obj_set_style_pad_all(area, 4, 0);
    lv_obj_set_style_border_width(area, 0, 0);
    lv_obj_set_style_shadow_width(area, 0, 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, 0);
    return area;
}

static void ui_uart_terminal_create_receive_area(void)
{
    uart_context.receive_panel = uart_ui_panel(uart_context.work_area, true);
    lv_obj_set_size(uart_context.receive_panel, lv_pct(100), 0);
    lv_obj_set_flex_grow(uart_context.receive_panel, 1);
    lv_obj_set_flex_flow(uart_context.receive_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* header = uart_ui_area_header(uart_context.receive_panel, "接收区");
    uart_ui_spacer(header);
    uart_context.ctrl_panel_hex_disp_checkbox = uart_ui_checkbox(header, "HEX");
    uart_context.ctrl_panel_clr_rx_btn = uart_ui_button(header, "清空", 72);
    uart_context.receive_area = uart_ui_textarea(uart_context.receive_panel);
    lv_obj_set_size(uart_context.receive_area, lv_pct(100), 0);
    lv_obj_set_flex_grow(uart_context.receive_area, 1);
    lv_textarea_set_placeholder_text(uart_context.receive_area, "等待接收数据...");
    lv_obj_clear_state(uart_context.receive_area, LV_STATE_FOCUSED);
    lv_obj_clear_flag(uart_context.receive_area, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_label_set_recolor(lv_textarea_get_label(uart_context.receive_area), true);
}

static void ui_uart_terminal_create_send_area(void)
{
    uart_context.send_panel = uart_ui_panel(uart_context.work_area, true);
    lv_obj_set_size(uart_context.send_panel, lv_pct(100), 128);
    lv_obj_set_flex_flow(uart_context.send_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* header = uart_ui_area_header(uart_context.send_panel, "发送区");
    uart_ui_spacer(header);
    uart_context.bottom_hex_send_checkbox = uart_ui_checkbox(header, "HEX");
    uart_context.bottom_send_new_line_checkbox = uart_ui_checkbox(header, "换行");
    uart_context.ctrl_panel_clr_tx_btn = uart_ui_button(header, "清空", 72);
    lv_obj_t* row = uart_ui_panel(uart_context.send_panel, false);
    lv_obj_set_size(row, lv_pct(100), 0);
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    uart_context.send_area = uart_ui_textarea(row);
    lv_obj_set_size(uart_context.send_area, 0, lv_pct(100));
    lv_obj_set_flex_grow(uart_context.send_area, 1);
    lv_textarea_set_max_length(uart_context.send_area, UART_APP_TX_BUFFER_SIZE - 3U);
    lv_textarea_set_placeholder_text(uart_context.send_area, "输入发送内容...");
    lv_obj_add_event_cb(uart_context.send_area, send_textarea_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(uart_context.send_area, send_textarea_event_cb, LV_EVENT_CANCEL, NULL);
    uart_context.ctrl_panel_tx_btn = uart_ui_button(row, "发送", 72);
    lv_obj_set_style_bg_color(uart_context.ctrl_panel_tx_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_text_color(uart_context.ctrl_panel_tx_btn, lv_color_white(), 0);
    lv_obj_set_style_bg_color(uart_context.ctrl_panel_tx_btn, lv_palette_darken(LV_PALETTE_BLUE, 2), LV_STATE_PRESSED);
}

static void ui_uart_terminal_create_bottom_status_bar(void)
{
    uart_context.bottom_status_bar = uart_ui_panel(uart_context.container, false);
    lv_obj_set_size(uart_context.bottom_status_bar, lv_pct(100), 30);
    lv_obj_set_flex_flow(uart_context.bottom_status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(uart_context.bottom_status_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    uart_context.connection_label = lv_label_create(uart_context.bottom_status_bar);
    lv_obj_set_width(uart_context.connection_label, 0);
    lv_obj_set_flex_grow(uart_context.connection_label, 1);
    lv_label_set_long_mode(uart_context.connection_label, LV_LABEL_LONG_DOT);
    uart_context.bottom_tx_label = lv_label_create(uart_context.bottom_status_bar);
    lv_label_set_text(uart_context.bottom_tx_label, "S:0");
    lv_obj_set_style_text_color(uart_context.bottom_tx_label, lv_color_hex(UART_UI_MUTED), 0);
    lv_obj_set_width(uart_context.bottom_tx_label, 98);
    uart_context.bottom_rx_label = lv_label_create(uart_context.bottom_status_bar);
    lv_label_set_text(uart_context.bottom_rx_label, "R:0");
    lv_obj_set_style_text_color(uart_context.bottom_rx_label, lv_color_hex(UART_UI_MUTED), 0);
    lv_obj_set_width(uart_context.bottom_rx_label, 98);
}

void ui_uart_terminal_create(lv_obj_t* parent)
{
    if (uart_context.container != NULL)
    {
        lv_obj_move_foreground(uart_context.container);
        return;
    }
    ui_uart_terminal_reset_state();
    ui_uart_terminal_create_container(parent);
    ui_uart_terminal_create_title_bar();
    ui_uart_terminal_create_control_panel();
    ui_uart_terminal_create_bottom_status_bar();
    ui_uart_terminal_create_receive_area();
    ui_uart_terminal_create_send_area();
    uart_service_init_rx_ring_buffer();
    ui_uart_terminal_update_connection();
}
