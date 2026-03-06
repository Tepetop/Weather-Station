#include "weather_station.h"

#include <string.h>

static uint8_t ws_clamp_count(uint8_t node_count) {
  if (node_count == 0U) {
    return 1U;
  }
  if (node_count > WS_MAX_NODES) {
    return WS_MAX_NODES;
  }
  return node_count;
}

void WS_InitManager(WS_Manager_t *ctx,const uint8_t tx_addrs[][5],const uint8_t rx_addrs[][5],uint8_t node_count) {
  if (ctx == NULL) {
    return;
  }
  memset(ctx, 0, sizeof(*ctx));
  ctx->node_count = ws_clamp_count(node_count);
  ctx->active_node = 0U;

  for (uint8_t i = 0U; i < ctx->node_count; i++) {
    if (tx_addrs != NULL) {
      memcpy(ctx->nodes[i].tx_addr, tx_addrs[i], 5U);
    }
    if (rx_addrs != NULL) {
      memcpy(ctx->nodes[i].rx_addr, rx_addrs[i], 5U);
    }
    ctx->nodes[i].state = WS_NODE_IDLE;
  }
}

WS_NodeState_t *WS_GetActiveNode(WS_Manager_t *ctx) {
  if (ctx == NULL) {
    return NULL;
  }
  if (ctx->active_node >= ctx->node_count) {
    ctx->active_node = 0U;
  }
  return &ctx->nodes[ctx->active_node];
}

const WS_NodeState_t *WS_GetActiveNodeConst(const WS_Manager_t *ctx) {
  if ((ctx == NULL) || (ctx->active_node >= ctx->node_count)) {
    return NULL;
  }
  return &ctx->nodes[ctx->active_node];
}

void WS_SetIrqFlag(WS_Manager_t *ctx) {
  if (ctx != NULL) {
    ctx->nrf_irq_flag = 1U;
  }
}

void WS_ClearIrqFlag(WS_Manager_t *ctx) {
  if (ctx != NULL) {
    ctx->nrf_irq_flag = 0U;
  }
}

bool WS_ShouldFallbackToStatusRead(const WS_Manager_t *ctx) {
  const WS_NodeState_t *node = WS_GetActiveNodeConst(ctx);
  if (node == NULL) {
    return false;
  }
  return (node->tx_in_progress != 0U) || (node->awaiting_response != 0U);
}

void WS_RequestMeasurementForActiveNode(WS_Manager_t *ctx) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node != NULL) {
    node->measurement_pending = 1U;
  }
}

void WS_ConsumePendingForActiveNode(WS_Manager_t *ctx) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node != NULL) {
    node->measurement_pending = 0U;
  }
}

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

void WS_MarkTxResultFromIrq(WS_Manager_t *ctx, bool ok, uint8_t status) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  node->last_status = status;
  node->tx_ok = ok ? 1U : 0U;
  node->tx_done = 1U;
}

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

bool WS_IsActiveTxTimedOut(const WS_Manager_t *ctx, uint32_t now_tick, uint32_t timeout_ms) {
  const WS_NodeState_t *node = WS_GetActiveNodeConst(ctx);
  if ((node == NULL) || (node->tx_in_progress == 0U)) {
    return false;
  }
  return (now_tick - node->tx_start_tick) > timeout_ms;
}

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

void WS_MarkActiveResponseWaiting(WS_Manager_t *ctx, uint32_t now_tick) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  node->awaiting_response = 1U;
  node->response_start_tick = now_tick;
  node->state = WS_NODE_WAIT_RESPONSE;
}

bool WS_IsActiveRxTimedOut(const WS_Manager_t *ctx, uint32_t now_tick, uint32_t timeout_ms) {
  const WS_NodeState_t *node = WS_GetActiveNodeConst(ctx);
  if ((node == NULL) || (node->awaiting_response == 0U)) {
    return false;
  }
  return (now_tick - node->response_start_tick) > timeout_ms;
}

void WS_HandleActiveRxTimeout(WS_Manager_t *ctx, uint8_t status) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  node->awaiting_response = 0U;
  node->last_status = status;
  node->state = WS_NODE_ERROR;
}

void WS_MarkActiveDataReceived(WS_Manager_t *ctx, const WS_MeasurementData_t *data, uint8_t status) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if (node == NULL) {
    return;
  }

  if (data != NULL) {
    memcpy(&node->data, data, sizeof(node->data));
  }
  node->awaiting_response = 0U;
  node->data_received = 1U;
  node->last_status = status;
  node->state = WS_NODE_DATA_READY;
}

bool WS_ConsumeActiveDataReady(WS_Manager_t *ctx) {
  WS_NodeState_t *node = WS_GetActiveNode(ctx);
  if ((node == NULL) || (node->data_received == 0U)) {
    return false;
  }

  node->data_received = 0U;
  node->state = WS_NODE_IDLE;
  return true;
}

void WS_ScheduleNextNode(WS_Manager_t *ctx) {
  if ((ctx == NULL) || (ctx->node_count <= 1U)) {
    return;
  }

  ctx->active_node++;
  if (ctx->active_node >= ctx->node_count) {
    ctx->active_node = 0U;
  }
}
