/**
 * @file    bme280.h
 * @brief   BME280 temperature, pressure and humidity sensor driver (I2C)
 * @details Public types and API for Bosch BME280 over STM32 HAL I2C.
 *          Configuration setters mostly update cached settings; call
 *          BME280_ApplySettings() to write them to the device (except
 *          BME280_SetConfig / BME280_SetMode which write immediately).
 *
 *          Raw register I/O supports blocking, DMA and interrupt modes via
 *          BME280_IoMode. High-level Get* helpers use blocking I/O only.
 *          For DMA/IT, start a raw read/write, then parse/compensate from
 *          HAL_I2C_MemRxCpltCallback / HAL_I2C_MemTxCpltCallback using
 *          BME280_HandleMemRxCplt() / BME280_HandleMemTxCplt().
 */

#ifndef BME280_H
#define BME280_H

#include <stdint.h>
#include "main.h"

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

typedef struct BME280_t BME280_t;

/**
 * @brief Datasheet recommended operating profiles (section 3.5)
 */
typedef enum {
    BME280_PROFILE_WEATHER_MONITORING = 0, /**< Low power weather monitoring (forced) */
    BME280_PROFILE_HUMIDITY_SENSING,       /**< Humidity-focused, pressure skipped */
    BME280_PROFILE_INDOOR_NAVIGATION,      /**< High oversampling, normal mode */
    BME280_PROFILE_GAMING,                 /**< Fast updates, humidity skipped */
} BME280_Profile_t;

/**
 * @brief BME280 register map
 */
typedef enum {
    BME280_REG_CHIP_ID         = 0xD0, /**< Chip ID register */
    BME280_REG_RESET           = 0xE0, /**< Soft reset register */
    BME280_REG_CTRL_HUM        = 0xF2, /**< Humidity oversampling control */
    BME280_REG_STATUS          = 0xF3, /**< Measuring / NVM update status */
    BME280_REG_CTRL_MEAS       = 0xF4, /**< Temp/pressure oversampling and mode */
    BME280_REG_CONFIG          = 0xF5, /**< Standby time and IIR filter */
    BME280_REG_PRESS_MSB       = 0xF7, /**< Pressure data MSB */
    BME280_REG_PRESS_LSB       = 0xF8, /**< Pressure data LSB */
    BME280_REG_PRESS_XLSB      = 0xF9, /**< Pressure data XLSB */
    BME280_REG_TEMP_MSB        = 0xFA, /**< Temperature data MSB */
    BME280_REG_TEMP_LSB        = 0xFB, /**< Temperature data LSB */
    BME280_REG_TEMP_XLSB       = 0xFC, /**< Temperature data XLSB */
    BME280_REG_HUM_MSB         = 0xFD, /**< Humidity data MSB */
    BME280_REG_HUM_LSB         = 0xFE, /**< Humidity data LSB */
    BME280_REG_CALIB_START     = 0x88, /**< Start of T/P calibration block */
    BME280_REG_CALIB_END       = 0xA1, /**< End of T/P calibration block */
    BME280_REG_HUM_CALIB_START = 0xE1, /**< Start of humidity calibration block */
    BME280_REG_HUM_CALIB_END   = 0xE7, /**< End of humidity calibration block */
} BME280_Registers;

/**
 * @brief Device commands and fixed identifiers
 */
typedef enum {
    BME280_RESET_COMMAND = 0xB6, /**< Soft-reset command written to RESET register */
    BME280_CHIP_ID       = 0x60, /**< Expected chip ID value */
} BME280_Command_t;

/**
 * @brief Sensor power / measurement mode
 */
typedef enum {
    BME280_MODE_SLEEP  = 0x00, /**< No measurements, lowest power */
    BME280_MODE_FORCED = 0x01, /**< Single conversion then return to sleep */
    BME280_MODE_NORMAL = 0x03, /**< Continuous cycling with standby */
} BME280_Mode;

/**
 * @brief Oversampling settings for temperature, pressure and humidity
 */
