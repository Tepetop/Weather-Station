/**
 * @file debug_log.c
 * @brief Simple UART debug logging implementation
 */

#include "debug_log.h"

#ifdef DEBUG_LOG_ENABLE

#include "usart.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;

/* ============================================================================
 * PRIVATE VARIABLES
 * ========================================================================== */

static char debug_buffer[128];
static uint32_t last_heartbeat_tick = 0;
static uint32_t heartbeat_count = 0;

/* ============================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static void debug_send(const char *str, uint16_t len) {
  HAL_UART_Transmit(&huart1, (uint8_t *)str, len, 100);
}

static void debug_format_uptime(uint32_t *hours, uint32_t *minutes, uint32_t *seconds) {
  uint32_t total_sec = HAL_GetTick() / 1000U;
  *hours = (total_sec / 3600U) % 24U;
  *minutes = (total_sec / 60U) % 60U;
  *seconds = total_sec % 60U;
}

static void debug_print_timestamped(const char *msg) {
  uint32_t hours;
  uint32_t minutes;
  uint32_t seconds;

  debug_format_uptime(&hours, &minutes, &seconds);
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[%02lu:%02lu:%02lu] %s\r\n",
                     (unsigned long)hours, (unsigned long)minutes,
                     (unsigned long)seconds, msg);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_send(debug_buffer, (uint16_t)len);
  }
}

static void debug_log_reset_cause(void) {
  uint8_t cause_count = 0U;

  Debug_LogHex("LOG:RESET:CSR=", RCC->CSR);

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET) {
    Debug_Log("LOG:RESET:CAUSE=WWDG");
    cause_count++;
  }

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) {
    Debug_Log("LOG:RESET:CAUSE=IWDG");
    cause_count++;
  }

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET) {
    Debug_Log("LOG:RESET:CAUSE=SOFT");
    cause_count++;
  }

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET) {
    Debug_Log("LOG:RESET:CAUSE=POR_PDR");
    cause_count++;
  }

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET) {
    Debug_Log("LOG:RESET:CAUSE=PIN");
    cause_count++;
  }

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET) {
    Debug_Log("LOG:RESET:CAUSE=LPWR");
    cause_count++;
  }

  if (cause_count == 0U) {
    Debug_Log("LOG:RESET:CAUSE=UNKNOWN");
  } else if (cause_count > 1U) {
    Debug_LogValue("LOG:RESET:CAUSE_COUNT=", cause_count);
  }

  __HAL_RCC_CLEAR_RESET_FLAGS();
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION
 * ========================================================================== */

void Debug_Init(void) {
  last_heartbeat_tick = HAL_GetTick();
  heartbeat_count = 0;
  Debug_LogBoot();
}

void Debug_Log(const char *msg) {
  if (msg == NULL) return;
  debug_print_timestamped(msg);
}

void Debug_LogValue(const char *msg, int32_t value) {
  if (msg == NULL) return;

  uint32_t hours;
  uint32_t minutes;
  uint32_t seconds;
  debug_format_uptime(&hours, &minutes, &seconds);

  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[%02lu:%02lu:%02lu] %s%ld\r\n",
                     (unsigned long)hours, (unsigned long)minutes,
                     (unsigned long)seconds, msg, (long)value);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_send(debug_buffer, (uint16_t)len);
  }
}

void Debug_LogHex(const char *msg, uint32_t value) {
  if (msg == NULL) return;

  uint32_t hours;
  uint32_t minutes;
  uint32_t seconds;
  debug_format_uptime(&hours, &minutes, &seconds);

  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[%02lu:%02lu:%02lu] %s0x%08lX\r\n",
                     (unsigned long)hours, (unsigned long)minutes,
                     (unsigned long)seconds, msg, (unsigned long)value);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_send(debug_buffer, (uint16_t)len);
  }
}

#ifdef DEBUG_LOG_NRF_EVENTS
void Debug_LogNrfInit(uint8_t success) {
  Debug_Log(success ? "LOG:NRF:INIT_OK" : "LOG:NRF:INIT_FAIL");
}

void Debug_LogNrfListening(void) {
  Debug_Log("LOG:NRF:LISTENING");
}

void Debug_LogNrfRxCmd(void) {
  Debug_Log("LOG:NRF:RX_CMD_MEASURE");
}

