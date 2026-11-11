#include "dap_task.h"

#include "FreeRTOS.h"
#include "task.h"
#include "dap_service.h"

#define DAP_TASK_STACK_DEPTH 768U
#define DAP_TASK_PRIORITY    (tskIDLE_PRIORITY + 3U)
#define DAP_TASK_RAM         __attribute__((section(".axisram"), aligned(8)))

static DAP_TASK_RAM StaticTask_t dap_task_control_block;
static DAP_TASK_RAM StackType_t dap_task_stack[DAP_TASK_STACK_DEPTH];

static void dap_task_entry(void *argument)
{
    (void)argument;

    if (!dap_service_init())
    {
        vTaskDelete(NULL);
    }

    for (;;)
    {
        dap_service_process(portMAX_DELAY);
    }
}

bool dap_task_create(void)
{
    return xTaskCreateStatic(dap_task_entry,
                             "dap",
                             DAP_TASK_STACK_DEPTH,
                             NULL,
                             DAP_TASK_PRIORITY,
                             dap_task_stack,
                             &dap_task_control_block) != NULL;
}