typedef enum {
    BME280_OVERSAMPLING_SKIPPED = 0x00, /**< Measurement skipped */
    BME280_OVERSAMPLING_X1      = 0x01, /**< x1 oversampling */
    BME280_OVERSAMPLING_X2      = 0x02, /**< x2 oversampling */
    BME280_OVERSAMPLING_X4      = 0x03, /**< x4 oversampling */
    BME280_OVERSAMPLING_X8      = 0x04, /**< x8 oversampling */
    BME280_OVERSAMPLING_X16     = 0x05, /**< x16 oversampling */
} BME280_Oversampling;

/**
 * @brief IIR filter coefficient
 */
typedef enum {
    BME280_FILTER_OFF = 0x00, /**< Filter disabled */
    BME280_FILTER_2   = 0x01, /**< Filter coefficient 2 */
    BME280_FILTER_4   = 0x02, /**< Filter coefficient 4 */
    BME280_FILTER_8   = 0x03, /**< Filter coefficient 8 */
    BME280_FILTER_16  = 0x04, /**< Filter coefficient 16 */
} BME280_Filter;

/**
 * @brief Standby duration between measurements in normal mode
 */
typedef enum {
    BME280_STANDBY_0_5_MS  = 0x00, /**< 0.5 ms */
    BME280_STANDBY_62_5_MS = 0x01, /**< 62.5 ms */
    BME280_STANDBY_125_MS  = 0x02, /**< 125 ms */
    BME280_STANDBY_250_MS  = 0x03, /**< 250 ms */
    BME280_STANDBY_500_MS  = 0x04, /**< 500 ms */
    BME280_STANDBY_1000_MS = 0x05, /**< 1000 ms */
    BME280_STANDBY_10_MS   = 0x06, /**< 10 ms */
    BME280_STANDBY_20_MS   = 0x07, /**< 20 ms */
} BME280_StandbyTime;

/**
 * @brief I2C transfer mode for raw register reads and writes
 */
typedef enum {
    BME280_IO_BLOCKING = 0x00, /**< Blocking HAL_I2C_Mem_Read/Write */
    BME280_IO_DMA      = 0x01, /**< Non-blocking HAL_I2C_Mem_Read/Write_DMA */
    BME280_IO_IT       = 0x02, /**< Non-blocking HAL_I2C_Mem_Read/Write_IT */
} BME280_IoMode;

/**
 * @brief Optional callback invoked after a DMA/IT transfer completes
 * @param dev     Device handle that started the transfer
 * @param status  HAL_OK on success, HAL_ERROR on bus failure
 */
typedef void (*BME280_TransferCallback_t)(BME280_t *dev, HAL_StatusTypeDef status);

/**
 * @brief Self-test result codes
 */
typedef enum {
    BME280_SELFTEST_OK                = 0,  /**< All checks passed */
    BME280_SELFTEST_COMM_ERROR        = 10, /**< I2C / communication failure */
    BME280_SELFTEST_TRIM_ERROR        = 20, /**< Calibration trim values implausible */
    BME280_SELFTEST_TEMP_BOND_ERROR   = 30, /**< Raw temperature out of bounds */
    BME280_SELFTEST_PRESS_BOND_ERROR  = 31, /**< Raw pressure out of bounds */
    BME280_SELFTEST_TEMP_PLAUS_ERROR  = 40, /**< Compensated temperature outside limits */
    BME280_SELFTEST_PRESS_PLAUS_ERROR = 41, /**< Compensated pressure outside limits */
    BME280_SELFTEST_HUM_PLAUS_ERROR   = 42, /**< Compensated humidity outside limits */
} BME280_SelfTestResult_t;

/**
 * @brief Factory calibration coefficients and fine temperature
 * @details Loaded from NVM via BME280_ReadCalibration().
 *          t_fine is updated by temperature compensation.
 */
