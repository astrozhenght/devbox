#include "file_task.h"
#include "file_service_port.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>

#define FILE_TASK_STACK_DEPTH 1536U /* 栈深度以StackType_t元素为单位，不是字节数。 */
#define FILE_TASK_PRIORITY    1U
#define FILE_REQUEST_DEPTH    4U
#define FILE_RESULT_DEPTH     4U
#define FILE_TASK_RAM __attribute__((section(".axisram"), aligned(8)))

typedef enum
{
    FILE_REQUEST_LIST,
    FILE_REQUEST_DELETE,
    FILE_REQUEST_STREAM_OPEN,
    FILE_REQUEST_STREAM_READ,
    FILE_REQUEST_STREAM_SEEK,
    FILE_REQUEST_STREAM_CLOSE
} file_request_kind_t;

typedef struct
{
    file_request_kind_t kind;
    uint32_t id, value;
    void *buffer;
    size_t capacity;
    char path[FILE_SERVICE_PATH_CAP]; /* 入队时复制路径，不持有调用方字符串。 */
} file_request_t;

typedef struct
{
    uint32_t id;
    file_service_status_t status;
    int32_t native_error;
    uint32_t size_bytes;
    size_t transferred;
} file_stream_response_t;

/* 队列及任务均静态分配，大缓冲区和任务栈放在AXI SRAM中。 */
static StaticTask_t task_control;
static FILE_TASK_RAM StackType_t task_stack[FILE_TASK_STACK_DEPTH];
static StaticQueue_t request_control, result_control, stream_result_control;
static FILE_TASK_RAM uint8_t request_memory[FILE_REQUEST_DEPTH * sizeof(file_request_t)];
static FILE_TASK_RAM uint8_t result_memory[FILE_RESULT_DEPTH * sizeof(file_service_event_t)];
static FILE_TASK_RAM uint8_t stream_result_memory[sizeof(file_stream_response_t)];
static FILE_TASK_RAM file_request_t request;
static QueueHandle_t request_queue, result_queue, stream_result_queue;
static uint32_t next_id, stream_next_id;
/* 仅该编号的结果可以继续投递；0表示当前没有需要接收结果的请求。 */
static volatile uint32_t active_id;

/**
 * emit_result() - 将当前请求的结果投递给UI
 * @event: 文件服务生成的事件，非空；成功投递时由队列复制其内容
 * @user: 文件服务回调上下文，当前不使用
 *
 * BEGIN事件仅作为浏览取消检查，不占用结果队列。其他事件每次等待最多
 * 20 ms重试入队；请求仍有效且队列一直满时，总等待时间没有固定上限。
 * 浏览服务会响应返回false；删除服务不因该返回值中止删除。
 *
 * Context: 文件任务上下文，可能阻塞；不可在中断中调用。
 * Return: 当前请求的BEGIN检查通过或事件成功入队返回%true，
 *         请求已取消或被新请求替代返回%false。
 */
static bool emit_result(const file_service_event_t *event, void *user)
{
    (void)user;
    if (event->kind == FILE_SERVICE_EVENT_BEGIN)
        return active_id == event->request_id;
    while (active_id == event->request_id)
    {
        if (xQueueSend(result_queue, event, pdMS_TO_TICKS(20)) == pdPASS) return true;
    }
    return false;
}

/**
 * file_task_entry() - 串行执行文件系统请求
 * @argument: FreeRTOS任务参数，当前不使用
 *
 * 无请求时阻塞等待。读取请求仅在编号仍有效时执行；删除请求一旦入队，
 * 即使编号失效也会尝试执行，直到操作完成或发生错误，不通过取消请求中止。
 * 所有目录遍历和删除共用文件服务工作区，因此只能由此任务串行执行。
 *
 * Context: FreeRTOS文件任务入口，长期运行且可能阻塞，不返回。
 */
static void file_task_entry(void *argument)
{
    file_stream_response_t response;

    (void)argument;
    for (;;)
    {
        if (xQueueReceive(request_queue, &request, portMAX_DELAY) != pdPASS) continue;
        switch (request.kind)
        {
            case FILE_REQUEST_LIST:
                if (request.id == active_id)
                    file_service_list(request.path, request.value, request.id, emit_result, NULL);
                break;
            case FILE_REQUEST_DELETE:
                file_service_delete(request.path, request.id, emit_result, NULL);
                break;
            case FILE_REQUEST_STREAM_OPEN:
                memset(&response, 0, sizeof(response));
                response.id = request.id;
                response.status = file_port_mount();
                if (response.status == FILE_SERVICE_OK)
                    response.status = file_port_open_file(request.path, &response.size_bytes);
                response.native_error = file_port_last_error();
                (void)xQueueSend(stream_result_queue, &response, portMAX_DELAY);
                break;
            case FILE_REQUEST_STREAM_READ:
                memset(&response, 0, sizeof(response));
                response.id = request.id;
                response.status = file_port_read_file(request.buffer, request.capacity,
                                                       &response.transferred);
                response.native_error = file_port_last_error();
                (void)xQueueSend(stream_result_queue, &response, portMAX_DELAY);
                break;
            case FILE_REQUEST_STREAM_SEEK:
                memset(&response, 0, sizeof(response));
                response.id = request.id;
                response.status = file_port_seek_file(request.value);
                response.native_error = file_port_last_error();
                (void)xQueueSend(stream_result_queue, &response, portMAX_DELAY);
                break;
            case FILE_REQUEST_STREAM_CLOSE:
                memset(&response, 0, sizeof(response));
                response.id = request.id;
                response.status = file_port_close_file();
                response.native_error = file_port_last_error();
                (void)xQueueSend(stream_result_queue, &response, portMAX_DELAY);
                break;
            default:
                break;
        }
    }
}

