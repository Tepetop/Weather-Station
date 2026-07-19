/**
 * @file    outdoor_station.c
 * @brief   Outdoor unit application implementation for STM32F103
 * @details Manages NRF24 communication, measurement command handling, sensor
 *          data encoding, and the OutdoorLink state machine.
 */

#include "main.h"
#include "outdoor_station.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "measurement_unit_config.h"
#include "gpio.h"
#include "i2c.h"
#include "iwdg.h"
#include "spi.h"
#include "usart.h"
#include "ws_protocol.h"
#include "debug_log.h"

/** @brief Measurement context for sensor data acquisition */
static Measurement_Context_t measCtx;

/** @brief NRF TX wire buffer (encoded measurement payload) */
uint8_t txPayload[WS_PROTOCOL_MAX_PAYLOAD];
/** @brief Length of encoded payload in txPayload */
uint8_t txPayloadLen;

/** @brief Message buffer for UART transfer */
char Message[128];

/** @brief Message length for UART transfer */
uint8_t Length;

/** @brief NRF24L01 handle instance */
NRF24_Handle_t nrf;

/** @brief OutdoorLink state machine context */
OutdoorLinkContext_t outLink = {0};

/** @brief NRF availability flag — set after successful presence check */
static uint8_t nrf_available = 0U;
/** @brief Tick of last periodic NRF reinit attempt */
static uint32_t nrf_last_reinit_tick = 0U;

/** @brief NRF TX address (multiceiver — derived from NODE_ID) */
const uint8_t NRF_TX_ADDR[5] = {0xC2 + NODE_ID, 0xC2, 0xC2, 0xC2, 0xC2};

/** @brief NRF RX address (multiceiver — derived from NODE_ID) */
const uint8_t NRF_RX_ADDR[5] = {0xE7 + NODE_ID, 0xE7, 0xE7, 0xE7, 0xE7};

#if CHECK_I2C_DEVICES
static HAL_StatusTypeDef I2C_CheckAddress(I2C_HandleTypeDef *i2c);
#endif

static void Outdoor_LedOn(void);
static void Outdoor_LedOff(void);
static void NRF_DelayUs(uint32_t us);
static HAL_StatusTypeDef OutdoorStation_InitCommunication(void);
static void OutdoorStation_TryReinitNrf(void);
static void OutdoorStation_StartReceive(void);
static void OutdoorStation_HandleIRQ(void);
static void OutdoorStation_SendMeasurementData(void);
static void OutdoorStation_InitLink(void);

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief   Initializes outdoor unit hardware and sensors
 * @retval  HAL_OK     Initialization successful
 * @retval  HAL_ERROR  Protocol self-check or sensor init failed
 */
HAL_StatusTypeDef OutdoorStation_Init(void)
{
#if CHECK_I2C_DEVICES
  I2C_CheckAddress(&hi2c2);
#endif

  nrf_available = 0U;
  nrf_last_reinit_tick = HAL_GetTick();

  if (OutdoorStation_InitCommunication() == HAL_OK)
  {
    nrf_available = 1U;
    NRF24_FlushRX(&nrf);
    OutdoorStation_StartReceive();
  }
  else
  {
    Debug_LogNrfUnavailable();
  }

  if (!WS_Protocol_SelfCheck())
  {
    return HAL_ERROR;
  }

  OutdoorStation_InitLink();

#if USE_LED_INDICATOR
  Outdoor_LedOff();
#endif

  /* Initialize measurement module and process sensor init to completion */
  if(Measurement_Init(&measCtx, &hi2c2) != HAL_OK)
  {
    return HAL_ERROR;
  }
  else
  {
    uint32_t initStart = HAL_GetTick();
    while (measCtx.state != MEAS_SLEEP && measCtx.state != MEAS_ERROR && (HAL_GetTick() - initStart) < 3000U)
    {
      Measurement_Process(&measCtx);
      HAL_IWDG_Refresh(&hiwdg);
    }
  }
/*  Check if sensors initialized successfully */
  if (measCtx.state == MEAS_ERROR)
  {
    Error_Handler_WithName("Failed to initialize sensors");
  }

  Debug_LogSystemReady(measCtx.sensorErrorCode);
  if (measCtx.sensorErrorCode & ERROR_TSL2561)
  {
    Debug_LogSensorError(ERROR_TSL2561, "TSL2561");
  }
  if (measCtx.sensorErrorCode & ERROR_BMP280)
  {
    Debug_LogSensorError(ERROR_BMP280, "BMP280");
  }
  if (measCtx.sensorErrorCode & ERROR_SI7021)
  {
    Debug_LogSensorError(ERROR_SI7021, "SI7021");
  }
    if (measCtx.sensorErrorCode & ERROR_BME280)
  {
    Debug_LogSensorError(ERROR_BME280, "BME280");
  }

  return HAL_OK;
}

