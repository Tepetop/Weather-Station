#include "main.h"
#include "outdoorstation.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "measurement_unit_config.h"
#include "gpio.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"

/* Measurement context for sensor data acquisition */
static Measurement_Context_t measCtx;

/* Data buffer for NRF transmission */
Measurement_Data_t txData;

/* Message buffer for UART transfer */
char Message[128];

/* Message length */
uint8_t Length;

/* NRF24L01 handle instance */
NRF24_Handle_t nrf;

/* OutdoorLink state machine context */
OutdoorLinkContext_t outLink = {0};

/* NRF TX address (multiceiver - derived from NODE_ID) */
const uint8_t NRF_TX_ADDR[5] = {0xC2 + NODE_ID, 0xC2, 0xC2, 0xC2, 0xC2};

/* NRF RX address (multiceiver - derived from NODE_ID) */
const uint8_t NRF_RX_ADDR[5] = {0xE7 + NODE_ID, 0xE7, 0xE7, 0xE7, 0xE7};

static void UartLog(char *msg);
#if CHECK_I2C_DEVICES
static HAL_StatusTypeDef I2C_CheckAddress(I2C_HandleTypeDef *i2c);
#endif
static void Outdoor_LedOn(void);
static void Outdoor_LedOff(void);
static void NRF_DelayUs(uint32_t us);
static HAL_StatusTypeDef OutdoorStation_InitCommunication(void);
static void OutdoorStation_StartReceive(void);
static void OutdoorStation_HandleIRQ(void);
static void OutdoorStation_SendMeasurementData(void);
static void OutdoorStation_InitLink(void);

/* ============================================================================
 * Public API
 * ============================================================================ */

HAL_StatusTypeDef OutdoorStation_Init(void)
{
#if CHECK_I2C_DEVICES
  I2C_CheckAddress(&hi2c2);
#endif

  if (OutdoorStation_InitCommunication() != HAL_OK)
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
    }
  }
/*  Check if sensors initialized successfully */
  if (measCtx.state == MEAS_ERROR)
  {
    Error_Handler_WithName("Failed to initialize sensors");
  }

#if USE_UART_LOGGING
/*      Some UART logging if enabled*/
  if (measCtx.sensorErrorCode == ERROR_SENSORS_NONE)
  {
    UartLog("System initialized\r\n");
  }
  else
  {
    if (measCtx.sensorErrorCode & ERROR_TSL2561)
    {
      UartLog("TSL2561 sensor failed\r\n");
    }
    if (measCtx.sensorErrorCode & ERROR_BMP280)
    {
      UartLog("BMP280 sensor failed\r\n");
    }
    if (measCtx.sensorErrorCode & ERROR_SI7021)
    {
      UartLog("SI7021 sensor failed\r\n");
    }
  }
#endif

  /* Start in RX mode, waiting for commands */
  NRF24_FlushRX(&nrf);
  OutdoorStation_StartReceive();

  return HAL_OK;
}

