#include "ui_task.h"

#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"
#include "bsp_rtc.h"
#include "wifi_service_port.h"

#define UI_TASK_PRIORITY       3U       // 任务优先级
#define UI_TASK_STACK_DEPTH    1280U    // 堆栈大小
#define UI_TASK_PERIOD_MS      10U      // 任务周期
static TaskHandle_t ui_task_handle;     // 任务句柄

static void ui_task_entry(void *pvParameters)
{
    (void)pvParameters;

    if (!wifi_service_enqueue_command(WIFI_SERVICE_CMD_CONNECT))
    {
        while(1);
    }

    if (bsp_rtc_require_init())
    {
        if (!wifi_service_enqueue_command(WIFI_SERVICE_CMD_GET_SNTPTIME))
        {
            while(1);
        }
        bsp_rtc_mark_init();
    }

    if (!wifi_service_enqueue_command(WIFI_SERVICE_CMD_GET_WEATHER))
    {
        while(1);
    }

    for (;;)
    {
        lv_task_handler(); // 处理与 lvgl 相关的任务
        vTaskDelay(pdMS_TO_TICKS(UI_TASK_PERIOD_MS));
    }
}

bool ui_task_create(void)
{
    return xTaskCreate(ui_task_entry,
                       "ui_task",
                       UI_TASK_STACK_DEPTH,
                       NULL,
                       UI_TASK_PRIORITY,
                       &ui_task_handle) == pdPASS;
}
