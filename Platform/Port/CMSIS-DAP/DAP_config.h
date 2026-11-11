/*
 * Copyright (c) 2013-2021 ARM Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * ----------------------------------------------------------------------
 *
 * STM32H743 WinUSB CMSIS-DAP v2 adaptation.
 * SWD only: SWCLK=PB5, SWDIO=PB6, nRESET=PB7.
 *
 */

#ifndef __DAP_CONFIG_H__
#define __DAP_CONFIG_H__

#include "stm32h7xx.h"
#include <string.h>

/// Processor Clock of the Cortex-M MCU used in the Debug Unit.
/// Use SystemCoreClock so the SWD delay calculation tracks the actual HCLK.
#define CPU_CLOCK               SystemCoreClock

/// Number of processor cycles for I/O Port write operations.
#define IO_PORT_WRITE_CYCLES    2U

/// Delay loop cycles for slow SWD clock generation.
/// Match usb-hid-daplink setting (default CMSIS-DAP is 3U).
#ifndef DELAY_SLOW_CYCLES
#define DELAY_SLOW_CYCLES       3U
#endif

/// Indicate that Serial Wire Debug (SWD) communication mode is available.
#define DAP_SWD                 1

/// JTAG disabled for this minimal SWD-only build.
#define DAP_JTAG                0

/// Maximum number of JTAG devices on the scan chain (unused when JTAG=0).
#define DAP_JTAG_DEV_CNT        0U

/// Default communication mode: SWD.
#define DAP_DEFAULT_PORT        1U

/// Default SWD clock frequency (start conservative for bring-up).
#define DAP_DEFAULT_SWJ_CLOCK   100000U

/// Maximum CMSIS-DAP command/response size. USB FS still uses 64-byte
/// physical Bulk packets; one logical DAP packet can span 8 USB packets.
#define DAP_PACKET_SIZE         512U

/// Number of logical DAP packets that can be pipelined by the host.
#define DAP_PACKET_COUNT        4U

/// SWO/UART bridge disabled.
#define SWO_UART                0
#define SWO_MANCHESTER          0
#define SWO_STREAM              0
#define SWO_BUFFER_SIZE         0U

#define DAP_UART                0
#define DAP_UART_USB_COM_PORT   0
#define DAP_UART_RX_BUFFER_SIZE 0U
#define DAP_UART_TX_BUFFER_SIZE 0U

/// DWT clock: 400 MHz, matching SystemClock_Config() (25 MHz / 2 * 64 / 2).
/// Must be a preprocessor constant: DAP.c also uses it in #if expressions.
#define TIMESTAMP_CLOCK         400000000U

/// Target device info not fixed.
#define TARGET_FIXED            0

#if TARGET_FIXED != 0
#include <string.h>
static const char TargetDeviceVendor [] = TARGET_DEVICE_VENDOR;
static const char TargetDeviceName   [] = TARGET_DEVICE_NAME;
static const char TargetBoardVendor  [] = TARGET_BOARD_VENDOR;
static const char TargetBoardName    [] = TARGET_BOARD_NAME;
#endif

/** Get Vendor Name string. */
__STATIC_INLINE uint8_t DAP_GetVendorString (char *str) {
  strcpy(str, "STMicroelectronics");
  return (uint8_t)(sizeof("STMicroelectronics"));
}

/** Get Product Name string. */
__STATIC_INLINE uint8_t DAP_GetProductString (char *str) {
  strcpy(str, "CMSIS-DAP v2");
  return (uint8_t)(sizeof("CMSIS-DAP v2"));
}

/** Get Serial Number string. */
__STATIC_INLINE uint8_t DAP_GetSerNumString (char *str) {
  (void)str;
  return (0U);
}

/** Get Target Device Vendor string. */
__STATIC_INLINE uint8_t DAP_GetTargetDeviceVendorString (char *str) {
#if TARGET_FIXED != 0
  uint8_t len;
  strcpy(str, TargetDeviceVendor);
  len = (uint8_t)(strlen(TargetDeviceVendor) + 1U);
  return (len);
#else
  (void)str;
  return (0U);
#endif
}

/** Get Target Device Name string. */
__STATIC_INLINE uint8_t DAP_GetTargetDeviceNameString (char *str) {
#if TARGET_FIXED != 0
  uint8_t len;
  strcpy(str, TargetDeviceName);
  len = (uint8_t)(strlen(TargetDeviceName) + 1U);
  return (len);
#else
  (void)str;
  return (0U);
#endif
}

/** Get Target Board Vendor string. */
__STATIC_INLINE uint8_t DAP_GetTargetBoardVendorString (char *str) {
#if TARGET_FIXED != 0
  uint8_t len;
  strcpy(str, TargetBoardVendor);
  len = (uint8_t)(strlen(TargetBoardVendor) + 1U);
  return (len);
#else
  (void)str;
  return (0U);
#endif
}

