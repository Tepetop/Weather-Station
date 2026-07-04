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

#define UART_CMD_LINE_MAX 40U

static UART_HandleTypeDef *uart_cmd_huart;
static WS_Manager_t *uart_cmd_ws;
static char uart_cmd_line[UART_CMD_LINE_MAX];
static uint8_t uart_cmd_line_len;
static uint8_t uart_cmd_rx_byte;

static void uart_cmd_reply(const char *msg) {
  if ((uart_cmd_huart == NULL) || (msg == NULL)) {
    return;
  }
  (void)HAL_UART_Transmit(uart_cmd_huart, (uint8_t *)msg, (uint16_t)strlen(msg), 100U);
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
    uart_cmd_reply("ERR:BUSY\n");
    return;
  }

  if ((ws->comm_watchdog_tripped != 0U) ||
      ((ws->app_state != WS_APP_IDLE) && (ws->app_state != WS_APP_DATA_READY))) {
    uart_cmd_reply("ERR:BUSY\n");
    return;
  }

  if (target != UART_CMD_TARGET_ACTIVE) {
    if (target >= ws->node_count) {
      uart_cmd_reply("ERR:UNKNOWN\n");
      return;
    }
    ws->active_node = target;
  }

  WS_NodeState_t *node = WS_GetActiveNode(ws);
  if ((node == NULL) || (node->state != WS_NODE_IDLE) || (node->measurement_pending != 0U)) {
    uart_cmd_reply("ERR:BUSY\n");
    return;
  }

  WS_RequestMeasurementForActiveNode(ws);
  uart_cmd_reply("ACK:MEASURE:QUEUED\n");
}

static void uart_cmd_handle_line(const char *line) {
  if (strcmp(line, "CMD:PING") == 0) {
    uart_cmd_reply("ACK:PING\n");
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
      uart_cmd_reply("ERR:UNKNOWN\n");
      return;
    }
    while (*p != '\0') {
      if ((*p < '0') || (*p > '9') || (node > WS_MAX_NODES)) {
        uart_cmd_reply("ERR:UNKNOWN\n");
        return;
      }
      node = (node * 10U) + (unsigned int)(*p - '0');
      p++;
    }
    if (node >= WS_MAX_NODES) {
      uart_cmd_reply("ERR:UNKNOWN\n");
      return;
    }
    uart_cmd_request_measure((uint8_t)node);
    return;
  }

  uart_cmd_reply("ERR:UNKNOWN\n");
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
