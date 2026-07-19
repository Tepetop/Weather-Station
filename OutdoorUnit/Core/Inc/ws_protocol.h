/**
 * @file ws_protocol.h
 * @brief Shared measurement payload protocol for Weather Station nRF24 / UART
 *
 * nRF24 binary frame (max 32 B):
 *   [version][sensor_status][count][channel_id+float] * count
 *
 * UART line to Pico (example, BMP280 station):
 *   DATA:2026-05-09T11:06:01,S0,01:23.45,02:65.20,03:18.10,04:1013.25,05:120.0,OK\n
 *
 * Channel registry uses distinct IDs for BMP280 (0x03–0x04) and BME280 (0x06–0x08).
 * sensor_status bits align with measurement.h Sensor_Error_t (Si7021, BMP280, TSL2561, BME280).
 */

#ifndef WS_PROTOCOL_H
#define WS_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Protocol version byte in wire frame header */
#define WS_PROTOCOL_VERSION      0x01U
/** @brief Maximum nRF24 payload size (bytes) */
#define WS_PROTOCOL_MAX_PAYLOAD  32U
/** @brief Header size: version + sensor_status + count */
#define WS_PROTOCOL_HEADER_SIZE  3U
/** @brief Size of one reading record: channel_id (1 B) + float (4 B) */
#define WS_PROTOCOL_RECORD_SIZE  5U
/**
 * @brief Maximum number of readings per frame
 * @note 32 B payload allows at most 5 records: 3 + 5 * 5 = 28 B
 */
#define WS_MAX_READINGS          5U

/**
 * @brief Channel IDs: sensor type + physical quantity (fixed registry)
 */
typedef enum {
  WS_CH_SI7021_TEMP   = 0x01U,  /**< Si7021 temperature (°C) */
  WS_CH_SI7021_HUM    = 0x02U,  /**< Si7021 relative humidity (%) */
  WS_CH_BMP280_TEMP   = 0x03U,  /**< BMP280 temperature (°C) */
  WS_CH_BMP280_PRESS  = 0x04U,  /**< BMP280 pressure (hPa) */
  WS_CH_TSL2561_LUX   = 0x05U,  /**< TSL2561 illuminance (lux) */
  WS_CH_BME280_TEMP   = 0x06U,  /**< BME280 temperature (°C) */
  WS_CH_BME280_PRESS  = 0x07U,  /**< BME280 pressure (hPa) */
  WS_CH_BME280_HUM    = 0x08U,  /**< BME280 relative humidity (%) */
} WS_ChannelId_t;

/**
 * @brief Sensor error flags (bitwise, matches measurement.h Sensor_Error_t)
 */
typedef enum {
  WS_SENSOR_ERR_NONE     = 0U,          /**< No sensor errors */
  WS_SENSOR_ERR_SI7021   = (1U << 0),   /**< Si7021 error */
  WS_SENSOR_ERR_BMP280   = (1U << 1),   /**< BMP280 error */
  WS_SENSOR_ERR_TSL2561  = (1U << 2),   /**< TSL2561 error */
  WS_SENSOR_ERR_BME280   = (1U << 3),   /**< BME280 error */
} WS_SensorError_t;

/** @brief Alias for healthy sensor status (no error bits set) */
#define WS_SENSOR_OK WS_SENSOR_ERR_NONE

/**
 * @brief Single tagged sensor reading
 */
typedef struct {
  uint8_t channel_id;  /**< Channel ID (WS_ChannelId_t) */
  float value;         /**< Measured value in channel-specific units */
} WS_Reading_t;

/**
 * @brief Collection of tagged readings for one transmission frame
 */
typedef struct {
  uint8_t sensor_status;                  /**< Bitwise sensor health (WS_SensorError_t) */
  uint8_t count;                          /**< Number of valid entries in readings[] */
  WS_Reading_t readings[WS_MAX_READINGS]; /**< Tagged channel values */
} WS_Readings_t;

/**
 * @brief   Calculates encoded frame size for a given reading count
 * @param   count  Number of readings (clamped to WS_MAX_READINGS)
 * @retval  uint8_t  Required buffer size in bytes
 */
uint8_t WS_Protocol_MaxEncodedSize(uint8_t count);

/**
 * @brief   Encodes readings into binary wire format
 * @param   in       Source readings structure
 * @param   buf      Destination buffer
 * @param   buf_size Buffer capacity
 * @param   out_len  Receives encoded length on success
 * @retval  true     Encoding successful
 * @retval  false    Invalid parameters or buffer too small
 */
bool WS_Protocol_Encode(const WS_Readings_t *in, uint8_t *buf, uint8_t buf_size, uint8_t *out_len);

/**
 * @brief   Decodes binary wire format into readings structure
 * @param   buf  Source buffer
 * @param   len  Buffer length in bytes
 * @param   out  Destination readings structure
 * @retval  true     Decoding successful
 * @retval  false    Invalid parameters, version mismatch, or truncated frame
 */
bool WS_Protocol_Decode(const uint8_t *buf, uint8_t len, WS_Readings_t *out);

/**
 * @brief   Looks up a channel value in decoded readings
 * @param   r          Readings structure to search
 * @param   channel_id Channel ID to find (WS_ChannelId_t)
 * @param   out_value  Optional output for the value (may be NULL)
 * @retval  true       Channel found
 * @retval  false      Channel not present or invalid readings pointer
 */
bool WS_Reading_Get(const WS_Readings_t *r, uint8_t channel_id, float *out_value);

/**
 * @brief   Maps a channel ID to its sensor error flag
 * @param   channel_id  Channel ID (WS_ChannelId_t)
 * @retval  uint8_t     WS_SensorError_t bit for the owning sensor, or 0
 */
uint8_t WS_ChannelSensorError(uint8_t channel_id);

/**
 * @brief   Runs encode/decode round-trip self-test at startup
 * @retval  true   Self-check passed
 * @retval  false  Encode, decode, or value verification failed
 */
bool WS_Protocol_SelfCheck(void);

#endif /* WS_PROTOCOL_H */
