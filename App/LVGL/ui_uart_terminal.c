#include <stdio.h>
#include "ui_uart_terminal.h"
#include "uart_service.h"
#include "uart_service_port.h"
#include "ring_buffer.h"
#include "hex_convert.h"
#include "ui_fonts.h"

// 对应UI界面的内容
static uint32_t s_tx_total_bytes = 0;       // 对应底部状态栏Label-发送数据个数，无中断调用，不需要临界区保护
static uint8_t s_tx_buffer[UART_APP_TX_BUFFER_SIZE]; // 存储发送区内容；无中断调用，不需要临界区保护
static uint8_t s_rx_display_buffer[RING_BUFFER_SIZE];  // 存储接收区显示的内容；无中断调用，不需要临界区保护
ui_uart_format_t s_send_format = UI_UART_FORMAT_TEXT;  // 对应复选框-16进制发送；无中断调用，不需要临界区保护
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
    lv_obj_t* title_bar_title_label;         // 三级
    lv_obj_t* title_bar_minimize_btn;        // 三级
    lv_obj_t* title_bar_close_btn;           // 三级
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
    lv_obj_t* ctrl_panel_timestamp_checkbox; // 三级
    lv_obj_t* keyboard;                      // 其它
    lv_timer_t* uart_rx_timer;               // 其它
} ui_uart_context_t;

static ui_uart_context_t uart_context = {0};


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
        
        // 清除接收完成标志，清除接收错误标志
        uart_service_set_rx_cplt_flag(false);
        uart_service_set_error_status(UART_SERVICE_RX_ERROR_NONE);

        // 限制接收个数的最大值，防止溢出；供底部状态栏显示
        if (uart_service_get_rx_total_bytes() > 999999)
        {
            uart_service_set_rx_total_bytes(uart_service_get_rx_total_bytes() - 999999); // 限制最大值
        }

        // 根据复选框的状态，选择文本或十六进制显示接收区内容
        if (s_rx_display_format == UI_UART_FORMAT_TEXT) // 文本显示
        {
            s_rx_display_buffer[len] = '\0'; // 添加字符串结束符
            lv_textarea_set_text(uart_context.receive_area, (const char*)s_rx_display_buffer); // 刷新显示
        }
        else // 十六进制显示
        {
            uint8_t* rx_hex_text = lv_mem_alloc(len * 3);
            if (rx_hex_text == NULL)
            {
                return;
            }
            bytes_to_hex_string((const char*)s_rx_display_buffer, (char*)rx_hex_text, len * 3);
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
                lv_obj_set_size(obj, lv_pct(75), lv_pct(60));
                lv_obj_align_to(obj, uart_context.send_area, LV_ALIGN_OUT_TOP_LEFT, 0, 0);
                lv_obj_add_event_cb(obj, keyboard_drag_event_cb, LV_EVENT_PRESSING, NULL);
                lv_obj_clear_flag(obj, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK | LV_OBJ_FLAG_SCROLLABLE);  // 禁用自动布局，禁止滚动
                lv_obj_add_flag(obj, LV_OBJ_FLAG_FLOATING);  // 允许自由移动
                lv_obj_move_foreground(obj);  // 保证在最上层

                label = lv_label_create(obj);
                lv_label_set_text(label, "按压拖动");
                lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
                lv_obj_align(label, LV_ALIGN_TOP_MID, 0, -5);
                lv_obj_set_style_text_opa(label, LV_OPA_70, 0);

                uart_context.keyboard = lv_keyboard_create(obj);
                lv_obj_set_size(uart_context.keyboard, lv_pct(100), lv_pct(90));
                lv_keyboard_set_popovers(uart_context.keyboard, true); // 按下虚拟键盘按键时，在按键上方显示一个放大的按键提示框
                lv_obj_align(uart_context.keyboard, LV_ALIGN_BOTTOM_MID, 0, 5);
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
        uart_service_port_open((const uart_service_config_t *)&s_uart_config); // 初始化并且使能UART，配置接收参数
    }
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
        char* output_text = lv_mem_alloc(UART_APP_TX_BUFFER_SIZE);
        const char* input_text = lv_textarea_get_text(uart_context.send_area);
        if ((input_text == NULL) || (input_text[0] == '\0'))
        {
            return;
        }
        if (checked)
        {
            s_send_format = UI_UART_FORMAT_HEX; // 发送16进制数
            lv_textarea_set_accepted_chars(uart_context.send_area, "0123456789abcdefABCDEF "); // 限制textarea输入字符，仅允许0-9,a-z,A-Z
            bytes_to_hex_string(input_text, output_text, UART_APP_TX_BUFFER_SIZE);
        }
        else
        {
            s_send_format = UI_UART_FORMAT_TEXT; // 发送文本
            lv_textarea_set_accepted_chars(uart_context.send_area, NULL); // 取消textarea输入字符限制
            hex_string_to_bytes(input_text, output_text, UART_APP_TX_BUFFER_SIZE);
        }
        lv_textarea_set_text(uart_context.send_area, output_text); // 更新发送窗口的值
        lv_mem_free(output_text);
    }
    else if (target == uart_context.ctrl_panel_timestamp_checkbox) // 时间戳
    {
        // @todo
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
            s_uart_connect_status = UI_UART_CONNECTED;
            lv_obj_set_style_bg_color(uart_context.ctrl_panel_open_com_btn, lv_palette_main(LV_PALETTE_RED), 0);
            lv_label_set_text(uart_context.ctrl_panel_open_com_label, "关闭串口");
            uart_context.uart_rx_timer = lv_timer_create(uart_rx_timer_cb, 50, NULL);
            uart_service_port_open(&s_uart_config); // 初始化并且使能UART，配置接收参数
        }
        else
        {
            uart_service_port_close(); // 关闭UART
            s_uart_connect_status = UI_UART_DISCONNECTED;
            lv_obj_set_style_bg_color(uart_context.ctrl_panel_open_com_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
            lv_label_set_text(uart_context.ctrl_panel_open_com_label, "打开串口");
            lv_timer_del(uart_context.uart_rx_timer);
        }
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
    if (btn == uart_context.title_bar_close_btn)
    {
        uart_service_port_close(); // 关闭UART
        s_uart_connect_status = UI_UART_DISCONNECTED;
        // lv_timer_del(uart_context.uart_rx_timer); // 关闭窗口时删除定时器会导致页面卡死
        lv_obj_del(uart_context.container);
    }
    if (btn == uart_context.title_bar_minimize_btn)
    {
        // @todo
    }
}

