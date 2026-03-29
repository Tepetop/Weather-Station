/**
 * @file debug_log.h
 * @brief Simple UART debug logging for Weather Station Indoor Unit
 * @details Provides timestamped debug output via UART2 for tracking
 *          system events and diagnosing hangs. Enable/disable with
 *          DEBUG_LOG_ENABLE define.
 */

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdint.h>

/* ============================================================================
 * CONFIGURATION
 * ========================================================================== */

/**
 * @brief Master enable switch for debug logging
 * @details Comment out to disable all debug output and minimize code size
 */
#define DEBUG_LOG_ENABLE

/**
 * @brief Enable individual log categories
 * @details Comment out specific categories to reduce log verbosity
 */
#ifdef DEBUG_LOG_ENABLE
  #define DEBUG_LOG_RTC_EVENTS      /* RTC alarm triggers */
  #define DEBUG_LOG_NRF_EVENTS      /* NRF TX/RX events */
  #define DEBUG_LOG_MENU_EVENTS     /* Menu navigation events */
  #define DEBUG_LOG_HEARTBEAT       /* Periodic main loop heartbeat */
  //#define DEBUG_LOG_VIEW_EVENTS   /* View state machine transitions (verbose) */
#endif

/**
 * @brief Heartbeat interval in milliseconds
 * @details How often to log heartbeat in main loop (if enabled)
 */
#define DEBUG_HEARTBEAT_INTERVAL_MS 60000U  /* 1 minute */

/* ============================================================================
 * PUBLIC API
 * ========================================================================== */

#ifdef DEBUG_LOG_ENABLE

/**
 * @brief Initialize debug logging system
 * @details Must be called after UART init and RTC init
 */
void Debug_Init(void);

/**
 * @brief Log a simple message
 * @param[in] msg NULL-terminated message string
 */
void Debug_Log(const char *msg);

/**
 * @brief Log a message with a single integer value
 * @param[in] msg Message prefix (NULL-terminated)
 * @param[in] value Integer value to append
 */
void Debug_LogValue(const char *msg, int32_t value);

/**
 * @brief Log a message with a hex value
 * @param[in] msg Message prefix (NULL-terminated)
 * @param[in] value Hex value to append
 */
void Debug_LogHex(const char *msg, uint32_t value);

/**
 * @brief Log RTC alarm 1 event
 */
void Debug_LogRtcAlarm1(void);

/**
 * @brief Log RTC alarm 2 event (measurement trigger)
 */
void Debug_LogRtcAlarm2(void);

/**
 * @brief Log NRF TX start
 * @param[in] node_idx Node index being addressed
 */
void Debug_LogNrfTxStart(uint8_t node_idx);

/**
 * @brief Log NRF TX result
 * @param[in] success 1 if TX succeeded, 0 if failed
 */
void Debug_LogNrfTxResult(uint8_t success);

/**
 * @brief Log NRF RX data received
 * @param[in] node_idx Node index data came from
 */
void Debug_LogNrfRxData(uint8_t node_idx);

/**
 * @brief Log NRF timeout
 * @param[in] is_tx 1 if TX timeout, 0 if RX timeout
 */
void Debug_LogNrfTimeout(uint8_t is_tx);

/**
 * @brief Log menu action
 * @param[in] action_name Action name string
 */
void Debug_LogMenuAction(const char *action_name);

/**
 * @brief Log view state transition
 * @param[in] from_state Previous state
 * @param[in] to_state New state
 */
void Debug_LogViewTransition(uint8_t from_state, uint8_t to_state);

/**
 * @brief Periodic heartbeat - call from main loop
 * @details Logs a heartbeat message at DEBUG_HEARTBEAT_INTERVAL_MS intervals.
 *          If heartbeat stops appearing in logs, program has hung.
 */
void Debug_Heartbeat(void);

/**
 * @brief Log system boot message with firmware version
 */
void Debug_LogBoot(void);

#else

/* Empty macros when debug is disabled */
#define Debug_Init()
#define Debug_Log(msg)
#define Debug_LogValue(msg, value)
#define Debug_LogHex(msg, value)
#define Debug_LogRtcAlarm1()
#define Debug_LogRtcAlarm2()
#define Debug_LogNrfTxStart(node_idx)
#define Debug_LogNrfTxResult(success)
#define Debug_LogNrfRxData(node_idx)
#define Debug_LogNrfTimeout(is_tx)
#define Debug_LogMenuAction(action_name)
#define Debug_LogViewTransition(from_state, to_state)
#define Debug_Heartbeat()
#define Debug_LogBoot()

#endif /* DEBUG_LOG_ENABLE */

#endif /* DEBUG_LOG_H */
