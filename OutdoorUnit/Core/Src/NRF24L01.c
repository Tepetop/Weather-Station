#include "NRF24L01.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * Private (static) helper functions
 * --------------------------------------------------------------------------- */

/**
 * @brief Drives CSN (Chip Select Not) pin low to start SPI transaction.
 * @param handle Pointer to the nRF24 handle structure.
 */
static void csn_low(NRF24_Handle_t *handle) {
    HAL_GPIO_WritePin(handle->csn_port, handle->csn_pin, GPIO_PIN_RESET);
}

/**
 * @brief Drives CSN (Chip Select Not) pin high to end SPI transaction.
 * @param handle Pointer to the nRF24 handle structure.
 */
static void csn_high(NRF24_Handle_t *handle) {
    HAL_GPIO_WritePin(handle->csn_port, handle->csn_pin, GPIO_PIN_SET);
}

/**
 * @brief Drives CE (Chip Enable) pin low to deactivate TX/RX mode.
 * @param handle Pointer to the nRF24 handle structure.
 */
static void ce_low(NRF24_Handle_t *handle) {
    HAL_GPIO_WritePin(handle->ce_port, handle->ce_pin, GPIO_PIN_RESET);
}

/**
 * @brief Drives CE (Chip Enable) pin high to activate TX/RX mode.
 * @param handle Pointer to the nRF24 handle structure.
 */
static void ce_high(NRF24_Handle_t *handle) {
    HAL_GPIO_WritePin(handle->ce_port, handle->ce_pin, GPIO_PIN_SET);
}

/**
 * @brief Performs a full-duplex SPI transfer.
 * @param handle Pointer to the nRF24 handle structure.
 * @param tx    Pointer to transmit buffer.
 * @param rx    Pointer to receive buffer.
 * @param len   Number of bytes to transfer.
 * @return HAL status.
 */
static HAL_StatusTypeDef spi_transfer(NRF24_Handle_t *handle, uint8_t *tx, uint8_t *rx, uint16_t len) {
    return HAL_SPI_TransmitReceive(handle->hspi, tx, rx, len, HAL_MAX_DELAY);
}

/**
 * @brief Sends a single-byte SPI command and optionally returns the status byte.
 * @param handle Pointer to the nRF24 handle structure.
 * @param cmd    Command byte to send.
 * @param status Pointer to store the returned status byte (may be NULL).
 * @return HAL status.
 */
static HAL_StatusTypeDef command(NRF24_Handle_t *handle, uint8_t cmd, uint8_t *status) {
    uint8_t tx = cmd;
    uint8_t rx;
    csn_low(handle);
    HAL_StatusTypeDef ret = spi_transfer(handle, &tx, &rx, 1);
    csn_high(handle);
    if (status) *status = rx;
    return ret;
}

/**
 * @brief Writes a multi-byte payload over SPI preceded by a command byte.
 * @param handle Pointer to the nRF24 handle structure.
 * @param cmd    Command byte (e.g. W_TX_PAYLOAD).
 * @param buf    Pointer to the data buffer to write.
 * @param len    Number of payload bytes.
 * @return HAL status.
 */
static HAL_StatusTypeDef write_payload(NRF24_Handle_t *handle, uint8_t cmd, const uint8_t *buf, uint8_t len) {
    csn_low(handle);
    uint8_t tx = cmd;
    uint8_t rx;
    HAL_StatusTypeDef ret = spi_transfer(handle, &tx, &rx, 1);

    if (ret == HAL_OK) {
        uint8_t dummy[NRF24_MAX_PAYLOAD_SIZE];
        ret = spi_transfer(handle, (uint8_t *)buf, dummy, len);
    }
    csn_high(handle);
    return ret;
}

/**
 * @brief Reads a multi-byte payload over SPI preceded by a command byte.
 * @param handle Pointer to the nRF24 handle structure.
 * @param cmd    Command byte (e.g. R_RX_PAYLOAD).
 * @param buf    Pointer to the buffer to store received data.
 * @param len    Number of payload bytes to read.
 * @return HAL status.
 */
