#ifndef RUNTIME_STATS_H
#define RUNTIME_STATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void platform_runtime_stats_init(void);
uint32_t platform_runtime_stats_get_counter(void);

#ifdef __cplusplus
}
#endif

#endif
