#ifndef WEATHER_STATION_CONFIG_H
#define WEATHER_STATION_CONFIG_H

#include <encoder.h>
#include <button_debounce.h>

#include "ds3231.h"
#include "NRF24L01.h"
#include "weather_station.h"


#define IRQ_FLAG_SET 1
#define IRQ_FLAG_CLEAR 0

/* NRF24L01 configuration */
#define NRF_CHANNEL      76      // 2476 MHz
#define NRF_PAYLOAD_SIZE 20     // Measurement data payload size
#define NRF_CMD_SIZE     8      // Command payload size
#define CMD_MEASURE      0x01   // Command to request measurement
#define NRF_TX_IRQ_TIMEOUT_MS 80U
#define WS_NODE_COUNT 1U
#define RTC_MANUAL_SET_HOLD_MS 1200U


/*      Structures*/
Menu_Context_t menuContext;   // Menu context for managing menu state
PCD8544_t LCD;                // LCD instance
Encoder_t encoder;            // Encoder instance
Button_t encoderSW;          // Button instance for encoder switch
DS3231_Handle rtc;
NRF24_Handle_t nrf;
WS_Manager_t wsCtx = {0};
WS_RuntimeConfig_t wsRuntime = {0};

/*      Assign data to time structures  */
DS3231_DateTime currentDateTime = {
  .seconds = 0,
  .minutes = 06,
  .hours   = 20,
  .ampm    = DS3231_AM,
  .format  = DS3231_FORMAT_24H,
  .day     = 7,
  .date    = 8,
  .month   = 3,
  .year    = 26,
  .century = false
};

  DS3231_Alarm1 RTCalarm1 = {
  .seconds = 1,
  .minutes = 0,
  .hours = 0,
  .ampm = 0,  
  .format = 0, 
  .day_date = 0,
  .mode = DS3231_ALM1_EVERY_SECOND  
};

DS3231_DateTime rtcNow;

volatile uint8_t alarm1_count= 0;
volatile uint8_t alarm2_count = 0;

/*      g_nrf_message  for NRF data*/
char g_nrf_message[64];




/* NRF addresses */
static const uint8_t WS_NODE_TX_ADDRS[WS_MAX_NODES][5] = {
  {0xE7, 0xE7, 0xE7, 0xE7, 0xE7},
  {0xE8, 0xE8, 0xE8, 0xE8, 0xE8},
  {0xE9, 0xE9, 0xE9, 0xE9, 0xE9},
  {0xEA, 0xEA, 0xEA, 0xEA, 0xEA}
};
static const uint8_t WS_NODE_RX_ADDRS[WS_MAX_NODES][5] = {
  {0xC2, 0xC2, 0xC2, 0xC2, 0xC2},
  {0xC3, 0xC3, 0xC3, 0xC3, 0xC3},
  {0xC4, 0xC4, 0xC4, 0xC4, 0xC4},
  {0xC5, 0xC5, 0xC5, 0xC5, 0xC5}
};

#endif