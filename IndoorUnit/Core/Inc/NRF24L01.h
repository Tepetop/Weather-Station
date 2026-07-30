/**
 * @file    NRF24L01.h
 * @brief   nRF24L01+ 2.4 GHz transceiver driver (SPI)
 * @details Public types and API for Nordic nRF24L01(+) over STM32 HAL SPI.
 *          Supports register access, TX/RX payloads, IRQ handling, and radio
 *          configuration (channel, data rate, PA level, pipes, CRC, dynamic
 *          payload features).
 */

#ifndef NRF24L01_H
#define NRF24L01_H

#include "stm32f1xx_hal.h"

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/** @brief nRF24L01 register addresses */
typedef enum {
    NRF24_REG_CONFIG      = 0x00,
    NRF24_REG_EN_AA       = 0x01,
    NRF24_REG_EN_RXADDR   = 0x02,
    NRF24_REG_SETUP_AW    = 0x03,
    NRF24_REG_SETUP_RETR  = 0x04,
    NRF24_REG_RF_CH       = 0x05,
    NRF24_REG_RF_SETUP    = 0x06,
    NRF24_REG_STATUS      = 0x07,
    NRF24_REG_OBSERVE_TX  = 0x08,
    NRF24_REG_RPD         = 0x09,
    NRF24_REG_RX_ADDR_P0  = 0x0A,
    NRF24_REG_RX_ADDR_P1  = 0x0B,
    NRF24_REG_RX_ADDR_P2  = 0x0C,
    NRF24_REG_RX_ADDR_P3  = 0x0D,
    NRF24_REG_RX_ADDR_P4  = 0x0E,
    NRF24_REG_RX_ADDR_P5  = 0x0F,
    NRF24_REG_TX_ADDR     = 0x10,
    NRF24_REG_RX_PW_P0    = 0x11,
    NRF24_REG_RX_PW_P1    = 0x12,
    NRF24_REG_RX_PW_P2    = 0x13,
    NRF24_REG_RX_PW_P3    = 0x14,
    NRF24_REG_RX_PW_P4    = 0x15,
    NRF24_REG_RX_PW_P5    = 0x16,
    NRF24_REG_FIFO_STATUS = 0x17,
    NRF24_REG_DYNPD       = 0x1C,
    NRF24_REG_FEATURE     = 0x1D
} NRF24_Register_t;

/** @brief SPI command opcodes */
typedef enum {
    NRF24_CMD_R_REGISTER          = 0x00,
    NRF24_CMD_W_REGISTER          = 0x20,
    NRF24_CMD_R_RX_PAYLOAD        = 0x61,
    NRF24_CMD_W_TX_PAYLOAD        = 0xA0,
    NRF24_CMD_FLUSH_TX            = 0xE1,
    NRF24_CMD_FLUSH_RX            = 0xE2,
    NRF24_CMD_REUSE_TX_PL         = 0xE3,
    NRF24_CMD_ACTIVATE            = 0x50,
    NRF24_CMD_R_RX_PL_WID         = 0x60,
    NRF24_CMD_W_ACK_PAYLOAD       = 0xA8,
    NRF24_CMD_W_TX_PAYLOAD_NOACK  = 0xB0,
    NRF24_CMD_NOP                 = 0xFF
} NRF24_Command_t;

/** @brief CONFIG register bit masks */
typedef enum {
    NRF24_CONFIG_PRIM_RX     = 0x01,
    NRF24_CONFIG_PWR_UP      = 0x02,
    NRF24_CONFIG_CRCO        = 0x04,
    NRF24_CONFIG_EN_CRC      = 0x08,
    NRF24_CONFIG_MASK_MAX_RT = 0x10,
    NRF24_CONFIG_MASK_TX_DS  = 0x20,
    NRF24_CONFIG_MASK_RX_DR  = 0x40
} NRF24_Config_t;

