#include "TSL2561.h"
#include <math.h>

// Helper function to write a single byte to a register
static HAL_StatusTypeDef TSL2561_WriteByte(TSL2561_t *sensor, uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {TSL2561_REG_COMMAND | reg, value};
    return HAL_I2C_Master_Transmit(sensor->hi2c, sensor->address, data, 2, HAL_MAX_DELAY);
}

// Helper function to read a single byte from a register
static HAL_StatusTypeDef TSL2561_ReadByte(TSL2561_t *sensor, uint8_t reg, uint8_t *value)
{
    uint8_t reg_cmd = TSL2561_REG_COMMAND | reg;
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(sensor->hi2c, sensor->address, &reg_cmd, 1, HAL_MAX_DELAY);

    if (status != HAL_OK)
    	return status;

    return HAL_I2C_Master_Receive(sensor->hi2c, sensor->address | 1, value, 1, HAL_MAX_DELAY);
}

// Helper function to write a 16-bit word to a register pair
static HAL_StatusTypeDef TSL2561_WriteWord(TSL2561_t *sensor, uint8_t reg_low, uint16_t value)
{
    uint8_t data[3] = {TSL2561_REG_COMMAND | TSL2561_REG_WORD | reg_low, value & 0xFF, (value >> 8) & 0xFF};
    return HAL_I2C_Master_Transmit(sensor->hi2c, sensor->address, data, 3, HAL_MAX_DELAY);
}

// Helper function to read a 16-bit word from a register pair
static HAL_StatusTypeDef TSL2561_ReadWord(TSL2561_t *sensor, uint8_t reg_low, uint16_t *value)
{
    uint8_t reg_cmd = TSL2561_REG_COMMAND | TSL2561_REG_WORD | reg_low;
    uint8_t data[2];
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(sensor->hi2c, sensor->address, &reg_cmd, 1, HAL_MAX_DELAY);

    if (status != HAL_OK)
    	return status;

    status = HAL_I2C_Master_Receive(sensor->hi2c, sensor->address | 1, data, 2, HAL_MAX_DELAY);

    if (status != HAL_OK)
    	return status;

    *value = (data[1] << 8) | data[0];
    return HAL_OK;
}

// Initialize the TSL2561 sensor
HAL_StatusTypeDef TSL2561_Init(TSL2561_t *sensor, I2C_HandleTypeDef *hi2c, uint8_t address,
		TSL2561_IntegrationTime_t timing_ms, TSL2561_Gain_t gain)
{
    if (sensor == NULL || hi2c == NULL)
    	return HAL_ERROR;

    sensor->hi2c = hi2c;
    sensor->address = address << 1;
    sensor->gain = gain;
    sensor->timing_ms = timing_ms;

    // Verify communication by reading ID register
    uint8_t part_no = 0;
	uint8_t rev_no = 0;
    HAL_StatusTypeDef status = TSL2561_ReadID(sensor, &part_no, &rev_no);

    if (HAL_OK != status && part_no != 0x50)
    {
    	return HAL_ERROR; // Check for TSL2561T part number (0101)
    }

	TSL2561_PowerOn(sensor);
	TSL2561_SetTiming(sensor,sensor->timing_ms, sensor->gain);

    return HAL_OK;
}

// Power on the sensor
HAL_StatusTypeDef TSL2561_PowerOn(TSL2561_t *sensor)
{
    HAL_StatusTypeDef status = TSL2561_WriteByte(sensor, TSL2561_REG_CONTROL, 0x03);

    if (status != HAL_OK)
    	return HAL_ERROR;

    return status;
}

// Power off the sensor
HAL_StatusTypeDef TSL2561_PowerOff(TSL2561_t *sensor)
{
    HAL_StatusTypeDef status = TSL2561_WriteByte(sensor, TSL2561_REG_CONTROL, 0x00);

    if (status != HAL_OK)
    	return HAL_ERROR;

    return status;
}



// Configure integration time, gain, and manual mode
HAL_StatusTypeDef TSL2561_SetTiming(TSL2561_t *sensor, TSL2561_IntegrationTime_t time, TSL2561_Gain_t gain)
{
	if (sensor == NULL)
    	return HAL_ERROR;

    uint8_t timing = (uint8_t)gain | ((uint8_t)time & 0x03);

    sensor->gain = gain;
    sensor->timing_ms = time;

    return TSL2561_WriteByte(sensor, TSL2561_REG_TIMING, timing);
}

