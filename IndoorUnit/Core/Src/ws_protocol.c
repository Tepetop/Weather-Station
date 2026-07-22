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
    case WS_CH_BME280_TEMP:
    case WS_CH_BME280_PRESS:
    case WS_CH_BME280_HUM:
      return (uint8_t)WS_SENSOR_ERR_BME280;
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
          {.channel_id = WS_CH_BME280_HUM, .value = 54.25f},
      },
  };
  uint8_t buf[WS_PROTOCOL_MAX_PAYLOAD];
  uint8_t len = 0U;
  uint8_t cmd[WS_CMD_SIZE];
  uint8_t cycle_id = 0U;

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
  float humidity = 0.0f;
  if (!WS_Reading_Get(&out, WS_CH_SI7021_TEMP, &temp) || (temp != 21.5f)) {
    return false;
  }
  if (!WS_Reading_Get(&out, WS_CH_BME280_HUM, &humidity) || (humidity != 54.25f)) {
    return false;
  }

  if (!WS_Cmd_EncodeMeasureTo(9U, 0x02U, cmd, sizeof(cmd))) {
    return false;
  }
  {
    uint8_t mask = 0U;
    if (!WS_Cmd_DecodeMeasureEx(cmd, sizeof(cmd), &cycle_id, &mask) ||
        (cycle_id != 9U) || (mask != 0x02U)) {
      return false;
    }
  }
  if (!WS_Cmd_IsDuplicateCycle(42U, 42U, 1U)) {
    return false;
  }
  if (WS_Cmd_IsDuplicateCycle(43U, 42U, 1U)) {
    return false;
  }
  if (WS_Cycle_ExpectedMask(2U) != 0x03U) {
    return false;
  }
  if (!WS_Cycle_IsComplete(0x03U, 0x03U) || WS_Cycle_IsComplete(0x03U, 0x01U)) {
    return false;
  }

  return true;
}

bool WS_Cmd_EncodeMeasure(uint8_t cycle_id, uint8_t *buf, uint8_t buf_size) {
  return WS_Cmd_EncodeMeasureTo(cycle_id, WS_CMD_TARGET_ALL, buf, buf_size);
}

bool WS_Cmd_EncodeMeasureTo(uint8_t cycle_id, uint8_t target_mask, uint8_t *buf, uint8_t buf_size) {
  if ((buf == NULL) || (buf_size < WS_CMD_SIZE)) {
    return false;
  }

  memset(buf, 0, WS_CMD_SIZE);
  buf[0] = WS_CMD_MEASURE;
  buf[WS_CMD_CYCLE_ID_OFFSET] = cycle_id;
  buf[WS_CMD_TARGET_MASK_OFFSET] = target_mask;
  return true;
}

bool WS_Cmd_DecodeMeasure(const uint8_t *buf, uint8_t len, uint8_t *out_cycle_id) {
  return WS_Cmd_DecodeMeasureEx(buf, len, out_cycle_id, NULL);
}

bool WS_Cmd_DecodeMeasureEx(const uint8_t *buf, uint8_t len, uint8_t *out_cycle_id, uint8_t *out_target_mask) {
  if ((buf == NULL) || (len < 2U) || (buf[0] != WS_CMD_MEASURE)) {
    return false;
  }

  if (out_cycle_id != NULL) {
    *out_cycle_id = buf[WS_CMD_CYCLE_ID_OFFSET];
  }
  if (out_target_mask != NULL) {
    *out_target_mask = (len > WS_CMD_TARGET_MASK_OFFSET) ? buf[WS_CMD_TARGET_MASK_OFFSET] : WS_CMD_TARGET_ALL;
    if (*out_target_mask == 0U) {
      *out_target_mask = WS_CMD_TARGET_ALL;
    }
  }
  return true;
}

bool WS_Cmd_IsDuplicateCycle(uint8_t cycle_id, uint8_t last_cycle_id, uint8_t have_last) {
  return (have_last != 0U) && (cycle_id == last_cycle_id);
}

uint8_t WS_Cycle_ExpectedMask(uint8_t node_count) {
  if (node_count == 0U) {
    return 0U;
  }
  if (node_count >= 8U) {
    return 0xFFU;
  }
  return (uint8_t)((1U << node_count) - 1U);
}

bool WS_Cycle_IsComplete(uint8_t expected_mask, uint8_t received_mask) {
  return ((received_mask & expected_mask) == expected_mask);
}