/** @brief STATUS register bit masks */
typedef enum {
    NRF24_STATUS_TX_FULL      = 0x01,  /**< TX FIFO full flag */
    NRF24_STATUS_RX_P_NO_MASK = 0x0E,  /**< Bits 3:1 – data pipe number */
    NRF24_STATUS_MAX_RT       = 0x10,  /**< Max retransmits reached */
    NRF24_STATUS_TX_DS        = 0x20,  /**< Data sent (TX DS) */
    NRF24_STATUS_RX_DR        = 0x40,  /**< Data received (RX DR) */
    NRF24_STATUS_IRQ_MASK     = 0x70   /**< All IRQ flags combined */
} NRF24_Status_t;

/** @brief FIFO_STATUS register bit masks */
typedef enum {
    NRF24_FIFO_RX_EMPTY = 0x01,
    NRF24_FIFO_RX_FULL  = 0x02,
    NRF24_FIFO_TX_EMPTY = 0x10,
    NRF24_FIFO_TX_FULL  = 0x20,
    NRF24_FIFO_TX_REUSE = 0x40
} NRF24_FIFO_Status_t;

/** @brief FEATURE register bit masks */
typedef enum {
    NRF24_FEATURE_EN_DYN_ACK = 0x01,
    NRF24_FEATURE_EN_ACK_PAY = 0x02,
    NRF24_FEATURE_EN_DPL     = 0x04
} NRF24_Feature_t;

/** @brief Maximum payload size per datasheet */
#define NRF24_MAX_PAYLOAD_SIZE  32
/** @brief Maximum valid RF channel number */
#define NRF24_MAX_CHANNEL       125
/** @brief Default RF channel used during NRF24_Init */
#define NRF24_DEFAULT_CHANNEL   76

/**
 * @brief Air data rate settings
 * @note 250 kbps is available on nRF24L01+ only (not original nRF24L01)
 */
typedef enum {
    NRF24_DR_250KBPS = 0x20,  /**< RF_DR_LOW=1, RF_DR_HIGH=0 (nRF24L01+ only) */
    NRF24_DR_1MBPS   = 0x00,  /**< RF_DR_LOW=0, RF_DR_HIGH=0 */
    NRF24_DR_2MBPS   = 0x08   /**< RF_DR_LOW=0, RF_DR_HIGH=1 */
} NRF24_DataRate_t;

/** @brief Power amplifier output levels */
typedef enum {
    NRF24_PA_MIN  = 0x00,  /**< -18 dBm */
    NRF24_PA_LOW  = 0x02,  /**< -12 dBm */
    NRF24_PA_HIGH = 0x04,  /**<  -6 dBm */
    NRF24_PA_MAX  = 0x06   /**<   0 dBm */
} NRF24_PALevel_t;

/** @brief Address width settings */
typedef enum {
    NRF24_AW_3 = 0x01,
    NRF24_AW_4 = 0x02,
    NRF24_AW_5 = 0x03
} NRF24_AddrWidth_t;

/** @brief Radio operating modes */
typedef enum {
    NRF24_MODE_POWER_DOWN = 0x00,
    NRF24_MODE_STANDBY    = 0x01,
    NRF24_MODE_RX         = 0x02,
    NRF24_MODE_TX         = 0x03
} NRF24_Mode_t;

/** @brief CRC configuration */
typedef enum {
    NRF24_CRC_OFF = 0x00,
    NRF24_CRC_1B  = 0x01,
    NRF24_CRC_2B  = 0x02
} NRF24_CRC_t;

/** @brief IRQ handler return flags */
typedef enum {
    NRF24_IRQ_NONE   = 0x00,
    NRF24_IRQ_MAX_RT = 0x01,
    NRF24_IRQ_TX_DS  = 0x02,
    NRF24_IRQ_RX_DR  = 0x04
} NRF24_IRQ_Event_t;

/**
 * @brief nRF24L01 device handle bound to SPI and GPIO pins
 */
