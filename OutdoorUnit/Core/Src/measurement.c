#include "measurement.h"
#include <stdio.h>
#include <string.h>

/* Private variables */
static Si7021_t hsi7021;
static BMP280_t hbmp280;
static TSL2561_t htsl2561;
static I2C_HandleTypeDef *measurement_hi2c;
static Groupe_State_t devices;

/* Sensor initialization flags */
#define SENSOR_SI7021_INIT  (1 << 0)
#define SENSOR_BMP280_INIT  (1 << 1)
#define SENSOR_TSL2561_INIT (1 << 2)
#define ALL_SENSORS_INIT    (SENSOR_SI7021_INIT | SENSOR_BMP280_INIT | SENSOR_TSL2561_INIT)

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
    devices.initRetryCount = 0;
    devices.sensorsInitialized = 0;
    memset(&devices.data, 0, sizeof(Measurement_Data_t));
}

/**
 * @brief Starts a new measurement cycle
 */
void Measurement_Start(void) {
    if (devices.state == MEAS_IDLE || devices.state == MEAS_SLEEP) {
        // Clear error codes from previous measurement
        devices.sensorErrorCode = ERROR_NONE;
        // Wake up sensors first if they were sleeping
        if (devices.state == MEAS_SLEEP) {
            devices.state = MEAS_WAKEUP;
        } else {
            devices.state = MEAS_MEASURE;
        }
    }
}

/* ========== Private Helper Functions ========== */

/**
 * @brief Initialize Si7021 sensor
 * @return HAL_OK if successful
 */
static HAL_StatusTypeDef Measurement_InitSi7021(void) {
    if (Si7021_Init(&hsi7021, measurement_hi2c, 0x40, SI7021_RESOLUTION_RH11_TEMP11) != HAL_OK) {
        return HAL_ERROR;
    }
    devices.sensorsInitialized |= SENSOR_SI7021_INIT;
    return HAL_OK;
}

/**
 * @brief Initialize BMP280 sensor
 * @return HAL_OK if successful
 */
static HAL_StatusTypeDef Measurement_InitBMP280(void) {
    if (BMP280_Init(&hbmp280, measurement_hi2c, 0x76) != HAL_OK) {
        return HAL_ERROR;
    }
    // Configure for low power - use FORCED mode instead of NORMAL
    // In FORCED mode, sensor takes one measurement and goes back to sleep
    BMP280_SetCtrlMeas(&hbmp280, BMP280_OVERSAMPLING_X16, BMP280_MODE_SLEEP);
    BMP280_SetConfig(&hbmp280, BMP280_STANDBY_500_MS, BMP280_FILTER_16);
    devices.sensorsInitialized |= SENSOR_BMP280_INIT;
    return HAL_OK;
}

/**
 * @brief Initialize TSL2561 sensor
 * @return HAL_OK if successful
 */
static HAL_StatusTypeDef Measurement_InitTSL2561(void) {
    if (TSL2561_Init(&htsl2561, measurement_hi2c, 0x39, TSL2561_INTEG_402MS, TSL2561_GAIN_1X) != HAL_OK) {
        return HAL_ERROR;
    }
    // Power off after init to save power
    TSL2561_PowerOff(&htsl2561);
    devices.sensorsInitialized |= SENSOR_TSL2561_INIT;
    return HAL_OK;
}

/**
 * @brief Initialize all sensors (Si7021, BMP280, TSL2561)
 */
static void Measurement_InitializeSensors(void) {
    // Reset error code before initialization
    devices.sensorErrorCode = ERROR_NONE;
    
    // Initialize Si7021
    if (!(devices.sensorsInitialized & SENSOR_SI7021_INIT)) {
        if (Measurement_InitSi7021() != HAL_OK) {
            devices.sensorErrorCode |= ERROR_SI7021;
        }
    }

    // Initialize BMP280
    if (!(devices.sensorsInitialized & SENSOR_BMP280_INIT)) {
        if (Measurement_InitBMP280() != HAL_OK) {
            devices.sensorErrorCode |= ERROR_BMP280;
        }
    }

    // Initialize TSL2561
    if (!(devices.sensorsInitialized & SENSOR_TSL2561_INIT)) {
        if (Measurement_InitTSL2561() != HAL_OK) {
            devices.sensorErrorCode |= ERROR_TSL2561;
        }
    }
    
    // Transition to next state based on initialization result
    if (devices.sensorErrorCode != ERROR_NONE) {
        devices.initRetryCount++;
        if (devices.initRetryCount >= MEASUREMENT_MAX_RETRY_COUNT) {
            // Max retries reached, go to error state but allow partial operation
            if (devices.sensorsInitialized != 0) {
                // At least one sensor initialized - go to sleep and allow measurements
                devices.state = MEAS_SLEEP;
            } else {
                // No sensors initialized - critical error
                devices.state = MEAS_ERROR;
            }
        } else {
            devices.state = MEAS_INIT_ERROR;
        }
    } else {
        devices.initRetryCount = 0;
        devices.state = MEAS_SLEEP; // Go to sleep mode after successful init
    }
}

