/**
 ******************************************************************************
 * @file    bmp280.c
 * @brief   BMP280 temperature and pressure sensor driver with unified blocking/DMA support
 * 
 * This driver supports both blocking and DMA I2C operations through a unified API.
 * 
 * BLOCKING MODE Usage Example:
 *   BMP280_GetTemperatureAndPressure(&bmp);  // Single call for both
 *   // or individually:
 *   BMP280_GetTemperature(&bmp);
 *   BMP280_GetPressure(&bmp);
 * 
 * DMA MODE Usage Example:
 *   // 1. Start DMA read
 *   uint8_t buffer[6];
 *   BMP280_ReadRawTemperaturePressure(&bmp, buffer, 6, BMP280_IO_DMA);
 * 
 *   // 2. In I2C DMA complete callback (HAL_I2C_MemRxCpltCallback):
 *   BMP280_ParseRawTemperaturePressure(&bmp, buffer);
 *   BMP280_CompensateTemperatureAndPressure(&bmp);
 *   // Now bmp.data.temperature and bmp.data.pressure contain valid readings
 * 
 ******************************************************************************
 */
#include "bmp280.h"


static HAL_StatusTypeDef bmp280_ReadData(BMP280_t *dev, BMP280_Registers reg, uint8_t *buffer, uint16_t size)
{
	if( NULL == dev || NULL == buffer)
		return HAL_ERROR;

	return HAL_I2C_Mem_Read(dev->i2c_handle, dev->address, reg, I2C_MEMADD_SIZE_8BIT, buffer, size, HAL_MAX_DELAY);
}


static HAL_StatusTypeDef bmp280_WriteData(BMP280_t *dev, BMP280_Registers reg, uint8_t *reg_val)
{
	if( NULL == dev || reg_val == NULL)
		return HAL_ERROR;

	return HAL_I2C_Mem_Write(dev->i2c_handle, dev->address, reg, I2C_MEMADD_SIZE_8BIT, reg_val, 1, HAL_MAX_DELAY);
}


