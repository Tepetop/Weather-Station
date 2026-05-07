/**
 * @file weather_station.c
 * @brief Weather Station Manager implementation for STM32F103 Indoor Unit
 * @details Manages communication with outdoor sensor units via nRF24L01+,
 *          handles data processing, display updates, and system state management.
 * @author Weather Station Team
 * @date 2026
 */

#include "weather_station.h"
#include "weather_station_ui.h"
#include "debug_log.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * PRIVATE CONSTANTS
 * ========================================================================== */

/** @brief Minimum allowed node count */
#define WS_MIN_NODE_COUNT 1U

/** @brief Decimal scaling factor base */
#define WS_DECIMAL_SCALE_BASE 10

/** @brief Rounding offset for positive float conversion */
#define WS_ROUNDING_OFFSET 0.5f

/** @brief Status register pipe shift offset */
#define WS_STATUS_PIPE_SHIFT 1U

/** @brief Status register pipe mask */
#define WS_STATUS_PIPE_MASK 0x07U

/** @brief Maximum retries before forcing full radio recovery */
#define WS_MAX_RETRIES 3U

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS - LED Control
 * ========================================================================== */

/**
 * @brief Controls the LED state
 * @param[in] cfg Runtime configuration containing LED GPIO port and pin
 * @param[in] state Desired GPIO state (GPIO_PIN_SET or GPIO_PIN_RESET)
 * @details Safely sets LED state with NULL pointer checks. Used for visual
 *          feedback during radio transmission/reception operations.
 */
static void ws_set_led(const WS_RuntimeConfig_t *cfg, GPIO_PinState state) {
  if ((cfg != NULL) && (cfg->led_port != NULL)) {
    HAL_GPIO_WritePin(cfg->led_port, cfg->led_pin, state);
  }
}

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS - Radio Configuration
 * ========================================================================== */

/**
 * @brief Applies the active node's TX address and Pipe 0 (ACK) address
 * @param[in,out] ctx Weather station manager context
 * @param[in] cfg Runtime configuration containing nRF24 handle
 * @details Sets TX_ADDR and RX_ADDR_P0 (for auto-ACK) to the active node's
 *          transmit address. RX data pipes (1-4) are statically configured
 *          during radio init and do not need to be changed per node.
 */
static void ws_apply_active_node_address(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg) {
  const WS_NodeState_t *node = WS_GetActiveNodeConst(ctx);
  if ((cfg == NULL) || (cfg->nrf == NULL) || (node == NULL)) {
    return;
  }

  NRF24_SetTXAddress(cfg->nrf, node->tx_addr, 5U);
  NRF24_SetRXAddress(cfg->nrf, 0U, node->tx_addr, 5U);
}

/**
 * @brief Formats a floating-point value as a fixed-point string
 * @param[out] dst Destination buffer for formatted string
 * @param[in] dst_size Size of destination buffer
 * @param[in] value Floating-point value to format
 * @param[in] decimals Number of decimal places (0-N)
 * @details Converts float to fixed-point representation without using printf %f
 *          to avoid pulling in heavy floating-point formatting libs. Handles
 *          negative values and proper rounding. Example: 23.456 with decimals=2
 *          produces "23.45" (rounded).
 */
static void ws_format_fixed(char *dst, size_t dst_size, float value, uint8_t decimals) {
  int32_t scale = 1;
  for (uint8_t i = 0U; i < decimals; i++) {
    scale *= WS_DECIMAL_SCALE_BASE;
  }

  float scaled_f = value * (float)scale;
  if (scaled_f >= 0.0f) {
    scaled_f += WS_ROUNDING_OFFSET;
  } else {
    scaled_f -= WS_ROUNDING_OFFSET;
  }

  int32_t scaled = (int32_t)scaled_f;
  int32_t abs_scaled = (scaled < 0) ? -scaled : scaled;
  int32_t int_part = abs_scaled / scale;
  int32_t frac_part = abs_scaled % scale;

  if (decimals == 0U) {
    snprintf(dst, dst_size, "%s%ld", (scaled < 0) ? "-" : "", (long)int_part);
  } else {
    snprintf(dst, dst_size, "%s%ld.%0*ld", (scaled < 0) ? "-" : "", (long)int_part, decimals, (long)frac_part);
  }
}

/**
 * @brief Formats sensor status bitmask to Pico W status text
 * @param[out] dst Destination buffer
 * @param[in] dst_size Destination buffer size
 * @param[in] sensor_status Sensor status bitmask (WS_SensorError_t)
 */