typedef struct {
    SPI_HandleTypeDef *hspi;       /**< HAL SPI handle */
    GPIO_TypeDef      *csn_port;   /**< CSN (chip select) GPIO port */
    uint16_t           csn_pin;    /**< CSN GPIO pin */
    GPIO_TypeDef      *ce_port;    /**< CE (chip enable) GPIO port */
    uint16_t           ce_pin;     /**< CE GPIO pin */
    GPIO_TypeDef      *irq_port;   /**< IRQ GPIO port */
    uint16_t           irq_pin;    /**< IRQ GPIO pin */
    void (*delay_us)(uint32_t);    /**< Microsecond delay callback */
} NRF24_Handle_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief   Initializes the nRF24L01+ with default radio settings
 * @param   handle    Device handle to populate
 * @param   hspi      HAL SPI handle
 * @param   csn_port  CSN GPIO port
 * @param   csn_pin   CSN GPIO pin
 * @param   ce_port   CE GPIO port
 * @param   ce_pin    CE GPIO pin
 * @param   irq_port  IRQ GPIO port
 * @param   irq_pin   IRQ GPIO pin
 * @param   delay_us  Microsecond delay function (required for timing)
 * @retval  HAL_OK     Initialization successful
 * @retval  HAL_ERROR  Invalid parameter or register write failed
 */
HAL_StatusTypeDef NRF24_Init(NRF24_Handle_t *handle, SPI_HandleTypeDef *hspi, GPIO_TypeDef *csn_port, uint16_t csn_pin,
                             GPIO_TypeDef *ce_port, uint16_t ce_pin, GPIO_TypeDef *irq_port, uint16_t irq_pin,
                             void (*delay_us)(uint32_t));

/**
 * @brief   Reads a single register
 * @param   handle  Device handle
 * @param   reg     Register address
 * @param   value   Output: register value
 * @retval  HAL_OK     Read successful
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_ReadReg(NRF24_Handle_t *handle, uint8_t reg, uint8_t *value);

/**
 * @brief   Writes a single register
 * @param   handle  Device handle
 * @param   reg     Register address
 * @param   value   Value to write
 * @retval  HAL_OK     Write successful
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_WriteReg(NRF24_Handle_t *handle, uint8_t reg, uint8_t value);

/**
 * @brief   Reads multiple consecutive registers
 * @param   handle  Device handle
 * @param   reg     Starting register address
 * @param   buf     Output buffer
 * @param   len     Number of bytes to read (1–5)
 * @retval  HAL_OK     Read successful
 * @retval  HAL_ERROR  Invalid parameters or SPI error
 */
HAL_StatusTypeDef NRF24_ReadRegs(NRF24_Handle_t *handle, uint8_t reg, uint8_t *buf, uint8_t len);

/**
 * @brief   Writes multiple consecutive registers
 * @param   handle  Device handle
 * @param   reg     Starting register address
 * @param   buf     Data to write
 * @param   len     Number of bytes to write (1–5)
 * @retval  HAL_OK     Write successful
 * @retval  HAL_ERROR  Invalid parameters or SPI error
 */
HAL_StatusTypeDef NRF24_WriteRegs(NRF24_Handle_t *handle, uint8_t reg, uint8_t *buf, uint8_t len);

/**
 * @brief   Reads the STATUS register via NOP command
 * @param   handle  Device handle
 * @retval  STATUS register value, or HAL_ERROR on invalid handle
 */
uint8_t NRF24_GetStatus(NRF24_Handle_t *handle);

/**
 * @brief   Clears IRQ flags in the STATUS register
 * @param   handle    Device handle
 * @param   irq_flag  Bitmask of flags to clear (NRF24_STATUS_*)
 * @retval  HAL_OK     Clear successful
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_ClearIRQ(NRF24_Handle_t *handle, uint8_t irq_flag);

/**
 * @brief   Sets radio operating mode
 * @param   handle  Device handle
 * @param   mode    Desired mode (power down, standby, RX, TX)
 * @retval  HAL_OK     Mode set successfully
 * @retval  HAL_ERROR  Invalid handle or register access failed
 */
