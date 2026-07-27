/**
 * @file    TSL2561.h
 * @brief   TSL2561 light-to-digital converter driver (I2C)
 * @details Public types and API for AMS TSL2561 over STM32 HAL I2C.
 *          Lux is computed from dual-channel ADC readings per datasheet
 *          formulas in TSL2561_CalculateLux().
 */

#ifndef TSL2561_H
#define TSL2561_H

#include "main.h"
#include <stdbool.h>

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief TSL2561 register addresses and command modifiers
 */
typedef enum {
    TSL2561_REG_COMMAND        = 0x80, /**< Command bit set for all transfers */
    TSL2561_REG_CONTROL        = 0x00, /**< Power control register */
    TSL2561_REG_TIMING         = 0x01, /**< Integration time and gain register */
    TSL2561_REG_THRESHLOWLOW   = 0x02, /**< Low threshold low byte */
    TSL2561_REG_THRESHLOWHIGH  = 0x03, /**< Low threshold high byte */
    TSL2561_REG_THRESHHIGHLOW  = 0x04, /**< High threshold low byte */
    TSL2561_REG_THRESHHIGHHIGH = 0x05, /**< High threshold high byte */
    TSL2561_REG_INTERRUPT      = 0x06, /**< Interrupt control register */
    TSL2561_REG_ID             = 0x0A, /**< Part/revision ID register */
    TSL2561_REG_DATA0LOW       = 0x0C, /**< ADC channel 0 low byte */
    TSL2561_REG_DATA0HIGH      = 0x0D, /**< ADC channel 0 high byte */
    TSL2561_REG_DATA1LOW       = 0x0E, /**< ADC channel 1 low byte */
    TSL2561_REG_DATA1HIGH      = 0x0F, /**< ADC channel 1 high byte */
    TSL2561_REG_BLOCK          = 0x10, /**< Block protocol modifier */
    TSL2561_REG_WORD           = 0x20, /**< Word protocol modifier (bit 5) */
    TSL2561_REG_CLEAR          = 0x40, /**< Interrupt clear modifier (bit 6) */
} TSL2561_Register_t;

/**
 * @brief Power control values for CONTROL register
 */
typedef enum {
    TSL2561_CONTROL_POWEROFF = 0x00, /**< Power down */
    TSL2561_CONTROL_POWERON  = 0x03, /**< Active mode */
} TSL2561_PowerControl_t;

/**
 * @brief Integration time settings for TIMING register
 */
typedef enum {
    TSL2561_INTEG_13MS  = 0x00, /**< 13.7 ms integration time */
    TSL2561_INTEG_101MS = 0x01, /**< 101 ms integration time */
    TSL2561_INTEG_402MS = 0x02, /**< 402 ms integration time */
    TSL2561_SET_MANUAL  = 0x08, /**< Manual integration time bit */
} TSL2561_IntegrationTime_t;

/**
 * @brief Gain settings for TIMING register
 */
typedef enum {
    TSL2561_GAIN_1X  = 0x00, /**< 1x gain */
    TSL2561_GAIN_16X = 0x10, /**< 16x gain */
} TSL2561_Gain_t;

/**
 * @brief Interrupt mode settings for INTERRUPT register
 */
typedef enum {
    TSL2561_INTR_DISABLE = 0x00, /**< Interrupt disabled */
    TSL2561_INTR_LEVEL   = 0x10, /**< Level interrupt */
    TSL2561_INTR_TEST    = 0x30, /**< Test mode */
} TSL2561_InterruptControl_t;

/**
 * @brief Raw ADC counts and computed illuminance
 */
typedef struct {
    uint16_t chan0; /**< Broad-band (visible + IR) channel ADC count */
    uint16_t chan1; /**< IR-only channel ADC count */
    float lux;      /**< Computed illuminance in lux */
} TSL2561_Measurement_t;

/**
 * @brief TSL2561 device handle
 */
typedef struct {
    I2C_HandleTypeDef *hi2c;          /**< HAL I2C handle */
    uint8_t address;                  /**< 8-bit HAL address (7-bit << 1) */
    TSL2561_IntegrationTime_t timing_ms; /**< Cached integration time setting */
    TSL2561_Gain_t gain;              /**< Cached gain setting */
    TSL2561_Measurement_t data;       /**< Latest ADC and lux results */
} TSL2561_t;

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

/** @brief Interrupt persistence: trigger on every ADC cycle */
#define TSL2561_PERSIST_EVERY   0x00
/** @brief Interrupt persistence: any value outside threshold */
#define TSL2561_PERSIST_OUTSIDE 0x01
/** @brief Interrupt persistence: 2 consecutive cycles outside threshold */
#define TSL2561_PERSIST_2       0x02
/** @brief Interrupt persistence: 3 consecutive cycles outside threshold */
#define TSL2561_PERSIST_3       0x03
/** @brief Interrupt persistence: 15 consecutive cycles outside threshold */
#define TSL2561_PERSIST_15      0x0F