static HAL_StatusTypeDef read_payload(NRF24_Handle_t *handle, uint8_t cmd, uint8_t *buf, uint8_t len) {
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
HAL_StatusTypeDef NRF24_Init(NRF24_Handle_t *handle, SPI_HandleTypeDef *hspi, GPIO_TypeDef *csn_port, uint16_t csn_pin,
                            GPIO_TypeDef *ce_port, uint16_t ce_pin, GPIO_TypeDef *irq_port, uint16_t irq_pin,
                            void (*delay_us)(uint32_t)) 
{
    if(hspi == NULL || csn_port == NULL || ce_port == NULL || irq_port == NULL || delay_us == NULL) {
        return HAL_ERROR; // Invalid parameters
    }

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

    // Power on reset delay - datasheet specifies device needs time after VDD ramp
    handle->delay_us(5000); // 5ms delay for power-on reset

    // Send ACTIVATE command to enable FEATURE, DYNPD, ACK_PAYLOAD, etc.
    // Required for nRF24L01 (not needed for nRF24L01+ but harmless)
    NRF24_Activate(handle);

    // Default configuration: CRC enabled (1 byte)
    HAL_StatusTypeDef status = NRF24_WriteReg(handle, NRF24_REG_CONFIG, 0x08); // EN_CRC
    if (status != HAL_OK) return status;

    // Set auto-acknowledgement for all pipes (datasheet default: 0x3F)
    status = NRF24_WriteReg(handle, NRF24_REG_EN_AA, 0x3F);
    if (status != HAL_OK) return status;

    // Set auto retransmit: 500us delay, 3 retransmits
    // ARD=500us gives enough time for ACK with any payload length
    status = NRF24_SetAutoRetr(handle, 1, 3); // ARD=500us (1), ARC=3
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
HAL_StatusTypeDef NRF24_ReadReg(NRF24_Handle_t *handle, uint8_t reg, uint8_t *value) {
    if (handle == NULL) {
        return HAL_ERROR; // Invalid handle
    }
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
HAL_StatusTypeDef NRF24_WriteReg(NRF24_Handle_t *handle, uint8_t reg, uint8_t value) {
    if (handle == NULL) {
        return HAL_ERROR; // Invalid handle
    }
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
HAL_StatusTypeDef NRF24_ReadRegs(NRF24_Handle_t *handle, uint8_t reg, uint8_t *buf, uint8_t len) {
    if (handle == NULL || buf == NULL) {
        return HAL_ERROR; // Invalid handle
    }
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
HAL_StatusTypeDef NRF24_WriteRegs(NRF24_Handle_t *handle, uint8_t reg, uint8_t *buf, uint8_t len) {
    if (handle == NULL || buf == NULL) {
        return HAL_ERROR; // Invalid handle
    }
    return write_payload(handle, NRF24_CMD_W_REGISTER | reg, buf, len);
}

/**
* @brief Gets the current status of the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
* @return Current status register value.
*/
uint8_t NRF24_GetStatus(NRF24_Handle_t *handle) {
    if (handle == NULL) {
        return HAL_ERROR; // Invalid handle
    }
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
HAL_StatusTypeDef NRF24_ClearIRQ(NRF24_Handle_t *handle, uint8_t irq_flag) {
    if (handle == NULL) {
        return HAL_ERROR; // Invalid handle
    }
    return NRF24_WriteReg(handle, NRF24_REG_STATUS, irq_flag);
}

/**
* @brief Sets the operating mode of the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
* @param mode Desired operating mode (Power Down, Standby, RX, TX).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetMode(NRF24_Handle_t *handle, NRF24_Mode_t mode) {
    if(handle == NULL) {
        return HAL_ERROR; // Invalid handle
    }
    
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
            handle->delay_us(1500); // Tpd2stby max 1.5ms = 1500µs
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
HAL_StatusTypeDef NRF24_SetChannel(NRF24_Handle_t *handle, uint8_t channel) {
    if(handle == NULL) {
        return HAL_ERROR; // Invalid handle
    }
    if (channel > 0x7D) channel = 0x7D;
    return NRF24_WriteReg(handle, NRF24_REG_RF_CH, channel);
}

/**
* @brief Sets the air data rate.
* @param handle Pointer to the nRF24 handle structure.
* @param rate Desired data rate (250kbps, 1Mbps, 2Mbps).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetDataRate(NRF24_Handle_t *handle, NRF24_DataRate_t rate) {
    if(handle == NULL) {
        return HAL_ERROR; // Invalid handle
    }
    uint8_t rf_setup;
    HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_RF_SETUP, &rf_setup);
    if (status != HAL_OK) return status;
    rf_setup &= ~(0x28); // Clear RF_DR_LOW (bit 5) and RF_DR_HIGH (bit 3)

    switch (rate) {
        case NRF24_DR_250KBPS:
            rf_setup |= 0x20; // RF_DR_LOW=1, RF_DR_HIGH=0 (nRF24L01+ only)
            break;

        case NRF24_DR_2MBPS:
            rf_setup |= 0x08; // RF_DR_LOW=0, RF_DR_HIGH=1
            break;

        case NRF24_DR_1MBPS:
        default:
            // RF_DR_LOW=0, RF_DR_HIGH=0 → 1Mbps (no bits to set)
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
HAL_StatusTypeDef NRF24_SetPALevel(NRF24_Handle_t *handle, NRF24_PALevel_t level) {
    if(handle == NULL) {
        return HAL_ERROR; // Invalid handle
    }
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
HAL_StatusTypeDef NRF24_SetAddressWidth(NRF24_Handle_t *handle, NRF24_AddrWidth_t width) {
    if(handle == NULL) {
        return HAL_ERROR; // Invalid handle
    }
    return NRF24_WriteReg(handle, NRF24_REG_SETUP_AW, width);
}

/**
* @brief Enables or disables auto-acknowledgement for a specific pipe.
* @param handle Pointer to the nRF24 handle structure.
* @param pipe Pipe number (0-5).
* @param enable Enable (1) or disable (0) auto-ack.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetAutoAck(NRF24_Handle_t *handle, uint8_t pipe, uint8_t enable) {
    if(handle == NULL || pipe > 0x05) {
        return HAL_ERROR; // Invalid handle
    }

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
HAL_StatusTypeDef NRF24_EnablePipe(NRF24_Handle_t *handle, uint8_t pipe, uint8_t enable) {
    if(handle == NULL || pipe > 0x05) {
        return HAL_ERROR; // Invalid handle
    }
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
HAL_StatusTypeDef NRF24_SetRXAddress(NRF24_Handle_t *handle, uint8_t pipe, const uint8_t *addr, uint8_t len) {
    if (handle == NULL || pipe > 0x05 || len > 0x05 || len == 0) {
        return HAL_ERROR;
    }

    if (pipe < 0x02) {
        // Pipe 0 and 1 support full 3-5 byte addresses
        return NRF24_WriteRegs(handle, NRF24_REG_RX_ADDR_P0 + pipe, (uint8_t *)addr, len);
    } else {
        // Pipes 2-5 only use LSByte; MSBytes shared with RX_ADDR_P1
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
HAL_StatusTypeDef NRF24_SetTXAddress(NRF24_Handle_t *handle, const uint8_t *addr, uint8_t len) {
    if (handle == NULL || len > 0x05) {
        return HAL_ERROR;
    }
    return NRF24_WriteRegs(handle, NRF24_REG_TX_ADDR, (uint8_t *)addr, len);
}

/**
* @brief Sets the payload size for a specific pipe.
* @param handle Pointer to the nRF24 handle structure.
* @param pipe Pipe number (0-5).
* @param size Payload size (1-32 bytes).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetPayloadSize(NRF24_Handle_t *handle, uint8_t pipe, uint8_t size) {
    if (handle == NULL || pipe > 0x05 || size > 0x20) {
        return HAL_ERROR;
    }
    return NRF24_WriteReg(handle, NRF24_REG_RX_PW_P0 + pipe, size);
}

/**
* @brief Enables or disables dynamic payload length for a specific pipe.
* @param handle Pointer to the nRF24 handle structure.
* @param pipe Pipe number (0-5).
* @param enable Enable (1) or disable (0) dynamic payload.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_EnableDynamicPayload(NRF24_Handle_t *handle, uint8_t pipe, uint8_t enable) {
    if (handle == NULL || pipe > 0x05) {
        return HAL_ERROR;
    }

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

    // Update EN_DPL in FEATURE register based on whether any pipe uses DPL
    uint8_t feature;
    status = NRF24_ReadReg(handle, NRF24_REG_FEATURE, &feature);
    if (status != HAL_OK) return status;

    if (dynpd) {
        feature |= NRF24_FEATURE_EN_DPL;
    } else {
        feature &= ~NRF24_FEATURE_EN_DPL;
    }

    return NRF24_WriteReg(handle, NRF24_REG_FEATURE, feature);
}

/**
* @brief Checks if data is available in any RX pipe.
* @param handle Pointer to the nRF24 handle structure.
* @param pipe Pointer to store the pipe number where data is available.
* @return 1 if data is available, 0 otherwise.
*/
uint8_t NRF24_IsDataAvailable(NRF24_Handle_t *handle, uint8_t *pipe) {
    if(handle == NULL || pipe == NULL) {
        return 0; // Invalid handle
    }
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
HAL_StatusTypeDef NRF24_ReadPayload(NRF24_Handle_t *handle, uint8_t *buf, uint8_t len) {
    if(handle == NULL || buf == NULL) {
        return HAL_ERROR; // Invalid handle
    }
    return read_payload(handle, NRF24_CMD_R_RX_PAYLOAD, buf, len);
}

/**
* @brief Writes the payload data to the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
* @param buf Buffer containing the payload data.
* @param len Length of the payload to write.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_WritePayload(NRF24_Handle_t *handle, const uint8_t *buf, uint8_t len) {
    if(handle == NULL || buf == NULL || len == 0 || len > NRF24_MAX_PAYLOAD_SIZE) {
        return HAL_ERROR;
    }
    return write_payload(handle, NRF24_CMD_W_TX_PAYLOAD, buf, len);
}

/**
* @brief Flushes the TX FIFO.
* @param handle Pointer to the nRF24 handle structure.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_FlushTX(NRF24_Handle_t *handle) {
    if(handle == NULL) {
        return HAL_ERROR; // Invalid handle
    }
    return command(handle, NRF24_CMD_FLUSH_TX, NULL);
}

/**
* @brief Flushes the RX FIFO.
* @param handle Pointer to the nRF24 handle structure.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_FlushRX(NRF24_Handle_t *handle) {
    if(handle == NULL) {
        return HAL_ERROR; // Invalid handle
    }
    return command(handle, NRF24_CMD_FLUSH_RX, NULL);
}

/**
* @brief Handles IRQ events from the nRF24L01+.
* @param handle Pointer to the nRF24 handle structure.
*/
void NRF24_IRQ_Handler(NRF24_Handle_t *handle) {
    if(handle == NULL) {
        return; // Invalid handle
    }
    uint8_t status = NRF24_GetStatus(handle);

    // Use bitwise checks - multiple IRQ flags can be set simultaneously
    if (status & NRF24_STATUS_MAX_RT) {
        // Max retransmits reached - payload is NOT removed from TX FIFO
        // Per datasheet: MAX_RT must be cleared to enable further communication
        NRF24_FlushTX(handle);
    }

    if (status & NRF24_STATUS_TX_DS) {
        // Data sent successfully (ACK received if auto-ack enabled)
    }

    if (status & NRF24_STATUS_RX_DR) {
        // Data received and ready in RX FIFO
    }

    // Clear all asserted IRQ flags by writing 1s to them
    NRF24_ClearIRQ(handle, status & NRF24_STATUS_IRQ_MASK);
}

/**
* @brief Sets the CRC mode.
* @param handle Pointer to the nRF24 handle structure.
* @param crc CRC mode (OFF, 1B, 2B).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_SetCRC(NRF24_Handle_t *handle, NRF24_CRC_t crc) {
    uint8_t config;
    HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_CONFIG, &config);
    if (status != HAL_OK) return status;

    config &= ~(NRF24_CONFIG_EN_CRC | NRF24_CONFIG_CRCO);

    if (crc != NRF24_CRC_OFF) {
        config |= NRF24_CONFIG_EN_CRC;
    } 

    if (crc == NRF24_CRC_2B) {
        config |= NRF24_CONFIG_CRCO;
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
HAL_StatusTypeDef NRF24_SetAutoRetr(NRF24_Handle_t *handle, uint8_t ard, uint8_t arc) {
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
HAL_StatusTypeDef NRF24_EnableDynAck(NRF24_Handle_t *handle, uint8_t enable) {
    uint8_t feature;

    HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_FEATURE, &feature);
    if (status != HAL_OK) return status;

    if (enable) {
        feature |= NRF24_FEATURE_EN_DYN_ACK;
    } else {
        feature &= ~NRF24_FEATURE_EN_DYN_ACK;
    }

    return NRF24_WriteReg(handle, NRF24_REG_FEATURE, feature);
}

/**
* @brief Enables or disables ACK payload.
* @param handle Pointer to the nRF24 handle structure.
* @param enable Enable (1) or disable (0).
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_EnableAckPay(NRF24_Handle_t *handle, uint8_t enable) {
    uint8_t feature;
    HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_FEATURE, &feature);

    if (status != HAL_OK) return status;
    if (enable) {
        feature |= NRF24_FEATURE_EN_ACK_PAY;
    } else {
        feature &= ~NRF24_FEATURE_EN_ACK_PAY;
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
HAL_StatusTypeDef NRF24_WritePayloadNoAck(NRF24_Handle_t *handle, const uint8_t *buf, uint8_t len) {
    if(handle == NULL || buf == NULL || len == 0 || len > NRF24_MAX_PAYLOAD_SIZE) {
        return HAL_ERROR;
    }

    // EN_DYN_ACK in FEATURE register must be set before using this command.
    // Call NRF24_EnableDynAck(handle, 1) once during setup, not per-packet.
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
HAL_StatusTypeDef NRF24_WriteAckPayload(NRF24_Handle_t *handle, uint8_t pipe, const uint8_t *buf, uint8_t len) {
    if (pipe > 5 || handle == NULL || buf == NULL || len == 0 || len > NRF24_MAX_PAYLOAD_SIZE){
        return HAL_ERROR;
    }

    // EN_ACK_PAY in FEATURE register must be set before using this command.
    // Call NRF24_EnableAckPay(handle, 1) once during setup, not per-packet.
    // Also requires DPL enabled on pipe 0 for PTX and PRX per datasheet.
    return write_payload(handle, NRF24_CMD_W_ACK_PAYLOAD | pipe, buf, len);
}

/**
* @brief Sends the ACTIVATE command (0x50 + 0x73) to enable FEATURE register features.
*
* Per datasheet: "This write command followed by data 0x73 activates the following features:
* R_RX_PL_WID, W_ACK_PAYLOAD, W_TX_PAYLOAD_NOACK.
* A new ACTIVATE command with the same data deactivates them again."
* Required for nRF24L01 (original). On nRF24L01+ this is typically a NOP.
*
* @param handle Pointer to the nRF24 handle structure.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_Activate(NRF24_Handle_t *handle) {
    if (handle == NULL) return HAL_ERROR;

    uint8_t tx[2] = {NRF24_CMD_ACTIVATE, 0x73};
    uint8_t rx[2];
    csn_low(handle);
    HAL_StatusTypeDef status = spi_transfer(handle, tx, rx, 2);
    csn_high(handle);
    return status;
}

/**
* @brief Reads the payload width of the top payload in the RX FIFO.
*
* Per datasheet: "Read RX-payload width for the top R_RX_PAYLOAD in the RX FIFO."
* Used with dynamic payload length to determine how many bytes to read.
* Note: If the returned value is > 32, the RX FIFO should be flushed (corrupted).
*
* @param handle Pointer to the nRF24 handle structure.
* @return Payload width in bytes (0-32), or 0 on error.
*/
uint8_t NRF24_ReadDynamicPayloadWidth(NRF24_Handle_t *handle) {
    if (handle == NULL) return 0;

    uint8_t tx[2] = {NRF24_CMD_R_RX_PL_WID, NRF24_CMD_NOP};
    uint8_t rx[2];
    csn_low(handle);
    HAL_StatusTypeDef status = spi_transfer(handle, tx, rx, 2);
    csn_high(handle);

    if (status != HAL_OK) return 0;

    uint8_t width = rx[1];
    if (width > NRF24_MAX_PAYLOAD_SIZE) {
        // Corrupted data per datasheet - flush RX FIFO
        NRF24_FlushRX(handle);
        return 0;
    }
    return width;
}

/**
* @brief Reads the FIFO_STATUS register.
* @param handle Pointer to the nRF24 handle structure.
* @return FIFO status register value, or 0 on error.
*/
uint8_t NRF24_GetFIFOStatus(NRF24_Handle_t *handle) {
    if (handle == NULL) return 0;
    uint8_t fifo_status;
    if (NRF24_ReadReg(handle, NRF24_REG_FIFO_STATUS, &fifo_status) != HAL_OK) return 0;
    return fifo_status;
}

/**
* @brief Powers up the nRF24L01+ and waits for Tpd2stby.
* @param handle Pointer to the nRF24 handle structure.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_PowerUp(NRF24_Handle_t *handle) {
    if (handle == NULL) return HAL_ERROR;
    uint8_t config;
    HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_CONFIG, &config);
    if (status != HAL_OK) return status;
    if (!(config & NRF24_CONFIG_PWR_UP)) {
        config |= NRF24_CONFIG_PWR_UP;
        status = NRF24_WriteReg(handle, NRF24_REG_CONFIG, config);
        if (status != HAL_OK) return status;
        handle->delay_us(1500); // Tpd2stby = 1.5ms
    }
    return HAL_OK;
}

/**
* @brief Powers down the nRF24L01+ to minimize current consumption.
* @param handle Pointer to the nRF24 handle structure.
* @return HAL status.
*/
HAL_StatusTypeDef NRF24_PowerDown(NRF24_Handle_t *handle) {
    if (handle == NULL) return HAL_ERROR;
    ce_low(handle);
    uint8_t config;
    HAL_StatusTypeDef status = NRF24_ReadReg(handle, NRF24_REG_CONFIG, &config);
    if (status != HAL_OK) return status;
    config &= ~NRF24_CONFIG_PWR_UP;
    return NRF24_WriteReg(handle, NRF24_REG_CONFIG, config);
}

/**
* @brief Reads the OBSERVE_TX register.
*
* Per datasheet: PLOS_CNT (bits 7:4) = count of lost packets (reset by writing RF_CH).
* ARC_CNT (bits 3:0) = count of retransmits for current transaction.
*
* @param handle Pointer to the nRF24 handle structure.
* @return OBSERVE_TX register value, or 0 on error.
*/
uint8_t NRF24_GetObserveTX(NRF24_Handle_t *handle) {
    if (handle == NULL) return 0;
    uint8_t val;
    if (NRF24_ReadReg(handle, NRF24_REG_OBSERVE_TX, &val) != HAL_OK) return 0;
    return val;
}

/**
* @brief Reads the Carrier Detect (CD) / Received Power Detector (RPD) register.
*
* Per datasheet (register 0x09): CD bit indicates carrier detected on the current channel.
*
* @param handle Pointer to the nRF24 handle structure.
* @return 1 if carrier detected, 0 otherwise.
*/
uint8_t NRF24_GetCarrierDetect(NRF24_Handle_t *handle) {
    if (handle == NULL) return 0;
    uint8_t val;
    if (NRF24_ReadReg(handle, NRF24_REG_RPD, &val) != HAL_OK) return 0;
    return val & 0x01;
}