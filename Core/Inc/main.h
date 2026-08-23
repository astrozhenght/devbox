/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

extern QueueHandle_t wifi_info_queue;
extern QueueHandle_t wifi_action_queue;

#define QUEUE_SEND_MAX_DELAY                (pdMS_TO_TICKS(3000)) // 队列发送最大等待时间

#define WIFI_ACTION_NONE                    0
#define WIFI_ACTION_CONNECT                 1
#define WIFI_ACTION_GET_WEATHER             2
#define WIFI_ACTION_GET_SNTPTIME            3

#define WIFI_STATUS_OK                      0
#define WIFI_STATUS_ERROR                   1
#define WIFI_STATUS_MALLOC_ERROR            2
#define WIFI_STATUS_PARSE_WEATHER_ERROR     3
#define WIFI_STATUS_SNTP_1970               4


typedef struct {
    char weather_text[25]; // 最长数据：雷阵雨伴有冰雹，21个字节
    char weather_temp[10]; // 温度
    char weather_code[5];  // 天气现象代码
    char sntp_time[26];    // SNTP时间字符串，例如"Thu Dec 25 22:08:46 2025"
    uint8_t connected;     // 当前wifi连接状态
    uint8_t action_val;    // 动作值
    uint8_t status;        // 当前通知值对应的处理状态
} wifi_info_t;

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
