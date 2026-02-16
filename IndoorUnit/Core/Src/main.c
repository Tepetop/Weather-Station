/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include <PCD_LCD/PCD8544.h>
#include <PCD_LCD/PCD8544_fonts.h>
#include <PCD_LCD/PCD8544_Menu.h>
#include <PCD_LCD/PCD8544_Menu_config.h>
#include <PCD_LCD/PCD8544_Drawing.h>

#include <stdint.h>
#include <stdio.h>

#include <encoder.h>
#include <button_debounce.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DRAWING_DEMO 0
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
Menu_Context_t menuContext;   // Menu context for managing menu state
PCD8544_t LCD;                // LCD instance
Encoder_t encoder;            // Encoder instance
Button_t encoderSW;          // Button instance for encoder switch
char buffer[64];
uint8_t counter = 1;
uint32_t softTimer = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void EncoderButtonFlag(void);

void demo_measurement_function(void);
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
  MX_SPI1_Init();
  MX_I2C2_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */

  /*            Initialize encoder        */
  Encoder_Init(&encoder, &htim1, TIM_CHANNEL_1, TIM_CHANNEL_2);   

  /*             Initialize debounce button        */
  ButtonInitKey(&encoderSW, ENC_BUTTON_GPIO_Port, ENC_BUTTON_Pin, 50, 1000, 500, BUTTON_MODE_INTERRUPT);
  ButtonRegisterPressCallback(&encoderSW, EncoderButtonFlag);

  /*            Initialize LCD           */
  PCD8544_Init(&LCD, &hspi1, LCD_DC_GPIO_Port, LCD_DC_Pin, LCD_CE_GPIO_Port, LCD_CE_Pin, LCD_RST_GPIO_Port, LCD_RST_Pin);
  PCD8544_ClearScreen(&LCD);

#if DRAWING_DEMO == 0
  /* Initialize menu system with predefined configuration */
  Menu_Init(&StronaDomyslna, &menuContext); 
  // Set font for menu display
  PCD8544_SetFont(&LCD, &Font_6x8);  
  // Display initial menu and then show default measurement screen
  Menu_RefreshDisplay(&LCD, &menuContext);
  demo_measurement_function();

#elif DRAWING_DEMO == 1
  //PCD8544_DrawCircle(&LCD, 42, 24, 12);
  PCD8544_FillCircle(&LCD, 42, 24, 20);
//  PCD8544_DrawLine(&LCD, 0, 0, 83, 47);
//  PCD8544_DrawLine(&LCD, 0, 47, 83, 0);
  PCD8544_UpdateScreen(&LCD);  
#endif


  /*  Soft timer for LED toggle */
  softTimer = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if(HAL_GetTick() - softTimer > 750)
    {
      HAL_GPIO_TogglePin(USER_LED_GPIO_Port, USER_LED_Pin);
      softTimer = HAL_GetTick();
    }  
  
#if DRAWING_DEMO == 0
    /* Process button state machine*/
    ButtonTask(&encoderSW); 

    /*Call the menu task to handle any pending button actions*/
    Menu_Task(&LCD, &menuContext);
    
    /* Call user-defined encoder task*/
    Encoder_Task(&encoder, &menuContext);

    if (menuContext.state.InDefaultMeasurementsView)
    {
      demo_measurement_function();
    }
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL8;
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

/*      Encoder timer handler     */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM1) // Check if the interrupt is from TIM1
  {
    encoder.IRQ_Flag = 1;
  }
}

/*      Encoder button IRQ handler      */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  ButtonIRQHandler(&encoderSW, GPIO_Pin);
}

/*      Encoder button flag handler      */
void EncoderButtonFlag(void)
{
  encoder.ButtonIRQ_Flag = 1;
}

/* Simulation of measurements display */
void demo_measurement_function(void)
{
  static uint32_t lastUpdate = 0;
  static uint8_t initialized = 0;

  static int16_t tempDeciC = 253;   // 25.3C
  static uint8_t humidity = 57;     // 57%
  static uint16_t pressure = 1013;  // 1013 hPa

  static int8_t tempStep = 1;
  static int8_t humStep = 1;
  static int8_t pressStep = 1;

  static uint8_t hour = 15;
  static uint8_t minute = 48;
  static uint8_t day = 1;
  static uint8_t month = 1;
  static uint8_t year = 25;

  uint32_t now = HAL_GetTick();
  uint8_t advanceValues = initialized;

  if (initialized && (now - lastUpdate) < 700)
  {
    return;
  }

  lastUpdate = now;
  initialized = 1;

  if (advanceValues)
  {
    tempDeciC += tempStep;
    if (tempDeciC >= 299 || tempDeciC <= 214)
    {
      tempStep = -tempStep;
      tempDeciC += tempStep;
    }

    humidity += humStep;
    if (humidity >= 70 || humidity <= 45)
    {
      humStep = -humStep;
      humidity += humStep;
    }

    pressure += pressStep;
    if (pressure >= 1025 || pressure <= 1002)
    {
      pressStep = -pressStep;
      pressure += pressStep;
    }

    minute++;
    if (minute >= 60)
    {
      minute = 0;
      hour++;
      if (hour >= 24)
      {
        hour = 0;
        day++;
        if (day > 30)
        {
          day = 1;
          month++;
          if (month > 12)
          {
            month = 1;
            year++;
          }
        }
      }
    }
  }

  // Display the simulated measurements
  PCD8544_ClearScreen(&LCD);
  PCD8544_SetCursor(&LCD, 0, 0);
  PCD8544_WriteString(&LCD, "DANE POMIAROWE");

  snprintf(buffer, sizeof(buffer), "TEMP: %2d.%1dC", tempDeciC / 10, tempDeciC % 10);
  PCD8544_SetCursor(&LCD, 0, 1);
  PCD8544_WriteString(&LCD, buffer);

  snprintf(buffer, sizeof(buffer), "WILG: %2u%%", humidity);
  PCD8544_SetCursor(&LCD, 0, 2);
  PCD8544_WriteString(&LCD, buffer);

  snprintf(buffer, sizeof(buffer), "CISN: %4uhPa", pressure);
  PCD8544_SetCursor(&LCD, 0, 3);
  PCD8544_WriteString(&LCD, buffer);

  snprintf(buffer, sizeof(buffer), "%02u:%02u %02u.%02u.%02u", hour, minute, day, month, year);
  PCD8544_SetCursor(&LCD, 0, 4);
  PCD8544_WriteString(&LCD, buffer);
  PCD8544_UpdateScreen(&LCD);
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