/**
 * @brief   Processes the OutdoorLink state machine (call from main loop)
 * @retval  None
 */
void OutdoorStation_Process(void)
{
  OutdoorStation_TryReinitNrf();

  if (!nrf_available)
  {
    return;
  }

  /* ---- Always handle pending IRQ first ---- */
  if (outLink.irq_flag)
  {
    outLink.irq_flag = 0;
    OutdoorStation_HandleIRQ();
  }

  switch (outLink.state)
  {
    case OUT_LINK_IDLE:
      /* Fallback: poll NRF IRQ pin for missed EXTI edges */
      if (HAL_GPIO_ReadPin(NRF_IRQ_GPIO_Port, NRF_IRQ_Pin) == GPIO_PIN_RESET)
      {
        outLink.irq_flag = 1;
      }

      /* Check if a measurement command was received via OutdoorStation_HandleIRQ */
      if (outLink.cmd_received)
      {
        outLink.cmd_received = 0;
        outLink.meas_retry_count = 0;
        outLink.meas_started = 0;
        outLink.meas_start_tick = HAL_GetTick();

#if USE_LED_INDICATOR
        Outdoor_LedOn();
#endif

        Debug_LogMeasCmd();

        outLink.state = OUT_LINK_MEASURING;
      }
      break;

    case OUT_LINK_MEASURING:
      /* Step the measurement state machine */
      Measurement_Process(&measCtx);

      /* If sensors reached a startable state, kick off the measurement */
      if (!outLink.meas_started && (measCtx.state == MEAS_IDLE || measCtx.state == MEAS_SLEEP))
      {
        if (Measurement_Start(&measCtx) == HAL_OK)
        {
          outLink.meas_started = 1;
        }
      }

      /* Measurement cycle complete - send data */
      if (outLink.meas_started && measCtx.state == MEAS_SLEEP)
      {
        OutdoorStation_SendMeasurementData();
        outLink.state = OUT_LINK_TX_SENDING;

        Debug_LogMeasDone();

        break;
      }

      /* Critical sensor error - retry or send partial data */
      if (measCtx.state == MEAS_ERROR)
      {
        /* No sensors available at all - send error status immediately */
        if (measCtx.sensorsInitialized == 0)
        {
          OutdoorStation_SendMeasurementData();
          outLink.state = OUT_LINK_TX_SENDING;

          Debug_LogMeasNoSensors();

          break;
        }

        if (outLink.meas_retry_count < OUTDOOR_MEAS_MAX_RETRIES)
        {
          outLink.meas_retry_count++;
          outLink.meas_started = 0;
          Measurement_Init(&measCtx, &hi2c2);
          Debug_LogMeasRetry(outLink.meas_retry_count, OUTDOOR_MEAS_MAX_RETRIES);

        }
        else
        {
          /* Max retries exhausted - send whatever data we have with error flags */
          OutdoorStation_SendMeasurementData();
          outLink.state = OUT_LINK_TX_SENDING;
          Debug_LogMeasMaxRetries();
        }
        break;
      }

      /* Measurement timeout guard */
      if ((HAL_GetTick() - outLink.meas_start_tick) > OUTDOOR_MEAS_TIMEOUT_MS)
      {
        OutdoorStation_SendMeasurementData();
        outLink.state = OUT_LINK_TX_SENDING;

        Debug_LogMeasTimeout();

      }
      break;

    case OUT_LINK_TX_SENDING:
      /* Poll NRF status for missed IRQ during TX */
      if (!outLink.tx_done)
      {
        uint8_t st = NRF24_GetStatus(&nrf);
        if (st & (NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT))
        {
          outLink.irq_flag = 1;
        }
      }

      /* TX finished (ACK or MAX_RT) */
      if (outLink.tx_done)
      {
        outLink.tx_done = 0;
        outLink.tx_in_progress = 0;

        Debug_LogNrfTxResult(outLink.tx_ok);

#if USE_LED_INDICATOR
        Outdoor_LedOff();
#endif

#if USE_TIMER_PROFILING
        Debug_LogElapsedMs(HAL_GetTick() - outLink.tx_start_tick);
#endif

        OutdoorStation_StartReceive();
        outLink.state = OUT_LINK_IDLE;
        break;
      }

      /* TX timeout - hardware did not respond */
      if (outLink.tx_in_progress && (HAL_GetTick() - outLink.tx_start_tick) > NRF_TX_TIMEOUT_MS)
      {

        Debug_LogNrfTimeout();

#if USE_TIMER_PROFILING
        Debug_LogElapsedMs(HAL_GetTick() - outLink.tx_start_tick);
#endif

        outLink.state = OUT_LINK_RECOVERY;
      }
      break;

    case OUT_LINK_RECOVERY:

#if USE_LED_INDICATOR
      Outdoor_LedOff();
#endif
      /* Reset NRF module to known state */
      NRF24_SetMode(&nrf, NRF24_MODE_STANDBY);
      NRF24_FlushTX(&nrf);
      NRF24_FlushRX(&nrf);
      NRF24_ClearIRQ(&nrf, NRF24_STATUS_IRQ_MASK);

      /* Clear all link flags */
      outLink.tx_in_progress = 0;
      outLink.tx_done = 0;
      outLink.tx_ok = 0;
      outLink.cmd_received = 0;

      OutdoorStation_StartReceive();
      outLink.state = OUT_LINK_IDLE;
      Debug_LogRecovery();
      break;

    default:
      /* Unknown state - enter recovery */
      outLink.state = OUT_LINK_RECOVERY;
      break;
  }
}

