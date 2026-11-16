#include "pc_monitor_service.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define PC_MONITOR_LINE_CAPACITY 384U
#define PC_MONITOR_STALE_MS      3000U

static pc_monitor_snapshot_t monitor_snapshot;
static pc_monitor_history_t monitor_history;
static char receive_line[PC_MONITOR_LINE_CAPACITY];
static size_t receive_length;

static void pc_monitor_copy_text(char *destination, size_t capacity,
                                 const char *source)
{
    if ((destination == NULL) || (capacity == 0U)) return;
    (void)snprintf(destination, capacity, "%s", (source != NULL) ? source : "");
}

static uint8_t pc_monitor_vram_usage(const pc_monitor_snapshot_t *snapshot)
{
    uint32_t usage;

    if (snapshot->vram_total_mib == 0U) return 0U;
    usage = (snapshot->vram_used_mib * 100U) / snapshot->vram_total_mib;
    return (uint8_t)((usage > 100U) ? 100U : usage);
}

static void pc_monitor_append_history(const pc_monitor_snapshot_t *snapshot)
{
    uint8_t index = monitor_history.head;

    monitor_history.cpu_usage[index] = snapshot->cpu_usage;
    monitor_history.vram_usage[index] = (int8_t)pc_monitor_vram_usage(snapshot);
    monitor_history.head = (uint8_t)((index + 1U) % PC_MONITOR_HISTORY_POINTS);
    if (monitor_history.count < PC_MONITOR_HISTORY_POINTS) monitor_history.count++;
}

/* PCMON v3: 仅解析当前 UI 使用的 17 个数据字段。 */
static bool pc_monitor_parse_metrics_v3(const char *line,
                                        pc_monitor_snapshot_t *snapshot,
                                        uint32_t now_ms)
{
    unsigned protocol, sequence, ram_used, ram_total;
    unsigned net_upload, net_download, cpu_frequency, cpu_base_frequency;
    unsigned vram_used, vram_total, gpu_frequency, gpu_max_frequency;
    unsigned disk_read, disk_write;
    int cpu, ram, disk_usage;

    if (sscanf(line,
               "PCMON,%u,%u,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%u,%u",
               &protocol, &sequence, &cpu, &ram, &ram_used, &ram_total,
               &net_upload, &net_download, &cpu_frequency,
               &cpu_base_frequency, &vram_used, &vram_total, &gpu_frequency,
               &gpu_max_frequency, &disk_usage, &disk_read, &disk_write) != 17)
    {
        return false;
    }
    if ((protocol != 3U) || (cpu < -1) || (cpu > 100) ||
        (ram < 0) || (ram > 100) || (ram_used > ram_total) ||
        (disk_usage < -1) || (disk_usage > 100))
    {
        return false;
    }

    snapshot->sequence = sequence;
    snapshot->cpu_usage = (int8_t)cpu;
    snapshot->ram_usage = (int8_t)ram;
    snapshot->ram_used_mib = ram_used;
    snapshot->ram_total_mib = ram_total;
    snapshot->net_upload_kib_s = net_upload;
    snapshot->net_download_kib_s = net_download;
    snapshot->cpu_frequency_mhz = (uint16_t)cpu_frequency;
    snapshot->cpu_base_frequency_mhz = (uint16_t)cpu_base_frequency;
    snapshot->vram_used_mib = vram_used;
    snapshot->vram_total_mib = vram_total;
    snapshot->gpu_frequency_mhz = (uint16_t)gpu_frequency;
    snapshot->gpu_max_frequency_mhz = (uint16_t)gpu_max_frequency;
    snapshot->disk_usage = (int8_t)disk_usage;
    snapshot->disk_read_kib_s = disk_read;
    snapshot->disk_write_kib_s = disk_write;
    snapshot->received_ms = now_ms;
    snapshot->valid = true;
    snapshot->connected = true;
    snapshot->version++;
    return true;
}

static bool pc_monitor_parse_metrics(const char *line,
                                     pc_monitor_snapshot_t *snapshot,
                                     uint32_t now_ms)
{
    return pc_monitor_parse_metrics_v3(line, snapshot, now_ms);
}

