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
#include "ws_protocol.h"

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

/** @brief Delay used between nRF24 power-down and power-up during recovery */
#define WS_NRF_POWER_CYCLE_DELAY_MS 5U

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
 * @brief Performs a radio power-cycle before reconfiguration
 * @param[in] cfg Runtime configuration containing nRF24 handle
 * @details Used during error recovery when reinitialization alone is not enough.
 */
static void ws_power_cycle_radio(const WS_RuntimeConfig_t *cfg) {
  if ((cfg == NULL) || (cfg->nrf == NULL)) {
    return;
  }

  (void)NRF24_PowerDown(cfg->nrf);
  HAL_Delay(WS_NRF_POWER_CYCLE_DELAY_MS);
  (void)NRF24_PowerUp(cfg->nrf);
}

/**
 * @brief Captures RTC date/time of the latest successful measurement
 * @param[in,out] ctx Weather station manager context
 * @param[in] cfg Runtime configuration containing RTC snapshot pointer
 */
static void ws_capture_last_successful_time(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg) {
  if (ctx == NULL) {
    return;
  }

  if ((cfg == NULL) || (cfg->rtc_now == NULL)) {
    ctx->last_successful_rx_time_valid = 0U;
    return;
  }

  ctx->last_successful_rx_time = *cfg->rtc_now;
  ctx->last_successful_rx_time_valid = 1U;
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
 * @brief Appends one sensor name to an ERR status string
 */
static void ws_append_sensor_status(char *dst, size_t dst_size, const char *sensor_name) {
  size_t used = strlen(dst);
  if (used >= dst_size) {
    return;
  }

  snprintf(dst + used, dst_size - used, "%c%s", (used == 3U) ? ':' : ',', sensor_name);
}

/**
 * @brief Formats sensor status bitmask to Pico W status text
 * @param[out] dst Destination buffer
 * @param[in] dst_size Destination buffer size
 * @param[in] sensor_status Sensor status bitmask (WS_SensorError_t)
 */
static void ws_format_sensor_status(char *dst, size_t dst_size, uint8_t sensor_status) {
  uint8_t known_status = sensor_status &
      ((uint8_t)WS_SENSOR_ERR_SI7021 | (uint8_t)WS_SENSOR_ERR_BMP280 |
       (uint8_t)WS_SENSOR_ERR_TSL2561 | (uint8_t)WS_SENSOR_ERR_BME280);

  if ((dst == NULL) || (dst_size == 0U)) {
    return;
  }

  if (known_status == (uint8_t)WS_SENSOR_OK) {
    snprintf(dst, dst_size, "OK");
    return;
  }

  snprintf(dst, dst_size, "ERR");
  if ((known_status & (uint8_t)WS_SENSOR_ERR_SI7021) != 0U) {
    ws_append_sensor_status(dst, dst_size, "SI7021");
  }
  if ((known_status & (uint8_t)WS_SENSOR_ERR_BMP280) != 0U) {
    ws_append_sensor_status(dst, dst_size, "BMP280");
  }
  if ((known_status & (uint8_t)WS_SENSOR_ERR_TSL2561) != 0U) {
    ws_append_sensor_status(dst, dst_size, "TSL2561");
  }
  if ((known_status & (uint8_t)WS_SENSOR_ERR_BME280) != 0U) {
    ws_append_sensor_status(dst, dst_size, "BME280");
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
  char status_text[40];
  char line[160];
  char channel_part[24];
  uint8_t year = 0U;
  uint8_t month = 0U;
  uint8_t date = 0U;
  uint8_t hours = 0U;
  uint8_t minutes = 0U;
  uint8_t seconds = 0U;
  int line_len = 0;

  ws_format_sensor_status(status_text, sizeof(status_text), node->data.sensor_status);

  if (cfg->rtc_now != NULL) {
    year = cfg->rtc_now->year;
    month = cfg->rtc_now->month;
    date = cfg->rtc_now->date;
    hours = cfg->rtc_now->hours;
    minutes = cfg->rtc_now->minutes;
    seconds = cfg->rtc_now->seconds;
  }

  line_len = snprintf(
      line,
      sizeof(line),
      "DATA:20%02u-%02u-%02uT%02u:%02u:%02u,S%u",
      (unsigned int)year,
      (unsigned int)month,
      (unsigned int)date,
      (unsigned int)hours,
      (unsigned int)minutes,
      (unsigned int)seconds,
      (unsigned int)node_idx);

  for (uint8_t i = 0U; (i < node->data.count) && (line_len > 0); i++) {
    char value_text[16];
    ws_format_fixed(value_text, sizeof(value_text), node->data.readings[i].value, 2U);
    snprintf(channel_part, sizeof(channel_part), ",%02X:%s",
             (unsigned int)node->data.readings[i].channel_id, value_text);
    if (((size_t)line_len + strlen(channel_part) + strlen(status_text) + 2U) >= sizeof(line)) {
      break;
    }
    line_len += snprintf(line + line_len, sizeof(line) - (size_t)line_len, "%s", channel_part);
  }

  if (line_len > 0) {
    line_len += snprintf(line + line_len, sizeof(line) - (size_t)line_len, ",%s\r\n", status_text);
  }

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
 * @brief Sends a measurement command (broadcast NoAck, optionally single-node masked)
 */
static void ws_send_measure_command(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg, uint32_t now_tick) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  uint8_t cmd[WS_CMD_SIZE] = {0};
  uint8_t target_mask;

  if ((node == NULL) || (cfg == NULL) || (cfg->nrf == NULL) || (cfg->broadcast_addr == NULL)) {
    return;
  }

  if ((node->state == WS_NODE_TX_IN_PROGRESS) || (node->state == WS_NODE_WAIT_RESPONSE)) {
    return;
  }

  ctx->cycle_id++;
  target_mask = (uint8_t)(1U << ctx->active_node);
  if (!WS_Cmd_EncodeMeasureTo(ctx->cycle_id, target_mask, cmd, cfg->cmd_size)) {
    return;
  }

  /* Single-node requests still use the shared command address (NoAck). */
  ctx->expected_mask = target_mask;
  ctx->received_mask = 0U;
  ctx->parallel_cycle = 0U;
  ctx->cycle_tx_done = 0U;

  ws_set_led(cfg, GPIO_PIN_SET);
  WS_ClearIrqFlag(ctx);
  WS_StartTxForActiveNode(ctx, now_tick);

  NRF24_SetMode(cfg->nrf, NRF24_MODE_STANDBY);
  NRF24_SetTXAddress(cfg->nrf, cfg->broadcast_addr, 5U);
  NRF24_FlushTX(cfg->nrf);
  NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_IRQ_MASK);
  (void)NRF24_EnableDynAck(cfg->nrf, 1U);
  NRF24_WritePayloadNoAck(cfg->nrf, cmd, cfg->cmd_size);
  NRF24_SetMode(cfg->nrf, NRF24_MODE_TX);

  Debug_LogNrfTxStart(ctx->active_node);
}

/**
 * @brief Starts a parallel broadcast measurement cycle for all nodes
 */
static void ws_start_parallel_cycle(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg, uint32_t now_tick) {
  uint8_t cmd[WS_CMD_SIZE] = {0};

  if ((ctx == NULL) || (cfg == NULL) || (cfg->nrf == NULL) || (cfg->broadcast_addr == NULL)) {
    return;
  }

  ctx->cycle_pending = 0U;
  ctx->cycle_id++;
  ctx->expected_mask = WS_Cycle_ExpectedMask(ctx->node_count);
  ctx->received_mask = 0U;
  ctx->parallel_cycle = 1U;
  ctx->cycle_tx_done = 0U;
  ctx->cycle_tx_start_tick = now_tick;
  ctx->cycle_rx_start_tick = 0U;
  ctx->cycle_nodes_remaining = 0U;

  if (!WS_Cmd_EncodeMeasureTo(ctx->cycle_id, ctx->expected_mask, cmd, cfg->cmd_size)) {
    ctx->parallel_cycle = 0U;
    return;
  }

  for (uint8_t i = 0U; i < ctx->node_count; i++) {
    ctx->nodes[i].measurement_pending = 0U;
    ctx->nodes[i].retry_count = 0U;
    ctx->nodes[i].tx_start_tick = now_tick;
    ctx->nodes[i].response_start_tick = 0U;
    ctx->nodes[i].state = WS_NODE_TX_IN_PROGRESS;
  }

  ws_set_led(cfg, GPIO_PIN_SET);
  WS_ClearIrqFlag(ctx);

  NRF24_SetMode(cfg->nrf, NRF24_MODE_STANDBY);
  NRF24_SetTXAddress(cfg->nrf, cfg->broadcast_addr, 5U);
  NRF24_FlushTX(cfg->nrf);
  NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_IRQ_MASK);
  (void)NRF24_EnableDynAck(cfg->nrf, 1U);
  NRF24_WritePayloadNoAck(cfg->nrf, cmd, cfg->cmd_size);
  NRF24_SetMode(cfg->nrf, NRF24_MODE_TX);

  ctx->app_state = WS_APP_WAIT_TX_IRQ;
  Debug_LogValue("NRF:CYCLE_START id=", ctx->cycle_id);
  Debug_LogHex("NRF:CYCLE_EXPECT=", ctx->expected_mask);
}