/**
 * @brief Initializes the BMP280 sensor.
 *
 * This function sets up the I2C communication, determines the sensor's I2C address based on the SDO pin state,
 * verifies the chip ID, performs a soft reset, and reads the calibration parameters.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param i2c_handle Pointer to the I2C handle.
 * @param sdo_state State of the SDO pin (0 or 1) to determine the I2C address (0x76 or 0x77).
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_Init(BMP280_t *dev, I2C_HandleTypeDef *i2c_handle, uint8_t addres)
{
	if( NULL == dev || NULL == i2c_handle)
		return HAL_ERROR;

    dev->i2c_handle = i2c_handle;
    dev->address = addres << 1;
    dev->calibration.t_fine = 0;

    uint8_t chip_id;
    HAL_StatusTypeDef status = bmp280_ReadData(dev, BMP280_REG_CHIP_ID, &chip_id, 1);


    if (status != HAL_OK || chip_id != BMP280_CHIP_ID) 
		return HAL_ERROR;

	status = BMP280_ReadCalibration(dev);						// Read calibration data

	if (status != HAL_OK)
		return status;

	status = BMP280_OperationMode(dev, BMP280_OPERATION_1);		// Set sensor in operation mode 1. Lets call it default mode

	if (status != HAL_OK)
		return status;

	// Default settings
	status = BMP280_SetCtrlMeas(dev, BMP280_OVERSAMPLING_X16, BMP280_MODE_NORMAL);
	if (status != HAL_OK)
		return status;
	status = BMP280_SetConfig(dev, BMP280_STANDBY_500_MS, BMP280_FILTER_16);

	return status;
}

/**
 * @brief Performs a soft reset on the BMP280 sensor.
 *
 * Writes the reset command (0xB6) to the reset register (0xE0) to reset the sensor.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_SoftReset(BMP280_t *dev)
{
	uint8_t cmd = (uint8_t)BMP280_RESET_COMMAND;
	return bmp280_WriteData(dev, BMP280_REG_RESET, &cmd);
}

/**
 * @brief Reads the calibration parameters from the BMP280 sensor.
 *
 * Reads 24 bytes of calibration calibration starting from register 0x88 and stores them in the handle structure.
 * These parameters are used for temperature and pressure compensation.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_ReadCalibration(BMP280_t *dev)
{
    uint8_t calib_calibration[24];
    HAL_StatusTypeDef status = bmp280_ReadData(dev, BMP280_REG_CALIB_START, calib_calibration, 24);

    if (status != HAL_OK)
        return status;

    dev->calibration.dig_T1 = (uint16_t)((calib_calibration[1] << 8) | calib_calibration[0]);
    dev->calibration.dig_T2 = (int16_t)((calib_calibration[3] << 8) | calib_calibration[2]);
    dev->calibration.dig_T3 = (int16_t)((calib_calibration[5] << 8) | calib_calibration[4]);
    dev->calibration.dig_P1 = (uint16_t)((calib_calibration[7] << 8) | calib_calibration[6]);
    dev->calibration.dig_P2 = (int16_t)((calib_calibration[9] << 8) | calib_calibration[8]);
    dev->calibration.dig_P3 = (int16_t)((calib_calibration[11] << 8) | calib_calibration[10]);
    dev->calibration.dig_P4 = (int16_t)((calib_calibration[13] << 8) | calib_calibration[12]);
    dev->calibration.dig_P5 = (int16_t)((calib_calibration[15] << 8) | calib_calibration[14]);
    dev->calibration.dig_P6 = (int16_t)((calib_calibration[17] << 8) | calib_calibration[16]);
    dev->calibration.dig_P7 = (int16_t)((calib_calibration[19] << 8) | calib_calibration[18]);
    dev->calibration.dig_P8 = (int16_t)((calib_calibration[21] << 8) | calib_calibration[20]);
    dev->calibration.dig_P9 = (int16_t)((calib_calibration[23] << 8) | calib_calibration[22]);

    return HAL_OK;
}

/**
 * @brief Sets the configuration register of the BMP280 sensor.
 *
 * Configures the standby time, IIR filter coefficient, and SPI 3-wire mode (disabled for I2C).
 * Corresponds to register 0xF5 "config".
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param standby Standby time in normal mode.
 * @param filter IIR filter coefficient.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_SetConfig(BMP280_t *dev, BMP280_StandbyTime standby, BMP280_Filter filter)
{
    uint8_t config = ((standby & 0x07) << 5) | ((filter & 0x07) << 2) | 0x00;

    return bmp280_WriteData(dev, BMP280_REG_CONFIG, &config);
}

/**
 * @brief Sets the control measurement register of the BMP280 sensor.
 *
 * Configures the oversampling for temperature and pressure, and the operating mode.
 * Corresponds to register 0xF4 "ctrl_meas".
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param OVERSAMPLING_t Temperature oversampling.
 * @param OVERSAMPLING_p Pressure oversampling.
 * @param mode Operating mode (sleep, forced, normal).
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_SetCtrlMeas(BMP280_t *dev, BMP280_Oversampling OVERSAMPLING_p, BMP280_Mode mode)
{
	if(NULL == dev)
		return HAL_ERROR;
/*
 * Recomennded values for temperature oversampling based on pressure oversampling setting. Table 4 in doc
 */
	uint8_t ctrl;
	switch (OVERSAMPLING_p)
	{
		case BMP280_OVERSAMPLING_X16:
			ctrl = ((BMP280_OVERSAMPLING_X2 & 0x07) << 5) | ((OVERSAMPLING_p & 0x07) << 2) | (mode & 0x03);
			break;

		default:
			ctrl = ((BMP280_OVERSAMPLING_X1 & 0x07) << 5) | ((OVERSAMPLING_p & 0x07) << 2) | (mode & 0x03);
			break;
	}

	return bmp280_WriteData(dev, BMP280_REG_CTRL_MEAS, &ctrl);
}

