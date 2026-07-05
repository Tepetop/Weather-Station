/**
 * @file debug_log.h
 * @brief Simple UART debug logging for Weather Station Outdoor Unit
 * @details Provides timestamped debug output via UART1 for tracking
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
  #define DEBUG_LOG_INIT_EVENTS     /* Boot, sensor and NRF init */
  #define DEBUG_LOG_NRF_EVENTS      /* NRF TX/RX events */
  #define DEBUG_LOG_MEAS_EVENTS     /* Measurement cycle events */
  #define DEBUG_LOG_LINK_EVENTS     /* Link recovery and timing */
  //#define DEBUG_LOG_HEARTBEAT       /* Periodic main loop heartbeat */
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

void Debug_Init(void);
void Debug_Log(const char *msg);
void Debug_LogValue(const char *msg, int32_t value);
void Debug_LogHex(const char *msg, uint32_t value);
void Debug_Heartbeat(void);
void Debug_LogBoot(void);

void Debug_LogNrfInit(uint8_t success);
void Debug_LogNrfListening(void);
void Debug_LogNrfRxCmd(void);
void Debug_LogNrfTxResult(uint8_t success);
void Debug_LogNrfTimeout(void);

void Debug_LogSystemReady(uint8_t sensor_error_code);
void Debug_LogSensorError(uint8_t error_flag, const char *name);
void Debug_LogI2cDevice(uint8_t addr);

void Debug_LogMeasCmd(void);
void Debug_LogMeasDone(void);
void Debug_LogMeasRetry(uint8_t attempt, uint8_t max);
void Debug_LogMeasNoSensors(void);
void Debug_LogMeasMaxRetries(void);
void Debug_LogMeasTimeout(void);

void Debug_LogRecovery(void);
void Debug_LogElapsedMs(uint32_t ms);

#else

#define Debug_Init()
#define Debug_Log(msg)
#define Debug_LogValue(msg, value)
#define Debug_LogHex(msg, value)
#define Debug_Heartbeat()
#define Debug_LogBoot()

#define Debug_LogNrfInit(success)
#define Debug_LogNrfListening()
#define Debug_LogNrfRxCmd()
#define Debug_LogNrfTxResult(success)
#define Debug_LogNrfTimeout()

#define Debug_LogSystemReady(sensor_error_code)
#define Debug_LogSensorError(error_flag, name)
#define Debug_LogI2cDevice(addr)

#define Debug_LogMeasCmd()
#define Debug_LogMeasDone()
#define Debug_LogMeasRetry(attempt, max)
#define Debug_LogMeasNoSensors()
#define Debug_LogMeasMaxRetries()
#define Debug_LogMeasTimeout()

#define Debug_LogRecovery()
#define Debug_LogElapsedMs(ms)

#endif /* DEBUG_LOG_ENABLE */

#endif /* DEBUG_LOG_H */
