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
#define DEFAULT_DEMO 1  // Set to 1 to enable drawing demo instead of menu
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


// Chart data structures for measurement graphs
PCD8544_ChartData_t temperatureChart;
PCD8544_ChartData_t humidityChart;
PCD8544_ChartData_t pressureChart;

// Simulated measurement values (shared between functions)
static int16_t g_tempDeciC = 253;   // 25.3C
static uint8_t g_humidity = 57;     // 57%
static uint16_t g_pressure = 1013;  // 1013 hPa
static uint8_t g_hour = 8;
static uint8_t g_minute = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void EncoderButtonFlag(void);

#if DEFAULT_DEMO
void demo_measurement_function(void);
void demo_chart_function(void);
void chart_temperature_function(void);
void chart_humidity_function(void);
void chart_pressure_function(void);
void chart_view_task(void);
void simulate_measurements(void);
static void init_all_charts(void);
#endif

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

#if DEFAULT_DEMO
  /* Initialize menu system with predefined configuration */
  Menu_Init(&StronaDomyslna, &menuContext); 
  // Set font for menu display
  PCD8544_SetFont(&LCD, &Font_6x8);  
  // Display initial menu and then show default measurement screen
  Menu_RefreshDisplay(&LCD, &menuContext);
  // Chart demo mode - continuously update the chart
  demo_measurement_function();
  demo_chart_function();
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

    
#if DRAWING_DEMO
    PCD8544_DrawRectangle(&LCD, 10, 10, 10, 12);
    PCD8544_UpdateScreen(&LCD);  
#endif
  
#if DEFAULT_DEMO

  ButtonTask(&encoderSW); 

  /* Handle chart view mode */
  if (menuContext.state.InChartView)
  {
    chart_view_task();
  }
  else
  {
    /*Call the menu task to handle any pending button actions*/
    Menu_Task(&LCD, &menuContext);
    
    /* Call user-defined encoder task*/
    Encoder_Task(&encoder, &menuContext);

    if (menuContext.state.InDefaultMeasurementsView)
    {
      demo_measurement_function();
    }
  }
  
#endif
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
  if (htim->Instance == TIM1) // Check if the interrupt is from TIM1
  {
    encoder.IRQ_Flag = 1;
  }
}

/*      Encoder button IRQ handler      */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
  ButtonIRQHandler(&encoderSW, GPIO_Pin);
}

/*      Encoder button flag handler      */
void EncoderButtonFlag(void)
{
  //encoder.ButtonIRQ_Flag = 1;
  Menu_SetEnterAction(&menuContext);
}