/**
 * @brief Finalize a parallel cycle: mark missing nodes as ERROR and go DATA_READY
 */
static void ws_finalize_parallel_cycle(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg, uint8_t timed_out) {
  if ((ctx == NULL) || (ctx->parallel_cycle == 0U)) {
    return;
  }

  for (uint8_t i = 0U; i < ctx->node_count; i++) {
    uint8_t bit = (uint8_t)(1U << i);
    if ((ctx->expected_mask & bit) == 0U) {
      continue;
    }
    if ((ctx->received_mask & bit) != 0U) {
      continue;
    }
    ctx->nodes[i].state = WS_NODE_ERROR;
    Debug_LogValue("NRF:CYCLE_MISS node=", i);
  }

  if (timed_out != 0U) {
    Debug_Log("LOG:NRF:CYCLE_TIMEOUT");
    Debug_LogHex("NRF:CYCLE_RECV=", ctx->received_mask);
  } else {
    Debug_Log("LOG:NRF:CYCLE_COMPLETE");
  }

  ws_set_led(cfg, GPIO_PIN_RESET);
  ctx->parallel_cycle = 0U;
  ctx->cycle_tx_done = 0U;
  ctx->app_state = WS_APP_DATA_READY;
}

/**
 * @brief Process DATA_READY nodes after IRQ (UART + charts); complete cycle if done
 */
