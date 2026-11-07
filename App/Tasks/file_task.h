#ifndef FILE_TASK_H
#define FILE_TASK_H

#include "file_service.h"

/* 文件任务对外接口：由UI单一提交者发起异步请求，通过结果队列接收事件。 */

/**
 * file_task_create() - 初始化文件任务及其请求和结果队列
 *
 * 使用静态存储创建资源，只能在初始化阶段调用一次。不挂载文件系统，
 * 返回成功不表示已检测到SD卡；创建失败时不回滚已创建的资源。
 *
 * Context: 系统启动流程在调度器启动前调用，不可在中断中调用。
 * Return: 队列及任务均创建成功返回%true，否则返回%false。
 */
bool file_task_create(void);

/**
 * file_task_request_list() - 非阻塞提交目录分页读取请求
 * @path: 以空字符结束的有效SD:/目录路径，长度小于FILE_SERVICE_PATH_CAP字节
 * @page: 页码，从0开始，每页最多FILE_SERVICE_PAGE_SIZE条
 * @request_id: 非空输出指针，提交成功时写入请求编号，失败时保持原值
 *
 * 路径在提交时复制，返回后调用方可以复用原字符串。成功只表示请求入队，
 * 目录读取及页码检查结果通过file_task_receive()接收。
 * 新请求使旧请求的后续结果投递失效，但不会取消已入队的删除；
 * 已在结果队列中的旧事件仍需调用方按编号过滤。
 *
 * Context: UI任务单一提交者，不支持多个任务并发提交，不可在中断中调用。
 * Return: 成功入队返回%true；路径或输出指针无效、队列未创建或已满时返回%false。
 */
bool file_task_request_list(const char *path, uint32_t page, uint32_t *request_id);

/**
 * file_task_request_delete() - 非阻塞提交文件或目录树删除请求
 * @path: 以空字符结束的有效SD:/路径，长度小于FILE_SERVICE_PATH_CAP字节，禁止根目录
 * @request_id: 非空输出指针，提交成功时写入请求编号，失败时保持原值
 *
 * 路径随请求复制。调用前应由上层完成用户确认；成功入队不代表删除成功，
 * 进度和最终状态通过file_task_receive()获取。目录删除包含隐藏项和子目录，
 * 可能部分完成后失败且不回滚。已入队删除不会被file_task_cancel()中止。
 *
 * Context: UI任务单一提交者，不支持多个任务并发提交，不可在中断中调用。
 * Return: 成功入队返回%true；路径或输出指针无效、目标为根目录、
 *         队列未创建或已满时返回%false。
 */
bool file_task_request_delete(const char *path, uint32_t *request_id);

/**
 * file_task_receive() - 非阻塞取出一条文件任务事件
 * @event: 非空输出指针，成功时写入完整事件，失败时保持原值
 *
 * 成功接收会从队列移除该事件。函数不自动过滤过期结果，调用方必须核对
 * 事件的request_id，并检查事件类型和状态；收到事件不等于对应操作成功。
 *
 * Context: UI任务上下文，不可在中断中调用，队列为空时立即返回。
 * Return: 成功取出一条事件返回%true；输出指针无效、队列未创建或为空时返回%false。
 */
bool file_task_receive(file_service_event_t *event);

/**
 * file_task_cancel() - 使当前匹配请求的后续结果失效
 * @request_id: 待取消请求的编号；与当前有效编号不匹配时不改变状态
 *
 * 浏览在后续取消检查点退出，不直接中止底层读写，也不等待操作结束。
 * 已入队删除仍执行至完成或错误，仅停止后续结果投递。此函数不清空队列，
 * 已入队结果仍可能被接收，调用方需要自行丢弃过期事件。
 *
 * Context: 任务上下文，不可在中断中调用，使用短临界区更新有效编号。
 */
void file_task_cancel(uint32_t request_id);

#endif
