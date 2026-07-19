/**
 * @file    measurement.c
 * @brief   Measurement module implementation for multi-sensor data acquisition
 * @details State machine-based measurement management for Si7021, BMP280 and
 *          TSL2561 sensors with error handling and power management.
 */

#include "measurement.h"
#include "measurement_unit_config.h"
#include "stm32f1xx_hal_def.h"
#include "stm32f1xx_hal_dma.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Private Variables
 * ============================================================================ */

#ifdef SI7021_H
/** @brief Si7021 sensor handle */
static Si7021_t hsi7021;
#endif

#ifdef BMP280_H
// /** @brief BMP280 sensor handle */
 static BMP280_t hbmp280;
#endif

#ifdef BME280_H
/** @brief BME280 sensor handle */
static BME280_t hbme280;
#endif

#ifdef TSL2561_H
/** @brief TSL2561 sensor handle */
 static TSL2561_t htsl2561;
#endif

/** @brief I2C handle for sensor communication */
static I2C_HandleTypeDef *measurement_hi2c;

/** @brief Tick counter for wakeup timing */
static uint32_t measurementWakeupTick;

/* ============================================================================
 * Sensor Initialization Flags
 * ============================================================================ */

#ifdef SI7021_H
/** @brief Si7021 initialization flag */
#define SENSOR_SI7021_INIT  (1 << 0)
#endif

#ifdef BMP280_H
/** @brief BMP280 initialization flag */
#define SENSOR_BMP280_INIT  (1 << 1)
#endif

#ifdef TSL2561_H
/** @brief TSL2561 initialization flag */
#define SENSOR_TSL2561_INIT (1 << 2)
#endif

#ifdef BME280_H
/** @brief BME280 initialization flag */
#define SENSOR_BME280_INIT  (1 << 3)
#endif

/** @brief All sensors initialization mask */
#define ALL_SENSORS_INIT    (SENSOR_SI7021_INIT | SENSOR_TSL2561_INIT | SENSOR_BME280_INIT)

/* ============================================================================
 * Private Function Prototypes
 * ============================================================================ */

static uint32_t Measurement_GetTSL2561IntegrationDelayMs(void);
static HAL_StatusTypeDef Measurement_InitSi7021(Measurement_Context_t *ctx);
static HAL_StatusTypeDef Measurement_InitBME280(Measurement_Context_t *ctx);
static HAL_StatusTypeDef Measurement_InitTSL2561(Measurement_Context_t *ctx);
static void Measurement_InitializeSensors(Measurement_Context_t *ctx);
static void Measurement_ReadSi7021(Measurement_Context_t *ctx);
static void Measurement_ReadBME280(Measurement_Context_t *ctx);
static void Measurement_ReadTSL2561(Measurement_Context_t *ctx);
static void Measurement_ReadAllSensors(Measurement_Context_t *ctx);
static void Measurement_HandleError(Measurement_Context_t *ctx);

/* ============================================================================
 * Private Helper Functions
 * ============================================================================ */

/**
 * @brief   Gets the TSL2561 integration time delay in milliseconds
 * @retval  uint32_t  Integration delay in ms based on current timing setting
 */
static uint32_t Measurement_GetTSL2561IntegrationDelayMs(void) {
#ifdef TSL2561_H
    switch (htsl2561.timing_ms) {
        case TSL2561_INTEG_13MS:
            return 14U;
        case TSL2561_INTEG_101MS:
            return 101U;
        case TSL2561_INTEG_402MS:
        default:
            return 402U;
    }
#else
    return 0U;
#endif
}

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

/**
 * @brief   Initializes the measurement module
 * @param   ctx   Pointer to measurement context structure
 * @param   hi2c  Pointer to I2C handle used for sensor communication
 * @retval  None
 */
HAL_StatusTypeDef Measurement_Init(Measurement_Context_t *ctx, I2C_HandleTypeDef *hi2c) {
    if (ctx == NULL) {
        return HAL_ERROR;
    }
    
    measurement_hi2c = hi2c;
    
    if (measurement_hi2c == NULL) {
        ctx->state = MEAS_ERROR;
        return HAL_ERROR;
    }
    
    ctx->state = MEAS_INIT;
    ctx->sensorErrorCode = ERROR_SENSORS_NONE;
    ctx->data.sensorStatus = ERROR_SENSORS_NONE;
    ctx->initRetryCount = 0;
    ctx->sensorsInitialized = 0;
    measurementWakeupTick = 0U;
    memset(&ctx->data, 0, sizeof(Measurement_Data_t));
    return HAL_OK;
}

