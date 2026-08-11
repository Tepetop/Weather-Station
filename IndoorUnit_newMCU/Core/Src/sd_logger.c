/**
 * @file sd_logger.c
 * @brief JSON Lines SD logger matching picoserver log_<station>.json format
 */

#include "sd_logger.h"

#include "debug_log.h"
#include "fatfs.h"
#include "wwdg.h"

#include <stdio.h>
#include <string.h>

#define SD_LOGGER_LINE_MAX 320U
#define SD_LOGGER_RETRY_PERIOD_MS 30000U

static uint8_t sd_ready = 0U;
static uint32_t sd_next_retry_tick = 0U;
static uint32_t sd_wwdg_last_kick_tick = 0U;
static char sd_line[SD_LOGGER_LINE_MAX];

static const uint8_t SD_FIELD_CHANNELS[] = {
    (uint8_t)WS_CH_SI7021_TEMP,
    (uint8_t)WS_CH_SI7021_HUM,
    (uint8_t)WS_CH_BMP280_TEMP,
    (uint8_t)WS_CH_BMP280_PRESS,
    (uint8_t)WS_CH_TSL2561_LUX,
    (uint8_t)WS_CH_BME280_TEMP,
    (uint8_t)WS_CH_BME280_PRESS,
    (uint8_t)WS_CH_BME280_HUM,
};

static const char *const SD_FIELD_NAMES[] = {
    "si7021_temp",
    "si7021_hum",
    "bmp280_temp",
    "bmp280_press",
    "tsl2561_lux",
    "bme280_temp",
    "bme280_press",
    "bme280_hum",
};

#define SD_FIELD_COUNT (sizeof(SD_FIELD_CHANNELS) / sizeof(SD_FIELD_CHANNELS[0]))

static void sd_logger_wwdg_kick(void) {
  uint32_t now;

  if (hwwdg.Instance == NULL) {
    return;
  }

  now = HAL_GetTick();
  if ((now - sd_wwdg_last_kick_tick) < 2U) {
    return;
  }
  if (HAL_WWDG_Refresh(&hwwdg) == HAL_OK) {
    sd_wwdg_last_kick_tick = now;
  }
}

static void sd_logger_mark_unavailable(const char *reason) {
  sd_ready = 0U;
  sd_next_retry_tick = HAL_GetTick() + SD_LOGGER_RETRY_PERIOD_MS;
  Debug_Log(reason);
}

static void sd_format_fixed(char *dst, size_t dst_size, float value, uint8_t decimals) {
  int32_t scale = 1;
  float scaled_f;
  int32_t scaled;
  int32_t abs_scaled;
  int32_t int_part;
  int32_t frac_part;

  for (uint8_t i = 0U; i < decimals; i++) {
    scale *= 10;
  }

  scaled_f = value * (float)scale;
  if (scaled_f >= 0.0f) {
    scaled_f += 0.5f;
  } else {
    scaled_f -= 0.5f;
  }

  scaled = (int32_t)scaled_f;
  abs_scaled = (scaled < 0) ? -scaled : scaled;
  int_part = abs_scaled / scale;
  frac_part = abs_scaled % scale;

  if (decimals == 0U) {
    (void)snprintf(dst, dst_size, "%s%ld", (scaled < 0) ? "-" : "", (long)int_part);
  } else {
    (void)snprintf(dst, dst_size, "%s%ld.%0*ld", (scaled < 0) ? "-" : "", (long)int_part,
                   (int)decimals, (long)frac_part);
  }
}

static void sd_format_status(char *dst, size_t dst_size, uint8_t sensor_status) {
  uint8_t known = sensor_status &
      ((uint8_t)WS_SENSOR_ERR_SI7021 | (uint8_t)WS_SENSOR_ERR_BMP280 |
       (uint8_t)WS_SENSOR_ERR_TSL2561 | (uint8_t)WS_SENSOR_ERR_BME280);
  size_t used;

  if ((dst == NULL) || (dst_size == 0U)) {
    return;
  }

  if (known == (uint8_t)WS_SENSOR_OK) {
    (void)snprintf(dst, dst_size, "OK");
    return;
  }

  /* Match picoserver _normalize_status: ERR:SI7021_BMP280 (underscores). */
  (void)snprintf(dst, dst_size, "ERR:");
  used = strlen(dst);

  if ((known & (uint8_t)WS_SENSOR_ERR_SI7021) != 0U) {
    (void)snprintf(dst + used, dst_size - used, "%sSI7021", (used == 4U) ? "" : "_");
    used = strlen(dst);
  }
  if ((known & (uint8_t)WS_SENSOR_ERR_BMP280) != 0U) {
    (void)snprintf(dst + used, dst_size - used, "%sBMP280", (used == 4U) ? "" : "_");
    used = strlen(dst);
  }
  if ((known & (uint8_t)WS_SENSOR_ERR_TSL2561) != 0U) {
    (void)snprintf(dst + used, dst_size - used, "%sTSL2561", (used == 4U) ? "" : "_");
    used = strlen(dst);
  }
  if ((known & (uint8_t)WS_SENSOR_ERR_BME280) != 0U) {
    (void)snprintf(dst + used, dst_size - used, "%sBME280", (used == 4U) ? "" : "_");
  }

  if (strlen(dst) <= 4U) {
    (void)snprintf(dst, dst_size, "ERR:UNKNOWN");
  }
}