/**
 * @brief Sets mode of BMP280
 * @param dev Pointer to the BMP280 handle structure.
 * @param mode Operating mode (sleep, forced, normal).
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_SetMode(BMP280_t *dev, BMP280_Mode mode)
{
	if( NULL == dev)
		return HAL_ERROR;

	uint8_t Tmp;


	HAL_StatusTypeDef status =  bmp280_ReadData(dev, BMP280_REG_CTRL_MEAS, &Tmp, 1);
	if(HAL_OK != status)
		return HAL_ERROR;

	Tmp = Tmp & 0xFC; // Tmp (xxxx xx00)
	Tmp |= mode & 0x03;

	status = bmp280_WriteData(dev, BMP280_REG_CTRL_MEAS, &Tmp);

	return status;
}

/**
 * @brief Set recomended measurment settings for BMP280 sensor.
 *
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param One of 6 operation avalible
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_OperationMode(BMP280_t *dev, BMP280_Operation_t operation)
{
	if( NULL == dev)
		return HAL_ERROR;

	uint8_t ctrl;
	uint8_t config;
	HAL_StatusTypeDef status;

//TODO: Dodać pobranie stanu rejestrów, potem wyczyscic je tak, zeby zapobiec błednemu wpisywaniu danych
	switch(operation)
		{
			case BMP280_OPERATION_0:
				ctrl = ((BMP280_OVERSAMPLING_X2 & 0x07) << 5) | ((BMP280_OVERSAMPLING_X16 & 0x07) << 2) | (BMP280_MODE_NORMAL & 0x03);
				config = ((BMP280_STANDBY_62_5_MS & 0x07) << 5) | ((BMP280_FILTER_4 & 0x07) << 2) | 0x00 ;
			break;

			case BMP280_OPERATION_1:
				ctrl = ((BMP280_OVERSAMPLING_X1 & 0x07) << 5) | ((BMP280_OVERSAMPLING_X4 & 0x07) << 2) | (BMP280_MODE_NORMAL & 0x03);
				config = ((BMP280_STANDBY_0_5_MS & 0x07) << 5) | ((BMP280_FILTER_16 & 0x07) << 2) | 0x00 ;
			break;
				// TODO: ten chyba trzeba wyjebac bo działa tylko w trybie FORCED
			case BMP280_OPERATION_2:
				ctrl = ((BMP280_OVERSAMPLING_X1 & 0x07) << 5) | ((BMP280_OVERSAMPLING_X1 & 0x07) << 2) | (BMP280_MODE_FORCED & 0x03);
				config = ((BMP280_FILTER_OFF & 0x07) << 2) | 0x00 ;
			break;

			case BMP280_OPERATION_3:
				ctrl = ((BMP280_OVERSAMPLING_X1 & 0x07) << 5) | ((BMP280_OVERSAMPLING_X4 & 0x07) << 2) | (BMP280_MODE_NORMAL & 0x03);
				config = ((BMP280_STANDBY_125_MS & 0x07) << 5) | ((BMP280_FILTER_4 & 0x07) << 2) | 0x00 ;
			break;

			case BMP280_OPERATION_4:
				ctrl = ((BMP280_OVERSAMPLING_X1 & 0x07) << 5) | ((BMP280_OVERSAMPLING_X2 & 0x07) << 2) | (BMP280_MODE_NORMAL & 0x03);
				config = ((BMP280_STANDBY_0_5_MS & 0x07) << 5) | ((BMP280_FILTER_OFF & 0x07) << 2) | 0x00 ;
			break;

			case BMP280_OPERATION_5:
				ctrl = ((BMP280_OVERSAMPLING_X2 & 0x07) << 5) | ((BMP280_OVERSAMPLING_X16 & 0x07) << 2) | (BMP280_MODE_NORMAL & 0x03);
				config = ((BMP280_STANDBY_0_5_MS & 0x07) << 5) | ((BMP280_FILTER_16 & 0x07) << 2) | 0x00 ;
			break;
		}
	status = bmp280_WriteData(dev, BMP280_REG_CTRL_MEAS, &ctrl);
	status = bmp280_WriteData(dev, BMP280_REG_CONFIG, &config);

	return status;
}
/**
 * @brief Reads the status register of the BMP280 sensor.
 *
 * Reads the status register (0xF3) to check if a measurement is ongoing or if NVM calibration is being copied.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param measuring Pointer to store the measuring status (1 if measuring, 0 otherwise).
 * @param im_update Pointer to store the image update status (1 if updating, 0 otherwise).
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_GetStatus(BMP280_t *dev, uint8_t *measuring, uint8_t *im_update)
{
	if (NULL == dev || NULL == measuring || NULL == im_update)
		return HAL_ERROR;

    uint8_t status_reg;
    HAL_StatusTypeDef status = bmp280_ReadData(dev, BMP280_REG_STATUS, &status_reg, 1);
    if (status == HAL_OK)
    {
        *measuring = (status_reg >> 3) & 0x01;
        *im_update = status_reg & 0x01;
    }
    return status;
}

/**
 * @brief Unified read function supporting both blocking and DMA modes.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param reg Register address to read from.
 * @param buffer Buffer to store read data.
 * @param size Number of bytes to read.
 * @param mode BMP280_IO_BLOCKING or BMP280_IO_DMA.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_ReadRawData(BMP280_t *dev, BMP280_Registers reg, uint8_t *buffer, uint16_t size, BMP280_IoMode mode)
{
	if (NULL == dev || NULL == buffer)
		return HAL_ERROR;

	if (mode == BMP280_IO_DMA)
		return HAL_I2C_Mem_Read_DMA(dev->i2c_handle, dev->address, reg, I2C_MEMADD_SIZE_8BIT, buffer, size);

	return bmp280_ReadData(dev, reg, buffer, size);
}

/**
 * @brief Reads raw temperature data (3 bytes) using specified I/O mode.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param buffer Buffer to store 3 bytes of raw temperature data.
 * @param size Buffer size (must be >= 3).
 * @param mode BMP280_IO_BLOCKING or BMP280_IO_DMA.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_ReadRawTemperature(BMP280_t *dev, uint8_t *buffer, uint16_t size, BMP280_IoMode mode)
{
	if (NULL == dev || NULL == buffer || size < 3)
		return HAL_ERROR;

	return BMP280_ReadRawData(dev, BMP280_REG_TEMP_MSB, buffer, 3, mode);
}

/**
 * @brief Reads raw pressure data (3 bytes) using specified I/O mode.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param buffer Buffer to store 3 bytes of raw pressure data.
 * @param size Buffer size (must be >= 3).
 * @param mode BMP280_IO_BLOCKING or BMP280_IO_DMA.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_ReadRawPressure(BMP280_t *dev, uint8_t *buffer, uint16_t size, BMP280_IoMode mode)
{
	if (NULL == dev || NULL == buffer || size < 3)
		return HAL_ERROR;

	return BMP280_ReadRawData(dev, BMP280_REG_PRESS_MSB, buffer, 3, mode);
}

/**
 * @brief Reads both raw pressure and temperature data (6 bytes) using specified I/O mode.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param buffer Buffer to store 6 bytes (pressure MSB/LSB/XLSB, temp MSB/LSB/XLSB).
 * @param size Buffer size (must be >= 6).
 * @param mode BMP280_IO_BLOCKING or BMP280_IO_DMA.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_ReadRawTemperaturePressure(BMP280_t *dev, uint8_t *buffer, uint16_t size, BMP280_IoMode mode)
{
	if (NULL == dev || NULL == buffer || size < 6)
		return HAL_ERROR;

	return BMP280_ReadRawData(dev, BMP280_REG_PRESS_MSB, buffer, 6, mode);
}

/**
 * @brief Parses 3 bytes of raw temperature data into the device structure.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param buffer Buffer containing 3 bytes of raw temperature data.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_ParseRawTemperature(BMP280_t *dev, const uint8_t *buffer)
{
	if (NULL == dev || NULL == buffer)
		return HAL_ERROR;

	dev->data.raw_temperature = ((int32_t)buffer[0] << 12) | ((int32_t)buffer[1] << 4) | ((int32_t)buffer[2] >> 4);
	return HAL_OK;
}

/**
 * @brief Parses 3 bytes of raw pressure data into the device structure.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param buffer Buffer containing 3 bytes of raw pressure data.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_ParseRawPressure(BMP280_t *dev, const uint8_t *buffer)
{
	if (NULL == dev || NULL == buffer)
		return HAL_ERROR;

	dev->data.raw_pressure = ((int32_t)buffer[0] << 12) | ((int32_t)buffer[1] << 4) | ((int32_t)buffer[2] >> 4);
	return HAL_OK;
}

/**
 * @brief Parses 6 bytes of raw pressure+temperature data into the device structure.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @param buffer Buffer containing 6 bytes (pressure MSB/LSB/XLSB, temp MSB/LSB/XLSB).
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_ParseRawTemperaturePressure(BMP280_t *dev, const uint8_t *buffer)
{
	if (NULL == dev || NULL == buffer)
		return HAL_ERROR;

	dev->data.raw_pressure = ((int32_t)buffer[0] << 12) | ((int32_t)buffer[1] << 4) | ((int32_t)buffer[2] >> 4);
	dev->data.raw_temperature = ((int32_t)buffer[3] << 12) | ((int32_t)buffer[4] << 4) | ((int32_t)buffer[5] >> 4);
	return HAL_OK;
}

/**
 * @brief Reads and compensates temperature in blocking mode.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_GetTemperature(BMP280_t *dev)
{
	uint8_t buf[3];
	if (HAL_OK != BMP280_ReadRawTemperature(dev, buf, sizeof(buf), BMP280_IO_BLOCKING))
		return HAL_ERROR;

	if (HAL_OK != BMP280_ParseRawTemperature(dev, buf))
		return HAL_ERROR;

	return BMP280_CompensateTemperature(dev);
}

/**
 * @brief Reads and compensates pressure in blocking mode.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_GetPressure(BMP280_t *dev)
{
	uint8_t buf[3];
	if (HAL_OK != BMP280_ReadRawPressure(dev, buf, sizeof(buf), BMP280_IO_BLOCKING))
		return HAL_ERROR;

	if (HAL_OK != BMP280_ParseRawPressure(dev, buf))
		return HAL_ERROR;

	return BMP280_CompensatePressure(dev);
}

/**
 * @brief Reads and compensates both temperature and pressure in blocking mode.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_GetTemperatureAndPressure(BMP280_t *dev)
{
	uint8_t buf[6];
	if (HAL_OK != BMP280_ReadRawTemperaturePressure(dev, buf, sizeof(buf), BMP280_IO_BLOCKING))
		return HAL_ERROR;

	if (HAL_OK != BMP280_ParseRawTemperaturePressure(dev, buf))
		return HAL_ERROR;

	return BMP280_CompensateTemperatureAndPressure(dev);
}

/**
 * @brief Compensates raw temperature data using calibration parameters.
 *
 * Uses the raw temperature and calibration parameters to compute the temperature in degrees Celsius.
 * Updates the t_fine value used for pressure compensation.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_CompensateTemperature(BMP280_t *dev)
{
	if (NULL == dev)
		return HAL_ERROR;

	int32_t var1 = ((((dev->data.raw_temperature >> 3) - ((int32_t)dev->calibration.dig_T1 << 1))) * ((int32_t)dev->calibration.dig_T2)) >> 11;
	int32_t var2 = (((((dev->data.raw_temperature >> 4) - ((int32_t)dev->calibration.dig_T1)) * ((dev->data.raw_temperature >> 4) - ((int32_t)dev->calibration.dig_T1))) >> 12) * ((int32_t)dev->calibration.dig_T3)) >> 14;

	dev->calibration.t_fine = var1 + var2;
	int32_t T = ((dev->calibration.t_fine) * 5 + 128) >> 8;

	dev->data.temperature = (float)(T / 100.0);
	return HAL_OK;
}

/**
 * @brief Compensates raw pressure data using calibration parameters.
 *
 * Uses the raw pressure, t_fine from temperature compensation, and calibration parameters
 * to compute the pressure in hPa. Temperature must be compensated first.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_CompensatePressure(BMP280_t *dev)
{
	if (NULL == dev)
		return HAL_ERROR;

	int32_t var1 = (((int32_t)dev->calibration.t_fine) >> 1) - (int32_t)64000;
	int32_t var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((int32_t)dev->calibration.dig_P6);
	var2 = var2 + ((var1 * ((int32_t)dev->calibration.dig_P5)) << 1);
	var2 = (var2 >> 2) + (((int32_t)dev->calibration.dig_P4) << 16);
	var1 = (((dev->calibration.dig_P3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) + ((((int32_t)dev->calibration.dig_P2) * var1) >> 1)) >> 18;
	var1 = ((((32768 + var1)) * ((int32_t)dev->calibration.dig_P1)) >> 15);

	if (var1 == 0)
		return HAL_ERROR;

	uint32_t p = (((uint32_t)(((int32_t)1048576) - dev->data.raw_pressure) - (var2 >> 12))) * 3125;

	if (p < 0x80000000UL)
	{
		p = (p << 1) / ((uint32_t)var1);
	}
	else
	{
		p = (p / (uint32_t)var1) * 2;
	}

	var1 = (((int32_t)dev->calibration.dig_P9) * ((int32_t)(((p >> 3) * (p >> 3)) >> 13))) >> 12;
	var2 = (((int32_t)(p >> 2)) * ((int32_t)dev->calibration.dig_P8)) >> 13;

	p = (uint32_t)((int32_t)p + ((var1 + var2 + dev->calibration.dig_P7) >> 4));
	dev->data.pressure = (float)(p/100.0);

	return HAL_OK;
}

/**
 * @brief Compensates both temperature and pressure.
 *
 * Temperature must be compensated first as it calculates t_fine needed for pressure.
 *
 * @param dev Pointer to the BMP280 handle structure.
 * @return HAL status.
 */
HAL_StatusTypeDef BMP280_CompensateTemperatureAndPressure(BMP280_t *dev)
{
	if (HAL_OK != BMP280_CompensateTemperature(dev))
		return HAL_ERROR;

	return BMP280_CompensatePressure(dev);
}