/**
 * @brief   EXTI callback for NRF IRQ pin
 * @param   GPIO_Pin  Pin that triggered the interrupt
 * @retval  None
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == NRF_IRQ_Pin)
  {
    outLink.irq_flag = 1;
  }
}

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

#if CHECK_I2C_DEVICES
/**
 * @brief   Scans I2C bus and logs responding device addresses (debug)
 * @param   i2c  Pointer to I2C handle
 * @retval  HAL_OK  Scan completed
 */
static HAL_StatusTypeDef I2C_CheckAddress(I2C_HandleTypeDef *i2c)
{
  for (uint8_t addr = 0x01; addr < 0x7F; addr++)
  {
    if (HAL_OK == HAL_I2C_IsDeviceReady(i2c, addr << 1, 1, 100))
    {
      Debug_LogI2cDevice(addr);
    }
  }
  return HAL_OK;
}
#endif

/**
 * @brief   Turns on the user LED (active low)
 * @retval  None
 */
static void Outdoor_LedOn(void)
{
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_RESET);
}

/**
 * @brief   Turns off the user LED (active low)
 * @retval  None
 */
static void Outdoor_LedOff(void)
{
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_SET);
}

/**
 * @brief   Busy-wait microsecond delay using DWT cycle counter
 * @param   us  Delay duration in microseconds
 * @retval  None
 */