// Set interrupt thresholds
HAL_StatusTypeDef TSL2561_SetInterruptThreshold(TSL2561_t *sensor, uint16_t low_threshold, uint16_t high_threshold)
{
    HAL_StatusTypeDef status;
    status = TSL2561_WriteWord(sensor, TSL2561_REG_THRESHLOWLOW, low_threshold);

    if (status != HAL_OK)
    	return status;

    return TSL2561_WriteWord(sensor, TSL2561_REG_THRESHHIGHLOW, high_threshold);
}

// Configure interrupt control and persistence
HAL_StatusTypeDef TSL2561_SetInterruptControl(TSL2561_t *sensor, uint8_t intr_mode, uint8_t persist)
{
    uint8_t intr_control = (intr_mode & 0x30) | (persist & 0x0F);
    return TSL2561_WriteByte(sensor, TSL2561_REG_INTERRUPT, intr_control);
}

// Clear interrupt
HAL_StatusTypeDef TSL2561_ClearInterrupt(TSL2561_t *sensor)
{
    return TSL2561_WriteByte(sensor, TSL2561_REG_COMMAND | TSL2561_REG_CLEAR, 0x00);
}

// Read ID register
HAL_StatusTypeDef TSL2561_ReadID(TSL2561_t *sensor, uint8_t *part_no, uint8_t *rev_no)
{
    uint8_t id;
    HAL_StatusTypeDef status = TSL2561_ReadByte(sensor, TSL2561_REG_ID, &id);

    if (status != HAL_OK)
    	return status;

    *part_no = (id >> 4) & 0x0F;
    *rev_no = id & 0x0F;
    return HAL_OK;
}

// Read ADC channel data
HAL_StatusTypeDef TSL2561_ReadADC(TSL2561_t *sensor)
{
	if (sensor == NULL)
	{
        return HAL_ERROR;
    }

    HAL_StatusTypeDef status;
    uint16_t data[2];

    status = TSL2561_ReadWord(sensor, TSL2561_REG_DATA0LOW, &data[0]);

    if (status != HAL_OK)
    {
        
    	return status;
    }

    status = TSL2561_ReadWord(sensor, TSL2561_REG_DATA1LOW, &data[1]);
    sensor->data.chan0 = data[0];
    sensor->data.chan1 = data[1];
    return status;
}

// Calculate lux value based on datasheet formulas
HAL_StatusTypeDef TSL2561_CalculateLux(TSL2561_t *sensor)
{
	HAL_StatusTypeDef status;
	float fChan0;
	float fChan1;
	status = TSL2561_ReadADC(sensor);

	if (status != HAL_OK)
    	return status;

    if (0 == sensor->data.chan0)
    {
        sensor->data.lux = 0.0f;
        return HAL_OK;
    }


    fChan0 = (float)sensor->data.chan0;
    fChan1 = (float)sensor->data.chan1;
    float ratio = fChan1 / fChan0;

    // Normalize for integration time
    fChan0 *= (402.0 / sensor->timing_ms);
    fChan1 *= (402.0 / sensor->timing_ms);

    // Normalize for gain
    if (sensor->gain == TSL2561_GAIN_16X)
    {
        fChan0 *= 16;
        fChan1 *= 16;
    }

    // Determine lux per datasheet equations:
    if (ratio < 0.5)
    {
        sensor->data.lux = 0.0304 * fChan0 - 0.062 * fChan0 * pow(ratio,1.4);
        return HAL_OK;
    }

    if (ratio < 0.61)
    {
        sensor->data.lux = 0.0224 * fChan0 - 0.031 * fChan1;
        return HAL_OK;
    }

    if (ratio < 0.80)
    {
        sensor->data.lux = 0.0128 * fChan0 - 0.0153 * fChan1;
        return HAL_OK;
    }

    if (ratio < 1.30)
    {
        sensor->data.lux = 0.00146 * fChan0 - 0.00112 * fChan1;
        return HAL_OK;
    }

    // if (ratio > 1.30)

    sensor->data.lux = 0.0;

    return HAL_OK;
}
