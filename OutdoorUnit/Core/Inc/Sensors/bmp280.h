/**
 * @file    bmp280.h
 * @brief   BMP280 temperature and pressure sensor driver (I2C)
 * @details Public types and API for Bosch BMP280 over STM32 HAL I2C.
 *          High-level Get* helpers use blocking I/O. Raw register reads
 *          support blocking and DMA modes via BMP280_IoMode.
 */

#ifndef BMP280_H
#define BMP280_H

#include <stdint.h>
#include "main.h"

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Datasheet recommended operating profiles (Table 7)
 */
typedef enum {
    BMP280_OPERATION_0 = 0x00, /**< Handheld device low-power */
    BMP280_OPERATION_1 = 0x01, /**< Handheld device dynamic */
    BMP280_OPERATION_2 = 0x02, /**< Weather monitoring (lowest power) */
    BMP280_OPERATION_3 = 0x03, /**< Elevator / floor change detection */
    BMP280_OPERATION_4 = 0x04, /**< Drop detection */
    BMP280_OPERATION_5 = 0x05, /**< Indoor navigation */
} BMP280_Operation_t;

/**
 * @brief Driver-specific status codes (legacy; most APIs return HAL_StatusTypeDef)
 */
typedef enum {
    BMP280_OK         = 0x0U, /**< Operation successful */
    BMP280_ERROR      = 0x01U, /**< Generic error */
    BMP280_BUSY       = 0x02U, /**< Device or bus busy */
    BMP280_TIMEOUT    = 0x03U, /**< Operation timed out */
    BMP280_INIT_ERROR = 0x04U, /**< Initialization failure */
    BMP280_READ_ERROR = 0x05U, /**< Register read failure */
    BMP280_WRITE_ERROR = 0x06U, /**< Register write failure */
} BMP280_Status_t;

/**
 * @brief Device commands and fixed identifiers
 */
typedef enum {
    BMP280_RESET_COMMAND = 0xB6, /**< Soft-reset command written to RESET register */
    BMP280_CHIP_ID       = 0x58, /**< Expected chip ID value */
} BMP280_Command_t;

/**
 * @brief BMP280 register map
 */
typedef enum {
    BMP280_REG_CHIP_ID    = 0xD0, /**< Chip ID register */
    BMP280_REG_RESET      = 0xE0, /**< Soft reset register */
    BMP280_REG_STATUS     = 0xF3, /**< Measuring / NVM update status */
    BMP280_REG_CTRL_MEAS  = 0xF4, /**< Temp/pressure oversampling and mode */
    BMP280_REG_CONFIG     = 0xF5, /**< Standby time and IIR filter */
    BMP280_REG_PRESS_MSB  = 0xF7, /**< Pressure data MSB */
    BMP280_REG_PRESS_LSB  = 0xF8, /**< Pressure data LSB */
    BMP280_REG_PRESS_XLSB = 0xF9, /**< Pressure data XLSB */
    BMP280_REG_TEMP_MSB   = 0xFA, /**< Temperature data MSB */
    BMP280_REG_TEMP_LSB   = 0xFB, /**< Temperature data LSB */
    BMP280_REG_TEMP_XLSB  = 0xFC, /**< Temperature data XLSB */
    BMP280_REG_CALIB_START = 0x88, /**< Start of calibration block */
    BMP280_REG_CALIB_END  = 0xA1, /**< End of calibration block */
} BMP280_Registers;

/**
 * @brief Sensor power / measurement mode
 */
typedef enum {
    BMP280_MODE_SLEEP  = 0x00, /**< No measurements, lowest power */
    BMP280_MODE_FORCED = 0x01, /**< Single conversion then return to sleep */
    BMP280_MODE_NORMAL = 0x03, /**< Continuous cycling with standby */
} BMP280_Mode;

/**
 * @brief Oversampling settings for temperature and pressure
 */
typedef enum {
    BMP280_OVERSAMPLING_SKIPPED = 0x00, /**< Measurement skipped */
    BMP280_OVERSAMPLING_X1      = 0x01, /**< x1 oversampling (ultra low power) */
    BMP280_OVERSAMPLING_X2      = 0x02, /**< x2 oversampling (low power) */
    BMP280_OVERSAMPLING_X4      = 0x03, /**< x4 oversampling (standard resolution) */
    BMP280_OVERSAMPLING_X8      = 0x04, /**< x8 oversampling (high resolution) */
    BMP280_OVERSAMPLING_X16     = 0x05, /**< x16 oversampling (ultra high resolution) */
} BMP280_Oversampling;

