/**
 * @file weather_station.c
 * @brief Weather Station Manager implementation for STM32F103 Indoor Unit
 * @details Manages communication with outdoor sensor units via nRF24L01+,
 *          handles data processing, display updates, and system state management.
 * @author Weather Station Team
 * @date 2026
 */

#include "weather_station.h"

#include <PCD_LCD/PCD8544_fonts.h>

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

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS - Display
 * ========================================================================== */

/**
 * @brief Displays a status message on the LCD's bottom line
 * @param[in] cfg Runtime configuration containing LCD handle
 * @param[in] text Status text to display (NULL-terminated string)
 * @details Clears line 5 (bottom line) and writes the status text.
 *          Used for debugging and user feedback during operations.
 * @note Currently unused but available for debugging. Uncomment calls in
 *       WS_ProcessEventHandler() to enable status display.
 */
__attribute__((unused))
static void ws_show_status_line(const WS_RuntimeConfig_t *cfg, const char *text) {
  if ((cfg == NULL) || (cfg->lcd == NULL) || (text == NULL)) {
    return;
  }

  PCD8544_SetCursor(cfg->lcd, 0, 5);
  PCD8544_ClearBufferLine(cfg->lcd, 5);
  PCD8544_WriteString(cfg->lcd, (char *)text);
  PCD8544_UpdateScreen(cfg->lcd);
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

  if ((node->tx_in_progress != 0U) || (node->awaiting_response != 0U)) {
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

  //ws_show_status_line(cfg, "TX START");
}

/**
 * @brief Displays measurement data on the LCD screen
 * @param[in,out] ctx Weather station manager context
 * @param[in] cfg Runtime configuration containing LCD and text buffer
 * @details Formats and displays:
 *          - Current time (from RTC)
 *          - Temperature (°C)
 *          - Humidity (%)
 *          - Pressure (hPa)
 *          - Light intensity (lux)
 *          All values are formatted to 2 decimal places.
 * @note Currently unused but available for display feature. Uncomment call in
 *       WS_ProcessEventHandler() to enable automatic measurement display.
 */
__attribute__((unused))
static void ws_display_measurements(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if ((node == NULL) || (cfg == NULL) || (cfg->lcd == NULL) || (cfg->text_buffer == NULL) || (cfg->text_buffer_size == 0U)) {
    return;
  }

  char value_text[16];
  PCD8544_ClearScreen(cfg->lcd);
  PCD8544_SetFont(cfg->lcd, &Font_6x8);

  PCD8544_SetCursor(cfg->lcd, 0, 0);
  if (cfg->rtc_now != NULL) {
    snprintf(cfg->text_buffer, cfg->text_buffer_size, "%02d:%02d:%02d", cfg->rtc_now->hours, cfg->rtc_now->minutes, cfg->rtc_now->seconds);
  } else {
    snprintf(cfg->text_buffer, cfg->text_buffer_size, "--:--:--");
  }
  PCD8544_WriteString(cfg->lcd, cfg->text_buffer);

  PCD8544_SetCursor(cfg->lcd, 0, 1);
  ws_format_fixed(value_text, sizeof(value_text), node->data.si7021_temp, 2U);
  snprintf(cfg->text_buffer, cfg->text_buffer_size, "T:%sC", value_text);
  PCD8544_WriteString(cfg->lcd, cfg->text_buffer);

  PCD8544_SetCursor(cfg->lcd, 0, 2);
  ws_format_fixed(value_text, sizeof(value_text), node->data.si7021_hum, 2U);
  snprintf(cfg->text_buffer, cfg->text_buffer_size, "H:%s%%", value_text);
  PCD8544_WriteString(cfg->lcd, cfg->text_buffer);

  PCD8544_SetCursor(cfg->lcd, 0, 3);
  ws_format_fixed(value_text, sizeof(value_text), node->data.bmp280_press, 2U);
  snprintf(cfg->text_buffer, cfg->text_buffer_size, "P:%shPa", value_text);
  PCD8544_WriteString(cfg->lcd, cfg->text_buffer);

  PCD8544_SetCursor(cfg->lcd, 0, 4);
  ws_format_fixed(value_text, sizeof(value_text), node->data.tsl2561_lux, 2U);
  snprintf(cfg->text_buffer, cfg->text_buffer_size, "L:%slux", value_text);
  PCD8544_WriteString(cfg->lcd, cfg->text_buffer);

  PCD8544_SetCursor(cfg->lcd, 0, 5);
  PCD8544_WriteString(cfg->lcd, "OK");
  PCD8544_UpdateScreen(cfg->lcd);
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
      rx_node->awaiting_response = 0U;
      rx_node->data_received = 1U;
      rx_node->last_status = status;
      rx_node->state = WS_NODE_DATA_READY;

      memcpy(&ctx->latest_data, &measurement, sizeof(ctx->latest_data));
      ctx->latest_data_valid = 1U;

      ws_set_led(cfg, GPIO_PIN_RESET);
    }
  }

  if (status & NRF24_STATUS_TX_DS) {
    NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_TX_DS);
    WS_MarkTxResultFromIrq(ctx, true, status);
  }

  if (status & NRF24_STATUS_MAX_RT) {
    NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_MAX_RT);
    NRF24_FlushTX(cfg->nrf);
    WS_MarkTxResultFromIrq(ctx, false, status);
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
  return (node->tx_in_progress != 0U) || (node->awaiting_response != 0U);
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

  node->awaiting_response = 0U;
  node->tx_done = 0U;
  node->tx_ok = 0U;
  node->tx_in_progress = 1U;
  node->tx_start_tick = now_tick;
  node->state = WS_NODE_TX_IN_PROGRESS;
  node->retry_count = 0U;
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
  if (node == NULL) {
    return;
  }

  node->last_status = status;
  node->tx_ok = ok ? 1U : 0U;
  node->tx_done = 1U;
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
  if ((node == NULL) || (node->tx_done == 0U)) {
    return WS_TX_EVENT_NONE;
  }

  node->tx_done = 0U;
  node->tx_in_progress = 0U;

  if (node->tx_ok != 0U) {
    node->awaiting_response = 1U;
    node->response_start_tick = now_tick;
    node->state = WS_NODE_WAIT_RESPONSE;
    return WS_TX_EVENT_OK;
  }

  node->state = WS_NODE_ERROR;
  return WS_TX_EVENT_FAIL;
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
  if ((node == NULL) || (node->tx_in_progress == 0U)) {
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

  node->tx_in_progress = 0U;
  node->tx_done = 0U;
  node->tx_ok = 0U;
  node->last_status = status;
  node->state = WS_NODE_ERROR;
}