typedef struct {
    uint16_t dig_T1; /**< Temperature calibration T1 */
    int16_t dig_T2;  /**< Temperature calibration T2 */
    int16_t dig_T3;  /**< Temperature calibration T3 */

    uint16_t dig_P1; /**< Pressure calibration P1 */
    int16_t dig_P2;  /**< Pressure calibration P2 */
    int16_t dig_P3;  /**< Pressure calibration P3 */
    int16_t dig_P4;  /**< Pressure calibration P4 */
    int16_t dig_P5;  /**< Pressure calibration P5 */
    int16_t dig_P6;  /**< Pressure calibration P6 */
    int16_t dig_P7;  /**< Pressure calibration P7 */
    int16_t dig_P8;  /**< Pressure calibration P8 */
    int16_t dig_P9;  /**< Pressure calibration P9 */

    uint8_t dig_H1;  /**< Humidity calibration H1 */
    int16_t dig_H2;  /**< Humidity calibration H2 */
    uint8_t dig_H3;  /**< Humidity calibration H3 */
    int16_t dig_H4;  /**< Humidity calibration H4 */
    int16_t dig_H5;  /**< Humidity calibration H5 */
    int8_t dig_H6;   /**< Humidity calibration H6 */

    int32_t t_fine;  /**< Fine temperature used by P/H compensation */
} BME280_Calibration_t;

/**
 * @brief Raw ADC samples and compensated measurement results
 */
typedef struct {
    int32_t raw_temperature; /**< Uncompensated temperature ADC value */
    int32_t raw_pressure;    /**< Uncompensated pressure ADC value */
    int32_t raw_humidity;    /**< Uncompensated humidity ADC value */
    float temperature;       /**< Compensated temperature in °C */
    float pressure;          /**< Compensated pressure in hPa */
    float humidity;          /**< Compensated relative humidity in %RH */
} BME280_Measurement_t;

/**
 * @brief Cached sensor configuration written by ApplySettings / SetConfig
 */
typedef struct {
    BME280_Oversampling osrs_h;  /**< Humidity oversampling */
    BME280_Oversampling osrs_t;  /**< Temperature oversampling */
    BME280_Oversampling osrs_p;  /**< Pressure oversampling */
    BME280_Mode mode;            /**< Sleep / forced / normal */
    BME280_StandbyTime standby;  /**< Standby time (normal mode) */
    BME280_Filter filter;        /**< IIR filter coefficient */
} BME280_Settings_t;

/**
 * @brief Plausibility limits used by BME280_RunSelfTest()
 */
typedef struct {
    float temp_min_c;    /**< Minimum accepted temperature (°C) */
    float temp_max_c;    /**< Maximum accepted temperature (°C) */
    float press_min_hpa; /**< Minimum accepted pressure (hPa) */
    float press_max_hpa; /**< Maximum accepted pressure (hPa) */
    float hum_min_pct;   /**< Minimum accepted humidity (%RH) */
    float hum_max_pct;   /**< Maximum accepted humidity (%RH) */
} BME280_SelfTestLimits_t;

/**
 * @brief BME280 device handle
 * @details Holds I2C binding, latest data, calibration and settings.
 */
typedef struct BME280_t {
    I2C_HandleTypeDef *i2c_handle; /**< HAL I2C handle */
    uint8_t address;               /**< 8-bit HAL address (7-bit << 1) */
    BME280_Measurement_t data;     /**< Latest raw and compensated values */
    BME280_Calibration_t calibration; /**< NVM trim coefficients */
    BME280_Settings_t settings;    /**< Cached configuration */
    volatile uint8_t io_busy;      /**< 1 while a DMA/IT transfer is active */
    BME280_TransferCallback_t transfer_cb; /**< Optional async completion hook */
} BME280_t;

/* ============================================================================
 * Public API — Lifecycle
 * ============================================================================ */

/**
 * @brief   Initializes the BME280 device handle and applies weather-monitoring profile
 * @param   dev         Pointer to device handle
 * @param   i2c_handle  Pointer to HAL I2C handle
 * @param   address     7-bit I2C address (typically 0x76 or 0x77)
 * @retval  HAL_OK      Initialization successful
 * @retval  HAL_ERROR   Null pointer, wrong chip ID, or I2C/calibration failure
 * @note    Stores address as (address << 1) for HAL I2C APIs.
 */
