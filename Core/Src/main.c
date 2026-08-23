#include <stdio.h>
#include <stdlib.h> // 包含 malloc 的声明
#include <string.h>
#include <ctype.h>

#include "main.h"
#include "usart.h"
#include "dma.h"
#include "gpio.h"
#include "sdram.h"
#include "lcd.h"
#include "dma2d.h"
#include "gt911.h"
#include "w25q64.h"
#include "esp8266.h"
#include "sdmmc_sd.h"
#include "ff_gen_drv.h"
#include "sd_diskio_dma.h"
#include "rtc.h"

#include "stm32h7xx_ll_dma2d.h"

#include "lv_port_disp.h"
#include "lv_demos.h"
#include "lv_port_indev.h"
#include "app/app_uart.h"
#include "app/app_main.h"
#include "app/app_calendar.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

void SystemClock_Config(void);
static void MPU_Config(void);

///* 图片数据 */
//extern const uint32_t dog_rgb565[128 * 128 * 2];
//extern const uint32_t dog_argb8888[128 * 128 * 4];
//extern const uint32_t keji_argb8888[128 * 128 * 4];
//extern const uint32_t power_rgb565[128 * 128 * 2];
//extern const uint32_t power_argb8888[128 * 128 * 4];
//extern const uint32_t wife_rgb565[128 * 128 * 2];

/* TASK1 任务配置 */
#define GPIO_TASK1_PRIO         3       // 任务优先级
#define GPIO_TASK1_STACK_SIZE   1280    // 堆栈大小
TaskHandle_t    gpio_task1_handler;     // 任务句柄
void task1( void * pvParameters );      // 任务函数

/* TASK2 任务配置 */
#define GPIO_TASK2_PRIO         2       // 任务优先级
#define GPIO_TASK2_STACK_SIZE   1280    // 堆栈大小
TaskHandle_t    gpio_task2_handler;     // 任务句柄
void task2( void * pvParameters );      // 任务函数

QueueHandle_t wifi_info_queue;
QueueHandle_t wifi_action_queue;


static void prvGPIOTask01( void *pvParameters )
{
    uint8_t wifi_action;

    wifi_action = WIFI_ACTION_CONNECT;
    if (xQueueSend(wifi_action_queue, &wifi_action, QUEUE_SEND_MAX_DELAY) != pdTRUE)
    {
        // printf("队列发送失败\r\n");
        while(1);
    }

    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) != 0X5A69)
    {
        wifi_action = WIFI_ACTION_GET_SNTPTIME;
        if (xQueueSend(wifi_action_queue, &wifi_action, QUEUE_SEND_MAX_DELAY) != pdTRUE)
        {
            // printf("队列发送失败\r\n");
            while(1);
        }
        HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0X5A69);
    }

    wifi_action = WIFI_ACTION_GET_WEATHER;
    if (xQueueSend(wifi_action_queue, &wifi_action, QUEUE_SEND_MAX_DELAY) != pdTRUE)
    {
        // printf("队列发送失败\r\n");
        while(1);
    }

    for(;;)
    {
        // 定期调用 lv_task_handler()，用于处理与 lvgl 相关的任务
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(10)); // 延时10ms
    }
}