static void ws_process_ready_nodes(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg) {
  uint8_t processed = 0U;

  if (ctx == NULL) {
    return;
  }

  for (uint8_t i = 0U; i < ctx->node_count; i++) {
    WS_NodeState_t *node = &ctx->nodes[i];
    if (node->state != WS_NODE_DATA_READY) {
      continue;
    }

    ws_send_measurement_uart(ctx, cfg, i);
    if (WS_UI.rtc_now != NULL) {
      WS_UI_AddMeasurementToCharts(&node->data, WS_UI.rtc_now->hours, WS_UI.rtc_now->minutes);
    }
    node->retry_count = 0U;
    node->state = WS_NODE_IDLE;
    processed = 1U;
  }

  if (processed == 0U) {
    return;
  }

  if ((ctx->parallel_cycle != 0U) &&
      WS_Cycle_IsComplete(ctx->expected_mask, ctx->received_mask)) {
    ws_finalize_parallel_cycle(ctx, cfg, 0U);
  } else if ((ctx->parallel_cycle == 0U) && (ctx->app_state == WS_APP_WAIT_RX_DATA)) {
    ctx->app_state = WS_APP_DATA_READY;
  }
}

/* ============================================================================
 * PRIVATE HELPER FUNCTIONS - Interrupt Handling
 * ========================================================================== */

/**
 * @brief Handles nRF24L01+ interrupt events
 * @param[in,out] ctx Weather station manager context
 * @param[in] cfg Runtime configuration
 * @details Processes three interrupt sources:
 *          - RX_DR: Data received (reads all pending payloads from pipes)
 *          - TX_DS: Transmission successful (or NoAck packet left the air)
 *          - MAX_RT: Maximum retransmissions reached (TX failed)
 */