static void ui_uart_terminal_create_container(lv_obj_t* parent)
{
    uart_context.container = lv_obj_create(parent);
    lv_obj_set_size(uart_context.container, lv_pct(100), lv_pct(92));
    lv_obj_set_style_border_width(uart_context.container, 0, 0);
    lv_obj_set_style_pad_all(uart_context.container, 0, 0);
    lv_obj_set_y(uart_context.container, lv_pct(8));
}

static void ui_uart_terminal_create_title_bar(void)
{
    lv_obj_t *label;

    uart_context.title_bar = lv_obj_create(uart_context.container);
    lv_obj_set_style_pad_gap(uart_context.title_bar, 0, 0);   // 去掉容器与子项的外边距
    //lv_obj_set_style_pad_all(uart_context.title_bar, 0, 0); // 一次性设置top/bottom/left/right四个方向为内边距为0
    lv_obj_set_style_pad_right(uart_context.title_bar, 0, 0);
    lv_obj_set_style_pad_left(uart_context.title_bar, 25, 0);
    lv_obj_clear_flag(uart_context.title_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(uart_context.title_bar, 0, 0);  // 设置圆角为0，变成矩形
    static const lv_coord_t top_grid_cols[] = { LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t top_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(uart_context.title_bar, top_grid_cols, top_grid_rows);
    lv_obj_set_style_grid_row_align(uart_context.title_bar, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间

    uart_context.title_bar_title_label = lv_label_create(uart_context.title_bar);
    lv_obj_set_style_text_font(uart_context.title_bar_title_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(uart_context.title_bar_title_label, lv_palette_main(LV_PALETTE_PINK), 0);
    lv_label_set_text(uart_context.title_bar_title_label, "串口调试助手");

    uart_context.title_bar_minimize_btn = lv_btn_create(uart_context.title_bar);
    lv_obj_set_style_bg_opa(uart_context.title_bar_minimize_btn, LV_OPA_TRANSP, LV_PART_MAIN);      // 背景透明
    lv_obj_set_style_border_opa(uart_context.title_bar_minimize_btn, LV_OPA_TRANSP, LV_PART_MAIN);  // 边框透明
    lv_obj_set_style_shadow_opa(uart_context.title_bar_minimize_btn, LV_OPA_TRANSP, LV_PART_MAIN);  // 阴影透明
    lv_obj_set_style_radius(uart_context.title_bar_minimize_btn, 0, LV_PART_MAIN);                  // 直角
    lv_obj_set_style_bg_opa(uart_context.title_bar_minimize_btn, LV_OPA_COVER, LV_STATE_PRESSED);   // 设置按下状态：灰色背景
    lv_obj_set_style_bg_color(uart_context.title_bar_minimize_btn, lv_color_hex(0xAAAAAA), LV_STATE_PRESSED);
    label = lv_label_create(uart_context.title_bar_minimize_btn);
    lv_obj_set_style_text_color(label, lv_color_hex(0), 0); // 黑色字体颜色
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_label_set_text(label, LV_SYMBOL_MINUS);
    lv_obj_center(label);

    uart_context.title_bar_close_btn = lv_btn_create(uart_context.title_bar);
    lv_obj_set_style_bg_opa(uart_context.title_bar_close_btn, LV_OPA_TRANSP, LV_PART_MAIN);      // 背景透明
    lv_obj_set_style_border_opa(uart_context.title_bar_close_btn, LV_OPA_TRANSP, LV_PART_MAIN);  // 边框透明
    lv_obj_set_style_shadow_opa(uart_context.title_bar_close_btn, LV_OPA_TRANSP, LV_PART_MAIN);  // 阴影透明
    lv_obj_set_style_radius(uart_context.title_bar_close_btn, 0, LV_PART_MAIN);                  // 直角
    lv_obj_set_style_bg_opa(uart_context.title_bar_close_btn, LV_OPA_COVER, LV_STATE_PRESSED);   // 设置按下状态：红色背景
    lv_obj_set_style_bg_color(uart_context.title_bar_close_btn, lv_color_hex(0xFF0000), LV_STATE_PRESSED);
    lv_obj_add_event_cb(uart_context.title_bar_close_btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(uart_context.title_bar_close_btn);
    lv_obj_set_style_text_color(label, lv_color_hex(0), 0); // 黑色字体颜色
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_label_set_text(label, LV_SYMBOL_CLOSE);
    lv_obj_center(label);

    lv_obj_set_grid_cell(uart_context.title_bar_title_label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(uart_context.title_bar_minimize_btn, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_grid_cell(uart_context.title_bar_close_btn, LV_GRID_ALIGN_END, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
}

static void ui_uart_terminal_create_control_panel(void)
{
    lv_obj_t *label;

    uart_context.control_panel = lv_obj_create(uart_context.container);
    lv_obj_clear_flag(uart_context.control_panel, LV_OBJ_FLAG_SCROLLABLE);
    //lv_obj_remove_style_all(uart_context.control_panel);
    //lv_obj_set_height(uart_context.control_panel, LV_SIZE_CONTENT);
    //lv_obj_set_style_pad_bottom(uart_context.control_panel, 8, 0);
    static const lv_coord_t ctrl_grid_col[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t ctrl_grid_row[] = { LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST }; // LV_GRID_FR(1)作为弹性占位
    //static const lv_coord_t ctrl_grid_row[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(uart_context.control_panel, ctrl_grid_col, ctrl_grid_row);
    lv_obj_set_style_grid_row_align(uart_context.control_panel, LV_GRID_ALIGN_CENTER, 0);
    lv_obj_set_style_grid_column_align(uart_context.control_panel, LV_GRID_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_set_scrollbar_mode(uart_context.control_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(uart_context.control_panel, 10, 0);

    label = lv_label_create(uart_context.control_panel);
    lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(label, "串口选择");
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    uart_context.ctrl_panel_com_dropdown = lv_dropdown_create(uart_context.control_panel);
    lv_dropdown_set_options(uart_context.ctrl_panel_com_dropdown, "COM1\nCOM2");
    lv_dropdown_set_dir(uart_context.ctrl_panel_com_dropdown, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(uart_context.ctrl_panel_com_dropdown, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected(uart_context.ctrl_panel_com_dropdown, 0); // 默认显示的选项
    lv_obj_set_height(uart_context.ctrl_panel_com_dropdown, 31);
    lv_obj_set_grid_cell(uart_context.ctrl_panel_com_dropdown, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    label = lv_label_create(uart_context.control_panel);
    lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(label, "波特率");
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 1, 1);

    uart_context.ctrl_panel_baudrate_dropdown = lv_dropdown_create(uart_context.control_panel);
    lv_dropdown_set_options(uart_context.ctrl_panel_baudrate_dropdown, "4800\n9600\n19200\n38400\n115200\n128000\n230400\n460800\n921600");
    lv_dropdown_set_dir(uart_context.ctrl_panel_baudrate_dropdown, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(uart_context.ctrl_panel_baudrate_dropdown, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected(uart_context.ctrl_panel_baudrate_dropdown, 4); // 默认显示的选项
    lv_obj_add_event_cb(uart_context.ctrl_panel_baudrate_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_height(uart_context.ctrl_panel_baudrate_dropdown, 31);
    lv_obj_set_grid_cell(uart_context.ctrl_panel_baudrate_dropdown, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);

    label = lv_label_create(uart_context.control_panel);
    lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(label, "停止位");
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 2, 1);

    uart_context.ctrl_panel_stopbit_dropdown = lv_dropdown_create(uart_context.control_panel);
    lv_dropdown_set_options(uart_context.ctrl_panel_stopbit_dropdown, "1\n2\n0.5\n1.5");
    lv_dropdown_set_dir(uart_context.ctrl_panel_stopbit_dropdown, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(uart_context.ctrl_panel_stopbit_dropdown, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected(uart_context.ctrl_panel_stopbit_dropdown, 0); // 默认显示的选项
    lv_obj_add_event_cb(uart_context.ctrl_panel_stopbit_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_height(uart_context.ctrl_panel_stopbit_dropdown, 31);
    lv_obj_set_grid_cell(uart_context.ctrl_panel_stopbit_dropdown, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 2, 1);

    label = lv_label_create(uart_context.control_panel);
    lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(label, "数据位");
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 3, 1);

    uart_context.ctrl_panel_databit_dropdown = lv_dropdown_create(uart_context.control_panel);
    lv_dropdown_set_options(uart_context.ctrl_panel_databit_dropdown, "7 (+parity)\n8 (+parity)\n9 (+parity)");
    lv_dropdown_set_dir(uart_context.ctrl_panel_databit_dropdown, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(uart_context.ctrl_panel_databit_dropdown, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected(uart_context.ctrl_panel_databit_dropdown, 1); // 默认显示的选项
    lv_obj_add_event_cb(uart_context.ctrl_panel_databit_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_height(uart_context.ctrl_panel_databit_dropdown, 31);
    lv_obj_set_grid_cell(uart_context.ctrl_panel_databit_dropdown, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 3, 1);

    label = lv_label_create(uart_context.control_panel);
    lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_label_set_text(label, "校验位");
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 4, 1);

    uart_context.ctrl_panel_parity_dropdown = lv_dropdown_create(uart_context.control_panel);
    lv_dropdown_set_options(uart_context.ctrl_panel_parity_dropdown, "None\nOdd\nEven");
    lv_dropdown_set_dir(uart_context.ctrl_panel_parity_dropdown, LV_DIR_BOTTOM);
    lv_dropdown_set_symbol(uart_context.ctrl_panel_parity_dropdown, LV_SYMBOL_DOWN);
    lv_dropdown_set_selected(uart_context.ctrl_panel_parity_dropdown, 0); // 默认显示的选项
    lv_obj_add_event_cb(uart_context.ctrl_panel_parity_dropdown, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_height(uart_context.ctrl_panel_parity_dropdown, 31);
    lv_obj_set_grid_cell(uart_context.ctrl_panel_parity_dropdown, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 4, 1);

    uart_context.ctrl_panel_open_com_btn = lv_btn_create(uart_context.control_panel);
    lv_obj_set_style_bg_color(uart_context.ctrl_panel_open_com_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(uart_context.ctrl_panel_open_com_btn, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_set_height(uart_context.ctrl_panel_open_com_btn, 40);
    lv_obj_add_event_cb(uart_context.ctrl_panel_open_com_btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    uart_context.ctrl_panel_open_com_label = lv_label_create(uart_context.ctrl_panel_open_com_btn);
    lv_obj_set_style_text_font(uart_context.ctrl_panel_open_com_label, &ui_font_chinese_24_4, 0);
    lv_label_set_text(uart_context.ctrl_panel_open_com_label, "打开串口");
    lv_obj_center(uart_context.ctrl_panel_open_com_label);
    lv_obj_set_grid_cell(uart_context.ctrl_panel_open_com_btn, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 6, 1);

    uart_context.ctrl_panel_clr_rx_btn = lv_btn_create(uart_context.control_panel);
    lv_obj_set_style_bg_color(uart_context.ctrl_panel_clr_rx_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(uart_context.ctrl_panel_clr_rx_btn, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_set_height(uart_context.ctrl_panel_clr_rx_btn, 40);
    lv_obj_add_event_cb(uart_context.ctrl_panel_clr_rx_btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(uart_context.ctrl_panel_clr_rx_btn);
    lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
    lv_label_set_text(label, "清除接收");
    lv_obj_center(label);
    lv_obj_set_grid_cell(uart_context.ctrl_panel_clr_rx_btn, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 6, 1);

    uart_context.ctrl_panel_tx_btn = lv_btn_create(uart_context.control_panel);
    lv_obj_set_style_bg_color(uart_context.ctrl_panel_tx_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(uart_context.ctrl_panel_tx_btn, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_set_height(uart_context.ctrl_panel_tx_btn, 40);
    lv_obj_add_event_cb(uart_context.ctrl_panel_tx_btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(uart_context.ctrl_panel_tx_btn);
    lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
    lv_label_set_text(label, "发送");
    lv_obj_center(label);
    lv_obj_set_grid_cell(uart_context.ctrl_panel_tx_btn, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 7, 1);

    uart_context.ctrl_panel_clr_tx_btn = lv_btn_create(uart_context.control_panel);
    lv_obj_set_style_bg_color(uart_context.ctrl_panel_clr_tx_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(uart_context.ctrl_panel_clr_tx_btn, LV_OPA_100, LV_STATE_DEFAULT);
    lv_obj_set_height(uart_context.ctrl_panel_clr_tx_btn, 40);
    lv_obj_add_event_cb(uart_context.ctrl_panel_clr_tx_btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(uart_context.ctrl_panel_clr_tx_btn);
    lv_obj_set_style_text_font(label, &ui_font_chinese_24_4, 0);
    lv_label_set_text(label, "清除发送");
    lv_obj_center(label);
    lv_obj_set_grid_cell(uart_context.ctrl_panel_clr_tx_btn, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 7, 1);

    uart_context.ctrl_panel_hex_disp_checkbox = lv_checkbox_create(uart_context.control_panel); /* 创建复选框 */
    lv_obj_set_style_text_font(uart_context.ctrl_panel_hex_disp_checkbox, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(uart_context.ctrl_panel_hex_disp_checkbox, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_add_event_cb(uart_context.ctrl_panel_hex_disp_checkbox, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_checkbox_set_text(uart_context.ctrl_panel_hex_disp_checkbox, "16进制显示");
    lv_obj_set_grid_cell(uart_context.ctrl_panel_hex_disp_checkbox, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 9, 1);

    uart_context.ctrl_panel_timestamp_checkbox = lv_checkbox_create(uart_context.control_panel); /* 创建复选框 */
    lv_obj_set_style_text_font(uart_context.ctrl_panel_timestamp_checkbox, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(uart_context.ctrl_panel_timestamp_checkbox, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_checkbox_set_text(uart_context.ctrl_panel_timestamp_checkbox, "时间戳");
    lv_obj_set_grid_cell(uart_context.ctrl_panel_timestamp_checkbox, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 9, 1);
    lv_obj_set_style_pad_left(uart_context.ctrl_panel_timestamp_checkbox, 10, 0);   // 向右偏移 10 像素
}

static void ui_uart_terminal_create_receive_area(void)
{
    uart_context.receive_area = lv_textarea_create(uart_context.container);

    // 禁止textarea控件光标显示，降低cpu占用率
    lv_obj_clear_state(uart_context.receive_area, LV_STATE_FOCUSED);  // 手动清掉焦点
    lv_obj_clear_flag(uart_context.receive_area, LV_OBJ_FLAG_CLICK_FOCUSABLE); // 点击时不会获得焦点
    //lv_obj_add_style(uart_context.receive_area, &style_uart, 0);
    lv_obj_set_size(uart_context.receive_area, 200/*LV_HOR_RES - 100*/, LV_VER_RES - 100);
    lv_label_set_recolor(lv_textarea_get_label(uart_context.receive_area), true); // 启用内嵌颜色解析
    lv_obj_set_style_text_font(uart_context.receive_area, &ui_font_chinese_32_4, LV_PART_MAIN);
}

static void ui_uart_terminal_create_send_area(void)
{
    uart_context.send_area = lv_textarea_create(uart_context.container);

    lv_textarea_set_placeholder_text(uart_context.send_area, "Please enter text...");
    lv_obj_set_style_text_font(uart_context.send_area, &ui_font_chinese_24_4, 0);
    //lv_obj_remove_state(uart_context.send_area, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(uart_context.send_area, send_textarea_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(uart_context.send_area, send_textarea_event_cb, LV_EVENT_CANCEL, NULL);
}

static void ui_uart_terminal_create_bottom_status_bar(void)
{
    uart_context.bottom_status_bar = lv_obj_create(uart_context.container);
    lv_obj_clear_flag(uart_context.bottom_status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(uart_context.bottom_status_bar, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(uart_context.bottom_status_bar, LV_FLEX_FLOW_ROW); // 创建flow
    lv_obj_set_flex_align(uart_context.bottom_status_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    uart_context.bottom_hex_send_checkbox = lv_checkbox_create(uart_context.bottom_status_bar);
    lv_obj_set_style_text_font(uart_context.bottom_hex_send_checkbox, &ui_font_chinese_24_4, 0);
    lv_checkbox_set_text(uart_context.bottom_hex_send_checkbox, "16进制发送");
    lv_obj_set_style_text_color(uart_context.bottom_hex_send_checkbox, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(uart_context.bottom_hex_send_checkbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(uart_context.bottom_hex_send_checkbox, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    uart_context.bottom_send_new_line_checkbox = lv_checkbox_create(uart_context.bottom_status_bar);
    lv_obj_set_style_text_font(uart_context.bottom_send_new_line_checkbox, &ui_font_chinese_24_4, 0);
    lv_checkbox_set_text(uart_context.bottom_send_new_line_checkbox, "发送新行");
    lv_obj_set_style_text_color(uart_context.bottom_send_new_line_checkbox, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(uart_context.bottom_send_new_line_checkbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(uart_context.bottom_send_new_line_checkbox, checkbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    uart_context.bottom_tx_label = lv_label_create(uart_context.bottom_status_bar);
    lv_obj_set_style_text_font(uart_context.bottom_tx_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(uart_context.bottom_tx_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_width(uart_context.bottom_tx_label, 90);
    lv_label_set_text(uart_context.bottom_tx_label, "S:0");

    uart_context.bottom_rx_label = lv_label_create(uart_context.bottom_status_bar);
    lv_obj_set_style_text_font(uart_context.bottom_rx_label, &ui_font_chinese_24_4, 0);
    lv_obj_set_style_text_color(uart_context.bottom_rx_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_width(uart_context.bottom_rx_label, 90);
    lv_label_set_text(uart_context.bottom_rx_label, "R:0");
}

static void ui_uart_terminal_configure_layout(void)
{
    lv_obj_set_style_pad_gap(uart_context.container, 0, 0); // 去掉容器与子项的外边距
    static const lv_coord_t console_grid_cols[] = { LV_GRID_FR(10), LV_GRID_FR(3), LV_GRID_FR(3), LV_GRID_TEMPLATE_LAST };
    static const lv_coord_t console_grid_rows[] = { LV_GRID_FR(10), LV_GRID_FR(62), LV_GRID_FR(17), LV_GRID_FR(11), LV_GRID_TEMPLATE_LAST };
    //static const lv_coord_t console_grid_rows[] = { LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST };
    lv_obj_set_grid_dsc_array(uart_context.container, console_grid_cols, console_grid_rows);
    lv_obj_set_style_grid_row_align(uart_context.container, LV_GRID_ALIGN_SPACE_BETWEEN, 0); // 均匀分布，首尾元素贴边，剩余空间在元素之间
    lv_obj_set_grid_cell(uart_context.title_bar, LV_GRID_ALIGN_STRETCH, 0, 3, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(uart_context.send_area, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_set_grid_cell(uart_context.receive_area, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(uart_context.control_panel, LV_GRID_ALIGN_STRETCH, 1, 2, LV_GRID_ALIGN_STRETCH, 1, 3);
    lv_obj_set_grid_cell(uart_context.bottom_status_bar, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 3, 1);
}

void ui_uart_terminal_create(lv_obj_t* parent)
{
    ui_uart_terminal_reset_state();
    ui_uart_terminal_create_container(parent);
    ui_uart_terminal_create_title_bar();
    ui_uart_terminal_create_control_panel();
    ui_uart_terminal_create_bottom_status_bar();
    ui_uart_terminal_create_receive_area();
    ui_uart_terminal_create_send_area();
    ui_uart_terminal_configure_layout();
    uart_service_init_rx_ring_buffer(); // 初始化串口接收使用的环形缓冲区
}

