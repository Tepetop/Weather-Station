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

#include <PCD_LCD/PCD8544.h>
#include <PCD_LCD/PCD8544_Drawing.h>
#include <PCD_LCD/PCD8544_Menu.h>
#include <encoder.h>

/* ============================================================================
 * PUBLIC CONSTANTS
 * ========================================================================== */

/** @brief Maximum number of outdoor nodes supported */
#define WS_MAX_NODES 4U

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
 * @brief Sensor error flags (bitwise)
 * @details Matches Sensor_Error_t in OutdoorUnit. Each bit indicates a
 *          sensor failure on the remote measurement station.
 */
typedef enum {
  WS_SENSOR_OK       = 0,        /**< All sensors operational */
  WS_SENSOR_ERR_SI7021  = (1 << 0), /**< SI7021 (temp/humidity) error */
  WS_SENSOR_ERR_BMP280  = (1 << 1), /**< BMP280 (pressure/temp) error */
  WS_SENSOR_ERR_TSL2561 = (1 << 2)  /**< TSL2561 (light) error */
} WS_SensorError_t;

/**
 * @brief Measurement data structure for radio transmission
 * @details Contains sensor readings from outdoor unit. Layout must match
 *          Measurement_Data_t in OutdoorUnit exactly (unpacked, 24 bytes).
 *          The struct is NOT packed — both sides use ARM Cortex-M3 with
 *          identical GCC ABI, so natural alignment is consistent.
 */
typedef struct {
  float si7021_temp;             /**< Temperature from SI7021 sensor (°C) */
  float si7021_hum;              /**< Humidity from SI7021 sensor (%) */
  float bmp280_temp;             /**< Temperature from BMP280 sensor (°C) */
  float bmp280_press;            /**< Pressure from BMP280 sensor (hPa) */
  float tsl2561_lux;             /**< Light intensity from TSL2561 (lux) */
  uint8_t sensorStatus;          /**< Bitwise sensor health flags (WS_SensorError_t) */
} WS_MeasurementData_t;

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
  volatile uint8_t awaiting_response;  /**< Flag: waiting for data response */
  volatile uint8_t tx_in_progress;     /**< Flag: transmission in progress */
  volatile uint8_t tx_done;            /**< Flag: transmission completed */
  volatile uint8_t tx_ok;              /**< Flag: transmission successful */
  volatile uint8_t data_received;      /**< Flag: data received and ready */
  uint32_t tx_start_tick;              /**< Timestamp: TX start time */
  uint32_t response_start_tick;        /**< Timestamp: response wait start */
  uint8_t last_status;                 /**< Last nRF24 status register value */
  uint8_t retry_count;                 /**< Retry counter for failures */
  WS_NodeStateEnum_t state;            /**< Current node state */
  WS_MeasurementData_t data;           /**< Latest measurement data */
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
  volatile uint8_t latest_data_valid;  /**< Flag: latest_data contains valid data */
  WS_MeasurementData_t latest_data;    /**< Cache of most recent measurement */
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
 * @brief Marks that the active node is waiting for a response
 * @param[in,out] ctx Manager context
 * @param[in] now_tick Current tick count (timestamp)
 */
void WS_MarkActiveResponseWaiting(WS_Manager_t *ctx, uint32_t now_tick);

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
void WS_MarkActiveDataReceived(WS_Manager_t *ctx, const WS_MeasurementData_t *data, uint8_t status);

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
bool WS_GetLatestMeasurement(const WS_Manager_t *ctx, WS_MeasurementData_t *out_data);

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

/* ============================================================================
 * PUBLIC API - USER INTERFACE & DISPLAY
 * ========================================================================== */

/**
 * @brief UI runtime context containing references to all required components
 * @details Must be initialized before calling any WS_UI_* functions
 */
/**
 * @brief View state machine enumeration
 * @details Tracks the active view/screen of the weather station UI
 */
typedef enum {
  WS_VIEW_MENU = 0,               /**< Normal menu navigation */
  WS_VIEW_CHART,                   /**< Chart view active */
  WS_VIEW_STATIONS_STATUS,         /**< Stations status view active */
  WS_VIEW_DEFAULT_MEASUREMENT      /**< Default measurement display active */
} WS_ViewState_t;