/**
 * @brief Marks that the active node is waiting for a response
 * @param[in,out] ctx Manager context
 * @param[in] now_tick Current tick count (timestamp)
 * @details Sets awaiting_response flag, timestamps start, updates state to
 *          WAIT_RESPONSE. Called after successful TX when expecting data.
 */
void WS_MarkActiveResponseWaiting(WS_Manager_t *ctx, uint32_t now_tick) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  node->awaiting_response = 1U;
  node->response_start_tick = now_tick;
  node->state = WS_NODE_WAIT_RESPONSE;
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
  if ((node == NULL) || (node->awaiting_response == 0U)) {
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

  node->awaiting_response = 0U;
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
    memcpy(&ctx->latest_data, data, sizeof(ctx->latest_data));
    ctx->latest_data_valid = 1U;
  }
  node->awaiting_response = 0U;
  node->data_received = 1U;
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
  if ((node == NULL) || (node->data_received == 0U)) {
    return false;
  }

  node->data_received = 0U;
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
  if ((ctx == NULL) || (out_data == NULL) || (ctx->latest_data_valid == 0U)) {
    return false;
  }

  memcpy(out_data, &ctx->latest_data, sizeof(*out_data));
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

  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  if ((ctx->nrf_irq_flag == 0U) && WS_ShouldFallbackToStatusRead(ctx)) {
    uint8_t st = NRF24_GetStatus(cfg->nrf);
    if (st & (NRF24_STATUS_RX_DR | NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT)) {
      WS_SetIrqFlag(ctx);
    }
  }

  if (ctx->nrf_irq_flag != 0U) {
    WS_ClearIrqFlag(ctx);
    ws_handle_irq(ctx, cfg);
  }

  WS_TxEvent_t tx_event = WS_ConsumeTxEvent(ctx, now_tick);
  
  if (tx_event == WS_TX_EVENT_OK) {
    ws_start_receive(ctx, cfg);
   // ws_show_status_line(cfg, "WAIT DATA");
    ctx->app_state = WS_APP_WAIT_RX_DATA;

  } else if (tx_event == WS_TX_EVENT_FAIL) {
    ws_start_receive(ctx, cfg);
   // ws_show_status_line(cfg, "TX FAIL  ");
    WS_ScheduleNextNode(ctx);
    ctx->app_state = WS_APP_ERROR_RECOVERY;
  }

  if (WS_IsActiveTxTimedOut(ctx, now_tick, cfg->tx_irq_timeout_ms)) {
    uint8_t st = NRF24_GetStatus(cfg->nrf);
    if (st & (NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT)) {
      WS_SetIrqFlag(ctx);
      ws_handle_irq(ctx, cfg);
    }
  }

  if (WS_IsActiveTxTimedOut(ctx, now_tick, cfg->tx_irq_timeout_ms) && (node->tx_done == 0U)) {
    WS_HandleActiveTxTimeout(ctx, node->last_status);
    ws_start_receive(ctx, cfg);
   // ws_show_status_line(cfg, "TX IRQ TO");
    WS_ScheduleNextNode(ctx);
    ctx->app_state = WS_APP_ERROR_RECOVERY;
  }

  if (node->measurement_pending != 0U) {
    WS_ConsumePendingForActiveNode(ctx);
    ws_send_measure_command(ctx, cfg, now_tick);
    ctx->app_state = WS_APP_WAIT_TX_IRQ;
  }

  if (WS_IsActiveRxTimedOut(ctx, now_tick, cfg->rx_timeout_ms)) {
    WS_HandleActiveRxTimeout(ctx, node->last_status);
    //ws_show_status_line(cfg, "RX TIMEOUT");
    ws_start_receive(ctx, cfg);
    WS_ScheduleNextNode(ctx);
    ctx->app_state = WS_APP_ERROR_RECOVERY;
  }

  if (WS_ConsumeActiveDataReady(ctx)) {

    //ws_display_measurements(ctx, cfg);

    /* Add new measurement data to charts when data is received */
    if ((WS_UI.rtc_now != NULL) && (ctx->latest_data_valid != 0U)) {
      WS_UI_AddMeasurementToCharts(&ctx->latest_data, WS_UI.rtc_now->hours, WS_UI.rtc_now->minutes);
    }

    WS_ScheduleNextNode(ctx);
    ctx->app_state = WS_APP_DATA_READY;
  }
}

