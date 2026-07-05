/**
 * @file uart_cmd.c
 * @brief Line-based UART commands: CMD:MEASURE, CMD:MEASURE:N, CMD:PING
 *
 * Fully interrupt-driven: bytes are received via USART2 RX interrupt and a
 * completed line is parsed and executed directly in the ISR. Queuing a
 * measurement only sets the measurement_pending flag consumed by the radio
 * state machine (WS_ProcessEventHandler), so no UART polling is needed.
 */

#include "uart_cmd.h"

#include <string.h>

#define UART_CMD_LINE_MAX  40U
#define UART_CMD_REPLY_MAX 32U

static UART_HandleTypeDef *uart_cmd_huart;
static WS_Manager_t *uart_cmd_ws;
static char uart_cmd_line[UART_CMD_LINE_MAX];
static uint8_t uart_cmd_line_len;
static uint8_t uart_cmd_rx_byte;

/* Pending reply written in ISR, sent from main loop by UartCmd_FlushReply(). */
static volatile char    uart_cmd_pending_reply[UART_CMD_REPLY_MAX];
static volatile uint8_t uart_cmd_reply_pending;

/* Called from ISR: store response in buffer, do NOT call HAL_UART_Transmit. */
static void uart_cmd_reply(const char *msg) {
  if (msg == NULL) {
    return;
  }
  uint8_t i = 0U;
  while ((msg[i] != '\0') && (msg[i] != '\r') && (msg[i] != '\n') &&
         (i < (UART_CMD_REPLY_MAX - 3U))) {
    uart_cmd_pending_reply[i] = msg[i];
    i++;
  }
  uart_cmd_pending_reply[i++] = '\r';
  uart_cmd_pending_reply[i++] = '\n';
  uart_cmd_pending_reply[i] = '\0';
  uart_cmd_reply_pending = 1U;
}

static void uart_cmd_reset_line(void) {
  uart_cmd_line_len = 0U;
  uart_cmd_line[0] = '\0';
}

/**
 * @brief Queue a measurement for the active (or given) node.
 * @param target Node index or UART_CMD_TARGET_ACTIVE
 *
 * Safe to call from ISR context: WS_RequestMeasurementForActiveNode only
 * sets measurement_pending and resets app_state to IDLE. WS_APP_DATA_READY
 * is a terminal state after a finished measurement, so it also counts as
 * ready for a new request.
 */
static void uart_cmd_request_measure(uint8_t target) {
  WS_Manager_t *ws = uart_cmd_ws;

  if (ws == NULL) {
    uart_cmd_reply("ERR:BUSY");
    return;
  }

  if ((ws->comm_watchdog_tripped != 0U) ||
      ((ws->app_state != WS_APP_IDLE) && (ws->app_state != WS_APP_DATA_READY))) {
    uart_cmd_reply("ERR:BUSY");
    return;
  }

  if (target != UART_CMD_TARGET_ACTIVE) {
    if (target >= ws->node_count) {
      uart_cmd_reply("ERR:UNKNOWN");
      return;
    }
    ws->active_node = target;
  }

  WS_NodeState_t *node = WS_GetActiveNode(ws);
  if ((node == NULL) || (node->state != WS_NODE_IDLE) || (node->measurement_pending != 0U)) {
    uart_cmd_reply("ERR:BUSY");
    return;
  }

  WS_RequestMeasurementForActiveNode(ws);
  uart_cmd_reply("ACK:MEASURE:QUEUED");
}

static void uart_cmd_handle_line(const char *line) {
  if (strcmp(line, "CMD:PING") == 0) {
    uart_cmd_reply("ACK:PING");
    return;
  }

  if (strcmp(line, "CMD:MEASURE") == 0) {
    uart_cmd_request_measure(UART_CMD_TARGET_ACTIVE);
    return;
  }

  if (strncmp(line, "CMD:MEASURE:", 12) == 0) {
    const char *p = line + 12;
    unsigned int node = 0U;

    if (*p == '\0') {
      uart_cmd_reply("ERR:UNKNOWN");
      return;
    }
    while (*p != '\0') {
      if ((*p < '0') || (*p > '9') || (node > WS_MAX_NODES)) {
        uart_cmd_reply("ERR:UNKNOWN");
        return;
      }
      node = (node * 10U) + (unsigned int)(*p - '0');
      p++;
    }
    if (node >= WS_MAX_NODES) {
      uart_cmd_reply("ERR:UNKNOWN");
      return;
    }
    uart_cmd_request_measure((uint8_t)node);
    return;
  }

  uart_cmd_reply("ERR:UNKNOWN");
}

static void uart_cmd_on_byte(uint8_t byte) {
  if ((byte == '\r') || (byte == '\n')) {
    if (uart_cmd_line_len > 0U) {
      uart_cmd_line[uart_cmd_line_len] = '\0';
      uart_cmd_handle_line(uart_cmd_line);
    }
    uart_cmd_reset_line();
    return;
  }

  if (uart_cmd_line_len >= (UART_CMD_LINE_MAX - 1U)) {
    uart_cmd_reset_line();
    return;
  }

  uart_cmd_line[uart_cmd_line_len++] = (char)byte;
}

void UartCmd_Init(UART_HandleTypeDef *huart, WS_Manager_t *ws) {
  uart_cmd_huart = huart;
  uart_cmd_ws = ws;
  uart_cmd_reset_line();

  if (huart != NULL) {
    (void)HAL_UART_Receive_IT(huart, &uart_cmd_rx_byte, 1U);
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if ((huart == NULL) || (huart != uart_cmd_huart)) {
    return;
  }

  uart_cmd_on_byte(uart_cmd_rx_byte);
  (void)HAL_UART_Receive_IT(huart, &uart_cmd_rx_byte, 1U);
}

/**
 * @brief Send pending UART reply from main-loop context.
 *
 * Call once per main-loop iteration. Transmits the ACK/ERR response that was
 * buffered inside the RX ISR, avoiding a blocking HAL_UART_Transmit call from
 * interrupt context on the shared huart2 line.
 */
void UartCmd_FlushReply(void) {
  if (uart_cmd_reply_pending == 0U) {
    return;
  }
  uart_cmd_reply_pending = 0U;
  if (uart_cmd_huart != NULL) {
    (void)HAL_UART_Transmit(uart_cmd_huart,
                            (uint8_t *)uart_cmd_pending_reply,
                            (uint16_t)strlen((const char *)uart_cmd_pending_reply),
                            100U);
  }
}

/* On any RX error (overrun/framing/noise) the HAL aborts interrupt reception
 * and does not re-arm it, which permanently stops receiving commands from the
 * Pico W. Clear the error flags, drop the partial line and re-arm RX. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  if ((huart == NULL) || (huart != uart_cmd_huart)) {
    return;
  }

  __HAL_UART_CLEAR_OREFLAG(huart);
  uart_cmd_reset_line();
  (void)HAL_UART_Receive_IT(huart, &uart_cmd_rx_byte, 1U);
}