/**
 * @brief Read Si7021 temperature and humidity sensor
 */
static void Measurement_ReadSi7021(void) {
    // Skip if sensor not initialized
    if (!(devices.sensorsInitialized & SENSOR_SI7021_INIT)) {
        devices.sensorErrorCode |= ERROR_SI7021;
        return;
    }
    
    if (Si7021_ReadHumidityAndTemperature(&hsi7021) == HAL_OK) {
        devices.data.si7021_temp = hsi7021.data.temperature;
        devices.data.si7021_hum = hsi7021.data.humidity;
    } else {
        devices.sensorErrorCode |= ERROR_SI7021;
        // Mark sensor as needing reinitialization
        devices.sensorsInitialized &= ~SENSOR_SI7021_INIT;
    }
}

/**
 * @brief Read BMP280 temperature and pressure sensor
 */
static void Measurement_ReadBMP280(void) {
    // Skip if sensor not initialized
    if (!(devices.sensorsInitialized & SENSOR_BMP280_INIT)) {
        devices.sensorErrorCode |= ERROR_BMP280;
        return;
    }
    
    // Trigger a forced measurement (sensor wakes up, measures, and goes back to sleep)
    if (BMP280_SetCtrlMeas(&hbmp280, BMP280_OVERSAMPLING_X16, BMP280_MODE_FORCED) != HAL_OK) {
        devices.sensorErrorCode |= ERROR_BMP280;
        devices.sensorsInitialized &= ~SENSOR_BMP280_INIT;
        return;
    }
    
    // Small delay for measurement to complete (depends on oversampling settings)
    HAL_Delay(50);
    
    if (BMP280_GetTemperatureAndPressure(&hbmp280) == HAL_OK) {
        devices.data.bmp280_temp = hbmp280.data.temperature;
        devices.data.bmp280_press = hbmp280.data.pressure;
    } else {
        devices.sensorErrorCode |= ERROR_BMP280;
        // Mark sensor as needing reinitialization
        devices.sensorsInitialized &= ~SENSOR_BMP280_INIT;
    }
}

/**
 * @brief Read TSL2561 light intensity sensor
 */
static void Measurement_ReadTSL2561(void) {
    // Skip if sensor not initialized
    if (!(devices.sensorsInitialized & SENSOR_TSL2561_INIT)) {
        devices.sensorErrorCode |= ERROR_TSL2561;
        return;
    }
    
    if (TSL2561_CalculateLux(&htsl2561) == HAL_OK) {
        devices.data.tsl2561_lux = htsl2561.data.lux;
    } else {
        devices.sensorErrorCode |= ERROR_TSL2561;
        // Mark sensor as needing reinitialization
        devices.sensorsInitialized &= ~SENSOR_TSL2561_INIT;
    }
}

/**
 * @brief Perform all measurements sequentially
 */
static void Measurement_ReadAllSensors(void) {
    Measurement_ReadSi7021();
    Measurement_ReadBMP280();
    Measurement_ReadTSL2561();
    
    // All sensors read, measurement cycle complete
    devices.state = MEAS_DONE;
}

/**
 * @brief Handle measurement errors and attempt sensor reinitialization
 */
static void Measurement_HandleError(void) {
    // Try to reinitialize failed sensors
    if (devices.sensorErrorCode & ERROR_SI7021) {
        if (Measurement_InitSi7021() == HAL_OK) {
            devices.sensorErrorCode &= ~ERROR_SI7021;
        }
    }
    
    if (devices.sensorErrorCode & ERROR_BMP280) {
        if (Measurement_InitBMP280() == HAL_OK) {
            devices.sensorErrorCode &= ~ERROR_BMP280;
        }
    }
    
    if (devices.sensorErrorCode & ERROR_TSL2561) {
        if (Measurement_InitTSL2561() == HAL_OK) {
            devices.sensorErrorCode &= ~ERROR_TSL2561;
        }
    }
}

