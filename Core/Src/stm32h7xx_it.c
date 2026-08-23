/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7xx_it.c
  * @brief   Interrupt Service Routines.
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h7xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lv_hal_tick.h"
#include "FreeRTOS.h"
#include "task.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern TIM_HandleTypeDef  htim2;
extern DMA_HandleTypeDef  hdma_usart1_rx;
extern DMA_HandleTypeDef  hdma_usart1_tx;
extern DMA_HandleTypeDef  hdma_usart6_rx;
extern DMA_HandleTypeDef  hdma_usart6_tx;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart6;


/* FreeRTOS tick timer interrupt handler prototype */
extern void xPortSysTickHandler (void);

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

// /**
//   * @brief This function handles System service call via SWI instruction.
//   */
// void SVC_Handler(void)
// {
//   /* USER CODE BEGIN SVCall_IRQn 0 */

//   /* USER CODE END SVCall_IRQn 0 */
//   /* USER CODE BEGIN SVCall_IRQn 1 */

//   /* USER CODE END SVCall_IRQn 1 */
// }

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

// /**
//   * @brief This function handles Pendable request for system service.
//   */
// void PendSV_Handler(void)
// {
//   /* USER CODE BEGIN PendSV_IRQn 0 */

//   /* USER CODE END PendSV_IRQn 0 */
//   /* USER CODE BEGIN PendSV_IRQn 1 */

//   /* USER CODE END PendSV_IRQn 1 */
// }

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */
  //lv_tick_inc(1); // 告诉LVGL过去1ms了
  /* USER CODE END SysTick_IRQn 0 */
  //HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */
  /* USER CODE END SysTick_IRQn 1 */
  
  /* Clear overflow flag */
  SysTick->CTRL;

  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    /* Call tick handler */
    xPortSysTickHandler();
  }
  
}

/******************************************************************************/
/* STM32H7xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h7xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles TIM2 global interrupt.
  */
void TIM2_IRQHandler(void)
{
  /* USER CODE BEGIN TIM2_IRQn 0 */
  lv_tick_inc(1); // 告诉LVGL过去1ms了
  /* USER CODE END TIM2_IRQn 0 */
  HAL_TIM_IRQHandler(&htim2);
  /* USER CODE BEGIN TIM2_IRQn 1 */

  /* USER CODE END TIM2_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream0 global interrupt.
  */
void DMA1_Stream0_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream0_IRQn 0 */

  /* USER CODE END DMA1_Stream0_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart1_rx);
  /* USER CODE BEGIN DMA1_Stream0_IRQn 1 */

  /* USER CODE END DMA1_Stream0_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream1 global interrupt.
  */
void DMA1_Stream1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream1_IRQn 0 */

  /* USER CODE END DMA1_Stream1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
  /* USER CODE BEGIN DMA1_Stream1_IRQn 1 */

  /* USER CODE END DMA1_Stream1_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream2 global interrupt.
  */
void DMA1_Stream2_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream2_IRQn 0 */

  /* USER CODE END DMA1_Stream2_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart6_rx);
  /* USER CODE BEGIN DMA1_Stream2_IRQn 1 */

  /* USER CODE END DMA1_Stream2_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream3 global interrupt.
  */
void DMA1_Stream3_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream3_IRQn 0 */

  /* USER CODE END DMA1_Stream3_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart6_tx);
  /* USER CODE BEGIN DMA1_Stream3_IRQn 1 */

  /* USER CODE END DMA1_Stream3_IRQn 1 */
}

/**
  * @brief This function handles DMAMUX1 overrun interrupt.
  */
void DMAMUX1_OVR_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN DMAMUX1_OVR_IRQn 0 */

  /* USER CODE END DMAMUX1_OVR_IRQn 0 */

  /* USER CODE BEGIN DMAMUX1_OVR_IRQn 1 */

  /* USER CODE END DMAMUX1_OVR_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles USART6 global interrupt.
  */
