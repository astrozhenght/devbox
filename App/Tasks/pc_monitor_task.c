#include "pc_monitor_task.h"

#include "FreeRTOS.h"
#include "task.h"
#include "pc_monitor_service.h"
#include "usbd_dap.h"

#define PC_MONITOR_TASK_STACK_DEPTH 512U
#define PC_MONITOR_TASK_PRIORITY    (tskIDLE_PRIORITY + 2U)
#define PC_MONITOR_TASK_RAM         __attribute__((section(".axisram"), aligned(8)))

static PC_MONITOR_TASK_RAM StaticTask_t monitor_task_control_block;
static PC_MONITOR_TASK_RAM StackType_t monitor_task_stack[PC_MONITOR_TASK_STACK_DEPTH];

static void pc_monitor_task_entry(void *argument)
{
    uint8_t packet[USBD_PC_MONITOR_PACKET_SIZE];
    uint32_t length;

    (void)argument;
    pc_monitor_service_init();
    usbd_pc_monitor_bind_consumer_task(xTaskGetCurrentTaskHandle());

    for (;;)
    {
        if (usbd_pc_monitor_receive(packet, sizeof(packet), &length,
                                    pdMS_TO_TICKS(500U)))
        {
            pc_monitor_service_ingest(packet, length,
                                      (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        }
        pc_monitor_service_poll((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    }
}

bool pc_monitor_task_create(void)
{
    return xTaskCreateStatic(pc_monitor_task_entry,
                             "pc_monitor",
                             PC_MONITOR_TASK_STACK_DEPTH,
                             NULL,
                             PC_MONITOR_TASK_PRIORITY,
                             monitor_task_stack,
                             &monitor_task_control_block) != NULL;
}