static void ws_format_sensor_status(char *dst, size_t dst_size, uint8_t sensor_status) {
  uint8_t known_status = sensor_status &
      ((uint8_t)WS_SENSOR_ERR_SI7021 | (uint8_t)WS_SENSOR_ERR_BMP280 | (uint8_t)WS_SENSOR_ERR_TSL2561);

  if ((dst == NULL) || (dst_size == 0U)) {
    return;
  }

  switch (known_status) {
    case (uint8_t)WS_SENSOR_OK:
      snprintf(dst, dst_size, "OK");
      break;
    case (uint8_t)WS_SENSOR_ERR_SI7021:
      snprintf(dst, dst_size, "ERR:SI7021");
      break;
    case (uint8_t)WS_SENSOR_ERR_BMP280:
      snprintf(dst, dst_size, "ERR:BMP280");
      break;
    case (uint8_t)WS_SENSOR_ERR_TSL2561:
      snprintf(dst, dst_size, "ERR:TSL2561");
      break;
    case (uint8_t)(WS_SENSOR_ERR_SI7021 | WS_SENSOR_ERR_BMP280):
      snprintf(dst, dst_size, "ERR:SI7021,BMP280");
      break;
    case (uint8_t)(WS_SENSOR_ERR_SI7021 | WS_SENSOR_ERR_TSL2561):
      snprintf(dst, dst_size, "ERR:SI7021,TSL2561");
      break;
    case (uint8_t)(WS_SENSOR_ERR_BMP280 | WS_SENSOR_ERR_TSL2561):
      snprintf(dst, dst_size, "ERR:BMP280,TSL2561");
      break;
    case (uint8_t)(WS_SENSOR_ERR_SI7021 | WS_SENSOR_ERR_BMP280 | WS_SENSOR_ERR_TSL2561):
      snprintf(dst, dst_size, "ERR:SI7021,BMP280,TSL2561");
      break;
    default:
      snprintf(dst, dst_size, "ERR:UNKNOWN");
      break;
  }
}

/**
 * @brief Sends one measurement record to Pico W over UART as CSV line
 * @param[in] ctx Weather station manager context
 * @param[in] cfg Runtime configuration containing UART handle and RTC
 * @param[in] node_idx Node index mapped to station id S0..S3
 */
static void ws_send_measurement_uart(const WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg, uint8_t node_idx) {
  if ((ctx == NULL) || (cfg == NULL) || (cfg->huart_pico == NULL) || (node_idx >= ctx->node_count)) {
    return;
  }

  const WS_NodeState_t *node = &ctx->nodes[node_idx];
  char si7021_temp_text[16];
  char si7021_hum_text[16];
  char bmp280_temp_text[16];
  char bmp280_press_text[16];
  char tsl2561_lux_text[16];
  char status_text[40];
  char line[128];
  uint8_t year = 0U;
  uint8_t month = 0U;
  uint8_t date = 0U;
  uint8_t hours = 0U;
  uint8_t minutes = 0U;
  uint8_t seconds = 0U;

  ws_format_fixed(si7021_temp_text, sizeof(si7021_temp_text), node->data.si7021_temp, 2U);
  ws_format_fixed(si7021_hum_text, sizeof(si7021_hum_text), node->data.si7021_hum, 2U);
  ws_format_fixed(bmp280_temp_text, sizeof(bmp280_temp_text), node->data.bmp280_temp, 2U);
  ws_format_fixed(bmp280_press_text, sizeof(bmp280_press_text), node->data.bmp280_press, 2U);
  ws_format_fixed(tsl2561_lux_text, sizeof(tsl2561_lux_text), node->data.tsl2561_lux, 2U);
  ws_format_sensor_status(status_text, sizeof(status_text), node->data.sensorStatus);

  if (cfg->rtc_now != NULL) {
    year = cfg->rtc_now->year;
    month = cfg->rtc_now->month;
    date = cfg->rtc_now->date;
    hours = cfg->rtc_now->hours;
    minutes = cfg->rtc_now->minutes;
    seconds = cfg->rtc_now->seconds;
  }

  int line_len = snprintf(
      line,
      sizeof(line),
      "DATA:20%02u-%02u-%02uT%02u:%02u:%02u,S%u,%s,%s,%s,%s,%s,%s\n",
      (unsigned int)year,
      (unsigned int)month,
      (unsigned int)date,
      (unsigned int)hours,
      (unsigned int)minutes,
      (unsigned int)seconds,
      (unsigned int)node_idx,
      si7021_temp_text,
      si7021_hum_text,
      bmp280_temp_text,
      bmp280_press_text,
      tsl2561_lux_text,
      status_text);

  if ((line_len <= 0) || ((size_t)line_len >= sizeof(line))) {
    return;
  }

  (void)HAL_UART_Transmit(cfg->huart_pico, (uint8_t *)line, (uint16_t)line_len, 100U);
}

