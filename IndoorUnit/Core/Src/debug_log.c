/**
 * @file debug_log.c
 * @brief Simple UART debug logging implementation
 */

#include "debug_log.h"

#ifdef DEBUG_LOG_ENABLE

#include "usart.h"
#include "ds3231.h"
#include <stdio.h>
#include <string.h>

/* External RTC time reference */
extern DS3231_DateTime rtcNow;
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

/**
 * @brief Send buffer contents via UART (blocking)
 */
static void debug_send(const char *str, uint16_t len) {
  HAL_UART_Transmit(&huart1, (uint8_t *)str, len, 100);
}

/**
 * @brief Format and send message with timestamp prefix
 */
static void debug_print_timestamped(const char *msg) {
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[%02u:%02u:%02u] %s\r\n",
                     rtcNow.hours, rtcNow.minutes, rtcNow.seconds, msg);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_send(debug_buffer, (uint16_t)len);
  }
}

/**
 * @brief Log reset source flags latched in RCC CSR since previous boot
 */
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
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[%02u:%02u:%02u] %s%ld\r\n",
                     rtcNow.hours, rtcNow.minutes, rtcNow.seconds,
                     msg, (long)value);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_send(debug_buffer, (uint16_t)len);
  }
}

void Debug_LogHex(const char *msg, uint32_t value) {
  if (msg == NULL) return;
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[%02u:%02u:%02u] %s0x%08lX\r\n",
                     rtcNow.hours, rtcNow.minutes, rtcNow.seconds,
                     msg, (unsigned long)value);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_send(debug_buffer, (uint16_t)len);
  }
}

void Debug_LogRtcAlarm1(void) {
#ifdef DEBUG_LOG_RTC1_EVENTS
  Debug_Log("LOG:RTC:ALM1 (screen update)");
#endif
}

void Debug_LogRtcAlarm2(void) {
#ifdef DEBUG_LOG_RTC2_EVENTS
  Debug_Log("LOG:RTC:ALM2 (measurement trigger)");
#endif
}

#ifdef DEBUG_LOG_NRF_EVENTS
void Debug_LogNrfTxStart(uint8_t node_idx) {
  Debug_LogValue("LOG:NRF:TX_START node=", node_idx);
}

void Debug_LogNrfTxResult(uint8_t success) {
  if (success) {
    Debug_Log("LOG:NRF:TX_OK (ACK received)");
  } else {
    Debug_Log("LOG:NRF:TX_FAIL (MAX_RT)");
  }
}

void Debug_LogNrfRxData(uint8_t node_idx) {
  Debug_LogValue("LOG:NRF:RX_DATA from node=", node_idx);
}

void Debug_LogNrfTimeout(uint8_t is_tx) {
  if (is_tx) {
    Debug_Log("LOG:NRF:TX_TIMEOUT");
  } else {
    Debug_Log("LOG:NRF:RX_TIMEOUT (no response)");
  }
}
#else
void Debug_LogNrfTxStart(uint8_t node_idx) { (void)node_idx; }
void Debug_LogNrfTxResult(uint8_t success) { (void)success; }
void Debug_LogNrfRxData(uint8_t node_idx) { (void)node_idx; }
void Debug_LogNrfTimeout(uint8_t is_tx) { (void)is_tx; }
#endif

#ifdef DEBUG_LOG_MENU_EVENTS
void Debug_LogMenuAction(const char *action_name) {
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[%02u:%02u:%02u] MENU:%s\r\n",
                     rtcNow.hours, rtcNow.minutes, rtcNow.seconds, action_name);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_send(debug_buffer, (uint16_t)len);
  }
}
#else
void Debug_LogMenuAction(const char *action_name) { (void)action_name; }
#endif

#ifdef DEBUG_LOG_VIEW_EVENTS
void Debug_LogViewTransition(uint8_t from_state, uint8_t to_state) {
  int len = snprintf(debug_buffer, sizeof(debug_buffer),
                     "LOG:[%02u:%02u:%02u] VIEW:%u->%u\r\n",
                     rtcNow.hours, rtcNow.minutes, rtcNow.seconds,
                     from_state, to_state);
  if (len > 0 && len < (int)sizeof(debug_buffer)) {
    debug_send(debug_buffer, (uint16_t)len);
  }
}
#else
void Debug_LogViewTransition(uint8_t from_state, uint8_t to_state) {
  (void)from_state;
  (void)to_state;
}
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
  Debug_Log("LOG:WEATHER STATION BOOT");
  Debug_Log("LOG:Debug logging enabled");
  debug_log_reset_cause();
}

#endif /* DEBUG_LOG_ENABLE */
