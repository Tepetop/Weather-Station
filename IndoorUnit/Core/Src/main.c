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
#include "stm32f1xx_hal.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <PCD_LCD/PCD8544.h>
#include <PCD_LCD/PCD8544_fonts.h>
#include <PCD_LCD/PCD8544_Menu.h>
#include <PCD_LCD/PCD8544_Menu_config.h>
#include <PCD_LCD/PCD8544_Drawing.h>

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/_types.h>

#include "weather_station_config.h"
#include "debug_log.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

void EncoderButtonPress(void);
static void NRF_DelayUs(uint32_t us);
void RTC_alarm1(void);
void RTC_alarm2(void);
static bool RTC_IsManualSetRequestedAtBoot(void);
void Menu_EscapeWraper (void);

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
  MX_SPI2_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  bool rtcManualSetRequested = RTC_IsManualSetRequestedAtBoot();

  /*            Initialize encoder        */
  Encoder_Init(&encoder, &htim1, TIM_CHANNEL_1, TIM_CHANNEL_2);   

  /*             Initialize debounce button        */
  ButtonInitKey(&encoderSW, ENC_BUTTON_GPIO_Port, ENC_BUTTON_Pin, 50, 1000, 500, BUTTON_MODE_INTERRUPT);
  ButtonRegisterPressCallback(&encoderSW, EncoderButtonPress);

  /*            Initialize LCD           */
  if(PCD8544_Init(&LCD, &hspi1, LCD_DC_GPIO_Port, LCD_DC_Pin, LCD_CE_GPIO_Port, LCD_CE_Pin, LCD_RST_GPIO_Port, LCD_RST_Pin, LCD_BLK_GPIO_Port, LCD_BLK_Pin) != PCD_OK) {
    Error_Handler();
  }
  else {
  {
    PCD8544_ClearScreen(&LCD);
  }
  }
 
  /*            Initialize RTC        */
  if (DS3231_Init(&rtc, &hi2c2, RTC_SQW_GPIO_Port, RTC_SQW_Pin, DS3231_I2C_ADDR, DS3231_FORMAT_24H) != DS3231_OK) {
    Error_Handler();
  }

  if (DS3231_GetOscillatorStopFlag(&rtc) != DS3231_OK) {
    Error_Handler();
  }

  if (rtcManualSetRequested || rtc.oscilator_stopped) 
  {
    /* Set time once on manual request or after oscillator stop (OSF=1). */
    if (DS3231_SetDateTime(&rtc, &currentDateTime) != DS3231_OK) {
      Error_Handler();
    }

    if (DS3231_ClearOscillatorStopFlag(&rtc) != DS3231_OK) {
      Error_Handler();
    }

    rtcNow = currentDateTime;
  } else {
    if (DS3231_GetDateTime(&rtc, &rtcNow) != DS3231_OK) {
      Error_Handler();
    }
  }

  DS3231_SetAlarm1(&rtc, &RTCalarm1);
  DS3231_SetAlarm2(&rtc, &RTCalarm2);
  DS3231_EnableAlarm1Interrupt(&rtc);
  DS3231_EnableAlarm2Interrupt(&rtc);


  /* Initialize menu system with predefined configuration */
  Menu_Init(&StronaDomyslna, &menuContext); 
  // Set font for menu display
  PCD8544_SetFont(&LCD, &Font_6x8);  
  // Display initial menu and then show default measurement screen
  Menu_RefreshDisplay(&LCD, &menuContext);
  
  /* Initialize charts */
  WS_UI_InitCharts();

  /* ---------------------------------------------------------------
   * NRF24L01 Weather Station Initialization
   * --------------------------------------------------------------- */

  /* 1. Initialize nRF24L01 driver handle. */
  if (NRF24_Init(&nrf, &hspi2,SPI2_CS_GPIO_Port, SPI2_CS_Pin, SPI2_CE_GPIO_Port, SPI2_CE_Pin, SPI2_IRQ_GPIO_Port, SPI2_IRQ_Pin, NRF_DelayUs) != HAL_OK) 
  {
    PCD8544_SetFont(&LCD, &Font_6x8);
    PCD8544_SetCursor(&LCD, 0, 0);
    PCD8544_WriteString(&LCD, "NRF FAIL");
    PCD8544_UpdateScreen(&LCD);
    Error_Handler();
  }

  WS_InitManager(&wsCtx, WS_NODE_TX_ADDRS, WS_NODE_RX_ADDRS, WS_NODE_COUNT);


  /*  Initialize Weather Station Runtime struct   */  
  wsRuntime.nrf = &nrf;
  wsRuntime.lcd = &LCD;
  wsRuntime.rtc_now = &rtcNow;
  wsRuntime.text_buffer = g_nrf_message;
  wsRuntime.text_buffer_size = sizeof(g_nrf_message);
  wsRuntime.led_port = USER_LED_GPIO_Port;
  wsRuntime.led_pin = USER_LED_Pin;
  wsRuntime.channel = NRF_CHANNEL;
  wsRuntime.cmd_measure = CMD_MEASURE;
  wsRuntime.cmd_size = NRF_CMD_SIZE;
  wsRuntime.payload_size = NRF_PAYLOAD_SIZE;
  wsRuntime.tx_irq_timeout_ms = NRF_TX_IRQ_TIMEOUT_MS;
  wsRuntime.rx_timeout_ms = 2000U;

  /*  If NRF24L01 initialization fails, display error on LCD , go to error handler*/
  if (WS_InitRadioAndStart(&wsCtx, &wsRuntime) != HAL_OK) {
    PCD8544_SetFont(&LCD, &Font_6x8);
    PCD8544_SetCursor(&LCD, 0, 0);
    PCD8544_WriteString(&LCD, "WS INIT ERR");
    PCD8544_UpdateScreen(&LCD);
    Error_Handler();
  }

  /* Initialize UI context for weather station display functions */
  WS_UI_Init(&WS_UI, &wsCtx, &wsRuntime, &LCD, &menuContext, &encoder, &rtcNow, g_nrf_message, sizeof(g_nrf_message));

  /* Initialize debug logging system */
  Debug_Init();

  /* Force initial measurement display render (show time + placeholders) */
  WS_UI.chart_data_dirty = 1U;

  /*  Initial measurement request */
  WS_RequestMeasurementForActiveNode(&wsCtx);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /*  Process with NRF24  */
    WS_ProcessEventHandler(&wsCtx, &wsRuntime, HAL_GetTick());

    /*    Process with RTC event     */
    DS3231_EventHandler(&rtc, &rtcNow, RTC_alarm1, RTC_alarm2);

    /*    Process with button event routine    */
    ButtonTask(&encoderSW);

    /* View state machine handles chart, status, measurement and menu views */
    WS_UI_ViewTask();

    /* Debug heartbeat - logs every minute to detect program hangs */
    Debug_Heartbeat();

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
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim){
  /*   Check if the interrupt is from TIM1*/
  if (htim->Instance == TIM1)
  {
    encoder.IRQ_Flag = IRQ_FLAG_SET;
  }
}