HAL_StatusTypeDef NRF24_SetMode(NRF24_Handle_t *handle, NRF24_Mode_t mode);

/**
 * @brief   Sets RF channel (0–125)
 * @warning Requires CE low (standby or power down)
 * @param   handle   Device handle
 * @param   channel  RF channel number
 * @retval  HAL_OK     Channel set successfully
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_SetChannel(NRF24_Handle_t *handle, uint8_t channel);

/**
 * @brief   Sets air data rate
 * @warning Requires CE low (standby or power down)
 * @param   handle  Device handle
 * @param   rate    Data rate setting
 * @retval  HAL_OK     Rate set successfully
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_SetDataRate(NRF24_Handle_t *handle, NRF24_DataRate_t rate);

/**
 * @brief   Sets power amplifier level
 * @warning Requires CE low (standby or power down)
 * @param   handle  Device handle
 * @param   level   PA level setting
 * @retval  HAL_OK     Level set successfully
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_SetPALevel(NRF24_Handle_t *handle, NRF24_PALevel_t level);

/**
 * @brief   Sets RX/TX address width
 * @warning Requires CE low (standby or power down)
 * @param   handle  Device handle
 * @param   width   Address width (3, 4, or 5 bytes)
 * @retval  HAL_OK     Width set successfully
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_SetAddressWidth(NRF24_Handle_t *handle, NRF24_AddrWidth_t width);

/**
 * @brief   Enables or disables auto-acknowledgement for a pipe
 * @warning Requires CE low (standby or power down)
 * @param   handle  Device handle
 * @param   pipe    Pipe number (0–5)
 * @param   enable  1 to enable, 0 to disable
 * @retval  HAL_OK     Setting applied
 * @retval  HAL_ERROR  Invalid handle, pipe, or SPI error
 */
HAL_StatusTypeDef NRF24_SetAutoAck(NRF24_Handle_t *handle, uint8_t pipe, uint8_t enable);

/**
 * @brief   Enables or disables an RX data pipe
 * @warning Requires CE low (standby or power down)
 * @param   handle  Device handle
 * @param   pipe    Pipe number (0–5)
 * @param   enable  1 to enable, 0 to disable
 * @retval  HAL_OK     Setting applied
 * @retval  HAL_ERROR  Invalid handle, pipe, or SPI error
 */
HAL_StatusTypeDef NRF24_EnablePipe(NRF24_Handle_t *handle, uint8_t pipe, uint8_t enable);

/**
 * @brief   Sets RX address for a data pipe
 * @warning Requires CE low (standby or power down)
 * @param   handle  Device handle
 * @param   pipe    Pipe number (0–5)
 * @param   addr    Address bytes (LSByte first)
 * @param   len     Address length (1–5 bytes)
 * @retval  HAL_OK     Address set successfully
 * @retval  HAL_ERROR  Invalid parameters or SPI error
 */
HAL_StatusTypeDef NRF24_SetRXAddress(NRF24_Handle_t *handle, uint8_t pipe, const uint8_t *addr, uint8_t len);

/**
 * @brief   Sets TX address
 * @warning Requires CE low (standby or power down)
 * @param   handle  Device handle
 * @param   addr    Address bytes (LSByte first)
 * @param   len     Address length (1–5 bytes)
 * @retval  HAL_OK     Address set successfully
 * @retval  HAL_ERROR  Invalid parameters or SPI error
 */
HAL_StatusTypeDef NRF24_SetTXAddress(NRF24_Handle_t *handle, const uint8_t *addr, uint8_t len);

/**
 * @brief   Sets fixed payload width for an RX pipe
 * @warning Requires CE low (standby or power down)
 * @param   handle  Device handle
 * @param   pipe    Pipe number (0–5)
 * @param   size    Payload size (1–32 bytes)
 * @retval  HAL_OK     Size set successfully
 * @retval  HAL_ERROR  Invalid parameters or SPI error
 */
