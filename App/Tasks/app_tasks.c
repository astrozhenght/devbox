#include "app_tasks.h"
#include "ui_task.h"
#include "wifi_task.h"
#include "wifi_service_port.h"

bool app_tasks_create(void)
{
    // 创建队列
    if (!wifi_service_create_command_queue() ||
        !wifi_service_create_message_queue())
    {
        return false;
    }

    if (!ui_task_create())
    {
        return false;
    }

    if (!wifi_task_create())
    {
        return false;
    }

    return true;
}
