#include "target_profile.h"

#include <stdbool.h>
#include <stddef.h>

static const target_profile_t stm32f103c8_profile =
{
    .name = "STM32F103C8T6",
    .default_flm_path = "SD:/DAP/Algorithms/STM32F10x_128.FLM",
    .dbg_device_id = 0x410U,
    .flash_start = 0x08000000U,
    .flash_size = 64U * 1024U,
    .ram_start = 0x20000000U,
    .ram_size = 20U * 1024U,
    .algorithm_clock_hz = 8000000U
};

const target_profile_t *target_profile_stm32f103c8(void)
{
    return &stm32f103c8_profile;
}

bool target_profile_matches(const target_profile_t *profile, uint32_t dbg_idcode)
{
    return (profile != NULL) && ((dbg_idcode & 0x0FFFU) == profile->dbg_device_id);
}
