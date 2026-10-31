#include "wifi_task.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "bsp_wifi.h"
#include "wifi_credentials.h"
#include "wifi_service_port.h"

#define WIFI_TASK_PRIORITY          2U      // 任务优先级
#define WIFI_TASK_STACK_DEPTH       1280U   // 堆栈大小
#define WIFI_TASK_STARTUP_DELAY_MS  500U    // 保留充足的 ESP8266 上电稳定时间
#define WIFI_TASK_IDLE_DELAY_MS     1000U

static TaskHandle_t wifi_task_handle;

static void wifi_task_entry(void *pvParameters)
{
    (void)pvParameters;
    uint8_t wifi_command;
    wifi_service_message_t wifi_message = {0};
    uint8_t *wifi_rx_buffer = bsp_wifi_get_rx_buffer();

    vTaskDelay(pdMS_TO_TICKS(WIFI_TASK_STARTUP_DELAY_MS));
    (void)bsp_wifi_start_receive();

    for (;;)
    {
        if (wifi_service_dequeue_command(&wifi_command))
        {
            wifi_message.status = WIFI_SERVICE_STATUS_OK;
            wifi_message.command = (wifi_service_command_t)wifi_command;

            switch (wifi_message.command)
            {
                case WIFI_SERVICE_CMD_CONNECT:
                    if ((bsp_wifi_execute_at_command((const uint8_t *)"AT\r\n", 100) != BSP_WIFI_OK) ||
                        (bsp_wifi_execute_at_command((const uint8_t *)"AT+CWMODE=1\r\n", 100) != BSP_WIFI_OK) ||
                        (bsp_wifi_execute_at_command((const uint8_t *)"AT+CWJAP=\"" APP_WIFI_SSID "\",\"" APP_WIFI_PASSWORD "\"\r\n", 10000) != BSP_WIFI_OK))
                    {
                        wifi_message.status = WIFI_SERVICE_STATUS_ERROR;
                    }
                    break;

                case WIFI_SERVICE_CMD_GET_WEATHER:
                {
                    // 获取天气信息
                    if ((bsp_wifi_execute_at_command((const uint8_t *)"AT+CIPSTART=\"TCP\",\"api.seniverse.com\",80\r\n", 500) != BSP_WIFI_OK) ||
                        (bsp_wifi_execute_at_command((const uint8_t *)"AT+CIPMODE=1\r\n", 100) != BSP_WIFI_OK) ||
                        (bsp_wifi_execute_at_command((const uint8_t *)"AT+CIPSEND\r\n", 100) != BSP_WIFI_OK) ||
                        !weather_service_request_update((const uint8_t *)"GET https://api.seniverse.com/v3/weather/now.json?key=" APP_WEATHER_API_KEY "&location=jiangsu suzhou&language=zh-Hans&unit=c\r\n"))
                    {
                        wifi_message.status = WIFI_SERVICE_STATUS_ERROR;
                        break;
                    }

                    // 把天气数据缓存下来
                    uint32_t weather_length = strlen((const char *)wifi_rx_buffer) + 1U;
                    uint8_t *weather_response = malloc(weather_length);
                    if (weather_response == NULL)
                    {
                        wifi_message.status = WIFI_SERVICE_STATUS_NO_MEMORY;
                        break;
                    }

                    memcpy((void *)weather_response, (const void *)wifi_rx_buffer, weather_length);
                    if ((bsp_wifi_exit_passthrough() != BSP_WIFI_OK) || // 退出透传模式失败
                        (bsp_wifi_execute_at_command((const uint8_t *)"AT+CIPCLOSE\r\n", 10) != BSP_WIFI_OK)) // 关闭tcp连接失败
                    {
                        free(weather_response);
                        wifi_message.status = WIFI_SERVICE_STATUS_ERROR;
                        break;
                    }

                    // 有效天气数据在本任务中提取，提取后发送给主任务
                    if (!weather_service_parse_response(
                            (const char *)weather_response,
                            &wifi_message.data.weather))
                    {
                        wifi_message.status = WIFI_SERVICE_STATUS_WEATHER_PARSE_ERROR;
                    }
                    free(weather_response);
                    break;
                }

                case WIFI_SERVICE_CMD_GET_SNTPTIME:
                    //AT+CIPSNTPCFG=1,8 //设置时区，1ms接收完成
                    //AT+CIPSNTPTIME?   //查询时间，5ms接收完成
                    if (bsp_wifi_execute_at_command(
                            (const uint8_t *)"AT+CIPSNTPCFG=1,8\r\n",
                            10) != BSP_WIFI_OK)
                    {
                        wifi_message.status = WIFI_SERVICE_STATUS_ERROR;
                        break;
                    }

                    for (uint8_t i = 0; i < 10U; i++)
                    {
                        if (bsp_wifi_execute_at_command(
                                (const uint8_t *)"AT+CIPSNTPTIME?\r\n",
                                30) != BSP_WIFI_OK)
                        {
                            wifi_message.status = WIFI_SERVICE_STATUS_ERROR;
                            break;
                        }
                        // AT+CIPSNTPTIME?
                        // +CIPSNTPTIME:Thu Dec 25 22:08:46 2025
                        // OK
                        // 把Thu Dec 25 22:08:46 2025，包含'\0'的话长度为25，通过队列发送出去
                        wifi_rx_buffer[54] = '\0'; // 手动添加字符串结束符
                        if (strcmp((const char *)(wifi_rx_buffer + 50), "1970") != 0) // SNTP同步完成
                        {
                            memcpy((void *)wifi_message.data.sntp_time,
                                   (const char *)(wifi_rx_buffer + 30),
                                   25);
                            wifi_message.status = WIFI_SERVICE_STATUS_OK;
                            break;
                        }
                        wifi_message.status = WIFI_SERVICE_STATUS_INVALID_SNTP_TIME;
                        // 查询时间太早，SNTP还没同步完成，此时极容易返回 1970
                        vTaskDelay(pdMS_TO_TICKS(1000)); // 延时1秒后重试
                    }
                    break;

                default:
                    wifi_message.command = WIFI_SERVICE_CMD_NONE;
                    break;
            }

            if (wifi_message.command != WIFI_SERVICE_CMD_NONE)
            {
                if (!wifi_service_send_message(&wifi_message))
                {
                    while(1);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_TASK_IDLE_DELAY_MS));
    }
}

bool wifi_task_create(void)
{
    return xTaskCreate(wifi_task_entry,
                       "wifi_service",
                       WIFI_TASK_STACK_DEPTH,
                       NULL,
                       WIFI_TASK_PRIORITY,
                       &wifi_task_handle) == pdPASS;
}
