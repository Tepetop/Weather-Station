#ifndef MEASUREMENT_UNIT_CONFIG_H

/*      INCLUDES    */
#include "si7021.h"
#include "TSL2561.h"
#include "bmp280.h"
#include "measurement.h"
#include "NRF24L01.h"



/*      DEFINES */
#define NRF_ENABLED 1
#define NRF_CHANNEL      76      // 2476 MHz - same as IndoorUnit
#define NRF_PAYLOAD_SIZE 20     // Payload size for measurement data
#define NRF_CMD_SIZE     8      // Command payload size
#define CMD_MEASURE      0x01   // Command to request measurement
#define NRF_TX_TIMEOUT_MS 120U

/* Node identity — change this per outdoor unit (0-3) */
#define NODE_ID          0U


/*          VARIABES AND STRUCTS */
typedef enum {
  OUT_LINK_IDLE = 0,
  OUT_LINK_CMD_PENDING,
  OUT_LINK_TX_IN_PROGRESS,
  OUT_LINK_ERROR
} OutdoorLinkStateEnum_t;

typedef struct {
  volatile uint8_t irq_flag;
  volatile uint8_t cmd_received;
  volatile uint8_t tx_in_progress;
  volatile uint8_t tx_done;
  volatile uint8_t tx_ok;
  uint32_t tx_start_tick;
  uint8_t last_status;
  OutdoorLinkStateEnum_t state;
} OutdoorLinkContext_t;

Measurement_Data_t txData;
char Message[128]; // Message to transfer by UART
uint8_t Length; // Message length

/*      MULTIRECEIVER  SETTINGS */
NRF24_Handle_t nrf;
OutdoorLinkContext_t outLink = {0};

/*
 * Multiceiver address scheme (must match IndoorUnit config):
 *
 * Indoor central station:
 *   TX_ADDR[node]  = {0xE7+node, 0xE7, 0xE7, 0xE7, 0xE7}  (sends commands)
 *   RX Pipe 1      = {0xC2, 0xC2, 0xC2, 0xC2, 0xC2}        (full addr, Node 0)
 *   RX Pipe 2      = LSB 0xC3  (Node 1, shares MSBytes with Pipe 1)
 *   RX Pipe 3      = LSB 0xC4  (Node 2)
 *   RX Pipe 4      = LSB 0xC5  (Node 3)
 *
 * This outdoor unit (NODE_ID):
 *   TX_ADDR = {0xC2+NODE_ID, 0xC2, 0xC2, 0xC2, 0xC2}  → Indoor's Pipe (1+NODE_ID)
 *   RX Pipe 1 = {0xE7+NODE_ID, 0xE7, 0xE7, 0xE7, 0xE7} ← Indoor's TX for this node
 *   Pipe 0 = TX_ADDR (for auto-ACK)
 */
static const uint8_t NRF_TX_ADDR[5] = {0xC2 + NODE_ID, 0xC2, 0xC2, 0xC2, 0xC2};
static const uint8_t NRF_RX_ADDR[5] = {0xE7 + NODE_ID, 0xE7, 0xE7, 0xE7, 0xE7};




#define MEASUREMENT_UNIT_CONFIG_H
#endif