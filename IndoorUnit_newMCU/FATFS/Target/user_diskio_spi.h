/**
 ******************************************************************************
 * @file user_diskio_spi.h
 * @brief FatFs SPI disk driver for an MMC/SD card.
 ******************************************************************************
 * Portions copyright (C) 2014, ChaN, all rights reserved.
 * Portions copyright (C) 2017, kiwih, all rights reserved.
 *
 * This software is free software and there is NO WARRANTY.
 * No restriction on use. You can use, modify and redistribute it for
 * personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
 * Redistributions of source code must retain the above copyright notice.
 ******************************************************************************
 */

#ifndef USER_DISKIO_SPI_H
#define USER_DISKIO_SPI_H

#include "diskio.h"
#include "ff_gen_drv.h"
#include "integer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USER_SPI_PDRV 0U

/** Card type flags returned by USER_SPI_get_card_type(). */
#define USER_SPI_CT_MMC   0x01U
#define USER_SPI_CT_SD1   0x02U
#define USER_SPI_CT_SD2   0x04U
#define USER_SPI_CT_BLOCK 0x08U

/** Initialization/transfer diagnostics returned by USER_SPI_get_last_error(). */
typedef enum {
  USER_SPI_ERR_NONE = 0,
  USER_SPI_ERR_BAD_DRIVE,
  USER_SPI_ERR_BAD_CONFIG,
  USER_SPI_ERR_HAL,
  USER_SPI_ERR_CMD0,
  USER_SPI_ERR_IFCOND,
  USER_SPI_ERR_INIT_TIMEOUT,
  USER_SPI_ERR_CMD58,
  USER_SPI_ERR_CMD16,
  USER_SPI_ERR_TIMEOUT
} USER_SPI_Error;

DSTATUS USER_SPI_initialize(BYTE pdrv);
DSTATUS USER_SPI_status(BYTE pdrv);
DRESULT USER_SPI_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
DRESULT USER_SPI_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif
#if _USE_IOCTL == 1
DRESULT USER_SPI_ioctl(BYTE pdrv, BYTE cmd, void *buff);
#endif

USER_SPI_Error USER_SPI_get_last_error(void);
BYTE USER_SPI_get_card_type(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_DISKIO_SPI_H */
