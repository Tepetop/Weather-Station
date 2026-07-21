/**
 * @file weather_station.h
 * @brief Weather Station Manager API for STM32F103 Indoor Unit
 * @details Provides high-level interface for managing multiple outdoor sensor
 *          nodes via nRF24L01+ wireless communication. Handles measurement
 *          requests, timeouts, data reception, and multi-node scheduling.
 * @author Weather Station Team
 * @date 2026
 */

#ifndef WEATHER_STATION_H
#define WEATHER_STATION_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "NRF24L01.h"
#include "ds3231.h"
#include "ws_protocol.h"

#include <PCD_LCD/PCD8544.h>

/* ============================================================================
 * PUBLIC CONSTANTS
 * ========================================================================== */

/** @brief Maximum number of outdoor nodes supported */
#define WS_MAX_NODES 4U

#define SCREEN_SAVER_TIMEOUT_MS  30000U /* 30 seconds */

/* ============================================================================
 * PUBLIC ENUMERATIONS
 * ========================================================================== */

/**
 * @brief Node state enumeration
 * @details Tracks the current operational state of an outdoor sensor node
 */
typedef enum {
  WS_NODE_IDLE = 0,              /**< Node idle, ready for new operation */
  WS_NODE_TX_IN_PROGRESS,        /**< Transmitting command to node */
  WS_NODE_WAIT_RESPONSE,         /**< Waiting for measurement data response */
  WS_NODE_DATA_READY,            /**< Data received and ready for processing */
  WS_NODE_ERROR                  /**< Error state (timeout or TX failure) */
} WS_NodeStateEnum_t;

/**
 * @brief Transmission event result enumeration
 * @details Indicates the outcome of a transmission attempt
 */
typedef enum {
  WS_TX_EVENT_NONE = 0,          /**< No TX event occurred */
  WS_TX_EVENT_OK,                /**< Transmission successful (ACK received) */
  WS_TX_EVENT_FAIL               /**< Transmission failed (MAX_RT reached) */
} WS_TxEvent_t;

/**
 * @brief Application state machine enumeration
 * @details Tracks the overall state of the weather station application
 */
typedef enum {
  WS_APP_IDLE = 0,               /**< Idle state, ready for commands */
  WS_APP_WAIT_TX_IRQ,            /**< Waiting for TX completion interrupt */
  WS_APP_WAIT_RX_DATA,           /**< Waiting for measurement data */
  WS_APP_DATA_READY,             /**< Data received and processed */
  WS_APP_ERROR_RECOVERY          /**< Recovering from error condition */
} WS_AppState_t;

/* ============================================================================
 * PUBLIC STRUCTURES
 * ========================================================================== */

/**
 * @brief Sensor error flags (bitwise) — defined in ws_protocol.h as WS_SensorError_t
 */

/**
 * @brief Measurement readings from an outdoor node (tagged channel values)
 */
typedef WS_Readings_t WS_NodeReadings_t;

/**
 * @brief Runtime configuration structure
 * @details Contains handles and parameters needed for weather station
 *          operation. Passed to most WS_ functions to enable hardware access.
 */
typedef struct {
  NRF24_Handle_t *nrf;           /**< nRF24L01+ handle */
  PCD8544_t *lcd;                /**< Nokia 5110 LCD handle (can be NULL) */
  DS3231_DateTime *rtc_now;      /**< Pointer to current RTC time */
  char *text_buffer;             /**< Scratch buffer for text formatting */
  size_t text_buffer_size;       /**< Size of text_buffer in bytes */
  GPIO_TypeDef *led_port;        /**< LED GPIO port (can be NULL) */
  uint16_t led_pin;              /**< LED GPIO pin */
  uint8_t channel;               /**< nRF24 RF channel (0-125) */
  uint8_t cmd_measure;           /**< Measurement command byte */
  uint8_t cmd_size;              /**< Command packet size (bytes) */
  uint8_t payload_size;          /**< Data payload size (bytes) */
  uint32_t tx_irq_timeout_ms;    /**< TX interrupt timeout (ms) */
  uint32_t rx_timeout_ms;        /**< RX response timeout (ms) */
  uint32_t comm_watchdog_timeout_ms; /**< Max allowed time without valid RX data (ms) */
  UART_HandleTypeDef *huart_pico;/**< UART handle for Pico W CSV output (can be NULL) */
} WS_RuntimeConfig_t;

/**
 * @brief Node state tracking structure
 * @details Maintains state for a single outdoor sensor node including
 *          addresses, flags, timestamps, and measurement data.
 */
