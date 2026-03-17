#ifndef MEASUREMENT_UNIT_CONFIG_H
#define MEASUREMENT_UNIT_CONFIG_H

/* ============================================================================
 * Includes
 * ============================================================================ */
#include "si7021.h"
#include "TSL2561.h"
#include "bmp280.h"
#include "measurement.h"
#include "NRF24L01.h"



/* ============================================================================
 * Feature Flags
 * ============================================================================ */
#define NRF_ENABLED           1     /**< Enable NRF24L01 wireless module */
#define USE_LED_INDICATOR     1     /**< Enable LED status indication */
#define USE_UART_LOGGING      1     /**< Enable UART debug logging */
#define CHECK_I2C_DEVICES     0     /**< Scan I2C bus on startup (debug) */
#define USE_TIMER_PROFILING   1     /**< Enable timing measurements for profiling */

/* ============================================================================
 * Node Configuration
 * ============================================================================ */
/** @brief Node identity — change this per outdoor unit (0-3) */
#define NODE_ID               0U

/* ============================================================================
 * NRF24L01 Configuration
 * ============================================================================ */
#define NRF_CHANNEL               76U     /**< RF channel: 2476 MHz (must match IndoorUnit) */
#define NRF_PAYLOAD_SIZE          24U     /**< sizeof(Measurement_Data_t) with padding */
#define NRF_CMD_SIZE              8U      /**< Command payload size */
#define CMD_MEASURE               0x01U   /**< Command to request measurement */
#define NRF_TX_TIMEOUT_MS         120U    /**< TX timeout in milliseconds */
#define NRF_INIT_MAX_RETRIES      3U      /**< Max NRF init retry attempts */
#define NRF_INIT_RETRY_DELAY_MS   200U    /**< Delay between init retries */

/* ============================================================================
 * Measurement Configuration
 * ============================================================================ */
#define OUTDOOR_MEAS_MAX_RETRIES  3U      /**< Max measurement retry attempts */
#define OUTDOOR_MEAS_TIMEOUT_MS   2000U   /**< Measurement cycle timeout */


/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief OutdoorLink state machine states
 */
typedef enum {
  OUT_LINK_IDLE = 0,          /**< RX mode, waiting for commands */
  OUT_LINK_MEASURING,         /**< Running measurement state machine */
  OUT_LINK_TX_SENDING,        /**< Transmitting data, waiting for ACK/timeout */
  OUT_LINK_RECOVERY,          /**< Error recovery: reset NRF and return to IDLE */
} OutdoorLinkStateEnum_t;

/**
 * @brief OutdoorLink context structure for state machine management
 */
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

/**
 * @brief OutdoorUnit operation status codes
 */
typedef enum {
  OUT_STATE_OK = 0,           /**< Operation successful */
  OUT_STATE_ERROR,            /**< Generic error */
  OUT_STATE_INIT_ERROR,       /**< Initialization error */
  OUT_STATE_TX_ERROR,         /**< Transmission error */
  OUT_STATE_PARAM_ERROR,      /**< Invalid parameter */
  OUT_STATE_RX_ERROR,         /**< Reception error */
  OUT_STATE_TIMEOUT           /**< Operation timeout */
} OUT_STATE_t;

/* ============================================================================
 * External Variables (defined in main.c)
 * ============================================================================ */
extern Measurement_Data_t txData;     /**< Data buffer for NRF transmission */
extern char Message[128];             /**< Message buffer for UART transfer */
extern uint8_t Length;                /**< Message length */

/* ============================================================================
 * NRF24L01 Multireceiver Settings
 * ============================================================================ */
extern NRF24_Handle_t nrf;            /**< NRF24L01 handle instance */
extern OutdoorLinkContext_t outLink;  /**< OutdoorLink state machine context */

/* ============================================================================
 * NRF24L01 Address Configuration (Multiceiver)
 * ============================================================================
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
 * ============================================================================ */
extern const uint8_t NRF_TX_ADDR[5];  /**< TX address for this outdoor unit */
extern const uint8_t NRF_RX_ADDR[5];  /**< RX address for receiving commands */

#endif /* MEASUREMENT_UNIT_CONFIG_H */