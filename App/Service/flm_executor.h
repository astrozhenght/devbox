#ifndef FLM_EXECUTOR_H
#define FLM_EXECUTOR_H

#include <stdint.h>

#include "flm_parser.h"
#include "swd_target.h"
#include "target_profile.h"

typedef struct
{
    const flm_algorithm_t *algorithm;
    uint32_t load_address;
    uint32_t static_base;
    uint32_t return_address;
    uint32_t page_buffer;
    uint32_t stack_pointer;
} flm_executor_t;

swd_target_status_t flm_executor_load(flm_executor_t *executor,
                                      const flm_algorithm_t *algorithm,
                                      const target_profile_t *profile);
swd_target_status_t flm_executor_load_at(flm_executor_t *executor,
                                         const flm_algorithm_t *algorithm,
                                         uint32_t ram_start, uint32_t ram_size);
swd_target_status_t flm_executor_call(const flm_executor_t *executor,
                                      uint32_t function_offset,
                                      uint32_t argument0,
                                      uint32_t argument1,
                                      uint32_t argument2,
                                      uint32_t argument3,
                                      uint32_t timeout_ms,
                                      uint32_t *return_value);

#endif /* FLM_EXECUTOR_H */
