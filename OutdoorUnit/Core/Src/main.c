/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "i2c.h"
#include "measurement.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "measurement_unit_config.h"


/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */


/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */


/* NRF24L01 configuration */

/* Node identity — change this per outdoor unit (0-3) */
#define NODE_ID          0U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/** @brief Measurement context for sensor data acquisition */
static Measurement_Context_t measCtx;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* Debug/Logging functions */
void UartLog(char *msg);
HAL_StatusTypeDef I2C_CheckAddress(I2C_HandleTypeDef *i2c);

/* LED control */
static void Outdoor_LedOn(void);
static void Outdoor_LedOff(void);

/* NRF24L01 functions */
static void NRF_DelayUs(uint32_t us);
static HAL_StatusTypeDef NRF_InitOutdoorUnit(void);
static void NRF_StartReceive(void);
static void NRF_HandleIRQ(void);
static void NRF_SendMeasurementData(void);

/* OutdoorLink context */
static void OutLink_Init(void);

/* Main state machine */
static void OutdoorLink_Process(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C2_Init();
  MX_USART1_UART_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

#if CHECK_I2C_DEVICES
  I2C_CheckAddress(&hi2c2);
#endif

  /* Initialize NRF24L01 with retries */
  {
    uint8_t nrf_ok = 0;
    for (uint8_t i = 0; i < NRF_INIT_MAX_RETRIES; i++) {
      if (NRF_InitOutdoorUnit() == HAL_OK) {
        nrf_ok = 1;
        break;
      }
      HAL_Delay(NRF_INIT_RETRY_DELAY_MS);
    }
    if (!nrf_ok) {
      Error_Handler();
    }
  }

  OutLink_Init();

#if USE_LED_INDICATOR
  Outdoor_LedOff();
#endif

  /* Initialize measurement module and process sensor init to completion */
  Measurement_Init(&measCtx, &hi2c2);
  {
    uint32_t initStart = HAL_GetTick();
    while (measCtx.state != MEAS_SLEEP && measCtx.state != MEAS_ERROR
           && (HAL_GetTick() - initStart) < 3000U) {
      Measurement_Process(&measCtx);
    }
  }

#if USE_UART_LOGGING
  UartLog("System initialized\r\n");
#endif

  /* Start in RX mode, waiting for commands */
  NRF24_FlushRX(&nrf);
  NRF_StartReceive();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    OutdoorLink_Process();

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void){
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* ============================================================================
 * Debug/Logging Functions
 * ============================================================================ */

/**
 * @brief   Send a debug message via UART
 * @param   msg  Null-terminated string to send
 * @retval  None
 */
void UartLog(char *msg)
{
  uint16_t len = strlen(msg);
  HAL_UART_Transmit(&huart1, (uint8_t *)msg, len, HAL_MAX_DELAY);
}

/**
 * @brief   Scan I2C bus for connected devices
 * @param   i2c  Pointer to I2C handle
 * @retval  HAL_OK always (logs found devices via UART)
 */
HAL_StatusTypeDef I2C_CheckAddress(I2C_HandleTypeDef *i2c)
{
  for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
    if (HAL_OK == HAL_I2C_IsDeviceReady(i2c, addr << 1, 1, 100)) {
      sprintf(Message, "Found I2C device at address: 0x%02X\n\r", addr);
      UartLog(Message);
    }
  }
  return HAL_OK;
}

/* ============================================================================
 * LED Control Functions
 * ============================================================================ */

/**
 * @brief   Turn on the user LED (active low)
 * @retval  None
 */
static void Outdoor_LedOn(void)
{
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_RESET);
}

/**
 * @brief   Turn off the user LED (active low)
 * @retval  None
 */
static void Outdoor_LedOff(void)
{
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_SET);
}

/* ============================================================================
 * NRF24L01 Functions
 * ============================================================================ */

/**
 * @brief   Microsecond delay using DWT cycle counter
 * @param   us  Number of microseconds to delay
 * @retval  None
 * @note    Requires DWT (available on Cortex-M3)
 */
static void NRF_DelayUs(uint32_t us)
{
  /* Enable DWT if not already enabled */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  uint32_t cycles = (SystemCoreClock / 1000000U) * us;
  uint32_t start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < cycles);
}