/** Get Target Board Name string. */
__STATIC_INLINE uint8_t DAP_GetTargetBoardNameString (char *str) {
#if TARGET_FIXED != 0
  uint8_t len;
  strcpy(str, TargetBoardName);
  len = (uint8_t)(strlen(TargetBoardName) + 1U);
  return (len);
#else
  (void)str;
  return (0U);
#endif
}

/** Get Product Firmware Version string. */
__STATIC_INLINE uint8_t DAP_GetProductFirmwareVersionString (char *str) {
  (void)str;
  return (0U);
}


//**************************************************************************************************
// Debug Port I/O Pins (SWD)
//   SWCLK/TCK : PB5
//   SWDIO/TMS : PB6
//   nRESET    : PB7
//**************************************************************************************************

#define DAP_GPIO_PORT           GPIOB
#define DAP_PIN_SWCLK           5U
#define DAP_PIN_SWDIO           6U
#define DAP_PIN_nRESET          7U

#define DAP_PIN_SWCLK_Msk       (1UL << DAP_PIN_SWCLK)
#define DAP_PIN_SWDIO_Msk       (1UL << DAP_PIN_SWDIO)
#define DAP_PIN_nRESET_Msk      (1UL << DAP_PIN_nRESET)

/** Setup JTAG I/O pins (unused because DAP_JTAG=0). */
__STATIC_INLINE void PORT_JTAG_SETUP (void) {
  ;
}

/** Setup SWD I/O pins: SWCLK, SWDIO, and nRESET.
    Configures the DAP Hardware I/O pins for Serial Wire Debug (SWD) mode:
     - SWCLK, SWDIO to output push-pull mode and set to default high level.
     - SWDIO with internal pull-up.
     - nRESET to open-drain output mode, released (high via pull-up).
*/
__STATIC_INLINE void PORT_SWD_SETUP (void) {
  uint32_t moder;

  /* Default levels: all high */
  DAP_GPIO_PORT->BSRR = DAP_PIN_SWCLK_Msk | DAP_PIN_SWDIO_Msk | DAP_PIN_nRESET_Msk;

  /* Low slew rate avoids extra SWCLK edges on the external SWD wiring.
     OSPEEDR controls edge speed, independently of the SWD clock frequency. */
  DAP_GPIO_PORT->OSPEEDR &= ~((3UL << (DAP_PIN_SWCLK * 2U)) |
                             (3UL << (DAP_PIN_SWDIO * 2U)));

  /* Pull-ups on SWDIO and nRESET */
  DAP_GPIO_PORT->PUPDR = (DAP_GPIO_PORT->PUPDR &
                          ~((3UL << (DAP_PIN_SWDIO * 2U)) | (3UL << (DAP_PIN_nRESET * 2U)))) |
                         (1UL << (DAP_PIN_SWDIO * 2U)) | (1UL << (DAP_PIN_nRESET * 2U));

  /* nRESET open-drain */
  DAP_GPIO_PORT->OTYPER |= DAP_PIN_nRESET_Msk;

  /* Output mode for all three pins */
  moder = DAP_GPIO_PORT->MODER;
  moder &= ~((3UL << (DAP_PIN_SWCLK * 2U)) | (3UL << (DAP_PIN_SWDIO * 2U)) |
             (3UL << (DAP_PIN_nRESET * 2U)));
  moder |=  (1UL << (DAP_PIN_SWCLK * 2U)) | (1UL << (DAP_PIN_SWDIO * 2U)) |
            (1UL << (DAP_PIN_nRESET * 2U));
  DAP_GPIO_PORT->MODER = moder;
}

/** Disable JTAG/SWD I/O Pins.
    Configures:
     - SWCLK, SWDIO to High-Z input mode.
     - nRESET to input mode with pull-up (target is not held in reset).
*/
__STATIC_INLINE void PORT_OFF (void) {
  DAP_GPIO_PORT->MODER &= ~((3UL << (DAP_PIN_SWCLK * 2U)) | (3UL << (DAP_PIN_SWDIO * 2U)) |
                            (3UL << (DAP_PIN_nRESET * 2U)));

  /* Keep a pull-up on nRESET so the target is never held in reset by this probe */
  DAP_GPIO_PORT->PUPDR = (DAP_GPIO_PORT->PUPDR & ~(3UL << (DAP_PIN_nRESET * 2U))) |
                         (1UL << (DAP_PIN_nRESET * 2U));
}


// SWCLK/TCK I/O pin -------------------------------------
__STATIC_FORCEINLINE uint32_t PIN_SWCLK_TCK_IN  (void) {
  return ((DAP_GPIO_PORT->IDR >> DAP_PIN_SWCLK) & 1U);
}
__STATIC_FORCEINLINE void     PIN_SWCLK_TCK_SET (void) {
  DAP_GPIO_PORT->BSRR = DAP_PIN_SWCLK_Msk;
}
__STATIC_FORCEINLINE void     PIN_SWCLK_TCK_CLR (void) {
  DAP_GPIO_PORT->BSRR = (DAP_PIN_SWCLK_Msk << 16U);
}


