/**
 * @file debug_log.c
 * @brief UART debug logging implementation
 * @details Formats timestamped lines from the DS3231 snapshot (`rtcNow`) and
 *          sends them on USART1. Compiled only when `DEBUG_LOG_ENABLE` is set.
 *          Timestamp format: `LOG:[YYYY-MM-DD HH:MM:SS] ...`.
 */

#include "debug_log.h"

#ifdef DEBUG_LOG_ENABLE

#include "usart.h"
#include "ds3231.h"
#include "sd_logger.h"
#include <stdio.h>
#include <string.h>

/** Current RTC date/time snapshot updated by `DS3231_EventHandler()`. */
extern DS3231_DateTime rtcNow;
/** USART1 handle used for blocking debug TX. */
extern UART_HandleTypeDef huart1;

/* ============================================================================
 * PRIVATE VARIABLES
 * ========================================================================== */

/** Scratch buffer for a single formatted log line. */
static char debug_buffer[128];
/** `HAL_GetTick()` of the last heartbeat line. */
static uint32_t last_heartbeat_tick = 0;
/** Number of heartbeat lines emitted since `Debug_Init()`. */
static uint32_t heartbeat_count = 0;

/* ============================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

/**
 * @brief Send a buffer via UART (blocking)
 * @param[in] str Bytes to transmit
 * @param[in] len Number of bytes
 */
static void debug_send(const char *str, uint16_t len) {
  HAL_UART_Transmit(&huart1, (uint8_t *)str, len, 100);
}

/**
 * @brief Send a formatted line on UART and append it to SD `status_log`.
 * @param[in] str Bytes to emit
 * @param[in] len Number of bytes
 */
static void debug_emit(const char *str, uint16_t len) {
  debug_send(str, len);
  (void)SD_Logger_AppendStatus(str, len);
}

/**
 * @brief Format and send a message with a date/time prefix
 * @param[in] msg NULL-terminated payload after the timestamp
 * @details Drops the line if `snprintf` fails or the result does not fit
 *          in `debug_buffer`.
 */
static void debug_print_timestamped(const char *msg) {
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[20%02u-%02u-%02u %02u:%02u:%02u] %s\r\n",
                     rtcNow.year, rtcNow.month, rtcNow.date,
                     rtcNow.hours, rtcNow.minutes, rtcNow.seconds, msg);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_emit(debug_buffer, (uint16_t)len);
  }
}

/**
 * @brief Log reset source flags latched in RCC CSR since the previous boot
 * @details Emits the raw CSR value, then each set cause flag, and clears
 *          the reset flags afterwards.
 */
static void debug_log_reset_cause(void) {
  uint8_t cause_count = 0U;

  Debug_LogHex("RESET:CSR=", RCC->CSR);

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET) {
    Debug_Log("RESET:CAUSE=WWDG");
    cause_count++;
  }

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) {
    Debug_Log("RESET:CAUSE=IWDG");
    cause_count++;
  }

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET) {
    Debug_Log("RESET:CAUSE=SOFT");
    cause_count++;
  }

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET) {
    Debug_Log("RESET:CAUSE=POR_PDR");
    cause_count++;
  }

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET) {
    Debug_Log("RESET:CAUSE=PIN");
    cause_count++;
  }

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET) {
    Debug_Log("RESET:CAUSE=LPWR");
    cause_count++;
  }

  if (cause_count == 0U) {
    Debug_Log("RESET:CAUSE=UNKNOWN");
  } else if (cause_count > 1U) {
    Debug_LogValue("RESET:CAUSE_COUNT=", cause_count);
  }

  __HAL_RCC_CLEAR_RESET_FLAGS();
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION
 * ========================================================================== */

/**
 * @brief Initialize debug logging
 * @details Resets heartbeat state and emits the boot / reset-cause lines.
 *          Call after UART and RTC are up.
 */
void Debug_Init(void) {
  last_heartbeat_tick = HAL_GetTick();
  heartbeat_count = 0;
  Debug_LogBoot();
}

/**
 * @brief Log a simple message
 * @param[in] msg NULL-terminated message string
 */
void Debug_Log(const char *msg) {
  if (msg == NULL) return;
  debug_print_timestamped(msg);
}

/**
 * @brief Log a message with a single integer value
 * @param[in] msg Message prefix (NULL-terminated)
 * @param[in] value Integer value to append
 */
void Debug_LogValue(const char *msg, int32_t value) {
  if (msg == NULL) return;
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[20%02u-%02u-%02u %02u:%02u:%02u] %s%ld\r\n",
                     rtcNow.year, rtcNow.month, rtcNow.date,
                     rtcNow.hours, rtcNow.minutes, rtcNow.seconds,
                     msg, (long)value);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_emit(debug_buffer, (uint16_t)len);
  }
}

/**
 * @brief Log a message with a hex value
 * @param[in] msg Message prefix (NULL-terminated)
 * @param[in] value Hex value to append
 */
void Debug_LogHex(const char *msg, uint32_t value) {
  if (msg == NULL) return;
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[20%02u-%02u-%02u %02u:%02u:%02u] %s0x%08lX\r\n",
                     rtcNow.year, rtcNow.month, rtcNow.date,
                     rtcNow.hours, rtcNow.minutes, rtcNow.seconds,
                     msg, (unsigned long)value);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_emit(debug_buffer, (uint16_t)len);
  }
}