HAL_StatusTypeDef BME280_Init(BME280_t *dev, I2C_HandleTypeDef *i2c_handle, uint8_t address);

/**
 * @brief   Performs a soft reset of the sensor
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Reset command written
 * @retval  HAL_ERROR  Null pointer or I2C write failure
 * @note    Waits 2 ms after a successful write for the device to reboot.
 */
HAL_StatusTypeDef BME280_SoftReset(BME280_t *dev);

/**
 * @brief   Reads factory calibration coefficients into the device handle
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Calibration data loaded
 * @retval  HAL_ERROR  Null pointer or I2C read failure
 */
HAL_StatusTypeDef BME280_ReadCalibration(BME280_t *dev);

/* ============================================================================
 * Public API — Configuration
 * ============================================================================ */

/**
 * @brief   Caches humidity oversampling setting
 * @param   dev     Pointer to device handle
 * @param   osrs_h  Humidity oversampling
 * @retval  HAL_OK     Setting stored
 * @retval  HAL_ERROR  Null pointer
 * @note    Does not write to the device; call BME280_ApplySettings().
 */
HAL_StatusTypeDef BME280_SetCtrlHum(BME280_t *dev, BME280_Oversampling osrs_h);

/**
 * @brief   Caches temperature/pressure oversampling and mode
 * @param   dev     Pointer to device handle
 * @param   osrs_t  Temperature oversampling
 * @param   osrs_p  Pressure oversampling
 * @param   mode    Operating mode
 * @retval  HAL_OK     Settings stored
 * @retval  HAL_ERROR  Null pointer
 * @note    Does not write to the device; call BME280_ApplySettings().
 */
HAL_StatusTypeDef BME280_SetCtrlMeas(BME280_t *dev, BME280_Oversampling osrs_t,
                                     BME280_Oversampling osrs_p, BME280_Mode mode);

/**
 * @brief   Caches pressure oversampling and mode with recommended temperature OSRS
 * @param   dev     Pointer to device handle
 * @param   osrs_p  Pressure oversampling
 * @param   mode    Operating mode
 * @retval  HAL_OK     Settings stored
 * @retval  HAL_ERROR  Null pointer
 * @details Temperature oversampling is set to x2 when pressure is x16, otherwise x1.
 * @note    Does not write to the device; call BME280_ApplySettings().
 */
HAL_StatusTypeDef BME280_SetCtrlMeasSimple(BME280_t *dev, BME280_Oversampling osrs_p,
                                           BME280_Mode mode);

/**
 * @brief   Caches and immediately writes standby time and IIR filter
 * @param   dev      Pointer to device handle
 * @param   standby  Standby duration (normal mode)
 * @param   filter   IIR filter coefficient
 * @retval  HAL_OK     CONFIG register written
 * @retval  HAL_ERROR  Null pointer or I2C write failure
 */
HAL_StatusTypeDef BME280_SetConfig(BME280_t *dev, BME280_StandbyTime standby, BME280_Filter filter);

/**
 * @brief   Updates mode in cache and CTRL_MEAS register (read-modify-write)
 * @param   dev   Pointer to device handle
 * @param   mode  New operating mode
 * @retval  HAL_OK     Mode applied
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef BME280_SetMode(BME280_t *dev, BME280_Mode mode);

/**
 * @brief   Writes cached settings to CTRL_HUM, CTRL_MEAS and CONFIG
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     All registers written
 * @retval  HAL_ERROR  Null pointer or I2C write failure
 * @note    CTRL_HUM must be written before CTRL_MEAS for changes to take effect.
 */
HAL_StatusTypeDef BME280_ApplySettings(BME280_t *dev);

/**
 * @brief   Applies a datasheet recommended profile and writes settings to the device
 * @param   dev      Pointer to device handle
 * @param   profile  Profile selector
 * @retval  HAL_OK     Profile applied
 * @retval  HAL_ERROR  Null pointer, invalid profile, or I2C write failure
 */
