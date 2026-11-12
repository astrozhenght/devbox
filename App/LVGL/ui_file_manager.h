#ifndef UI_FILE_MANAGER_H
#define UI_FILE_MANAGER_H

#include "lvgl.h"

#include "file_service.h"

typedef void (*ui_file_picker_callback_t)(const char *path, void *user);

void ui_file_manager_create(lv_obj_t *parent);
void ui_file_manager_create_picker(lv_obj_t *parent, const char *start_path,
                                   const char *extension,
                                   ui_file_picker_callback_t callback,
                                   void *user);

#endif
