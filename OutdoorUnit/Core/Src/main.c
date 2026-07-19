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
#include "iwdg.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "outdoor_station.h"
#include "debug_log.h"
#include "measurement.h"
#include "measurement_unit_config.h"

#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */


/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */


/* NRF24L01 configuration */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint32_t meastimer = 0U; /* Timestamp of last measurement command */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void TestBmeMeasurements(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#if USE_UART_LOGGING
static void TestBme_LogMetric(const char *name, float value, const char *unit)
{
  int32_t centi = (int32_t)(value * 100.0f + ((value >= 0.0f) ? 0.5f : -0.5f));
  int32_t whole = centi / 100;
  int32_t frac = centi % 100;

  if (frac < 0)
  {
    frac = -frac;
  }

  char line[80];
  int len = snprintf(line, sizeof(line), "TEST:BME:%s=%ld.%02ld %s\r\n",
                     name, (long)whole, (long)frac, unit);
  if (len > 0 && len < (int)sizeof(line))
  {
    HAL_UART_Transmit(&huart1, (uint8_t *)line, (uint16_t)len, 100);
  }
}
#else
static void TestBme_LogMetric(const char *name, float value, const char *unit)
{
  (void)name;
  (void)value;
  (void)unit;
}
#endif

static void TestBmeMeasurements(void)
{
  Measurement_Data_t data;

  Debug_Log("TEST:BME:start");

  if (OutdoorStation_RunMeasurementCycle(&data, OUTDOOR_MEAS_TIMEOUT_MS) != HAL_OK)
  {
    Debug_Log("TEST:BME:ERROR=cycle");
    return;
  }

#ifdef BME280_H
  if (data.sensorStatus & ERROR_BME280)
  {
    Debug_Log("TEST:BME:ERROR=sensor");
    return;
  }

  TestBme_LogMetric("temp", data.bme280_temp, "degC");
  TestBme_LogMetric("press", data.bme280_press, "hPa");
  TestBme_LogMetric("hum", data.bme280_hum, "%");
#else
  Debug_Log("TEST:BME:ERROR=not_configured");
#endif

  Debug_Log("TEST:BME:done");
}

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

#if defined(DEBUG)
  /* Keep IWDG stopped while CPU is halted by debugger (breakpoints/step). */
  __HAL_DBGMCU_FREEZE_IWDG();
#endif

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
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */

  Debug_Init();

  if (OutdoorStation_Init() != HAL_OK)
  {
    Error_Handler_WithName("OutdoorStation_Init");
  }
  meastimer = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    OutdoorStation_Process();
    HAL_IWDG_Refresh(&hiwdg);
    if(HAL_GetTick() - meastimer >= 10000u)
    {
      meastimer = HAL_GetTick();
      TestBmeMeasurements();
    }

#ifdef DEBUG_LOG_HEARTBEAT
    Debug_Heartbeat();
#endif

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
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

void Error_Handler_WithName(const char *function_name)
{
#if USE_UART_LOGGING
  char error_msg[80];
  snprintf(error_msg, sizeof(error_msg), "ERROR: Failure in function '%s'\r\n", function_name);
  HAL_UART_Transmit(&huart1, (uint8_t *)error_msg, strlen(error_msg), HAL_MAX_DELAY);
#else
  (void)function_name; /* Suppress unused parameter warning */
#endif
  __disable_irq();
  while (1) {
    /* Stay halted - watchdog or external reset required */
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
  Error_Handler_WithName("Unknown");
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