HAL_StatusTypeDef BME280_SetProfile(BME280_t *dev, BME280_Profile_t profile);

/* ============================================================================
 * Public API — Status / Timing
 * ============================================================================ */

/**
 * @brief   Reads STATUS register measuring and im_update flags
 * @param   dev        Pointer to device handle
 * @param   measuring  Output: 1 while conversion in progress
 * @param   im_update  Output: 1 while NVM data are being copied
 * @retval  HAL_OK     Status read successfully
 * @retval  HAL_ERROR  Null pointer or I2C read failure
 */
HAL_StatusTypeDef BME280_GetStatus(BME280_t *dev, uint8_t *measuring, uint8_t *im_update);

/**
 * @brief   Polls until measurement completes or timeout expires
 * @param   dev         Pointer to device handle
 * @param   timeout_ms  Maximum wait time in milliseconds
 * @retval  HAL_OK       Measurement finished
 * @retval  HAL_ERROR    Null pointer or status read failure
 * @retval  HAL_TIMEOUT  Still measuring after timeout
 */
HAL_StatusTypeDef BME280_WaitForMeasurement(BME280_t *dev, uint32_t timeout_ms);

/**
 * @brief   Estimates measurement duration from current oversampling settings
 * @param   dev           Pointer to device handle
 * @param   use_max_time  Non-zero: datasheet max-time formula; zero: typical-time formula
 * @retval  Duration in milliseconds, or 0 if @p dev is NULL
 */
uint32_t BME280_GetMeasurementDurationMs(const BME280_t *dev, uint8_t use_max_time);

/* ============================================================================
 * Public API — Raw I/O
 * ============================================================================ */

/**
 * @brief   Reads raw register data using blocking, DMA or interrupt I2C
 * @param   dev     Pointer to device handle
 * @param   reg     Start register address
 * @param   buffer  Destination buffer (must stay valid until DMA/IT completes)
 * @param   size    Number of bytes to read
 * @param   mode    Blocking, DMA or interrupt transfer mode
 * @retval  HAL_OK     Transfer started/completed successfully
 * @retval  HAL_BUSY   Another DMA/IT transfer is already in progress
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef BME280_ReadRawData(BME280_t *dev, BME280_Registers reg, uint8_t *buffer,
                                     uint16_t size, BME280_IoMode mode);

/**
 * @brief   Writes a single register using blocking, DMA or interrupt I2C
 * @param   dev     Pointer to device handle
 * @param   reg     Register address
 * @param   value   Byte value to write
 * @param   mode    Blocking, DMA or interrupt transfer mode
 * @retval  HAL_OK     Transfer started/completed successfully
 * @retval  HAL_BUSY   Another DMA/IT transfer is already in progress
 * @retval  HAL_ERROR  Null pointer or I2C failure
 * @note    For DMA/IT, @p value must remain valid until the transfer completes.
 */
HAL_StatusTypeDef BME280_WriteRawData(BME280_t *dev, BME280_Registers reg, const uint8_t *value,
                                      BME280_IoMode mode);

/**
 * @brief   Returns whether a DMA/IT transfer is in progress on this handle
 * @param   dev  Pointer to device handle
 * @retval  1 if busy, 0 if idle or @p dev is NULL
 */
uint8_t BME280_IsBusy(const BME280_t *dev);

/**
 * @brief   Registers an optional callback for DMA/IT transfer completion
 * @param   dev  Pointer to device handle
 * @param   cb   Callback invoked from BME280_HandleMemRxCplt/TxCplt; NULL to disable
 */
void BME280_SetTransferCallback(BME280_t *dev, BME280_TransferCallback_t cb);

/**
 * @brief   Call from HAL_I2C_MemRxCpltCallback when a BME280 DMA/IT read finishes
 * @param   dev  Device handle that started the read
 */
void BME280_HandleMemRxCplt(BME280_t *dev);

/**
 * @brief   Call from HAL_I2C_MemTxCpltCallback when a BME280 DMA/IT write finishes
 * @param   dev  Device handle that started the write
 */
