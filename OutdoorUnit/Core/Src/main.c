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
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "si7021.h"
#include "TSL2561.h"
#include "bmp280.h"
#include "measurement.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define GROUP 0
#define TSL 0
#define SI7021 0
#define BMP 0
#define BMP_DMA 1  // Set to 1 to test DMA mode
#define CHECK 0
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
char Message[128]; // Message to transfer by UART
uint8_t Length; // Message length

#if TSL
TSL2561_t tsl;
#endif

#if SI7021
Si7021_t sio;
#endif

#if BMP || BMP_DMA
BMP280_t bmp;
#endif

#if BMP_DMA
static uint8_t bmp_dma_buffer[6];
static volatile bool bmp_dma_complete = false;
static volatile bool bmp_dma_in_progress = false;
#endif

#if GROUP
TSL2561_t tsl;
Si7021_t sio;
BMP280_t bmp;
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

void UartLog(char *msg);
HAL_StatusTypeDef I2C_CheckAddress(I2C_HandleTypeDef *i2c);

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

#if GROUP
  Measurement_Init(&hi2c2);
  Measurement_Process();
#endif

#if TSL
  TSL2561_Init(&tsl,&hi2c2, (uint8_t)0x39, TSL2561_INTEG_402MS, TSL2561_GAIN_1X);
#endif

#if SI7021
  Si7021_Init(&sio, &hi2c2, 0x40, SI7021_RESOLUTION_RH11_TEMP11);
#endif

#if BMP
  BMP280_Init(&bmp, &hi2c2, 0x76);
  BMP280_SetCtrlMeas(&bmp, BMP280_OVERSAMPLING_X16, BMP280_MODE_NORMAL);
  BMP280_SetConfig(&bmp, BMP280_STANDBY_500_MS, BMP280_FILTER_16);
#endif

#if BMP_DMA
  BMP280_Init(&bmp, &hi2c2, 0x76);
  BMP280_SetCtrlMeas(&bmp, BMP280_OVERSAMPLING_X16, BMP280_MODE_NORMAL);
  BMP280_SetConfig(&bmp, BMP280_STANDBY_500_MS, BMP280_FILTER_16);
#endif

#if CHECK
	I2C_CheckAddress(&hi2c2);
#endif

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if SI7021
	 Si7021_ReadHumidityAndTemperature(&sio);
	 sprintf(Message,"Si7021 Temp = %.2f, Si7021 Hum = %.2f\n\r",sio.data.temperature, sio.data.humidity);
	 UartLog(Message);
#endif

#if TSL
	 TSL2561_CalculateLux(&tsl);
	 sprintf(Message,"Lux from TSL = %.2f\n\r", tsl.data.lux);
	 UartLog(Message);
#endif

#if BMP
	 BMP280_GetTemperature(&bmp);
	 BMP280_GetPressure(&bmp);
	 sprintf(Message,"BMP Temp = %.2f, BMP Presure = %.2f\n\r", bmp.data.temperature, bmp.data.pressure);
	 UartLog(Message);
#endif

#if BMP_DMA
	 // DMA workflow: Check completion first, then start new transfer
	 if (bmp_dma_complete)
	 {
		 // DMA complete - parse and compensate
		 bmp_dma_complete = false;
		 bmp_dma_in_progress = false;
		 BMP280_ParseRawTemperaturePressure(&bmp, bmp_dma_buffer);
		 BMP280_CompensateTemperatureAndPressure(&bmp);
		 sprintf(Message,"BMP DMA Temp = %.2f, Pressure = %.2f\n\r", bmp.data.temperature, bmp.data.pressure);
		 UartLog(Message);
	 }
	 else if (!bmp_dma_in_progress)
	 {
		 // Start DMA read of both pressure and temperature (6 bytes)
		 if (HAL_OK == BMP280_ReadRawTemperaturePressure(&bmp, bmp_dma_buffer, sizeof(bmp_dma_buffer), BMP280_IO_DMA))
		 {
			 bmp_dma_in_progress = true;
		 }
	 }
#endif

#if GROUP
   Measurement_Start();
   Measurement_Process();
   Measurement_GetCSV(Message, sizeof(Message));
   UartLog(Message);
#endif
   HAL_GPIO_TogglePin(USER_LED_GPIO_Port, USER_LED_Pin);
	 HAL_Delay(350);
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

/**
  * @brief  Send a message via UART
  * @param  msg: Message to send
  * @retval None
  */
void UartLog(char *msg)
{
  uint16_t len = strlen(msg);
  HAL_UART_Transmit(&huart1, (uint8_t *)msg, len, HAL_MAX_DELAY);
}

/**
  * @brief  Check for I2C devices on the bus
  * @param  i2c: I2C handle
  * @retval HAL_StatusTypeDef
  */
HAL_StatusTypeDef I2C_CheckAddress(I2C_HandleTypeDef *i2c)
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

#if BMP_DMA
/**
  * @brief  I2C Memory Read DMA completion callback
  * @param  hi2c: I2C handle
  * @retval None
  */
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c->Instance == hi2c2.Instance)
  {
    bmp_dma_complete = true;
  }
}
#endif

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
