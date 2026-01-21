#include "measurement.h"
#include <stdio.h>

/* Private variables */
static Si7021_t hsi7021;
static BMP280_t hbmp280;
static TSL2561_t htsl2561;
static I2C_HandleTypeDef *measurement_hi2c = NULL;
static Groupe_State_t *devices;

/**
 * @brief Initializes the measurement module
 */
void Measurement_Init(I2C_HandleTypeDef *hi2c) {
    if (measurement_hi2c == NULL) 
    {
        devices.state = MEAS_ERROR;
        return;
    }
    devices.state = MEAS_INIT;
    devices.sensorErrorCode = ERROR_NONE
    device.data = {0};
    //devices = MEAS_STATE_INIT;
}

/**
 * @brief Starts a new measurement cycle
 */
void Measurement_Start(void) {
    if (devices.state == MEAS_STATE_IDLE || devices.state == MEAS_STATE_DONE || devices.state == MEAS_STATE_ERROR) {
        devices.state = MEAS_STATE_RUN;
    }

    //TODO idk purpouse of this function tbh
}

/**
 * @brief Process the measurement state machine
 */
void Measurement_Process(void) 
{
    if (measurement_hi2c == NULL) {
        return;
    }

    switch (devices.state) {
        case MEAS_INIT:
            // Initialize Si7021
            if (Si7021_Init(&hsi7021, measurement_hi2c, 0x40, SI7021_RESOLUTION_RH11_TEMP11) != HAL_OK) {
                devices.sensorErrorCode |= ERROR_SI7021;
            }

            // Initialize BMP280
            if (BMP280_Init(&hbmp280, measurement_hi2c, 0x76) != HAL_OK) {
               devices.sensorErrorCode |= ERROR_BMP280;
            }
            BMP280_SetCtrlMeas(&hbmp280, BMP280_OVERSAMPLING_X16, BMP280_MODE_NORMAL);
            BMP280_SetConfig(&hbmp280, BMP280_STANDBY_500_MS, BMP280_FILTER_16);

            // Initialize TSL2561
            if (TSL2561_Init(&htsl2561, measurement_hi2c, 0x39, TSL2561_INTEG_402MS, TSL2561_GAIN_1X) != HAL_OK) {
                devices.sensorErrorCode |= ERROR_TSL2561;
            }
            
            if((devices.sensorErrorCode & (ERROR_SI7021 | ERROR_BMP280 | ERROR_TSL2561)))
            {
                devices.state = MEAS_ERROR; // TODO: handle somehow this state
            }

            devices.state = MEAS_MEASURE;
            break;

        case MEAS_IDLE:
            // Do nothing, wait for external change (e.g., call to Measurement_Start)
            break;

        case MEAS_MEASURE:
            // Start the measurement cycle
            devices.state = MEAS_STATE_SI7021;
            break;

        case MEAS_SI7021:
            if (Si7021_ReadHumidityAndTemperature(&hsi7021) == HAL_OK)
            {
                devices.data.si7021_temp = hsi7021.data.temperature;
                devices.data.si7021_hum = hsi7021.data.humidity;
                devices.state = MEAS_BMP280;
            } 
            else
            {
                devices.sensorErrorCode |= ERROR_SI7021;
                devices.state = MEAS_ERROR;
            }
            break;

        case MEAS_BMP280:
            if (BMP280_TemperatureAndPressure(&hbmp280) == HAL_OK) 
            {
                devices.data.bmp280_temp = hbmp280.data.temperature;
                devices.data.bmp280_press = hbmp280.data.pressure;
                devices.state = MEAS_TSL2561;
            } 
            else 
            {
                devices.sensorErrorCode |= ERROR_BMP280;
                devices.state = MEAS_ERROR;
            }
            break;

        case MEAS_TSL2561:
            if (TSL2561_CalculateLux(&htsl2561) == HAL_OK) 
            {
                devices.data.tsl2561_lux = htsl2561.data.lux;
                devices.state = MEAS_DONE;
            } 
            else 
            {
                devices.sensorErrorCode |= ERROR_TSL2561;
                devices.state = MEAS_ERROR;
            }
            break;

        case MEAS_DONE
            // Measurement cycle finished, go back to IDLE
            devices.state = MEAS_IDLE;
            break;

        case MEAS_ERROR:
            // Could implement retry logic here
            //TODO: impement init handler
            break;

        default:
            devices.state = MEAS_IDLE;
            devices.sensorErrorCode = ERROR_NONE;
            break;
    }
}

/**
 * @brief Returns the current state of the measurement state machine
 */
Measurement_State_t Measurement_GetState(void) {
    return devices.state;
}

/**
 * @brief Formats the latest measurement data into CSV format
 */
void Measurement_GetCSV(char *buffer, uint16_t len) {
    if (buffer == NULL || len == 0) {
        return;
    }
    snprintf(buffer, len, "%.2f,%.2f,%.2f,%.2f,%.2f",
             devices.data.si7021_temp, devices.data.si7021_hum,
             devices.data.bmp280_temp, devices.data.bmp280_press,
             devices.data.tsl2561_lux);
}