/**
 * @brief   Initialize nRF24L01 for OutdoorUnit (slave/receiver mode)
 * @retval  HAL_OK     Initialization successful
 * @retval  HAL_ERROR  Initialization failed
 * @details Configures radio parameters, addresses, and pipes.
 *          Settings must match IndoorUnit configuration.
 */
static HAL_StatusTypeDef NRF_InitOutdoorUnit(void)
{
  /* Initialize the nRF24L01 driver */
  if (NRF24_Init(&nrf, &hspi1,NRF_CS_GPIO_Port, NRF_CS_Pin,NRF_CE_GPIO_Port, NRF_CE_Pin,NRF_IRQ_GPIO_Port, NRF_IRQ_Pin, NRF_DelayUs) != HAL_OK) {
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

/**
 * @brief   Switch nRF24L01 to RX mode and start listening
 * @retval  None
 */
static void NRF_StartReceive(void)
{
  NRF24_SetMode(&nrf, NRF24_MODE_STANDBY);
  NRF24_ClearIRQ(&nrf, NRF24_STATUS_IRQ_MASK);
  NRF24_SetMode(&nrf, NRF24_MODE_RX);
}

/**
 * @brief   Handle nRF24L01 IRQ events
 * @retval  None
 * @details Processes RX_DR, TX_DS, and MAX_RT interrupts
 */
static void NRF_HandleIRQ(void)
{
  uint8_t status = NRF24_GetStatus(&nrf);
  outLink.last_status = status;

  /* Data received - check if it's a measurement command */
  if (status & NRF24_STATUS_RX_DR) {
    uint8_t rx_data[NRF_CMD_SIZE];
    NRF24_ReadPayload(&nrf, rx_data, NRF_CMD_SIZE);
    NRF24_ClearIRQ(&nrf, NRF24_STATUS_RX_DR);

    if (rx_data[0] == CMD_MEASURE) {
      outLink.cmd_received = 1;
    }
  }

  /* TX complete (ACK received) */
  if (status & NRF24_STATUS_TX_DS) {
    NRF24_ClearIRQ(&nrf, NRF24_STATUS_TX_DS);
    outLink.tx_ok = 1;
    outLink.tx_done = 1;
  }

  /* Max retries reached (no ACK) */
  if (status & NRF24_STATUS_MAX_RT) {
    NRF24_ClearIRQ(&nrf, NRF24_STATUS_MAX_RT);
    NRF24_FlushTX(&nrf);
    outLink.tx_ok = 0;
    outLink.tx_done = 1;
  }
}

/**
 * @brief   Send measurement data to IndoorUnit via nRF24L01
 * @retval  None
 * @details Fills txData from measCtx (including sensorStatus) and initiates TX.
 */
static void NRF_SendMeasurementData(void)
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

/**
 * @brief   GPIO EXTI callback for NRF24L01 IRQ pin
 * @param   GPIO_Pin  Pin that triggered the interrupt
 * @retval  None
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == NRF_IRQ_Pin) {
    outLink.irq_flag = 1;
  }
}

/* ============================================================================
 * OutdoorLink Context Functions
 * ============================================================================ */

/**
 * @brief   Initialize OutdoorLink context to default state
 * @retval  None
 */
static void OutLink_Init(void)
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

/* ============================================================================
 * Main State Machine
 * ============================================================================ */

/**
 * @brief   Non-blocking state machine for the outdoor unit main loop
 * @details Manages the complete flow: receive command → measure → transmit → idle.
 *          Includes timeout protection and error recovery at every stage.
 *          Replaces the previous sequential MainLoop_* functions to prevent
 *          blocking while loops that could stall the system.
 */
static void OutdoorLink_Process(void)
{
  /* ---- Always handle pending IRQ first ---- */
  if (outLink.irq_flag) {
    outLink.irq_flag = 0;
    NRF_HandleIRQ();
  }

  switch (outLink.state) {

  /* ============================================================
   * IDLE — RX mode, listening for commands from IndoorUnit
   * ============================================================ */
  case OUT_LINK_IDLE:
    /* Fallback: poll NRF IRQ pin for missed EXTI edges */
    if (HAL_GPIO_ReadPin(NRF_IRQ_GPIO_Port, NRF_IRQ_Pin) == GPIO_PIN_RESET) {
      outLink.irq_flag = 1;
    }

    /* Check if a measurement command was received via NRF_HandleIRQ */
    if (outLink.cmd_received) {
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

  /* ============================================================
   * MEASURING — Run measurement state machine (non-blocking)
   * ============================================================ */
  case OUT_LINK_MEASURING:
    /* Step the measurement state machine */
    Measurement_Process(&measCtx);

    /* If sensors reached a startable state, kick off the measurement */
    if (!outLink.meas_started &&
        (measCtx.state == MEAS_IDLE || measCtx.state == MEAS_SLEEP)) {
      if (Measurement_Start(&measCtx) == HAL_OK) {
        outLink.meas_started = 1;
      }
    }

    /* Measurement cycle complete — send data */
    if (outLink.meas_started && measCtx.state == MEAS_SLEEP) {
      NRF_SendMeasurementData();
      outLink.state = OUT_LINK_TX_SENDING;
#if USE_UART_LOGGING
      UartLog("MEAS: Done, sending data\r\n");
#endif
      break;
    }

    /* Critical sensor error — retry or send partial data */
    if (measCtx.state == MEAS_ERROR) {
      if (outLink.meas_retry_count < OUTDOOR_MEAS_MAX_RETRIES) {
        outLink.meas_retry_count++;
        outLink.meas_started = 0;
        Measurement_Init(&measCtx, &hi2c2);
        outLink.meas_start_tick = HAL_GetTick();
#if USE_UART_LOGGING
        {
          char msg[40];
          sprintf(msg, "MEAS: Retry %d/%d\r\n",
                  outLink.meas_retry_count, OUTDOOR_MEAS_MAX_RETRIES);
          UartLog(msg);
        }
#endif
      } else {
        /* Max retries exhausted — send whatever data we have with error flags */
        NRF_SendMeasurementData();
        outLink.state = OUT_LINK_TX_SENDING;
#if USE_UART_LOGGING
        UartLog("MEAS: Max retries, sending partial\r\n");
#endif
      }
      break;
    }

    /* Measurement timeout guard */
    if ((HAL_GetTick() - outLink.meas_start_tick) > OUTDOOR_MEAS_TIMEOUT_MS) {
      NRF_SendMeasurementData();
      outLink.state = OUT_LINK_TX_SENDING;
#if USE_UART_LOGGING
      UartLog("MEAS: Timeout, sending partial\r\n");
#endif
    }
    break;

  /* ============================================================
   * TX_SENDING — Wait for NRF TX completion or timeout
   * ============================================================ */
  case OUT_LINK_TX_SENDING:
    /* Poll NRF status for missed IRQ during TX */
    if (!outLink.tx_done) {
      uint8_t st = NRF24_GetStatus(&nrf);
      if (st & (NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT)) {
        outLink.irq_flag = 1;
      }
    }

    /* TX finished (ACK or MAX_RT) */
    if (outLink.tx_done) {
      outLink.tx_done = 0;
      outLink.tx_in_progress = 0;
#if USE_UART_LOGGING
      UartLog(outLink.tx_ok ? "TX: OK\r\n" : "TX: FAIL (no ACK)\r\n");
#endif
#if USE_LED_INDICATOR
      Outdoor_LedOff();
#endif
      NRF_StartReceive();
      outLink.state = OUT_LINK_IDLE;
      break;
    }

    /* TX timeout — hardware did not respond */
    if (outLink.tx_in_progress &&
        (HAL_GetTick() - outLink.tx_start_tick) > NRF_TX_TIMEOUT_MS) {
#if USE_UART_LOGGING
      UartLog("TX: Timeout\r\n");
#endif
      outLink.state = OUT_LINK_RECOVERY;
    }
    break;

  /* ============================================================
   * RECOVERY — Reset NRF and return to IDLE
   * ============================================================ */
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

    NRF_StartReceive();
    outLink.state = OUT_LINK_IDLE;
#if USE_UART_LOGGING
    UartLog("Recovery complete\r\n");
#endif
    break;

  default:
    /* Unknown state — enter recovery */
    outLink.state = OUT_LINK_RECOVERY;
    break;
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