static void NRF_DelayUs(uint32_t us)
{
  /* Enable DWT if not already enabled */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  uint32_t cycles = (SystemCoreClock / 1000000U) * us;
  uint32_t start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < cycles)
  {
  }
}

/**
 * @brief   Initializes NRF24 radio with retries and multiceiver addresses
 * @retval  HAL_OK     Radio present and configured
 * @retval  HAL_ERROR  Init or presence check failed after all retries
 */
static HAL_StatusTypeDef OutdoorStation_InitCommunication(void)
{
  for (uint8_t attempt = 1U; attempt <= NRF_INIT_MAX_RETRIES; attempt++)
  {
    if (NRF24_Init(&nrf, &hspi1, NRF_CS_GPIO_Port, NRF_CS_Pin,
                   NRF_CE_GPIO_Port, NRF_CE_Pin, NRF_IRQ_GPIO_Port, NRF_IRQ_Pin,
                   NRF_DelayUs) != HAL_OK)
    {
      goto init_retry;
    }

    if (NRF24_IsPresent(&nrf) != HAL_OK)
    {
      goto init_retry;
    }

    /* Configure radio parameters - MUST match IndoorUnit */
    NRF24_SetChannel(&nrf, NRF_CHANNEL);
    NRF24_SetDataRate(&nrf, NRF24_DR_1MBPS);
    NRF24_SetPALevel(&nrf, NRF24_PA_MAX);
    NRF24_SetCRC(&nrf, NRF24_CRC_2B);
    NRF24_SetAddressWidth(&nrf, NRF24_AW_5);
    NRF24_SetAutoRetr(&nrf, 1, 10);

    /* Configure addresses (multiceiver - derived from NODE_ID) */
    NRF24_SetTXAddress(&nrf, NRF_TX_ADDR, 5);
    NRF24_SetRXAddress(&nrf, 0, NRF_TX_ADDR, 5);
    NRF24_SetRXAddress(&nrf, 1, NRF_RX_ADDR, 5);

    /* Configure pipes */
    NRF24_SetAutoAck(&nrf, 0, 1);
    NRF24_SetAutoAck(&nrf, 1, 1);
    NRF24_EnablePipe(&nrf, 0, 1);
    NRF24_EnablePipe(&nrf, 1, 1);
    NRF24_SetPayloadSize(&nrf, 0, NRF_PAYLOAD_SIZE);
    NRF24_SetPayloadSize(&nrf, 1, NRF_CMD_SIZE);

    Debug_LogNrfInit(1U);
    Debug_LogNrfListening();
    return HAL_OK;

  init_retry:
    Debug_LogNrfInitRetry(attempt, NRF_INIT_MAX_RETRIES);
    if (attempt < NRF_INIT_MAX_RETRIES)
    {
      HAL_Delay(NRF_INIT_RETRY_DELAY_MS);
    }
  }

  Debug_LogNrfInit(0U);
  return HAL_ERROR;
}

/**
 * @brief   Periodically retries NRF init when radio was unavailable at boot
 * @retval  None
 */
static void OutdoorStation_TryReinitNrf(void)
{
  if (nrf_available)
  {
    return;
  }

  uint32_t now = HAL_GetTick();
  if ((now - nrf_last_reinit_tick) < NRF_REINIT_INTERVAL_MS)
  {
    return;
  }
  nrf_last_reinit_tick = now;

  Debug_LogNrfReinitAttempt();

  if (OutdoorStation_InitCommunication() == HAL_OK)
  {
    nrf_available = 1U;
    OutdoorStation_InitLink();
    NRF24_FlushRX(&nrf);
    OutdoorStation_StartReceive();
    Debug_LogNrfReinitOk();
  }
}

