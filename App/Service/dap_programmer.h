#ifndef DAP_PROGRAMMER_H
#define DAP_PROGRAMMER_H

#include <stdbool.h>
#include <stdint.h>

#include "file_service.h"

#define DAP_PROGRAMMER_SWD_CLOCK_HZ 10000000U

typedef enum
{
    DAP_PROGRAMMER_IDLE = 0,
    DAP_PROGRAMMER_VALIDATING,
    DAP_PROGRAMMER_CONNECTING,
    DAP_PROGRAMMER_LOADING_ALGORITHM,
    DAP_PROGRAMMER_ERASING,
    DAP_PROGRAMMER_PROGRAMMING,
    DAP_PROGRAMMER_VERIFYING,
    DAP_PROGRAMMER_RESETTING,
    DAP_PROGRAMMER_SUCCESS,
    DAP_PROGRAMMER_FAILED,
    DAP_PROGRAMMER_CANCELLED
} dap_programmer_state_t;

/* 本地烧录擦除方式。 */
typedef enum
{
    DAP_ERASE_SECTOR = 0, /* 只擦除固件覆盖的扇区。 */
    DAP_ERASE_CHIP = 1    /* 整片擦除（EraseChip 或全部扇区）。 */
} dap_erase_mode_t;

typedef struct
{
    dap_programmer_state_t state;
    bool active;
    bool pending;
    bool cancel_requested;
    uint8_t progress_percent;
    uint32_t completed;
    uint32_t total;
    uint32_t dpidr;
    uint32_t device_idcode;
    uint32_t firmware_bytes;
    uint32_t firmware_start; /* 固件有效数据最低地址。 */
    uint32_t firmware_end;   /* 固件有效数据结束地址，不包含此地址。 */
    uint32_t elapsed_ms;     /* 本次作业总耗时，终态冻结。 */
    uint8_t erase_mode;      /* 本次作业使用的擦除方式。 */
    uint32_t ram_start;      /* FLM 算法所在的 RAM 起始地址。 */
    uint32_t ram_size;       /* 可供 FLM 算法使用的 RAM 大小。 */
    dap_programmer_state_t operation_stage; /* 失败/取消后保留所在阶段。 */
    uint32_t swd_clock_hz; /* 提交作业时锁定的请求时钟。 */
    char firmware_path[FILE_SERVICE_PATH_CAP];
    char flm_path[FILE_SERVICE_PATH_CAP];
    char algorithm_name[129];
    char message[96];
} dap_programmer_status_t;

void dap_programmer_init(void);
bool dap_programmer_request(const char *firmware_path, const char *flm_path);
bool dap_programmer_request_with_clock(const char *firmware_path, const char *flm_path,
                                        uint32_t swd_clock_hz);
bool dap_programmer_request_with_options(const char *firmware_path, const char *flm_path,
                                          uint32_t swd_clock_hz, uint8_t erase_mode);
bool dap_programmer_request_with_ram(const char *firmware_path, const char *flm_path,
                                     uint32_t swd_clock_hz, uint8_t erase_mode,
                                     uint32_t ram_start, uint32_t ram_size);
void dap_programmer_cancel(void);
bool dap_programmer_process(void);
bool dap_programmer_get_status(dap_programmer_status_t *status);
const char *dap_programmer_state_text(dap_programmer_state_t state);

#endif /* DAP_PROGRAMMER_H */
