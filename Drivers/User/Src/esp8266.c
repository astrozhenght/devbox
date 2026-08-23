#include <stdio.h>
#include <string.h>
#include "esp8266.h"
#include "usart.h"
#include "comm_queue.h"
#include "cJSON.h"
#include "FreeRTOS.h"
#include "task.h"

Comm_Queue uart6_queue;                            // 已经添加临界区保护
uint8_t g_esp_uart_recv_buff[ESP_UART_RECV_SIZE];  // 无中断调用，不需要临界区保护
uint8_t g_esp_uart_recv_complete_flag = 0;         // 已经放入临界区保护


// 通过UART给ESP8266发送AT指令
// 返回值0：成功，给ESP8266发送指令成功，并且接收到ESP8266回复的“OK”
//         有效数据已经存到g_uart_textarea_buffer数组当中
// 返回值1：失败，给ESP8266发送指令失败，500ms内未发送完成
// 返回值2：失败，接收ESP8266回复的数据，接收到ESP8266回复的“ERROR/FAIL”
// 返回值3：失败，接收ESP8266回复的数据超时
// 注意：ESP8266回复的数据个数不应该超过环形缓冲区的长度
uint8_t esp_send_at_cmd(UART_HandleTypeDef *huart, const uint8_t *p_data, uint16_t timeout_ms)
{
    // g_esp_uart_recv_complete_flag = 0;
    // HAL_UARTEx_ReceiveToIdle_DMA(huart, g_esp_uart_recv_buff, ESP_UART_RECV_SIZE); // 重新初始化接收参数

    // 发送指令给ESP8266
    if (HAL_OK != HAL_UART_Transmit_DMA(huart, p_data, strlen((const char*)p_data)))
    {
        return 1;
    }
    // 等待指令发送完成，若500ms内未发送完成，则强制退出
    uint8_t times = 0;
    while ((huart->gState != HAL_UART_STATE_READY) && (times < 100))
    {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (times >= 100)
    {
        return 1;
    }

    // 等待ESP8266回复数据
    // 超时处理：若在规定时间内未接收到ESP8266回复的“OK/ERROR/FAIL”，则强制退出
    for (uint8_t i = 0; i < 10; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(timeout_ms / 10)); // 单位ms
        if (g_esp_uart_recv_complete_flag == 1)
        {
            // 获取ESP8266回复的完整数据，方式是从环形缓冲区中取数据放入指定buffer中
            __disable_irq();
            uint32_t len = Comm_Queue_Memcpy(&uart6_queue, g_esp_uart_recv_buff);
            g_esp_uart_recv_buff[len] = '\0'; // 添加字符串结束符
            g_esp_uart_recv_complete_flag = 0;
            __enable_irq();
            
            // 判断是否接收到OK/ERROR/FAIL
            if (strstr((const char *)g_esp_uart_recv_buff, "OK") != NULL) // 接收到OK
            {
                Comm_Queue_Clear(&uart6_queue); // 清空环形缓冲区数据
                return 0;
            }
            if ((strstr((const char *)g_esp_uart_recv_buff, "ERROR") != NULL) ||
                (strstr((const char *)g_esp_uart_recv_buff, "FAIL")  != NULL)) // 接收到ERROR/FAIL
            {
                Comm_Queue_Clear(&uart6_queue); // 清空环形缓冲区数据
                return 2;
            }
        }
    }
    Comm_Queue_Clear(&uart6_queue); // 清空环形缓冲区数据
    return 3;
}

// 向心知天气发送获取天气数据的请求
// 返回值0：成功，给ESP8266发送指令成功，并且接收到ESP8266回复的“last_update”
//    有效数据已经存到g_uart_textarea_buffer数组当中
// 返回值1：失败，给ESP8266发送指令失败，500ms内未发送完成
// 返回值3：失败，接收ESP8266回复的数据超时
// 注意：ESP8266回复的数据个数不应该超过环形缓冲区的长度
uint8_t esp_send_req(UART_HandleTypeDef *huart, const uint8_t *p_data, uint16_t timeout_ms)
{
    // g_esp_uart_recv_complete_flag = 0;
    // HAL_UARTEx_ReceiveToIdle_DMA(&huart1, g_esp_uart_recv_buff, ESP_UART_RECV_SIZE); // 重新初始化接收参数

    // 发送指令给ESP8266
    if (HAL_OK != HAL_UART_Transmit_DMA(huart, p_data, strlen((const char*)p_data)))
    {
        return 1;
    }
    // 等待指令发送完成，若500ms内未发送完成，则强制退出
    uint8_t times = 0;
    while ((huart->gState != HAL_UART_STATE_READY) && (times < 100))
    {
        vTaskDelay(pdMS_TO_TICKS(5)); // 单位ms
    }
    if (times >= 100)
    {
        return 1;
    }

    // 等待ESP8266回复数据
    // 超时处理：若在规定时间内未接收到ESP8266回复的“last_update”，则强制退出
    for (uint8_t i = 0; i < 10; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(timeout_ms / 10)); // 单位ms
        if (g_esp_uart_recv_complete_flag == 1)
        {
            // 获取ESP8266回复的完整数据，方式是从环形缓冲区中取数据放入指定buffer中
            __disable_irq();
            uint32_t len = Comm_Queue_Memcpy(&uart6_queue, g_esp_uart_recv_buff);
            g_esp_uart_recv_buff[len] = '\0'; // 添加字符串结束符
            g_esp_uart_recv_complete_flag = 0;
            __enable_irq();

            // 判断是否接收到last_update
            if (strstr((const char *)g_esp_uart_recv_buff, "last_update") != NULL) // 接收到OK
            {
                Comm_Queue_Clear(&uart6_queue); // 清空环形缓冲区数据
                return 0;
            }
        }
    }
    Comm_Queue_Clear(&uart6_queue); // 清空环形缓冲区数据
    return 3;
}