/* ============================================================================
 * USER INTERFACE MODULE - Chart Data & Display Functions
 * ========================================================================== */

/** @brief Global chart data instances */
PCD8544_ChartData_t WS_TemperatureChart;
PCD8544_ChartData_t WS_HumidityChart;
PCD8544_ChartData_t WS_PressureChart;
PCD8544_ChartData_t WS_LuxChart;

/** @brief Global UI context */
WS_UIContext_t WS_UI = {0};

/**
 * @brief Initialize UI context with required handles
 */
void WS_UI_Init(WS_UIContext_t *ui, WS_Manager_t *ws_ctx, WS_RuntimeConfig_t *ws_cfg,
                PCD8544_t *lcd, Menu_Context_t *menu_ctx, Encoder_t *encoder,
                DS3231_DateTime *rtc_now, char *text_buffer, size_t text_buffer_size) {
  if (ui == NULL) return;
  
  ui->ws_ctx = ws_ctx;
  ui->ws_cfg = ws_cfg;
  ui->lcd = lcd;
  ui->menu_ctx = menu_ctx;
  ui->encoder = encoder;
  ui->rtc_now = rtc_now;
  ui->text_buffer = text_buffer;
  ui->text_buffer_size = text_buffer_size;
  ui->view_state = WS_VIEW_MENU;
}

