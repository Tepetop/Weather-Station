/**
 * @file sd_logger.h
 * @brief Append measurement JSON Lines to SD (picoserver-compatible)
 * @details Requires MX_FATFS_Init(), SD_SPI_Bind(), then SD_Logger_Init().
 *          Missing or failed media is non-fatal: the station keeps running
 *          and retries the mount every 30 s.
 */

#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include "ds3231.h"
#include "ws_protocol.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mount the FatFs USER volume after MX_FATFS_Init() and SD_SPI_Bind().
 * @retval 0 Volume mounted.
 * @retval non-zero Link, SPI, or mount failed (system continues without SD).
 * @note Logs INIT_START, FRESULT, driver error, card type, capacity, INIT_OK
 *       on UART. Call after Debug_Init() so the messages are visible.
 */
uint8_t SD_Logger_Init(void);

/**
 * @brief Append one measurement for station Sn to `log_Sn.json`.
 * @param station_idx Node index (0 → S0).
 * @param readings    Decoded measurement payload.
 * @param rtc_now     Timestamp snapshot (NULL → zeros).
 * @retval 0 Success, or SD currently unavailable (non-blocking).
 * @retval non-zero Hard JSON format error (line would not fit).
 * @note I/O failures log the FatFs stage (OPEN/SEEK/WRITE/CLOSE) and code,
 *       then mark the volume unavailable until the retry period elapses.
 */
uint8_t SD_Logger_AppendMeasurement(uint8_t station_idx,
                                    const WS_Readings_t *readings,
                                    const DS3231_DateTime *rtc_now);

/**
 * @brief Report whether the volume is mounted and the last I/O succeeded.
 * @retval 1 Ready to append.
 * @retval 0 Unmounted, missing, or last operation failed.
 */
uint8_t SD_Logger_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_LOGGER_H */