static void ws_handle_irq(WS_Manager_t *ctx, const WS_RuntimeConfig_t *cfg) {
  if ((ctx == NULL) || (cfg == NULL) || (cfg->nrf == NULL)) {
    return;
  }

  uint8_t status = NRF24_GetStatus(cfg->nrf);
  WS_NodeState_t *active = WS_GetActiveNode(ctx);

  /* Drain RX FIFO — both outdoor replies may arrive before this handler runs. */
  while (status & NRF24_STATUS_RX_DR) {
    uint8_t pipe = (status >> WS_STATUS_PIPE_SHIFT) & WS_STATUS_PIPE_MASK;
    uint8_t rx_data[NRF24_MAX_PAYLOAD_SIZE] = {0};
    uint8_t payload_len = (pipe == 0U) ? cfg->cmd_size : cfg->payload_size;

    NRF24_ReadPayload(cfg->nrf, rx_data, payload_len);
    NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_RX_DR);

    /* Map pipe to node index: pipe 1 → node 0, pipe 2 → node 1, etc. */
    uint8_t node_idx = pipe - 1U;
    if ((pipe >= 1U) && (node_idx < ctx->node_count) &&
        (payload_len >= WS_PROTOCOL_HEADER_SIZE)) {
      WS_NodeReadings_t measurement;
      if (WS_Protocol_Decode(rx_data, payload_len, &measurement)) {
        WS_NodeState_t *rx_node = &ctx->nodes[node_idx];
        memcpy(&rx_node->data, &measurement, sizeof(measurement));
        rx_node->last_status = status;
        rx_node->state = WS_NODE_DATA_READY;
        rx_node->retry_count = 0U;

        if (ctx->parallel_cycle != 0U) {
          ctx->received_mask |= (uint8_t)(1U << node_idx);
        }

        ctx->latest_data_valid = 1U;
        ctx->latest_node_index = node_idx;
        ctx->last_successful_rx_tick = HAL_GetTick();
        ws_capture_last_successful_time(ctx, cfg);
        ctx->comm_watchdog_tripped = 0U;

        ws_set_led(cfg, GPIO_PIN_RESET);
        Debug_LogNrfRxData(node_idx);
        Debug_LogValue("NRF:RX_PIPE=", pipe);
      }
    }

    status = NRF24_GetStatus(cfg->nrf);
    if ((NRF24_GetFIFOStatus(cfg->nrf) & NRF24_FIFO_RX_EMPTY) != 0U) {
      break;
    }
  }

  if (status & NRF24_STATUS_TX_DS) {
    NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_TX_DS);
    if (ctx->parallel_cycle != 0U) {
      ctx->cycle_tx_done = 1U;
      for (uint8_t i = 0U; i < ctx->node_count; i++) {
        if ((ctx->expected_mask & (uint8_t)(1U << i)) != 0U) {
          ctx->nodes[i].state = WS_NODE_WAIT_RESPONSE;
          ctx->nodes[i].response_start_tick = HAL_GetTick();
        }
      }
      Debug_LogNrfTxResult(1U);
    } else if (active != NULL) {
      active->last_status = status;
      WS_MarkTxResultFromIrq(ctx, true, status);
      Debug_LogNrfTxResult(1U);
    }
  }

  if (status & NRF24_STATUS_MAX_RT) {
    NRF24_ClearIRQ(cfg->nrf, NRF24_STATUS_MAX_RT);
    NRF24_FlushTX(cfg->nrf);
    if (ctx->parallel_cycle != 0U) {
      /* Broadcast uses NoAck; MAX_RT here is unexpected — fail the cycle TX. */
      ctx->cycle_tx_done = 0U;
      for (uint8_t i = 0U; i < ctx->node_count; i++) {
        ctx->nodes[i].state = WS_NODE_ERROR;
      }
      Debug_LogNrfTxResult(0U);
    } else if (active != NULL) {
      active->last_status = status;
      WS_MarkTxResultFromIrq(ctx, false, status);
      Debug_LogNrfTxResult(0U);
    }
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
  ctx->last_successful_rx_tick = 0U;
  ctx->last_successful_rx_time_valid = 0U;
  ctx->comm_watchdog_tripped = 0U;
  ctx->next_measure_earliest_tick = 0U;
  ctx->cycle_nodes_remaining = 0U;
  ctx->cycle_id = 0U;
  ctx->expected_mask = 0U;
  ctx->received_mask = 0U;
  ctx->cycle_pending = 0U;
  ctx->parallel_cycle = 0U;
  ctx->cycle_tx_done = 0U;
  ctx->cycle_tx_start_tick = 0U;
  ctx->cycle_rx_start_tick = 0U;
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
  if (ctx == NULL) {
    return false;
  }

  if (ctx->parallel_cycle != 0U) {
    return (ctx->app_state == WS_APP_WAIT_TX_IRQ) || (ctx->app_state == WS_APP_WAIT_RX_DATA);
  }

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
    if (node->state == WS_NODE_ERROR) {
      node->state = WS_NODE_IDLE;
    }
    ctx->app_state = WS_APP_IDLE;
  }
}

