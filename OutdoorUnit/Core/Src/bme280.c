/**
 ******************************************************************************
 * @file    bme280.c
 * @brief   BME280 temperature, pressure and humidity sensor driver (I2C)
 * @details Implements register I/O (blocking, DMA and interrupt), calibration
 *          unpacking, datasheet compensation formulas and convenience helpers.
 *
 * BLOCKING usage:
 *   BME280_GetTemperaturePressureHumidity(&dev);
 *
 * DMA / interrupt usage (burst read T/P/H):
 *   uint8_t buf[8];
 *   BME280_ReadRawTemperaturePressureHumidity(&dev, buf, sizeof(buf), BME280_IO_DMA);
 *   // In HAL_I2C_MemRxCpltCallback:
 *   BME280_HandleMemRxCplt(&dev);
 *   BME280_ParseRawTemperaturePressureHumidity(&dev, buf);
 *   BME280_CompensateAll(&dev);
 *
 * IT usage is identical except pass BME280_IO_IT instead of BME280_IO_DMA.
 ******************************************************************************
 */
#include "bme280.h"

/** @brief Invalid / skipped raw pressure ADC sentinel (20-bit) */
#define BME280_RAW_PRESSURE_INVALID   ((int32_t)0x80000)

/** @brief Invalid / skipped raw humidity ADC sentinel (16-bit) */
#define BME280_RAW_HUMIDITY_INVALID   ((int32_t)0x8000)

/** @brief Delay after soft reset before accessing the device (ms) */
#define BME280_SOFT_RESET_DELAY_MS    2U

/** @brief Conversion wait used during self-test forced measurement (ms) */
#define BME280_SELFTEST_CONV_DELAY_MS 7U

/**
 * @brief Datasheet recommended profiles indexed by BME280_Profile_t
 */
static const BME280_Settings_t bme280_profiles[] = {
    [BME280_PROFILE_WEATHER_MONITORING] = {
        .osrs_h = BME280_OVERSAMPLING_X1,
        .osrs_t = BME280_OVERSAMPLING_X1,
        .osrs_p = BME280_OVERSAMPLING_X1,
        .mode = BME280_MODE_FORCED,
        .standby = BME280_STANDBY_1000_MS,
        .filter = BME280_FILTER_OFF,
    },
    [BME280_PROFILE_HUMIDITY_SENSING] = {
        .osrs_h = BME280_OVERSAMPLING_X1,
        .osrs_t = BME280_OVERSAMPLING_X1,
        .osrs_p = BME280_OVERSAMPLING_SKIPPED,
        .mode = BME280_MODE_FORCED,
        .standby = BME280_STANDBY_1000_MS,
        .filter = BME280_FILTER_OFF,
    },
    [BME280_PROFILE_INDOOR_NAVIGATION] = {
        .osrs_h = BME280_OVERSAMPLING_X1,
        .osrs_t = BME280_OVERSAMPLING_X2,
        .osrs_p = BME280_OVERSAMPLING_X16,
        .mode = BME280_MODE_NORMAL,
        .standby = BME280_STANDBY_0_5_MS,
        .filter = BME280_FILTER_16,
    },
    [BME280_PROFILE_GAMING] = {
        .osrs_h = BME280_OVERSAMPLING_SKIPPED,
        .osrs_t = BME280_OVERSAMPLING_X1,
        .osrs_p = BME280_OVERSAMPLING_X4,
        .mode = BME280_MODE_NORMAL,
        .standby = BME280_STANDBY_0_5_MS,
        .filter = BME280_FILTER_16,
    },
};

/* ============================================================================
 * Private helpers
 * ============================================================================ */

/**
 * @brief   Converts oversampling enum to sample count factor
 * @param   osrs  Oversampling setting
 * @retval  0 if skipped, otherwise 1/2/4/8/16
 */
static uint8_t bme280_OversamplingFactor(BME280_Oversampling osrs)
{
    if (osrs == BME280_OVERSAMPLING_SKIPPED) {
        return 0U;
    }
    return (uint8_t)(1U << (osrs - 1U));
}

/**
 * @brief   Unpacks little-endian unsigned 16-bit value
 * @param   bytes  Pointer to two bytes (LSB, MSB)
 * @retval  Unpacked uint16_t
 */
static uint16_t bme280_Le16u(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[1] << 8) | bytes[0]);
}

/**
 * @brief   Unpacks little-endian signed 16-bit value
 * @param   bytes  Pointer to two bytes (LSB, MSB)
 * @retval  Unpacked int16_t
 */
static int16_t bme280_Le16s(const uint8_t *bytes)
{
    return (int16_t)(((uint16_t)bytes[1] << 8) | bytes[0]);
}

/**
 * @brief   Unpacks 20-bit ADC sample from MSB/LSB/XLSB bytes
 * @param   bytes  Pointer to three consecutive data bytes
 * @retval  20-bit value left-aligned in int32_t
 */
static int32_t bme280_Unpack20(const uint8_t *bytes)
{
    return ((int32_t)bytes[0] << 12) | ((int32_t)bytes[1] << 4) | ((int32_t)bytes[2] >> 4);
}

/**
 * @brief   Unpacks big-endian unsigned 16-bit humidity ADC sample
 * @param   bytes  Pointer to two bytes (MSB, LSB)
 * @retval  16-bit value in int32_t
 */
static int32_t bme280_Unpack16be(const uint8_t *bytes)
{
    return ((int32_t)bytes[0] << 8) | (int32_t)bytes[1];
}

