/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*                                                                       */
/*   Portions COPYRIGHT 2017 STMicroelectronics                          */
/*   Portions Copyright (C) 2019, ChaN, all right reserved               */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */
#include "ff_gen_drv.h"
#include "rtc.h"

#if defined ( __GNUC__ )
#ifndef __weak
#define __weak __attribute__((weak))
#endif
#endif

extern Disk_drvTypeDef  disk;

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
    DSTATUS stat;

    stat = disk.drv[pdrv]->disk_status(disk.lun[pdrv]);
    return stat;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
    DSTATUS stat = RES_OK;

    if(disk.is_initialized[pdrv] == 0)
    {
        stat = disk.drv[pdrv]->disk_initialize(disk.lun[pdrv]);
        if(stat == RES_OK)
        {
            disk.is_initialized[pdrv] = 1;
        }
    }
    return stat;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
    DRESULT res;

    res = disk.drv[pdrv]->disk_read(disk.lun[pdrv], buff, sector, count);
    return res;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0
#if _USE_WRITE == 1
DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
    DRESULT res;

    res = disk.drv[pdrv]->disk_write(disk.lun[pdrv], buff, sector, count);
    return res;
}
#endif /* _USE_WRITE == 1 */
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
#if _USE_IOCTL == 1
DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
    DRESULT res;

    res = disk.drv[pdrv]->disk_ioctl(disk.lun[pdrv], cmd, buff);
    return res;
}
#endif /* _USE_IOCTL == 1 */

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
__weak DWORD get_fattime(void)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};
    DWORD fatTime = 0;

    // 获取当前时间和日期
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    // 将时间转换为FAT格式
    fatTime = ((DWORD)(sDate.Year + 20) << 25) |   // 年份
              ((DWORD)sDate.Month << 21) |         // 月份
              ((DWORD)sDate.Date << 16) |          // 日
              ((DWORD)sTime.Hours << 11) |         // 小时
              ((DWORD)sTime.Minutes << 5) |        // 分钟
              ((DWORD)sTime.Seconds >> 1);         // 秒/2

    return fatTime;
}

 /************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