// wireless_task
static void prvGPIOTask02( void *pvParameters )
{
    uint8_t wifi_action;
    wifi_info_t wifi_info = {0};

    vTaskDelay(pdMS_TO_TICKS(500)); // ESP8266上电到打印AT固件版本需要220ms左右
    __HAL_UART_ENABLE(&huart6); // 使能UART

    // 初始化串口上位机队列
    Comm_Queue_Init(&uart6_queue);

    // 使能UART接收空闲中断DMA模式
    HAL_StatusTypeDef ret;
    ret = HAL_UARTEx_ReceiveToIdle_DMA(&huart6, g_esp_uart_recv_buff, ESP_UART_RECV_SIZE);
    if (ret == HAL_ERROR)
    {
        Error_Handler();
    }
    else if (ret == HAL_BUSY)
    {
        Error_Handler();
    }

    // HAL_UART_AbortReceive(&huart6); // 停止UART接收
    while(1)
    {
        if (xQueueReceive(wifi_action_queue, &wifi_action, 0) == pdPASS)
        {
            switch (wifi_action)
            {
                case WIFI_ACTION_CONNECT:
                    wifi_info.action_val = WIFI_ACTION_CONNECT;
                    wifi_info.status = WIFI_STATUS_OK;
                    if (esp_send_at_cmd(&huart6, (const uint8_t *)"AT\r\n", 100) || // 测试AT指令
                        esp_send_at_cmd(&huart6, (const uint8_t *)"AT+CWMODE=1\r\n", 100) || // 设置当前Wi-Fi模式为Station模式
                        esp_send_at_cmd(&huart6, (const uint8_t *)"AT+CWJAP=\"TP-LINK_70B9\",\"qwertyuiop\"\r\n", 10000)) // 连接wifi，预计4s连接成功
                    {
                        wifi_info.status = WIFI_STATUS_ERROR;
                    }
                    break;
                case WIFI_ACTION_GET_WEATHER:
                    wifi_info.action_val = WIFI_ACTION_GET_WEATHER;
                    wifi_info.status = WIFI_STATUS_OK;
                    // 获取天气信息
                    if (esp_send_at_cmd(&huart6, (const uint8_t *)"AT+CIPSTART=\"TCP\",\"api.seniverse.com\",80\r\n", 500) || // 连接心知服务器，预计50ms连接成功
                        esp_send_at_cmd(&huart6, (const uint8_t *)"AT+CIPMODE=1\r\n", 100) || // 使能esp8266透传模式，透传模式自动重连tcp
                        esp_send_at_cmd(&huart6, (const uint8_t *)"AT+CIPSEND\r\n", 100)   || // 开启数据传输
                        esp_send_req(&huart6, (const uint8_t *)"GET https://api.seniverse.com/v3/weather/now.json?key=S3agVnw68C-QvEMYp&location=jiangsu suzhou&language=zh-Hans&unit=c\r\n", 600)) // 获取天气数据，预计120ms获取成功
                    {
                        wifi_info.status = WIFI_STATUS_ERROR;
                        break;
                    }
                    // 把天气数据缓存下来
                    uint32_t weather_len = strlen((const char *)g_esp_uart_recv_buff) + 1;
                    uint8_t *p_weather = (uint8_t *)malloc(weather_len);
                    if (p_weather == NULL) // 分配失败
                    {
                        wifi_info.status = WIFI_STATUS_MALLOC_ERROR;
                        break;
                    }
                    memcpy((void *)p_weather, (const void *)g_esp_uart_recv_buff, weather_len);
                    if (esp_exit_pass_through(&huart6) || // 退出透传模式
                        esp_send_at_cmd(&huart6, (const uint8_t *)"AT+CIPCLOSE\r\n", 10)) // 关闭tcp连接
                    {
                        free(p_weather);
                        wifi_info.status = WIFI_STATUS_ERROR;
                        break;
                    }
                    // 有效天气数据在本任务中提取，提取后发送给主任务
                    if (weather_get_info((WeatherInfo_t *)&wifi_info, p_weather))
                    {
                        free(p_weather);
                        wifi_info.status = WIFI_STATUS_PARSE_WEATHER_ERROR;
                        break;
                    }
                    free(p_weather);
                    break;
                case WIFI_ACTION_GET_SNTPTIME:
                    wifi_info.action_val = WIFI_ACTION_GET_SNTPTIME;
                    wifi_info.status = WIFI_STATUS_OK;
                    //AT+CIPSNTPCFG=1,8 //设置时区，1ms接收完成
                    //AT+CIPSNTPTIME?  //查询时间，5ms接收完成
                    if (esp_send_at_cmd(&huart6, (const uint8_t *)"AT+CIPSNTPCFG=1,8\r\n", 10)) // 设置时区，1ms接收完成
                    {
                        wifi_info.status = WIFI_STATUS_ERROR;
                        break;
                    }
                    for (uint8_t i = 0; i < 6; i++)
                    {
                        if (esp_send_at_cmd(&huart6, (const uint8_t *)"AT+CIPSNTPTIME?\r\n", 30)) // 查询时间，5ms接收完成
                        {
                            wifi_info.status = WIFI_STATUS_ERROR;
                            break;
                        }
                        // AT+CIPSNTPTIME?
                        // +CIPSNTPTIME:Thu Dec 25 22:08:46 2025
                        // OK
                        // 把Thu Dec 25 22:08:46 2025，包含'\0'的话长度为25，通过队列发送出去
                        g_esp_uart_recv_buff[54] = 0; // 手动添加字符串结束符
                        if (strcmp((const char *)(g_esp_uart_recv_buff + 50), "1970")) // SNTP同步完成
                        {
                            memcpy(wifi_info.sntp_time, (const char *)(g_esp_uart_recv_buff + 30), 25);
                            wifi_info.status = WIFI_STATUS_OK;
                            break;
                        }
                        else
                        {
                            wifi_info.status = WIFI_STATUS_SNTP_1970;
                        }
                        // 查询时间太早，SNTP还没同步完成，此时极容易返回 1970
                        vTaskDelay(pdMS_TO_TICKS(1000)); // 延时1秒后重试
                    }
                    break;
                default:
                    wifi_info.action_val = WIFI_ACTION_NONE;
                    break;
            }

            if (wifi_info.action_val != WIFI_ACTION_NONE)
            {
                if (xQueueSend(wifi_info_queue, &wifi_info, QUEUE_SEND_MAX_DELAY) != pdTRUE)
                {
                    // printf("队列发送失败\r\n");
                    while(1);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{
    MPU_Config();
    // SCB_EnableICache();
    // SCB_EnableDCache();

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* Configure the system clock */
    SystemClock_Config();

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART1_UART_Init();
    __HAL_UART_DISABLE(&huart1); // 禁用UART1
    MX_USART6_UART_Init();       // USART6用于连接ESP8266
    __HAL_UART_DISABLE(&huart6); // 禁用UART6
    MX_SDRAM_Init();
    MX_LTDC_Init();
    MX_RTC_Init();
    Touch_Init();

    /* NVIC configuration for DMA2D interrupts   */
    HAL_NVIC_SetPriority(DMA2D_IRQn, 14, 0);
    HAL_NVIC_EnableIRQ(DMA2D_IRQn);
    /* Enable the transfer complete interrupt */
    LL_DMA2D_EnableIT_TC(DMA2D);

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    // 创建主界面
    lv_main_create(lv_scr_act());

    // SDRAM_Test();
    printf("SysClock: %d\n", HAL_RCC_GetSysClockFreq());
    printf("HCLK: %d\n", HAL_RCC_GetHCLKFreq());
    printf("PCLK1: %d\n", HAL_RCC_GetPCLK1Freq());
    printf("PCLK2: %d\n", HAL_RCC_GetPCLK2Freq());

    PLL3_ClocksTypeDef PLL3_Clocks = {0};
    HAL_RCCEx_GetPLL3ClockFreq(&PLL3_Clocks);
    printf("PLL3_P: %d\n", PLL3_Clocks.PLL3_P_Frequency);
    printf("PLL3_Q: %d\n", PLL3_Clocks.PLL3_Q_Frequency);
    printf("PLL3_R: %d\n", PLL3_Clocks.PLL3_R_Frequency);

    RCC_PeriphCLKInitTypeDef RCC_PeriphCLKInit = {0};
    HAL_RCCEx_GetPeriphCLKConfig(&RCC_PeriphCLKInit);
    /* FMC kernel clock source selection
       00: rcc_hclk3 clock selected as kernel peripheral clock (default after reset)
       01: pll1_q_ck clock selected as kernel peripheral clock
       10: pll2_r_ck clock selected as kernel peripheral clock
       11: per_ck clock selected as kernel peripheral clock
    */
    printf("FmcClockSelection: %d\n", RCC_PeriphCLKInit.FmcClockSelection);
    printf("PLL3_M: %d\n", RCC_PeriphCLKInit.PLL3.PLL3M);
    printf("PLL3_N: %d\n", RCC_PeriphCLKInit.PLL3.PLL3N);
    printf("PLL3_P: %d\n", RCC_PeriphCLKInit.PLL3.PLL3P);
    printf("PLL3_Q: %d\n", RCC_PeriphCLKInit.PLL3.PLL3Q);
    printf("PLL3_R: %d\n", RCC_PeriphCLKInit.PLL3.PLL3R);

    // 创建队列
    wifi_info_queue = xQueueCreate(5, sizeof(wifi_info_t));
    if(wifi_info_queue != NULL)
    {
        printf("wifi_info_queue队列创建成功!\r\n");
    }
    else
    {
        printf("wifi_info_queue队列创建失败!\r\n");
        while(1);
    }
    wifi_action_queue = xQueueCreate(5, sizeof(uint8_t));
    if(wifi_action_queue != NULL)
    {
        printf("wifi_action_queue队列创建成功!\r\n");
    }
    else
    {
        printf("wifi_action_queue队列创建失败!\r\n");
        while(1);
    }

    xTaskCreate(prvGPIOTask01, "gpio_task01", GPIO_TASK1_STACK_SIZE, NULL, GPIO_TASK1_PRIO, &gpio_task1_handler);
    xTaskCreate(prvGPIOTask02, "gpio_task02", GPIO_TASK2_STACK_SIZE, NULL, GPIO_TASK2_PRIO, &gpio_task2_handler);

    /* Start scheduler */
    vTaskStartScheduler();

    /* Will not get here if the scheduler starts successfully.  If you do end up
    here then there wasn't enough heap memory available to start either the idle
    task or the timer/daemon task.  https://www.freertos.org/a00111.html */
    while(1)
    {
    }

    // LCD_SetLayer(0);
    // LCD_SetColor(LCD_RED);
    // LCD_DrawLine(0, 100,100, 100);

//#if 1
//    // 第1个图：使用DMA2D刷色块
//    _DMA2D_Fill((void *)(LCD_MEMORY_ADDR + LCD_WIDTH * 20 * 2 + 24 * 2), /* 显示起始地址(24, 20) */
//                128,                                                     /* 色块长 */
//                128,                                                     /* 色块高 */
//                LCD_WIDTH - 128,                                         /* 色块行偏移 */
//                RGB888_TO_RGB565(0, 0, 255),                             /* 色块颜色 */
//                LTDC_PIXEL_FORMAT_RGB565);                               /* 色块颜色格式 */

//    // 第2个图：显示ARGB8888位图
//   _DMA2D_DrawAlphaBitmap((void *)(LCD_MEMORY_ADDR + LCD_WIDTH * 20 * 2 + 176 * 2), /* 显示起始地址(176, 20) */
//                          (void *)keji_argb8888,                                    /* 位图数据 */
//                          128,                                                      /* 位图长 */
//                          128,                                                      /* 位图高 */
//                          0,                                                        /* 位图行偏移 */
//                          LCD_WIDTH - 128,                                          /* 目标区行偏移 */
//                          LTDC_PIXEL_FORMAT_RGB565);                                /* 目标区颜色格式 */

//    // 第3个图：显示RGB565位图
//    _DMA2D_Copy((void *)dog_rgb565,                                       /* 位图数据 */
//                (void *)(LCD_MEMORY_ADDR + LCD_WIDTH * 20 * 2 + 328 * 2), /* 显示起始地址(328, 20) */
//                128,                                                      /* 位图长 */
//                128,                                                      /* 位图高 */
//                0,                                                        /* 位图行偏移 */
//                LCD_WIDTH - 128);                                         /* 目标区行偏移 */

//    // 第4个图：两个ARGB8888位图混合
//    _DMA2D_AlphaBlendingBulk((uint32_t *)power_argb8888, /* 位图数据1 */
//                             0,
//                             (uint32_t *)dog_argb8888, /* 位图数据2 */
//                             0,
//                             (void *)(LCD_MEMORY_ADDR + LCD_WIDTH * 168 * 2 + 24 * 2), /* 显示起始地址(24, 168) */
//                             LCD_WIDTH - 128,                                          /* 目标区行偏移 */
//                             128,                                                      /* 位图长 */
//                             128);                                                     /* 位图高 */

//    // 第5个图：Alpha透明度200的位图显示
//    _DMA2D_MixColorsBulk((void *)dog_rgb565, /* 位图数据 */
//                         0,
//                         (void *)(LCD_MEMORY_ADDR + LCD_WIDTH * 168 * 2 + 176 * 2), /* 显示起始地址(176, 168) */
//                         LCD_WIDTH - 128,                                           /* 目标区行偏移 */
//                         128,                                                       /* 位图长 */
//                         128,                                                       /* 位图高 */
//                         200);                                                      /* Alpha透明度(0~255) */

//    // 第6个图：Alpha透明度100的位图显示
//    _DMA2D_MixColorsBulk((void *)dog_rgb565, /* 位图数据 */
//                         0,
//                         (void *)(LCD_MEMORY_ADDR + LCD_WIDTH * 168 * 2 + 328 * 2), /* 显示起始地址(328, 168) */
//                         LCD_WIDTH - 128,                                           /* 目标区行偏移 */
//                         128,                                                       /* 位图长 */
//                         128,                                                       /* 位图高 */
//                         100);                                                      /* Alpha透明度(0~255) */
//#else
//    // _DMA2D_Copy((void *)wife_rgb565,       /* 位图数据 */
//    //             (void *)(LCD_MEMORY_ADDR), /* 显示起始地址(0, 0) */
//    //             800,                       /* 位图长 */
//    //             480,                       /* 位图高 */
//    //             0,                         /* 位图行偏移 */
//    //             0);                        /* 目标区行偏移 */

//#endif

//    // while (1)
//    // {
//    //     //        printf("hello %d\n", SystemCoreClock);
//    //     //        printf("%d\n", HAL_GetTick());
//    // }
    while(1)
    {
        lv_task_handler();
        HAL_Delay(10);
    }
    uint32_t timer_delay;
    while(1)
    {
        // 定期调用 lv_task_handler()，用于处理与 lvgl 相关的任务
        timer_delay = lv_task_handler();
        if(timer_delay > 20)
        {
            timer_delay = 20;  // 设置最大延迟避免响应延迟
        }
        HAL_Delay(timer_delay);

        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);

        // Touch_ReadData();
        // if (touchInfo.flag == 1)
        // {
        //     for (uint8_t i = 0; i < touchInfo.num; i++) // 取相应的触摸坐标
        //     {
        //         printf("point[%d]: x = %d, y = %d, ", i, touchInfo.x[i], touchInfo.y[i]);
        //     }
        //     printf("\n");
        // }
    }
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    /** Supply configuration update enable
     */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    /** Configure the main internal regulator output voltage
     */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    /** Configure LSE Drive Capability
     */
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

    /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 2;
    RCC_OscInitStruct.PLL.PLLN = 64;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    RCC_OscInitStruct.PLL.PLLR = 2;

    // RCC_PLL1VCIRANGE_0: Clock range frequency between 1 and 2 MHz
    // RCC_PLL1VCIRANGE_1: Clock range frequency between 2 and 4 MHz
    // RCC_PLL1VCIRANGE_2: Clock range frequency between 4 and 8 MHz
    // RCC_PLL1VCIRANGE_3: Clock range frequency between 8 and 16 MHz
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3; // PLL1输入时钟频率范围介于8MHz到16MHz之间，由于25M晶振2分频等于11.25M

    // RCC_PLL1VCOWIDE: 宽VCO范围，192MHz到836MHz，输入时钟频率为2MHz~16MHz时配置该选项
    // RCC_PLL1VCOMEDIUM: 中等VCO范围，150MHz到420MHz，输入时钟频率为1MHz~2MHz时配置该选项
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;

    RCC_OscInitStruct.PLL.PLLFRACN = 0; // 设置PLL1 VCO的倍频系数的小数部分
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }

#if 0  // LTDC 33MHz
    /* LTDC Clock = HSE / PLL3M * PLL3N / PLL3R
                  = 25MHz / 25 * 330 / 10
                  = 33MHz */
    PeriphClkInitStruct.PLL3.PLL3M = 25;
    PeriphClkInitStruct.PLL3.PLL3N = 330;
    PeriphClkInitStruct.PLL3.PLL3P = 2;
    PeriphClkInitStruct.PLL3.PLL3Q = 2;
    PeriphClkInitStruct.PLL3.PLL3R = 10;

    // RCC_PLL3VCIRANGE_0: Clock range frequency between 1 and 2 MHz
    // RCC_PLL3VCIRANGE_1: Clock range frequency between 2 and 4 MHz
    // RCC_PLL3VCIRANGE_2: Clock range frequency between 4 and 8 MHz
    // RCC_PLL3VCIRANGE_3: Clock range frequency between 8 and 16 MHz
    PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0;

    // RCC_PLL1VCOWIDE: 宽VCO范围，192MHz到836MHz，输入时钟频率为2MHz~16MHz时配置该选项
    // RCC_PLL1VCOMEDIUM: 中等VCO范围，150MHz到420MHz，输入时钟频率为1MHz~2MHz时配置该选项
    PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOMEDIUM;
    PeriphClkInitStruct.PLL3.PLL3FRACN = 0;
#else  // LTDC 50MHz
    /* LTDC Clock = HSE / PLL3M * PLL3N / PLL3R
                  = 25MHz / 2 * 12 / 3
                  = 50MHz */
    PeriphClkInitStruct.PLL3.PLL3M = 2;
    PeriphClkInitStruct.PLL3.PLL3N = 12;
    PeriphClkInitStruct.PLL3.PLL3P = 2;
    PeriphClkInitStruct.PLL3.PLL3Q = 2;
    PeriphClkInitStruct.PLL3.PLL3R = 3;

    // RCC_PLL3VCIRANGE_0: Clock range frequency between 1 and 2 MHz
    // RCC_PLL3VCIRANGE_1: Clock range frequency between 2 and 4 MHz
    // RCC_PLL3VCIRANGE_2: Clock range frequency between 4 and 8 MHz
    // RCC_PLL3VCIRANGE_3: Clock range frequency between 8 and 16 MHz
    PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_3;

    // RCC_PLL1VCOWIDE: 宽VCO范围，192MHz到836MHz，输入时钟频率为2MHz~16MHz时配置该选项
    // RCC_PLL1VCOMEDIUM: 中等VCO范围，150MHz到420MHz，输入时钟频率为1MHz~2MHz时配置该选项
    PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL1VCOWIDE;
    PeriphClkInitStruct.PLL3.PLL3FRACN = 0;
#endif

    /* FMC Clock 200MHz */
    /* LTDC时钟说明：VOS3下最高50MHz，VOS2下最高75MHz，VOS1下最高150MHz，VOS0下最高150MHz。显示屏Typ 30MHz，Max 50MHz*/
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_FMC | RCC_PERIPHCLK_LTDC;
    PeriphClkInitStruct.FmcClockSelection = RCC_FMCCLKSOURCE_HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }
}

void MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_InitStruct;

    HAL_MPU_Disable(); // 先禁止MPU

    // 如下配置内存类型Normal，缓存策略是Write-through, no write allocate
    // Write-through缓存策略可以保证DMA读取正确！但是DMA写入可能会导致CPU读取出错。
    MPU_InitStruct.Enable = MPU_REGION_ENABLE;
    MPU_InitStruct.BaseAddress = SDRAM_BANK_ADDR;
    MPU_InitStruct.Size = MPU_REGION_SIZE_32MB;
    MPU_InitStruct.Number = MPU_REGION_NUMBER0;                 // 设置区域编号
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;   // 设置访问权限
    MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;               // 设置TEX字段
    MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;          // 设置缓存属性，启用
    MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;    // 设置缓冲属性，无缓冲
    MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;      // 设置共享属性，非共享
    MPU_InitStruct.SubRegionDisable = 0x00;                     // 0表示8个子区域均使能
    MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE; // 允许执行从该区域获取的指令
    HAL_MPU_ConfigRegion(&MPU_InitStruct);
    
//    MPU_InitStruct.Enable = MPU_REGION_ENABLE;
//    MPU_InitStruct.BaseAddress = SDRAM_BANK_ADDR;
//    MPU_InitStruct.Size = MPU_REGION_SIZE_32MB;
//    MPU_InitStruct.Number = MPU_REGION_NUMBER0;                 // 设置区域编号
//    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;   // 设置访问权限
//    MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;               // 设置TEX字段
//    MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_BUFFERABLE;     // 设置缓存属性，启用
//    MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;    // 设置缓冲属性，无缓冲
//    MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;      // 设置共享属性，非共享
//    MPU_InitStruct.SubRegionDisable = 0x00;                     // 0表示8个子区域均使能
//    MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE; // 允许执行从该区域获取的指令
//    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* 配置AXI SRAM的MPU属性为NORMAL, 缓存策略为Outer and inner Non-cacheable，即关闭AXI SRAM缓存 */
    MPU_InitStruct.Enable = MPU_REGION_ENABLE;
    MPU_InitStruct.BaseAddress = 0x24000000;
    MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
    MPU_InitStruct.Number = MPU_REGION_NUMBER1;
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
    MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    MPU_InitStruct.SubRegionDisable = 0x00;
    MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    // // 配置AXI SRAM的MPU属性为NORMAL, 缓存策略为 write-back,no write allocate
    // MPU_InitStruct.Enable = MPU_REGION_ENABLE;
    // MPU_InitStruct.BaseAddress = 0x24000000;
    // MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
    // MPU_InitStruct.Number = MPU_REGION_NUMBER1;
    // MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
    // MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
    // MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
    // MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;
    // MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    // MPU_InitStruct.SubRegionDisable = 0x00;
    // MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    // HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* 配置SRAM1和SRAM2的属性为Write through, read allocate，no write allocate */
    MPU_InitStruct.Enable = MPU_REGION_ENABLE;
    MPU_InitStruct.BaseAddress = 0x30000000;
    MPU_InitStruct.Size = MPU_REGION_SIZE_256KB;
    MPU_InitStruct.Number = MPU_REGION_NUMBER2;                 // 设置区域编号
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;   // 设置访问权限
    MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;               // 设置TEX字段
    MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;          // 设置缓存属性，启用
    MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;    // 设置缓冲属性，无缓冲
    MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;      // 设置共享属性，非共享
    MPU_InitStruct.SubRegionDisable = 0x00;                     // 0表示8个子区域均使能
    MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE; // 允许执行从该区域获取的指令
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* 配置SRAM3的属性为Write through, read allocate，no write allocate */
    MPU_InitStruct.Enable = MPU_REGION_ENABLE;
    MPU_InitStruct.BaseAddress = 0x30040000;
    MPU_InitStruct.Size = MPU_REGION_SIZE_32KB;
    MPU_InitStruct.Number = MPU_REGION_NUMBER3;                 // 设置区域编号
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;   // 设置访问权限
    MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;               // 设置TEX字段
    MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;          // 设置缓存属性，启用
    MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;    // 设置缓冲属性，无缓冲
    MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;      // 设置共享属性，非共享
    MPU_InitStruct.SubRegionDisable = 0x00;                     // 0表示8个子区域均使能
    MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE; // 允许执行从该区域获取的指令
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    /* 配置SRAM4的属性为Write through, read allocate，no write allocate */
    MPU_InitStruct.Enable = MPU_REGION_ENABLE;
    MPU_InitStruct.BaseAddress = 0x38000000;
    MPU_InitStruct.Size = MPU_REGION_SIZE_64KB;
    MPU_InitStruct.Number = MPU_REGION_NUMBER4;                 // 设置区域编号
    MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;   // 设置访问权限
    MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;               // 设置TEX字段
    MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;          // 设置缓存属性，启用
    MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;    // 设置缓冲属性，无缓冲
    MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;      // 设置共享属性，非共享
    MPU_InitStruct.SubRegionDisable = 0x00;                     // 0表示8个子区域均使能
    MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE; // 允许执行从该区域获取的指令
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    // 启用默认内存映射，作为特权访问的背景区域。
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT); // 使能MPU
}