/* ============================================================================
 * Public API — Lifecycle
 * ============================================================================ */

/**
 * @brief   Initializes the TSL2561 device handle and powers on the sensor
 * @param   sensor     Pointer to device handle
 * @param   hi2c       Pointer to HAL I2C handle
 * @param   address    7-bit I2C address
 * @param   timing_ms  Integration time setting
 * @param   gain       Gain setting
 * @retval  HAL_OK     Initialization and ID check successful
 * @retval  HAL_ERROR  Null pointer, ID mismatch, or I2C failure
 * @note    Stores address as (address << 1) for HAL I2C APIs.
 */
HAL_StatusTypeDef TSL2561_Init(TSL2561_t *sensor, I2C_HandleTypeDef *hi2c, uint8_t address,
                               TSL2561_IntegrationTime_t timing_ms, TSL2561_Gain_t gain);

/**
 * @brief   Powers on the sensor (CONTROL = 0x03)
 * @param   sensor  Pointer to device handle
 * @retval  HAL_OK     Sensor powered on
 * @retval  HAL_ERROR  I2C write failure
 */
HAL_StatusTypeDef TSL2561_PowerOn(TSL2561_t *sensor);

/**
 * @brief   Powers off the sensor (CONTROL = 0x00)
 * @param   sensor  Pointer to device handle
 * @retval  HAL_OK     Sensor powered off
 * @retval  HAL_ERROR  I2C write failure
 */
HAL_StatusTypeDef TSL2561_PowerOff(TSL2561_t *sensor);

/* ============================================================================
 * Public API — Configuration
 * ============================================================================ */

/**
 * @brief   Configures integration time and gain in the TIMING register
 * @param   sensor  Pointer to device handle
 * @param   time    Integration time setting
 * @param   gain    Gain setting
 * @retval  HAL_OK     TIMING register written
 * @retval  HAL_ERROR  Null pointer or I2C write failure
 */
HAL_StatusTypeDef TSL2561_SetTiming(TSL2561_t *sensor, TSL2561_IntegrationTime_t time,
                                    TSL2561_Gain_t gain);

/**
 * @brief   Sets low and high interrupt thresholds
 * @param   sensor          Pointer to device handle
 * @param   low_threshold   Low threshold ADC value
 * @param   high_threshold  High threshold ADC value
 * @retval  HAL_OK     Threshold registers written
 * @retval  HAL_ERROR  I2C write failure
 */
HAL_StatusTypeDef TSL2561_SetInterruptThreshold(TSL2561_t *sensor, uint16_t low_threshold,
                                                uint16_t high_threshold);

/**
 * @brief   Configures interrupt mode and persistence filter
 * @param   sensor     Pointer to device handle
 * @param   intr_mode  Interrupt mode (TSL2561_InterruptControl_t bits)
 * @param   persist    Persistence count (TSL2561_PERSIST_*)
 * @retval  HAL_OK     INTERRUPT register written
 * @retval  HAL_ERROR  I2C write failure
 */
HAL_StatusTypeDef TSL2561_SetInterruptControl(TSL2561_t *sensor, uint8_t intr_mode, uint8_t persist);

/**
 * @brief   Clears a pending interrupt condition
 * @param   sensor  Pointer to device handle
 * @retval  HAL_OK     Interrupt cleared
 * @retval  HAL_ERROR  I2C write failure
 */
HAL_StatusTypeDef TSL2561_ClearInterrupt(TSL2561_t *sensor);

/* ============================================================================
 * Public API — Measurements
 * ============================================================================ */

/**
 * @brief   Reads part and revision numbers from the ID register
 * @param   sensor   Pointer to device handle
 * @param   part_no  Output: part number (upper nibble)
 * @param   rev_no   Output: revision number (lower nibble)
 * @retval  HAL_OK     ID read successfully
 * @retval  HAL_ERROR  I2C read failure
 */
HAL_StatusTypeDef TSL2561_ReadID(TSL2561_t *sensor, uint8_t *part_no, uint8_t *rev_no);

/**
 * @brief   Reads ADC channel 0 and channel 1 into data.chan0 / data.chan1
 * @param   sensor  Pointer to device handle
 * @retval  HAL_OK     ADC data read successfully
 * @retval  HAL_ERROR  Null pointer or I2C read failure
 */
HAL_StatusTypeDef TSL2561_ReadADC(TSL2561_t *sensor);

/**
 * @brief   Reads ADC channels and computes illuminance in data.lux
 * @param   sensor  Pointer to device handle
 * @retval  HAL_OK     Lux value computed and stored
 * @retval  HAL_ERROR  ADC read failure
 * @details Applies integration-time and gain normalization, then selects
 *          the datasheet lux equation based on chan1/chan0 ratio.
 */
HAL_StatusTypeDef TSL2561_CalculateLux(TSL2561_t *sensor);

#endif /* TSL2561_H */
