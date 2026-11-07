#include "ff.h"
#include <stdbool.h>
#include <stddef.h>

#if FF_USE_LFN == 3

/* FatFS由唯一文件任务串行调用。独立有界工作区避免与Wi-Fi的libc malloc争用。 */
#define FATFS_NAME_SLOTS 2U
#define FATFS_NAME_BYTES ((FF_MAX_LFN + 1U) * sizeof(WCHAR) + \
    (FF_FS_EXFAT ? ((FF_MAX_LFN + 44U) / 15U * 32U) : 0U))

static unsigned char name_memory[FATFS_NAME_SLOTS][FATFS_NAME_BYTES]
    __attribute__((section(".axisram"), aligned(32)));
static bool name_used[FATFS_NAME_SLOTS];

void *ff_memalloc(UINT bytes)
{
    if (bytes > FATFS_NAME_BYTES) return NULL;
    for (unsigned i = 0; i < FATFS_NAME_SLOTS; i++)
    {
        if (!name_used[i])
        {
            name_used[i] = true;
            return name_memory[i];
        }
    }
    return NULL;
}

void ff_memfree(void *memory)
{
    for (unsigned i = 0; i < FATFS_NAME_SLOTS; i++)
    {
        if (memory == name_memory[i])
        {
            name_used[i] = false;
            return;
        }
    }
}

#endif
