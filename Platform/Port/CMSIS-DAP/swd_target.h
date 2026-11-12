#ifndef SWD_TARGET_H
#define SWD_TARGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    SWD_TARGET_OK = 0,
    SWD_TARGET_ARGUMENT,
    SWD_TARGET_DAP_ERROR,
    SWD_TARGET_NO_ACK,
    SWD_TARGET_TIMEOUT,
    SWD_TARGET_CORE_FAULT,
    SWD_TARGET_VERIFY_ERROR
} swd_target_status_t;

swd_target_status_t swd_target_connect(uint32_t clock_hz, uint32_t *dpidr);
void swd_target_disconnect(void);
swd_target_status_t swd_target_read_memory(uint32_t address, void *data, size_t size);
swd_target_status_t swd_target_write_memory(uint32_t address, const void *data, size_t size);
swd_target_status_t swd_target_read32(uint32_t address, uint32_t *value);
swd_target_status_t swd_target_write32(uint32_t address, uint32_t value);
swd_target_status_t swd_target_halt(uint32_t timeout_ms);
swd_target_status_t swd_target_wait_halted(uint32_t timeout_ms);
swd_target_status_t swd_target_resume(void);
swd_target_status_t swd_target_reset(void);
swd_target_status_t swd_target_reset_and_halt(uint32_t timeout_ms);
swd_target_status_t swd_target_read_core_register(uint32_t selector, uint32_t *value,
                                                  uint32_t timeout_ms);
swd_target_status_t swd_target_write_core_register(uint32_t selector, uint32_t value,
                                                   uint32_t timeout_ms);
const char *swd_target_status_text(swd_target_status_t status);

#endif /* SWD_TARGET_H */