void BME280_HandleMemTxCplt(BME280_t *dev);

/**
 * @brief   Call from HAL_I2C_ErrorCallback when a BME280 DMA/IT transfer fails
 * @param   dev  Device handle that started the transfer
 */
void BME280_HandleError(BME280_t *dev);

/**
 * @brief   Parses a STATUS register byte into measuring / im_update flags
 * @param   status_reg  Raw STATUS register value
 * @param   measuring   Output: 1 while conversion in progress
 * @param   im_update   Output: 1 while NVM data are being copied
 * @retval  HAL_OK     Parsed successfully
 * @retval  HAL_ERROR  Null output pointer
 */
HAL_StatusTypeDef BME280_ParseStatus(const uint8_t *status_reg, uint8_t *measuring,
                                     uint8_t *im_update);

/**
 * @brief   Reads 3 raw temperature bytes starting at TEMP_MSB
 * @param   dev     Pointer to device handle
 * @param   buffer  Destination buffer (at least 3 bytes)
 * @param   size    Buffer capacity in bytes
 * @param   mode    Blocking, DMA or interrupt transfer mode
 * @retval  HAL_OK     Read successful or DMA/IT started
 * @retval  HAL_BUSY   DMA/IT transfer already in progress
 * @retval  HAL_ERROR  Null pointer, buffer too small, or I2C failure
 */
HAL_StatusTypeDef BME280_ReadRawTemperature(BME280_t *dev, uint8_t *buffer, uint16_t size,
                                            BME280_IoMode mode);

/**
 * @brief   Reads 3 raw pressure bytes starting at PRESS_MSB
 * @param   dev     Pointer to device handle
 * @param   buffer  Destination buffer (at least 3 bytes)
 * @param   size    Buffer capacity in bytes
 * @param   mode    Blocking, DMA or interrupt transfer mode
 * @retval  HAL_OK     Read successful or DMA/IT started
 * @retval  HAL_BUSY   DMA/IT transfer already in progress
 * @retval  HAL_ERROR  Null pointer, buffer too small, or I2C failure
 */
HAL_StatusTypeDef BME280_ReadRawPressure(BME280_t *dev, uint8_t *buffer, uint16_t size,
                                         BME280_IoMode mode);

/**
 * @brief   Reads 2 raw humidity bytes starting at HUM_MSB
 * @param   dev     Pointer to device handle
 * @param   buffer  Destination buffer (at least 2 bytes)
 * @param   size    Buffer capacity in bytes
 * @param   mode    Blocking, DMA or interrupt transfer mode
 * @retval  HAL_OK     Read successful or DMA/IT started
 * @retval  HAL_BUSY   DMA/IT transfer already in progress
 * @retval  HAL_ERROR  Null pointer, buffer too small, or I2C failure
 */
HAL_StatusTypeDef BME280_ReadRawHumidity(BME280_t *dev, uint8_t *buffer, uint16_t size,
                                         BME280_IoMode mode);

/**
 * @brief   Reads 8 consecutive bytes: pressure, temperature and humidity
 * @param   dev     Pointer to device handle
 * @param   buffer  Destination buffer (at least 8 bytes)
 * @param   size    Buffer capacity in bytes
 * @param   mode    Blocking, DMA or interrupt transfer mode
 * @retval  HAL_OK     Read successful or DMA/IT started
 * @retval  HAL_BUSY   DMA/IT transfer already in progress
 * @retval  HAL_ERROR  Null pointer, buffer too small, or I2C failure
 */
HAL_StatusTypeDef BME280_ReadRawTemperaturePressureHumidity(BME280_t *dev, uint8_t *buffer,
                                                            uint16_t size, BME280_IoMode mode);

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
HAL_StatusTypeDef BME280_ParseRawTemperature(BME280_t *dev, const uint8_t *buffer);