void WS_RequestMeasurementCycle(WS_Manager_t *ctx) {
  if (ctx == NULL) {
    return;
  }

  if (ctx->node_count <= 1U) {
    WS_RequestMeasurementForActiveNode(ctx);
    return;
  }

  ctx->cycle_pending = 1U;
  ctx->cycle_nodes_remaining = 0U;
  if ((ctx->app_state == WS_APP_IDLE) || (ctx->app_state == WS_APP_DATA_READY)) {
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
  if ((ctx == NULL) || (ctx->app_state != WS_APP_WAIT_TX_IRQ)) {
    return WS_TX_EVENT_NONE;
  }

  if (ctx->parallel_cycle != 0U) {
    if (ctx->cycle_tx_done != 0U) {
      if (ctx->cycle_rx_start_tick == 0U) {
        ctx->cycle_rx_start_tick = now_tick;
      }
      return WS_TX_EVENT_OK;
    }

    /* Parallel TX failed if all expected nodes were marked ERROR after MAX_RT. */
    if ((ctx->node_count > 0U) && (ctx->nodes[0].state == WS_NODE_ERROR)) {
      return WS_TX_EVENT_FAIL;
    }
    return WS_TX_EVENT_NONE;
  }

  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
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
void WS_MarkActiveDataReceived(WS_Manager_t *ctx, const WS_NodeReadings_t *data, uint8_t status) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  if (data != NULL) {
    memcpy(&node->data, data, sizeof(node->data));
    ctx->latest_data_valid = 1U;
    ctx->latest_node_index = ctx->active_node;
    ctx->last_successful_rx_tick = HAL_GetTick();
    ctx->last_successful_rx_time_valid = 0U;
    ctx->comm_watchdog_tripped = 0U;
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
bool WS_GetLatestMeasurement(const WS_Manager_t *ctx, WS_NodeReadings_t *out_data) {
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
  (void)NRF24_EnableDynAck(cfg->nrf, 1U);

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

  if (ctx->comm_watchdog_tripped != 0U) {
    return;
  }

  if (ctx->last_successful_rx_tick == 0U) {
    ctx->last_successful_rx_tick = now_tick;
  }

  if ((cfg->comm_watchdog_timeout_ms != 0U) &&
      ((now_tick - ctx->last_successful_rx_tick) > cfg->comm_watchdog_timeout_ms)) {
    ctx->comm_watchdog_tripped = 1U;
    Debug_Log("LOG:NRF:COMM_WATCHDOG_TRIPPED");
    return;
  }

  if (ctx->app_state == WS_APP_ERROR_RECOVERY) {
    ws_power_cycle_radio(cfg);
    ctx->parallel_cycle = 0U;
    ctx->cycle_tx_done = 0U;
    if (WS_InitRadioAndStart(ctx, cfg) == HAL_OK) {
      Debug_Log("LOG:NRF:RECOVERY_OK");
    } else {
      Debug_Log("LOG:NRF:RECOVERY_FAIL");
    }
    if (ctx->cycle_pending != 0U) {
      ctx->app_state = WS_APP_IDLE;
      return;
    }
    WS_NodeState_t *recovery_node = WS_GetActiveNode(ctx);
    if (recovery_node != NULL) {
      recovery_node->state = WS_NODE_IDLE;
      recovery_node->measurement_pending = 1U;
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

    if (ctx->parallel_cycle != 0U) {
      if ((ctx->app_state == WS_APP_WAIT_TX_IRQ) &&
          ((now_tick - ctx->cycle_tx_start_tick) > ((cfg->tx_irq_timeout_ms * 3U) / 4U))) {
        should_poll = 1U;
      }
      if ((ctx->app_state == WS_APP_WAIT_RX_DATA) &&
          (ctx->cycle_rx_start_tick != 0U) &&
          ((now_tick - ctx->cycle_rx_start_tick) > ((cfg->rx_timeout_ms * 3U) / 4U))) {
        should_poll = 1U;
      }
    } else {
      if ((node->state == WS_NODE_TX_IN_PROGRESS) &&
          ((now_tick - node->tx_start_tick) > ((cfg->tx_irq_timeout_ms * 3U) / 4U))) {
        should_poll = 1U;
      }
      if ((node->state == WS_NODE_WAIT_RESPONSE) &&
          ((now_tick - node->response_start_tick) > ((cfg->rx_timeout_ms * 3U) / 4U))) {
        should_poll = 1U;
      }
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

  /* Deliver any DATA_READY payloads (parallel or unicast) before further state work. */
  ws_process_ready_nodes(ctx, cfg);
  if (ctx->app_state == WS_APP_DATA_READY) {
    return;
  }

  WS_TxEvent_t tx_event = WS_ConsumeTxEvent(ctx, now_tick);

  if (tx_event == WS_TX_EVENT_OK) {
    ws_start_receive(ctx, cfg);
    if (ctx->parallel_cycle != 0U) {
      ctx->cycle_rx_start_tick = now_tick;
    }
    ctx->app_state = WS_APP_WAIT_RX_DATA;
    return;

  } else if (tx_event == WS_TX_EVENT_FAIL) {
    ws_start_receive(ctx, cfg);
    if (ctx->parallel_cycle != 0U) {
      ctx->parallel_cycle = 0U;
      ctx->cycle_tx_done = 0U;
      ctx->app_state = WS_APP_ERROR_RECOVERY;
      ctx->cycle_pending = 1U;
    } else {
      (void)ws_schedule_retry_or_recover(ctx);
    }
    return;
  }

  /* ---- Parallel cycle TX/RX timeouts ---- */
  if (ctx->parallel_cycle != 0U) {
    if ((ctx->app_state == WS_APP_WAIT_TX_IRQ) &&
        ((now_tick - ctx->cycle_tx_start_tick) > cfg->tx_irq_timeout_ms)) {
      uint8_t st = NRF24_GetStatus(cfg->nrf);
      if (st & (NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT)) {
        WS_SetIrqFlag(ctx);
        ws_handle_irq(ctx, cfg);
        tx_event = WS_ConsumeTxEvent(ctx, now_tick);
        if (tx_event == WS_TX_EVENT_OK) {
          ws_start_receive(ctx, cfg);
          ctx->cycle_rx_start_tick = now_tick;
          ctx->app_state = WS_APP_WAIT_RX_DATA;
          return;
        }
      }
      Debug_LogNrfTimeout(1U);
      ctx->parallel_cycle = 0U;
      ctx->app_state = WS_APP_ERROR_RECOVERY;
      ctx->cycle_pending = 1U;
      ws_start_receive(ctx, cfg);
      return;
    }

    if ((ctx->app_state == WS_APP_WAIT_RX_DATA) &&
        (ctx->cycle_rx_start_tick != 0U) &&
        ((now_tick - ctx->cycle_rx_start_tick) > cfg->rx_timeout_ms)) {
      Debug_LogNrfTimeout(0U);
      ws_finalize_parallel_cycle(ctx, cfg, 1U);
      return;
    }

    /* Start queued parallel cycle when idle. */
    if ((ctx->cycle_pending != 0U) && (ctx->app_state == WS_APP_IDLE)) {
      ws_start_parallel_cycle(ctx, cfg, now_tick);
    }
    return;
  }

  /* ---- Unicast (single-node) path ---- */
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
    Debug_LogNrfTimeout(1U);
    (void)ws_schedule_retry_or_recover(ctx);
    return;
  }

  if ((ctx->cycle_pending != 0U) && (ctx->app_state == WS_APP_IDLE)) {
    ws_start_parallel_cycle(ctx, cfg, now_tick);
    return;
  }

  if ((node->measurement_pending != 0U) &&
      (ctx->app_state == WS_APP_IDLE) &&
      (node->state == WS_NODE_IDLE) &&
      (now_tick >= ctx->next_measure_earliest_tick)) {
    WS_ConsumePendingForActiveNode(ctx);
    ws_send_measure_command(ctx, cfg, now_tick);
    ctx->app_state = WS_APP_WAIT_TX_IRQ;
  }

  if (WS_IsActiveRxTimedOut(ctx, now_tick, cfg->rx_timeout_ms)) {
    WS_HandleActiveRxTimeout(ctx, node->last_status);
    Debug_LogNrfTimeout(0U);
    ws_start_receive(ctx, cfg);
    (void)ws_schedule_retry_or_recover(ctx);
    return;
  }
}