/**
 * @brief Log RTC alarm 1 event (screen update)
 * @note Compiled only when `DEBUG_LOG_RTC1_EVENTS` is defined.
 */
void Debug_LogRtcAlarm1(void) {
#ifdef DEBUG_LOG_RTC1_EVENTS
  Debug_Log("RTC:ALM1 (screen update)");
#endif
}

/**
 * @brief Log RTC alarm 2 event (measurement trigger)
 * @note Compiled only when `DEBUG_LOG_RTC2_EVENTS` is defined.
 */
void Debug_LogRtcAlarm2(void) {
#ifdef DEBUG_LOG_RTC2_EVENTS
  Debug_Log("RTC:ALM2 (measurement trigger)");
#endif
}

#ifdef DEBUG_LOG_NRF_EVENTS
/**
 * @brief Log NRF TX start
 * @param[in] node_idx Node index being addressed
 */
void Debug_LogNrfTxStart(uint8_t node_idx) {
  Debug_LogValue("NRF:TX_START node=", node_idx);
}

/**
 * @brief Log NRF TX result
 * @param[in] success 1 if TX succeeded, 0 if failed
 */
void Debug_LogNrfTxResult(uint8_t success) {
  if (success) {
    Debug_Log("NRF:TX_OK (ACK received)");
  } else {
    Debug_Log("NRF:TX_FAIL (MAX_RT)");
  }
}

/**
 * @brief Log NRF NoAck TX success (TX_DS without peer ACK)
 */
void Debug_LogNrfTxNoAck(void) {
  Debug_Log("NRF:TX_OK (NoAck sent)");
}

/**
 * @brief Log NRF RX data received
 * @param[in] node_idx Node index data came from
 */
void Debug_LogNrfRxData(uint8_t node_idx) {
  Debug_LogValue("NRF:RX_DATA from node=", node_idx);
}

/**
 * @brief Log NRF timeout
 * @param[in] is_tx 1 if TX timeout, 0 if RX timeout
 */
void Debug_LogNrfTimeout(uint8_t is_tx) {
  if (is_tx) {
    Debug_Log("NRF:TX_TIMEOUT");
  } else {
    Debug_Log("NRF:RX_TIMEOUT (no response)");
  }
}
#else
void Debug_LogNrfTxStart(uint8_t node_idx) { (void)node_idx; }
void Debug_LogNrfTxResult(uint8_t success) { (void)success; }
void Debug_LogNrfTxNoAck(void) {}
void Debug_LogNrfRxData(uint8_t node_idx) { (void)node_idx; }
void Debug_LogNrfTimeout(uint8_t is_tx) { (void)is_tx; }
#endif

#ifdef DEBUG_LOG_MENU_EVENTS
/**
 * @brief Log a menu action
 * @param[in] action_name Action name string
 */
void Debug_LogMenuAction(const char *action_name) {
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[20%02u-%02u-%02u %02u:%02u:%02u] MENU:%s\r\n",
                     rtcNow.year, rtcNow.month, rtcNow.date,
                     rtcNow.hours, rtcNow.minutes, rtcNow.seconds, action_name);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_emit(debug_buffer, (uint16_t)len);
  }
}
#else
void Debug_LogMenuAction(const char *action_name) { (void)action_name; }
#endif

#ifdef DEBUG_LOG_VIEW_EVENTS
/**
 * @brief Log a view state-machine transition
 * @param[in] from_state Previous state
 * @param[in] to_state New state
 */
void Debug_LogViewTransition(uint8_t from_state, uint8_t to_state) {
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[20%02u-%02u-%02u %02u:%02u:%02u] VIEW:%u->%u\r\n",
                     rtcNow.year, rtcNow.month, rtcNow.date,
                     rtcNow.hours, rtcNow.minutes, rtcNow.seconds,
                     from_state, to_state);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_emit(debug_buffer, (uint16_t)len);
  }
}
#else
void Debug_LogViewTransition(uint8_t from_state, uint8_t to_state) {
  (void)from_state;
  (void)to_state;
}
#endif

#ifdef DEBUG_LOG_HEARTBEAT
/**
 * @brief Periodic heartbeat — call from the main loop
 * @details Emits a line every `DEBUG_HEARTBEAT_INTERVAL_MS`. A missing
 *          heartbeat in the UART stream indicates a hang.
 */
void Debug_Heartbeat(void) {
  uint32_t now = HAL_GetTick();
  if ((now - last_heartbeat_tick) >= DEBUG_HEARTBEAT_INTERVAL_MS) {
    last_heartbeat_tick = now;
    heartbeat_count++;
    Debug_LogValue("HEARTBEAT #", heartbeat_count);
  }
}
#else
void Debug_Heartbeat(void) {}
#endif

/**
 * @brief Log the boot banner and reset cause
 */
void Debug_LogBoot(void) {
  Debug_Log("WEATHER STATION BOOT");
  Debug_Log("Debug logging enabled");
  debug_log_reset_cause();
}

#endif /* DEBUG_LOG_ENABLE */
