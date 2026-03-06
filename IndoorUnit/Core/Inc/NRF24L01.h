#ifndef NRF24L01_H
#define NRF24L01_H

#include "stm32f1xx_hal.h"

// Register addresses
typedef enum {
NRF24_REG_CONFIG = 0x00,
NRF24_REG_EN_AA = 0x01,
NRF24_REG_EN_RXADDR = 0x02,
NRF24_REG_SETUP_AW = 0x03,
NRF24_REG_SETUP_RETR = 0x04,
NRF24_REG_RF_CH = 0x05,
NRF24_REG_RF_SETUP = 0x06,
NRF24_REG_STATUS = 0x07,
NRF24_REG_OBSERVE_TX = 0x08,
NRF24_REG_RPD = 0x09,
NRF24_REG_RX_ADDR_P0 = 0x0A,
NRF24_REG_RX_ADDR_P1 = 0x0B,
NRF24_REG_RX_ADDR_P2 = 0x0C,
NRF24_REG_RX_ADDR_P3 = 0x0D,
NRF24_REG_RX_ADDR_P4 = 0x0E,
NRF24_REG_RX_ADDR_P5 = 0x0F,
NRF24_REG_TX_ADDR = 0x10,
NRF24_REG_RX_PW_P0 = 0x11,
NRF24_REG_RX_PW_P1 = 0x12,
NRF24_REG_RX_PW_P2 = 0x13,
NRF24_REG_RX_PW_P3 = 0x14,
NRF24_REG_RX_PW_P4 = 0x15,
NRF24_REG_RX_PW_P5 = 0x16,
NRF24_REG_FIFO_STATUS = 0x17,
NRF24_REG_DYNPD = 0x1C,
NRF24_REG_FEATURE = 0x1D
} NRF24_Register_t;

// SPI commands
typedef enum {
NRF24_CMD_R_REGISTER = 0x00,
NRF24_CMD_W_REGISTER = 0x20,
NRF24_CMD_R_RX_PAYLOAD = 0x61,
NRF24_CMD_W_TX_PAYLOAD = 0xA0,
NRF24_CMD_FLUSH_TX = 0xE1,
NRF24_CMD_FLUSH_RX = 0xE2,
NRF24_CMD_REUSE_TX_PL = 0xE3,
NRF24_CMD_ACTIVATE = 0x50,
NRF24_CMD_R_RX_PL_WID = 0x60,
NRF24_CMD_W_ACK_PAYLOAD = 0xA8,
NRF24_CMD_W_TX_PAYLOAD_NOACK = 0xB0,
NRF24_CMD_NOP = 0xFF
} NRF24_Command_t;

// CONFIG register bits
typedef enum {
NRF24_CONFIG_PRIM_RX = 0x01,
NRF24_CONFIG_PWR_UP = 0x02,
NRF24_CONFIG_CRCO = 0x04,
NRF24_CONFIG_EN_CRC = 0x08,
NRF24_CONFIG_MASK_MAX_RT = 0x10,
NRF24_CONFIG_MASK_TX_DS = 0x20,
NRF24_CONFIG_MASK_RX_DR = 0x40
} NRF24_Config_t;

// STATUS register bits
typedef enum {
NRF24_STATUS_TX_FULL = 0x01,        // TX FIFO full flag
NRF24_STATUS_RX_P_NO_MASK = 0x0E,   // Bits 3:1 - data pipe number
NRF24_STATUS_MAX_RT = 0x10,         // Max retransmits
NRF24_STATUS_TX_DS = 0x20,          // Data sent
NRF24_STATUS_RX_DR = 0x40,          // Data received
NRF24_STATUS_IRQ_MASK = 0x70        // All IRQ flags combined
} NRF24_Status_t;

// FIFO_STATUS register bits
typedef enum {
NRF24_FIFO_RX_EMPTY = 0x01,
NRF24_FIFO_RX_FULL = 0x02,
NRF24_FIFO_TX_EMPTY = 0x10,
NRF24_FIFO_TX_FULL = 0x20,
NRF24_FIFO_TX_REUSE = 0x40
} NRF24_FIFO_Status_t;

// FEATURE register bits
#define NRF24_FEATURE_EN_DYN_ACK 0x01
#define NRF24_FEATURE_EN_ACK_PAY 0x02
#define NRF24_FEATURE_EN_DPL     0x04

// Max payload size per datasheet
#define NRF24_MAX_PAYLOAD_SIZE   32

// Data rates
// Note: 250kbps only available on nRF24L01+ (not original nRF24L01)
typedef enum {
NRF24_DR_250KBPS = 0x20, // RF_DR_LOW=1, RF_DR_HIGH=0 (nRF24L01+ only)
NRF24_DR_1MBPS = 0x00,  // RF_DR_LOW=0, RF_DR_HIGH=0
NRF24_DR_2MBPS = 0x08   // RF_DR_LOW=0, RF_DR_HIGH=1
} NRF24_DataRate_t;

// PA levels
typedef enum {
NRF24_PA_MIN = 0x00, // -18dBm
NRF24_PA_LOW = 0x02, // -12dBm
NRF24_PA_HIGH = 0x04, // -6dBm
NRF24_PA_MAX = 0x06 // 0dBm
} NRF24_PALevel_t;

// Address widths
typedef enum {
NRF24_AW_3 = 0x01,
NRF24_AW_4 = 0x02,
NRF24_AW_5 = 0x03
} NRF24_AddrWidth_t;

// Modes
typedef enum {
NRF24_MODE_POWER_DOWN = 0x00,
NRF24_MODE_STANDBY = 0x01,
NRF24_MODE_RX = 0x02,
NRF24_MODE_TX = 0x03
} NRF24_Mode_t;