/**
 * @brief   Blocking I2C register read
 * @param   dev     Device handle
 * @param   reg     Start register
 * @param   buffer  Destination buffer
 * @param   size    Byte count
 * @retval  HAL_OK / HAL_ERROR
 */
static HAL_StatusTypeDef bme280_ReadData(BME280_t *dev, BME280_Registers reg,
                                         uint8_t *buffer, uint16_t size)
{
    if ((dev == NULL) || (buffer == NULL)) {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(dev->i2c_handle, dev->address, reg, I2C_MEMADD_SIZE_8BIT,
                            buffer, size, HAL_MAX_DELAY);
}

/**
 * @brief   Blocking I2C single-byte register write
 * @param   dev      Device handle
 * @param   reg      Register address
 * @param   reg_val  Value to write
 * @retval  HAL_OK / HAL_ERROR
 */
static HAL_StatusTypeDef bme280_WriteData(BME280_t *dev, BME280_Registers reg, uint8_t reg_val)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Write(dev->i2c_handle, dev->address, reg, I2C_MEMADD_SIZE_8BIT,
                             &reg_val, 1U, HAL_MAX_DELAY);
}

/**
 * @brief   Marks a DMA/IT transfer as started on the device handle
 * @param   dev  Device handle
 * @retval  HAL_OK if idle, HAL_BUSY if a transfer is already active
 */
static HAL_StatusTypeDef bme280_BeginAsyncIo(BME280_t *dev)
{
    if (dev->io_busy != 0U) {
        return HAL_BUSY;
    }

    dev->io_busy = 1U;
    return HAL_OK;
}

/**
 * @brief   Clears busy flag and invokes optional transfer callback
 * @param   dev     Device handle
 * @param   status  Transfer result passed to the callback
 */
static void bme280_EndAsyncIo(BME280_t *dev, HAL_StatusTypeDef status)
{
    dev->io_busy = 0U;
    if (dev->transfer_cb != NULL) {
        dev->transfer_cb(dev, status);
    }
}

/**
 * @brief   Reads chip ID and checks it against BME280_CHIP_ID
 * @param   dev  Device handle
 * @retval  HAL_OK if ID matches, HAL_ERROR otherwise
 */
