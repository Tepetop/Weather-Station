#include "NRF24L01.h"
#include <string.h>

// Private functions
static void csn_low(nrf24_handle_t *handle) {
HAL_GPIO_WritePin(handle->csn_port, handle->csn_pin, GPIO_PIN_RESET);
}

static void csn_high(nrf24_handle_t *handle) {
HAL_GPIO_WritePin(handle->csn_port, handle->csn_pin, GPIO_PIN_SET);
}

static void ce_low(nrf24_handle_t *handle) {
HAL_GPIO_WritePin(handle->ce_port, handle->ce_pin, GPIO_PIN_RESET);
}

static void ce_high(nrf24_handle_t *handle) {
HAL_GPIO_WritePin(handle->ce_port, handle->ce_pin, GPIO_PIN_SET);
}

static HAL_StatusTypeDef spi_transfer(nrf24_handle_t *handle, uint8_t *tx, uint8_t *rx, uint16_t len) {
return HAL_SPI_TransmitReceive(handle->hspi, tx, rx, len, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef command(nrf24_handle_t *handle, uint8_t cmd, uint8_t *status) {
uint8_t tx = cmd;
uint8_t rx;
csn_low(handle);
HAL_StatusTypeDef ret = spi_transfer(handle, &tx, &rx, 1);
csn_high(handle);
if (status) *status = rx;
return ret;
}

static HAL_StatusTypeDef write_payload(nrf24_handle_t *handle, uint8_t cmd, const uint8_t *buf, uint8_t len) {
csn_low(handle);
uint8_t tx = cmd;
uint8_t rx;
HAL_StatusTypeDef ret = spi_transfer(handle, &tx, &rx, 1);
if (ret == HAL_OK) {
ret = spi_transfer(handle, (uint8_t *)buf, NULL, len);
}
csn_high(handle);
return ret;
}

static HAL_StatusTypeDef read_payload(nrf24_handle_t *handle, uint8_t cmd, uint8_t *buf, uint8_t len) {
csn_low(handle);
uint8_t tx = cmd;
uint8_t rx;
HAL_StatusTypeDef ret = spi_transfer(handle, &tx, &rx, 1);
if (ret == HAL_OK) {
uint8_t nop[len];
memset(nop, NRF24_CMD_NOP, len);
ret = spi_transfer(handle, nop, buf, len);
}
csn_high(handle);
return ret;
}

// Public functions
/**
* @brief Initializes the nRF24L01+ device with provided parameters.
* @param handle Pointer to the nRF24 handle structure.
* @param hspi Pointer to SPI handle.
* @param csn_port CSN GPIO port.
* @param csn_pin CSN GPIO pin.
* @param ce_port CE GPIO port.
* @param ce_pin CE GPIO pin.
* @param irq_port IRQ GPIO port.
* @param irq_pin IRQ GPIO pin.
* @param delay_us Pointer to delay function in microseconds.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_Init(nrf24_handle_t *handle, SPI_HandleTypeDef *hspi, GPIO_TypeDef *csn_port, uint16_t csn_pin,
GPIO_TypeDef *ce_port, uint16_t ce_pin, GPIO_TypeDef *irq_port, uint16_t irq_pin,
void (*delay_us)(uint32_t)) {
// Initialize handle with provided parameters
handle->hspi = hspi;
handle->csn_port = csn_port;
handle->csn_pin = csn_pin;
handle->ce_port = ce_port;
handle->ce_pin = ce_pin;
handle->irq_port = irq_port;
handle->irq_pin = irq_pin;
handle->delay_us = delay_us;

ce_low(handle);
csn_high(handle);

// Power on reset delay
handle->delay_us(0x05); // 5µs delay

// Default configuration
HAL_StatusTypeDef status = NRF24_WriteReg(handle, NRF24_REG_CONFIG, 0x08); // EN_CRC
if (status != HAL_OK) return status;
status = NRF24_SetAddressWidth(handle, NRF24_AW_5);
if (status != HAL_OK) return status;
status = NRF24_SetDataRate(handle, NRF24_DR_1MBPS);
if (status != HAL_OK) return status;
status = NRF24_SetPALevel(handle, NRF24_PA_MAX);
if (status != HAL_OK) return status;
status = NRF24_SetChannel(handle, 0x4C); // Default channel
if (status != HAL_OK) return status;
status = NRF24_WriteReg(handle, NRF24_REG_DYNPD, 0x00); // Disable dynamic payload
if (status != HAL_OK) return status;
status = NRF24_WriteReg(handle, NRF24_REG_FEATURE, 0x00); // Disable features
if (status != HAL_OK) return status;
status = NRF24_FlushTX(handle);
if (status != HAL_OK) return status;
status = NRF24_FlushRX(handle);
if (status != HAL_OK) return status;
status = NRF24_ClearIRQ(handle, 0x70); // Clear all IRQs
return status;
}

/**
* @brief Reads a single register from the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
* @param reg Register address to read.
* @param value Pointer to store the read value.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_ReadReg(nrf24_handle_t *handle, uint8_t reg, uint8_t *value) {
uint8_t tx[2] = {NRF24_CMD_R_REGISTER | reg, NRF24_CMD_NOP};
uint8_t rx[2];
csn_low(handle);
HAL_StatusTypeDef status = spi_transfer(handle, tx, rx, 2);
csn_high(handle);
if (status == HAL_OK && value) *value = rx[1];
return status;
}

/**
* @brief Writes a single register in the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
* @param reg Register address to write.
* @param value Value to write to the register.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_WriteReg(nrf24_handle_t *handle, uint8_t reg, uint8_t value) {
uint8_t tx[2] = {NRF24_CMD_W_REGISTER | reg, value};
uint8_t rx[2];
csn_low(handle);
HAL_StatusTypeDef status = spi_transfer(handle, tx, rx, 2);
csn_high(handle);
return status;
}

/**
* @brief Reads multiple registers from the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
* @param reg Starting register address.
* @param buf Buffer to store the read data.
* @param len Number of bytes to read.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_ReadRegs(nrf24_handle_t *handle, uint8_t reg, uint8_t *buf, uint8_t len) {
return read_payload(handle, NRF24_CMD_R_REGISTER | reg, buf, len);
}

/**
* @brief Writes multiple registers in the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
* @param reg Starting register address.
* @param buf Buffer containing data to write.
* @param len Number of bytes to write.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_WriteRegs(nrf24_handle_t *handle, uint8_t reg, uint8_t *buf, uint8_t len) {
return write_payload(handle, NRF24_CMD_W_REGISTER | reg, buf, len);
}

/**
* @brief Gets the current status of the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
* @return Current status register value.
*/
uint8_t NRF24_GetStatus(nrf24_handle_t *handle) {
uint8_t status;
command(handle, NRF24_CMD_NOP, &status);
return status;
}

/**
* @brief Clears specified IRQ flags in the status register.
* @param handle Pointer to the nRF24 handle structure.
* @param irq_flag Bitmask of IRQ flags to clear.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_ClearIRQ(nrf24_handle_t *handle, uint8_t irq_flag) {
return NRF24_WriteReg(handle, NRF24_REG_STATUS, irq_flag);
}

/**
* @brief Sets the operating mode of the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
* @param mode Desired operating mode (Power Down, Standby, RX, TX).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetMode(nrf24_handle_t *handle, nrf24_mode_t mode) {
uint8_t config_val;
HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_CONFIG, &config_val);
if (status != HAL_OK) return status;

uint8_t config = config_val;

switch (mode) {
case NRF24_MODE_POWER_DOWN:
config &= ~NRF24_CONFIG_PWR_UP;
status = NRF24_WriteReg(handle, NRF24_REG_CONFIG, config);
if (status != HAL_OK) return status;
ce_low(handle);
break;

case NRF24_MODE_STANDBY:
config |= NRF24_CONFIG_PWR_UP;
status = NRF24_WriteReg(handle, NRF24_REG_CONFIG, config);
if (status != HAL_OK) return status;
ce_low(handle);
handle->delay_us(0x4B0); // Tpd2stby max 1.5ms (1200µs as safe margin)
break;

case NRF24_MODE_RX:
config |= (NRF24_CONFIG_PWR_UP | NRF24_CONFIG_PRIM_RX);
status = NRF24_WriteReg(handle, NRF24_REG_CONFIG, config);
if (status != HAL_OK) return status;
ce_high(handle);
handle->delay_us(0x82); // Tstby2a max 130µs
break;

case NRF24_MODE_TX:
config |= NRF24_CONFIG_PWR_UP;
config &= ~NRF24_CONFIG_PRIM_RX;
status = NRF24_WriteReg(handle, NRF24_REG_CONFIG, config);
if (status != HAL_OK) return status;
ce_high(handle); // Start transmission with CE high
handle->delay_us(0x0A); // Minimum 10µs CE pulse
ce_low(handle); // Return to Standby-I after transmission
break;
}
return HAL_OK;
}

/**
* @brief Sets the RF channel frequency.
* @param handle Pointer to the nRF24 handle structure.
* @param channel RF channel number (0-125).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetChannel(nrf24_handle_t *handle, uint8_t channel) {
if (channel > 0x7D) channel = 0x7D;
return NRF24_WriteReg(handle, NRF24_REG_RF_CH, channel);
}

/**
* @brief Sets the air data rate.
* @param handle Pointer to the nRF24 handle structure.
* @param rate Desired data rate (250kbps, 1Mbps, 2Mbps).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetDataRate(nrf24_handle_t *handle, nrf24_data_rate_t rate) {
uint8_t rf_setup;
HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_RF_SETUP, &rf_setup);
if (status != HAL_OK) return status;
rf_setup &= ~(0x28); // Clear RF_DR_LOW and RF_DR_HIGH

switch (rate) {
case NRF24_DR_250KBPS:
rf_setup |= 0x20;
break;
case NRF24_DR_2MBPS:
rf_setup |= 0x08;
break;
case NRF24_DR_1MBPS:
default:
// Default is 1Mbps
break;
}
return NRF24_WriteReg(handle, NRF24_REG_RF_SETUP, rf_setup);
}

/**
* @brief Sets the power amplifier level.
* @param handle Pointer to the nRF24 handle structure.
* @param level Desired PA level (-18dBm to 0dBm).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetPALevel(nrf24_handle_t *handle, nrf24_pa_level_t level) {
uint8_t rf_setup;
HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_RF_SETUP, &rf_setup);
if (status != HAL_OK) return status;
rf_setup &= ~0x06; // Clear RF_PWR
rf_setup |= level;
return NRF24_WriteReg(handle, NRF24_REG_RF_SETUP, rf_setup);
}

/**
* @brief Sets the address width for RX and TX addresses.
* @param handle Pointer to the nRF24 handle structure.
* @param width Address width (3, 4, or 5 bytes).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetAddressWidth(nrf24_handle_t *handle, nrf24_addr_width_t width) {
return NRF24_WriteReg(handle, NRF24_REG_SETUP_AW, width);
}

/**
* @brief Enables or disables auto-acknowledgement for a specific pipe.
* @param handle Pointer to the nRF24 handle structure.
* @param pipe Pipe number (0-5).
* @param enable Enable (1) or disable (0) auto-ack.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetAutoAck(nrf24_handle_t *handle, uint8_t pipe, uint8_t enable) {
if (pipe > 0x05) return HAL_ERROR;
uint8_t en_aa;
HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_EN_AA, &en_aa);
if (status != HAL_OK) return status;
if (enable) {
en_aa |= (0x01 << pipe);
} else {
en_aa &= ~(0x01 << pipe);
}
return NRF24_WriteReg(handle, NRF24_REG_EN_AA, en_aa);
}

/**
* @brief Enables or disables a specific data pipe.
* @param handle Pointer to the nRF24 handle structure.
* @param pipe Pipe number (0-5).
* @param enable Enable (1) or disable (0) the pipe.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_EnablePipe(nrf24_handle_t *handle, uint8_t pipe, uint8_t enable) {
if (pipe > 0x05) return HAL_ERROR;
uint8_t en_rxaddr;
HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_EN_RXADDR, &en_rxaddr);
if (status != HAL_OK) return status;
if (enable) {
en_rxaddr |= (0x01 << pipe);
} else {
en_rxaddr &= ~(0x01 << pipe);
}
return NRF24_WriteReg(handle, NRF24_REG_EN_RXADDR, en_rxaddr);
}

/**
* @brief Sets the RX address for a specific pipe.
* @param handle Pointer to the nRF24 handle structure.
* @param pipe Pipe number (0-5).
* @param addr Pointer to the address array.
* @param len Length of the address (up to 5 bytes).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetRXAddress(nrf24_handle_t *handle, uint8_t pipe, const uint8_t *addr, uint8_t len) {
if (pipe > 0x05 || len > 0x05) return HAL_ERROR;
if (pipe < 0x02) {
return NRF24_WriteRegs(handle, NRF24_REG_RX_ADDR_P0 + pipe, (uint8_t *)addr, len);
} else {
return NRF24_WriteReg(handle, NRF24_REG_RX_ADDR_P0 + pipe, addr[0]);
}
}

/**
* @brief Sets the TX address.
* @param handle Pointer to the nRF24 handle structure.
* @param addr Pointer to the address array.
* @param len Length of the address (up to 5 bytes).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetTXAddress(nrf24_handle_t *handle, const uint8_t *addr, uint8_t len) {
return NRF24_WriteRegs(handle, NRF24_REG_TX_ADDR, (uint8_t *)addr, len);
}

/**
* @brief Sets the payload size for a specific pipe.
* @param handle Pointer to the nRF24 handle structure.
* @param pipe Pipe number (0-5).
* @param size Payload size (1-32 bytes).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetPayloadSize(nrf24_handle_t *handle, uint8_t pipe, uint8_t size) {
if (pipe > 0x05 || size > 0x20) return HAL_ERROR;
return NRF24_WriteReg(handle, NRF24_REG_RX_PW_P0 + pipe, size);
}

/**
* @brief Enables or disables dynamic payload length for a specific pipe.
* @param handle Pointer to the nRF24 handle structure.
* @param pipe Pipe number (0-5).
* @param enable Enable (1) or disable (0) dynamic payload.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_EnableDynamicPayload(nrf24_handle_t *handle, uint8_t pipe, uint8_t enable) {
if (pipe > 0x05) return HAL_ERROR;
uint8_t dynpd;
HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_DYNPD, &dynpd);
if (status != HAL_OK) return status;
if (enable) {
dynpd |= (0x01 << pipe);
} else {
dynpd &= ~(0x01 << pipe);
}
status = NRF24_WriteReg(handle, NRF24_REG_DYNPD, dynpd);
if (status != HAL_OK) return status;

// Enable dynamic payload feature
uint8_t feature;
status = NRF24_ReadReg(handle, NRF24_REG_FEATURE, &feature);
if (status != HAL_OK) return status;
feature |= 0x04; // EN_DPL
return NRF24_WriteReg(handle, NRF24_REG_FEATURE, feature);
}

/**
* @brief Checks if data is available in any RX pipe.
* @param handle Pointer to the nRF24 handle structure.
* @param pipe Pointer to store the pipe number where data is available.
* @return 1 if data is available, 0 otherwise.
*/
uint8_t NRF24_IsDataAvailable(nrf24_handle_t *handle, uint8_t *pipe) {
uint8_t status = NRF24_GetStatus(handle);
if (status & NRF24_STATUS_RX_DR) {
if (pipe) *pipe = (status >> 0x01) & 0x07;
return 0x01;
}
return 0x00;
}

/**
* @brief Reads the payload data from the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
* @param buf Buffer to store the payload data.
* @param len Length of the payload to read.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_ReadPayload(nrf24_handle_t *handle, uint8_t *buf, uint8_t len) {
return read_payload(handle, NRF24_CMD_R_RX_PAYLOAD, buf, len);
}

/**
* @brief Writes the payload data to the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
* @param buf Buffer containing the payload data.
* @param len Length of the payload to write.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_WritePayload(nrf24_handle_t *handle, const uint8_t *buf, uint8_t len) {
return write_payload(handle, NRF24_CMD_W_TX_PAYLOAD, buf, len);
}

/**
* @brief Flushes the TX FIFO.
* @param handle Pointer to the nRF24 handle structure.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_FlushTX(nrf24_handle_t *handle) {
return command(handle, NRF24_CMD_FLUSH_TX, NULL);
}

/**
* @brief Flushes the RX FIFO.
* @param handle Pointer to the nRF24 handle structure.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_FlushRX(nrf24_handle_t *handle) {
return command(handle, NRF24_CMD_FLUSH_RX, NULL);
}

/**
* @brief Handles IRQ events from the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
*/
void NRF24_IRQ_Handler(nrf24_handle_t *handle) {
uint8_t status = NRF24_GetStatus(handle);

if (status & NRF24_STATUS_RX_DR) {
// Data received, user can handle
}
if (status & NRF24_STATUS_TX_DS) {
// Data sent
}
if (status & NRF24_STATUS_MAX_RT) {
// Max retransmits, flush TX
NRF24_FlushTX(handle);
}

NRF24_ClearIRQ(handle, status & 0x70); // Clear flags
}

/**
* @brief Sets the CRC mode.
* @param handle Pointer to the nRF24 handle structure.
* @param crc CRC mode (OFF, 1B, 2B).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetCRC(nrf24_handle_t *handle, nrf24_crc_t crc) {
uint8_t config;
HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_CONFIG, &config);
if (status != HAL_OK) return status;
config &= ~(NRF24_CONFIG_EN_CRC | NRF24_CONFIG_CRCO);
if (crc != NRF24_CRC_OFF) {
config |= NRF24_CONFIG_EN_CRC;
if (crc == NRF24_CRC_2B) {
config |= NRF24_CONFIG_CRCO;
}
}
return NRF24_WriteReg(handle, NRF24_REG_CONFIG, config);
}

/**
* @brief Sets the auto retransmit parameters.
* @param handle Pointer to the nRF24 handle structure.
* @param ard Auto retransmit delay code (0-15).
* @param arc Auto retransmit count (0-15).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetAutoRetr(nrf24_handle_t *handle, uint8_t ard, uint8_t arc) {
if (ard > 15 || arc > 15) return HAL_ERROR;
uint8_t val = (ard << 4) | arc;
return NRF24_WriteReg(handle, NRF24_REG_SETUP_RETR, val);
}

/**
* @brief Enables or disables dynamic ACK.
* @param handle Pointer to the nRF24 handle structure.
* @param enable Enable (1) or disable (0).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_EnableDynAck(nrf24_handle_t *handle, uint8_t enable) {
uint8_t feature;
HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_FEATURE, &feature);
if (status != HAL_OK) return status;
if (enable) {
feature |= 0x01; // EN_DYN_ACK
} else {
feature &= ~0x01;
}
return NRF24_WriteReg(handle, NRF24_REG_FEATURE, feature);
}

/**
* @brief Enables or disables ACK payload.
* @param handle Pointer to the nRF24 handle structure.
* @param enable Enable (1) or disable (0).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_EnableAckPay(nrf24_handle_t *handle, uint8_t enable) {
uint8_t feature;
HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_FEATURE, &feature);
if (status != HAL_OK) return status;
if (enable) {
feature |= 0x02; // EN_ACK_PAY
} else {
feature &= ~0x02;
}
return NRF24_WriteReg(handle, NRF24_REG_FEATURE, feature);
}

/**
* @brief Writes the payload data without requesting ACK.
* @param handle Pointer to the nRF24 handle structure.
* @param buf Buffer containing the payload data.
* @param len Length of the payload to write.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_WritePayloadNoAck(nrf24_handle_t *handle, const uint8_t *buf, uint8_t len) {
HAL_StatusTypeDef status = NRF24_EnableDynAck(handle, 1);
if (status != HAL_OK) return status;
return write_payload(handle, NRF24_CMD_W_TX_PAYLOAD_NOACK, buf, len);
}

/**
* @brief Writes the ACK payload for a specific pipe.
* @param handle Pointer to the nRF24 handle structure.
* @param pipe Pipe number (0-5).
* @param buf Buffer containing the payload data.
* @param len Length of the payload to write.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_WriteAckPayload(nrf24_handle_t *handle, uint8_t pipe, const uint8_t *buf, uint8_t len) {
if (pipe > 5) return HAL_ERROR;
HAL_StatusTypeDef status = NRF24_EnableAckPay(handle, 1);
if (status != HAL_OK) return status;
return write_payload(handle, NRF24_CMD_W_ACK_PAYLOAD | pipe, buf, len);
}