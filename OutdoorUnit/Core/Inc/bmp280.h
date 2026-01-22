// bmp280.h
#ifndef BMP280_H
#define BMP280_H

#include <stdint.h>
#include "main.h"



/*	Recommended filter settings based on use cases. Doc Table 7 page 14 */

typedef enum{
	BMP280_OPERATION_0	= 0x00,				// handheld device low-power
	BMP280_OPERATION_1	= 0x01,				// handheld device dynamic
	BMP280_OPERATION_2	= 0x02,				// Weather monitoring (lowest power)
	BMP280_OPERATION_3	= 0x03,				// Elevator / floor change detection
	BMP280_OPERATION_4	= 0x04,				// Drop detection
	BMP280_OPERATION_5	= 0x05,				// Indoor navigation
}BMP280_Operation_t;


/*	Register adresses and commands	*/
typedef enum{
	BMP280_OK				= 0x0U,
	BMP280_ERROR			= 0x01U,
	BMP280_BUSY    			= 0x02U,
	BMP280_TIMEOUT  		= 0x03U,
	BMP280_INIT_ERROR		= 0x04U,
	BMP280_READ_ERROR		= 0x05U,
	BMP280_WRITE_ERROR		= 0x06U,
}BMP280_Status_t;

typedef enum {
	BMP280_RESET_COMMAND 	= 0xB6,
	BMP280_CHIP_ID 			= 0x58
}BMP280_Command_t;

typedef enum {
    BMP280_REG_CHIP_ID      = 0xD0,
    BMP280_REG_RESET        = 0xE0,
    BMP280_REG_STATUS       = 0xF3,
    BMP280_REG_CTRL_MEAS    = 0xF4,
    BMP280_REG_CONFIG       = 0xF5,
    BMP280_REG_PRESS_MSB    = 0xF7,
    BMP280_REG_PRESS_LSB    = 0xF8,
    BMP280_REG_PRESS_XLSB   = 0xF9,
    BMP280_REG_TEMP_MSB     = 0xFA,
    BMP280_REG_TEMP_LSB     = 0xFB,
    BMP280_REG_TEMP_XLSB    = 0xFC,
    BMP280_REG_CALIB_START  = 0x88,
    BMP280_REG_CALIB_END    = 0xA1
} BMP280_Registers;


typedef enum {
    BMP280_MODE_SLEEP = 0x00,
    BMP280_MODE_FORCED = 0x01,
    BMP280_MODE_NORMAL = 0x03
} BMP280_Mode;

typedef enum {
    BMP280_OVERSAMPLING_SKIPPED = 0x00,		// Pressure measurement skipped
    BMP280_OVERSAMPLING_X1 = 0x01,			// Ultra low power
    BMP280_OVERSAMPLING_X2 = 0x02,			// Low power
    BMP280_OVERSAMPLING_X4 = 0x03,			// Standard resolution
    BMP280_OVERSAMPLING_X8 = 0x04,			// High resolution
    BMP280_OVERSAMPLING_X16 = 0x05			// Ultra high resolution
} BMP280_Oversampling;

typedef enum {
    BMP280_FILTER_OFF = 0x00,
    BMP280_FILTER_2 = 0x01,
    BMP280_FILTER_4 = 0x02,
    BMP280_FILTER_8 = 0x03,
    BMP280_FILTER_16 = 0x04
} BMP280_Filter;

typedef enum {
    BMP280_STANDBY_0_5_MS = 0x00,
    BMP280_STANDBY_62_5_MS = 0x01,
    BMP280_STANDBY_125_MS = 0x02,
    BMP280_STANDBY_250_MS = 0x03,
    BMP280_STANDBY_500_MS = 0x04,
    BMP280_STANDBY_1000_MS = 0x05,
    BMP280_STANDBY_2000_MS = 0x06,
    BMP280_STANDBY_4000_MS = 0x07
} BMP280_StandbyTime;

/*	Data structures for BMP280	*/
typedef struct {

    int16_t dig_T2;
    int16_t dig_T3;

    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;

    uint16_t dig_P1;
    uint16_t dig_T1;

    int32_t t_fine;

} BMP280_Calibcalibration_t;

typedef struct {
	int32_t raw_temperature;
	int32_t raw_pressure;
	float temperature;
	float pressure;
}BMP280_Measurment_t;

typedef struct {
    I2C_HandleTypeDef *i2c_handle;
    uint8_t address;
    BMP280_Measurment_t data;
    BMP280_Calibcalibration_t calibration;

} BMP280_t;



HAL_StatusTypeDef BMP280_Init(BMP280_t *dev, I2C_HandleTypeDef *i2c_handle, uint8_t sdo_state);

HAL_StatusTypeDef BMP280_SoftReset(BMP280_t *dev);

HAL_StatusTypeDef BMP280_ReadCalibration(BMP280_t *dev);

HAL_StatusTypeDef BMP280_SetConfig(BMP280_t *dev, BMP280_StandbyTime standby, BMP280_Filter filter);

HAL_StatusTypeDef BMP280_SetCtrlMeas(BMP280_t *dev, BMP280_Oversampling OVERSAMPLING_p, BMP280_Mode mode);

HAL_StatusTypeDef BMP280_SetMode(BMP280_t *dev, BMP280_Mode mode);

HAL_StatusTypeDef BMP280_OperationMode(BMP280_t *dev, BMP280_Operation_t operation);

HAL_StatusTypeDef BMP280_GetStatus(BMP280_t *dev, uint8_t *measuring, uint8_t *im_update);

HAL_StatusTypeDef BMP280_ReadRawTemperature(BMP280_t *dev);

HAL_StatusTypeDef BMP280_ReadRawPressure(BMP280_t *dev);

HAL_StatusTypeDef BMP280_GetTemperature(BMP280_t *dev);

HAL_StatusTypeDef BMP280_GetPressure(BMP280_t *dev);

HAL_StatusTypeDef BMP280_TemperatureAndPressure(BMP280_t *dev);

#endif // BMP280_H