/**
 * @brief   Starts a new measurement cycle
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 */
HAL_StatusTypeDef Measurement_Start(Measurement_Context_t *ctx) {
    if (ctx == NULL) {
        return HAL_ERROR;
    }
    
    if (ctx->state == MEAS_IDLE || ctx->state == MEAS_SLEEP) {
        /* Clear error codes from previous measurement */
        ctx->sensorErrorCode = ERROR_SENSORS_NONE;
        ctx->data.sensorStatus = ERROR_SENSORS_NONE;
        measurementWakeupTick = 0U;
        
        /* Wake up sensors first if they were sleeping */
        if (ctx->state == MEAS_SLEEP) {
            ctx->state = MEAS_WAKEUP;
        } else {
            ctx->state = MEAS_MEASURE;
        }
        return HAL_OK;
    }
    return HAL_ERROR;
}

/* ============================================================================
 * Private Sensor Initialization Functions
 * ============================================================================ */
#ifdef SI7021_H
/**
 * @brief   Initialize Si7021 temperature/humidity sensor
 * @param   ctx  Pointer to measurement context structure
 * @retval  HAL_OK     Initialization successful
 * @retval  HAL_ERROR  Initialization failed
 */
static HAL_StatusTypeDef Measurement_InitSi7021(Measurement_Context_t *ctx) {
    if (Si7021_Init(&hsi7021, measurement_hi2c, 0x40, SI7021_RESOLUTION_RH11_TEMP11) != HAL_OK) {
        return HAL_ERROR;
    }
    ctx->sensorsInitialized |= SENSOR_SI7021_INIT;
    return HAL_OK;
}
#endif

#ifdef BMP280_H
/**
 * @brief   Initialize BMP280 pressure/temperature sensor
 * @param   ctx  Pointer to measurement context structure
 * @retval  HAL_OK     Initialization successful
 * @retval  HAL_ERROR  Initialization failed
 */
static HAL_StatusTypeDef Measurement_InitBMP280(Measurement_Context_t *ctx) {
    if (BMP280_Init(&hbmp280, measurement_hi2c, 0x76) != HAL_OK) {
        return HAL_ERROR;
    }
    /* Configure for low power - use FORCED mode instead of NORMAL
     * In FORCED mode, sensor takes one measurement and goes back to sleep */
    BMP280_SetCtrlMeas(&hbmp280, BMP280_OVERSAMPLING_X16, BMP280_MODE_SLEEP);
    BMP280_SetConfig(&hbmp280, BMP280_STANDBY_500_MS, BMP280_FILTER_16);
    ctx->sensorsInitialized |= SENSOR_BMP280_INIT;
    return HAL_OK;
}
#endif

#ifdef TSL2561_H
/**
 * @brief   Initialize TSL2561 light sensor
 * @param   ctx  Pointer to measurement context structure
 * @retval  HAL_OK     Initialization successful
 * @retval  HAL_ERROR  Initialization failed
 */
static HAL_StatusTypeDef Measurement_InitTSL2561(Measurement_Context_t *ctx) {
    if (TSL2561_Init(&htsl2561, measurement_hi2c, 0x39, TSL2561_INTEG_402MS, TSL2561_GAIN_1X) != HAL_OK) {
        return HAL_ERROR;
    }
    /* Power off after init to save power */
    TSL2561_PowerOff(&htsl2561);
    ctx->sensorsInitialized |= SENSOR_TSL2561_INIT;
    return HAL_OK;
}
#endif

#ifdef BME280_H
/**
 * @brief   Initialize BME280 temperature/pressure/humidity sensor
 * @param   ctx  Pointer to measurement context structure
 * @retval  HAL_OK     Initialization successful
 * @retval  HAL_ERROR  Initialization failed
 */