// SWDIO/TMS Pin I/O --------------------------------------
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_TMS_IN  (void) {
  return ((DAP_GPIO_PORT->IDR >> DAP_PIN_SWDIO) & 1U);
}
__STATIC_FORCEINLINE void     PIN_SWDIO_TMS_SET (void) {
  DAP_GPIO_PORT->BSRR = DAP_PIN_SWDIO_Msk;
}
__STATIC_FORCEINLINE void     PIN_SWDIO_TMS_CLR (void) {
  DAP_GPIO_PORT->BSRR = (DAP_PIN_SWDIO_Msk << 16U);
}

// SWDIO I/O pin (used in SWD mode only) ------------------
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_IN      (void) {
  return ((DAP_GPIO_PORT->IDR >> DAP_PIN_SWDIO) & 1U);
}
__STATIC_FORCEINLINE void     PIN_SWDIO_OUT     (uint32_t bit) {
  if ((bit & 1U) != 0U) {
    DAP_GPIO_PORT->BSRR = DAP_PIN_SWDIO_Msk;
  } else {
    DAP_GPIO_PORT->BSRR = (DAP_PIN_SWDIO_Msk << 16U);
  }
}
__STATIC_FORCEINLINE void     PIN_SWDIO_OUT_ENABLE  (void) {
  DAP_GPIO_PORT->MODER = (DAP_GPIO_PORT->MODER & ~(3UL << (DAP_PIN_SWDIO * 2U))) |
                         (1UL << (DAP_PIN_SWDIO * 2U));
}
__STATIC_FORCEINLINE void     PIN_SWDIO_OUT_DISABLE (void) {
  DAP_GPIO_PORT->MODER &= ~(3UL << (DAP_PIN_SWDIO * 2U));
}


// TDI Pin I/O (unused in SWD-only build) -----------------
__STATIC_FORCEINLINE uint32_t PIN_TDI_IN  (void) {
  return (0U);
}
__STATIC_FORCEINLINE void     PIN_TDI_OUT (uint32_t bit) {
  (void)bit;
}


// TDO Pin I/O (unused in SWD-only build) -----------------
__STATIC_FORCEINLINE uint32_t PIN_TDO_IN  (void) {
  return (0U);
}


// nTRST Pin I/O (unused in SWD-only build) ---------------
__STATIC_FORCEINLINE uint32_t PIN_nTRST_IN   (void) {
  return (0U);
}
__STATIC_FORCEINLINE void     PIN_nTRST_OUT  (uint32_t bit) {
  (void)bit;
}


// nRESET Pin I/O -----------------------------------------
__STATIC_FORCEINLINE uint32_t PIN_nRESET_IN  (void) {
  return ((DAP_GPIO_PORT->IDR >> DAP_PIN_nRESET) & 1U);
}
__STATIC_FORCEINLINE void     PIN_nRESET_OUT (uint32_t bit) {
  if ((bit & 1U) != 0U) {
    DAP_GPIO_PORT->BSRR = DAP_PIN_nRESET_Msk;
  } else {
    DAP_GPIO_PORT->BSRR = (DAP_PIN_nRESET_Msk << 16U);
  }
}


//**************************************************************************************************
// Status LEDs
//**************************************************************************************************
// Set pin to 0 to disable.
#define LED_CONNECTED_PORT      GPIOI
#define LED_CONNECTED_PIN       0U
#define LED_RUNNING_PORT        GPIOI
#define LED_RUNNING_PIN         0U

__STATIC_INLINE void LED_CONNECTED_OUT (uint32_t bit) {
#if (LED_CONNECTED_PIN != 0)
  HAL_GPIO_WritePin(LED_CONNECTED_PORT, LED_CONNECTED_PIN,
                    (bit != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
  (void)bit;
#endif
}
__STATIC_INLINE void LED_RUNNING_OUT (uint32_t bit) {
#if (LED_RUNNING_PIN != 0)
  HAL_GPIO_WritePin(LED_RUNNING_PORT, LED_RUNNING_PIN,
                    (bit != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
  (void)bit;
#endif
}


//**************************************************************************************************
// Timestamp
//**************************************************************************************************
__STATIC_INLINE uint32_t TIMESTAMP_GET (void) {
  return (DWT->CYCCNT);
}


//**************************************************************************************************
// Initialization
//**************************************************************************************************
/** Setup of the Debug Unit I/O pins and LEDs. */
__STATIC_INLINE void DAP_SETUP (void) {
  /* Enable GPIOB clock */
  RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN;
  (void)RCC->AHB4ENR;

  /* All pins to safe state */
  PORT_OFF();

  /* Enable DWT cycle counter for TIMESTAMP_GET */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR = 0xC5ACCE55UL;  /* Unlock the Cortex-M7 DWT registers. */
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/** Reset Target Device (optional custom sequence). */
__STATIC_INLINE uint8_t RESET_TARGET (void) {
  return (0U);
}


#endif /* __DAP_CONFIG_H__ */
