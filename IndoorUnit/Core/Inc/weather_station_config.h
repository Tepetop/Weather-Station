/**
 * @file    weather_station_config.h
 * @brief   Global instances and NRF24 link constants for the indoor unit
 * @details Defines shared application objects (LCD, encoder, RTC, NRF radio,
 *          weather-station manager) and radio timing/address configuration used
 *          by the central station. Include once from a single translation unit.
 */

#ifndef WEATHER_STATION_CONFIG_H
#define WEATHER_STATION_CONFIG_H

#include <encoder.h>
#include <button_debounce.h>

#include "ds3231.h"
#include "NRF24L01.h"
#include "weather_station.h"
#include "weather_station_ui.h"
#include "ws_protocol.h"

/** @brief IRQ flag value: event pending */
#define IRQ_FLAG_SET 1
/** @brief IRQ flag value: no event */
#define IRQ_FLAG_CLEAR 0

/* ============================================================================
 * NRF24L01 link configuration
 * ============================================================================ */

/** @brief RF channel number (76 → 2476 MHz) */
#define NRF_CHANNEL      76
/** @brief Fixed RX/TX payload size (matches protocol) */
#define NRF_PAYLOAD_SIZE WS_PROTOCOL_MAX_PAYLOAD
/** @brief Command field size in protocol frames */
#define NRF_CMD_SIZE     WS_CMD_SIZE
/** @brief Measure command token */
#define CMD_MEASURE      WS_CMD_MEASURE
/** @brief TX IRQ wait timeout in milliseconds */
#define NRF_TX_IRQ_TIMEOUT_MS 120U
/** @brief RX response timeout in milliseconds */
#define NRF_RX_TIMEOUT_MS 4500U
/** @brief Communication watchdog timeout in milliseconds */
#define NRF_COMM_WATCHDOG_TIMEOUT_MS 600000U
/** @brief Window watchdog refresh period in milliseconds */
#define WWDG_REFRESH_PERIOD_MS 10U
/** @brief Number of outdoor nodes managed by this central unit */
#define WS_NODE_COUNT 2U
/** @brief Hold time for manual RTC set gesture in milliseconds */
#define RTC_MANUAL_SET_HOLD_MS 1200U

/**
 * @brief Broadcast address for parallel measure commands (no Auto-ACK)
 */
static const uint8_t NRF_BROADCAST_ADDR[5] = {0xB0U, 0xB0U, 0xB0U, 0xB0U, 0xB0U};

/* ============================================================================
 * Global application instances
 * ============================================================================ */

Menu_Context_t menuContext;   /**< Menu navigation context */
PCD8544_t LCD;                /**< PCD8544 LCD display instance */
Encoder_t encoder;            /**< Rotary encoder instance */
Button_t encoderSW;           /**< Encoder push-button instance */
DS3231_Handle rtc;            /**< DS3231 RTC driver handle */
NRF24_Handle_t nrf;           /**< nRF24L01+ radio handle */
WS_Manager_t wsCtx = {0};     /**< Weather-station manager context */
WS_RuntimeConfig_t wsRuntime = {0}; /**< Runtime configuration */

/** @brief Default RTC date/time used at startup */
DS3231_DateTime currentDateTime = {
  .seconds = 0,
  .minutes = 6,
  .hours   = 11,
  .ampm    = DS3231_AM,
  .format  = DS3231_FORMAT_24H,
  .day     = 6,
  .date    = 9,
  .month   = 5,
  .year    = 26,
  .century = false
};

/** @brief Alarm 1 configuration (every second) */
DS3231_Alarm1 RTCalarm1 = {
  .seconds = 1,
  .minutes = 0,
  .hours = 0,
  .ampm = 0,
  .format = 0,
  .day_date = 0,
  .mode = DS3231_ALM1_EVERY_SECOND
};

/** @brief Alarm 2 configuration (every minute) */
DS3231_Alarm2 RTCalarm2 = {
  .minutes = 1,
  .hours = 0,
  .ampm = 0,
  .format = 0,
  .day_date = 0,
  .mode = DS3231_ALM2_EVERY_MINUTE
};

/** @brief Latest RTC snapshot read from hardware */
DS3231_DateTime rtcNow;

volatile uint8_t alarm1_count = 0;  /**< Alarm 1 interrupt counter */
volatile uint8_t alarm2_count = 0;  /**< Alarm 2 interrupt counter */

/** @brief Scratch buffer for NRF status/debug messages */
char g_nrf_message[64];

/* ============================================================================
 * NRF24 multiceiver address map
 * ============================================================================
 *
 * Central station RX pipes (static, configured once at init):
 *   Pipe 0: Reserved for auto-ACK (dynamically set to match TX_ADDR)
 *   Pipe 1: Node 0 data reception  (full 5-byte addr)
 *   Pipe 2: Node 1 data reception  (only LSByte differs from Pipe 1)
 *   Pipe 3: Node 2 data reception  (only LSByte differs from Pipe 1)
 *   Pipe 4: Node 3 data reception  (only LSByte differs from Pipe 1)
 *
 * Broadcast TX address (NRF_BROADCAST_ADDR):
 *   One NoAck measure command heard by all outdoor units in parallel.
 *   Outdoor units listen on this address on a dedicated RX pipe (no Auto-ACK).
 *
 * Unicast TX addresses: used for single-node CMD:MEASURE:N.
 *   Only LSByte differs between nodes (outdoor nodes use same scheme).
 *
 * Byte order: addr[0] = LSByte (transmitted first on-air).
 */

/** @brief Unicast TX addresses per outdoor node (central → node commands) */
static const uint8_t WS_NODE_TX_ADDRS[WS_MAX_NODES][5] = {
  {0xE7, 0xE7, 0xE7, 0xE7, 0xE7},  /**< Node 0 */
  {0xE8, 0xE7, 0xE7, 0xE7, 0xE7},  /**< Node 1 (LSB=0xE8) */
  {0xE9, 0xE7, 0xE7, 0xE7, 0xE7},  /**< Node 2 (LSB=0xE9) */
  {0xEA, 0xE7, 0xE7, 0xE7, 0xE7}   /**< Node 3 (LSB=0xEA) */
};

/** @brief Multiceiver RX addresses per outdoor node (node → central data) */
static const uint8_t WS_NODE_RX_ADDRS[WS_MAX_NODES][5] = {
  {0xC2, 0xC2, 0xC2, 0xC2, 0xC2},  /**< Pipe 1 - Node 0 (full addr) */
  {0xC3, 0xC2, 0xC2, 0xC2, 0xC2},  /**< Pipe 2 - Node 1 (LSB=0xC3) */
  {0xC4, 0xC2, 0xC2, 0xC2, 0xC2},  /**< Pipe 3 - Node 2 (LSB=0xC4) */
  {0xC5, 0xC2, 0xC2, 0xC2, 0xC2}   /**< Pipe 4 - Node 3 (LSB=0xC5) */
};

#endif