/**
 * @brief IIR filter coefficient
 */
typedef enum {
    BMP280_FILTER_OFF = 0x00, /**< Filter disabled */
    BMP280_FILTER_2   = 0x01, /**< Filter coefficient 2 */
    BMP280_FILTER_4   = 0x02, /**< Filter coefficient 4 */
    BMP280_FILTER_8   = 0x03, /**< Filter coefficient 8 */
    BMP280_FILTER_16  = 0x04, /**< Filter coefficient 16 */
} BMP280_Filter;

/**
 * @brief Standby duration between measurements in normal mode
 */
typedef enum {
    BMP280_STANDBY_0_5_MS  = 0x00, /**< 0.5 ms */
    BMP280_STANDBY_62_5_MS = 0x01, /**< 62.5 ms */
    BMP280_STANDBY_125_MS  = 0x02, /**< 125 ms */
    BMP280_STANDBY_250_MS  = 0x03, /**< 250 ms */
    BMP280_STANDBY_500_MS  = 0x04, /**< 500 ms */
    BMP280_STANDBY_1000_MS = 0x05, /**< 1000 ms */
    BMP280_STANDBY_2000_MS = 0x06, /**< 2000 ms */
    BMP280_STANDBY_4000_MS = 0x07, /**< 4000 ms */
} BMP280_StandbyTime;

/**
 * @brief I2C transfer mode for raw register reads
 */
typedef enum {
    BMP280_IO_BLOCKING = 0x00, /**< Blocking HAL_I2C_Mem_Read */
    BMP280_IO_DMA      = 0x01, /**< Non-blocking HAL_I2C_Mem_Read_DMA */
} BMP280_IoMode;

/**
 * @brief Factory calibration coefficients and fine temperature
 * @details Loaded from NVM via BMP280_ReadCalibration().
 *          t_fine is updated by temperature compensation.
 */
typedef struct {
    int16_t dig_T2;  /**< Temperature calibration T2 */
    int16_t dig_T3;  /**< Temperature calibration T3 */
    int16_t dig_P2;  /**< Pressure calibration P2 */
    int16_t dig_P3;  /**< Pressure calibration P3 */
    int16_t dig_P4;  /**< Pressure calibration P4 */
    int16_t dig_P5;  /**< Pressure calibration P5 */
    int16_t dig_P6;  /**< Pressure calibration P6 */
    int16_t dig_P7;  /**< Pressure calibration P7 */
    int16_t dig_P8;  /**< Pressure calibration P8 */
    int16_t dig_P9;  /**< Pressure calibration P9 */
    uint16_t dig_P1; /**< Pressure calibration P1 */
    uint16_t dig_T1; /**< Temperature calibration T1 */
    int32_t t_fine;  /**< Fine temperature used by pressure compensation */
} BMP280_Calibcalibration_t;

/**
 * @brief Raw ADC samples and compensated measurement results
 */
typedef struct {
    int32_t raw_temperature; /**< Uncompensated temperature ADC value */
    int32_t raw_pressure;    /**< Uncompensated pressure ADC value */
    float temperature;       /**< Compensated temperature in °C */
    float pressure;          /**< Compensated pressure in hPa */
} BMP280_Measurment_t;

/**
 * @brief BMP280 device handle
 * @details Holds I2C binding, latest data and calibration coefficients.
 */
typedef struct {
    I2C_HandleTypeDef *i2c_handle; /**< HAL I2C handle */
    uint8_t address;               /**< 8-bit HAL address (7-bit << 1) */
    BMP280_Measurment_t data;      /**< Latest raw and compensated values */
    BMP280_Calibcalibration_t calibration; /**< NVM trim coefficients */
} BMP280_t;

/* ============================================================================
 * Public API — Lifecycle
 * ============================================================================ */

/**
 * @brief   Initializes the BMP280 device handle
 * @param   dev         Pointer to device handle
 * @param   i2c_handle  Pointer to HAL I2C handle
 * @param   sdo_state   SDO pin level: 0 selects 0x76, non-zero selects 0x77
 * @retval  HAL_OK      Initialization successful
 * @retval  HAL_ERROR   Null pointer, wrong chip ID, or I2C/calibration failure
 * @note    Stores address as (7-bit address << 1) for HAL I2C APIs.
 */
