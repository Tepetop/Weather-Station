/**
 * @file    si7021.h
 * @brief   Si7021 temperature and humidity sensor driver (I2C)
 * @details Public types and API for Silicon Labs Si7021 over STM32 HAL I2C.
 *          Measurement results are stored in the device handle data field.
 */

#ifndef SI7021_H
#define SI7021_H

#include "main.h"

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Latest measurement results and cached configuration
 */
typedef struct {
    float humidity;        /**< Relative humidity in %RH */
    float temperature;     /**< Temperature in °C */
    uint8_t resolution;    /**< Cached resolution setting (Si7021_Resolution_t) */
    uint8_t heater_current; /**< Cached heater current in mA */
} Si7021_Measurement_t;

/**
 * @brief Si7021 device handle
 */
typedef struct {
    I2C_HandleTypeDef *hi2c;   /**< HAL I2C handle */
    uint8_t address;           /**< 8-bit HAL address (7-bit << 1), typically 0x40 */
    uint8_t firmware;          /**< Firmware revision read at init */
    Si7021_Measurement_t data; /**< Latest measurement and cached settings */
} Si7021_t;

/**
 * @brief Humidity and temperature ADC resolution settings
 */
typedef enum {
    SI7021_RESOLUTION_RH12_TEMP14 = 0, /**< RH 12-bit, temperature 14-bit */
    SI7021_RESOLUTION_RH8_TEMP12  = 1, /**< RH 8-bit, temperature 12-bit */
    SI7021_RESOLUTION_RH10_TEMP13 = 2, /**< RH 10-bit, temperature 13-bit */
    SI7021_RESOLUTION_RH11_TEMP11 = 3, /**< RH 11-bit, temperature 11-bit */
} Si7021_Resolution_t;

/**
 * @brief I2C command codes (datasheet Table 11)
 */
typedef enum {
    SI7021_CMD_MEASURE_RH_HOLD     = 0xE5,   /**< Measure RH, hold master mode */
    SI7021_CMD_MEASURE_RH_NOHOLD   = 0xF5,   /**< Measure RH, no-hold master mode */
    SI7021_CMD_MEASURE_TEMP_HOLD   = 0xE3,   /**< Measure temperature, hold master mode */
    SI7021_CMD_MEASURE_TEMP_NOHOLD = 0xF3,   /**< Measure temperature, no-hold master mode */
    SI7021_CMD_READ_TEMP_PREV_RH   = 0xE0,   /**< Read temperature from previous RH measurement */
    SI7021_CMD_RESET               = 0xFE,   /**< Software reset */
    SI7021_CMD_WRITE_USER_REG1     = 0xE6,   /**< Write RH/T user register 1 */
    SI7021_CMD_READ_USER_REG1      = 0xE7,   /**< Read RH/T user register 1 */
    SI7021_CMD_WRITE_HEATER_REG    = 0x51,   /**< Write heater control register */
    SI7021_CMD_READ_HEATER_REG     = 0x11,   /**< Read heater control register */
    SI7021_CMD_READ_EID_1ST        = 0xFC0F, /**< Read electronic ID (first byte) */
    SI7021_CMD_READ_EID_2ND        = 0xFCC9, /**< Read electronic ID (second byte) */
    SI7021_CMD_READ_FIRMWARE       = 0x84B8, /**< Read firmware revision */
} Si7021_Command_t;

/**
 * @brief Heater current calculation constants (VDD = 3.3 V)
 * @details Heater register D3:fChan0 maps to current (mA):
 *          3.09, 9.18, 15.24, 21.32, 27.395, 33.47, 39.545, 45.62,
 *          51.695, 57.77, 63.845, 69.92, 75.995, 82.07, 88.145, 94.22
 */
typedef enum {
    SI7021_HEATER_MIN_CURRENT    = 3, /**< Minimum heater current (mA) */
    SI7021_HEATER_CURRENT_OFFSET = 6, /**< Step size between register codes (mA) */
} Si7021_Heater_t;

/* ============================================================================
 * Public API — Low-level I/O
 * ============================================================================ */