// 退出透传模式
// 当输入单独一包 +++ 时，返回普通AT指令模式。
// 发送 +++ 退出透传时，至少间隔 1 秒再发下一条 AT 指令。
// +++指令必须在开启透传模式以及单连接下使用
// 返回值0：成功，给ESP8266发送指令成功
// 返回值1：失败，给ESP8266发送指令失败，500ms内未发送完成
uint8_t esp_exit_pass_through(UART_HandleTypeDef *huart)
{
    // g_esp_uart_recv_complete_flag = 0;
    // HAL_UARTEx_ReceiveToIdle_DMA(huart, g_esp_uart_recv_buff, ESP_UART_RECV_SIZE); // 重新初始化接收参数

    // 发送指令给ESP8266
    if (HAL_OK != HAL_UART_Transmit_DMA(huart, (const uint8_t *)"+++", 3))
    {
        return 1;
    }

    // 等待指令发送完成，若500ms内未发送完成，则强制退出
    uint8_t times = 0;
    while ((huart->gState != HAL_UART_STATE_READY) && (times < 100))
    {
        vTaskDelay(pdMS_TO_TICKS(5)); // 单位ms
    }
    if (times >= 100)
    {
        return 1;
    }

    vTaskDelay(pdMS_TO_TICKS(1200)); // 发送+++后至少间隔1秒再发下一条AT指令
    return 0;
}

// 返回值0：获取天气信息成功
// 返回值1：获取天气信息失败
uint8_t weather_get_info(WeatherInfo_t *info, uint8_t *buff)
{
    // 解析整段JSO数据
    cJSON *cjson_test = cJSON_Parse((const char *)buff);
    if (cjson_test == NULL) // 解析失败
    {
        return 1;
    }

    // results是数组
    cJSON *results = cJSON_GetObjectItem(cjson_test, "results");
    if ((results == NULL) || !cJSON_IsArray(results)) // results error
    {
        cJSON_Delete(cjson_test);
        return 1;
    }

    // 取results[0]
    cJSON *result0 = cJSON_GetArrayItem(results, 0);
    if (result0 == NULL) // results[0] error
    {
        cJSON_Delete(cjson_test);
        return 1;
    }

    // 解析 location 对象
    // cJSON *location = cJSON_GetObjectItem(result0, "location");
    // cJSON *id = cJSON_GetObjectItem(location, "id");
    // cJSON *name = cJSON_GetObjectItem(location, "name");
    // cJSON *country = cJSON_GetObjectItem(location, "country");
    // cJSON *path = cJSON_GetObjectItem(location, "path");
    // printf("Location:\n");
    // printf("  id: %s\n", id->valuestring);
    // printf("  name: %s\n", name->valuestring);
    // printf("  country: %s\n", country->valuestring);
    // printf("  path: %s\n", path->valuestring);

    // 解析 now 对象
    cJSON *now  = cJSON_GetObjectItem(result0, "now");
    cJSON *text = cJSON_GetObjectItem(now, "text");
    cJSON *code = cJSON_GetObjectItem(now, "code");
    cJSON *temp = cJSON_GetObjectItem(now, "temperature");
    // printf("Now:\n");
    // printf("  text: %s\n", text->valuestring);
    // printf("  code: %s\n", code->valuestring);
    // printf("  temperature: %s\n", temp->valuestring);

    // 复制数据到info结构体变量中，包含'\0'
    uint32_t len_text = strlen(text->valuestring) + 1;
    uint32_t len_temp = strlen(temp->valuestring) + 1;
    uint32_t len_code = strlen(code->valuestring) + 1;
    if ((len_text > sizeof(info->text)) || 
        (len_temp > sizeof(info->temp)) ||
        (len_code > sizeof(info->code)))
    {
        return 1;
    }
    memcpy((void *)(info->text), (const void *)(text->valuestring), len_text);
    memcpy((void *)(info->temp), (const void *)(temp->valuestring), len_temp);
    memcpy((void *)(info->code), (const void *)(code->valuestring), len_code);

    // last_update
    cJSON *last_update = cJSON_GetObjectItem(result0, "last_update");

    // 释放内存
    cJSON_Delete(cjson_test);
    return 0;
}

