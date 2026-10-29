#ifndef UART_SERVICE_PORT_H
#define UART_SERVICE_PORT_H

#include <stddef.h>
#include <stdint.h>
#include "uart_service.h"

#ifdef __cplusplus
extern "C" {
#endif

uart_service_status_t uart_service_port_open(const uart_service_config_t *config);
uart_service_status_t uart_service_port_close(void);
uart_service_status_t uart_service_port_reconfigure_rx_params(void);
uart_service_status_t uart_service_port_send(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
