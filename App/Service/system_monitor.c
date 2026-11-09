#include "system_monitor.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "runtime_stats.h"

typedef struct
{
    system_monitor_snapshot_t snapshot;
    uint32_t previous_runtime_counter;
    uint32_t previous_idle_counter;
    uint32_t previous_frame_counter;
    uint32_t previous_tick;
    bool baseline_ready;
} system_monitor_context_t;

static system_monitor_context_t monitor_context;

static uint8_t system_monitor_percent(uint32_t used, uint32_t total)
{
    uint32_t percent;

    if (total == 0U)
    {
        return 0U;
    }

    percent = (uint32_t)(((uint64_t)used * 100U) / total);
    return (uint8_t)((percent > 100U) ? 100U : percent);
}

static void system_monitor_sample_memory(void)
{
    lv_mem_monitor_t lvgl_memory;
    uint32_t freertos_free = (uint32_t)xPortGetFreeHeapSize();
    uint32_t total_used;
    uint32_t total_capacity;

    lv_mem_monitor(&lvgl_memory);
    monitor_context.snapshot.freertos_heap_total_bytes =
        (uint32_t)configTOTAL_HEAP_SIZE;
    monitor_context.snapshot.freertos_heap_used_bytes =
        monitor_context.snapshot.freertos_heap_total_bytes - freertos_free;
    monitor_context.snapshot.lvgl_heap_total_bytes = lvgl_memory.total_size;
    monitor_context.snapshot.lvgl_heap_used_bytes =
        lvgl_memory.total_size - lvgl_memory.free_size;

    /* RAM 表示两个动态内存池的综合占用，不冒充全部物理 SRAM。 */
    total_used = monitor_context.snapshot.freertos_heap_used_bytes +
                 monitor_context.snapshot.lvgl_heap_used_bytes;
    total_capacity = monitor_context.snapshot.freertos_heap_total_bytes +
                     monitor_context.snapshot.lvgl_heap_total_bytes;
    monitor_context.snapshot.ram_usage_percent =
        system_monitor_percent(total_used, total_capacity);
}

void system_monitor_init(void)
{
    /* 主页早于调度器创建，此处只清状态，不能提前访问空闲任务句柄。 */
    memset(&monitor_context, 0, sizeof(monitor_context));
}

void system_monitor_sample(uint32_t current_tick)
{
    uint32_t runtime_counter = platform_runtime_stats_get_counter();
    uint32_t idle_counter = ulTaskGetIdleRunTimeCounter();
    uint32_t frame_counter = lv_port_disp_get_frame_count();
    uint32_t runtime_delta;
    uint32_t idle_delta;
    uint32_t elapsed_ms;
    uint32_t frame_delta;
    uint32_t idle_percent;
    uint32_t fps;

    system_monitor_sample_memory();

    if (!monitor_context.baseline_ready)
    {
        monitor_context.previous_runtime_counter = runtime_counter;
        monitor_context.previous_idle_counter = idle_counter;
        monitor_context.previous_frame_counter = frame_counter;
        monitor_context.previous_tick = current_tick;
        monitor_context.baseline_ready = true;
        return;
    }

    /* 无符号减法天然处理 32 位计数器回绕。 */
    runtime_delta = runtime_counter - monitor_context.previous_runtime_counter;
    idle_delta = idle_counter - monitor_context.previous_idle_counter;
    elapsed_ms = current_tick - monitor_context.previous_tick;
    frame_delta = frame_counter - monitor_context.previous_frame_counter;

    if ((runtime_delta == 0U) || (elapsed_ms == 0U))
    {
        return;
    }

    idle_percent = (uint32_t)(((uint64_t)idle_delta * 100U) / runtime_delta);
    if (idle_percent > 100U)
    {
        idle_percent = 100U;
    }
    monitor_context.snapshot.cpu_usage_percent = (uint8_t)(100U - idle_percent);

    fps = (uint32_t)(((uint64_t)frame_delta * 1000U) / elapsed_ms);
    monitor_context.snapshot.fps = (uint16_t)((fps > UINT16_MAX) ? UINT16_MAX : fps);
    monitor_context.snapshot.valid = true;

    monitor_context.previous_runtime_counter = runtime_counter;
    monitor_context.previous_idle_counter = idle_counter;
    monitor_context.previous_frame_counter = frame_counter;
    monitor_context.previous_tick = current_tick;
}

const system_monitor_snapshot_t *system_monitor_get_snapshot(void)
{
    return &monitor_context.snapshot;
}
