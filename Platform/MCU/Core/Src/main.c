
#include "main.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "ui_home.h"

#include "bsp_types.h"
#include "bsp_board.h"
#include "bsp_rtc.h"
#include "bsp_wifi.h"
#include "sdram.h"

#include "wifi_service_port.h"
#include "wifi_credentials.h"

void SystemClock_Config(void);
static void MPU_Config(void);

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

/**
 * @brief UI任务入口函数
 *
 * 任务启动后依次执行以下操作：
 * 1. 请求Wi-Fi服务连接无线网络；
 * 2. RTC未初始化时，请求通过SNTP同步网络时间；
 * 3. 请求获取天气数据；
 * 4. 周期性调用LVGL任务处理函数，驱动界面刷新和事件处理。
 *
 * @param pvParameters 任务创建时传入的参数，当前未使用
 *
 * @note 本函数作为FreeRTOS任务运行，初始化完成后不会返回。
 * @note LVGL处理周期当前设置为10 ms。
 */
static void prvGPIOTask01( void *pvParameters )
{
    if (wifi_service_enqueue_command(WIFI_SERVICE_CMD_CONNECT) == false)
    {
        while(1);
    }

    if (bsp_rtc_require_init())
    {
        if (wifi_service_enqueue_command(WIFI_SERVICE_CMD_GET_SNTPTIME) == false)
        {
            while(1);
        }
        bsp_rtc_mark_init();
    }

    if (wifi_service_enqueue_command(WIFI_SERVICE_CMD_GET_WEATHER) == false)
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
// 功能包括：联网，获取网络天气，获取网络时间
static void prvGPIOTask02( void *pvParameters )
{
    uint8_t wifi_command;
    wifi_service_message_t wifi_message = {0};
    wifi_message.status = WIFI_SERVICE_STATUS_OK;
    uint8_t *p_wifi_rx_buffer = bsp_wifi_get_rx_buffer();

    vTaskDelay(pdMS_TO_TICKS(500)); // ESP8266上电到打印AT固件版本需要220ms左右
    bsp_wifi_start_receive();

    while(1)
    {
        if (wifi_service_dequeue_command(&wifi_command))
        {
            wifi_message.status = WIFI_SERVICE_STATUS_OK;
            wifi_message.command = (wifi_service_command_t)wifi_command;

            switch (wifi_command)
            {
                case WIFI_SERVICE_CMD_CONNECT:
                    wifi_message.command = WIFI_SERVICE_CMD_CONNECT;
                    if ((bsp_wifi_execute_at_command((const uint8_t *)"AT\r\n", 100) != BSP_WIFI_OK)          || // 测试AT指令
                        (bsp_wifi_execute_at_command((const uint8_t *)"AT+CWMODE=1\r\n", 100) != BSP_WIFI_OK) || // 设置当前Wi-Fi模式为Station模式
                        (bsp_wifi_execute_at_command((const uint8_t *)"AT+CWJAP=\"" APP_WIFI_SSID "\",\"" APP_WIFI_PASSWORD "\"\r\n", 10000) != BSP_WIFI_OK)) // 连接wifi，预计4s连接成功
                    {
                        wifi_message.status = WIFI_SERVICE_STATUS_ERROR;
                    }
                    break;
                case WIFI_SERVICE_CMD_GET_WEATHER:
                    wifi_message.command = WIFI_SERVICE_CMD_GET_WEATHER;
                    // 获取天气信息
                    if ((bsp_wifi_execute_at_command((const uint8_t *)"AT+CIPSTART=\"TCP\",\"api.seniverse.com\",80\r\n", 500) != BSP_WIFI_OK) || // 连接心知服务器，预计50ms连接成功
                        (bsp_wifi_execute_at_command((const uint8_t *)"AT+CIPMODE=1\r\n", 100) != BSP_WIFI_OK) || // 使能esp8266透传模式，透传模式自动重连tcp
                        (bsp_wifi_execute_at_command((const uint8_t *)"AT+CIPSEND\r\n", 100)   != BSP_WIFI_OK) || // 开启数据传输
                        !weather_service_request_update((const uint8_t *)"GET https://api.seniverse.com/v3/weather/now.json?key=" APP_WEATHER_API_KEY "&location=jiangsu suzhou&language=zh-Hans&unit=c\r\n"))
                    {
                        wifi_message.status = WIFI_SERVICE_STATUS_ERROR;
                        break;
                    }
                    // 把天气数据缓存下来
                    uint32_t weather_len = strlen((const char *)p_wifi_rx_buffer) + 1;
                    uint8_t *p_weather = (uint8_t *)malloc(weather_len);
                    if (p_weather == NULL) // 分配失败
                    {
                        wifi_message.status = WIFI_SERVICE_STATUS_NO_MEMORY;
                        break;
                    }
                    memcpy((void *)p_weather, (const void *)p_wifi_rx_buffer, weather_len);
                    if ((bsp_wifi_exit_passthrough() != BSP_WIFI_OK) || // 退出透传模式失败
                        (bsp_wifi_execute_at_command((const uint8_t *)"AT+CIPCLOSE\r\n", 10) != BSP_WIFI_OK)) // 关闭tcp连接失败
                    {
                        free(p_weather);
                        wifi_message.status = WIFI_SERVICE_STATUS_ERROR;
                        break;
                    }
                    // 有效天气数据在本任务中提取，提取后发送给主任务
                    if (!weather_service_parse_response(
                        (const char *)p_weather,
                        (weather_service_info_t *)&wifi_message))
                    {
                        wifi_message.status = WIFI_SERVICE_STATUS_WEATHER_PARSE_ERROR;
                    }
                    free(p_weather);
                    break;
                case WIFI_SERVICE_CMD_GET_SNTPTIME:
                    wifi_message.command = WIFI_SERVICE_CMD_GET_SNTPTIME;
                    //AT+CIPSNTPCFG=1,8 //设置时区，1ms接收完成
                    //AT+CIPSNTPTIME?  //查询时间，5ms接收完成
                    if ((bsp_wifi_execute_at_command((const uint8_t *)"AT+CIPSNTPCFG=1,8\r\n", 10) != BSP_WIFI_OK)) // 设置时区，1ms接收完成
                    {
                        wifi_message.status = WIFI_SERVICE_STATUS_ERROR;
                        break;
                    }
                    for (uint8_t i = 0; i < 10; i++)
                    {
                        if ((bsp_wifi_execute_at_command((const uint8_t *)"AT+CIPSNTPTIME?\r\n", 30) != BSP_WIFI_OK)) // 查询时间，5ms接收完成
                        {
                            wifi_message.status = WIFI_SERVICE_STATUS_ERROR;
                            break;
                        }
                        // AT+CIPSNTPTIME?
                        // +CIPSNTPTIME:Thu Dec 25 22:08:46 2025
                        // OK
                        // 把Thu Dec 25 22:08:46 2025，包含'\0'的话长度为25，通过队列发送出去
                        p_wifi_rx_buffer[54] = 0; // 手动添加字符串结束符
                        if (strcmp((const char *)(p_wifi_rx_buffer + 50), "1970")) // SNTP同步完成
                        {
                            memcpy(wifi_message.data.sntp_time, (const char *)(p_wifi_rx_buffer + 30), 25);
                            wifi_message.status = WIFI_SERVICE_STATUS_OK;
                            break;
                        }
                        else
                        {
                            wifi_message.status = WIFI_SERVICE_STATUS_INVALID_SNTP_TIME;
                        }
                        // 查询时间太早，SNTP还没同步完成，此时极容易返回 1970
                        vTaskDelay(pdMS_TO_TICKS(1000)); // 延时1秒后重试
                    }
                    break;
                default:
                    wifi_message.command = WIFI_SERVICE_CMD_NONE;
                    break;
            }
            if ((wifi_message.command == WIFI_SERVICE_CMD_CONNECT) ||
                (wifi_message.command == WIFI_SERVICE_CMD_GET_WEATHER) ||
                (wifi_message.command == WIFI_SERVICE_CMD_GET_SNTPTIME))
            {
                if (wifi_service_send_message(&wifi_message) == false)
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

    if (bsp_board_init() != BSP_OK)
    {
        Error_Handler();
    }
    printf("SysClock: %d\n", HAL_RCC_GetSysClockFreq());

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    // 创建主界面
    ui_home_create(lv_scr_act());

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
    if (!wifi_service_create_command_queue() ||
        !wifi_service_create_message_queue())
    {
        printf("create queue fail!\n");
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