typedef struct {
  WS_Manager_t *ws_ctx;           /**< Weather station manager context */
  WS_RuntimeConfig_t *ws_cfg;     /**< Runtime configuration */
  PCD8544_t *lcd;                 /**< LCD display handle */
  Menu_Context_t *menu_ctx;       /**< Menu context */
  Encoder_t *encoder;             /**< Encoder handle for button input */
  DS3231_DateTime *rtc_now;       /**< Current RTC time */
  char *text_buffer;              /**< Scratch buffer for text formatting */
  size_t text_buffer_size;        /**< Size of text buffer */
  volatile uint8_t chart_data_dirty; /**< Flag: new chart data available, redraw needed */
  WS_ViewState_t view_state;      /**< Current view state machine state */
} WS_UIContext_t;

/**
 * @brief Chart instances for all measurement types (owned by weather_station.c)
 */
extern PCD8544_ChartData_t WS_TemperatureChart;
extern PCD8544_ChartData_t WS_HumidityChart;
extern PCD8544_ChartData_t WS_PressureChart;

/**
 * @brief Global UI context (must be set before using menu functions)
 */
extern WS_UIContext_t WS_UI;

/**
 * @brief Initialize UI context with required handles
 * @param[out] ui UI context to initialize
 * @param[in] ws_ctx Weather station manager context
 * @param[in] ws_cfg Weather station runtime config
 * @param[in] lcd LCD display handle
 * @param[in] menu_ctx Menu context
 * @param[in] rtc_now RTC datetime handle
 * @param[in] text_buffer Scratch buffer for text
 * @param[in] text_buffer_size Size of scratch buffer
 */
void WS_UI_Init(WS_UIContext_t *ui, WS_Manager_t *ws_ctx, WS_RuntimeConfig_t *ws_cfg,
                PCD8544_t *lcd, Menu_Context_t *menu_ctx, Encoder_t *encoder,
                DS3231_DateTime *rtc_now, char *text_buffer, size_t text_buffer_size);

/**
 * @brief Initialize all chart data structures with default settings
 */
void WS_UI_InitCharts(void);

/**
 * @brief Add new measurement data point to all charts
 * @param[in] data Measurement data to add
 * @param[in] hour Hour of measurement (0-23)
 * @param[in] minute Minute of measurement (0-59)
 */
void WS_UI_AddMeasurementToCharts(const WS_MeasurementData_t *data, uint8_t hour, uint8_t minute);

/**
 * @brief Display live measurement data on LCD (menu function callback)
 * @details Shows temperature, humidity, pressure, light, and time
 */
void WS_UI_MeasurementDisplay(void);

/**
 * @brief Enter temperature chart view (menu function callback)
 */
void WS_UI_ChartTemperature(void);

/**
 * @brief Enter humidity chart view (menu function callback)
 */
void WS_UI_ChartHumidity(void);

/**
 * @brief Enter pressure chart view (menu function callback)
 */
void WS_UI_ChartPressure(void);

/**
 * @brief Enter light intensivity chart view (menu function callback)
 */
void WS_UI_ChartLux(void);

/**
 * @brief Chart view main task - update and redraw chart while in view
 * @details Call in main loop when menuContext.state.InChartView == 1
 */
void WS_UI_ChartViewTask(void);

/**
 * @brief Request a new measurement from the active node (menu function callback)
 */
void WS_UI_TakeMeasurement(void);

/**
 * @brief Enter stations status view (menu callback)
 * @details Shows list of all configured stations with their current state.
 *          Call once from menu. Use WS_UI_StationsStatusTask() in main loop.
 */
void WS_UI_StationsStatus(void);

/**
 * @brief Stations status view task
 * @details Call in main loop when menuContext.state.InStationsStatusView == 1
 *          to periodically update station status display.
 */
void WS_UI_StationsStatusTask(void);

/**
 * @brief Main view state machine task - call in main loop
 * @details Handles all view transitions and updates:
 *          - WS_VIEW_MENU: normal menu + encoder navigation
 *          - WS_VIEW_CHART: chart display with button exit
 *          - WS_VIEW_STATIONS_STATUS: station status with button exit
 *          - WS_VIEW_DEFAULT_MEASUREMENT: live measurements + menu navigation
 */
void WS_UI_ViewTask(void);

#endif