/**
 * @brief Configures nRF24L01+ to enter receive mode
 * @param[in,out] ctx Weather station manager context
 * @param[in] cfg Runtime configuration containing nRF24 handle
 * @details Clears interrupts and switches radio to RX mode.
 *          RX pipe addresses are statically configured during init
 *          and do not need to be reconfigured between nodes.
 */
static void ws_start_receive(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg) {
  if ((ctx == NULL) || (cfg == NULL) || (cfg->nrf == NULL)) {
    return;
  }

  NRF24_SetMode(cfg->nrf, NRF24_MODE_STANDBY);
  NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_IRQ_MASK);
  NRF24_SetMode(cfg->nrf, NRF24_MODE_RX);
}

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS - Measurement Control
 * ========================================================================== */

/**
 * @brief Sends a measurement command to the active outdoor node
 * @param[in,out] ctx Weather station manager context
 * @param[in] cfg Runtime configuration
 * @param[in] now_tick Current system tick count
 * @details Constructs and transmits a measurement request packet via nRF24.
 *          Updates node state to TX_IN_PROGRESS and timestamps the operation.
 *          Guards against sending if TX is already in progress or awaiting response.
 */
static void ws_send_measure_command(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg, uint32_t now_tick) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if ((node == NULL) || (cfg == NULL) || (cfg->nrf == NULL)) {
    return;
  }

  if ((node->state == WS_NODE_TX_IN_PROGRESS) || (node->state == WS_NODE_WAIT_RESPONSE)) {
    return;
  }

  uint8_t cmd[8] = {0};
  cmd[0] = cfg->cmd_measure;

  ws_apply_active_node_address(ctx, cfg);
  ws_set_led(cfg, GPIO_PIN_SET);
  WS_ClearIrqFlag(ctx);
  WS_StartTxForActiveNode(ctx, now_tick);

  NRF24_SetMode(cfg->nrf, NRF24_MODE_STANDBY);
  NRF24_FlushTX(cfg->nrf);
  NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_IRQ_MASK);
  NRF24_WritePayload(cfg->nrf, cmd, cfg->cmd_size);
  NRF24_SetMode(cfg->nrf, NRF24_MODE_TX);

  Debug_LogNrfTxStart(ctx->active_node);
}

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS - Interrupt Handling
 * ========================================================================== */

/**
 * @brief Handles nRF24L01+ interrupt events
 * @param[in,out] ctx Weather station manager context
 * @param[in] cfg Runtime configuration
 * @details Processes three interrupt sources:
 *          - RX_DR: Data received (reads payload from pipe)
 *          - TX_DS: Transmission successful
 *          - MAX_RT: Maximum retransmissions reached (TX failed)
 *          Updates node state and clears interrupt flags accordingly.
 */
static void ws_handle_irq(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg) {
  if ((ctx == NULL) || (cfg == NULL) || (cfg->nrf == NULL)) {
    return;
  }

  uint8_t status = NRF24_GetStatus(cfg->nrf);
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  node->last_status = status;

  if (status & NRF24_STATUS_RX_DR) {
    uint8_t pipe = (status >> WS_STATUS_PIPE_SHIFT) & WS_STATUS_PIPE_MASK;
    uint8_t rx_data[NRF24_MAX_PAYLOAD_SIZE] = {0};
    uint8_t payload_len = (pipe == 0U) ? cfg->cmd_size : cfg->payload_size;

    NRF24_ReadPayload(cfg->nrf, rx_data, payload_len);
    NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_RX_DR);

    /* Map pipe to node index: pipe 1 → node 0, pipe 2 → node 1, etc. */
    uint8_t node_idx = pipe - 1U;
    if ((pipe >= 1U) && (node_idx < ctx->node_count) &&
        (payload_len >= sizeof(WS_MeasurementData_t))) {
      WS_MeasurementData_t measurement;
      memcpy(&measurement, rx_data, sizeof(WS_MeasurementData_t));

      WS_NodeState_t *rx_node = &ctx->nodes[node_idx];
      memcpy(&rx_node->data, &measurement, sizeof(measurement));
      rx_node->last_status = status;
      rx_node->state = WS_NODE_DATA_READY;
      rx_node->retry_count = 0U;

      ctx->latest_data_valid = 1U;
      ctx->latest_node_index = node_idx;

      ws_set_led(cfg, GPIO_PIN_RESET);
      Debug_LogNrfRxData(node_idx);
    }
  }

  if (status & NRF24_STATUS_TX_DS) {
    NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_TX_DS);
    WS_MarkTxResultFromIrq(ctx, true, status);
    Debug_LogNrfTxResult(1U);
  }

  if (status & NRF24_STATUS_MAX_RT) {
    NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_MAX_RT);
    NRF24_FlushTX(cfg->nrf);
    WS_MarkTxResultFromIrq(ctx, false, status);
    Debug_LogNrfTxResult(0U);
  }
}

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS - Validation
 * ========================================================================== */

