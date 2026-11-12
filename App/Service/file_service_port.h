#ifndef FILE_SERVICE_PORT_H
#define FILE_SERVICE_PORT_H

#include <stddef.h>
#include <stdint.h>

#include "file_service.h"

/* 仅由文件任务串行调用；同一时刻最多打开一个枚举句柄。 */
file_service_status_t file_port_mount(void);
file_service_status_t file_port_open_directory(const char *path);
file_service_status_t file_port_read_entry(file_service_entry_t *entry, bool *end);
file_service_status_t file_port_close_directory(void);
file_service_status_t file_port_stat(const char *path, file_service_entry_t *entry);
file_service_status_t file_port_remove(const char *path);
file_service_status_t file_port_open_file(const char *path, uint32_t *size_bytes);
file_service_status_t file_port_read_file(void *buffer, size_t capacity, size_t *read_bytes);
file_service_status_t file_port_seek_file(uint32_t offset);
file_service_status_t file_port_close_file(void);
int32_t file_port_last_error(void);
void file_port_yield(void);

#endif