static HAL_StatusTypeDef Measurement_InitBME280(Measurement_Context_t *ctx) {
    if (BME280_Init(&hbme280, measurement_hi2c, 0x76) != HAL_OK) {
        return HAL_ERROR;
    }
    /* Configure for low power - use FORCED mode during measurement */
    BME280_SetCtrlHum(&hbme280, BME280_OVERSAMPLING_X16);
    BME280_SetCtrlMeasSimple(&hbme280, BME280_OVERSAMPLING_X16, BME280_MODE_SLEEP);
    BME280_SetConfig(&hbme280, BME280_STANDBY_500_MS, BME280_FILTER_16);
    if (BME280_ApplySettings(&hbme280) != HAL_OK) {
        return HAL_ERROR;
    }
    ctx->sensorsInitialized |= SENSOR_BME280_INIT;
    return HAL_OK;
}
#endif

/**
 * @brief   Initialize all sensors (Si7021, BMP280, TSL2561, BME280)
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 * @details Attempts to initialize all sensors that are not yet initialized.
 *          Updates ctx->sensorErrorCode and ctx->state based on results.
 */
static void Measurement_InitializeSensors(Measurement_Context_t *ctx) {
    /* Reset error code before initialization */
    ctx->sensorErrorCode = ERROR_SENSORS_NONE;
    ctx->data.sensorStatus = ERROR_SENSORS_NONE;

#ifdef SI7021_H 
    /* Initialize Si7021 */
    if (!(ctx->sensorsInitialized & SENSOR_SI7021_INIT)) {
        if (Measurement_InitSi7021(ctx) != HAL_OK) {
            ctx->sensorErrorCode |= ERROR_SI7021;
        }
    }
#endif

#ifdef BMP280_H
    /* Initialize BMP280 */
    if (!(ctx->sensorsInitialized & SENSOR_BMP280_INIT)) {
        if (Measurement_InitBMP280(ctx) != HAL_OK) {
            ctx->sensorErrorCode |= ERROR_BMP280;
        }
    }
#endif

#ifdef TSL2561_H
    /* Initialize TSL2561 */
    if (!(ctx->sensorsInitialized & SENSOR_TSL2561_INIT)) {
        if (Measurement_InitTSL2561(ctx) != HAL_OK) {
            ctx->sensorErrorCode |= ERROR_TSL2561;
        }
    }
#endif

#ifdef BME280_H
    /* Initialize BME280 */
    if (!(ctx->sensorsInitialized & SENSOR_BME280_INIT)) {
        if (Measurement_InitBME280(ctx) != HAL_OK) {
            ctx->sensorErrorCode |= ERROR_BME280;
        }
    }
#endif

    /* Transition to next state based on initialization result */
    if (ctx->sensorErrorCode != ERROR_SENSORS_NONE) {
        ctx->initRetryCount++;
        if (ctx->initRetryCount >= MEASUREMENT_MAX_RETRY_COUNT) {
            /* Max retries reached, go to error state but allow partial operation */
            if (ctx->sensorsInitialized != 0) {
                /* At least one sensor initialized - go to sleep and allow measurements */
                ctx->state = MEAS_SLEEP;
            } else {
                /* No sensors initialized - critical error */
                ctx->state = MEAS_ERROR;
            }
        } else {
            ctx->state = MEAS_INIT_ERROR;
        }
    } else {
        ctx->initRetryCount = 0;
        ctx->state = MEAS_SLEEP; /* Go to sleep mode after successful init */
    }
}

/* ============================================================================
 * Private Sensor Reading Functions
 * ============================================================================ */

#ifdef SI7021_H
/**
 * @brief   Read Si7021 temperature and humidity sensor
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 */
static void Measurement_ReadSi7021(Measurement_Context_t *ctx) {
    /* Skip if sensor not initialized */
    if (!(ctx->sensorsInitialized & SENSOR_SI7021_INIT)) {
        ctx->data.sensorStatus |= ERROR_SI7021;
        ctx->sensorErrorCode |= ERROR_SI7021;
        return;
    }
    
    if (Si7021_ReadHumidityAndTemperature(&hsi7021) == HAL_OK) {
        ctx->data.si7021_temp = hsi7021.data.temperature;
        ctx->data.si7021_hum = hsi7021.data.humidity;
    } else {
        ctx->data.sensorStatus |= ERROR_SI7021;
        ctx->sensorErrorCode |= ERROR_SI7021;
        /* Mark sensor as needing reinitialization */
        ctx->sensorsInitialized &= ~SENSOR_SI7021_INIT;
    }
}
#endif