/**
 * submit() - 非阻塞提交文件请求并切换有效结果编号
 * @path: 非空且以SD:/为根的有效路径，必须以空字符结束，长度小于路径缓冲容量
 * @page: 目录页码，从0开始；删除时忽略，页码合法性由文件服务进一步检查
 * @deleting: true提交删除请求，false提交目录读取请求
 * @request_id: 非空输出指针，仅提交成功时写入新请求编号
 *
 * 路径随请求复制，函数返回后调用方可复用原字符串。队列只有一个待处理槽，
 * 队列满时直接失败，不覆盖待处理请求。成功后旧请求的结果不再继续投递，
 * 但已进入结果队列的事件仍需接收方根据编号过滤。
 *
 * Context: UI任务中的单一提交者，不可在中断中调用；不等待队列空位或操作完成。
 *          请求编号生成位于临界区之外，不支持多个任务并发提交。
 * Return: 请求成功入队返回%true；参数无效、队列尚未创建或已满时返回%false，
 *         失败时不修改@request_id指向的值。
 */
static bool submit(const char *path, uint32_t page, bool deleting, uint32_t *request_id)
{
    file_request_t message;
    if (request_id == NULL || request_queue == NULL || !file_service_path_valid(path)) return false;
    memset(&message, 0, sizeof(message));
    next_id++;
    if (next_id == 0U) next_id++;
    message.id = next_id;
    message.value = page;
    message.kind = deleting ? FILE_REQUEST_DELETE : FILE_REQUEST_LIST;
    strcpy(message.path, path);
    taskENTER_CRITICAL();
    BaseType_t sent = xQueueSend(request_queue, &message, 0);
    if (sent == pdPASS) active_id = message.id;
    taskEXIT_CRITICAL();
    if (sent != pdPASS) return false;
    *request_id = message.id;
    return true;
}

/**
 * file_task_create() - 创建文件任务及请求和结果队列
 *
 * 使用静态存储创建容量为1的请求队列、容量为FILE_RESULT_DEPTH的结果队列，
 * 以及文件任务。本函数不挂载文件系统，也不检查SD卡是否存在。
 * 相关存储只供该任务实例使用，初始化阶段只能调用一次。
 *
 * Context: 当前由系统启动流程在调度器启动前调用，不可在中断中调用。
 * Return: 两个队列和任务均创建成功返回%true，否则返回%false；
 *         失败时不回滚已经完成的创建步骤。
 */
bool file_task_create(void)
{
    request_queue = xQueueCreateStatic(FILE_REQUEST_DEPTH, sizeof(file_request_t),
                                       request_memory, &request_control);
    result_queue = xQueueCreateStatic(FILE_RESULT_DEPTH, sizeof(file_service_event_t), result_memory, &result_control);
    stream_result_queue = xQueueCreateStatic(1U, sizeof(file_stream_response_t),
                                             stream_result_memory, &stream_result_control);
    return request_queue != NULL && result_queue != NULL && stream_result_queue != NULL &&
        xTaskCreateStatic(file_task_entry, "file_service", FILE_TASK_STACK_DEPTH,
                          NULL, FILE_TASK_PRIORITY, task_stack, &task_control) != NULL;
}

/**
 * file_task_request_list() - 提交异步目录分页读取请求
 * @path: 以空字符结束的有效SD:/目录路径，提交时复制，长度小于路径缓冲容量
 * @page: 页码，从0开始，每页最多FILE_SERVICE_PAGE_SIZE条
 * @id: 非空输出指针，提交成功时保存请求编号，失败时保持原值
 *
 * 返回成功仅表示入队，目录是否存在和读取是否成功由文件服务异步报告。
 * 调用方通过file_task_receive()获取结果，并按请求编号过滤过期事件。
 * 成功提交会替代旧请求的有效结果编号，但不会取消已入队的删除操作。
 *
 * Context: UI任务中的单一提交者，不可在中断中调用；不等待读取完成。
 * Return: 成功入队返回%true；路径或输出指针无效、队列未创建或已满时返回%false。
 */
bool file_task_request_list(const char *path, uint32_t page, uint32_t *id)
{
    return submit(path, page, false, id);
}

