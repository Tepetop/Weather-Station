/**
 * @file    outdoor_station.h
 * @brief   Outdoor unit application API for STM32F103
 * @details High-level interface for initializing sensors, NRF24 link, and
 *          running the OutdoorLink state machine in the main loop.
 */

#ifndef OUTDOO_RSTATION_H
#define OUTDOO_RSTATION_H

#include "main.h"
#include "measurement.h"

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

/**
 * @brief   Runs one blocking measurement cycle using the station context
 * @param   data        Filled with latest readings on success (may be NULL)
 * @param   timeout_ms  Maximum wait time for the cycle
 * @retval  HAL_OK      Cycle finished in MEAS_SLEEP
 * @retval  HAL_ERROR   Invalid state, start failure, timeout, or MEAS_ERROR
 */
HAL_StatusTypeDef OutdoorStation_RunMeasurementCycle(Measurement_Data_t *data,
                                                     uint32_t timeout_ms);

/**
 * @brief   Initializes outdoor unit hardware and sensors
 * @retval  HAL_OK     Initialization successful
 * @retval  HAL_ERROR  Protocol self-check or sensor init failed
 * @details Configures NRF24 (if present), runs measurement module init to
 *          completion, and logs sensor error flags over UART.
 */
HAL_StatusTypeDef OutdoorStation_Init(void);

/**
 * @brief   Processes the OutdoorLink state machine (call from main loop)
 * @retval  None
 * @details Handles NRF IRQ, measurement commands, sensor reads, TX/ACK, and
 *          radio recovery. Non-blocking — one state step per call.
 */
void OutdoorStation_Process(void);

#endif /* OUTDOORSTATION_H */
