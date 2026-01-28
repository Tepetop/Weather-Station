#include "si7021.h"

/**
 * @brief Read sensor register
 * @param hsi7021 Pointer to the configuration structure
 * @param reg_cmd Register read command or measurement command (e.g. SI7021_CMD_READ_USER_REG1, SI7021_CMD_MEASURE_RH_HOLD)
 * @param value Pointer to buffer storing read data
 * @param size Number of bytes to read
 * @return Operation status
 */
HAL_StatusTypeDef Si7021_ReadRegister(Si7021_t *hsi7021, Si7021_Command_t reg_cmd, uint8_t *value, uint8_t size)
{
    if (hsi7021 == NULL || value == NULL)
        return HAL_ERROR;

    // Dla komend dwubajtowych (EID i firmware)
    if (reg_cmd == SI7021_CMD_READ_EID_1ST || reg_cmd == SI7021_CMD_READ_EID_2ND || reg_cmd == SI7021_CMD_READ_FIRMWARE)
    {
        uint8_t cmd[2] = {(reg_cmd >> 8) & 0xFF, reg_cmd & 0xFF};
        HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(hsi7021->hi2c, hsi7021->address, cmd, 2, HAL_MAX_DELAY);
        if (status != HAL_OK)
            return status;
        return HAL_I2C_Master_Receive(hsi7021->hi2c, hsi7021->address | 1, value, size, HAL_MAX_DELAY);
    }

    // Dla komend jednobajtowych (rejestry i pomiary)
    uint8_t cmd = (uint8_t)reg_cmd;
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(hsi7021->hi2c, hsi7021->address, &cmd, 1, HAL_MAX_DELAY);
    if (status != HAL_OK)
        return status;

    return HAL_I2C_Master_Receive(hsi7021->hi2c, hsi7021->address | 1, value, size, HAL_MAX_DELAY);
}

/**
 * @brief Write to sensor register
 * @param hsi7021 Pointer to the configuration structure
 * @param reg_cmd Register write command (e.g. SI7021_CMD_WRITE_USER_REG1)
 * @param value Value to write
 * @return Operation status
 */
HAL_StatusTypeDef Si7021_WriteRegister(Si7021_t *hsi7021, Si7021_Command_t reg_cmd, uint8_t value)
{
    return HAL_I2C_Mem_Write(hsi7021->hi2c, hsi7021->address, reg_cmd, 1, &value, 1, HAL_MAX_DELAY);
}

/**
 * @brief Read factory settings
 * @param hsi7021 Pointer to the configuration structure
 * @return Operation status
 */
HAL_StatusTypeDef Si7021_ReadFirmware(Si7021_t *hsi7021)
{
	return Si7021_ReadRegister(hsi7021, SI7021_CMD_READ_FIRMWARE, &(hsi7021->firmware), 2);
}

/**
 * @brief Software reset of sensor to factory settings
 * @param hsi7021 Pointer to the configuration structure
 * @return Operation status
 */
HAL_StatusTypeDef Si7021_SoftwareReset(Si7021_t *hsi7021)
{
	return Si7021_ReadRegister(hsi7021, SI7021_CMD_RESET, &(hsi7021->firmware), 2);
}

/**
 * @brief Initialize Si7021 sensor
 * @param hsi7021 Pointer to sensor configuration structure
 * @param hi2c Pointer to I2C handle
 * @param address I2C address of sensor
 * @param resolution Measurement resolution setting
 * @return Operation status (HAL_OK if successful)
 */
HAL_StatusTypeDef Si7021_Init(Si7021_t *hsi7021, I2C_HandleTypeDef *hi2c, uint8_t address, Si7021_Resolution_t resolution)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }

    hsi7021->hi2c = hi2c;
    hsi7021->address = address << 1;
    if(HAL_OK != Si7021_ReadFirmware(hsi7021))
    	return HAL_ERROR;

    Si7021_SetResolution(hsi7021, resolution);
    return HAL_OK;
}

/**
 * @brief Set measurement resolution
 * @param hsi7021 Pointer to the configuration structure
 * @param resolution Selected resolution
 * @return Operation status
 */
HAL_StatusTypeDef Si7021_SetResolution(Si7021_t *hsi7021, Si7021_Resolution_t resolution)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }
    uint8_t reg;
    HAL_StatusTypeDef status = Si7021_ReadRegister(hsi7021, SI7021_CMD_READ_USER_REG1, &reg, 1);

    if (status != HAL_OK)
    	return status;

    // Clear RES1 (bit 7) and RES0 (bit 0) bits
    reg &= ~(1 << 7 | 1 << 0);

    // Set bits according to resolution
    switch (resolution)
    {
        case SI7021_RESOLUTION_RH12_TEMP14:
            // 00: bit7=0, bit0=0
            break;
        case SI7021_RESOLUTION_RH8_TEMP12:
            // 01: bit7=0, bit0=1
            reg |= (1 << 0);
            break;
        case SI7021_RESOLUTION_RH10_TEMP13:
            // 10: bit7=1, bit0=0
            reg |= (1 << 7);
            break;
        case SI7021_RESOLUTION_RH11_TEMP11:
            // 11: bit7=1, bit0=1
            reg |= (1 << 7 | 1 << 0);
            break;
    }

    return Si7021_WriteRegister(hsi7021, SI7021_CMD_WRITE_USER_REG1, reg);
}

