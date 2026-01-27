#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include "main.h"
#include "si7021.h"
#include "bmp280.h"
#include "TSL2561.h"

/* Configuration */
#define MEASUREMENT_MAX_RETRY_COUNT     3   // Maximum retries for sensor reinitialization
#define MEASUREMENT_RETRY_DELAY_MS      100 // Delay between retries

/**
 * @brief Each sensor error code (bit flags)
 */
typedef enum {
    ERROR_NONE     = 0,         // 0000 0000
    ERROR_SI7021   = (1 << 0),  // 0000 0001
    ERROR_BMP280   = (1 << 1),  // 0000 0010
    ERROR_TSL2561  = (1 << 2),  // 0000 0100
} Sensor_Error_t;

/**
 * @brief State machine states for measurement process
 */
typedef enum {
    MEAS_IDLE,          // Waiting for measurement request
    MEAS_INIT,          // Initializing sensors
    MEAS_INIT_ERROR,    // Sensor initialization failed
    MEAS_WAKEUP,        // Waking up sensors from sleep mode
    MEAS_MEASURE,       // Performing all measurements
    MEAS_DONE,          // Measurement cycle complete
    MEAS_SLEEP,         // Putting sensors to sleep mode
    MEAS_ERROR,         // Critical error state
} Measurement_State_t;

/**
 * @brief Structure to hold all sensor measurement data
 */
typedef struct {
    float si7021_temp;
    float si7021_hum;
    float bmp280_temp;
    float bmp280_press;
    float tsl2561_lux;
} Measurement_Data_t;

/**
 * @brief Structure to hold all states, data and errors from sensors
 */
typedef struct {
    Measurement_State_t state;
    uint8_t sensorErrorCode;
    uint8_t initRetryCount;     // Counter for initialization retries
    uint8_t sensorsInitialized; // Bit flags for initialized sensors
    Measurement_Data_t data;
} Groupe_State_t;

/**
 * @brief Initializes the measurement module
 * @param hi2c Pointer to I2C handle to be used for sensors
 */
void Measurement_Init(I2C_HandleTypeDef *hi2c);

/**
 * @brief Starts a new measurement cycle by setting state to MEAS_STATE_RUN
 */
void Measurement_Start(void);

/**
 * @brief Process the measurement state machine
 * This function should be called periodically
 */
void Measurement_Process(void);

/**
 * @brief Returns the current state of the measurement state machine
 * @return Current state
 */
Measurement_State_t Measurement_GetState(void);

/**
 * @brief Gets the current sensor error code
 * @return Bit flags indicating which sensors have errors
 */
uint8_t Measurement_GetErrorCode(void);

/**
 * @brief Attempts to reinitialize a specific failed sensor
 * @param sensor_error The sensor error flag to reinitialize
 * @return HAL_OK if successful, HAL_ERROR otherwise
 */
HAL_StatusTypeDef Measurement_ReinitSensor(Sensor_Error_t sensor_error);

/**
 * @brief Puts all sensors into sleep/power-save mode
 */
void Measurement_SleepSensors(void);

/**
 * @brief Wakes up all sensors from sleep mode
 */
void Measurement_WakeupSensors(void);

/**
 * @brief Formats the latest measurement data into CSV format
 * @param buffer Pointer to buffer where CSV string will be stored
 * @param len Maximum length of the buffer
 */
void Measurement_GetCSV(char *buffer, uint16_t len);

#endif /* MEASUREMENT_H */