/**
 * file_task_request_delete() - 提交异步文件或目录删除请求
 * @path: 以空字符结束的有效SD:/路径，不允许根目录；提交时复制
 * @id: 非空输出指针，提交成功时保存请求编号，失败时保持原值
 *
 * 目录删除包含内部子项。返回成功仅表示入队，不保证目标存在或最终删除成功。
 * 调用方应先完成用户确认，再提交请求；通过结果事件获取进度和最终状态。
 * 删除可能部分完成后失败，没有回滚。已入队删除不受file_task_cancel()中止。
 *
 * Context: UI任务中的单一提交者，不可在中断中调用；不等待删除完成。
 * Return: 成功入队返回%true；路径无效、目标为根目录、输出指针无效、
 *         队列未创建或已满时返回%false。
 */
bool file_task_request_delete(const char *path, uint32_t *id)
{
    return path != NULL && strlen(path) > 4U && submit(path, 0U, true, id);
}

/**
 * file_task_receive() - 非阻塞取出一条文件任务结果
 * @event: 非空输出指针，成功时写入完整事件，失败时保持原值
 *
 * 成功接收会从队列移除该事件。此处不按有效请求编号过滤，调用方必须检查
 * 事件中的request_id，避免页面切换后使用旧结果；事件也可能报告操作失败。
 *
 * Context: UI任务上下文，不可在中断中调用；队列为空时立即返回。
 * Return: 取出一条事件返回%true；队列未创建、为空或输出指针无效时返回%false。
 */
bool file_task_receive(file_service_event_t *event)
{
    return result_queue != NULL && event != NULL && xQueueReceive(result_queue, event, 0) == pdPASS;
}

/**
 * file_task_cancel() - 使指定请求的后续结果失效并触发浏览取消检查
 * @id: 要取消的请求编号；与当前有效编号不匹配时不改变状态
 *
 * 不清空队列，也不等待操作退出。浏览会在后续回调检查时停止；已开始的
 * 底层读写不会在此直接中止。已入队删除仍执行至完成或错误，仅停止后续结果
 * 投递；已经进入结果队列的事件仍可能被取出，调用方需要自行过滤。
 *
 * Context: 任务上下文，不可在中断中调用；使用短临界区更新有效编号。
 */
void file_task_cancel(uint32_t id)
{
    taskENTER_CRITICAL();
    if (active_id == id) active_id = 0U;
    taskEXIT_CRITICAL();
}

static file_service_status_t stream_request(file_request_t *message,
                                            file_stream_response_t *response,
                                            TickType_t timeout)
{
    TickType_t start;
    TickType_t remaining;

    if ((message == NULL) || (response == NULL) || (request_queue == NULL) ||
        (stream_result_queue == NULL)) return FILE_SERVICE_NOT_READY;
    stream_next_id++;
    if (stream_next_id == 0U) stream_next_id++;
    message->id = stream_next_id;
    start = xTaskGetTickCount();
    if (xQueueSend(request_queue, message, timeout) != pdPASS) return FILE_SERVICE_IO_ERROR;
    remaining = timeout;
    for (;;)
    {
        if (xQueueReceive(stream_result_queue, response, remaining) != pdPASS)
            return FILE_SERVICE_IO_ERROR;
        if (response->id == message->id) return response->status;
        if (timeout != portMAX_DELAY)
        {
            TickType_t elapsed = xTaskGetTickCount() - start;
            if (elapsed >= timeout) return FILE_SERVICE_IO_ERROR;
            remaining = timeout - elapsed;
        }
    }
}

file_service_status_t file_task_stream_open(const char *path, uint32_t *size_bytes,
                                            TickType_t timeout)
{
    file_request_t message;
    file_stream_response_t response;
    file_service_status_t status;

    if ((size_bytes == NULL) || !file_service_path_valid(path))
        return FILE_SERVICE_INVALID_PATH;
    memset(&message, 0, sizeof(message));
    message.kind = FILE_REQUEST_STREAM_OPEN;
    strcpy(message.path, path);
    status = stream_request(&message, &response, timeout);
    if (status == FILE_SERVICE_OK) *size_bytes = response.size_bytes;
    return status;
}

file_service_status_t file_task_stream_read(void *buffer, size_t capacity,
                                            size_t *read_bytes, TickType_t timeout)
{
    file_request_t message;
    file_stream_response_t response;
    file_service_status_t status;

    if ((buffer == NULL) || (read_bytes == NULL) || (capacity == 0U))
        return FILE_SERVICE_INVALID_PATH;
    memset(&message, 0, sizeof(message));
    message.kind = FILE_REQUEST_STREAM_READ;
    message.buffer = buffer;
    message.capacity = capacity;
    status = stream_request(&message, &response, timeout);
    if (status == FILE_SERVICE_OK) *read_bytes = response.transferred;
    return status;
}

file_service_status_t file_task_stream_seek(uint32_t offset, TickType_t timeout)
{
    file_request_t message;
    file_stream_response_t response;

    memset(&message, 0, sizeof(message));
    message.kind = FILE_REQUEST_STREAM_SEEK;
    message.value = offset;
    return stream_request(&message, &response, timeout);
}

file_service_status_t file_task_stream_close(TickType_t timeout)
{
    file_request_t message;
    file_stream_response_t response;

    memset(&message, 0, sizeof(message));
    message.kind = FILE_REQUEST_STREAM_CLOSE;
    return stream_request(&message, &response, timeout);
}