// CRC modes
typedef enum {
NRF24_CRC_OFF = 0x00,
NRF24_CRC_1B = 0x01,
NRF24_CRC_2B = 0x02
} NRF24_CRC_t;

// Structure for device handle
typedef struct {
SPI_HandleTypeDef *hspi;
GPIO_TypeDef *csn_port;
uint16_t csn_pin;
GPIO_TypeDef *ce_port;
uint16_t ce_pin;
GPIO_TypeDef *irq_port;
uint16_t irq_pin;
void (*delay_us)(uint32_t); // Pointer to user-defined delay function in microseconds
} NRF24_Handle_t;

// Function prototypes
HAL_StatusTypeDef NRF24_Init(NRF24_Handle_t *handle, SPI_HandleTypeDef *hspi, GPIO_TypeDef *csn_port, uint16_t csn_pin,
GPIO_TypeDef *ce_port, uint16_t ce_pin, GPIO_TypeDef *irq_port, uint16_t irq_pin,
void (*delay_us)(uint32_t));
HAL_StatusTypeDef NRF24_ReadReg(NRF24_Handle_t *handle, uint8_t reg, uint8_t *value);
HAL_StatusTypeDef NRF24_WriteReg(NRF24_Handle_t *handle, uint8_t reg, uint8_t value);
HAL_StatusTypeDef NRF24_ReadRegs(NRF24_Handle_t *handle, uint8_t reg, uint8_t *buf, uint8_t len);
HAL_StatusTypeDef NRF24_WriteRegs(NRF24_Handle_t *handle, uint8_t reg, uint8_t *buf, uint8_t len);
uint8_t NRF24_GetStatus(NRF24_Handle_t *handle);
HAL_StatusTypeDef NRF24_ClearIRQ(NRF24_Handle_t *handle, uint8_t irq_flag);
HAL_StatusTypeDef NRF24_SetMode(NRF24_Handle_t *handle, NRF24_Mode_t mode);
HAL_StatusTypeDef NRF24_SetChannel(NRF24_Handle_t *handle, uint8_t channel);
HAL_StatusTypeDef NRF24_SetDataRate(NRF24_Handle_t *handle, NRF24_DataRate_t rate);
HAL_StatusTypeDef NRF24_SetPALevel(NRF24_Handle_t *handle, NRF24_PALevel_t level);
HAL_StatusTypeDef NRF24_SetAddressWidth(NRF24_Handle_t *handle, NRF24_AddrWidth_t width);
HAL_StatusTypeDef NRF24_SetAutoAck(NRF24_Handle_t *handle, uint8_t pipe, uint8_t enable);
HAL_StatusTypeDef NRF24_EnablePipe(NRF24_Handle_t *handle, uint8_t pipe, uint8_t enable);
HAL_StatusTypeDef NRF24_SetRXAddress(NRF24_Handle_t *handle, uint8_t pipe, const uint8_t *addr, uint8_t len);
HAL_StatusTypeDef NRF24_SetTXAddress(NRF24_Handle_t *handle, const uint8_t *addr, uint8_t len);
HAL_StatusTypeDef NRF24_SetPayloadSize(NRF24_Handle_t *handle, uint8_t pipe, uint8_t size);
HAL_StatusTypeDef NRF24_EnableDynamicPayload(NRF24_Handle_t *handle, uint8_t pipe, uint8_t enable);
uint8_t NRF24_IsDataAvailable(NRF24_Handle_t *handle, uint8_t *pipe);
HAL_StatusTypeDef NRF24_ReadPayload(NRF24_Handle_t *handle, uint8_t *buf, uint8_t len);
HAL_StatusTypeDef NRF24_WritePayload(NRF24_Handle_t *handle, const uint8_t *buf, uint8_t len);
HAL_StatusTypeDef NRF24_FlushTX(NRF24_Handle_t *handle);
HAL_StatusTypeDef NRF24_FlushRX(NRF24_Handle_t *handle);
void NRF24_IRQ_Handler(NRF24_Handle_t *handle);
HAL_StatusTypeDef NRF24_SetCRC(NRF24_Handle_t *handle, NRF24_CRC_t crc);
HAL_StatusTypeDef NRF24_SetAutoRetr(NRF24_Handle_t *handle, uint8_t ard, uint8_t arc);
HAL_StatusTypeDef NRF24_EnableDynAck(NRF24_Handle_t *handle, uint8_t enable);
HAL_StatusTypeDef NRF24_EnableAckPay(NRF24_Handle_t *handle, uint8_t enable);
HAL_StatusTypeDef NRF24_WritePayloadNoAck(NRF24_Handle_t *handle, const uint8_t *buf, uint8_t len);
HAL_StatusTypeDef NRF24_WriteAckPayload(NRF24_Handle_t *handle, uint8_t pipe, const uint8_t *buf, uint8_t len);
HAL_StatusTypeDef NRF24_Activate(NRF24_Handle_t *handle);
uint8_t NRF24_ReadDynamicPayloadWidth(NRF24_Handle_t *handle);
uint8_t NRF24_GetFIFOStatus(NRF24_Handle_t *handle);
HAL_StatusTypeDef NRF24_PowerUp(NRF24_Handle_t *handle);
HAL_StatusTypeDef NRF24_PowerDown(NRF24_Handle_t *handle);
uint8_t NRF24_GetObserveTX(NRF24_Handle_t *handle);
uint8_t NRF24_GetCarrierDetect(NRF24_Handle_t *handle);

#endif