HAL_StatusTypeDef BMP280_Init(BMP280_t *dev, I2C_HandleTypeDef *i2c_handle, uint8_t sdo_state);

/**
 * @brief   Performs a soft reset of the sensor
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Reset command written
 * @retval  HAL_ERROR  Null pointer or I2C write failure
 */
HAL_StatusTypeDef BMP280_SoftReset(BMP280_t *dev);

/**
 * @brief   Reads factory calibration coefficients into the device handle
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Calibration data loaded
 * @retval  HAL_ERROR  Null pointer or I2C read failure
 */
HAL_StatusTypeDef BMP280_ReadCalibration(BMP280_t *dev);

/* ============================================================================
 * Public API — Configuration
 * ============================================================================ */

/**
 * @brief   Writes standby time and IIR filter to CONFIG register
 * @param   dev      Pointer to device handle
 * @param   standby  Standby duration (normal mode)
 * @param   filter   IIR filter coefficient
 * @retval  HAL_OK     CONFIG register written
 * @retval  HAL_ERROR  Null pointer or I2C write failure
 */
HAL_StatusTypeDef BMP280_SetConfig(BMP280_t *dev, BMP280_StandbyTime standby, BMP280_Filter filter);

/**
 * @brief   Writes pressure oversampling and mode to CTRL_MEAS register
 * @param   dev            Pointer to device handle
 * @param   OVERSAMPLING_p Pressure oversampling
 * @param   mode           Operating mode
 * @retval  HAL_OK     CTRL_MEAS register written
 * @retval  HAL_ERROR  Null pointer or I2C write failure
 */
HAL_StatusTypeDef BMP280_SetCtrlMeas(BMP280_t *dev, BMP280_Oversampling OVERSAMPLING_p, BMP280_Mode mode);

/**
 * @brief   Updates mode in CTRL_MEAS register (read-modify-write)
 * @param   dev   Pointer to device handle
 * @param   mode  New operating mode
 * @retval  HAL_OK     Mode applied
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef BMP280_SetMode(BMP280_t *dev, BMP280_Mode mode);

/**
 * @brief   Applies a datasheet recommended operating profile
 * @param   dev        Pointer to device handle
 * @param   operation  Profile selector (BMP280_Operation_t)
 * @retval  HAL_OK     Profile applied
 * @retval  HAL_ERROR  Null pointer, invalid profile, or I2C write failure
 */
HAL_StatusTypeDef BMP280_OperationMode(BMP280_t *dev, BMP280_Operation_t operation);

/* ============================================================================
 * Public API — Status
 * ============================================================================ */

/**
 * @brief   Reads STATUS register measuring and im_update flags
 * @param   dev        Pointer to device handle
 * @param   measuring  Output: 1 while conversion in progress
 * @param   im_update  Output: 1 while NVM data are being copied
 * @retval  HAL_OK     Status read successfully
 * @retval  HAL_ERROR  Null pointer or I2C read failure
 */
HAL_StatusTypeDef BMP280_GetStatus(BMP280_t *dev, uint8_t *measuring, uint8_t *im_update);

/* ============================================================================
 * Public API — Raw I/O
 * ============================================================================ */

/**
 * @brief   Reads raw register data using blocking or DMA I2C
 * @param   dev     Pointer to device handle
 * @param   reg     Start register address
 * @param   buffer  Destination buffer (must stay valid until DMA completes)
 * @param   size    Number of bytes to read
 * @param   mode    Blocking or DMA transfer mode
 * @retval  HAL_OK     Transfer started/completed successfully
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef BMP280_ReadRawData(BMP280_t *dev, BMP280_Registers reg, uint8_t *buffer,
                                     uint16_t size, BMP280_IoMode mode);

/**
 * @brief   Reads 3 raw temperature bytes starting at TEMP_MSB
 * @param   dev     Pointer to device handle
 * @param   buffer  Destination buffer (at least 3 bytes)
 * @param   size    Buffer capacity in bytes
 * @param   mode    Blocking or DMA transfer mode
 * @retval  HAL_OK     Read successful or DMA started
 * @retval  HAL_ERROR  Null pointer, buffer too small, or I2C failure
 */
HAL_StatusTypeDef BMP280_ReadRawTemperature(BMP280_t *dev, uint8_t *buffer, uint16_t size,
                                            BMP280_IoMode mode);