/**
 * @brief Initialize all chart data structures
 */
void WS_UI_InitCharts(void) {
  /* Temperature chart - tenths of degree Celsius */
  PCD8544_InitChartData(&WS_TemperatureChart);
  WS_TemperatureChart.decimalPlaces = 1;
  WS_TemperatureChart.chartType = PCD8544_CHART_DOT_LINE;

  /* Humidity chart - tenths of percent */
  PCD8544_InitChartData(&WS_HumidityChart);
  WS_HumidityChart.decimalPlaces = 1;
  WS_HumidityChart.chartType = PCD8544_CHART_DOT_LINE;

  /* Pressure chart - integer hPa */
  PCD8544_InitChartData(&WS_PressureChart);
  WS_PressureChart.decimalPlaces = 0;
  WS_PressureChart.chartType = PCD8544_CHART_DOT_LINE;

  /* Light intensity chart - integer lux */
  PCD8544_InitChartData(&WS_LuxChart);
  WS_LuxChart.decimalPlaces = 0;
  WS_LuxChart.chartType = PCD8544_CHART_DOT_LINE;
}

/**
 * @brief Add new measurement data to all charts
 */
void WS_UI_AddMeasurementToCharts(const WS_MeasurementData_t *data, uint8_t hour, uint8_t minute) {
  if (data == NULL) return;
  
  /* Convert float values to chart units (scaled integers) */
  int16_t tempVal = (int16_t)(data->si7021_temp * 10.0f);  /* tenths of C */
  int16_t humVal = (int16_t)(data->si7021_hum * 10.0f);    /* tenths of % */
  int16_t pressVal = (int16_t)(data->bmp280_press);        /* hPa integer */
  int16_t luxVal = (int16_t)(data->tsl2561_lux);             /* lux integer */

  PCD8544_AddChartPoint(&WS_TemperatureChart, tempVal, hour, minute);
  PCD8544_AddChartPoint(&WS_HumidityChart, humVal, hour, minute);
  PCD8544_AddChartPoint(&WS_PressureChart, pressVal, hour, minute);
  PCD8544_AddChartPoint(&WS_LuxChart, luxVal, hour, minute);

  WS_UI.chart_data_dirty = 1U;
}

/**
 * @brief Display live measurement data (menu function callback)
 */