/**
 * @brief Clamps node count to valid range [1, WS_MAX_NODES]
 * @param[in] node_count Requested node count
 * @return Clamped value between WS_MIN_NODE_COUNT and WS_MAX_NODES
 * @details Ensures at least one node exists and doesn't exceed array bounds.
 */
static uint8_t ws_clamp_count(uint8_t node_count) {
  if (node_count == 0U) {
    return WS_MIN_NODE_COUNT;
  }
  if (node_count > WS_MAX_NODES) {
    return WS_MAX_NODES;
  }
  return node_count;
}

/**
 * @brief Handles retry scheduling before entering full radio recovery
 * @param[in,out] ctx Manager context
 * @return true when a retry was scheduled, false when recovery is required
 */
static bool ws_schedule_retry_or_recover(WS_Manager_t *ctx) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return false;
  }

  node->retry_count++;
  if (node->retry_count < WS_MAX_RETRIES) {
    node->measurement_pending = 1U;
    node->state = WS_NODE_IDLE;
    ctx->app_state = WS_APP_IDLE;
    return true;
  }

  node->retry_count = 0U;
  WS_ScheduleNextNode(ctx);
  ctx->app_state = WS_APP_ERROR_RECOVERY;
  return false;
}

/* ============================================================================
 * PUBLIC API - Initialization
 * ========================================================================== */

/**
 * @brief Initializes the Weather Station Manager
 * @param[out] ctx Manager context to initialize (must not be NULL)
 * @param[in] tx_addrs Array of TX addresses for each node (5 bytes each)
 * @param[in] rx_addrs Array of RX addresses for each node (5 bytes each)
 * @param[in] node_count Number of outdoor nodes to manage (1-WS_MAX_NODES)
 * @details Zeroes the context, clamps node count to valid range, copies
 *          addresses, and initializes all nodes to IDLE state. Must be called
 *          before any other WS_ functions.
 */
void WS_InitManager(WS_Manager_t *ctx, const uint8_t tx_addrs[][5], const uint8_t rx_addrs[][5], uint8_t node_count) {
  if (ctx == NULL) {
    return;
  }
  memset(ctx, 0, sizeof(*ctx));
  ctx->node_count = ws_clamp_count(node_count);
  ctx->active_node = 0U;
  ctx->latest_data_valid = 0U;
  ctx->latest_node_index = 0U;
  ctx->app_state = WS_APP_IDLE;

  for (uint8_t i = 0U; i < ctx->node_count; i++) {
    if (tx_addrs != NULL) {
      memcpy(ctx->nodes[i].tx_addr, tx_addrs[i], 5U);
    }
    if (rx_addrs != NULL) {
      memcpy(ctx->nodes[i].rx_addr, rx_addrs[i], 5U);
    }
    ctx->nodes[i].rx_pipe = i + 1U;
    ctx->nodes[i].state = WS_NODE_IDLE;
  }
}

/* ============================================================================
 * PUBLIC API - Node Access
 * ========================================================================== */

/**
 * @brief Gets a pointer to the currently active node state
 * @param[in,out] ctx Manager context
 * @return Pointer to active node state, or NULL if ctx is NULL
 * @details Performs bounds checking and wraps active_node index if needed.
 *          Use for read-write access to node state.
 */
WS_NodeState_t *WS_GetActiveNode(WS_Manager_t *ctx) {
  if (ctx == NULL) {
    return NULL;
  }
  if (ctx->active_node >= ctx->node_count) {
    ctx->active_node = 0U;
  }
  return &ctx->nodes[ctx->active_node];
}

/**
 * @brief Gets a const pointer to the currently active node state
 * @param[in] ctx Manager context
 * @return Const pointer to active node state, or NULL if invalid
 * @details Read-only access to active node. Safer for queries that don't
 *          modify state.
 */
const WS_NodeState_t *WS_GetActiveNodeConst(const WS_Manager_t *ctx) {
  if ((ctx == NULL) || (ctx->active_node >= ctx->node_count)) {
    return NULL;
  }
  return &ctx->nodes[ctx->active_node];
}

/* ============================================================================
 * PUBLIC API - Interrupt Management
 * ========================================================================== */

