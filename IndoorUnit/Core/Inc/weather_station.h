#ifndef WEATHER_STATION_H
#define WEATHER_STATION_H

#include <stdbool.h>
#include <stdint.h>

#define WS_MAX_NODES 4U

typedef enum {
  WS_NODE_IDLE = 0,
  WS_NODE_TX_IN_PROGRESS,
  WS_NODE_WAIT_RESPONSE,
  WS_NODE_DATA_READY,
  WS_NODE_ERROR
} WS_NodeStateEnum_t;

typedef enum {
  WS_TX_EVENT_NONE = 0,
  WS_TX_EVENT_OK,
  WS_TX_EVENT_FAIL
} WS_TxEvent_t;

typedef struct __attribute__((packed)) {
  float si7021_temp;
  float si7021_hum;
  float bmp280_temp;
  float bmp280_press;
  float tsl2561_lux;
} WS_MeasurementData_t;

typedef struct {
  uint8_t tx_addr[5];
  uint8_t rx_addr[5];
  volatile uint8_t measurement_pending;
  volatile uint8_t awaiting_response;
  volatile uint8_t tx_in_progress;
  volatile uint8_t tx_done;
  volatile uint8_t tx_ok;
  volatile uint8_t data_received;
  uint32_t tx_start_tick;
  uint32_t response_start_tick;
  uint8_t last_status;
  uint8_t retry_count;
  WS_NodeStateEnum_t state;
  WS_MeasurementData_t data;
} WS_NodeState_t;

typedef struct {
  volatile uint8_t nrf_irq_flag;
  uint8_t active_node;
  uint8_t node_count;
  WS_NodeState_t nodes[WS_MAX_NODES];
} WS_Manager_t;

void WS_InitManager(WS_Manager_t *ctx, const uint8_t tx_addrs[][5], const uint8_t rx_addrs[][5], uint8_t node_count);

WS_NodeState_t *WS_GetActiveNode(WS_Manager_t *ctx);
const WS_NodeState_t *WS_GetActiveNodeConst(const WS_Manager_t *ctx);

void WS_SetIrqFlag(WS_Manager_t *ctx);
void WS_ClearIrqFlag(WS_Manager_t *ctx);

bool WS_ShouldFallbackToStatusRead(const WS_Manager_t *ctx);

void WS_RequestMeasurementForActiveNode(WS_Manager_t *ctx);
void WS_ConsumePendingForActiveNode(WS_Manager_t *ctx);

void WS_StartTxForActiveNode(WS_Manager_t *ctx, uint32_t now_tick);
void WS_MarkTxResultFromIrq(WS_Manager_t *ctx, bool ok, uint8_t status);
WS_TxEvent_t WS_ConsumeTxEvent(WS_Manager_t *ctx, uint32_t now_tick);

bool WS_IsActiveTxTimedOut(const WS_Manager_t *ctx, uint32_t now_tick, uint32_t timeout_ms);
void WS_HandleActiveTxTimeout(WS_Manager_t *ctx, uint8_t status);

void WS_MarkActiveResponseWaiting(WS_Manager_t *ctx, uint32_t now_tick);
bool WS_IsActiveRxTimedOut(const WS_Manager_t *ctx, uint32_t now_tick, uint32_t timeout_ms);
void WS_HandleActiveRxTimeout(WS_Manager_t *ctx, uint8_t status);

void WS_MarkActiveDataReceived(WS_Manager_t *ctx, const WS_MeasurementData_t *data, uint8_t status);
bool WS_ConsumeActiveDataReady(WS_Manager_t *ctx);

void WS_ScheduleNextNode(WS_Manager_t *ctx);

#endif
