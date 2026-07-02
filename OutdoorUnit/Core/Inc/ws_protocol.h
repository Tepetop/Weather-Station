/**
 * @file ws_protocol.h
 * @brief Shared measurement payload protocol for Weather Station nRF24 / UART
 *
 * nRF24 binary frame (max 32 B):
 *   [version][sensor_status][count][channel_id+float] * count
 *
 * UART line to Pico (example):
 *   DATA:2026-05-09T11:06:01,S0,01:23.45,02:65.20,04:1013.25,OK\n
 */

#ifndef WS_PROTOCOL_H
#define WS_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WS_PROTOCOL_VERSION      0x01U
#define WS_PROTOCOL_MAX_PAYLOAD  32U
#define WS_PROTOCOL_HEADER_SIZE  3U
#define WS_PROTOCOL_RECORD_SIZE  5U
#define WS_MAX_READINGS          5U

/** Channel IDs: sensor type + physical quantity (fixed registry). */
typedef enum {
  WS_CH_SI7021_TEMP  = 0x01U,
  WS_CH_SI7021_HUM   = 0x02U,
  WS_CH_BMP280_TEMP  = 0x03U,
  WS_CH_BMP280_PRESS = 0x04U,
  WS_CH_TSL2561_LUX  = 0x05U,
} WS_ChannelId_t;

/** Sensor error flags (bitwise, matches legacy ERROR_SI7021 etc.). */
typedef enum {
  WS_SENSOR_ERR_NONE    = 0U,
  WS_SENSOR_ERR_SI7021  = (1U << 0),
  WS_SENSOR_ERR_BMP280  = (1U << 1),
  WS_SENSOR_ERR_TSL2561 = (1U << 2),
} WS_SensorError_t;

#define WS_SENSOR_OK WS_SENSOR_ERR_NONE

typedef struct {
  uint8_t channel_id;
  float value;
} WS_Reading_t;

typedef struct {
  uint8_t sensor_status;
  uint8_t count;
  WS_Reading_t readings[WS_MAX_READINGS];
} WS_Readings_t;

uint8_t WS_Protocol_MaxEncodedSize(uint8_t count);
bool WS_Protocol_Encode(const WS_Readings_t *in, uint8_t *buf, uint8_t buf_size, uint8_t *out_len);
bool WS_Protocol_Decode(const uint8_t *buf, uint8_t len, WS_Readings_t *out);
bool WS_Reading_Get(const WS_Readings_t *r, uint8_t channel_id, float *out_value);
uint8_t WS_ChannelSensorError(uint8_t channel_id);
bool WS_Protocol_SelfCheck(void);

#endif /* WS_PROTOCOL_H */