HAL_StatusTypeDef NRF24_SetPayloadSize(NRF24_Handle_t *handle, uint8_t pipe, uint8_t size);

/**
 * @brief   Enables or disables dynamic payload length for a pipe
 * @warning Requires CE low (standby or power down)
 * @param   handle  Device handle
 * @param   pipe    Pipe number (0–5)
 * @param   enable  1 to enable, 0 to disable
 * @retval  HAL_OK     Setting applied
 * @retval  HAL_ERROR  Invalid parameters or SPI error
 */
HAL_StatusTypeDef NRF24_EnableDynamicPayload(NRF24_Handle_t *handle, uint8_t pipe, uint8_t enable);

/**
 * @brief   Checks whether RX data is available
 * @param   handle  Device handle
 * @param   pipe    Output: pipe number with pending data
 * @retval  1 if data available, 0 otherwise
 */
uint8_t NRF24_IsDataAvailable(NRF24_Handle_t *handle, uint8_t *pipe);

/**
 * @brief   Reads payload from RX FIFO
 * @param   handle  Device handle
 * @param   buf     Output buffer
 * @param   len     Number of bytes to read
 * @retval  HAL_OK     Read successful
 * @retval  HAL_ERROR  Invalid parameters or SPI error
 */
HAL_StatusTypeDef NRF24_ReadPayload(NRF24_Handle_t *handle, uint8_t *buf, uint8_t len);

/**
 * @brief   Writes payload to TX FIFO
 * @param   handle  Device handle
 * @param   buf     Payload data
 * @param   len     Payload length (1–32 bytes)
 * @retval  HAL_OK     Write successful
 * @retval  HAL_ERROR  Invalid parameters or SPI error
 */
HAL_StatusTypeDef NRF24_WritePayload(NRF24_Handle_t *handle, const uint8_t *buf, uint8_t len);

/**
 * @brief   Flushes the TX FIFO
 * @param   handle  Device handle
 * @retval  HAL_OK     Flush successful
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_FlushTX(NRF24_Handle_t *handle);

/**
 * @brief   Flushes the RX FIFO
 * @param   handle  Device handle
 * @retval  HAL_OK     Flush successful
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_FlushRX(NRF24_Handle_t *handle);

/**
 * @brief   Processes IRQ status flags
 * @param   handle  Device handle
 * @retval  Bitmask of asserted events (NRF24_IRQ_Event_t)
 */
uint8_t NRF24_IRQ_Handler(NRF24_Handle_t *handle);

/**
 * @brief   Sets CRC mode
 * @warning Requires CE low (standby or power down)
 * @param   handle  Device handle
 * @param   crc     CRC setting (off, 1-byte, 2-byte)
 * @retval  HAL_OK     CRC configured
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_SetCRC(NRF24_Handle_t *handle, NRF24_CRC_t crc);

/**
 * @brief   Sets auto-retransmit delay and count
 * @warning Requires CE low (standby or power down)
 * @param   handle  Device handle
 * @param   ard     Auto retransmit delay code (0–15)
 * @param   arc     Auto retransmit count (0–15)
 * @retval  HAL_OK     Setting applied
 * @retval  HAL_ERROR  Invalid parameters or SPI error
 */
HAL_StatusTypeDef NRF24_SetAutoRetr(NRF24_Handle_t *handle, uint8_t ard, uint8_t arc);

/**
 * @brief   Enables or disables dynamic ACK (no-ACK per packet)
 * @param   handle  Device handle
 * @param   enable  1 to enable, 0 to disable
 * @retval  HAL_OK     Setting applied
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_EnableDynAck(NRF24_Handle_t *handle, uint8_t enable);

/**
 * @brief   Enables or disables ACK payload feature
 * @param   handle  Device handle
 * @param   enable  1 to enable, 0 to disable
 * @retval  HAL_OK     Setting applied
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_EnableAckPay(NRF24_Handle_t *handle, uint8_t enable);

/**
 * @brief   Writes TX payload without requesting ACK
 * @param   handle  Device handle
 * @param   buf     Payload data
 * @param   len     Payload length (1–32 bytes)
 * @retval  HAL_OK     Write successful
 * @retval  HAL_ERROR  Invalid parameters or SPI error
 * @details Requires NRF24_EnableDynAck(handle, 1) during setup.
 */
