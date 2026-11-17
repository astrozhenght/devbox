#ifndef PC_MONITOR_SERVICE_H
#define PC_MONITOR_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PC_MONITOR_MODEL_CAPACITY     48U
#define PC_MONITOR_RAM_INFO_CAPACITY  24U
#define PC_MONITOR_HISTORY_POINTS     60U
#define PC_MONITOR_VALUE_UNAVAILABLE  (-1)

/**
 * struct pc_monitor_snapshot_t - Windows 性能数据的最新一致性快照
 *
 * PCMON/PCINFO v4 只保留 STM32 界面实际显示的数据，并补充硬件型号与
 * 内存规格。速率字段统一使用 B/s，保留低速传输的显示精度。
 */
typedef struct
{
    uint32_t version;
    uint32_t sequence;
    uint32_t received_ms;
    uint32_t ram_used_mib;
    uint32_t ram_total_mib;
    uint32_t vram_used_mib;
    uint32_t vram_total_mib;
    uint64_t net_upload_bytes_s;
    uint64_t net_download_bytes_s;
    uint64_t disk_read_bytes_s;
    uint64_t disk_write_bytes_s;
    uint16_t cpu_frequency_mhz;
    uint16_t cpu_base_frequency_mhz;
    uint16_t gpu_frequency_mhz;
    uint16_t gpu_max_frequency_mhz;
    uint16_t ram_data_rate_mts;
    int8_t cpu_usage;
    int8_t ram_usage;
    int8_t disk_usage;
    char cpu_model[PC_MONITOR_MODEL_CAPACITY];
    char gpu_model[PC_MONITOR_MODEL_CAPACITY];
    char ram_model[PC_MONITOR_MODEL_CAPACITY];
    char ram_type[PC_MONITOR_RAM_INFO_CAPACITY];
    char ram_configuration[PC_MONITOR_RAM_INFO_CAPACITY];
    bool valid;
    bool connected;
    bool info_valid;
} pc_monitor_snapshot_t;

typedef struct
{
    uint8_t count;
    uint8_t head;
    int8_t cpu_usage[PC_MONITOR_HISTORY_POINTS];
    int8_t vram_usage[PC_MONITOR_HISTORY_POINTS];
} pc_monitor_history_t;

void pc_monitor_service_init(void);
void pc_monitor_service_ingest(const uint8_t *data, size_t length, uint32_t now_ms);
void pc_monitor_service_poll(uint32_t now_ms);
bool pc_monitor_service_get_snapshot(pc_monitor_snapshot_t *snapshot);
void pc_monitor_service_get_history(pc_monitor_history_t *history);

#endif /* PC_MONITOR_SERVICE_H */