typedef struct {
  uint8_t tx_addr[5];                  /**< Transmit address for this node */
  uint8_t rx_addr[5];                  /**< Receive address for this node */
  uint8_t rx_pipe;                     /**< RX pipe number on central (1-5) */
  volatile uint8_t measurement_pending;/**< Flag: measurement request pending */
  uint32_t tx_start_tick;              /**< Timestamp: TX start time */
  uint32_t response_start_tick;        /**< Timestamp: response wait start */
  uint8_t last_status;                 /**< Last nRF24 status register value */
  uint8_t retry_count;                 /**< Retry counter for failures */
  WS_NodeStateEnum_t state;            /**< Current node state */
  WS_NodeReadings_t data;              /**< Latest measurement readings */
} WS_NodeState_t;

/**
 * @brief Weather Station Manager main context structure
 * @details Central context managing all nodes and application state.
 *          Should be initialized with WS_InitManager() before use.
 */
typedef struct {
  volatile uint8_t nrf_irq_flag;       /**< Flag: nRF24 interrupt pending */
  uint8_t active_node;                 /**< Index of currently active node */
  uint8_t node_count;                  /**< Total number of managed nodes */
  volatile uint8_t latest_data_valid;  /**< Flag: at least one node has valid data */
  uint8_t latest_node_index;           /**< Index of node with the latest valid data */
  uint32_t last_successful_rx_tick;    /**< Tick timestamp of last valid RX payload */
  DS3231_DateTime last_successful_rx_time; /**< RTC date/time of last valid measurement */
  volatile uint8_t last_successful_rx_time_valid; /**< 1 when last_successful_rx_time is valid */
  volatile uint8_t comm_watchdog_tripped; /**< 1 when communication watchdog timed out */
  uint8_t cycle_nodes_remaining;       /**< Nodes left in scheduled multi-node cycle */
  uint32_t next_measure_earliest_tick; /**< Earliest tick for next TX in a multi-node cycle */
  WS_AppState_t app_state;             /**< Current application state */
  WS_NodeState_t nodes[WS_MAX_NODES];  /**< Array of node state structures */
} WS_Manager_t;

/* ============================================================================
 * PUBLIC API - INITIALIZATION
 * ========================================================================== */

/**
 * @brief Initializes the Weather Station Manager
 * @param[out] ctx Manager context to initialize (must not be NULL)
 * @param[in] tx_addrs Array of TX addresses for each node (5 bytes each)
 * @param[in] rx_addrs Array of RX addresses for each node (5 bytes each)
 * @param[in] node_count Number of outdoor nodes to manage (1-WS_MAX_NODES)
 */
void WS_InitManager(WS_Manager_t *ctx, const uint8_t tx_addrs[][5], const uint8_t rx_addrs[][5], uint8_t node_count);

/* ============================================================================
 * PUBLIC API - NODE ACCESS
 * ========================================================================== */

/**
 * @brief Gets a pointer to the currently active node state
 * @param[in,out] ctx Manager context
 * @return Pointer to active node state, or NULL if ctx is NULL
 */
WS_NodeState_t *WS_GetActiveNode(WS_Manager_t *ctx);

/**
 * @brief Gets a const pointer to the currently active node state
 * @param[in] ctx Manager context
 * @return Const pointer to active node state, or NULL if invalid
 */
const WS_NodeState_t *WS_GetActiveNodeConst(const WS_Manager_t *ctx);

/* ============================================================================
 * PUBLIC API - INTERRUPT MANAGEMENT
 * ========================================================================== */

/**
 * @brief Sets the nRF24 IRQ pending flag
 * @param[in,out] ctx Manager context
 */
void WS_SetIrqFlag(WS_Manager_t *ctx);

/**
 * @brief Clears the nRF24 IRQ pending flag
 * @param[in,out] ctx Manager context
 */
void WS_ClearIrqFlag(WS_Manager_t *ctx);

/**
 * @brief Checks if status polling is needed (IRQ pin may have failed)
 * @param[in] ctx Manager context
 * @return true if TX or RX operation is pending and needs status check
 */
bool WS_ShouldFallbackToStatusRead(const WS_Manager_t *ctx);

/* ============================================================================
 * PUBLIC API - MEASUREMENT CONTROL
 * ========================================================================== */

/**
 * @brief Requests a measurement from the active outdoor node
 * @param[in,out] ctx Manager context
 */
void WS_RequestMeasurementForActiveNode(WS_Manager_t *ctx);

/**
 * @brief Requests measurements from all outdoor nodes in sequence
 * @param[in,out] ctx Manager context
 * @details Starts a round-robin cycle at the current active node. After each
 *          successful response the next node is queued automatically until all
 *          nodes in node_count have been polled.
 */
void WS_RequestMeasurementCycle(WS_Manager_t *ctx);

/**
 * @brief Clears the measurement pending flag for the active node
 * @param[in,out] ctx Manager context
 */
void WS_ConsumePendingForActiveNode(WS_Manager_t *ctx);