static HAL_StatusTypeDef bme280_VerifyChipId(BME280_t *dev)
{
    uint8_t chip_id;

    if (bme280_ReadData(dev, BME280_REG_CHIP_ID, &chip_id, 1U) != HAL_OK) {
        return HAL_ERROR;
    }
    if (chip_id != BME280_CHIP_ID) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

/**
 * @brief   Selects recommended temperature oversampling for a given pressure OSRS
 * @param   osrs_p  Pressure oversampling
 * @retval  BME280_OVERSAMPLING_X2 when pressure is x16, otherwise x1
 */
static BME280_Oversampling bme280_RecommendedTemperatureOversampling(BME280_Oversampling osrs_p)
{
    if (osrs_p == BME280_OVERSAMPLING_X16) {
        return BME280_OVERSAMPLING_X2;
    }
    return BME280_OVERSAMPLING_X1;
}

/**
 * @brief   Writes CTRL_HUM from cached settings
 * @param   dev  Device handle
 * @retval  HAL_OK / HAL_ERROR
 */
static HAL_StatusTypeDef bme280_WriteCtrlHum(BME280_t *dev)
{
    uint8_t ctrl_hum = (uint8_t)(dev->settings.osrs_h & 0x07U);
    return bme280_WriteData(dev, BME280_REG_CTRL_HUM, ctrl_hum);
}

/**
 * @brief   Writes CTRL_MEAS from cached settings
 * @param   dev  Device handle
 * @retval  HAL_OK / HAL_ERROR
 */
static HAL_StatusTypeDef bme280_WriteCtrlMeas(BME280_t *dev)
{
    uint8_t ctrl_meas = (uint8_t)(((dev->settings.osrs_t & 0x07U) << 5) |
                                  ((dev->settings.osrs_p & 0x07U) << 2) |
                                  (dev->settings.mode & 0x03U));
    return bme280_WriteData(dev, BME280_REG_CTRL_MEAS, ctrl_meas);
}

/**
 * @brief   Writes CONFIG from cached settings
 * @param   dev  Device handle
 * @retval  HAL_OK / HAL_ERROR
 */
static HAL_StatusTypeDef bme280_WriteConfig(BME280_t *dev)
{
    uint8_t config = (uint8_t)(((dev->settings.standby & 0x07U) << 5) |
                               ((dev->settings.filter & 0x07U) << 2));
    return bme280_WriteData(dev, BME280_REG_CONFIG, config);
}

/**
 * @brief   Basic sanity check of loaded trim coefficients
 * @param   dev  Device handle
 * @retval  1 if plausible, 0 otherwise
 */
static uint8_t bme280_IsTrimmingPlausible(const BME280_t *dev)
{
    const BME280_Calibration_t *c = &dev->calibration;

    if ((c->dig_T1 == 0U) || (c->dig_P1 == 0U)) {
        return 0U;
    }
    if ((c->dig_H1 == 0U) && (c->dig_H2 == 0)) {
        return 0U;
    }
    return 1U;
}

/**
 * @brief   Computes measurement duration from oversampling factors
 * @param   osrs_t        Temperature sample factor (0 if skipped)
 * @param   osrs_p        Pressure sample factor (0 if skipped)
 * @param   osrs_h        Humidity sample factor (0 if skipped)
 * @param   use_max_time  Non-zero for datasheet max-time formula
 * @retval  Duration in milliseconds
 */
static uint32_t bme280_CalcDurationMs(uint8_t osrs_t, uint8_t osrs_p, uint8_t osrs_h,
                                      uint8_t use_max_time)
{
    uint32_t duration;

    if (use_max_time != 0U) {
        duration = 1250U;
        if (osrs_t != 0U) {
            duration += (uint32_t)(2300U * osrs_t);
        }
        if (osrs_p != 0U) {
            duration += (uint32_t)((2300U * osrs_p) + 575U);
        }
        if (osrs_h != 0U) {
            duration += (uint32_t)((2300U * osrs_h) + 575U);
        }
        return (duration + 999U) / 1000U;
    }

    duration = 1U;
    if (osrs_t != 0U) {
        duration += (uint32_t)(2U * osrs_t);
    }
    if (osrs_p != 0U) {
        duration += (uint32_t)((2U * osrs_p) + 1U) / 2U;
    }
    if (osrs_h != 0U) {
        duration += (uint32_t)((2U * osrs_h) + 1U) / 2U;
    }
    return duration;
}

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
 */
HAL_StatusTypeDef BME280_Init(BME280_t *dev, I2C_HandleTypeDef *i2c_handle, uint8_t address)
{
    if ((dev == NULL) || (i2c_handle == NULL)) {
        return HAL_ERROR;
    }

    dev->i2c_handle = i2c_handle;
    dev->address = (uint8_t)(address << 1);
    dev->io_busy = 0U;
    dev->transfer_cb = NULL;
    dev->calibration.t_fine = 0;
    dev->settings.osrs_h = BME280_OVERSAMPLING_X1;
    dev->settings.osrs_t = BME280_OVERSAMPLING_X1;
    dev->settings.osrs_p = BME280_OVERSAMPLING_X1;
    dev->settings.mode = BME280_MODE_SLEEP;
    dev->settings.standby = BME280_STANDBY_1000_MS;
    dev->settings.filter = BME280_FILTER_OFF;

    if (bme280_VerifyChipId(dev) != HAL_OK) {
        return HAL_ERROR;
    }

    if (BME280_ReadCalibration(dev) != HAL_OK) {
        return HAL_ERROR;
    }

    return BME280_SetProfile(dev, BME280_PROFILE_WEATHER_MONITORING);
}

/**
 * @brief   Performs a soft reset of the sensor
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Reset command written
 * @retval  HAL_ERROR  Null pointer or I2C write failure
 */
HAL_StatusTypeDef BME280_SoftReset(BME280_t *dev)
{
    uint8_t cmd = (uint8_t)BME280_RESET_COMMAND;
    HAL_StatusTypeDef status = bme280_WriteData(dev, BME280_REG_RESET, cmd);

    if (status == HAL_OK) {
        HAL_Delay(BME280_SOFT_RESET_DELAY_MS);
    }
    return status;
}

/**
 * @brief   Reads factory calibration coefficients into the device handle
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Calibration data loaded
 * @retval  HAL_ERROR  Null pointer or I2C read failure
 */
HAL_StatusTypeDef BME280_ReadCalibration(BME280_t *dev)
{
    uint8_t calib_tp[26];
    uint8_t calib_h[7];
    HAL_StatusTypeDef status;
    BME280_Calibration_t *c;

    if (dev == NULL) {
        return HAL_ERROR;
    }

    status = bme280_ReadData(dev, BME280_REG_CALIB_START, calib_tp, sizeof(calib_tp));
    if (status != HAL_OK) {
        return status;
    }

    status = bme280_ReadData(dev, BME280_REG_HUM_CALIB_START, calib_h, sizeof(calib_h));
    if (status != HAL_OK) {
        return status;
    }

    c = &dev->calibration;
    c->dig_T1 = bme280_Le16u(&calib_tp[0]);
    c->dig_T2 = bme280_Le16s(&calib_tp[2]);
    c->dig_T3 = bme280_Le16s(&calib_tp[4]);
    c->dig_P1 = bme280_Le16u(&calib_tp[6]);
    c->dig_P2 = bme280_Le16s(&calib_tp[8]);
    c->dig_P3 = bme280_Le16s(&calib_tp[10]);
    c->dig_P4 = bme280_Le16s(&calib_tp[12]);
    c->dig_P5 = bme280_Le16s(&calib_tp[14]);
    c->dig_P6 = bme280_Le16s(&calib_tp[16]);
    c->dig_P7 = bme280_Le16s(&calib_tp[18]);
    c->dig_P8 = bme280_Le16s(&calib_tp[20]);
    c->dig_P9 = bme280_Le16s(&calib_tp[22]);
    c->dig_H1 = calib_tp[25];

    c->dig_H2 = bme280_Le16s(&calib_h[0]);
    c->dig_H3 = calib_h[2];
    c->dig_H4 = (int16_t)((calib_h[3] << 4) | (calib_h[4] & 0x0FU));
    c->dig_H5 = (int16_t)((calib_h[5] << 4) | (calib_h[4] >> 4));
    c->dig_H6 = (int8_t)calib_h[6];

    return HAL_OK;
}

/* ============================================================================
 * Public API — Configuration
 * ============================================================================ */

/**
 * @brief   Caches humidity oversampling setting
 * @param   dev     Pointer to device handle
 * @param   osrs_h  Humidity oversampling
 * @retval  HAL_OK     Setting stored
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BME280_SetCtrlHum(BME280_t *dev, BME280_Oversampling osrs_h)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    dev->settings.osrs_h = osrs_h;
    return HAL_OK;
}

/**
 * @brief   Caches temperature/pressure oversampling and mode
 * @param   dev     Pointer to device handle
 * @param   osrs_t  Temperature oversampling
 * @param   osrs_p  Pressure oversampling
 * @param   mode    Operating mode
 * @retval  HAL_OK     Settings stored
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BME280_SetCtrlMeas(BME280_t *dev, BME280_Oversampling osrs_t,
                                     BME280_Oversampling osrs_p, BME280_Mode mode)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    dev->settings.osrs_t = osrs_t;
    dev->settings.osrs_p = osrs_p;
    dev->settings.mode = mode;
    return HAL_OK;
}

/**
 * @brief   Caches pressure oversampling and mode with recommended temperature OSRS
 * @param   dev     Pointer to device handle
 * @param   osrs_p  Pressure oversampling
 * @param   mode    Operating mode
 * @retval  HAL_OK     Settings stored
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BME280_SetCtrlMeasSimple(BME280_t *dev, BME280_Oversampling osrs_p,
                                           BME280_Mode mode)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    dev->settings.osrs_t = bme280_RecommendedTemperatureOversampling(osrs_p);
    dev->settings.osrs_p = osrs_p;
    dev->settings.mode = mode;
    return HAL_OK;
}

/**
 * @brief   Caches and immediately writes standby time and IIR filter
 * @param   dev      Pointer to device handle
 * @param   standby  Standby duration (normal mode)
 * @param   filter   IIR filter coefficient
 * @retval  HAL_OK     CONFIG register written
 * @retval  HAL_ERROR  Null pointer or I2C write failure
 */
HAL_StatusTypeDef BME280_SetConfig(BME280_t *dev, BME280_StandbyTime standby, BME280_Filter filter)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    dev->settings.standby = standby;
    dev->settings.filter = filter;
    return bme280_WriteConfig(dev);
}