/**
 * @brief Sets the nRF24 IRQ pending flag
 * @param[in,out] ctx Manager context
 * @details Should be called from GPIO EXTI interrupt handler when nRF24 IRQ
 *          pin goes low. Flag is processed by WS_ProcessEventHandler().
 */
void WS_SetIrqFlag(WS_Manager_t *ctx) {
  if (ctx != NULL) {
    ctx->nrf_irq_flag = 1U;
  }
}

/**
 * @brief Clears the nRF24 IRQ pending flag
 * @param[in,out] ctx Manager context
 * @details Used internally after processing interrupt. Not typically needed
 *          in application code.
 */
void WS_ClearIrqFlag(WS_Manager_t *ctx) {
  if (ctx != NULL) {
    ctx->nrf_irq_flag = 0U;
  }
}

/**
 * @brief Checks if status polling is needed (IRQ pin may have failed)
 * @param[in] ctx Manager context
 * @return true if TX or RX operation is pending and needs status check
 * @details Used as fallback mechanism when hardware IRQ pin doesn't trigger.
 *          Enables software polling during critical operations.
 */
bool WS_ShouldFallbackToStatusRead(const WS_Manager_t *ctx) {
  const WS_NodeState_t *node = WS_GetActiveNodeConst(ctx);
  if (node == NULL) {
    return false;
  }
  return (node->state == WS_NODE_TX_IN_PROGRESS) || (node->state == WS_NODE_WAIT_RESPONSE);
}

/* ============================================================================
 * PUBLIC API - Measurement Control
 * ========================================================================== */

/**
 * @brief Requests a measurement from the active outdoor node
 * @param[in,out] ctx Manager context
 * @details Sets measurement_pending flag which triggers command transmission
 *          in the next WS_ProcessEventHandler() call. Resets app state to IDLE.
 */
void WS_RequestMeasurementForActiveNode(WS_Manager_t *ctx) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node != NULL) {
    node->measurement_pending = 1U;
    ctx->app_state = WS_APP_IDLE;
  }
}

/**
 * @brief Clears the measurement pending flag for the active node
 * @param[in,out] ctx Manager context
 * @details Used internally after processing a pending measurement request.
 */
void WS_ConsumePendingForActiveNode(WS_Manager_t *ctx) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node != NULL) {
    node->measurement_pending = 0U;
  }
}

/* ============================================================================
 * PUBLIC API - Transmission Management
 * ========================================================================== */

/**
 * @brief Initiates a transmission sequence for the active node
 * @param[in,out] ctx Manager context
 * @param[in] now_tick Current tick count (timestamp)
 * @details Resets TX/RX flags, timestamps start time, updates state to
 *          TX_IN_PROGRESS. Call before sending any packet.
 */
void WS_StartTxForActiveNode(WS_Manager_t *ctx, uint32_t now_tick) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  node->tx_start_tick = now_tick;
  node->response_start_tick = 0U;
  node->state = WS_NODE_TX_IN_PROGRESS;
}

/**
 * @brief Marks the result of a transmission (from IRQ handler)
 * @param[in,out] ctx Manager context
 * @param[in] ok true if TX successful (TX_DS), false if failed (MAX_RT)
 * @param[in] status nRF24 status register value
 * @details Called from ws_handle_irq() when TX_DS or MAX_RT interrupt occurs.
 *          Sets tx_done flag and tx_ok result for consumption by event handler.
 */
void WS_MarkTxResultFromIrq(WS_Manager_t *ctx, bool ok, uint8_t status) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if ((node == NULL) || (node->state != WS_NODE_TX_IN_PROGRESS)) {
    return;
  }

  node->last_status = status;

  if (ok) {
    node->response_start_tick = HAL_GetTick();
    node->state = WS_NODE_WAIT_RESPONSE;
  } else {
    node->state = WS_NODE_ERROR;
  }
}

/**
 * @brief Consumes and processes a completed TX event
 * @param[in,out] ctx Manager context
 * @param[in] now_tick Current tick count
 * @return WS_TX_EVENT_OK if transmission succeeded,
 *         WS_TX_EVENT_FAIL if failed,
 *         WS_TX_EVENT_NONE if no event pending
 * @details Clears tx_done flag, updates node state based on result.
 *          On success, sets node to WAIT_RESPONSE. On failure, sets to ERROR.
 */