static bool pc_monitor_parse_info(const char *line,
                                  pc_monitor_snapshot_t *snapshot)
{
    unsigned protocol;
    unsigned ram_data_rate = 0U;
    char cpu[PC_MONITOR_MODEL_CAPACITY] = {0};
    char gpu[PC_MONITOR_MODEL_CAPACITY] = {0};
    char ram[PC_MONITOR_MODEL_CAPACITY] = {0};
    char ram_type[PC_MONITOR_RAM_INFO_CAPACITY] = {0};
    char ram_configuration[PC_MONITOR_RAM_INFO_CAPACITY] = {0};
    int fields;

    fields = sscanf(line,
                    "PCINFO,%u,%47[^,],%47[^,],%47[^,],%23[^,],%23[^,],%u",
                    &protocol, cpu, gpu, ram, ram_type,
                    ram_configuration, &ram_data_rate);
    if ((fields != 7) || (protocol != 3U)) return false;

    pc_monitor_copy_text(snapshot->cpu_model, sizeof(snapshot->cpu_model), cpu);
    pc_monitor_copy_text(snapshot->gpu_model, sizeof(snapshot->gpu_model), gpu);
    pc_monitor_copy_text(snapshot->ram_model, sizeof(snapshot->ram_model), ram);
    pc_monitor_copy_text(snapshot->ram_type, sizeof(snapshot->ram_type),
                         ram_type);
    pc_monitor_copy_text(snapshot->ram_configuration,
                         sizeof(snapshot->ram_configuration),
                         ram_configuration);
    snapshot->ram_data_rate_mts = (uint16_t)ram_data_rate;
    snapshot->info_valid = true;
    snapshot->version++;
    return true;
}

static bool pc_monitor_parse_line(char *line, pc_monitor_snapshot_t *snapshot,
                                  uint32_t now_ms)
{
    if (strncmp(line, "PCMON,", 6U) == 0)
        return pc_monitor_parse_metrics(line, snapshot, now_ms);
    if (strncmp(line, "PCINFO,", 7U) == 0)
        return pc_monitor_parse_info(line, snapshot);
    return false;
}

static void pc_monitor_commit_line(uint32_t now_ms)
{
    pc_monitor_snapshot_t next;
    bool metrics_frame = (strncmp(receive_line, "PCMON,", 6U) == 0);

    receive_line[receive_length] = '\0';
    taskENTER_CRITICAL();
    next = monitor_snapshot;
    taskEXIT_CRITICAL();
    if (pc_monitor_parse_line(receive_line, &next, now_ms))
    {
        taskENTER_CRITICAL();
        if (metrics_frame) pc_monitor_append_history(&next);
        monitor_snapshot = next;
        taskEXIT_CRITICAL();
    }
    receive_length = 0U;
}

void pc_monitor_service_init(void)
{
    taskENTER_CRITICAL();
    memset(&monitor_snapshot, 0, sizeof(monitor_snapshot));
    memset(&monitor_history, 0, sizeof(monitor_history));
    monitor_snapshot.cpu_usage = -1;
    monitor_snapshot.ram_usage = -1;
    monitor_snapshot.disk_usage = -1;
    taskEXIT_CRITICAL();
    receive_length = 0U;
}

void pc_monitor_service_ingest(const uint8_t *data, size_t length, uint32_t now_ms)
{
    size_t index;

    if (data == NULL) return;
    for (index = 0U; index < length; index++)
    {
        char value = (char)data[index];
        if (value == '\n')
        {
            if (receive_length != 0U) pc_monitor_commit_line(now_ms);
        }
        else if (value != '\r')
        {
            if (receive_length < (sizeof(receive_line) - 1U))
                receive_line[receive_length++] = value;
            else
                receive_length = 0U;
        }
    }
}

void pc_monitor_service_poll(uint32_t now_ms)
{
    taskENTER_CRITICAL();
    if (monitor_snapshot.connected &&
        ((uint32_t)(now_ms - monitor_snapshot.received_ms) > PC_MONITOR_STALE_MS))
    {
        monitor_snapshot.connected = false;
        monitor_snapshot.version++;
    }
    taskEXIT_CRITICAL();
}

bool pc_monitor_service_get_snapshot(pc_monitor_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    taskENTER_CRITICAL();
    *snapshot = monitor_snapshot;
    taskEXIT_CRITICAL();
    return snapshot->valid;
}

void pc_monitor_service_get_history(pc_monitor_history_t *history)
{
    if (history == NULL) return;
    taskENTER_CRITICAL();
    *history = monitor_history;
    taskEXIT_CRITICAL();
}