/* Simulation of measurements display */
void demo_measurement_function(void){
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

/**
 * @brief   Demo function to display temperature chart
 * @details Uses simulated temperature data to demonstrate chart drawing.
 *          Temperature varies between 21.4°C and 29.9°C over time.
 *          Press encoder button to toggle between LINE and BAR chart.
 */
void demo_chart_function(void){
  static uint32_t lastUpdate = 0;
  static uint8_t initialized = 0;

  // Simulated temperature (in 0.1°C units, e.g., 253 = 25.3°C)
  static int16_t tempDeciC = 253;
  static int8_t tempStep = 3;

  // Simulated time
  static uint8_t hour = 8;
  static uint8_t minute = 0;
  
  uint32_t now = HAL_GetTick();

  // Initialize chart data on first run
  if (!initialized) {
    PCD8544_InitChartData(&temperatureChart);
    temperatureChart.decimalPlaces = 1;  // Values are in 0.1°C units
    temperatureChart.chartType = PCD8544_CHART_DOT_LINE;  // Start with line chart
    
    // Pre-populate with some initial data points
    PCD8544_AddChartPoint(&temperatureChart, 240, 8, 0);   // 24.0°C at 08:00
    PCD8544_AddChartPoint(&temperatureChart, 245, 8, 5);   // 24.5°C at 08:05
    PCD8544_AddChartPoint(&temperatureChart, 252, 8, 10);  // 25.2°C at 08:10
    PCD8544_AddChartPoint(&temperatureChart, 258, 8, 15);  // 25.8°C at 08:15
    PCD8544_AddChartPoint(&temperatureChart, 263, 8, 20);  // 26.3°C at 08:20
    PCD8544_AddChartPoint(&temperatureChart, 268, 8, 25);  // 26.8°C at 08:25
    PCD8544_AddChartPoint(&temperatureChart, 270, 8, 30);  // 27.0°C at 08:30
    PCD8544_AddChartPoint(&temperatureChart, 267, 8, 35);  // 26.7°C at 08:35
    tempDeciC = 267;
    hour = 8;
    minute = 40;
    
    initialized = 1;
    lastUpdate = now;
    
    // Draw initial chart
    PCD8544_ClearBuffer(&LCD);
    PCD8544_DrawChart(&LCD, &temperatureChart);
    PCD8544_UpdateScreen(&LCD);
    return;
  }

  // Update every 1 second (simulated data)
  if ((now - lastUpdate) < 1000) {
    return;
  }
  lastUpdate = now;

  // Update simulated temperature
  tempDeciC += tempStep;
  if (tempDeciC >= 299 || tempDeciC <= 214) {
    tempStep = -tempStep;
    tempDeciC += tempStep;
  }

  // Update simulated time
  minute += 5;  // Each update represents 5 minutes
  if (minute >= 60) {
    minute = 0;
    hour++;
    if (hour >= 24) {
      hour = 0;
    }
  }

  // Add new data point
  PCD8544_AddChartPoint(&temperatureChart, tempDeciC, hour, minute);

  // Redraw chart
  PCD8544_ClearBuffer(&LCD);
  PCD8544_DrawChart(&LCD, &temperatureChart);
  PCD8544_UpdateScreen(&LCD);
}

/**
 * @brief   Simulate measurement data updates
 * @details Called periodically to update simulated values for temp, humidity, pressure
 */
void simulate_measurements(void){
  static uint32_t lastUpdate = 0;
  static int8_t tempStep = 3;
  static int8_t humStep = 1;
  static int8_t pressStep = 2;
  
  uint32_t now = HAL_GetTick();
  
  // Update every 1 second
  if ((now - lastUpdate) < 1000) {
    return;
  }
  lastUpdate = now;
  
  // Update temperature
  g_tempDeciC += tempStep;
  if (g_tempDeciC >= 299 || g_tempDeciC <= 214) {
    tempStep = -tempStep;
    g_tempDeciC += tempStep;
  }
  
  // Update humidity
  g_humidity += humStep;
  if (g_humidity >= 75 || g_humidity <= 40) {
    humStep = -humStep;
    g_humidity += humStep;
  }
  
  // Update pressure
  g_pressure += pressStep;
  if (g_pressure >= 1030 || g_pressure <= 1000) {
    pressStep = -pressStep;
    g_pressure += pressStep;
  }
  
  // Update time
  g_minute += 5;
  if (g_minute >= 60) {
    g_minute = 0;
    g_hour++;
    if (g_hour >= 24) {
      g_hour = 0;
    }
  }
  
  // Add points to all charts
  PCD8544_AddChartPoint(&temperatureChart, g_tempDeciC, g_hour, g_minute);
  PCD8544_AddChartPoint(&humidityChart, (int16_t)g_humidity * 10, g_hour, g_minute);  // Store as 0.1% units
  PCD8544_AddChartPoint(&pressureChart, (int16_t)(g_pressure - 900), g_hour, g_minute);  // Store as offset from 900 hPa
}

/**
 * @brief   Initialize chart data for all measurement types
 */
static void init_all_charts(void){
  // Initialize temperature chart
  PCD8544_InitChartData(&temperatureChart);
  temperatureChart.decimalPlaces = 1;
  temperatureChart.chartType = PCD8544_CHART_DOT;
  
  // Initialize humidity chart  
  PCD8544_InitChartData(&humidityChart);
  humidityChart.decimalPlaces = 1;
  humidityChart.chartType = PCD8544_CHART_DOT_LINE;
  
  // Initialize pressure chart
  PCD8544_InitChartData(&pressureChart);
  pressureChart.decimalPlaces = 0;
  pressureChart.chartType = PCD8544_CHART_BAR;
  
  // Pre-populate with some initial data
  PCD8544_AddChartPoint(&temperatureChart, 240, 8, 0);
  PCD8544_AddChartPoint(&temperatureChart, 245, 8, 5);
  PCD8544_AddChartPoint(&temperatureChart, 252, 8, 10);
  PCD8544_AddChartPoint(&temperatureChart, 258, 8, 15);
  PCD8544_AddChartPoint(&temperatureChart, 263, 8, 20);
  
  PCD8544_AddChartPoint(&humidityChart, 550, 8, 0);
  PCD8544_AddChartPoint(&humidityChart, 560, 8, 5);
  PCD8544_AddChartPoint(&humidityChart, 580, 8, 10);
  PCD8544_AddChartPoint(&humidityChart, 570, 8, 15);
  PCD8544_AddChartPoint(&humidityChart, 540, 8, 20);
  
  PCD8544_AddChartPoint(&pressureChart, 110, 8, 0);
  PCD8544_AddChartPoint(&pressureChart, 112, 8, 5);
  PCD8544_AddChartPoint(&pressureChart, 115, 8, 10);
  PCD8544_AddChartPoint(&pressureChart, 113, 8, 15);
  PCD8544_AddChartPoint(&pressureChart, 118, 8, 20);
}

/**
 * @brief   Chart display function for temperature - called from menu
 */
void chart_temperature_function(void){
  static uint8_t chartsInitialized = 0;
  
  if (!chartsInitialized) {
    init_all_charts();
    chartsInitialized = 1;
  }
  
  menuContext.state.InChartView = 1;
  menuContext.state.ChartViewType = CHART_VIEW_TEMPERATURE;
  
  // Draw initial chart
  PCD8544_ClearBuffer(&LCD);
  PCD8544_DrawChart(&LCD, &temperatureChart);
  PCD8544_UpdateScreen(&LCD);
}

/**
 * @brief   Chart display function for humidity - called from menu
 */
void chart_humidity_function(void){
  static uint8_t chartsInitialized = 0;
  
  if (!chartsInitialized) {
    init_all_charts();
    chartsInitialized = 1;
  }
  
  menuContext.state.InChartView = 1;
  menuContext.state.ChartViewType = CHART_VIEW_HUMIDITY;
  
  // Draw initial chart
  PCD8544_ClearBuffer(&LCD);
  PCD8544_DrawChart(&LCD, &humidityChart);
  PCD8544_UpdateScreen(&LCD);
}

/**
 * @brief   Chart display function for pressure - called from menu
 */
void chart_pressure_function(void){

  static uint8_t chartsInitialized = 0;
  
  if (!chartsInitialized) {
    init_all_charts();
    chartsInitialized = 1;
  }
  
  menuContext.state.InChartView = 1;
  menuContext.state.ChartViewType = CHART_VIEW_PRESSURE;
  
  // Draw initial chart
  PCD8544_ClearBuffer(&LCD);
  PCD8544_DrawChart(&LCD, &pressureChart);
  PCD8544_UpdateScreen(&LCD);
}

/**
 * @brief   Chart view task - handles updating and exiting chart view
 * @details Called in main loop when InChartView is active
 */
void chart_view_task(void){
  static uint32_t lastRedraw = 0;
  uint32_t now = HAL_GetTick();
  
  // Check for button press to exit chart view
  if (encoderSW.InterruptFlag) {        
    // Exit chart view and return to menu
    menuContext.state.InChartView = 0;
    menuContext.state.ChartViewType = CHART_VIEW_NONE;
    
    // Just refresh the current menu display (Wykresy submenu showing tempWykres, wilgWykres, cisnWykres)
    // We don't call Menu_Escape because we didn't actually enter a submenu when viewing charts
    Menu_RefreshDisplay(&LCD, &menuContext);
    return;
  }
  
  // Simulate measurements
  simulate_measurements();
  
  // Redraw chart every 500ms
  if ((now - lastRedraw) >= PCD8544_REFRESH_RATE_MS) {
    lastRedraw = now;
    
    PCD8544_ClearBuffer(&LCD);
    
    switch (menuContext.state.ChartViewType) {
      case CHART_VIEW_TEMPERATURE:
        PCD8544_DrawChart(&LCD, &temperatureChart);
        break;
      case CHART_VIEW_HUMIDITY:
        PCD8544_DrawChart(&LCD, &humidityChart);
        break;
      case CHART_VIEW_PRESSURE:
        PCD8544_DrawChart(&LCD, &pressureChart);
        break;
      default:
        break;
    }
    
    PCD8544_UpdateScreen(&LCD);
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
