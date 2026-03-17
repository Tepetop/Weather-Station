/**
 * @file    measurement.h
 * @brief   Measurement module for multi-sensor data acquisition
 * @details Provides state machine-based measurement management for
 *          Si7021, BMP280 and TSL2561 sensors with error handling
 *          and power management capabilities.
 */

#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include "main.h"
#include "si7021.h"
#include "bmp280.h"
#include "TSL2561.h"
#include "stm32_hal_legacy.h"
#include "stm32f1xx_hal_def.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

/** @brief Maximum retries for sensor reinitialization */
#define MEASUREMENT_MAX_RETRY_COUNT     3

/** @brief Delay between retries in milliseconds */
#define MEASUREMENT_RETRY_DELAY_MS      100

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Sensor error code flags
 * @details Bit flags indicating which sensors have encountered errors.
 *          Multiple flags can be combined using bitwise OR.
 */
typedef enum {
    ERROR_SENSORS_NONE     = 0,         /**< No errors (0x00) */
    ERROR_SI7021   = (1 << 0),  /**< Si7021 sensor error (0x01) */
    ERROR_BMP280   = (1 << 1),  /**< BMP280 sensor error (0x02) */
    ERROR_TSL2561  = (1 << 2),  /**< TSL2561 sensor error (0x04) */
    ERROR_ALL_SENSORS      = (ERROR_SI7021 | ERROR_BMP280 | ERROR_TSL2561) /**< All sensors error (0x07) */
} Sensor_Error_t;

/**
 * @brief State machine states for measurement process
 * @details Defines all possible states of the measurement state machine.
 */
typedef enum {
    MEAS_IDLE,          /**< Waiting for measurement request */
    MEAS_INIT,          /**< Initializing sensors */
    MEAS_INIT_ERROR,    /**< Sensor initialization failed */
    MEAS_WAKEUP,        /**< Waking up sensors from sleep mode */
    MEAS_MEASURE,       /**< Performing all measurements */
    MEAS_DONE,          /**< Measurement cycle complete */
    MEAS_SLEEP,         /**< Sensors in sleep/low-power mode */
    MEAS_ERROR,         /**< Critical error state */
} Measurement_State_t;

/**
 * @brief Structure to hold all sensor measurement data
 */
typedef struct {
    float si7021_temp;      /**< Si7021 temperature in degrees Celsius */
    float si7021_hum;       /**< Si7021 relative humidity in percent */
    float bmp280_temp;      /**< BMP280 temperature in degrees Celsius */
    float bmp280_press;     /**< BMP280 pressure in hPa */
    float tsl2561_lux;      /**< TSL2561 illuminance in lux */
    uint8_t sensorStatus;   /**< Bitwise sensor health flags (Sensor_Error_t). 0 = all OK */
} Measurement_Data_t;

/**
 * @brief Measurement context structure
 * @details Holds all state, error information and measurement data.
 *          Pass this structure to all measurement functions to avoid
 *          reliance on global state.
 */
typedef struct {
    Measurement_State_t state;          /**< Current state machine state */
    uint8_t sensorErrorCode;            /**< Bit flags for sensor errors (Sensor_Error_t) */
    uint8_t initRetryCount;             /**< Counter for initialization retries */
    uint8_t sensorsInitialized;         /**< Bit flags for successfully initialized sensors */
    Measurement_Data_t data;            /**< Latest measurement data */
} Measurement_Context_t;

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

/**
 * @brief   Initializes the measurement module
 * @param   ctx   Pointer to measurement context structure
 * @param   hi2c  Pointer to I2C handle used for sensor communication
 * @retval  None
 * @note    This function must be called before any other measurement functions.
 *          The context structure will be initialized to default values.
 */
HAL_StatusTypeDef Measurement_Init(Measurement_Context_t *ctx, I2C_HandleTypeDef *hi2c);

/**
 * @brief   Starts a new measurement cycle
 * @param   ctx  Pointer to measurement context structure
 * @retval  HAL_OK        Measurement cycle started successfully
 * @retval  HAL_ERROR     Failed to start measurement cycle
 * @details Sets state to MEAS_WAKEUP or MEAS_MEASURE depending on current state.
 *          Only effective when in MEAS_IDLE or MEAS_SLEEP states.
 */
HAL_StatusTypeDef Measurement_Start(Measurement_Context_t *ctx);

/**
 * @brief   Process the measurement state machine
 * @param   ctx  Pointer to measurement context structure
 * @retval  HAL_OK        State machine processed successfully
 * @retval  HAL_ERROR     Failed to process state machine
 * @details This function should be called periodically (e.g., in main loop).
 *          Executes one state transition per call (non-blocking design).
 */
HAL_StatusTypeDef Measurement_Process(Measurement_Context_t *ctx);

/**
 * @brief   Returns the current state of the measurement state machine
 * @param   ctx  Pointer to measurement context structure
 * @retval  Measurement_State_t  Current state
 */
Measurement_State_t Measurement_GetState(const Measurement_Context_t *ctx);

/**
 * @brief   Gets the current sensor error code
 * @param   ctx  Pointer to measurement context structure
 * @retval  uint8_t  Bit flags indicating which sensors have errors (Sensor_Error_t)
 */
uint8_t Measurement_GetErrorCode(const Measurement_Context_t *ctx);

/**
 * @brief   Attempts to reinitialize a specific failed sensor
 * @param   ctx           Pointer to measurement context structure
 * @param   sensor_error  The sensor error flag to reinitialize
 * @retval  HAL_OK        Sensor reinitialized successfully
 * @retval  HAL_ERROR     Reinitialization failed
 */
HAL_StatusTypeDef Measurement_ReinitSensor(Measurement_Context_t *ctx, Sensor_Error_t sensor_error);

/**
 * @brief   Puts all sensors into sleep/power-save mode
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 */
void Measurement_SleepSensors(Measurement_Context_t *ctx);

/**
 * @brief   Wakes up all sensors from sleep mode
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 */
void Measurement_WakeupSensors(Measurement_Context_t *ctx);

/**
 * @brief   Formats the latest measurement data into CSV format
 * @param   ctx     Pointer to measurement context structure
 * @param   buffer  Pointer to buffer where CSV string will be stored
 * @param   len     Maximum length of the buffer
 * @retval  None
 * @note    Output format: "temp1,hum,temp2,press,lux"
 */
void Measurement_GetCSV(const Measurement_Context_t *ctx, char *buffer, uint16_t len);

/**
 * @brief   Gets the latest measurement data directly
 * @param   ctx   Pointer to measurement context structure (source)
 * @param   data  Pointer to Measurement_Data_t structure to fill (destination)
 * @retval  None
 */
void Measurement_GetData(const Measurement_Context_t *ctx, Measurement_Data_t *data);

#endif /* MEASUREMENT_H */
