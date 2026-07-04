/**
 * @file uart_cmd.c
 * @brief Line-based UART commands: CMD:MEASURE, CMD:MEASURE:N, CMD:PING
 */

#include "uart_cmd.h"

#include <stdio.h>
#include <string.h>

#define UART_CMD_LINE_MAX 40U

static UART_HandleTypeDef *uart_cmd_huart;
static char uart_cmd_line[UART_CMD_LINE_MAX];
static char uart_cmd_pending_line[UART_CMD_LINE_MAX];
static uint8_t uart_cmd_line_len;
static uint8_t uart_cmd_rx_byte;
static volatile uint8_t uart_cmd_line_ready;
static volatile uint8_t uart_cmd_measure_pending;
static volatile uint8_t uart_cmd_target_node;

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

static void uart_cmd_handle_line(const char *line) {
  if (line == NULL) {
    return;
  }

  if (strcmp(line, "CMD:PING") == 0) {
    uart_cmd_reply("ACK:PING\n");
    return;
  }

  if (strcmp(line, "CMD:MEASURE") == 0) {
    if (uart_cmd_measure_pending != 0U) {
      uart_cmd_reply("ERR:BUSY\n");
      return;
    }
    uart_cmd_target_node = UART_CMD_TARGET_ACTIVE;
    uart_cmd_measure_pending = 1U;
    uart_cmd_reply("ACK:MEASURE:QUEUED\n");
    return;
  }

  if (strncmp(line, "CMD:MEASURE:", 12) == 0) {
    if (uart_cmd_measure_pending != 0U) {
      uart_cmd_reply("ERR:BUSY\n");
      return;
    }
    unsigned int node = 0U;
    if (sscanf(line + 12, "%u", &node) != 1) {
      uart_cmd_reply("ERR:UNKNOWN\n");
      return;
    }
    if (node >= WS_MAX_NODES) {
      uart_cmd_reply("ERR:UNKNOWN\n");
      return;
    }
    uart_cmd_target_node = (uint8_t)node;
    uart_cmd_measure_pending = 1U;
    uart_cmd_reply("ACK:MEASURE:QUEUED\n");
    return;
  }

  uart_cmd_reply("ERR:UNKNOWN\n");
}

static void uart_cmd_queue_line(void) {
  if (uart_cmd_line_len == 0U) {
    return;
  }

  if (uart_cmd_line_ready != 0U) {
    uart_cmd_reset_line();
    return;
  }

  uart_cmd_line[uart_cmd_line_len] = '\0';
  (void)strncpy(uart_cmd_pending_line, uart_cmd_line, UART_CMD_LINE_MAX - 1U);
  uart_cmd_pending_line[UART_CMD_LINE_MAX - 1U] = '\0';
  uart_cmd_line_ready = 1U;
  uart_cmd_reset_line();
}

static void uart_cmd_on_byte(uint8_t byte) {
  if ((byte == '\r') || (byte == '\n')) {
    uart_cmd_queue_line();
    return;
  }

  if (uart_cmd_line_len >= (UART_CMD_LINE_MAX - 1U)) {
    uart_cmd_reset_line();
    return;
  }

  uart_cmd_line[uart_cmd_line_len++] = (char)byte;
}

static void uart_cmd_process_ready_line(void) {
  char line_copy[UART_CMD_LINE_MAX];
  uint8_t ready = 0U;

  if (uart_cmd_line_ready == 0U) {
    return;
  }

  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (uart_cmd_line_ready != 0U) {
    (void)strncpy(line_copy, uart_cmd_pending_line, UART_CMD_LINE_MAX - 1U);
    line_copy[UART_CMD_LINE_MAX - 1U] = '\0';
    uart_cmd_line_ready = 0U;
    ready = 1U;
  }
  __set_PRIMASK(primask);

  if (ready != 0U) {
    uart_cmd_handle_line(line_copy);
  }
}

void UartCmd_Init(UART_HandleTypeDef *huart) {
  uart_cmd_huart = huart;
  uart_cmd_reset_line();
  uart_cmd_pending_line[0] = '\0';
  uart_cmd_line_ready = 0U;
  uart_cmd_measure_pending = 0U;
  uart_cmd_target_node = UART_CMD_TARGET_ACTIVE;

  if (huart != NULL) {
    (void)HAL_UART_Receive_IT(huart, &uart_cmd_rx_byte, 1U);
  }
}

void UartCmd_Process(WS_Manager_t *ws) {
  uart_cmd_process_ready_line();

  if ((ws == NULL) || (uart_cmd_measure_pending == 0U)) {
    return;
  }

  WS_NodeState_t *node = WS_GetActiveNode(ws);
  if ((node == NULL) || (node->state != WS_NODE_IDLE) || (ws->app_state != WS_APP_IDLE)) {
    return;
  }

  if (uart_cmd_target_node != UART_CMD_TARGET_ACTIVE) {
    if (uart_cmd_target_node >= ws->node_count) {
      uart_cmd_measure_pending = 0U;
      return;
    }
    ws->active_node = uart_cmd_target_node;
  }

  uart_cmd_measure_pending = 0U;
  WS_RequestMeasurementForActiveNode(ws);
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
