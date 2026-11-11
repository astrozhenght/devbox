#ifndef DAP_SERVICE_H
#define DAP_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bool initialized;          /* DAP 服务及 USB 设备是否初始化完成。 */
    bool usb_configured;       /* 主机是否已经完成 USB 枚举和配置。 */
    bool command_active;       /* 命令正在执行或等待响应入队。 */
    bool dpidr_valid;          /* 是否已从正常 DP 读取响应中取得 DPIDR。 */
    uint8_t debug_port;        /* 当前调试端口，0=未连接，1=SWD。 */
    uint8_t last_command;      /* 最近收到的 CMSIS-DAP 命令编号。 */
    uint32_t swd_clock_hz;     /* 主机最近设置的 SWD 时钟频率。 */
    uint32_t dpidr;            /* 最近读取到的 SW-DP DPIDR/IDCODE。 */
    uint32_t communication_bytes; /* CMSIS-DAP 请求和响应的累计有效字节数。 */
    uint32_t connection_duration_ms; /* 当前 SWD 连接的持续时间。 */
    uint32_t command_count;    /* 已交给 CMSIS-DAP 引擎处理的命令包数。 */
    uint32_t response_count;   /* 已成功送入 USB IN 队列的响应包数。 */
    uint32_t error_count;      /* 初始化失败或响应长度异常的累计次数。 */
    uint32_t success_count;    /* 成功执行并将响应入队的命令包数。 */
    uint32_t failure_count;    /* 返回错误、传输未完成或响应异常的命令包数。 */
} dap_service_status_t;

bool dap_service_init(void);
void dap_service_process(TickType_t timeout);
bool dap_service_get_status(dap_service_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* DAP_SERVICE_H */