WS_TxEvent_t WS_ConsumeTxEvent(WS_Manager_t *ctx, uint32_t now_tick) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if ((ctx == NULL) || (node == NULL) || (ctx->app_state != WS_APP_WAIT_TX_IRQ)) {
    return WS_TX_EVENT_NONE;
  }

  if (node->state == WS_NODE_WAIT_RESPONSE) {
    if (node->response_start_tick == 0U) {
      node->response_start_tick = now_tick;
    }
    return WS_TX_EVENT_OK;
  }

  if (node->state == WS_NODE_ERROR) {
    return WS_TX_EVENT_FAIL;
  }

  return WS_TX_EVENT_NONE;
}

/* ============================================================================
 * PUBLIC API - Timeout Management
 * ========================================================================== */

/**
 * @brief Checks if active node's transmission has timed out
 * @param[in] ctx Manager context
 * @param[in] now_tick Current tick count
 * @param[in] timeout_ms Timeout threshold in milliseconds
 * @return true if TX is in progress and has exceeded timeout
 * @details Compares elapsed time since tx_start_tick against threshold.
 */
bool WS_IsActiveTxTimedOut(const WS_Manager_t *ctx, uint32_t now_tick, uint32_t timeout_ms) {
  const WS_NodeState_t *node = WS_GetActiveNodeConst(ctx);
  if ((node == NULL) || (node->state != WS_NODE_TX_IN_PROGRESS)) {
    return false;
  }
  return (now_tick - node->tx_start_tick) > timeout_ms;
}

/**
 * @brief Handles a transmission timeout
 * @param[in,out] ctx Manager context
 * @param[in] status nRF24 status register value
 * @details Clears TX flags, records status, and sets node state to ERROR.
 *          Should be followed by recovery actions (e.g., switch to RX mode).
 */
void WS_HandleActiveTxTimeout(WS_Manager_t *ctx, uint8_t status) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  node->last_status = status;
  node->state = WS_NODE_ERROR;
}

/**
 * @brief Checks if active node's response wait has timed out
 * @param[in] ctx Manager context
 * @param[in] now_tick Current tick count
 * @param[in] timeout_ms Timeout threshold in milliseconds
 * @return true if awaiting response and has exceeded timeout
 * @details Compares elapsed time since response_start_tick against threshold.
 */
bool WS_IsActiveRxTimedOut(const WS_Manager_t *ctx, uint32_t now_tick, uint32_t timeout_ms) {
  const WS_NodeState_t *node = WS_GetActiveNodeConst(ctx);
  if ((node == NULL) || (node->state != WS_NODE_WAIT_RESPONSE)) {
    return false;
  }
  return (now_tick - node->response_start_tick) > timeout_ms;
}

/**
 * @brief Handles a response wait timeout
 * @param[in,out] ctx Manager context
 * @param[in] status nRF24 status register value
 * @details Clears awaiting_response flag, records status, sets node to ERROR.
 *          Outdoor node likely didn't respond to measurement request.
 */
void WS_HandleActiveRxTimeout(WS_Manager_t *ctx, uint8_t status) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  node->last_status = status;
  node->state = WS_NODE_ERROR;
}

/* ============================================================================
 * PUBLIC API - Data Reception
 * ========================================================================== */

/**
 * @brief Marks that measurement data has been received from the active node
 * @param[in,out] ctx Manager context
 * @param[in] data Received measurement data (can be NULL for status-only)
 * @param[in] status nRF24 status register value
 * @details Copies data to node state and manager's latest_data cache.
 *          Sets data_received flag and updates state to DATA_READY.
 *          Called from interrupt handler when valid payload is received.
 */
void WS_MarkActiveDataReceived(WS_Manager_t *ctx, const WS_MeasurementData_t *data, uint8_t status) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  if (data != NULL) {
    memcpy(&node->data, data, sizeof(node->data));
    ctx->latest_data_valid = 1U;
    ctx->latest_node_index = ctx->active_node;
  }
  node->last_status = status;
  node->state = WS_NODE_DATA_READY;
}

/**
 * @brief Checks and consumes data ready flag for the active node
 * @param[in,out] ctx Manager context
 * @return true if data was ready (flag cleared), false otherwise
 * @details Clears data_received flag and resets node state to IDLE.
 *          Returns false if no data was ready. Application should process
 *          node->data before calling this function.
 */
bool WS_ConsumeActiveDataReady(WS_Manager_t *ctx) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if ((node == NULL) || (node->state != WS_NODE_DATA_READY)) {
    return false;
  }

  node->retry_count = 0U;
  node->state = WS_NODE_IDLE;
  return true;
}

/* ============================================================================
 * PUBLIC API - Node Scheduling
 * ========================================================================== */

/**
 * @brief Advances to the next outdoor node in round-robin fashion
 * @param[in,out] ctx Manager context
 * @details Increments active_node index with wraparound. Used for polling
 *          multiple outdoor units sequentially. No-op if only one node exists.
 */
