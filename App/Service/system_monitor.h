#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint8_t cpu_usage_percent;
    uint8_t ram_usage_percent;
    uint16_t fps;
    uint32_t freertos_heap_used_bytes;
    uint32_t freertos_heap_total_bytes;
    uint32_t lvgl_heap_used_bytes;
    uint32_t lvgl_heap_total_bytes;
    bool valid;
} system_monitor_snapshot_t;

void system_monitor_init(void);
void system_monitor_sample(uint32_t current_tick);
const system_monitor_snapshot_t *system_monitor_get_snapshot(void);

#endif
