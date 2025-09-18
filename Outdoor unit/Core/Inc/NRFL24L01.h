#ifndef NRF24L01_H
#define NRF24L01_H

#include "stm32f3xx_hal.h"

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
} nrf24_register_t;

// SPI commands
typedef enum {
NRF24_CMD_R_REGISTER = 0x00,
NRF24_CMD_W_REGISTER = 0x20,
NRF24_CMD_R_RX_PAYLOAD = 0x61,
NRF24_CMD_W_TX_PAYLOAD = 0xA0,
NRF24_CMD_FLUSH_TX = 0xE1,
NRF24_CMD_FLUSH_RX = 0xE2,
NRF24_CMD_REUSE_TX_PL = 0xE3,
NRF24_CMD_R_RX_PL_WID = 0x60,
NRF24_CMD_W_ACK_PAYLOAD = 0xA8,
NRF24_CMD_W_TX_PAYLOAD_NOACK = 0xB0,
NRF24_CMD_NOP = 0xFF
} nrf24_command_t;

// CONFIG register bits
typedef enum {
NRF24_CONFIG_PRIM_RX = 0x01,
NRF24_CONFIG_PWR_UP = 0x02,
NRF24_CONFIG_CRCO = 0x04,
NRF24_CONFIG_EN_CRC = 0x08,
NRF24_CONFIG_MASK_MAX_RT = 0x10,
NRF24_CONFIG_MASK_TX_DS = 0x20,
NRF24_CONFIG_MASK_RX_DR = 0x40
} nrf24_config_t;

// STATUS register bits
typedef enum {
NRF24_STATUS_TX_FULL = 0x01,
NRF24_STATUS_MAX_RT = 0x10,
NRF24_STATUS_TX_DS = 0x20,
NRF24_STATUS_RX_DR = 0x40
} nrf24_status_t;

// Data rates
typedef enum {
NRF24_DR_250KBPS = 0x00,
NRF24_DR_1MBPS = 0x00,
NRF24_DR_2MBPS = 0x08
} nrf24_data_rate_t;

// PA levels
typedef enum {
NRF24_PA_MIN = 0x00, // -18dBm
NRF24_PA_LOW = 0x02, // -12dBm
NRF24_PA_HIGH = 0x04, // -6dBm
NRF24_PA_MAX = 0x06 // 0dBm
} nrf24_pa_level_t;

// Address widths
typedef enum {
NRF24_AW_3 = 0x01,
NRF24_AW_4 = 0x02,
NRF24_AW_5 = 0x03
} nrf24_addr_width_t;

// Modes
typedef enum {
NRF24_MODE_POWER_DOWN = 0x00,
NRF24_MODE_STANDBY = 0x01,
NRF24_MODE_RX = 0x02,
NRF24_MODE_TX = 0x03
} nrf24_mode_t;

// CRC modes
typedef enum {
NRF24_CRC_OFF = 0x00,
NRF24_CRC_1B = 0x01,
NRF24_CRC_2B = 0x02
} nrf24_crc_t;

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
} nrf24_handle_t;

// Function prototypes
HAL_StatusTypeDef NRF24_Init(nrf24_handle_t *handle, SPI_HandleTypeDef *hspi, GPIO_TypeDef *csn_port, uint16_t csn_pin,
GPIO_TypeDef *ce_port, uint16_t ce_pin, GPIO_TypeDef *irq_port, uint16_t irq_pin,
void (*delay_us)(uint32_t));
HAL_StatusTypeDef NRF24_ReadReg(nrf24_handle_t *handle, uint8_t reg, uint8_t *value);
HAL_StatusTypeDef NRF24_WriteReg(nrf24_handle_t *handle, uint8_t reg, uint8_t value);
HAL_StatusTypeDef NRF24_ReadRegs(nrf24_handle_t *handle, uint8_t reg, uint8_t *buf, uint8_t len);
HAL_StatusTypeDef NRF24_WriteRegs(nrf24_handle_t *handle, uint8_t reg, uint8_t *buf, uint8_t len);
uint8_t NRF24_GetStatus(nrf24_handle_t *handle);
HAL_StatusTypeDef NRF24_ClearIRQ(nrf24_handle_t *handle, uint8_t irq_flag);
HAL_StatusTypeDef NRF24_SetMode(nrf24_handle_t *handle, nrf24_mode_t mode);
HAL_StatusTypeDef NRF24_SetChannel(nrf24_handle_t *handle, uint8_t channel);
HAL_StatusTypeDef NRF24_SetDataRate(nrf24_handle_t *handle, nrf24_data_rate_t rate);
HAL_StatusTypeDef NRF24_SetPALevel(nrf24_handle_t *handle, nrf24_pa_level_t level);
HAL_StatusTypeDef NRF24_SetAddressWidth(nrf24_handle_t *handle, nrf24_addr_width_t width);
HAL_StatusTypeDef NRF24_SetAutoAck(nrf24_handle_t *handle, uint8_t pipe, uint8_t enable);
HAL_StatusTypeDef NRF24_EnablePipe(nrf24_handle_t *handle, uint8_t pipe, uint8_t enable);
HAL_StatusTypeDef NRF24_SetRXAddress(nrf24_handle_t *handle, uint8_t pipe, const uint8_t *addr, uint8_t len);
HAL_StatusTypeDef NRF24_SetTXAddress(nrf24_handle_t *handle, const uint8_t *addr, uint8_t len);
HAL_StatusTypeDef NRF24_SetPayloadSize(nrf24_handle_t *handle, uint8_t pipe, uint8_t size);
HAL_StatusTypeDef NRF24_EnableDynamicPayload(nrf24_handle_t *handle, uint8_t pipe, uint8_t enable);
uint8_t NRF24_IsDataAvailable(nrf24_handle_t *handle, uint8_t *pipe);
HAL_StatusTypeDef NRF24_ReadPayload(nrf24_handle_t *handle, uint8_t *buf, uint8_t len);
HAL_StatusTypeDef NRF24_WritePayload(nrf24_handle_t *handle, const uint8_t *buf, uint8_t len);
HAL_StatusTypeDef NRF24_FlushTX(nrf24_handle_t *handle);
HAL_StatusTypeDef NRF24_FlushRX(nrf24_handle_t *handle);
void NRF24_IRQ_Handler(nrf24_handle_t *handle);
HAL_StatusTypeDef NRF24_SetCRC(nrf24_handle_t *handle, nrf24_crc_t crc);
HAL_StatusTypeDef NRF24_SetAutoRetr(nrf24_handle_t *handle, uint8_t ard, uint8_t arc);
HAL_StatusTypeDef NRF24_EnableDynAck(nrf24_handle_t *handle, uint8_t enable);
HAL_StatusTypeDef NRF24_EnableAckPay(nrf24_handle_t *handle, uint8_t enable);
HAL_StatusTypeDef NRF24_WritePayloadNoAck(nrf24_handle_t *handle, const uint8_t *buf, uint8_t len);
HAL_StatusTypeDef NRF24_WriteAckPayload(nrf24_handle_t *handle, uint8_t pipe, const uint8_t *buf, uint8_t len);

#endif