/**
 * @brief   Updates mode in cache and CTRL_MEAS register (read-modify-write)
 * @param   dev   Pointer to device handle
 * @param   mode  New operating mode
 * @retval  HAL_OK     Mode applied
 * @retval  HAL_ERROR  Null pointer or I2C failure
 */
HAL_StatusTypeDef BME280_SetMode(BME280_t *dev, BME280_Mode mode)
{
    uint8_t ctrl_meas;

    if (dev == NULL) {
        return HAL_ERROR;
    }

    dev->settings.mode = mode;
    if (bme280_ReadData(dev, BME280_REG_CTRL_MEAS, &ctrl_meas, 1U) != HAL_OK) {
        return HAL_ERROR;
    }

    ctrl_meas = (uint8_t)((ctrl_meas & 0xFCU) | (mode & 0x03U));
    return bme280_WriteData(dev, BME280_REG_CTRL_MEAS, ctrl_meas);
}

/**
 * @brief   Writes cached settings to CTRL_HUM, CTRL_MEAS and CONFIG
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     All registers written
 * @retval  HAL_ERROR  Null pointer or I2C write failure
 */
HAL_StatusTypeDef BME280_ApplySettings(BME280_t *dev)
{
    HAL_StatusTypeDef status;

    if (dev == NULL) {
        return HAL_ERROR;
    }

    status = bme280_WriteCtrlHum(dev);
    if (status != HAL_OK) {
        return status;
    }

    status = bme280_WriteCtrlMeas(dev);
    if (status != HAL_OK) {
        return status;
    }

    return bme280_WriteConfig(dev);
}

/**
 * @brief   Applies a datasheet recommended profile and writes settings to the device
 * @param   dev      Pointer to device handle
 * @param   profile  Profile selector
 * @retval  HAL_OK     Profile applied
 * @retval  HAL_ERROR  Null pointer, invalid profile, or I2C write failure
 */
HAL_StatusTypeDef BME280_SetProfile(BME280_t *dev, BME280_Profile_t profile)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }
    if ((unsigned)profile >= (sizeof(bme280_profiles) / sizeof(bme280_profiles[0]))) {
        return HAL_ERROR;
    }

    dev->settings = bme280_profiles[profile];
    return BME280_ApplySettings(dev);
}

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
HAL_StatusTypeDef BME280_GetStatus(BME280_t *dev, uint8_t *measuring, uint8_t *im_update)
{
    uint8_t status_reg;

    if ((dev == NULL) || (measuring == NULL) || (im_update == NULL)) {
        return HAL_ERROR;
    }

    if (bme280_ReadData(dev, BME280_REG_STATUS, &status_reg, 1U) != HAL_OK) {
        return HAL_ERROR;
    }

    return BME280_ParseStatus(&status_reg, measuring, im_update);
}

/**
 * @brief   Polls until measurement completes or timeout expires
 * @param   dev         Pointer to device handle
 * @param   timeout_ms  Maximum wait time in milliseconds
 * @retval  HAL_OK       Measurement finished
 * @retval  HAL_ERROR    Null pointer or status read failure
 * @retval  HAL_TIMEOUT  Still measuring after timeout
 */
