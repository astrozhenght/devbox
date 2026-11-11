#ifndef USBD_DAP_H
#define USBD_DAP_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "DAP_config.h"
#include "task.h"
#include "usbd_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USBD_DAP_OUT_EP             0x01U
#define USBD_DAP_IN_EP              0x82U
#define USBD_DAP_FS_MAX_PACKET_SIZE 64U
#define USBD_DAP_PACKET_SIZE        DAP_PACKET_SIZE
#define USBD_DAP_PACKET_COUNT       DAP_PACKET_COUNT

extern USBD_ClassTypeDef USBD_DAP;

void usbd_dap_bind_consumer_task(TaskHandle_t task);
bool usbd_dap_receive(uint8_t *data,
                      uint32_t capacity,
                      uint32_t *length,
                      TickType_t timeout);
bool usbd_dap_transmit(const uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* USBD_DAP_H */