void Debug_LogNrfTxResult(uint8_t success) {
  Debug_Log(success ? "LOG:NRF:TX_OK (ACK received)" : "LOG:NRF:TX_FAIL (MAX_RT)");
}

void Debug_LogNrfTimeout(void) {
  Debug_Log("LOG:NRF:TX_TIMEOUT");
}
#else
void Debug_LogNrfInit(uint8_t success) { (void)success; }
void Debug_LogNrfListening(void) {}
void Debug_LogNrfRxCmd(void) {}
void Debug_LogNrfTxResult(uint8_t success) { (void)success; }
void Debug_LogNrfTimeout(void) {}
#endif

#ifdef DEBUG_LOG_INIT_EVENTS
void Debug_LogSystemReady(uint8_t sensor_error_code) {
  if (sensor_error_code == 0U) {
    Debug_Log("LOG:INIT:SYSTEM_OK");
  } else {
    Debug_LogHex("LOG:INIT:SENSOR_ERRORS=0x", sensor_error_code);
  }
}

void Debug_LogSensorError(uint8_t error_flag, const char *name) {
  if (name == NULL) return;
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:INIT:SENSOR_FAIL %s (0x%02X)", name, error_flag);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_buffer[len] = '\0';
    Debug_Log(debug_buffer);
  }
}

void Debug_LogI2cDevice(uint8_t addr) {
  Debug_LogHex("LOG:INIT:I2C_DEVICE=0x", addr);
}
#else
void Debug_LogSystemReady(uint8_t sensor_error_code) { (void)sensor_error_code; }
void Debug_LogSensorError(uint8_t error_flag, const char *name) {
  (void)error_flag;
  (void)name;
}
void Debug_LogI2cDevice(uint8_t addr) { (void)addr; }
#endif

#ifdef DEBUG_LOG_MEAS_EVENTS
void Debug_LogMeasCmd(void) {
  Debug_Log("LOG:MEAS:CMD_RECEIVED");
}

void Debug_LogMeasDone(void) {
  Debug_Log("LOG:MEAS:DONE");
}

void Debug_LogMeasRetry(uint8_t attempt, uint8_t max) {
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:MEAS:RETRY %u/%u", attempt, max);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_buffer[len] = '\0';
    Debug_Log(debug_buffer);
  }
}

void Debug_LogMeasNoSensors(void) {
  Debug_Log("LOG:MEAS:NO_SENSORS");
}

void Debug_LogMeasMaxRetries(void) {
  Debug_Log("LOG:MEAS:MAX_RETRIES");
}

void Debug_LogMeasTimeout(void) {
  Debug_Log("LOG:MEAS:TIMEOUT");
}
#else
void Debug_LogMeasCmd(void) {}
void Debug_LogMeasDone(void) {}
void Debug_LogMeasRetry(uint8_t attempt, uint8_t max) {
  (void)attempt;
  (void)max;
}
void Debug_LogMeasNoSensors(void) {}
void Debug_LogMeasMaxRetries(void) {}
void Debug_LogMeasTimeout(void) {}
#endif

#ifdef DEBUG_LOG_LINK_EVENTS
void Debug_LogRecovery(void) {
  Debug_Log("LOG:LINK:RECOVERY_DONE");
}

void Debug_LogElapsedMs(uint32_t ms) {
  Debug_LogValue("LOG:LINK:ELAPSED_MS=", (int32_t)ms);
}
#else
void Debug_LogRecovery(void) {}
void Debug_LogElapsedMs(uint32_t ms) { (void)ms; }
#endif

#ifdef DEBUG_LOG_HEARTBEAT
void Debug_Heartbeat(void) {
  uint32_t now = HAL_GetTick();
  if ((now - last_heartbeat_tick) >= DEBUG_HEARTBEAT_INTERVAL_MS) {
    last_heartbeat_tick = now;
    heartbeat_count++;
    Debug_LogValue("LOG:HEARTBEAT #", heartbeat_count);
  }
}
#else
void Debug_Heartbeat(void) {}
#endif

void Debug_LogBoot(void) {
  Debug_Log("LOG:OUTDOOR UNIT BOOT");
  Debug_Log("LOG:Debug logging enabled");
  debug_log_reset_cause();
}

#endif /* DEBUG_LOG_ENABLE */