/**
 * @brief   Reads 3 raw pressure bytes starting at PRESS_MSB
 * @param   dev     Pointer to device handle
 * @param   buffer  Destination buffer (at least 3 bytes)
 * @param   size    Buffer capacity in bytes
 * @param   mode    Blocking or DMA transfer mode
 * @retval  HAL_OK     Read successful or DMA started
 * @retval  HAL_ERROR  Null pointer, buffer too small, or I2C failure
 */
HAL_StatusTypeDef BMP280_ReadRawPressure(BMP280_t *dev, uint8_t *buffer, uint16_t size,
                                         BMP280_IoMode mode);

/**
 * @brief   Reads 6 consecutive bytes: pressure then temperature
 * @param   dev     Pointer to device handle
 * @param   buffer  Destination buffer (at least 6 bytes)
 * @param   size    Buffer capacity in bytes
 * @param   mode    Blocking or DMA transfer mode
 * @retval  HAL_OK     Read successful or DMA started
 * @retval  HAL_ERROR  Null pointer, buffer too small, or I2C failure
 */
HAL_StatusTypeDef BMP280_ReadRawTemperaturePressure(BMP280_t *dev, uint8_t *buffer, uint16_t size,
                                                    BMP280_IoMode mode);

/* ============================================================================
 * Public API — Parse / Compensate
 * ============================================================================ */

/**
 * @brief   Parses 3-byte temperature frame into raw_temperature
 * @param   dev     Pointer to device handle
 * @param   buffer  Raw temperature bytes (MSB, LSB, XLSB)
 * @retval  HAL_OK     Parsed successfully
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BMP280_ParseRawTemperature(BMP280_t *dev, const uint8_t *buffer);

/**
 * @brief   Parses 3-byte pressure frame into raw_pressure
 * @param   dev     Pointer to device handle
 * @param   buffer  Raw pressure bytes (MSB, LSB, XLSB)
 * @retval  HAL_OK     Parsed successfully
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BMP280_ParseRawPressure(BMP280_t *dev, const uint8_t *buffer);

/**
 * @brief   Parses 6-byte burst frame into raw pressure and temperature
 * @param   dev     Pointer to device handle
 * @param   buffer  Burst data starting at PRESS_MSB
 * @retval  HAL_OK     Parsed successfully
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BMP280_ParseRawTemperaturePressure(BMP280_t *dev, const uint8_t *buffer);

/**
 * @brief   Compensates temperature and updates t_fine and data.temperature
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Compensation successful
 * @retval  HAL_ERROR  Null pointer
 * @note    Must be called before pressure compensation.
 */
HAL_StatusTypeDef BMP280_CompensateTemperature(BMP280_t *dev);

/**
 * @brief   Compensates pressure into data.pressure (hPa)
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Compensation successful
 * @retval  HAL_ERROR  Null pointer or division by zero in trim math
 * @note    Requires a prior BMP280_CompensateTemperature() call (valid t_fine).
 */
HAL_StatusTypeDef BMP280_CompensatePressure(BMP280_t *dev);

/**
 * @brief   Compensates temperature then pressure in one call
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Compensation successful
 * @retval  HAL_ERROR  Compensation step failed
 */
HAL_StatusTypeDef BMP280_CompensateTemperatureAndPressure(BMP280_t *dev);

/* ============================================================================
 * Public API — Convenience
 * ============================================================================ */

/**
 * @brief   Blocking read, parse and compensate for temperature only
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Temperature available in data.temperature
 * @retval  HAL_ERROR  Read/parse/compensate failure
 */
HAL_StatusTypeDef BMP280_GetTemperature(BMP280_t *dev);

/**
 * @brief   Blocking read of pressure with temperature compensation for t_fine
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Pressure available in data.pressure
 * @retval  HAL_ERROR  Read/parse/compensate failure
 * @warning Compensates using existing raw_temperature in the handle
 *          (does not re-read temperature ADC). Prefer GetTemperatureAndPressure
 *          when both values are needed.
 */
HAL_StatusTypeDef BMP280_GetPressure(BMP280_t *dev);

/**
 * @brief   Blocking burst read and full compensation of temperature and pressure
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Results in data.temperature and data.pressure
 * @retval  HAL_ERROR  Read/parse/compensate failure
 */
HAL_StatusTypeDef BMP280_GetTemperatureAndPressure(BMP280_t *dev);

#endif /* BMP280_H */