#ifdef BMP280_H
/**
 * @brief   Read BMP280 temperature and pressure sensor
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 */
static void Measurement_ReadBMP280(Measurement_Context_t *ctx) {
    /* Skip if sensor not initialized */
    if (!(ctx->sensorsInitialized & SENSOR_BMP280_INIT)) {
        ctx->data.sensorStatus |= ERROR_BMP280;
        ctx->sensorErrorCode |= ERROR_BMP280;
        return;
    }
    
    /* Trigger a forced measurement (sensor wakes up, measures, and goes back to sleep) */
    if (BMP280_SetCtrlMeas(&hbmp280, BMP280_OVERSAMPLING_X16, BMP280_MODE_FORCED) != HAL_OK) {
        ctx->sensorErrorCode |= ERROR_BMP280;
        ctx->sensorsInitialized &= ~SENSOR_BMP280_INIT;
        return;
    }
    
    /* Small delay for measurement to complete (depends on oversampling settings) */
    HAL_Delay(50);
    
    if (BMP280_GetTemperatureAndPressure(&hbmp280) == HAL_OK) {
        ctx->data.bmp280_temp = hbmp280.data.temperature;
        ctx->data.bmp280_press = hbmp280.data.pressure;
    } else {
        ctx->data.sensorStatus |= ERROR_BMP280;
        ctx->sensorErrorCode |= ERROR_BMP280;
        /* Mark sensor as needing reinitialization */
        ctx->sensorsInitialized &= ~SENSOR_BMP280_INIT;
    }
}
#endif

#ifdef TSL2561_H
/**
 * @brief   Read TSL2561 light intensity sensor
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 */
static void Measurement_ReadTSL2561(Measurement_Context_t *ctx) {
    /* Skip if sensor not initialized */
    if (!(ctx->sensorsInitialized & SENSOR_TSL2561_INIT)) {
        ctx->data.sensorStatus |= ERROR_TSL2561;
        ctx->sensorErrorCode |= ERROR_TSL2561;
        return;
    }
    
    if (TSL2561_CalculateLux(&htsl2561) == HAL_OK) {
        ctx->data.tsl2561_lux = htsl2561.data.lux;
    } else {
        ctx->data.sensorStatus |= ERROR_TSL2561;
        ctx->sensorErrorCode |= ERROR_TSL2561;
        /* Mark sensor as needing reinitialization */
        ctx->sensorsInitialized &= ~SENSOR_TSL2561_INIT;
    }
}
#endif

#ifdef BME280_H
/**
 * @brief   Read BME280 temperature, pressure and humidity sensor
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 */
static void Measurement_ReadBME280(Measurement_Context_t *ctx) {
    uint32_t timeout_ms;

    if (!(ctx->sensorsInitialized & SENSOR_BME280_INIT)) {
        ctx->data.sensorStatus |= ERROR_BME280;
        ctx->sensorErrorCode |= ERROR_BME280;
        return;
    }

    BME280_SetCtrlHum(&hbme280, BME280_OVERSAMPLING_X16);
    if (BME280_SetCtrlMeasSimple(&hbme280, BME280_OVERSAMPLING_X16, BME280_MODE_FORCED) != HAL_OK ||
        BME280_ApplySettings(&hbme280) != HAL_OK) {
        ctx->sensorErrorCode |= ERROR_BME280;
        ctx->sensorsInitialized &= ~SENSOR_BME280_INIT;
        return;
    }

    timeout_ms = BME280_GetMeasurementDurationMs(&hbme280, 1U) + 5U;
    if (BME280_WaitForMeasurement(&hbme280, timeout_ms) != HAL_OK) {
        ctx->data.sensorStatus |= ERROR_BME280;
        ctx->sensorErrorCode |= ERROR_BME280;
        ctx->sensorsInitialized &= ~SENSOR_BME280_INIT;
        return;
    }

    if (BME280_GetTemperaturePressureHumidity(&hbme280) == HAL_OK) {
        ctx->data.bme280_temp = hbme280.data.temperature;
        ctx->data.bme280_press = hbme280.data.pressure;
        ctx->data.bme280_hum = hbme280.data.humidity;
    } else {
        ctx->data.sensorStatus |= ERROR_BME280;
        ctx->sensorErrorCode |= ERROR_BME280;
        ctx->sensorsInitialized &= ~SENSOR_BME280_INIT;
    }
}
#endif