void WS_UI_MeasurementDisplay(void) {
  static uint32_t lastUpdate = 0;
  uint32_t now = HAL_GetTick();

  if ((now - lastUpdate) < 700) return;
  lastUpdate = now;

  if (WS_UI.lcd == NULL || WS_UI.ws_ctx == NULL) return;

  WS_MeasurementData_t measurement;
  uint8_t hasMeasurement = WS_GetLatestMeasurement(WS_UI.ws_ctx, &measurement) ? 1U : 0U;
  char value_text[16];

  PCD8544_ClearScreen(WS_UI.lcd);
  PCD8544_SetFont(WS_UI.lcd, &Font_6x8);

  /* Time display */
  PCD8544_SetCursor(WS_UI.lcd, 0, 0);
  if (WS_UI.rtc_now != NULL) {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "%02u:%02u:%02u",
             WS_UI.rtc_now->hours, WS_UI.rtc_now->minutes, WS_UI.rtc_now->seconds);
  } else {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "--:--:--");
  }
  PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);

  /* Temperature */
  PCD8544_SetCursor(WS_UI.lcd, 0, 1);
  if (hasMeasurement) {
    ws_format_fixed(value_text, sizeof(value_text), measurement.si7021_temp, 2U);
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "T:%sC", value_text);
  } else {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "T:--.--C");
  }
  PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);

  /* Humidity */
  PCD8544_SetCursor(WS_UI.lcd, 0, 2);
  if (hasMeasurement) {
    ws_format_fixed(value_text, sizeof(value_text), measurement.si7021_hum, 2U);
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "H:%s%%", value_text);
  } else {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "H:--.--%%");
  }
  PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);

  /* Pressure */
  PCD8544_SetCursor(WS_UI.lcd, 0, 3);
  if (hasMeasurement) {
    ws_format_fixed(value_text, sizeof(value_text), measurement.bmp280_press, 2U);
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "P:%shPa", value_text);
  } else {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "P:--.--hPa");
  }
  PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);

  /* Light intensity */
  PCD8544_SetCursor(WS_UI.lcd, 0, 4);
  if (hasMeasurement) {
    ws_format_fixed(value_text, sizeof(value_text), measurement.tsl2561_lux, 2U);
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "L:%slux", value_text);
  } else {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "L:--.--lux");
  }
  PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);
  PCD8544_UpdateScreen(WS_UI.lcd);
}

/**
 * @brief Enter temperature chart view (menu callback)
 */
void WS_UI_ChartTemperature(void) {
  if (WS_UI.menu_ctx == NULL || WS_UI.lcd == NULL) return;

  WS_UI.menu_ctx->state.InChartView = 1;
  WS_UI.menu_ctx->state.ChartViewType = CHART_VIEW_TEMPERATURE;

  PCD8544_ClearBuffer(WS_UI.lcd);
  PCD8544_DrawChart(WS_UI.lcd, &WS_TemperatureChart);
  PCD8544_UpdateScreen(WS_UI.lcd);
}

/**
 * @brief Enter humidity chart view (menu callback)
 */
void WS_UI_ChartHumidity(void) {
  if (WS_UI.menu_ctx == NULL || WS_UI.lcd == NULL) return;

  WS_UI.menu_ctx->state.InChartView = 1;
  WS_UI.menu_ctx->state.ChartViewType = CHART_VIEW_HUMIDITY;

  PCD8544_ClearBuffer(WS_UI.lcd);
  PCD8544_DrawChart(WS_UI.lcd, &WS_HumidityChart);
  PCD8544_UpdateScreen(WS_UI.lcd);
}

/**
 * @brief Enter pressure chart view (menu callback)
 */
void WS_UI_ChartPressure(void) {
  if (WS_UI.menu_ctx == NULL || WS_UI.lcd == NULL) return;

  WS_UI.menu_ctx->state.InChartView = 1;
  WS_UI.menu_ctx->state.ChartViewType = CHART_VIEW_PRESSURE;

  PCD8544_ClearBuffer(WS_UI.lcd);
  PCD8544_DrawChart(WS_UI.lcd, &WS_PressureChart);
  PCD8544_UpdateScreen(WS_UI.lcd);
}

/**
 * @brief Enter light intensity chart view (menu callback)
 */
void WS_UI_ChartLux(void) {
  if (WS_UI.menu_ctx == NULL || WS_UI.lcd == NULL) return;

  WS_UI.menu_ctx->state.InChartView = 1;
  WS_UI.menu_ctx->state.ChartViewType = CHART_VIEW_LUX;

  PCD8544_ClearBuffer(WS_UI.lcd);
  PCD8544_DrawChart(WS_UI.lcd, &WS_LuxChart);
  PCD8544_UpdateScreen(WS_UI.lcd);
}

/**
 * @brief Chart view main task - call in main loop when InChartView == 1
 * @details Redraws chart only when new measurement data has been received,
 *          avoiding unnecessary redraws since chart data changes only on new measurements.
 */