/**
 * @brief Read current measurement resolution
 * @param hsi7021 Pointer to the configuration structure
 * @return Operation status
 */
HAL_StatusTypeDef Si7021_GetResolution(Si7021_t *hsi7021)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }
    uint8_t reg;
    HAL_StatusTypeDef status = Si7021_ReadRegister(hsi7021, SI7021_CMD_READ_USER_REG1, &reg, 1);

    if (status != HAL_OK)
    	return status;

    // Extract RES1 (bit 7) and RES0 (bit 0) bits
    uint8_t res_bits = ((reg >> 7) & 1) << 1 | (reg & 1);
    hsi7021->data.resolution = (Si7021_Resolution_t)res_bits;
    return HAL_OK;
}

/**
 * @brief Set heater to specified current value
 * @param hsi7021 Pointer to the configuration structure
 * @param current Adjustable current value. At 3.3V maximum current is 94mA
 * @return Operation status
 */
HAL_StatusTypeDef Si7021_SetHeaterCurrent(Si7021_t *hsi7021, uint8_t current)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }
    uint8_t reg_value = (current - SI7021_HEATER_MIN_CURRENT) / SI7021_HEATER_CURRENT_OFFSET;
    if (reg_value > 0x0F)
    {
        reg_value = 0x0F;
    }
    return Si7021_WriteRegister(hsi7021, SI7021_CMD_WRITE_HEATER_REG, reg_value);
}

/**
 * @brief Get current heater current setting
 * @param hsi7021 Pointer to the configuration structure
 * @return Operation status
 */
HAL_StatusTypeDef Si7021_GetHeaterCurrent(Si7021_t *hsi7021)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }
    uint8_t reg;
    HAL_StatusTypeDef status = Si7021_ReadRegister(hsi7021, SI7021_CMD_READ_HEATER_REG, &reg, 1);
    if (status != HAL_OK)
    	return status;

    hsi7021->data.heater_current = ((reg & 0x0F) * SI7021_HEATER_CURRENT_OFFSET) + SI7021_HEATER_MIN_CURRENT;

    return HAL_OK;
}

/**
 * @brief Helper function to compute CRC-8 with polynomial 0x31
 * @param data Pointer to data for CRC calculation
 * @param len Data length
 * @return Calculated checksum
 */
static uint8_t Si7021_ComputeCRC8(uint8_t *data, uint8_t len) 
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) 
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) 
        {
            if (crc & 0x80) 
            {
                crc = (crc << 1) ^ 0x31;
            } else 
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief Read humidity with checksum verification
 * @param hsi7021 Pointer to the configuration structure
 * @return Operation status
 */
HAL_StatusTypeDef Si7021_ReadHumidity(Si7021_t *hsi7021)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }
    uint8_t data[3];

    HAL_StatusTypeDef status = Si7021_ReadRegister(hsi7021, SI7021_CMD_MEASURE_RH_HOLD, data, 3);

    if (status != HAL_OK)
    	return status;

    uint8_t crc = Si7021_ComputeCRC8(data, 2);

    if (crc != data[2])
    	return HAL_ERROR;

    uint16_t rh_code = (data[0] << 8) | data[1];
    hsi7021->data.humidity = (125.0f * rh_code) / 65536.0f - 6.0f;

    if (hsi7021->data.humidity < 0)
    	hsi7021->data.humidity = 0;

    if (hsi7021->data.humidity > 100)
    	hsi7021->data.humidity = 100;

    return HAL_OK;
}

/**
 * @brief Read temperature with checksum verification
 * @param hsi7021 Pointer to the configuration structure
 * @return Operation status
 */
HAL_StatusTypeDef Si7021_ReadTemperature(Si7021_t *hsi7021)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }

    uint8_t data[3];
    HAL_StatusTypeDef status = Si7021_ReadRegister(hsi7021, SI7021_CMD_MEASURE_TEMP_HOLD, data, 3);

    if (status != HAL_OK)
    	return status;

    uint8_t crc = Si7021_ComputeCRC8(data, 2);

    if (crc != data[2])
    	return HAL_ERROR;

    uint16_t temp_code = (data[0] << 8) | data[1];
    hsi7021->data.temperature = (175.72f * temp_code) / 65536.0f - 46.85f;

    return HAL_OK;
}

/**
 * @brief Read humidity and temperature. Temperature reading from previous humidity measurement (no checksum for 0xE0 command)
 * @param hsi7021 Pointer to the configuration structure
 * @return Operation status
 */
HAL_StatusTypeDef Si7021_ReadHumidityAndTemperature(Si7021_t *hsi7021)
{

	HAL_StatusTypeDef status = Si7021_ReadHumidity(hsi7021);
    uint8_t temp_data[2];

    status = Si7021_ReadRegister(hsi7021, SI7021_CMD_READ_TEMP_PREV_RH, temp_data, 2);
    if (status != HAL_OK)
    	return status;

    uint16_t temp_code = (temp_data[0] << 8) | temp_data[1];
    hsi7021->data.temperature = (175.72f * temp_code) / 65536.0f - 46.85f;

    return HAL_OK;
}