/**
 * @brief   Puts NRF24 into RX mode, flushing pending IRQ flags
 * @retval  None
 */
static void OutdoorStation_StartReceive(void)
{
  if (!nrf_available)
  {
    return;
  }

  NRF24_SetMode(&nrf, NRF24_MODE_STANDBY);
  NRF24_ClearIRQ(&nrf, NRF24_STATUS_IRQ_MASK);
  NRF24_SetMode(&nrf, NRF24_MODE_RX);
}

/**
 * @brief   Handles NRF24 IRQ: RX commands, TX_DS (ACK), and MAX_RT
 * @retval  None
 */
static void OutdoorStation_HandleIRQ(void)
{
  uint8_t status = NRF24_GetStatus(&nrf);
  outLink.last_status = status;

  /* Data received - check if it's a measurement command */
  if (status & NRF24_STATUS_RX_DR)
  {
    uint8_t rx_data[NRF_CMD_SIZE];
    NRF24_ReadPayload(&nrf, rx_data, NRF_CMD_SIZE);
    NRF24_ClearIRQ(&nrf, NRF24_STATUS_RX_DR);

    if (rx_data[0] == CMD_MEASURE)
    {
      outLink.cmd_received = 1;
    }
  }

  /* TX complete (ACK received) */
  if (status & NRF24_STATUS_TX_DS)
  {
    NRF24_ClearIRQ(&nrf, NRF24_STATUS_TX_DS);
    outLink.tx_ok = 1;
    outLink.tx_done = 1;
  }

  /* Max retries reached (no ACK) */
  if (status & NRF24_STATUS_MAX_RT)
  {
    NRF24_ClearIRQ(&nrf, NRF24_STATUS_MAX_RT);
    NRF24_FlushTX(&nrf);
    outLink.tx_ok = 0;
    outLink.tx_done = 1;
  }
}

/**
 * @brief   Encodes measurement data and starts NRF24 TX
 * @retval  None
 * @details Sends header-only frame when no channel readings are available.
 */
static void OutdoorStation_SendMeasurementData(void)
{
  uint8_t wire[NRF_PAYLOAD_SIZE] = {0};

  txPayloadLen = Measurement_EncodePayload(&measCtx, txPayload, sizeof(txPayload));
  if (txPayloadLen == 0U)
  {
    /* ponytail: header-only frame still carries sensor_status */
    WS_Readings_t readings = {.sensor_status = measCtx.data.sensorStatus, .count = 0U};
    (void)WS_Protocol_Encode(&readings, txPayload, sizeof(txPayload), &txPayloadLen);
  }

  memcpy(wire, txPayload, txPayloadLen);

  /* Prepare TX state */
  outLink.irq_flag = 0;
  outLink.tx_done = 0;
  outLink.tx_ok = 0;
  outLink.tx_in_progress = 1;
  outLink.tx_start_tick = HAL_GetTick();

  /* Configure and send */
  NRF24_SetMode(&nrf, NRF24_MODE_STANDBY);
  NRF24_FlushTX(&nrf);
  NRF24_ClearIRQ(&nrf, NRF24_STATUS_IRQ_MASK);
  NRF24_WritePayload(&nrf, wire, NRF_PAYLOAD_SIZE);
  NRF24_SetMode(&nrf, NRF24_MODE_TX);
}

/**
 * @brief   Resets OutdoorLink context to idle defaults
 * @retval  None
 */
static void OutdoorStation_InitLink(void)
{
  outLink.irq_flag = 0U;
  outLink.cmd_received = 0U;
  outLink.tx_in_progress = 0U;
  outLink.tx_done = 0U;
  outLink.tx_ok = 0U;
  outLink.meas_started = 0U;
  outLink.meas_retry_count = 0U;
  outLink.last_status = 0U;
  outLink.tx_start_tick = 0U;
  outLink.meas_start_tick = 0U;
  outLink.state = OUT_LINK_IDLE;
}