void WS_UI_ChartViewTask(void) {
  if (WS_UI.menu_ctx == NULL || WS_UI.lcd == NULL) return;

  /* Encoder button press exits chart view */
  /* Note: Button handling is done externally via Menu_SetEnterAction */

  if (WS_UI.chart_data_dirty == 0U) return;
  WS_UI.chart_data_dirty = 0U;

  PCD8544_ClearBuffer(WS_UI.lcd);

  switch (WS_UI.menu_ctx->state.ChartViewType) {
    case CHART_VIEW_TEMPERATURE:
      PCD8544_DrawChart(WS_UI.lcd, &WS_TemperatureChart);
      break;
    case CHART_VIEW_HUMIDITY:
      PCD8544_DrawChart(WS_UI.lcd, &WS_HumidityChart);
      break;
    case CHART_VIEW_PRESSURE:
      PCD8544_DrawChart(WS_UI.lcd, &WS_PressureChart);
      break;
    case CHART_VIEW_LUX:
      PCD8544_DrawChart(WS_UI.lcd, &WS_LuxChart);
      break;

    default:
      break;
  }

  PCD8544_UpdateScreen(WS_UI.lcd);
}

/**
 * @brief Request a new measurement (menu callback)
 */
void WS_UI_TakeMeasurement(void) {
  if (WS_UI.ws_ctx == NULL) return;
  WS_RequestMeasurementForActiveNode(WS_UI.ws_ctx);
}

/**
 * @brief Convert node state enum to short status string
 */
static const char* ws_node_state_str(WS_NodeStateEnum_t state) {
  switch (state) {
    case WS_NODE_IDLE:           return "OK";
    case WS_NODE_TX_IN_PROGRESS: return "TX..";
    case WS_NODE_WAIT_RESPONSE:  return "WAIT";
    case WS_NODE_DATA_READY:     return "DATA";
    case WS_NODE_ERROR:          return "ERR";
    default:                     return "?";
  }
}

/**
 * @brief Display list of measurement stations with their status
 * @note Helper function that renders station status. Used by both menu callback and task.
 */
static void ws_render_stations_status(void) {
  if (WS_UI.lcd == NULL || WS_UI.ws_ctx == NULL || WS_UI.text_buffer == NULL) return;

  PCD8544_ClearBuffer(WS_UI.lcd);
  PCD8544_SetFont(WS_UI.lcd, &Font_6x8);

  /* Header */
  PCD_8544_DrawCenteredTitle(WS_UI.lcd, "STATUS");

  /* Display each node below Return */
  uint8_t row = 2;
  for (uint8_t i = 0; i < WS_UI.ws_ctx->node_count && row < 6; i++) {
    const WS_NodeState_t *node = &WS_UI.ws_ctx->nodes[i];
    
    /* Determine status indicator */
    const char *status_str = ws_node_state_str(node->state);
    char active_marker = (i == WS_UI.ws_ctx->active_node) ? '*' : ' ';
    
    /* Check if node has valid data */
    uint8_t has_data = (node->data.si7021_temp != 0.0f || 
                        node->data.si7021_hum != 0.0f ||
                        node->data.bmp280_press != 0.0f) ? 1U : 0U;

    /* Build sensor status indicator */
    const char *sens_flag = "";
    if (has_data && node->data.sensorStatus != WS_SENSOR_OK) {
      sens_flag = "!";
    }

    PCD8544_SetCursor(WS_UI.lcd, 1, row);
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, 
             "%cS%u:%s %s%s", 
             active_marker, i + 1, status_str, 
             has_data ? "[+]" : "[-]",
             sens_flag);
    PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);
    row++;
  }

  /* If no nodes configured */
  if (WS_UI.ws_ctx->node_count == 0) {
    PCD8544_SetCursor(WS_UI.lcd, 1, 1);
    PCD8544_WriteString(WS_UI.lcd, "Brak stacji");
  }

  PCD8544_UpdateScreen(WS_UI.lcd);
}