void WS_ScheduleNextNode(WS_Manager_t *ctx) {
  if ((ctx == NULL) || (ctx->node_count <= 1U)) {
    return;
  }

  ctx->active_node++;
  if (ctx->active_node >= ctx->node_count) {
    ctx->active_node = 0U;
  }
}

/**
 * @brief Retrieves the most recent valid measurement from any node
 * @param[in] ctx Manager context
 * @param[out] out_data Buffer to receive measurement data
 * @return true if valid data was copied, false if no valid data available
 * @details Returns the cached latest_data if latest_data_valid flag is set.
 *          Useful for display updates when you need the most recent reading
 *          regardless of which node it came from.
 */
bool WS_GetLatestMeasurement(const WS_Manager_t *ctx, WS_MeasurementData_t *out_data) {
  if ((ctx == NULL) || (out_data == NULL) || (ctx->latest_data_valid == 0U) ||
      (ctx->latest_node_index >= ctx->node_count)) {
    return false;
  }

  memcpy(out_data, &ctx->nodes[ctx->latest_node_index].data, sizeof(*out_data));
  return true;
}

/* ============================================================================
 * PUBLIC API - Radio Initialization
 * ========================================================================== */

/**
 * @brief Initializes and configures the nRF24L01+ transceiver (multiceiver)
 * @param[in,out] ctx Manager context
 * @param[in] cfg Runtime configuration containing nRF24 parameters
 * @return HAL_OK on success, HAL_ERROR on failure
 * @details Configures:
 *          - RF channel, data rate (1Mbps), power (max)
 *          - CRC (2-byte), address width (5 bytes)
 *          - Auto-retransmit (10 retries, 1500us delay)
 *          - Pipe 0 for auto-ACK (dynamic, follows TX_ADDR)
 *          - Pipes 1-N statically mapped to outdoor nodes (multiceiver)
 *          Flushes FIFOs and enters receive mode.
 */
HAL_StatusTypeDef WS_InitRadioAndStart(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg) {
  if ((ctx == NULL) || (cfg == NULL) || (cfg->nrf == NULL)) {
    return HAL_ERROR;
  }

  NRF24_SetChannel(cfg->nrf, cfg->channel);
  NRF24_SetDataRate(cfg->nrf, NRF24_DR_1MBPS);
  NRF24_SetPALevel(cfg->nrf, NRF24_PA_MAX);
  NRF24_SetCRC(cfg->nrf, NRF24_CRC_2B);
  NRF24_SetAddressWidth(cfg->nrf, NRF24_AW_5);
  NRF24_SetAutoRetr(cfg->nrf, 1U, 10U);

  /* Pipe 0: auto-ACK (set to active node TX address) */
  ws_apply_active_node_address(ctx, cfg);
  NRF24_EnablePipe(cfg->nrf, 0U, 1U);
  NRF24_SetAutoAck(cfg->nrf, 0U, 1U);
  NRF24_SetPayloadSize(cfg->nrf, 0U, cfg->cmd_size);

  /* Pipes 1-N: static RX addresses for each outdoor node (multiceiver) */
  for (uint8_t i = 0U; i < ctx->node_count; i++) {
    uint8_t pipe = ctx->nodes[i].rx_pipe;
    NRF24_SetRXAddress(cfg->nrf, pipe, ctx->nodes[i].rx_addr, 5U);
    NRF24_EnablePipe(cfg->nrf, pipe, 1U);
    NRF24_SetAutoAck(cfg->nrf, pipe, 1U);
    NRF24_SetPayloadSize(cfg->nrf, pipe, cfg->payload_size);
  }

  /* Disable unused pipes */
  for (uint8_t p = ctx->node_count + 1U; p <= 5U; p++) {
    NRF24_EnablePipe(cfg->nrf, p, 0U);
  }

  NRF24_FlushTX(cfg->nrf);
  NRF24_FlushRX(cfg->nrf);
  ws_start_receive(ctx, cfg);
  ws_set_led(cfg, GPIO_PIN_SET);

  ctx->app_state = WS_APP_IDLE;
  return HAL_OK;
}

/* ============================================================================
 * PUBLIC API - Main Event Loop
 * ========================================================================== */

/**
 * @brief Main event processing loop - must be called periodically
 * @param[in,out] ctx Manager context
 * @param[in] cfg Runtime configuration
 * @param[in] now_tick Current system tick count (typically HAL_GetTick())
 * @details Processes all weather station events:
 *          - IRQ handling (with status polling fallback)
 *          - TX completion and failure
 *          - TX/RX timeout detection
 *          - Pending measurement requests
 *          - Data reception
 *          - Node scheduling
 *          Should be called in main loop or from timer callback.
 */
