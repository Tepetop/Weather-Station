/**
 * @file sd_spi.h
 * @brief SPI SD card low-level driver for FatFs diskio
 */

#ifndef SD_SPI_H
#define SD_SPI_H

#include "diskio.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void SD_SPI_Bind(SPI_HandleTypeDef *hspi);

DSTATUS SD_SPI_disk_initialize(BYTE pdrv);
DSTATUS SD_SPI_disk_status(BYTE pdrv);
DRESULT SD_SPI_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
DRESULT SD_SPI_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
DRESULT SD_SPI_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

#ifdef __cplusplus
}
#endif

#endif /* SD_SPI_H */