/**
 * @brief   Parses 3-byte pressure frame into raw_pressure
 * @param   dev     Pointer to device handle
 * @param   buffer  Raw pressure bytes (MSB, LSB, XLSB)
 * @retval  HAL_OK     Parsed successfully
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BME280_ParseRawPressure(BME280_t *dev, const uint8_t *buffer);

/**
 * @brief   Parses 2-byte humidity frame into raw_humidity
 * @param   dev     Pointer to device handle
 * @param   buffer  Raw humidity bytes (MSB, LSB)
 * @retval  HAL_OK     Parsed successfully
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BME280_ParseRawHumidity(BME280_t *dev, const uint8_t *buffer);

/**
 * @brief   Parses 8-byte burst frame into raw pressure, temperature and humidity
 * @param   dev     Pointer to device handle
 * @param   buffer  Burst data starting at PRESS_MSB
 * @retval  HAL_OK     Parsed successfully
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BME280_ParseRawTemperaturePressureHumidity(BME280_t *dev, const uint8_t *buffer);

/**
 * @brief   Compensates temperature and updates t_fine and data.temperature
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Compensation successful
 * @retval  HAL_ERROR  Null pointer
 * @note    Must be called before pressure/humidity compensation.
 */
HAL_StatusTypeDef BME280_CompensateTemperature(BME280_t *dev);

/**
 * @brief   Compensates pressure into data.pressure (hPa)
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Compensation successful
 * @retval  HAL_ERROR  Null pointer or division by zero in trim math
 * @note    Requires a prior BME280_CompensateTemperature() call (valid t_fine).
 */
HAL_StatusTypeDef BME280_CompensatePressure(BME280_t *dev);

/**
 * @brief   Compensates humidity into data.humidity (%RH)
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Compensation successful
 * @retval  HAL_ERROR  Null pointer
 * @note    Requires a prior BME280_CompensateTemperature() call (valid t_fine).
 */
HAL_StatusTypeDef BME280_CompensateHumidity(BME280_t *dev);

/**
 * @brief   Compensates temperature and optionally pressure/humidity if not skipped
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Compensation successful
 * @retval  HAL_ERROR  Compensation step failed
 */
HAL_StatusTypeDef BME280_CompensateAll(BME280_t *dev);

/* ============================================================================
 * Public API — Convenience
 * ============================================================================ */

/**
 * @brief   Blocking read, parse and compensate for temperature only
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Temperature available in data.temperature
 * @retval  HAL_ERROR  Read/parse/compensate failure
 */
HAL_StatusTypeDef BME280_GetTemperature(BME280_t *dev);

/**
 * @brief   Blocking read of pressure with temperature compensation for t_fine
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Pressure available in data.pressure
 * @retval  HAL_ERROR  Read/parse/compensate failure
 * @warning Compensates using existing raw_temperature in the handle
 *          (does not re-read temperature ADC). Prefer GetTemperaturePressureHumidity
 *          when both values are needed.
 */
HAL_StatusTypeDef BME280_GetPressure(BME280_t *dev);

/**
 * @brief   Blocking burst read then temperature and humidity compensation
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Humidity available in data.humidity
 * @retval  HAL_ERROR  Read/parse/compensate failure
 */
HAL_StatusTypeDef BME280_GetHumidity(BME280_t *dev);

/**
 * @brief   Blocking burst read and full compensation of T/P/H
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Results in data.temperature / pressure / humidity
 * @retval  HAL_ERROR  Read/parse/compensate failure
 */
HAL_StatusTypeDef BME280_GetTemperaturePressureHumidity(BME280_t *dev);

/**
 * @brief   Runs communication, trim and plausibility self-test
 * @param   dev     Pointer to device handle
 * @param   limits  Optional plausibility limits; NULL uses built-in defaults
 * @retval  BME280_SelfTestResult_t  Detailed result code
 * @note    Soft-resets the device and temporarily applies weather-monitoring settings.
 *          Restores cached settings (and writes them) on success / plausibility fail.
 */
BME280_SelfTestResult_t BME280_RunSelfTest(BME280_t *dev,
                                           const BME280_SelfTestLimits_t *limits);

#endif /* BME280_H */