/**
 * @brief   Perform all measurements sequentially
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 */
static void Measurement_ReadAllSensors(Measurement_Context_t *ctx) {
#ifdef SI7021_H
    Measurement_ReadSi7021(ctx);
#endif

#ifdef BMP280_H
    Measurement_ReadBMP280(ctx);
#endif

#ifdef TSL2561_H
    Measurement_ReadTSL2561(ctx);
#endif

#ifdef BME280_H
    Measurement_ReadBME280(ctx);
#endif

    /* All sensors read, measurement cycle complete */
    ctx->state = MEAS_DONE;
}

/**
 * @brief   Handle measurement errors and attempt sensor reinitialization
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 */
static void Measurement_HandleError(Measurement_Context_t *ctx) {
    /* Try to reinitialize failed sensors */
#ifdef SI7021_H
    if (ctx->sensorErrorCode & ERROR_SI7021) {
        if (Measurement_InitSi7021(ctx) == HAL_OK) {
            ctx->sensorErrorCode &= ~ERROR_SI7021;
        }
    }
#endif

#ifdef BMP280_H
    if (ctx->sensorErrorCode & ERROR_BMP280) {
        if (Measurement_InitBMP280(ctx) == HAL_OK) {
            ctx->sensorErrorCode &= ~ERROR_BMP280;
        }
    }
#endif

#ifdef TSL2561_H
    if (ctx->sensorErrorCode & ERROR_TSL2561) {
        if (Measurement_InitTSL2561(ctx) == HAL_OK) {
            ctx->sensorErrorCode &= ~ERROR_TSL2561;
        }
    }
#endif

#ifdef BME280_H
    if (ctx->sensorErrorCode & ERROR_BME280) {
        if (Measurement_InitBME280(ctx) == HAL_OK) {
            ctx->sensorErrorCode &= ~ERROR_BME280;
        }
    }
#endif
}

/* ============================================================================
 * Power Management Functions
 * ============================================================================ */

/**
 * @brief   Puts all sensors into sleep/power-save mode
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 */
void Measurement_SleepSensors(Measurement_Context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    
    /* Si7021 - automatically goes to standby after measurement, no explicit sleep needed */
    /* Si7021 is already in standby mode after measurement - no additional action needed */

#ifdef BMP280_H
    /* BMP280 - set to sleep mode */
    if (ctx->sensorsInitialized & SENSOR_BMP280_INIT) {
        BMP280_SetMode(&hbmp280, BMP280_MODE_SLEEP);
    }
#endif

#ifdef TSL2561_H
    /* TSL2561 - power off */
    if (ctx->sensorsInitialized & SENSOR_TSL2561_INIT) {
        TSL2561_PowerOff(&htsl2561);
    }
#endif

#ifdef BME280_H
    if (ctx->sensorsInitialized & SENSOR_BME280_INIT) {
        BME280_SetMode(&hbme280, BME280_MODE_SLEEP);
    }
#endif
}

/**
 * @brief   Wakes up all sensors from sleep mode
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 */
void Measurement_WakeupSensors(Measurement_Context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    
    /* Si7021 - wakes up automatically when a measurement command is sent */
    /* No explicit wake-up needed */
    
    /* BMP280 - will be woken up in forced mode during measurement */
    /* No explicit wake-up needed here */
    
#ifdef TSL2561_H
    /* TSL2561 - power on */
    if (ctx->sensorsInitialized & SENSOR_TSL2561_INIT) {
        if (TSL2561_PowerOn(&htsl2561) != HAL_OK) {
            ctx->sensorsInitialized &= ~SENSOR_TSL2561_INIT;
            ctx->sensorErrorCode |= ERROR_TSL2561;
        }
    }
#endif
}

/* ============================================================================
 * State Machine Processing
 * ============================================================================ */