/**
 * @brief Puts all sensors into sleep/power-save mode
 */
void Measurement_SleepSensors(void) {
    // Si7021 - automatically goes to standby after measurement, no explicit sleep needed
    // but we can send a reset command to ensure low power state
    if (devices.sensorsInitialized & SENSOR_SI7021_INIT) {
        // Si7021 is already in standby mode after measurement
        // No additional action needed
    }
    
    // BMP280 - set to sleep mode
    if (devices.sensorsInitialized & SENSOR_BMP280_INIT) {
        BMP280_SetMode(&hbmp280, BMP280_MODE_SLEEP);
    }
    
    // TSL2561 - power off
    if (devices.sensorsInitialized & SENSOR_TSL2561_INIT) {
        TSL2561_PowerOff(&htsl2561);
    }
}

/**
 * @brief Wakes up all sensors from sleep mode
 */
void Measurement_WakeupSensors(void) {
    // Si7021 - wakes up automatically when a measurement command is sent
    // No explicit wake-up needed
    
    // BMP280 - will be woken up in forced mode during measurement
    // No explicit wake-up needed here
    
    // TSL2561 - power on
    if (devices.sensorsInitialized & SENSOR_TSL2561_INIT) {
        TSL2561_PowerOn(&htsl2561);
        // Allow sensor to stabilize after power on
        HAL_Delay(5);
    }
}

/**
 * @brief Process the measurement state machine
 * Executes one state transition per call (non-blocking design).
 * If a sensor fails, other sensors continue reading.
 */
void Measurement_Process(void) 
{
    switch (devices.state) {
        case MEAS_INIT:
            Measurement_InitializeSensors();
            break;
        
        case MEAS_INIT_ERROR:
            // Try to reinitialize only failed sensors
            Measurement_InitializeSensors();
            break;
        
        case MEAS_IDLE:
            // Nothing to do, waiting for Measurement_Start()
            break;
        
        case MEAS_SLEEP:
            // Sensors are in sleep mode, waiting for Measurement_Start()
            break;
        
        case MEAS_WAKEUP:
            // Wake up sensors from sleep mode
            Measurement_WakeupSensors();
            devices.state = MEAS_MEASURE;
            break;
        
        case MEAS_MEASURE:
            // Try to reinitialize any failed sensors before starting measurement
            if (devices.sensorErrorCode != ERROR_NONE) {
                Measurement_HandleError();
            }
            // Perform all measurements sequentially
            Measurement_ReadAllSensors();
            break;

        case MEAS_DONE:
            // Measurement cycle finished, put sensors to sleep
            Measurement_SleepSensors();
            devices.state = MEAS_SLEEP;
            break;

        case MEAS_ERROR:
            // Critical error - attempt recovery
            Measurement_HandleError();
            // If at least one sensor is now working, go to sleep state
            if (devices.sensorsInitialized != 0) {
                devices.state = MEAS_SLEEP;
            }
            // Otherwise stay in error state
            break;

        default:
            // Unknown state - reset to init
            devices.state = MEAS_INIT;
            devices.sensorErrorCode = ERROR_NONE;
            devices.sensorsInitialized = 0;
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
 * @brief Gets the current sensor error code
 * @return Bit flags indicating which sensors have errors
 */
uint8_t Measurement_GetErrorCode(void) {
    return devices.sensorErrorCode;
}

/**
 * @brief Attempts to reinitialize a specific failed sensor
 * @param sensor_error The sensor error flag to reinitialize
 * @return HAL_OK if successful, HAL_ERROR otherwise
 */
HAL_StatusTypeDef Measurement_ReinitSensor(Sensor_Error_t sensor_error) {
    HAL_StatusTypeDef result = HAL_ERROR;
    
    switch (sensor_error) {
        case ERROR_SI7021:
            if (Measurement_InitSi7021() == HAL_OK) {
                devices.sensorErrorCode &= ~ERROR_SI7021;
                result = HAL_OK;
            }
            break;
            
        case ERROR_BMP280:
            if (Measurement_InitBMP280() == HAL_OK) {
                devices.sensorErrorCode &= ~ERROR_BMP280;
                result = HAL_OK;
            }
            break;
            
        case ERROR_TSL2561:
            if (Measurement_InitTSL2561() == HAL_OK) {
                devices.sensorErrorCode &= ~ERROR_TSL2561;
                result = HAL_OK;
            }
            break;
            
        default:
            break;
    }
    
    return result;
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