void OutdoorStation_Process(void)
{
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
#if USE_UART_LOGGING
        UartLog("CMD: Measure request\r\n");
#endif
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
#if USE_UART_LOGGING
        UartLog("MEAS: Done, sending data\r\n");
#endif
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
#if USE_UART_LOGGING
          UartLog("MEAS: No sensors available, sending error status\r\n");
#endif
          break;
        }

        if (outLink.meas_retry_count < OUTDOOR_MEAS_MAX_RETRIES)
        {
          outLink.meas_retry_count++;
          outLink.meas_started = 0;
          Measurement_Init(&measCtx, &hi2c2);
#if USE_UART_LOGGING
          {
            char msg[40];
            sprintf(msg, "MEAS: Retry %d/%d\r\n", outLink.meas_retry_count, OUTDOOR_MEAS_MAX_RETRIES);
            UartLog(msg);
          }
#endif
        }
        else
        {
          /* Max retries exhausted - send whatever data we have with error flags */
          OutdoorStation_SendMeasurementData();
          outLink.state = OUT_LINK_TX_SENDING;
#if USE_UART_LOGGING
          UartLog("MEAS: Max retries, sending partial\r\n");
#endif
        }
        break;
      }

      /* Measurement timeout guard */
      if ((HAL_GetTick() - outLink.meas_start_tick) > OUTDOOR_MEAS_TIMEOUT_MS)
      {
        OutdoorStation_SendMeasurementData();
        outLink.state = OUT_LINK_TX_SENDING;
#if USE_UART_LOGGING
        UartLog("MEAS: Timeout, sending partial\r\n");
#endif
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
#if USE_UART_LOGGING
        UartLog(outLink.tx_ok ? "TX: OK\r\n" : "TX: FAIL (no ACK)\r\n");
#endif

#if USE_LED_INDICATOR
        Outdoor_LedOff();
#endif

#if USE_TIMER_PROFILING
        {
          char timeBuf[50];
          snprintf(timeBuf, sizeof(timeBuf), "Time elapsed: %lu ms\r\n", (HAL_GetTick() - outLink.tx_start_tick));
          UartLog(timeBuf);
        }
#endif

        OutdoorStation_StartReceive();
        outLink.state = OUT_LINK_IDLE;
        break;
      }

      /* TX timeout - hardware did not respond */
      if (outLink.tx_in_progress && (HAL_GetTick() - outLink.tx_start_tick) > NRF_TX_TIMEOUT_MS)
      {
#if USE_UART_LOGGING
        UartLog("TX: Timeout\r\n");
#endif

#if USE_TIMER_PROFILING
        {
          char timeBuf[50];
          snprintf(timeBuf, sizeof(timeBuf), "Time elapsed: %lu ms\r\n", (HAL_GetTick() - outLink.tx_start_tick));
          UartLog(timeBuf);
        }
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
#if USE_UART_LOGGING
      UartLog("Recovery complete\r\n");
#endif
      break;

    default:
      /* Unknown state - enter recovery */
      outLink.state = OUT_LINK_RECOVERY;
      break;
  }
}

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

static void UartLog(char *msg)
{
  uint16_t len = (uint16_t)strlen(msg);
  HAL_UART_Transmit(&huart1, (uint8_t *)msg, len, HAL_MAX_DELAY);
}

#if CHECK_I2C_DEVICES
static HAL_StatusTypeDef I2C_CheckAddress(I2C_HandleTypeDef *i2c)
{
  for (uint8_t addr = 0x01; addr < 0x7F; addr++)
  {
    if (HAL_OK == HAL_I2C_IsDeviceReady(i2c, addr << 1, 1, 100))
    {
      sprintf(Message, "Found I2C device at address: 0x%02X\n\r", addr);
      UartLog(Message);
    }
  }
  return HAL_OK;
}
#endif

static void Outdoor_LedOn(void)
{
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_RESET);
}

static void Outdoor_LedOff(void)
{
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_SET);
}

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

static HAL_StatusTypeDef OutdoorStation_InitCommunication(void)
{
  /* Initialize the nRF24L01 driver */
  if (NRF24_Init(&nrf, &hspi1, NRF_CS_GPIO_Port, NRF_CS_Pin,
                 NRF_CE_GPIO_Port, NRF_CE_Pin, NRF_IRQ_GPIO_Port, NRF_IRQ_Pin,
                 NRF_DelayUs) != HAL_OK)
  {
#if USE_UART_LOGGING
    UartLog("NRF24 Init FAILED!\r\n");
#endif
    return HAL_ERROR;
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
#if USE_UART_LOGGING
  UartLog("NRF24 Init OK - Listening for commands...\r\n");
#endif
  return HAL_OK;
}

static void OutdoorStation_StartReceive(void)
{
  NRF24_SetMode(&nrf, NRF24_MODE_STANDBY);
  NRF24_ClearIRQ(&nrf, NRF24_STATUS_IRQ_MASK);
  NRF24_SetMode(&nrf, NRF24_MODE_RX);
}

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

static void OutdoorStation_SendMeasurementData(void)
{
  /* Get measurement data including sensor status flags */
  Measurement_GetData(&measCtx, &txData);

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
  NRF24_WritePayload(&nrf, (uint8_t *)&txData, sizeof(Measurement_Data_t));
  NRF24_SetMode(&nrf, NRF24_MODE_TX);
}

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