HAL_StatusTypeDef NRF24_WritePayloadNoAck(NRF24_Handle_t *handle, const uint8_t *buf, uint8_t len);

/**
 * @brief   Writes ACK payload for a pipe
 * @param   handle  Device handle
 * @param   pipe    Pipe number (0–5)
 * @param   buf     Payload data
 * @param   len     Payload length (1–32 bytes)
 * @retval  HAL_OK     Write successful
 * @retval  HAL_ERROR  Invalid parameters or SPI error
 * @details Requires NRF24_EnableAckPay(handle, 1) during setup.
 */
HAL_StatusTypeDef NRF24_WriteAckPayload(NRF24_Handle_t *handle, uint8_t pipe, const uint8_t *buf, uint8_t len);

/**
 * @brief   Sends ACTIVATE command (0x50 + 0x73) for legacy chips
 * @param   handle  Device handle
 * @retval  HAL_OK     Command sent
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_Activate(NRF24_Handle_t *handle);

/**
 * @brief   Ensures FEATURE register is writable (idempotent)
 * @param   handle  Device handle
 * @retval  HAL_OK     FEATURE register accessible
 * @retval  HAL_ERROR  Invalid handle or activation failed
 */
HAL_StatusTypeDef NRF24_EnsureFeatureRegisterActive(NRF24_Handle_t *handle);

/**
 * @brief   Checks whether register writes are safe (CE low)
 * @param   handle  Device handle
 * @retval  1 if safe (standby/power down), 0 if CE high (active RX/TX)
 */
uint8_t NRF24_IsSafeToConfigure(NRF24_Handle_t *handle);

/**
 * @brief   Reads dynamic payload width of top RX FIFO entry
 * @param   handle  Device handle
 * @retval  Payload width (0–32), or 0 on error/corruption
 */
uint8_t NRF24_ReadDynamicPayloadWidth(NRF24_Handle_t *handle);

/**
 * @brief   Reads FIFO_STATUS register
 * @param   handle  Device handle
 * @retval  FIFO status byte, or 0 on error
 */
uint8_t NRF24_GetFIFOStatus(NRF24_Handle_t *handle);

/**
 * @brief   Powers up the radio and waits Tpd2stby
 * @param   handle  Device handle
 * @retval  HAL_OK     Radio powered up
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_PowerUp(NRF24_Handle_t *handle);

/**
 * @brief   Powers down the radio (CE low, PWR_UP cleared)
 * @param   handle  Device handle
 * @retval  HAL_OK     Radio powered down
 * @retval  HAL_ERROR  Invalid handle or SPI error
 */
HAL_StatusTypeDef NRF24_PowerDown(NRF24_Handle_t *handle);

/**
 * @brief   Reads OBSERVE_TX register
 * @param   handle  Device handle
 * @retval  OBSERVE_TX value, or 0 on error
 */
uint8_t NRF24_GetObserveTX(NRF24_Handle_t *handle);

/**
 * @brief   Reads carrier detect (RPD) status
 * @param   handle  Device handle
 * @retval  1 if carrier detected, 0 otherwise
 */
uint8_t NRF24_GetCarrierDetect(NRF24_Handle_t *handle);

/**
 * @brief   Verifies device presence on SPI
 * @param   handle  Device handle
 * @retval  HAL_OK     Device responds correctly
 * @retval  HAL_ERROR  No response or readback mismatch
 */
HAL_StatusTypeDef NRF24_IsPresent(NRF24_Handle_t *handle);

#endif
