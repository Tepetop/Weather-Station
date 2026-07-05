/**
 * @file ws_protocol.c
 * @brief Encode/decode for Weather Station measurement payloads
 */

#include "ws_protocol.h"

#include <string.h>

uint8_t WS_Protocol_MaxEncodedSize(uint8_t count) {
  if (count > WS_MAX_READINGS) {
    count = WS_MAX_READINGS;
  }
  return (uint8_t)(WS_PROTOCOL_HEADER_SIZE + (count * WS_PROTOCOL_RECORD_SIZE));
}

bool WS_Protocol_Encode(const WS_Readings_t *in, uint8_t *buf, uint8_t buf_size, uint8_t *out_len) {
  if ((in == NULL) || (buf == NULL) || (out_len == NULL) || (in->count > WS_MAX_READINGS)) {
    return false;
  }

  uint8_t needed = WS_Protocol_MaxEncodedSize(in->count);
  if (buf_size < needed) {
    return false;
  }

  buf[0] = WS_PROTOCOL_VERSION;
  buf[1] = in->sensor_status;
  buf[2] = in->count;

  for (uint8_t i = 0U; i < in->count; i++) {
    uint8_t off = (uint8_t)(WS_PROTOCOL_HEADER_SIZE + (i * WS_PROTOCOL_RECORD_SIZE));
    buf[off] = in->readings[i].channel_id;
    memcpy(&buf[off + 1U], &in->readings[i].value, sizeof(float));
  }

  *out_len = needed;
  return true;
}

bool WS_Protocol_Decode(const uint8_t *buf, uint8_t len, WS_Readings_t *out) {
  if ((buf == NULL) || (out == NULL) || (len < WS_PROTOCOL_HEADER_SIZE)) {
    return false;
  }

  if (buf[0] != WS_PROTOCOL_VERSION) {
    return false;
  }

  uint8_t count = buf[2];
  if (count > WS_MAX_READINGS) {
    return false;
  }

  uint8_t needed = WS_Protocol_MaxEncodedSize(count);
  if (len < needed) {
    return false;
  }

  memset(out, 0, sizeof(*out));
  out->sensor_status = buf[1];
  out->count = count;

  for (uint8_t i = 0U; i < count; i++) {
    uint8_t off = (uint8_t)(WS_PROTOCOL_HEADER_SIZE + (i * WS_PROTOCOL_RECORD_SIZE));
    out->readings[i].channel_id = buf[off];
    memcpy(&out->readings[i].value, &buf[off + 1U], sizeof(float));
  }

  return true;
}

bool WS_Reading_Get(const WS_Readings_t *r, uint8_t channel_id, float *out_value) {
  if (r == NULL) {
    return false;
  }

  for (uint8_t i = 0U; i < r->count; i++) {
    if (r->readings[i].channel_id == channel_id) {
      if (out_value != NULL) {
        *out_value = r->readings[i].value;
      }
      return true;
    }
  }

  return false;
}

uint8_t WS_ChannelSensorError(uint8_t channel_id) {
  switch (channel_id) {
    case WS_CH_SI7021_TEMP:
    case WS_CH_SI7021_HUM:
      return (uint8_t)WS_SENSOR_ERR_SI7021;
    case WS_CH_BMP280_TEMP:
    case WS_CH_BMP280_PRESS:
      return (uint8_t)WS_SENSOR_ERR_BMP280;
    case WS_CH_TSL2561_LUX:
      return (uint8_t)WS_SENSOR_ERR_TSL2561;
    default:
      return 0U;
  }
}

bool WS_Protocol_SelfCheck(void) {
  WS_Readings_t in = {
      .sensor_status = 0U,
      .count = 2U,
      .readings = {
          {.channel_id = WS_CH_SI7021_TEMP, .value = 21.5f},
          {.channel_id = WS_CH_BMP280_PRESS, .value = 1013.25f},
      },
  };
  uint8_t buf[WS_PROTOCOL_MAX_PAYLOAD];
  uint8_t len = 0U;

  if (!WS_Protocol_Encode(&in, buf, sizeof(buf), &len)) {
    return false;
  }

  WS_Readings_t out;
  if (!WS_Protocol_Decode(buf, len, &out)) {
    return false;
  }

  if ((out.count != 2U) || (out.sensor_status != 0U)) {
    return false;
  }

  float temp = 0.0f;
  float press = 0.0f;
  if (!WS_Reading_Get(&out, WS_CH_SI7021_TEMP, &temp) || (temp != 21.5f)) {
    return false;
  }
  if (!WS_Reading_Get(&out, WS_CH_BMP280_PRESS, &press) || (press != 1013.25f)) {
    return false;
  }

  return true;
}
