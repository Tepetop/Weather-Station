#include "measurement.h"
#include <stdio.h>

/* Private variables */
static Si7021_t hsi7021;
static BMP280_t hbmp280;
static TSL2561_t htsl2561;
static Measurement_Data_t meas_data = {0};
static Measurement_State_t current_state = MEAS_STATE_IDLE;
static I2C_HandleTypeDef *measurement_hi2c = NULL;

/**
 * @brief Initializes the measurement module
 */
void Measurement_Init(I2C_HandleTypeDef *hi2c) {
    measurement_hi2c = hi2c;
    current_state = MEAS_STATE_INIT;
}

/**
 * @brief Process the measurement state machine
 */
void Measurement_Process(void) {
    if (measurement_hi2c == NULL) {
        return;
    }

    switch (current_state) {
        case MEAS_STATE_INIT:
            // Initialize Si7021
            if (Si7021_Init(&hsi7021, measurement_hi2c, 0x40, SI7021_RESOLUTION_RH11_TEMP11) != HAL_OK) {
                current_state = MEAS_STATE_ERROR;
                break;
            }

            // Initialize BMP280
            if (BMP280_Init(&hbmp280, measurement_hi2c, 0x76) != HAL_OK) {
                current_state = MEAS_STATE_ERROR;
                break;
            }
            BMP280_SetCtrlMeas(&hbmp280, BMP280_OVERSAMPLING_X16, BMP280_MODE_NORMAL);
            BMP280_SetConfig(&hbmp280, BMP280_STANDBY_500_MS, BMP280_FILTER_16);

            // Initialize TSL2561
            if (TSL2561_Init(&htsl2561, measurement_hi2c, 0x39, TSL2561_INTEG_402MS, TSL2561_GAIN_1X) != HAL_OK) {
                current_state = MEAS_STATE_ERROR;
                break;
            }

            current_state = MEAS_STATE_IDLE;
            break;

        case MEAS_STATE_IDLE:
            // Ready to start a new measurement cycle
            current_state = MEAS_STATE_SI7021;
            break;

        case MEAS_STATE_SI7021:
            if (Si7021_ReadHumidityAndTemperature(&hsi7021) == HAL_OK) {
                meas_data.si7021_temp = hsi7021.data.temperature;
                meas_data.si7021_hum = hsi7021.data.humidity;
                current_state = MEAS_STATE_BMP280;
            } else {
                current_state = MEAS_STATE_ERROR;
            }
            break;

        case MEAS_STATE_BMP280:
            if (BMP280_TemperatureAndPressure(&hbmp280) == HAL_OK) {
                meas_data.bmp280_temp = hbmp280.data.temperature;
                meas_data.bmp280_press = hbmp280.data.pressure;
                current_state = MEAS_STATE_TSL2561;
            } else {
                current_state = MEAS_STATE_ERROR;
            }
            break;

        case MEAS_STATE_TSL2561:
            if (TSL2561_CalculateLux(&htsl2561) == HAL_OK) {
                meas_data.tsl2561_lux = htsl2561.data.lux;
                current_state = MEAS_STATE_DONE;
            } else {
                current_state = MEAS_STATE_ERROR;
            }
            break;

        case MEAS_STATE_DONE:
            // Stay in DONE state until reset or manually moved back to IDLE
            break;

        case MEAS_STATE_ERROR:
            // Could implement retry logic here
            break;

        default:
            current_state = MEAS_STATE_IDLE;
            break;
    }
}

/**
 * @brief Returns the current state of the measurement state machine
 */
Measurement_State_t Measurement_GetState(void) {
    return current_state;
}

/**
 * @brief Formats the latest measurement data into CSV format
 */
void Measurement_GetCSV(char *buffer, uint16_t len) {
    if (buffer == NULL || len == 0) {
        return;
    }
    snprintf(buffer, len, "%.2f,%.2f,%.2f,%.2f,%.2f",
             meas_data.si7021_temp, meas_data.si7021_hum,
             meas_data.bmp280_temp, meas_data.bmp280_press,
             meas_data.tsl2561_lux);
}
