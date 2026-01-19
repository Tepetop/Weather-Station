#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include "main.h"
#include "si7021.h"
#include "bmp280.h"
#include "TSL2561.h"

/**
 * @brief State machine states for measurement process
 */
typedef enum {
    MEAS_STATE_IDLE,
    MEAS_STATE_INIT,
    MEAS_STATE_SI7021,
    MEAS_STATE_BMP280,
    MEAS_STATE_TSL2561,
    MEAS_STATE_DONE,
    MEAS_STATE_ERROR
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
 * @brief Initializes the measurement module
 * @param hi2c Pointer to I2C handle to be used for sensors
 */
void Measurement_Init(I2C_HandleTypeDef *hi2c);

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
 * @brief Formats the latest measurement data into CSV format
 * @param buffer Pointer to buffer where CSV string will be stored
 * @param len Maximum length of the buffer
 */
void Measurement_GetCSV(char *buffer, uint16_t len);

#endif /* MEASUREMENT_H */
