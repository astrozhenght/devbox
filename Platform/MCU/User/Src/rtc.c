/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   This file provides code for the configuration
  *          of the RTC instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "rtc.h"
#include "main.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

static RTC_HandleTypeDef hrtc;

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(rtcHandle->Instance==RTC)
  {
    /* USER CODE BEGIN RTC_MspInit 0 */
    
    /* USER CODE END RTC_MspInit 0 */
    
    /** Initializes the peripherals clock
     */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }
    
    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();
    /* USER CODE BEGIN RTC_MspInit 1 */
    
    /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RTC_DISABLE();
  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* RTC init function */
HAL_StatusTypeDef platform_rtc_init(void)
{
  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return HAL_OK;

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */
  
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */
}

// 获取当前时间和日期
HAL_StatusTypeDef platform_rtc_get_datetime(RTC_TimeTypeDef *time, RTC_DateTypeDef *date)
{
    if (HAL_RTC_GetTime(&hrtc, time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (HAL_RTC_GetDate(&hrtc, date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return HAL_ERROR;
    }
    return HAL_OK;
}

// 校准RTC时间和日期
HAL_StatusTypeDef platform_rtc_set_datetime(RTC_TimeTypeDef *time, RTC_DateTypeDef *date)
{
    if (HAL_RTC_SetTime(&hrtc, time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (HAL_RTC_SetDate(&hrtc, date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return HAL_ERROR;
    }
    return HAL_OK;
}

#define RTC_BACKUP_MAGIC    0x5A69U  // RTC备份寄存器中用于判断备份数据是否有效的魔数

// 判断RTC是否需要初始化
bool platform_rtc_require_init(void)
{
    return (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) != RTC_BACKUP_MAGIC);
}

// 写入并验证RTC初始化完成标志
bool platform_rtc_mark_init(void)
{
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_BACKUP_MAGIC);
    return (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_BACKUP_MAGIC);
}

/* USER CODE END 1 */
