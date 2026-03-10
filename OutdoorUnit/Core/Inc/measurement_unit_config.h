#ifndef MEASUREMENT_UNIT_CONFIG_H

/*      INCLUDES    */
#include "si7021.h"
#include "TSL2561.h"
#include "bmp280.h"
#include "measurement.h"
#include "NRF24L01.h"



/*      DEFINES */
#define NRF_ENABLED 1
#define NRF_CHANNEL      76      // 2476 MHz - same as IndoorUnit
#define NRF_PAYLOAD_SIZE 24     // sizeof(Measurement_Data_t) with sensorStatus + padding
#define NRF_CMD_SIZE     8      // Command payload size
#define CMD_MEASURE      0x01   // Command to request measurement
#define NRF_TX_TIMEOUT_MS     120U
#define NRF_INIT_MAX_RETRIES  3
#define NRF_INIT_RETRY_DELAY_MS 200U

#define OUTDOOR_MEAS_MAX_RETRIES  3
#define OUTDOOR_MEAS_TIMEOUT_MS   2000U

#define USE_LED_INDICATOR 1
#define USE_UART_LOGGING 1

/* Node identity — change this per outdoor unit (0-3) */
#define NODE_ID          0U


/*          VARIABES AND STRUCTS */
typedef enum {
  OUT_LINK_IDLE = 0,          /**< RX mode, waiting for commands */
  OUT_LINK_MEASURING,         /**< Running measurement state machine */
  OUT_LINK_TX_SENDING,        /**< Transmitting data, waiting for ACK/timeout */
  OUT_LINK_RECOVERY,          /**< Error recovery: reset NRF and return to IDLE */
} OutdoorLinkStateEnum_t;

typedef struct {
  volatile uint8_t irq_flag;       /**< Set by EXTI callback when NRF IRQ fires */
  volatile uint8_t cmd_received;   /**< Set when CMD_MEASURE payload received */
  volatile uint8_t tx_done;        /**< TX completed (success or MAX_RT) */
  volatile uint8_t tx_ok;          /**< TX acknowledged by receiver */
  uint8_t tx_in_progress;          /**< TX operation active */
  uint8_t meas_started;            /**< Measurement_Start() called in current cycle */
  uint8_t meas_retry_count;        /**< Current measurement retry counter */
  uint8_t last_status;             /**< Last NRF status register snapshot */
  uint32_t tx_start_tick;          /**< Tick when TX was initiated */
  uint32_t meas_start_tick;        /**< Tick when measurement cycle began */
  OutdoorLinkStateEnum_t state;    /**< Current link state machine state */
} OutdoorLinkContext_t;

typedef enum {
  OUT_STATE_OK = 0,
  OUT_STATE_ERROR,
  OUT_STATE_INIT_ERROR,
  OUT_STATE_TX_ERROR,
  OUT_STATE_PARAM_ERROR,
  OUT_STATE_RX_ERROR,
  OUT_STATE_TIMEOUT
}OUT_STATE_t;

Measurement_Data_t txData;
char Message[128]; // Message to transfer by UART
uint8_t Length; // Message length

/*      MULTIRECEIVER  SETTINGS */
NRF24_Handle_t nrf;
OutdoorLinkContext_t outLink = {0};

/*
 * Multiceiver address scheme (must match IndoorUnit config):
 *
 * Indoor central station:
 *   TX_ADDR[node]  = {0xE7+node, 0xE7, 0xE7, 0xE7, 0xE7}  (sends commands)
 *   RX Pipe 1      = {0xC2, 0xC2, 0xC2, 0xC2, 0xC2}        (full addr, Node 0)
 *   RX Pipe 2      = LSB 0xC3  (Node 1, shares MSBytes with Pipe 1)
 *   RX Pipe 3      = LSB 0xC4  (Node 2)
 *   RX Pipe 4      = LSB 0xC5  (Node 3)
 *
 * This outdoor unit (NODE_ID):
 *   TX_ADDR = {0xC2+NODE_ID, 0xC2, 0xC2, 0xC2, 0xC2}  → Indoor's Pipe (1+NODE_ID)
 *   RX Pipe 1 = {0xE7+NODE_ID, 0xE7, 0xE7, 0xE7, 0xE7} ← Indoor's TX for this node
 *   Pipe 0 = TX_ADDR (for auto-ACK)
 */
static const uint8_t NRF_TX_ADDR[5] = {0xC2 + NODE_ID, 0xC2, 0xC2, 0xC2, 0xC2};
static const uint8_t NRF_RX_ADDR[5] = {0xE7 + NODE_ID, 0xE7, 0xE7, 0xE7, 0xE7};




#define MEASUREMENT_UNIT_CONFIG_H
#endif