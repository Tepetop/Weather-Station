#include "measurement.h"
#include <stdio.h>
#include <string.h>

/* Private variables */
static Si7021_t hsi7021;
static BMP280_t hbmp280;
static TSL2561_t htsl2561;
static I2C_HandleTypeDef *measurement_hi2c;
static Groupe_State_t devices;

/**
 * @brief Initializes the measurement module
 */
void Measurement_Init(I2C_HandleTypeDef *hi2c) {
    measurement_hi2c = hi2c;
    if (measurement_hi2c == NULL) 
    {
        devices.state = MEAS_ERROR;
        return;
    }
    devices.state = MEAS_INIT;
    devices.sensorErrorCode = ERROR_NONE;
    memset(&devices.data, 0, sizeof(Measurement_Data_t));
}

/**
 * @brief Starts a new measurement cycle
 */
void Measurement_Start(void) {
    if (devices.state == MEAS_IDLE) {
        devices.state = MEAS_MEASURE;
    }
}

/* ========== Private Helper Functions ========== */

/**
 * @brief Initialize all sensors (Si7021, BMP280, TSL2561)
 */
static void Measurement_InitializeSensors(void) {
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
    
    // Transition to next state based on initialization result
    if (devices.sensorErrorCode != ERROR_NONE) {
        devices.state = MEAS_INIT_ERROR;
    } else {
        devices.state = MEAS_IDLE;
    }
}

/**
 * @brief Read Si7021 temperature and humidity sensor
 */
static void Measurement_ReadSi7021(void) {
    if (Si7021_ReadHumidityAndTemperature(&hsi7021) == HAL_OK) {
        devices.data.si7021_temp = hsi7021.data.temperature;
        devices.data.si7021_hum = hsi7021.data.humidity;
    } else {
        devices.sensorErrorCode |= ERROR_SI7021;
    }
    // Move to next sensor regardless of success/failure
    devices.state = MEAS_BMP280;
}

/**
 * @brief Read BMP280 temperature and pressure sensor
 */
static void Measurement_ReadBMP280(void) {
    if (BMP280_TemperatureAndPressure(&hbmp280) == HAL_OK) {
        devices.data.bmp280_temp = hbmp280.data.temperature;
        devices.data.bmp280_press = hbmp280.data.pressure;
    } else {
        devices.sensorErrorCode |= ERROR_BMP280;
    }
    // Move to next sensor regardless of success/failure
    devices.state = MEAS_TSL2561;
}

/**
 * @brief Read TSL2561 light intensity sensor
 */
static void Measurement_ReadTSL2561(void) {
    if (TSL2561_CalculateLux(&htsl2561) == HAL_OK) {
        devices.data.tsl2561_lux = htsl2561.data.lux;
    } else {
        devices.sensorErrorCode |= ERROR_TSL2561;
    }
    // All sensors read, measurement cycle complete
    devices.state = MEAS_DONE;
}

/**
 * @brief Handle measurement errors
 */
static void Measurement_HandleError(void) {
    // Could implement retry logic here
    // TODO: implement error recovery strategy (e.g., reinitialize sensors, retry count, etc.)
}

/**
 * @brief Process the measurement state machine
 * Executes the complete measurement cycle in one call.
 * If a sensor fails, other sensors continue reading.
 */
void Measurement_Process(void) 
{
    // Process states in a loop until measurement is complete or in idle
    while (devices.state != MEAS_IDLE) {
        switch (devices.state) {
            case MEAS_INIT:
                Measurement_InitializeSensors();
                // If initialization failed, exit loop to wait for retry
                if (devices.state == MEAS_INIT_ERROR) {
                    return;
                }
                break;
            
            case MEAS_MEASURE:
                // Start the measurement cycle
                devices.state = MEAS_SI7021;
                break;

            case MEAS_SI7021:
                Measurement_ReadSi7021();
                break;

            case MEAS_BMP280:
                Measurement_ReadBMP280();
                break;

            case MEAS_TSL2561:
                Measurement_ReadTSL2561();
                break;

            case MEAS_DONE:
                // Measurement cycle finished, go back to IDLE
                devices.state = MEAS_IDLE;
                break;

            case MEAS_ERROR:
                // Critical error, exit the loop
                Measurement_HandleError();
                return;

            default:
                devices.state = MEAS_IDLE;
                devices.sensorErrorCode = ERROR_NONE;
                break;
        }
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
    snprintf(buffer, len, "Si7021 temp=%.2f C, humidity=%.2f %%\r\nBMP280 temp=%.2f C, pressure=%.2f hPa\r\nTSL2561 lux=%.2f\r\n",
             devices.data.si7021_temp, devices.data.si7021_hum,
             devices.data.bmp280_temp, devices.data.bmp280_press,
             devices.data.tsl2561_lux);
}