// 此函数是从官方Demo\CORTEX_M7_M4_AMP_STM32H745I_Discovery_IAR\CM7\main.c中复制过来
__WEAK void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
/* 如果要在此函数内部声明提供给空闲任务的缓冲区，
   那么它们必须声明为静态变量 - 否则它们会被分配在栈上，
   导致函数退出后不复存在。 */
static StaticTask_t xIdleTaskTCB;
static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    /* 由于 configUSE_STATIC_ALLOCATION 设置为 1，应用程序必须提供
    vApplicationGetIdleTaskMemory() 的实现，用于提供空闲任务使用的内存。
    详见：https://www.freertos.org/a00110.html#configSUPPORT_STATIC_ALLOCATION */

    /* 输出指向 StaticTask_t 结构的指针，空闲任务的状态将存储在其中。 */
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

    /* 输出将用作空闲任务堆栈的数组。 */
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;

    /* 输出 *ppxIdleTaskStackBuffer 所指数组的大小。
    注意：由于该数组必须是 StackType_t 类型，
    configMINIMAL_STACK_SIZE 是以字(word)为单位指定的，而不是字节(bytes)。 */
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

// 参考CubeMX生成的工程进行修改
__WEAK void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize )
{
    /* 如果要在此函数内部声明提供给空闲任务的缓冲区，
       那么它们必须声明为静态变量 - 否则它们会被分配在栈上，
       导致函数退出后不复存在。 */
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

    /* 由于 configUSE_STATIC_ALLOCATION 设置为 1，应用程序必须提供
    vApplicationGetTimerTaskMemory() 的实现，用于提供空闲任务使用的内存。
    详见：https://www.freertos.org/a00110.html#configSUPPORT_STATIC_ALLOCATION */

    /* 输出指向 StaticTask_t 结构的指针，空闲任务的状态将存储在其中。 */
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;

    /* 输出将用作空闲任务堆栈的数组。 */
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;

    /* 输出 *ppxTimerTaskStackBuffer 所指数组的大小。
    注意：由于该数组必须是 StackType_t 类型，
    configMINIMAL_STACK_SIZE 是以字(word)为单位指定的，而不是字节(bytes)。 */
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM2 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM2) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
    /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
