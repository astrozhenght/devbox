#ifndef TARGET_PROFILE_H
#define TARGET_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    const char *name;
    const char *default_flm_path;
    uint32_t dbg_device_id;
    uint32_t flash_start;
    uint32_t flash_size;
    uint32_t ram_start;
    uint32_t ram_size;
    uint32_t algorithm_clock_hz;
} target_profile_t;

const target_profile_t *target_profile_stm32f103c8(void);
bool target_profile_matches(const target_profile_t *profile, uint32_t dbg_idcode);

#endif /* TARGET_PROFILE_H */