static uint8_t sd_try_mount(void) {
  FRESULT fr;

  sd_logger_wwdg_kick();
  fr = f_mount(&USERFatFS, USERPath, 1);
  sd_logger_wwdg_kick();

  if (fr != FR_OK) {
    sd_logger_mark_unavailable("LOG:SD:MOUNT_FAIL");
    return 1U;
  }

  sd_ready = 1U;
  Debug_Log("LOG:SD:MOUNT_OK");
  return 0U;
}

uint8_t SD_Logger_Init(void) {
  return sd_try_mount();
}

uint8_t SD_Logger_IsReady(void) {
  return sd_ready;
}

static uint8_t sd_ensure_ready(void) {
  uint32_t now;

  if (sd_ready != 0U) {
    return 1U;
  }

  now = HAL_GetTick();
  if ((int32_t)(now - sd_next_retry_tick) < 0) {
    return 0U;
  }

  (void)f_mount(NULL, USERPath, 0);
  return (sd_try_mount() == 0U) ? 1U : 0U;
}

uint8_t SD_Logger_AppendMeasurement(uint8_t station_idx,
                                    const WS_Readings_t *readings,
                                    const DS3231_DateTime *rtc_now) {
  char path[20];
  char status[40];
  char value_text[16];
  FIL file;
  FRESULT fr;
  UINT written = 0U;
  int len = 0;
  uint8_t year = 0U;
  uint8_t month = 0U;
  uint8_t date = 0U;
  uint8_t hours = 0U;
  uint8_t minutes = 0U;
  uint8_t seconds = 0U;

  if (readings == NULL) {
    return 1U;
  }

  if (sd_ensure_ready() == 0U) {
    return 0U;
  }

  if (rtc_now != NULL) {
    year = rtc_now->year;
    month = rtc_now->month;
    date = rtc_now->date;
    hours = rtc_now->hours;
    minutes = rtc_now->minutes;
    seconds = rtc_now->seconds;
  }

  sd_format_status(status, sizeof(status), readings->sensor_status);

  len = snprintf(sd_line, sizeof(sd_line),
                 "{\"station_id\":\"S%u\",\"timestamp\":\"20%02u-%02u-%02uT%02u:%02u:%02u\",\"status\":\"%s\"",
                 (unsigned int)station_idx,
                 (unsigned int)year,
                 (unsigned int)month,
                 (unsigned int)date,
                 (unsigned int)hours,
                 (unsigned int)minutes,
                 (unsigned int)seconds,
                 status);
  if ((len <= 0) || ((size_t)len >= sizeof(sd_line))) {
    return 1U;
  }

  for (uint8_t i = 0U; i < (uint8_t)SD_FIELD_COUNT; i++) {
    float value = 0.0f;
    int part;

    if (WS_Reading_Get(readings, SD_FIELD_CHANNELS[i], &value)) {
      if (SD_FIELD_CHANNELS[i] == (uint8_t)WS_CH_TSL2561_LUX) {
        sd_format_fixed(value_text, sizeof(value_text), value, 0U);
      } else if ((SD_FIELD_CHANNELS[i] == (uint8_t)WS_CH_BMP280_PRESS) ||
                 (SD_FIELD_CHANNELS[i] == (uint8_t)WS_CH_BME280_PRESS)) {
        sd_format_fixed(value_text, sizeof(value_text), value, 2U);
      } else {
        sd_format_fixed(value_text, sizeof(value_text), value, 1U);
      }
      part = snprintf(sd_line + len, sizeof(sd_line) - (size_t)len, ",\"%s\":%s",
                      SD_FIELD_NAMES[i], value_text);
    } else {
      part = snprintf(sd_line + len, sizeof(sd_line) - (size_t)len, ",\"%s\":null",
                      SD_FIELD_NAMES[i]);
    }

    if ((part <= 0) || (((size_t)len + (size_t)part) >= sizeof(sd_line))) {
      return 1U;
    }
    len += part;
  }

  {
    int part = snprintf(sd_line + len, sizeof(sd_line) - (size_t)len, "}\n");
    if ((part <= 0) || (((size_t)len + (size_t)part) >= sizeof(sd_line))) {
      return 1U;
    }
    len += part;
  }

  (void)snprintf(path, sizeof(path), "%slog_S%u.json", USERPath, (unsigned int)station_idx);

  sd_logger_wwdg_kick();
  /* FatFs R0.11: OPEN_ALWAYS + seek end ≈ append. */
  fr = f_open(&file, path, FA_OPEN_ALWAYS | FA_WRITE);
  if (fr != FR_OK) {
    sd_logger_mark_unavailable("LOG:SD:OPEN_FAIL");
    return 0U;
  }
  fr = f_lseek(&file, f_size(&file));
  if (fr != FR_OK) {
    (void)f_close(&file);
    sd_logger_mark_unavailable("LOG:SD:SEEK_FAIL");
    return 0U;
  }

  sd_logger_wwdg_kick();
  fr = f_write(&file, sd_line, (UINT)len, &written);
  sd_logger_wwdg_kick();
  (void)f_close(&file);

  if ((fr != FR_OK) || (written != (UINT)len)) {
    sd_logger_mark_unavailable("LOG:SD:WRITE_FAIL");
    return 0U;
  }

  return 0U;
}