/* ============================================================================
 * PUBLIC API - TRANSMISSION MANAGEMENT
 * ========================================================================== */

/**
 * @brief Initiates a transmission sequence for the active node
 * @param[in,out] ctx Manager context
 * @param[in] now_tick Current tick count (timestamp)
 */
void WS_StartTxForActiveNode(WS_Manager_t *ctx, uint32_t now_tick);
/**
 * @brief Marks the result of a transmission (from IRQ handler)
 * @param[in,out] ctx Manager context
 * @param[in] ok true if TX successful (TX_DS), false if failed (MAX_RT)
 * @param[in] status nRF24 status register value
 */
void WS_MarkTxResultFromIrq(WS_Manager_t *ctx, bool ok, uint8_t status);

/**
 * @brief Consumes and processes a completed TX event
 * @param[in,out] ctx Manager context
 * @param[in] now_tick Current tick count
 * @return WS_TX_EVENT_OK if transmission succeeded,
 *         WS_TX_EVENT_FAIL if failed,
 *         WS_TX_EVENT_NONE if no event pending
 */
WS_TxEvent_t WS_ConsumeTxEvent(WS_Manager_t *ctx, uint32_t now_tick);

/* ============================================================================
 * PUBLIC API - TIMEOUT MANAGEMENT
 * ========================================================================== */

/**
 * @brief Checks if active node's transmission has timed out
 * @param[in] ctx Manager context
 * @param[in] now_tick Current tick count
 * @param[in] timeout_ms Timeout threshold in milliseconds
 * @return true if TX is in progress and has exceeded timeout
 */
bool WS_IsActiveTxTimedOut(const WS_Manager_t *ctx, uint32_t now_tick, uint32_t timeout_ms);

/**
 * @brief Handles a transmission timeout
 * @param[in,out] ctx Manager context
 * @param[in] status nRF24 status register value
 */
void WS_HandleActiveTxTimeout(WS_Manager_t *ctx, uint8_t status);

/**
 * @brief Checks if active node's response wait has timed out
 * @param[in] ctx Manager context
 * @param[in] now_tick Current tick count
 * @param[in] timeout_ms Timeout threshold in milliseconds
 * @return true if awaiting response and has exceeded timeout
 */
bool WS_IsActiveRxTimedOut(const WS_Manager_t *ctx, uint32_t now_tick, uint32_t timeout_ms);

/**
 * @brief Handles a response wait timeout
 * @param[in,out] ctx Manager context
 * @param[in] status nRF24 status register value
 */
void WS_HandleActiveRxTimeout(WS_Manager_t *ctx, uint8_t status);

/* ============================================================================
 * PUBLIC API - DATA RECEPTION
 * ========================================================================== */

/**
 * @brief Marks that measurement data has been received from the active node
 * @param[in,out] ctx Manager context
 * @param[in] data Received measurement data (can be NULL for status-only)
 * @param[in] status nRF24 status register value
 */
void WS_MarkActiveDataReceived(WS_Manager_t *ctx, const WS_NodeReadings_t *data, uint8_t status);

/**
 * @brief Checks and consumes data ready flag for the active node
 * @param[in,out] ctx Manager context
 * @return true if data was ready (flag cleared), false otherwise
 */
bool WS_ConsumeActiveDataReady(WS_Manager_t *ctx);

/**
 * @brief Retrieves the most recent valid measurement from any node
 * @param[in] ctx Manager context
 * @param[out] out_data Buffer to receive measurement data
 * @return true if valid data was copied, false if no valid data available
 */
bool WS_GetLatestMeasurement(const WS_Manager_t *ctx, WS_NodeReadings_t *out_data);

/* ============================================================================
 * PUBLIC API - NODE SCHEDULING
 * ========================================================================== */

/**
 * @brief Advances to the next outdoor node in round-robin fashion
 * @param[in,out] ctx Manager context
 */
void WS_ScheduleNextNode(WS_Manager_t *ctx);

/* ============================================================================
 * PUBLIC API - RADIO INITIALIZATION & EVENT PROCESSING
 * ========================================================================== */

/**
 * @brief Initializes and configures the nRF24L01+ transceiver
 * @param[in,out] ctx Manager context
 * @param[in] cfg Runtime configuration containing nRF24 parameters
 * @return HAL_OK on success, HAL_ERROR on failure
 */
HAL_StatusTypeDef WS_InitRadioAndStart(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg);

/**
 * @brief Main event processing loop - must be called periodically
 * @param[in,out] ctx Manager context
 * @param[in] cfg Runtime configuration
 * @param[in] now_tick Current system tick count (typically HAL_GetTick())
 */
void WS_ProcessEventHandler(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg, uint32_t now_tick);

#endif
