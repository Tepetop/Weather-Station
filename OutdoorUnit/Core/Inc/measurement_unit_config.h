/**
 * @file    measurement_unit_config.h
 * @brief   Outdoor unit runtime configuration and link state definitions
 * @details Feature flags, NRF24 parameters, measurement timeouts, enabled
 *          sensor channels, and OutdoorLink state machine types for the
 *          STM32F103 outdoor measurement unit.
 */

#ifndef MEASUREMENT_UNIT_CONFIG_H
#define MEASUREMENT_UNIT_CONFIG_H

/* ============================================================================
 * Includes
 * ============================================================================ */
#include "measurement.h"
#include "NRF24L01.h"
#include "ws_protocol.h"



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
#define NODE_ID               1U

/* ============================================================================
 * NRF24L01 Configuration
 * ============================================================================ */
#define NRF_CHANNEL               76U     /**< RF channel: 2476 MHz (must match IndoorUnit) */
#define NRF_PAYLOAD_SIZE          WS_PROTOCOL_MAX_PAYLOAD
#define NRF_CMD_SIZE              WS_CMD_SIZE
#define CMD_MEASURE               WS_CMD_MEASURE
#define NRF_TX_TIMEOUT_MS         120U    /**< TX timeout in milliseconds */
#define NRF_INIT_MAX_RETRIES      3U      /**< Max NRF init retry attempts */
#define NRF_INIT_RETRY_DELAY_MS   200U    /**< Delay between init retries */
#define NRF_REINIT_INTERVAL_MS    10000U  /**< Periodic reinit when NRF is missing */
/** @brief Stagger outdoor reply TX by NODE_ID * this delay (collision avoidance) */
#define NRF_RESPONSE_SLOT_MS      100U
/** @brief Outdoor RX pipe for broadcast/unicast measure commands (no Auto-ACK) */
#define NRF_PIPE_CMD              1U

/** Shared command address — must match IndoorUnit NRF_BROADCAST_ADDR */
static const uint8_t NRF_BROADCAST_ADDR[5] = {0xB0U, 0xB0U, 0xB0U, 0xB0U, 0xB0U};

/* ============================================================================
 * Measurement Configuration
 * ============================================================================ */
#define OUTDOOR_MEAS_MAX_RETRIES  3U      /**< Max measurement retry attempts */
#define OUTDOOR_MEAS_TIMEOUT_MS   2000U   /**< Measurement cycle timeout */

/**
 * @brief Channels transmitted by this outdoor unit (edit per station hardware).
 * @note  Barometric channels follow the driver selected in measurement.h
 *        (bmp280.h or bme280.h). Frame size allows at most WS_MAX_READINGS (5)
 *        entries — to add WS_CH_BME280_HUM, drop another channel first.
 */
#if defined(BMP280_H)
static const uint8_t ENABLED_CHANNELS[] = {
    WS_CH_SI7021_TEMP,
    WS_CH_SI7021_HUM,
    WS_CH_BMP280_TEMP,
    WS_CH_BMP280_PRESS,
    WS_CH_TSL2561_LUX,
};
#elif defined(BME280_H)
static const uint8_t ENABLED_CHANNELS[] = {
    WS_CH_SI7021_TEMP,
    WS_CH_SI7021_HUM,
    WS_CH_BME280_TEMP,
    WS_CH_BME280_PRESS,
    WS_CH_TSL2561_LUX,
};
#else
static const uint8_t ENABLED_CHANNELS[] = {
    WS_CH_SI7021_TEMP,
    WS_CH_SI7021_HUM,
    WS_CH_TSL2561_LUX,
};
#endif
/** @brief Number of channels listed in ENABLED_CHANNELS */
#define ENABLED_CHANNEL_COUNT ((uint8_t)(sizeof(ENABLED_CHANNELS) / sizeof(ENABLED_CHANNELS[0])))


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
  uint8_t last_cycle_id;           /**< Last accepted measure cycle id */
  uint8_t have_last_cycle_id;      /**< 1 when last_cycle_id is valid */
  uint8_t tx_delay_armed;          /**< Waiting for NODE_ID response slot */
  uint32_t tx_start_tick;          /**< Tick when TX was initiated */
  uint32_t meas_start_tick;        /**< Tick when measurement cycle began */
  uint32_t tx_ready_tick;          /**< Earliest tick allowed to send response */
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
 * External Variables (defined in outdoor_station.c)
 * ============================================================================ */
extern uint8_t txPayload[WS_PROTOCOL_MAX_PAYLOAD]; /**< NRF TX wire buffer */
extern uint8_t txPayloadLen;                     /**< Encoded payload length */
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
 * Indoor central station:
 *   Broadcast TX   = {0xB0, 0xB0, 0xB0, 0xB0, 0xB0} (parallel measure, NoAck)
 *   Unicast TX[n]  = {0xE7+n, 0xE7, 0xE7, 0xE7, 0xE7} (single-node measure)
 *   RX Pipe 1..4   = {0xC2+n, 0xC2, ...} node reply pipes
 *
 * This outdoor unit (NODE_ID):
 *   TX_ADDR = {0xC2+NODE_ID, 0xC2, ...} -> Indoor RX pipe (1+NODE_ID)
 *   Pipe 0 = TX_ADDR (auto-ACK for replies)
 *   Pipe 1 = broadcast command address, Auto-ACK off (filter by target_mask)
 * ============================================================================ */
extern const uint8_t NRF_TX_ADDR[5];  /**< TX address for this outdoor unit */
extern const uint8_t NRF_RX_ADDR[5];  /**< Legacy unique RX (unused for commands; kept for reference) */

#endif /* MEASUREMENT_UNIT_CONFIG_H */