/**
 * @brief Enter stations status view (menu callback)
 * @details Called once when menu item is selected. Sets flag and performs initial draw.
 */
void WS_UI_StationsStatus(void) {
  if (WS_UI.menu_ctx == NULL || WS_UI.lcd == NULL) return;

  WS_UI.menu_ctx->state.InStationsStatusView = 1U;
  ws_render_stations_status();
}

/**
 * @brief Stations status view main task - call in main loop when InStationsStatusView == 1
 * @details Redraws station status only when new measurement data has been received,
 *          since node states change only during measurement cycles.
 */
void WS_UI_StationsStatusTask(void) {
  if (WS_UI.menu_ctx == NULL || WS_UI.lcd == NULL) return;

  if (WS_UI.chart_data_dirty == 0U) return;

  ws_render_stations_status();
}

/* ============================================================================
 * PUBLIC API - View State Machine
 * ========================================================================== */

/**
 * @brief Exit a dedicated view (chart or stations status) back to menu
 * @details Clears view flags, resets pending actions, and refreshes menu display.
 */
static void ws_exit_dedicated_view(void) {
  WS_UI.encoder->ButtonIRQ_Flag = 0;
  WS_UI.menu_ctx->state.actionPending = 0;
  WS_UI.menu_ctx->state.currentAction = MENU_ACTION_IDLE;
  Menu_RefreshDisplay(WS_UI.lcd, WS_UI.menu_ctx);
  WS_UI.view_state = WS_VIEW_MENU;
}

/**
 * @brief Main view state machine task - call in main loop
 */
void WS_UI_ViewTask(void) {
  if (WS_UI.menu_ctx == NULL || WS_UI.lcd == NULL || WS_UI.encoder == NULL) return;

  switch (WS_UI.view_state) {

    case WS_VIEW_MENU:
      Menu_Task(WS_UI.lcd, WS_UI.menu_ctx);
      Encoder_Task(WS_UI.encoder, WS_UI.menu_ctx);

      if (WS_UI.menu_ctx->state.InChartView) {
        WS_UI.view_state = WS_VIEW_CHART;
      } else if (WS_UI.menu_ctx->state.InStationsStatusView) {
        WS_UI.view_state = WS_VIEW_STATIONS_STATUS;
      } else if (WS_UI.menu_ctx->state.InDefaultMeasurementsView) {
        WS_UI.view_state = WS_VIEW_DEFAULT_MEASUREMENT;
      }
      break;

    case WS_VIEW_CHART:
      WS_UI_ChartViewTask();

      if (WS_UI.encoder->ButtonIRQ_Flag) {
        WS_UI.menu_ctx->state.InChartView = 0;
        WS_UI.menu_ctx->state.ChartViewType = CHART_VIEW_NONE;
        ws_exit_dedicated_view();
      }
      break;

    case WS_VIEW_STATIONS_STATUS:
      WS_UI_StationsStatusTask();

      if (WS_UI.encoder->ButtonIRQ_Flag) {
        WS_UI.menu_ctx->state.InStationsStatusView = 0;
        ws_exit_dedicated_view();
      }
      break;

    case WS_VIEW_DEFAULT_MEASUREMENT:
      Menu_Task(WS_UI.lcd, WS_UI.menu_ctx);
      Encoder_Task(WS_UI.encoder, WS_UI.menu_ctx);

      if (WS_UI.menu_ctx->state.InChartView) {
        WS_UI.view_state = WS_VIEW_CHART;
      } else if (WS_UI.menu_ctx->state.InStationsStatusView) {
        WS_UI.view_state = WS_VIEW_STATIONS_STATUS;
      } else if (!WS_UI.menu_ctx->state.InDefaultMeasurementsView) {
        WS_UI.view_state = WS_VIEW_MENU;
      } else {
        WS_UI_MeasurementDisplay();
      }
      break;

    default:
      WS_UI.view_state = WS_VIEW_MENU;
      break;
  }
}