void WS_ProcessEventHandler(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg, uint32_t now_tick) {
  if ((ctx == NULL) || (cfg == NULL) || (cfg->nrf == NULL)) {
    return;
  }

  if (ctx->app_state == WS_APP_ERROR_RECOVERY) {
    /* Recover radio state after TX/RX failures (MAX_RT/timeout). */
    if (WS_InitRadioAndStart(ctx, cfg) == HAL_OK) {
      Debug_Log("LOG:NRF:RECOVERY_OK");
    } else {
      Debug_Log("LOG:NRF:RECOVERY_FAIL");
    }
    ctx->app_state = WS_APP_IDLE;
    return;
  }

  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  if ((ctx->nrf_irq_flag == 0U) && WS_ShouldFallbackToStatusRead(ctx)) {
    uint8_t should_poll = 0U;

    if ((node->state == WS_NODE_TX_IN_PROGRESS) &&
        ((now_tick - node->tx_start_tick) > ((cfg->tx_irq_timeout_ms * 3U) / 4U))) {
      should_poll = 1U;
    }

    if ((node->state == WS_NODE_WAIT_RESPONSE) &&
        ((now_tick - node->response_start_tick) > ((cfg->rx_timeout_ms * 3U) / 4U))) {
      should_poll = 1U;
    }

    if (should_poll != 0U) {
      uint8_t st = NRF24_GetStatus(cfg->nrf);
      if (st & (NRF24_STATUS_RX_DR | NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT)) {
        WS_SetIrqFlag(ctx);
      }
    }
  }

  if (ctx->nrf_irq_flag != 0U) {
    WS_ClearIrqFlag(ctx);
    ws_handle_irq(ctx, cfg);
  }

  WS_TxEvent_t tx_event = WS_ConsumeTxEvent(ctx, now_tick);

  if (tx_event == WS_TX_EVENT_OK) {
    ws_start_receive(ctx, cfg);
    ctx->app_state = WS_APP_WAIT_RX_DATA;
    return;

  } else if (tx_event == WS_TX_EVENT_FAIL) {
    ws_start_receive(ctx, cfg);
    (void)ws_schedule_retry_or_recover(ctx);
    return;
  }

  if (WS_IsActiveTxTimedOut(ctx, now_tick, cfg->tx_irq_timeout_ms)) {
    uint8_t st = NRF24_GetStatus(cfg->nrf);
    if (st & (NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT)) {
      WS_SetIrqFlag(ctx);
      ws_handle_irq(ctx, cfg);

      tx_event = WS_ConsumeTxEvent(ctx, now_tick);
      if (tx_event == WS_TX_EVENT_OK) {
        ws_start_receive(ctx, cfg);
        ctx->app_state = WS_APP_WAIT_RX_DATA;
        return;
      }
      if (tx_event == WS_TX_EVENT_FAIL) {
        ws_start_receive(ctx, cfg);
        (void)ws_schedule_retry_or_recover(ctx);
        return;
      }
    }
  }

  if (WS_IsActiveTxTimedOut(ctx, now_tick, cfg->tx_irq_timeout_ms) && (node->state == WS_NODE_TX_IN_PROGRESS)) {
    WS_HandleActiveTxTimeout(ctx, node->last_status);
    ws_start_receive(ctx, cfg);
    Debug_LogNrfTimeout(1U);  /* TX timeout */
    (void)ws_schedule_retry_or_recover(ctx);
    return;
  }

  if (node->measurement_pending != 0U) {
    WS_ConsumePendingForActiveNode(ctx);
    ws_send_measure_command(ctx, cfg, now_tick);
    ctx->app_state = WS_APP_WAIT_TX_IRQ;
  }

  if (WS_IsActiveRxTimedOut(ctx, now_tick, cfg->rx_timeout_ms)) {
    WS_HandleActiveRxTimeout(ctx, node->last_status);
    Debug_LogNrfTimeout(0U);  /* RX timeout */
    ws_start_receive(ctx, cfg);
    (void)ws_schedule_retry_or_recover(ctx);
    return;
  }

  if (WS_ConsumeActiveDataReady(ctx)) {
    ws_send_measurement_uart(ctx, cfg, ctx->active_node);

    /* Add new measurement data to charts when data is received */
    if (WS_UI.rtc_now != NULL) {
      WS_UI_AddMeasurementToCharts(&node->data, WS_UI.rtc_now->hours, WS_UI.rtc_now->minutes);
    }

    WS_ScheduleNextNode(ctx);
    ctx->app_state = WS_APP_DATA_READY;
    return;
  }
}