/**
 * @brief   Reads a register or performs a measurement command
 * @param   hsi7021  Pointer to device handle
 * @param   reg_cmd  Register read or measurement command
 * @param   value    Destination buffer
 * @param   size     Number of bytes to read
 * @retval  HAL_OK     Read successful
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef Si7021_ReadRegister(Si7021_t *hsi7021, Si7021_Command_t reg_cmd, uint8_t *value,
                                      uint8_t size);

/**
 * @brief   Writes a single byte to a sensor register
 * @param   hsi7021  Pointer to device handle
 * @param   reg_cmd  Register write command
 * @param   value    Byte value to write
 * @retval  HAL_OK     Write successful
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef Si7021_WriteRegister(Si7021_t *hsi7021, Si7021_Command_t reg_cmd, uint8_t value);

/* ============================================================================
 * Public API — Lifecycle
 * ============================================================================ */

/**
 * @brief   Reads firmware revision into the device handle
 * @param   hsi7021  Pointer to device handle
 * @retval  HAL_OK     Firmware read successfully
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef Si7021_ReadFirmware(Si7021_t *hsi7021);

/**
 * @brief   Performs a software reset of the sensor
 * @param   hsi7021  Pointer to device handle
 * @retval  HAL_OK     Reset command issued
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef Si7021_SoftwareReset(Si7021_t *hsi7021);

/**
 * @brief   Initializes the Si7021 device handle
 * @param   hsi7021      Pointer to device handle
 * @param   hi2c         Pointer to HAL I2C handle
 * @param   address      7-bit I2C address (typically 0x40)
 * @param   resolution   Initial RH/temperature resolution
 * @retval  HAL_OK     Initialization successful
 * @retval  HAL_ERROR  Null pointer, firmware read failure, or resolution set failure
 * @note    Stores address as (address << 1) for HAL I2C APIs.
 */
HAL_StatusTypeDef Si7021_Init(Si7021_t *hsi7021, I2C_HandleTypeDef *hi2c, uint8_t address,
                               Si7021_Resolution_t resolution);

/* ============================================================================
 * Public API — Configuration
 * ============================================================================ */

/**
 * @brief   Sets humidity and temperature measurement resolution
 * @param   hsi7021      Pointer to device handle
 * @param   resolution   Resolution setting
 * @retval  HAL_OK     Resolution written to user register 1
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef Si7021_SetResolution(Si7021_t *hsi7021, Si7021_Resolution_t resolution);

/**
 * @brief   Reads resolution from user register 1 into data.resolution
 * @param   hsi7021  Pointer to device handle
 * @retval  HAL_OK     Resolution cached in handle
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef Si7021_GetResolution(Si7021_t *hsi7021);

/**
 * @brief   Sets on-chip heater current
 * @param   hsi7021  Pointer to device handle
 * @param   current  Desired heater current in mA (3–94 at 3.3 V)
 * @retval  HAL_OK     Heater register written
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef Si7021_SetHeaterCurrent(Si7021_t *hsi7021, uint8_t current);

/**
 * @brief   Reads heater current setting into data.heater_current
 * @param   hsi7021  Pointer to device handle
 * @retval  HAL_OK     Heater current cached in handle
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef Si7021_GetHeaterCurrent(Si7021_t *hsi7021);

/* ============================================================================
 * Public API — Measurements
 * ============================================================================ */

/**
 * @brief   Measures relative humidity with CRC verification
 * @param   hsi7021  Pointer to device handle
 * @retval  HAL_OK     Humidity available in data.humidity (%RH, clamped 0–100)
 * @retval  HAL_ERROR  Null pointer, I2C failure, or CRC mismatch
 */
HAL_StatusTypeDef Si7021_ReadHumidity(Si7021_t *hsi7021);

/**
 * @brief   Measures temperature with CRC verification
 * @param   hsi7021  Pointer to device handle
 * @retval  HAL_OK     Temperature available in data.temperature (°C)
 * @retval  HAL_ERROR  Null pointer, I2C failure, or CRC mismatch
 */
HAL_StatusTypeDef Si7021_ReadTemperature(Si7021_t *hsi7021);

/**
 * @brief   Measures humidity then reads temperature from previous RH conversion
 * @param   hsi7021  Pointer to device handle
 * @retval  HAL_OK     data.humidity and data.temperature updated
 * @retval  HAL_ERROR  Humidity or temperature read failure
 * @note    Temperature uses command 0xE0 (no CRC on temperature read).
 */
HAL_StatusTypeDef Si7021_ReadHumidityAndTemperature(Si7021_t *hsi7021);

#endif /* SI7021_H */