/**
 * @brief   Process the measurement state machine
 * @param   ctx  Pointer to measurement context structure
 * @retval  None
 * @details Executes one state transition per call (non-blocking design).
 *          If a sensor fails, other sensors continue reading.
 */
HAL_StatusTypeDef Measurement_Process(Measurement_Context_t *ctx)
{
    if (ctx == NULL) {
        return HAL_ERROR;
    }
    
    switch (ctx->state) {
        case MEAS_INIT:
            Measurement_InitializeSensors(ctx);
            break;
        
        case MEAS_INIT_ERROR:
            /* Try to reinitialize only failed sensors */
            Measurement_InitializeSensors(ctx);
            break;
        
        case MEAS_IDLE:
            /* Nothing to do, waiting for Measurement_Start() */
            break;
        
        case MEAS_SLEEP:
            /* Sensors are in sleep mode, waiting for Measurement_Start() */
            break;
        
        case MEAS_WAKEUP:
            if (measurementWakeupTick == 0U) {
                Measurement_WakeupSensors(ctx);
#ifdef TSL2561_H
                if (ctx->sensorsInitialized & SENSOR_TSL2561_INIT) {
                    measurementWakeupTick = HAL_GetTick();
                } else {
                    ctx->state = MEAS_MEASURE;
                }
#else
                ctx->state = MEAS_MEASURE;
#endif
                break;
            }
#ifdef TSL2561_H
            if ((HAL_GetTick() - measurementWakeupTick) >= Measurement_GetTSL2561IntegrationDelayMs()) {
                measurementWakeupTick = 0U;
                ctx->state = MEAS_MEASURE;
            }
#endif
            break;
        
        case MEAS_MEASURE:
            /* Try to reinitialize any failed sensors before starting measurement */
            if (ctx->sensorErrorCode == ERROR_ALL_SENSORS) {
                Measurement_HandleError(ctx);
            }
            /* Perform all measurements sequentially */
            Measurement_ReadAllSensors(ctx);
            break;

        case MEAS_DONE:
            /* Measurement cycle finished, put sensors to sleep */
            Measurement_SleepSensors(ctx);
            measurementWakeupTick = 0U;
            ctx->state = MEAS_SLEEP;
            break;

        case MEAS_ERROR:
            /* Critical error - attempt recovery */
            Measurement_HandleError(ctx);
            measurementWakeupTick = 0U;
            /* If at least one sensor is now working, go to sleep state */
            if (ctx->sensorsInitialized != 0) {
                ctx->state = MEAS_SLEEP;
            }
            if (ctx->sensorErrorCode == ERROR_ALL_SENSORS) {
                /* All sensors failed - stay in error state but allow retries */
                ctx->state = MEAS_ERROR;
            }
            /* Otherwise stay in error state */
            break;

        default:
            /* Unknown state - reset to init */
            ctx->state = MEAS_INIT;
            ctx->sensorErrorCode = ERROR_SENSORS_NONE;
            ctx->sensorsInitialized = 0;
            break;
    }
    return HAL_OK;
}

/* ============================================================================
 * Getter Functions
 * ============================================================================ */

/**
 * @brief   Returns the current state of the measurement state machine
 * @param   ctx  Pointer to measurement context structure
 * @retval  Measurement_State_t  Current state
 */
Measurement_State_t Measurement_GetState(const Measurement_Context_t *ctx) {
    if (ctx == NULL) {
        return MEAS_ERROR;
    }
    return ctx->state;
}

/**
 * @brief   Gets the current sensor error code
 * @param   ctx  Pointer to measurement context structure
 * @retval  uint8_t  Bit flags indicating which sensors have errors
 */
uint8_t Measurement_GetErrorCode(const Measurement_Context_t *ctx) {
    if (ctx == NULL) {
        return ERROR_SENSORS_NONE;
    }
    return ctx->sensorErrorCode;
}

/**
 * @brief   Attempts to reinitialize a specific failed sensor
 * @param   ctx           Pointer to measurement context structure
 * @param   sensor_error  The sensor error flag to reinitialize
 * @retval  HAL_OK        Sensor reinitialized successfully
 * @retval  HAL_ERROR     Reinitialization failed or invalid context
 */