void USART6_IRQHandler(void)
{
  /* USER CODE BEGIN USART6_IRQn 0 */

  /* USER CODE END USART6_IRQn 0 */
  HAL_UART_IRQHandler(&huart6);
  /* USER CODE BEGIN USART6_IRQn 1 */

  /* USER CODE END USART6_IRQn 1 */
}

/* USER CODE BEGIN 1 */

#include "app_uart.h"
#include "esp8266.h"
#include "comm_queue.h"

extern Comm_Queue uart1_queue;                 // app_uart.c中已经添加临界区保护
extern uint8_t  g_app_uart_recv_buff[APP_UART_RECV_SIZE]; // 无中断调用，不需要临界区保护
extern uint8_t  g_app_uart_recv_complete_flag; // 已经放入临界区保护
extern uint32_t g_app_uart_recv_length;        // 累计接收长度，已经放入临界区保护
extern uint8_t  g_app_uart_recv_error_status;  // 错误状态机，0-接收正常，1-接收出现错误需要刷新错误信息到屏幕上，2-错误信息已刷新并且未检测到接受正常

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size)
{
    if (huart == &huart1) {
        // 处理接收到的数据
        //uint8_t msg[] = "dma idle success!\r\n";
        //SCB_CleanDCache_by_Addr((uint32_t *)msg, sizeof(msg) - 1);

        HAL_UART_RxEventTypeTypeDef eventType = HAL_UARTEx_GetRxEventType(huart);
        switch (eventType)
        {
            // 这里不需要关心中断标志位的清除，HAL_UART_IRQHandler()中已经处理
            // 重新判断错误标志位，因为在执行中断函数期间，数据线接收到错误数据时，可能会引起错误标志位
        case HAL_UART_RXEVENT_IDLE: // 从UART中断函数中可以看出，当接收类型为IDLE并且UART工作在DMA模式下，会执行到这个分支
            __disable_irq();
            // 把接收到的数据放入FIFO队列
            Comm_Queue_Enqueue(&uart1_queue, g_app_uart_recv_buff, Size);
            g_app_uart_recv_complete_flag = 1;
            g_app_uart_recv_length += Size;
            __enable_irq();
#if 0
            HAL_UARTEx_ReceiveToIdle_DMA(&huart1, msg, 100); // 重新启动DMA接收
#else
            DMA1_Stream0->CR &= ~DMA_SxCR_EN;  /* Disable the peripheral */
            DMA1_Stream0->CR &= (uint32_t)(~DMA_SxCR_DBM); /* Clear DBM bit */
            DMA1->LIFCR = 0x3FUL;  /* 清零数据流0中断标志 */
            DMA1_Stream0->NDTR = APP_UART_RECV_SIZE;    /* Configure DMA Stream data length */
            DMA1_Stream0->PAR = (uint32_t)&huart->Instance->RDR; /* Configure DMA Stream destination address */
            DMA1_Stream0->M0AR = (uint32_t)g_app_uart_recv_buff; /* Configure DMA Stream source address */
            DMA1_Stream0->CR |= DMA_SxCR_EN;    /* Enable the Peripheral */
#endif
            //while ((huart->gState != HAL_UART_STATE_READY) || (HAL_DMA_STATE_READY != huart->hdmarx->State)); // 等待上一次的接收完成
            //huart->gState = HAL_UART_STATE_READY;
            //if (HAL_UART_Transmit_DMA(huart, g_app_uart_recv_buff, Size) != HAL_OK)
            //{
            //    Error_Handler();
            //}
            // g_app_uart_recv_length = Size;
            // g_app_uart_recv_buff[Size] = '\0'; // 添加字符串结束符
            break;
        case HAL_UART_RXEVENT_HT:
            // 处理半传输事件
            break;
        case HAL_UART_RXEVENT_TC: // DMA传输完成回调函数默认为UART_DMAReceiveCplt()函数，该函数中会调用HAL_UARTEx_RxEventCallback()函数
            // 处理传输完成事件
            //if (HAL_UART_Transmit_DMA(huart, g_app_uart_recv_buff, Size) != HAL_OK)
            //{
            //    Error_Handler();
            //}
            __disable_irq();
            // 把接收到的数据放入FIFO队列
            Comm_Queue_Enqueue(&uart1_queue, g_app_uart_recv_buff, Size);
            g_app_uart_recv_complete_flag = 1;
            g_app_uart_recv_length += Size;
            __enable_irq();
            break;
        default:
            break;
        }
    }
    else if (huart == &huart6)
    {
        // 处理接收到的数据
        // uint8_t msg[] = "dma idle success!\r\n";
        // SCB_CleanDCache_by_Addr((uint32_t *)msg, sizeof(msg) - 1);

        HAL_UART_RxEventTypeTypeDef eventType = HAL_UARTEx_GetRxEventType(huart);
        switch (eventType)
        {
            // 这里不需要关心中断标志位的清除，HAL_UART_IRQHandler()中已经处理
            // 重新判断错误标志位，因为在执行中断函数期间，数据线接收到错误数据时，可能会引起错误标志位
        case HAL_UART_RXEVENT_IDLE: // 从UART中断函数中可以看出，当接收类型为IDLE并且UART工作在DMA模式下，会执行到这个分支
            __disable_irq();
            // 把接收到的数据放入FIFO队列
            Comm_Queue_Enqueue(&uart6_queue, g_esp_uart_recv_buff, Size);
            g_esp_uart_recv_complete_flag = 1;
            __enable_irq();
#if 0
            HAL_UARTEx_ReceiveToIdle_DMA(huart, msg, 100); // 重新启动DMA接收
#else
            DMA1_Stream2->CR &= ~DMA_SxCR_EN;                    /* Disable the peripheral */
            DMA1_Stream2->CR &= (uint32_t)(~DMA_SxCR_DBM);       /* Clear DBM bit */
            DMA1->LIFCR = 0x3F0000UL;                            /* 清零数据流2中断标志 */
            DMA1_Stream2->NDTR = ESP_UART_RECV_SIZE;             /* Configure DMA Stream data length */
            DMA1_Stream2->PAR = (uint32_t)&huart->Instance->RDR; /* Configure DMA Stream destination address */
            DMA1_Stream2->M0AR = (uint32_t)g_esp_uart_recv_buff; /* Configure DMA Stream source address */
            DMA1_Stream2->CR |= DMA_SxCR_EN;                     /* Enable the Peripheral */
#endif
            break;
        case HAL_UART_RXEVENT_HT:
            // 处理半传输事件
            break;
        case HAL_UART_RXEVENT_TC: // DMA传输完成回调函数默认为UART_DMAReceiveCplt()函数，该函数中会调用HAL_UARTEx_RxEventCallback()函数
            // 处理传输完成事件
            // if (HAL_UART_Transmit_DMA(huart, g_esp_uart_recv_buff, Size) != HAL_OK)
            //{
            //    Error_Handler();
            //}
            __disable_irq();
            // 把接收到的数据放入FIFO队列
            Comm_Queue_Enqueue(&uart6_queue, g_esp_uart_recv_buff, Size);
            g_esp_uart_recv_complete_flag = 1;
            __enable_irq();
            break;
        default:
            break;
        }
    }
}

// 重定义weak函数
void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
    if (huart->Instance == USART1)
    {
        if (g_app_uart_recv_error_status == 0) // 上次正常接收
        {
            g_app_uart_recv_error_status = 1;  // 标记当前接收出错
        }
        // 在这里，ErrorCode还是有效值！执行完该函数，ReceptionType、Rxstate、ErrorCode都更新了
        HAL_UARTEx_ReceiveToIdle_DMA(huart, g_app_uart_recv_buff, APP_UART_RECV_SIZE); // 发生错误后重新开启接收
    }
    else if (huart->Instance == USART6)
    {
        // 在这里，ErrorCode还是有效值！执行完该函数，ReceptionType、Rxstate、ErrorCode都更新了
        HAL_UARTEx_ReceiveToIdle_DMA(huart, g_esp_uart_recv_buff, ESP_UART_RECV_SIZE); // 发生错误后重新开启接收
    }
}

/* USER CODE END 1 */
