/* STM32 SDMMC DMA disk adapter. Derived from ST's BSD-3-Clause BSP template.
 * Copyright (c) 2019 STMicroelectronics. All rights reserved.
 * License: https://opensource.org/licenses/BSD-3-Clause
 */
#include "sd_diskio_dma.h"
#include "bsp_sd.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <string.h>

#define SD_TIMEOUT_MS    5000U
#define SD_BOUNCE_BLOCKS 8U

/* All transfers use AXI SRAM; never DMA into DTCM or unaligned caller buffers. */
static uint8_t dma_buffer[SD_BOUNCE_BLOCKS * 512U]
    __attribute__((section(".axisram"), aligned(32)));
static volatile DSTATUS status = STA_NOINIT;
static volatile bool read_done, write_done, transfer_error;
static bool initialized;

static void sd_wait_yield(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        vTaskDelay(pdMS_TO_TICKS(1));
}

static bool sd_wait_ready(void)
{
    uint32_t started = HAL_GetTick();
    do
    {
        if (BSP_SD_GetCardState(SD_Instance) == SD_TRANSFER_OK) return true;
        sd_wait_yield();
    } while (HAL_GetTick() - started < SD_TIMEOUT_MS);
    return false;
}

static void sd_abort_transfer(void)
{
    (void)HAL_SD_Abort(&hsd_sdmmc[SD_Instance]);
    status = STA_NOINIT;
}

DSTATUS SD_initialize(BYTE lun)
{
    (void)lun;
    status = STA_NOINIT;
    initialized = false;
    /* 无卡初始化也可能已改变HAL状态，重试不能仅依据上次是否成功。 */
    if (hsd_sdmmc[SD_Instance].Instance != NULL &&
        BSP_SD_DeInit(SD_Instance) != BSP_SD_ERROR_NONE) return status;
    read_done = write_done = transfer_error = false;
    if (BSP_SD_Init(SD_Instance) == BSP_SD_ERROR_NONE && sd_wait_ready())
    {
        initialized = true;
        status = 0U;
    }
    else if (hsd_sdmmc[SD_Instance].Instance != NULL)
    {
        /* 释放失败尝试留下的控制器和GPIO，使后续插卡可以重新初始化。 */
        (void)BSP_SD_DeInit(SD_Instance);
    }
    return status;
}

DSTATUS SD_status(BYTE lun)
{
    (void)lun;
    if (!initialized || status != 0U) return STA_NOINIT;
    if (BSP_SD_GetCardState(SD_Instance) != SD_TRANSFER_OK) status = STA_NOINIT;
    return status;
}

static DRESULT sd_transfer(BYTE *buffer, DWORD sector, UINT count, bool writing)
{
    if (buffer == NULL || count == 0U) return RES_PARERR;
    if (status != 0U || !initialized) return RES_NOTRDY;
    while (count != 0U)
    {
        UINT blocks = count > SD_BOUNCE_BLOCKS ? SD_BOUNCE_BLOCKS : count;
        if (!sd_wait_ready()) { sd_abort_transfer(); return RES_ERROR; }
        transfer_error = false;
        read_done = write_done = false;
        if (writing) memcpy(dma_buffer, buffer, blocks * 512U);
        __DSB();
        int32_t result = writing ?
            BSP_SD_WriteBlocks_DMA(SD_Instance, (uint32_t *)dma_buffer, sector, blocks) :
            BSP_SD_ReadBlocks_DMA(SD_Instance, (uint32_t *)dma_buffer, sector, blocks);
        if (result != BSP_SD_ERROR_NONE) { sd_abort_transfer(); return RES_ERROR; }
        uint32_t started = HAL_GetTick();
        while (!(writing ? write_done : read_done) && !transfer_error &&
               HAL_GetTick() - started < SD_TIMEOUT_MS) sd_wait_yield();
        if (transfer_error || !(writing ? write_done : read_done) || !sd_wait_ready())
        {
            sd_abort_transfer();
            return RES_ERROR;
        }
        __DSB();
        if (!writing) memcpy(buffer, dma_buffer, blocks * 512U);
        buffer += blocks * 512U;
        sector += blocks;
        count -= blocks;
    }
    return RES_OK;
}

DRESULT SD_read(BYTE lun, BYTE *buffer, DWORD sector, UINT count)
{
    (void)lun;
    return sd_transfer(buffer, sector, count, false);
}

DRESULT SD_write(BYTE lun, const BYTE *buffer, DWORD sector, UINT count)
{
    (void)lun;
    return sd_transfer((BYTE *)buffer, sector, count, true);
}

DRESULT SD_ioctl(BYTE lun, BYTE command, void *buffer)
{
    BSP_SD_CardInfo info;
    (void)lun;
    if (status != 0U) return RES_NOTRDY;
    if (command == CTRL_SYNC) return sd_wait_ready() ? RES_OK : RES_ERROR;
    if (buffer == NULL) return RES_PARERR;
    if (BSP_SD_GetCardInfo(SD_Instance, &info) != BSP_SD_ERROR_NONE) return RES_ERROR;
    switch (command)
    {
        case GET_SECTOR_COUNT: *(LBA_t *)buffer = info.LogBlockNbr; break;
        case GET_SECTOR_SIZE: *(WORD *)buffer = 512U; break;
        case GET_BLOCK_SIZE: *(DWORD *)buffer = 1U; break;
        default: return RES_PARERR;
    }
    return RES_OK;
}

const Diskio_drvTypeDef SD_Driver = {SD_initialize, SD_status, SD_read, SD_write, SD_ioctl};

void BSP_SD_WriteCpltCallback(uint32_t instance)
{
    if (instance == SD_Instance) write_done = true;
}

void BSP_SD_ReadCpltCallback(uint32_t instance)
{
    if (instance == SD_Instance) read_done = true;
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &hsd_sdmmc[SD_Instance]) transfer_error = true;
}