HAL_StatusTypeDef Measurement_ReinitSensor(Measurement_Context_t *ctx, Sensor_Error_t sensor_error) {
    if (ctx == NULL) {
        return HAL_ERROR;
    }
    
    HAL_StatusTypeDef result = HAL_ERROR;
    
    switch (sensor_error) {
        case ERROR_SI7021:
            if (Measurement_InitSi7021(ctx) == HAL_OK) {
                ctx->sensorErrorCode &= ~ERROR_SI7021;
                ctx->data.sensorStatus &= ~ERROR_SI7021;
                result = HAL_OK;
            }
            break;
            
#ifdef BMP280_H
        case ERROR_BMP280:
            if (Measurement_InitBMP280(ctx) == HAL_OK) {
                ctx->sensorErrorCode &= ~ERROR_BMP280;
                ctx->data.sensorStatus &= ~ERROR_BMP280;
                result = HAL_OK;
            }
            break;
#endif

        case ERROR_TSL2561:
            if (Measurement_InitTSL2561(ctx) == HAL_OK) {
                ctx->sensorErrorCode &= ~ERROR_TSL2561;
                ctx->data.sensorStatus &= ~ERROR_TSL2561;
                result = HAL_OK;
            }
            break;

        case ERROR_BME280:
            if (Measurement_InitBME280(ctx) == HAL_OK) {
                ctx->sensorErrorCode &= ~ERROR_BME280;
                ctx->data.sensorStatus &= ~ERROR_BME280;
                result = HAL_OK;
            }
            break;
            
        default:
            break;
    }
    
    return result;
}

/**
 * @brief   Returns channel value from internal measurement cache
 */
static float Measurement_GetChannelValue(const Measurement_Data_t *data, uint8_t channel_id) {
    switch (channel_id) {
#ifdef SI7021_H
        case WS_CH_SI7021_TEMP:
            return data->si7021_temp;
        case WS_CH_SI7021_HUM:
            return data->si7021_hum;
#endif
#ifdef BMP280_H
        case WS_CH_BMP280_TEMP:
            return data->bmp280_temp;
        case WS_CH_BMP280_PRESS:
            return data->bmp280_press;
#endif
#ifdef BME280_H
        case WS_CH_BMP280_TEMP:
            return data->bme280_temp;
        case WS_CH_BMP280_PRESS:
            return data->bme280_press;
#endif
#ifdef TSL2561_H
        case WS_CH_TSL2561_LUX:
            return data->tsl2561_lux;
#endif
        default:
            return 0.0f;
    }
}

bool Measurement_BuildReadings(const Measurement_Context_t *ctx, WS_Readings_t *out) {
    if ((ctx == NULL) || (out == NULL)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->sensor_status = ctx->data.sensorStatus;

    for (uint8_t i = 0U; i < ENABLED_CHANNEL_COUNT; i++) {
        uint8_t channel_id = ENABLED_CHANNELS[i];
        uint8_t err_mask = WS_ChannelSensorError(channel_id);

        if ((err_mask != 0U) && ((ctx->data.sensorStatus & err_mask) != 0U)) {
            continue;
        }

        if (out->count >= WS_MAX_READINGS) {
            break;
        }

        out->readings[out->count].channel_id = channel_id;
        out->readings[out->count].value = Measurement_GetChannelValue(&ctx->data, channel_id);
        out->count++;
    }

    return (out->count > 0U);
}

uint8_t Measurement_EncodePayload(const Measurement_Context_t *ctx, uint8_t *buf, uint8_t buf_size) {
    WS_Readings_t readings;
    uint8_t encoded_len = 0U;

    if (!Measurement_BuildReadings(ctx, &readings)) {
        return 0U;
    }

    if (!WS_Protocol_Encode(&readings, buf, buf_size, &encoded_len)) {
        return 0U;
    }

    return encoded_len;
}

/**
 * @brief   Gets the latest measurement data directly
 * @param   ctx   Pointer to measurement context structure (source)
 * @param   data  Pointer to Measurement_Data_t structure to fill (destination)
 * @retval  None
 */
void Measurement_GetData(const Measurement_Context_t *ctx, Measurement_Data_t *data) {
    if (ctx == NULL || data == NULL) {
        return;
    }
    *data = ctx->data;
}
