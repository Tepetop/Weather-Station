/**
 * @file sd_logger.h
 * @brief Append measurement JSON Lines to SD (picoserver-compatible)
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
 * @brief Mount FatFs volume after MX_FATFS_Init / SPI bind
 * @retval 0 on success, non-zero on failure (system continues without SD)
 */
uint8_t SD_Logger_Init(void);

/**
 * @brief Append one measurement for station Sn to log_Sn.json
 * @param station_idx Node index (0 → S0)
 * @param readings Decoded measurement payload
 * @param rtc_now Timestamp snapshot (may be NULL → zeros)
 * @retval 0 on success or SD unavailable (non-blocking), non-zero on hard format error
 */
uint8_t SD_Logger_AppendMeasurement(uint8_t station_idx,
                                    const WS_Readings_t *readings,
                                    const DS3231_DateTime *rtc_now);

/**
 * @brief True if volume is mounted and last ops succeeded
 */
uint8_t SD_Logger_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_LOGGER_H */