/*      Encoder button IRQ handler      */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  /*Encoder button IRQ handler*/
  ButtonIRQHandler(&encoderSW, GPIO_Pin);

  /* RTC SQW/INT pin IRQ handler */
  DS3231_IRQHandler(&rtc, GPIO_Pin);

  /* NRF24L01 IRQ pin (active low) */
  if (GPIO_Pin == SPI2_IRQ_Pin)
  {
    WS_SetIrqFlag(&wsCtx);
  }
}

/*                PRIVATE FUNCTIONS                             */

/**
 * @brief Microsecond delay using DWT cycle counter.
 */
static void NRF_DelayUs(uint32_t us) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  uint32_t cycles = (SystemCoreClock / 1000000U) * us;
  uint32_t start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < cycles);
}

/* RTC alarm function assign to callback */
void RTC_alarm1(void){
  WS_UI.chart_data_dirty = 1U;
  Debug_LogRtcAlarm1();
}

/* RTC alarm function assign to callback */
void RTC_alarm2(void){
  WS_RequestMeasurementForActiveNode(&wsCtx);
  Debug_LogRtcAlarm2();
}

/*      Encoder button function to assign to callback     */
void EncoderButtonPress(void)
{
  encoder.ButtonIRQ_Flag = IRQ_FLAG_SET;
  Menu_SetEnterAction(&menuContext);
}

void Menu_EscapeWraper(void)
{
    Menu_Escape(&LCD, &menuContext);
};

/* Hold encoder button during boot to force one-time manual RTC update. */
static bool RTC_IsManualSetRequestedAtBoot(void){
  uint32_t startTick = HAL_GetTick();

  if (HAL_GPIO_ReadPin(ENC_BUTTON_GPIO_Port, ENC_BUTTON_Pin) != GPIO_PIN_RESET) {
    return false;
  }

  while ((HAL_GetTick() - startTick) < RTC_MANUAL_SET_HOLD_MS) {
    if (HAL_GPIO_ReadPin(ENC_BUTTON_GPIO_Port, ENC_BUTTON_Pin) != GPIO_PIN_RESET) {
      return false;
    }
  }

  return true;
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
