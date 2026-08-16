/**
 * @file sd_spi.h
 * @brief SPI SD card low-level driver for FatFs diskio
 * @details Shares SPI1 with the PCD8544 LCD. Call SD_SPI_Bind() after
 *          MX_SPI1_Init() and before SD_Logger_Init() / f_mount().
 */

#ifndef SD_SPI_H
#define SD_SPI_H

#include "diskio.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Physical drive number of the USER FatFs volume. */
#define SD_SPI_PDRV 0U

/** @name Card type codes returned by SD_SPI_GetCardType() */
/** @{ */
#define SD_SPI_TYPE_NONE 0U /**< Not identified / init failed */
#define SD_SPI_TYPE_SD1  1U /**< SDv1, byte addressed */
#define SD_SPI_TYPE_SD2  2U /**< SDv2 SDSC, byte addressed */
#define SD_SPI_TYPE_SDHC 3U /**< SDv2 SDHC/SDXC, block addressed */
/** @} */

/** @name Last init/bind error codes from SD_SPI_GetLastError() */
/** @{ */
#define SD_SPI_ERR_NONE        0U /**< No error */
#define SD_SPI_ERR_NO_HANDLE   1U /**< SPI handle missing or not bound */
#define SD_SPI_ERR_BAD_SPI_CFG 2U /**< SPI mode/size/NSS not SD-compatible */
#define SD_SPI_ERR_BAD_PDRV    3U /**< Unexpected FatFs physical drive */
#define SD_SPI_ERR_HAL         4U /**< HAL_SPI transfer failed */
#define SD_SPI_ERR_CMD0        5U /**< GO_IDLE_STATE did not return idle */
#define SD_SPI_ERR_CMD8        6U /**< SEND_IF_COND failed unexpectedly */
#define SD_SPI_ERR_IFCOND      7U /**< CMD8 voltage/check pattern mismatch */
#define SD_SPI_ERR_ACMD41      8U /**< Card did not leave idle (ACMD41) */
#define SD_SPI_ERR_CMD58       9U /**< READ_OCR failed */
#define SD_SPI_ERR_CMD16       10U /**< SET_BLOCKLEN 512 failed */
#define SD_SPI_ERR_TIMEOUT     11U /**< Card busy / response timeout */
/** @} */

/**
 * @brief Bind the SD driver to an already-initialized SPI handle.
 * @param hspi SPI master used by the card (SPI1, shared with LCD).
 * @retval 0 Handle accepted.
 * @retval 1 Handle or SPI parameters rejected; see SD_SPI_GetLastError().
 * @note Requires SPI mode 0 (CPOL=0, CPHA=0), 8-bit, MSB first, software NSS.
 *       Application baud is saved and restored after the <=400 kHz init phase.
 */
uint8_t SD_SPI_Bind(SPI_HandleTypeDef *hspi);

/**
 * @brief Last bind/initialize error (sticky until the next bind/init).
 * @retval One of SD_SPI_ERR_*.
 */
uint8_t SD_SPI_GetLastError(void);

/**
 * @brief Card type detected by the last successful initialize.
 * @retval One of SD_SPI_TYPE_*.
 */
uint8_t SD_SPI_GetCardType(void);

/**
 * @brief FatFs disk_initialize() implementation.
 * @param pdrv Physical drive (must be SD_SPI_PDRV).
 * @retval 0 Ready.
 * @retval STA_NOINIT Card missing, rejected, or SPI not bound.
 */
DSTATUS SD_SPI_disk_initialize(BYTE pdrv);

/**
 * @brief FatFs disk_status() implementation.
 * @param pdrv Physical drive (must be SD_SPI_PDRV).
 * @retval Current DSTATUS flags.
 */
DSTATUS SD_SPI_disk_status(BYTE pdrv);

/**
 * @brief FatFs disk_read() implementation.
 * @param pdrv   Physical drive (must be SD_SPI_PDRV).
 * @param buff   Destination, @p count * 512 bytes.
 * @param sector Start LBA.
 * @param count  Number of 512-byte sectors (non-zero).
 * @retval RES_OK on success.
 */
DRESULT SD_SPI_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);

/**
 * @brief FatFs disk_write() implementation.
 * @param pdrv   Physical drive (must be SD_SPI_PDRV).
 * @param buff   Source, @p count * 512 bytes.
 * @param sector Start LBA.
 * @param count  Number of 512-byte sectors (non-zero).
 * @retval RES_OK on success.
 */
DRESULT SD_SPI_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);

/**
 * @brief FatFs disk_ioctl() implementation.
 * @param pdrv Physical drive (must be SD_SPI_PDRV).
 * @param cmd  CTRL_SYNC, GET_SECTOR_COUNT, GET_SECTOR_SIZE, GET_BLOCK_SIZE.
 * @param buff Command-specific payload; required except for CTRL_SYNC.
 * @retval RES_OK on success.
 */
DRESULT SD_SPI_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

#ifdef __cplusplus
}
#endif

#endif /* SD_SPI_H */