HAL_StatusTypeDef BME280_WaitForMeasurement(BME280_t *dev, uint32_t timeout_ms)
{
    uint8_t measuring = 1U;
    uint8_t im_update = 0U;
    uint32_t start = HAL_GetTick();

    if (dev == NULL) {
        return HAL_ERROR;
    }

    while (measuring != 0U) {
        if (BME280_GetStatus(dev, &measuring, &im_update) != HAL_OK) {
            return HAL_ERROR;
        }

        if (measuring == 0U) {
            return HAL_OK;
        }
        if ((HAL_GetTick() - start) >= timeout_ms) {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

/**
 * @brief   Estimates measurement duration from current oversampling settings
 * @param   dev           Pointer to device handle
 * @param   use_max_time  Non-zero: datasheet max-time formula; zero: typical-time formula
 * @retval  Duration in milliseconds, or 0 if @p dev is NULL
 */
uint32_t BME280_GetMeasurementDurationMs(const BME280_t *dev, uint8_t use_max_time)
{
    if (dev == NULL) {
        return 0U;
    }

    return bme280_CalcDurationMs(bme280_OversamplingFactor(dev->settings.osrs_t),
                                 bme280_OversamplingFactor(dev->settings.osrs_p),
                                 bme280_OversamplingFactor(dev->settings.osrs_h),
                                 use_max_time);
}

/* ============================================================================
 * Public API — Raw I/O
 * ============================================================================ */

/**
 * @brief   Reads raw register data using blocking, DMA or interrupt I2C
 */
HAL_StatusTypeDef BME280_ReadRawData(BME280_t *dev, BME280_Registers reg, uint8_t *buffer,
                                     uint16_t size, BME280_IoMode mode)
{
    HAL_StatusTypeDef status;

    if ((dev == NULL) || (buffer == NULL)) {
        return HAL_ERROR;
    }

    if (mode == BME280_IO_DMA) {
        status = bme280_BeginAsyncIo(dev);
        if (status != HAL_OK) {
            return status;
        }

        status = HAL_I2C_Mem_Read_DMA(dev->i2c_handle, dev->address, reg, I2C_MEMADD_SIZE_8BIT,
                                      buffer, size);
        if (status != HAL_OK) {
            bme280_EndAsyncIo(dev, status);
        }
        return status;
    }

    if (mode == BME280_IO_IT) {
        status = bme280_BeginAsyncIo(dev);
        if (status != HAL_OK) {
            return status;
        }

        status = HAL_I2C_Mem_Read_IT(dev->i2c_handle, dev->address, reg, I2C_MEMADD_SIZE_8BIT,
                                     buffer, size);
        if (status != HAL_OK) {
            bme280_EndAsyncIo(dev, status);
        }
        return status;
    }

    return bme280_ReadData(dev, reg, buffer, size);
}

/**
 * @brief   Writes a single register using blocking, DMA or interrupt I2C
 */
HAL_StatusTypeDef BME280_WriteRawData(BME280_t *dev, BME280_Registers reg, const uint8_t *value,
                                      BME280_IoMode mode)
{
    HAL_StatusTypeDef status;

    if ((dev == NULL) || (value == NULL)) {
        return HAL_ERROR;
    }

    if (mode == BME280_IO_DMA) {
        status = bme280_BeginAsyncIo(dev);
        if (status != HAL_OK) {
            return status;
        }

        status = HAL_I2C_Mem_Write_DMA(dev->i2c_handle, dev->address, reg, I2C_MEMADD_SIZE_8BIT,
                                       (uint8_t *)value, 1U);
        if (status != HAL_OK) {
            bme280_EndAsyncIo(dev, status);
        }
        return status;
    }

    if (mode == BME280_IO_IT) {
        status = bme280_BeginAsyncIo(dev);
        if (status != HAL_OK) {
            return status;
        }

        status = HAL_I2C_Mem_Write_IT(dev->i2c_handle, dev->address, reg, I2C_MEMADD_SIZE_8BIT,
                                      (uint8_t *)value, 1U);
        if (status != HAL_OK) {
            bme280_EndAsyncIo(dev, status);
        }
        return status;
    }

    return HAL_I2C_Mem_Write(dev->i2c_handle, dev->address, reg, I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)value, 1U, HAL_MAX_DELAY);
}

/**
 * @brief   Returns whether a DMA/IT transfer is in progress on this handle
 */
uint8_t BME280_IsBusy(const BME280_t *dev)
{
    if (dev == NULL) {
        return 0U;
    }

    return dev->io_busy;
}

/**
 * @brief   Registers an optional callback for DMA/IT transfer completion
 */
void BME280_SetTransferCallback(BME280_t *dev, BME280_TransferCallback_t cb)
{
    if (dev == NULL) {
        return;
    }

    dev->transfer_cb = cb;
}

/**
 * @brief   Call from HAL_I2C_MemRxCpltCallback when a BME280 DMA/IT read finishes
 */
void BME280_HandleMemRxCplt(BME280_t *dev)
{
    if (dev == NULL) {
        return;
    }

    bme280_EndAsyncIo(dev, HAL_OK);
}

/**
 * @brief   Call from HAL_I2C_MemTxCpltCallback when a BME280 DMA/IT write finishes
 */
void BME280_HandleMemTxCplt(BME280_t *dev)
{
    if (dev == NULL) {
        return;
    }

    bme280_EndAsyncIo(dev, HAL_OK);
}

/**
 * @brief   Call from HAL_I2C_ErrorCallback when a BME280 DMA/IT transfer fails
 */
void BME280_HandleError(BME280_t *dev)
{
    if (dev == NULL) {
        return;
    }

    bme280_EndAsyncIo(dev, HAL_ERROR);
}

/**
 * @brief   Parses a STATUS register byte into measuring / im_update flags
 */
HAL_StatusTypeDef BME280_ParseStatus(const uint8_t *status_reg, uint8_t *measuring,
                                     uint8_t *im_update)
{
    if ((status_reg == NULL) || (measuring == NULL) || (im_update == NULL)) {
        return HAL_ERROR;
    }

    *measuring = (*status_reg >> 3) & 0x01U;
    *im_update = *status_reg & 0x01U;
    return HAL_OK;
}

/**
 * @brief   Reads 3 raw temperature bytes starting at TEMP_MSB
 * @param   dev     Pointer to device handle
 * @param   buffer  Destination buffer (at least 3 bytes)
 * @param   size    Buffer capacity in bytes
 * @param   mode    Blocking or DMA transfer mode
 * @retval  HAL_OK     Read successful
 * @retval  HAL_ERROR  Null pointer, buffer too small, or I2C failure
 */
HAL_StatusTypeDef BME280_ReadRawTemperature(BME280_t *dev, uint8_t *buffer, uint16_t size,
                                            BME280_IoMode mode)
{
    if ((dev == NULL) || (buffer == NULL) || (size < 3U)) {
        return HAL_ERROR;
    }

    return BME280_ReadRawData(dev, BME280_REG_TEMP_MSB, buffer, 3U, mode);
}

/**
 * @brief   Reads 3 raw pressure bytes starting at PRESS_MSB
 * @param   dev     Pointer to device handle
 * @param   buffer  Destination buffer (at least 3 bytes)
 * @param   size    Buffer capacity in bytes
 * @param   mode    Blocking or DMA transfer mode
 * @retval  HAL_OK     Read successful
 * @retval  HAL_ERROR  Null pointer, buffer too small, or I2C failure
 */
HAL_StatusTypeDef BME280_ReadRawPressure(BME280_t *dev, uint8_t *buffer, uint16_t size,
                                         BME280_IoMode mode)
{
    if ((dev == NULL) || (buffer == NULL) || (size < 3U)) {
        return HAL_ERROR;
    }

    return BME280_ReadRawData(dev, BME280_REG_PRESS_MSB, buffer, 3U, mode);
}

/**
 * @brief   Reads 2 raw humidity bytes starting at HUM_MSB
 * @param   dev     Pointer to device handle
 * @param   buffer  Destination buffer (at least 2 bytes)
 * @param   size    Buffer capacity in bytes
 * @param   mode    Blocking or DMA transfer mode
 * @retval  HAL_OK     Read successful
 * @retval  HAL_ERROR  Null pointer, buffer too small, or I2C failure
 */
HAL_StatusTypeDef BME280_ReadRawHumidity(BME280_t *dev, uint8_t *buffer, uint16_t size,
                                         BME280_IoMode mode)
{
    if ((dev == NULL) || (buffer == NULL) || (size < 2U)) {
        return HAL_ERROR;
    }

    return BME280_ReadRawData(dev, BME280_REG_HUM_MSB, buffer, 2U, mode);
}

/**
 * @brief   Reads 8 consecutive bytes: pressure, temperature and humidity
 * @param   dev     Pointer to device handle
 * @param   buffer  Destination buffer (at least 8 bytes)
 * @param   size    Buffer capacity in bytes
 * @param   mode    Blocking or DMA transfer mode
 * @retval  HAL_OK     Read successful
 * @retval  HAL_ERROR  Null pointer, buffer too small, or I2C failure
 */
HAL_StatusTypeDef BME280_ReadRawTemperaturePressureHumidity(BME280_t *dev, uint8_t *buffer,
                                                            uint16_t size, BME280_IoMode mode)
{
    if ((dev == NULL) || (buffer == NULL) || (size < 8U)) {
        return HAL_ERROR;
    }

    return BME280_ReadRawData(dev, BME280_REG_PRESS_MSB, buffer, 8U, mode);
}

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
HAL_StatusTypeDef BME280_ParseRawTemperature(BME280_t *dev, const uint8_t *buffer)
{
    if ((dev == NULL) || (buffer == NULL)) {
        return HAL_ERROR;
    }

    dev->data.raw_temperature = bme280_Unpack20(buffer);
    return HAL_OK;
}

/**
 * @brief   Parses 3-byte pressure frame into raw_pressure
 * @param   dev     Pointer to device handle
 * @param   buffer  Raw pressure bytes (MSB, LSB, XLSB)
 * @retval  HAL_OK     Parsed successfully
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BME280_ParseRawPressure(BME280_t *dev, const uint8_t *buffer)
{
    if ((dev == NULL) || (buffer == NULL)) {
        return HAL_ERROR;
    }

    dev->data.raw_pressure = bme280_Unpack20(buffer);
    return HAL_OK;
}

/**
 * @brief   Parses 2-byte humidity frame into raw_humidity
 * @param   dev     Pointer to device handle
 * @param   buffer  Raw humidity bytes (MSB, LSB)
 * @retval  HAL_OK     Parsed successfully
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BME280_ParseRawHumidity(BME280_t *dev, const uint8_t *buffer)
{
    if ((dev == NULL) || (buffer == NULL)) {
        return HAL_ERROR;
    }

    dev->data.raw_humidity = bme280_Unpack16be(buffer);
    return HAL_OK;
}

/**
 * @brief   Parses 8-byte burst frame into raw pressure, temperature and humidity
 * @param   dev     Pointer to device handle
 * @param   buffer  Burst data starting at PRESS_MSB
 * @retval  HAL_OK     Parsed successfully
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BME280_ParseRawTemperaturePressureHumidity(BME280_t *dev, const uint8_t *buffer)
{
    if ((dev == NULL) || (buffer == NULL)) {
        return HAL_ERROR;
    }

    dev->data.raw_pressure = bme280_Unpack20(&buffer[0]);
    dev->data.raw_temperature = bme280_Unpack20(&buffer[3]);
    dev->data.raw_humidity = bme280_Unpack16be(&buffer[6]);
    return HAL_OK;
}

/**
 * @brief   Compensates temperature and updates t_fine and data.temperature
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Compensation successful
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BME280_CompensateTemperature(BME280_t *dev)
{
    int32_t var1;
    int32_t var2;
    int32_t temperature;

    if (dev == NULL) {
        return HAL_ERROR;
    }

    var1 = (((dev->data.raw_temperature >> 3) - ((int32_t)dev->calibration.dig_T1 << 1)) *
            (int32_t)dev->calibration.dig_T2) >> 11;
    var2 = (((((dev->data.raw_temperature >> 4) - (int32_t)dev->calibration.dig_T1) *
              ((dev->data.raw_temperature >> 4) - (int32_t)dev->calibration.dig_T1)) >> 12) *
            (int32_t)dev->calibration.dig_T3) >> 14;

    dev->calibration.t_fine = var1 + var2;
    temperature = ((dev->calibration.t_fine * 5) + 128) >> 8;
    dev->data.temperature = (float)temperature / 100.0f;
    return HAL_OK;
}

/**
 * @brief   Compensates pressure into data.pressure (hPa)
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Compensation successful
 * @retval  HAL_ERROR  Null pointer or division by zero in trim math
 */
HAL_StatusTypeDef BME280_CompensatePressure(BME280_t *dev)
{
    int64_t var1;
    int64_t var2;
    int64_t pressure;
    int64_t p;

    if (dev == NULL) {
        return HAL_ERROR;
    }

    var1 = ((int64_t)dev->calibration.t_fine) - 128000LL;
    var2 = var1 * var1 * (int64_t)dev->calibration.dig_P6;
    var2 = var2 + ((var1 * (int64_t)dev->calibration.dig_P5) << 17);
    var2 = var2 + (((int64_t)dev->calibration.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)dev->calibration.dig_P3) >> 8) +
           ((var1 * (int64_t)dev->calibration.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * (int64_t)dev->calibration.dig_P1) >> 33;

    if (var1 == 0LL) {
        return HAL_ERROR;
    }

    p = 1048576LL - (int64_t)dev->data.raw_pressure;
    p = (((p << 31) - var2) * 3125LL) / var1;
    var1 = (((int64_t)dev->calibration.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)dev->calibration.dig_P8) * p) >> 19;
    pressure = ((p + var1 + var2) >> 8) + (((int64_t)dev->calibration.dig_P7) << 4);
    dev->data.pressure = (float)pressure / 25600.0f;
    return HAL_OK;
}

/**
 * @brief   Compensates humidity into data.humidity (%RH)
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Compensation successful
 * @retval  HAL_ERROR  Null pointer
 */
HAL_StatusTypeDef BME280_CompensateHumidity(BME280_t *dev)
{
    int32_t var_h;

    if (dev == NULL) {
        return HAL_ERROR;
    }

    var_h = dev->calibration.t_fine - 76800;
    var_h = (((((dev->data.raw_humidity << 14) -
                 (((int32_t)dev->calibration.dig_H4) << 20) -
                 (((int32_t)dev->calibration.dig_H5) * var_h)) +
                16384) >> 15) *
             (((((((var_h * (int32_t)dev->calibration.dig_H6) >> 10) *
                  (((var_h * (int32_t)dev->calibration.dig_H3) >> 11) + 32768)) >>
                 10) +
                2097152) *
                   (int32_t)dev->calibration.dig_H2 +
               8192) >>
              14));

    var_h -= (((((var_h >> 15) * (var_h >> 15)) >> 7) * (int32_t)dev->calibration.dig_H1) >> 4);
    if (var_h < 0) {
        var_h = 0;
    } else if (var_h > 419430400) {
        var_h = 419430400;
    }

    dev->data.humidity = (float)(var_h >> 12) / 1024.0f;
    return HAL_OK;
}

/**
 * @brief   Compensates temperature and optionally pressure/humidity if not skipped
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Compensation successful
 * @retval  HAL_ERROR  Compensation step failed
 */
HAL_StatusTypeDef BME280_CompensateAll(BME280_t *dev)
{
    if (BME280_CompensateTemperature(dev) != HAL_OK) {
        return HAL_ERROR;
    }
    if (dev->settings.osrs_p != BME280_OVERSAMPLING_SKIPPED) {
        if (BME280_CompensatePressure(dev) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    if (dev->settings.osrs_h != BME280_OVERSAMPLING_SKIPPED) {
        if (BME280_CompensateHumidity(dev) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    return HAL_OK;
}

/* ============================================================================
 * Public API — Convenience
 * ============================================================================ */

/**
 * @brief   Blocking read, parse and compensate for temperature only
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Temperature available in data.temperature
 * @retval  HAL_ERROR  Read/parse/compensate failure
 */
HAL_StatusTypeDef BME280_GetTemperature(BME280_t *dev)
{
    uint8_t buf[3];

    if (BME280_ReadRawTemperature(dev, buf, sizeof(buf), BME280_IO_BLOCKING) != HAL_OK) {
        return HAL_ERROR;
    }
    if (BME280_ParseRawTemperature(dev, buf) != HAL_OK) {
        return HAL_ERROR;
    }
    return BME280_CompensateTemperature(dev);
}

/**
 * @brief   Blocking read of pressure with temperature compensation for t_fine
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Pressure available in data.pressure
 * @retval  HAL_ERROR  Read/parse/compensate failure
 */
HAL_StatusTypeDef BME280_GetPressure(BME280_t *dev)
{
    uint8_t buf[3];

    if (BME280_ReadRawPressure(dev, buf, sizeof(buf), BME280_IO_BLOCKING) != HAL_OK) {
        return HAL_ERROR;
    }
    if (BME280_ParseRawPressure(dev, buf) != HAL_OK) {
        return HAL_ERROR;
    }
    if (BME280_CompensateTemperature(dev) != HAL_OK) {
        return HAL_ERROR;
    }
    return BME280_CompensatePressure(dev);
}

/**
 * @brief   Blocking burst read then temperature and humidity compensation
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Humidity available in data.humidity
 * @retval  HAL_ERROR  Read/parse/compensate failure
 */
HAL_StatusTypeDef BME280_GetHumidity(BME280_t *dev)
{
    uint8_t buf[8];

    if (BME280_ReadRawTemperaturePressureHumidity(dev, buf, sizeof(buf), BME280_IO_BLOCKING) != HAL_OK) {
        return HAL_ERROR;
    }
    if (BME280_ParseRawTemperaturePressureHumidity(dev, buf) != HAL_OK) {
        return HAL_ERROR;
    }
    if (BME280_CompensateTemperature(dev) != HAL_OK) {
        return HAL_ERROR;
    }
    return BME280_CompensateHumidity(dev);
}

/**
 * @brief   Blocking burst read and full compensation of T/P/H
 * @param   dev  Pointer to device handle
 * @retval  HAL_OK     Results in data.temperature / pressure / humidity
 * @retval  HAL_ERROR  Read/parse/compensate failure
 */
HAL_StatusTypeDef BME280_GetTemperaturePressureHumidity(BME280_t *dev)
{
    uint8_t buf[8];

    if (BME280_ReadRawTemperaturePressureHumidity(dev, buf, sizeof(buf), BME280_IO_BLOCKING) != HAL_OK) {
        return HAL_ERROR;
    }
    if (BME280_ParseRawTemperaturePressureHumidity(dev, buf) != HAL_OK) {
        return HAL_ERROR;
    }
    return BME280_CompensateAll(dev);
}

/**
 * @brief   Runs communication, trim and plausibility self-test
 * @param   dev     Pointer to device handle
 * @param   limits  Optional plausibility limits; NULL uses built-in defaults
 * @retval  BME280_SelfTestResult_t  Detailed result code
 */
BME280_SelfTestResult_t BME280_RunSelfTest(BME280_t *dev, const BME280_SelfTestLimits_t *limits)
{
    BME280_SelfTestLimits_t default_limits = {
        .temp_min_c = 0.0f,
        .temp_max_c = 40.0f,
        .press_min_hpa = 900.0f,
        .press_max_hpa = 1100.0f,
        .hum_min_pct = 20.0f,
        .hum_max_pct = 80.0f,
    };
    const BME280_SelfTestLimits_t *test_limits = (limits != NULL) ? limits : &default_limits;
    BME280_Settings_t saved_settings;

    if (dev == NULL) {
        return BME280_SELFTEST_COMM_ERROR;
    }

    saved_settings = dev->settings;

    if (BME280_SoftReset(dev) != HAL_OK) {
        return BME280_SELFTEST_COMM_ERROR;
    }

    if (bme280_VerifyChipId(dev) != HAL_OK) {
        return BME280_SELFTEST_COMM_ERROR;
    }

    if (BME280_ReadCalibration(dev) != HAL_OK) {
        return BME280_SELFTEST_COMM_ERROR;
    }
    if (bme280_IsTrimmingPlausible(dev) == 0U) {
        return BME280_SELFTEST_TRIM_ERROR;
    }

    dev->settings = bme280_profiles[BME280_PROFILE_WEATHER_MONITORING];

    if (BME280_ApplySettings(dev) != HAL_OK) {
        return BME280_SELFTEST_COMM_ERROR;
    }

    HAL_Delay(BME280_SELFTEST_CONV_DELAY_MS);
    if (BME280_WaitForMeasurement(dev, BME280_SELFTEST_CONV_DELAY_MS + 10U) != HAL_OK) {
        return BME280_SELFTEST_COMM_ERROR;
    }

    if (BME280_GetTemperaturePressureHumidity(dev) != HAL_OK) {
        return BME280_SELFTEST_COMM_ERROR;
    }

    if ((dev->data.raw_temperature <= 0) ||
        (dev->data.raw_temperature >= BME280_RAW_PRESSURE_INVALID)) {
        return BME280_SELFTEST_TEMP_BOND_ERROR;
    }
    if ((dev->data.raw_pressure <= 0) ||
        (dev->data.raw_pressure >= BME280_RAW_PRESSURE_INVALID)) {
        return BME280_SELFTEST_PRESS_BOND_ERROR;
    }

    if ((dev->data.temperature < test_limits->temp_min_c) ||
        (dev->data.temperature > test_limits->temp_max_c)) {
        dev->settings = saved_settings;
        return BME280_SELFTEST_TEMP_PLAUS_ERROR;
    }
    if ((dev->data.pressure < test_limits->press_min_hpa) ||
        (dev->data.pressure > test_limits->press_max_hpa)) {
        dev->settings = saved_settings;
        return BME280_SELFTEST_PRESS_PLAUS_ERROR;
    }
    if ((dev->data.raw_humidity != BME280_RAW_HUMIDITY_INVALID) &&
        ((dev->data.humidity < test_limits->hum_min_pct) ||
         (dev->data.humidity > test_limits->hum_max_pct))) {
        dev->settings = saved_settings;
        return BME280_SELFTEST_HUM_PLAUS_ERROR;
    }

    dev->settings = saved_settings;
    (void)BME280_ApplySettings(dev);
    return BME280_SELFTEST_OK;
}
