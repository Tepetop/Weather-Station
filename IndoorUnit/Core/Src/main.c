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

#include <complex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <encoder.h>
#include <button_debounce.h>

#include <demo_tests.h>

#include "ds3231_clod.h"
#include "NRF24L01.h"
#include "weather_station.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEFAULT_DEMO 0  // Set to 1 to enable drawing demo instead of menu
#define RTC_DEMO 1
#define WEATHER_STATION 1  // Weather station mode - communication with OutdoorUnit

#define IRQ_FLAG_SET 1
#define IRQ_FLAG_CLEAR 0

/* NRF24L01 configuration */
#define NRF_CHANNEL      76      // 2476 MHz
#define NRF_PAYLOAD_SIZE 20     // Measurement data payload size
#define NRF_CMD_SIZE     8      // Command payload size
#define CMD_MEASURE      0x01   // Command to request measurement
#define NRF_TX_IRQ_TIMEOUT_MS 80U
#define WS_NODE_COUNT 1U

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

#if RTC_DEMO
DS3231_Handle rtc2;
DS3231_DateTime currentDateTime = {
  .seconds = 0,
  .minutes = 15,
  .hours   = 12,
  .ampm    = DS3231_AM,
  .format  = DS3231_FORMAT_24H,
  .day     = 7,
  .date    = 22,
  .month   = 2,
  .year    = 26,
  .century = false
};
DS3231_DateTime rtcNow;

uint8_t alaram1_cout = 0;
uint8_t alarm2_count = 0;
#endif

char buffer[64];
uint8_t counter = 1;
uint32_t softTimer = 0;

#if WEATHER_STATION
NRF24_Handle_t nrf;
WS_Manager_t wsCtx = {0};

/* NRF addresses */
static const uint8_t WS_NODE_TX_ADDRS[WS_MAX_NODES][5] = {
  {0xE7, 0xE7, 0xE7, 0xE7, 0xE7},
  {0xE8, 0xE8, 0xE8, 0xE8, 0xE8},
  {0xE9, 0xE9, 0xE9, 0xE9, 0xE9},
  {0xEA, 0xEA, 0xEA, 0xEA, 0xEA}
};
static const uint8_t WS_NODE_RX_ADDRS[WS_MAX_NODES][5] = {
  {0xC2, 0xC2, 0xC2, 0xC2, 0xC2},
  {0xC3, 0xC3, 0xC3, 0xC3, 0xC3},
  {0xC4, 0xC4, 0xC4, 0xC4, 0xC4},
  {0xC5, 0xC5, 0xC5, 0xC5, 0xC5}
};
#endif

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

void EncoderButtonFlag(void);
#if WEATHER_STATION
static void NRF_DelayUs(uint32_t us);
static void WS_LedOn(void);
static void WS_LedOff(void);
static void WS_ApplyActiveNodeAddress(void);
static void WS_SendMeasureCommand(void);
static void WS_StartReceive(void);
static void WS_HandleIRQ(void);
static void WS_DisplayMeasurements(void);
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#if WEATHER_STATION

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

static void WS_LedOn(void) {
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_RESET);
}

static void WS_LedOff(void) {
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_SET);
}

static void WS_ApplyActiveNodeAddress(void) {
  const WS_NodeState_t *node = WS_GetActiveNodeConst(&wsCtx);
  if (node == NULL) {
    return;
  }

  NRF24_SetTXAddress(&nrf, (uint8_t*)node->tx_addr, 5);
  NRF24_SetRXAddress(&nrf, 0, (uint8_t*)node->tx_addr, 5); // Pipe 0 = TX_ADDR for auto-ACK
  NRF24_SetRXAddress(&nrf, 1, (uint8_t*)node->rx_addr, 5); // Pipe 1 = data from selected node
}

/**
  * @brief Format float value to fixed-point text without using printf float support.
  */
static void WS_FormatFixed(char *dst, size_t dst_size, float value, uint8_t decimals) {
  int32_t scale = 1;
  for (uint8_t i = 0; i < decimals; i++) {
    scale *= 10;
  }

  float scaled_f = value * (float)scale;
  if (scaled_f >= 0.0f) {
    scaled_f += 0.5f;
  } else {
    scaled_f -= 0.5f;
  }

  int32_t scaled = (int32_t)scaled_f;
  int32_t abs_scaled = (scaled < 0) ? -scaled : scaled;
  int32_t int_part = abs_scaled / scale;
  int32_t frac_part = abs_scaled % scale;

  if (decimals == 0U) {
    snprintf(dst, dst_size, "%s%ld", (scaled < 0) ? "-" : "", (long)int_part);
  } else {
    snprintf(dst, dst_size, "%s%ld.%0*ld", (scaled < 0) ? "-" : "", (long)int_part, decimals, (long)frac_part);
  }
}
#endif
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
  /* USER CODE BEGIN 2 */
#if DEFAULT_DEMO
  /*            Initialize encoder        */
  Encoder_Init(&encoder, &htim1, TIM_CHANNEL_1, TIM_CHANNEL_2);   

  /*             Initialize debounce button        */
  ButtonInitKey(&encoderSW, ENC_BUTTON_GPIO_Port, ENC_BUTTON_Pin, 50, 1000, 500, BUTTON_MODE_INTERRUPT);
  ButtonRegisterPressCallback(&encoderSW, EncoderButtonFlag);
#endif

  /*            Initialize LCD           */
  PCD8544_Init(&LCD, &hspi1, LCD_DC_GPIO_Port, LCD_DC_Pin, LCD_CE_GPIO_Port, LCD_CE_Pin, LCD_RST_GPIO_Port, LCD_RST_Pin);
  PCD8544_ClearScreen(&LCD);
 

#if RTC_DEMO

  if (DS3231_ERROR == DS3231_clod_Init(&rtc2, &hi2c2, DS3231_I2C_ADDR, DS3231_FORMAT_24H)) {
    Error_Handler();
  }

  if (DS3231_ERROR == DS3231_clod_SetDateTime(&rtc2, &currentDateTime)) {
    Error_Handler();
  }

  /*            Initialize RTC demo        */
   DS3231_Alarm1 alm1 = {
    .seconds = 1,
    .minutes = 0,
    .hours = 0,
    .ampm = 0,  
    .format = 0, 
    .day_date = 0,
    .mode = DS3231_ALM1_EVERY_SECOND  
  };

  DS3231_clod_SetAlarm1(&rtc2, &alm1);
  DS3231_clod_EnableAlarm1Interrupt(&rtc2);

#endif

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

#if WEATHER_STATION
  /* ---------------------------------------------------------------
   * NRF24L01 Weather Station Initialization - IndoorUnit as Master
   * --------------------------------------------------------------- */

  /* 1. Initialize the nRF24L01 driver */
  if (NRF24_Init(&nrf, &hspi2,SPI2_CS_GPIO_Port, SPI2_CS_Pin, SPI2_CE_GPIO_Port, SPI2_CE_Pin, SPI2_IRQ_GPIO_Port, SPI2_IRQ_Pin, NRF_DelayUs) != HAL_OK) {
    PCD8544_SetFont(&LCD, &Font_6x8);
    PCD8544_SetCursor(&LCD, 0, 0);
    PCD8544_WriteString(&LCD, "NRF FAIL");
    PCD8544_UpdateScreen(&LCD);
    Error_Handler();
  }

  /* 2. Configure radio parameters - MUST match OutdoorUnit */
  NRF24_SetChannel(&nrf, NRF_CHANNEL);          // RF channel 76
  NRF24_SetDataRate(&nrf, NRF24_DR_1MBPS);      // 1 Mbps
  NRF24_SetPALevel(&nrf, NRF24_PA_MAX);         // 0 dBm
  NRF24_SetCRC(&nrf, NRF24_CRC_2B);             // 2-byte CRC
  NRF24_SetAddressWidth(&nrf, NRF24_AW_5);      // 5-byte address
  NRF24_SetAutoRetr(&nrf, 1, 10);               // ARD=500us, ARC=10 retries

  WS_InitManager(&wsCtx, WS_NODE_TX_ADDRS, WS_NODE_RX_ADDRS, WS_NODE_COUNT);
  WS_ApplyActiveNodeAddress();

  /* 4. Configure pipes */
  NRF24_SetAutoAck(&nrf, 0, 1);                 // Auto-ACK on pipe 0
  NRF24_SetAutoAck(&nrf, 1, 1);                 // Auto-ACK on pipe 1
  NRF24_EnablePipe(&nrf, 0, 1);                 // Enable pipe 0
  NRF24_EnablePipe(&nrf, 1, 1);                 // Enable pipe 1
  NRF24_SetPayloadSize(&nrf, 0, NRF_CMD_SIZE);  // Pipe 0 for commands
  NRF24_SetPayloadSize(&nrf, 1, NRF_PAYLOAD_SIZE); // Pipe 1 for measurement data

  /* 5. Start in RX mode, waiting for response data */
  NRF24_FlushRX(&nrf);
  WS_StartReceive();
  WS_LedOff();
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

#if WEATHER_STATION
  WS_NodeState_t *node = WS_GetActiveNode(&wsCtx);

  /* --- Weather Station: minimal fallback when waiting for NRF event --- */
  if ((wsCtx.nrf_irq_flag == 0U) && WS_ShouldFallbackToStatusRead(&wsCtx)) {
    uint8_t st = NRF24_GetStatus(&nrf);
    if (st & (NRF24_STATUS_RX_DR | NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT)) {
      WS_SetIrqFlag(&wsCtx);
    }
  }

  /* --- Weather Station: NRF24 IRQ handling --- */
  if (wsCtx.nrf_irq_flag) {
    WS_ClearIrqFlag(&wsCtx);
    WS_HandleIRQ();
  }

  /* --- Weather Station: TX completion handling (event-driven) --- */
  {
    WS_TxEvent_t tx_event = WS_ConsumeTxEvent(&wsCtx, HAL_GetTick());
    if (tx_event == WS_TX_EVENT_OK) {
      WS_StartReceive();
      PCD8544_SetCursor(&LCD, 0, 5);
      PCD8544_ClearBufferLine(&LCD, 5);
      PCD8544_WriteString(&LCD, "WAIT DATA");
      PCD8544_UpdateScreen(&LCD);
    } else if (tx_event == WS_TX_EVENT_FAIL) {
      WS_StartReceive();
      PCD8544_SetCursor(&LCD, 0, 5);
      PCD8544_ClearBufferLine(&LCD, 5);
      PCD8544_WriteString(&LCD, "TX FAIL  ");
      PCD8544_UpdateScreen(&LCD);
      WS_ScheduleNextNode(&wsCtx);
      WS_ApplyActiveNodeAddress();
    }
  }

  /* --- Weather Station: TX watchdog (missing IRQ protection) --- */
  if (WS_IsActiveTxTimedOut(&wsCtx, HAL_GetTick(), NRF_TX_IRQ_TIMEOUT_MS)) {
    uint8_t st = NRF24_GetStatus(&nrf);
    if (st & (NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT)) {
      WS_SetIrqFlag(&wsCtx);
      WS_HandleIRQ();
    }
  }

  if (WS_IsActiveTxTimedOut(&wsCtx, HAL_GetTick(), NRF_TX_IRQ_TIMEOUT_MS) && (node->tx_done == 0U)) {
    WS_HandleActiveTxTimeout(&wsCtx, node->last_status);
    WS_StartReceive();
    PCD8544_SetCursor(&LCD, 0, 5);
    PCD8544_ClearBufferLine(&LCD, 5);
    PCD8544_WriteString(&LCD, "TX IRQ TO");
    PCD8544_UpdateScreen(&LCD);
    WS_ScheduleNextNode(&wsCtx);
    WS_ApplyActiveNodeAddress();
  }
    
    /* --- Weather Station: Trigger measurement request --- */
    if (node->measurement_pending) {
        WS_ConsumePendingForActiveNode(&wsCtx);
        WS_SendMeasureCommand();
    }

    if (WS_IsActiveRxTimedOut(&wsCtx, HAL_GetTick(), 2000U)) {
      WS_HandleActiveRxTimeout(&wsCtx, node->last_status);
      PCD8544_SetCursor(&LCD, 0, 5);
      PCD8544_ClearBufferLine(&LCD, 5);
      PCD8544_WriteString(&LCD, "RX TIMEOUT");
      PCD8544_UpdateScreen(&LCD);
      WS_StartReceive();
      WS_ScheduleNextNode(&wsCtx);
      WS_ApplyActiveNodeAddress();
    }
    
    /* --- Weather Station: Display received data --- */
    if (WS_ConsumeActiveDataReady(&wsCtx)) {
        WS_DisplayMeasurements();
        WS_ScheduleNextNode(&wsCtx);
        WS_ApplyActiveNodeAddress();
    }
#endif

#if RTC_DEMO
    if (rtc2.DS3231_IRQ_Flag)
    {
      DS3231_clod_CheckAndClearAlarmFlags(&rtc2);
      DS3231_clod_GetDateTime(&rtc2, &rtcNow);

      // Alarm 2 triggered handle if needed (not used in this demo)
      if(rtc2.DS3231_IRQ_Alarm & DS3231_IRQ_ALARM2) {
      }
      // Alarm 1 triggered 
      else if(rtc2.DS3231_IRQ_Alarm & DS3231_IRQ_ALARM1) {
        alaram1_cout++;
#if WEATHER_STATION
        if(alaram1_cout % 3 == 0) {
          alaram1_cout = 0;
          WS_RequestMeasurementForActiveNode(&wsCtx);
        }
#endif
      }
      rtc2.DS3231_IRQ_Flag = 0;
    }
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
  if (htim->Instance == TIM1) // Check if the interrupt is from TIM1
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
#if RTC_DEMO
  if (GPIO_Pin == GPIO_PIN_0)
  {
    rtc2.DS3231_IRQ_Flag = IRQ_FLAG_SET;
  }  
#endif

#if WEATHER_STATION
  /* NRF24L01 IRQ pin (active low) */
  if (GPIO_Pin == SPI2_IRQ_Pin)
  {
    WS_SetIrqFlag(&wsCtx);
  }
#endif
}

/*      Encoder button function to assign to callback     */
void EncoderButtonFlag(void)
{
  //encoder.ButtonIRQ_Flag = 1;
  encoder.ButtonIRQ_Flag = IRQ_FLAG_SET;
  Menu_SetEnterAction(&menuContext);
}

#if WEATHER_STATION
/* ---------------------------------------------------------------
 * Weather Station Functions - IndoorUnit (Master/Transmitter)
 * --------------------------------------------------------------- */

/**
 * @brief  Send measurement command to OutdoorUnit.
 */
static void WS_SendMeasureCommand(void) {
  WS_NodeState_t *node = WS_GetActiveNode(&wsCtx);
  uint8_t cmd[NRF_CMD_SIZE] = {CMD_MEASURE, 0, 0, 0, 0, 0, 0, 0};

  if ((node == NULL) || (node->tx_in_progress != 0U) || (node->awaiting_response != 0U)) {
    return;
  }

  WS_ApplyActiveNodeAddress();
  WS_LedOff();
  WS_ClearIrqFlag(&wsCtx);
  WS_StartTxForActiveNode(&wsCtx, HAL_GetTick());

  NRF24_SetMode(&nrf, NRF24_MODE_STANDBY);
  NRF24_FlushTX(&nrf);
  NRF24_ClearIRQ(&nrf, NRF24_STATUS_IRQ_MASK);
  NRF24_WritePayload(&nrf, cmd, NRF_CMD_SIZE);          /* Load command into TX FIFO */
  NRF24_SetMode(&nrf, NRF24_MODE_TX);                   /* Trigger transmission */

  PCD8544_SetCursor(&LCD, 0, 5);
  PCD8544_ClearBufferLine(&LCD, 5);
  PCD8544_WriteString(&LCD, "TX START");
  PCD8544_UpdateScreen(&LCD);
}

/**
 * @brief  Switch nRF24L01 to RX mode and start listening for response data.
 */
static void WS_StartReceive(void) {
  WS_ApplyActiveNodeAddress();
  NRF24_SetMode(&nrf, NRF24_MODE_STANDBY);
  NRF24_ClearIRQ(&nrf, NRF24_STATUS_IRQ_MASK);
  NRF24_SetMode(&nrf, NRF24_MODE_RX);
}

/**
 * @brief  Handle nRF24L01 IRQ: receive measurement data from OutdoorUnit.
 */
static void WS_HandleIRQ(void) {
  uint8_t status = NRF24_GetStatus(&nrf);
  WS_NodeState_t *node = WS_GetActiveNode(&wsCtx);
  if (node == NULL) {
    return;
  }
  node->last_status = status;
    
    if (status & NRF24_STATUS_RX_DR) {
        uint8_t pipe = (status >> 1) & 0x07;
        uint8_t rx_data[NRF24_MAX_PAYLOAD_SIZE] = {0};
        uint8_t payload_len = (pipe == 0U) ? NRF_CMD_SIZE : NRF_PAYLOAD_SIZE;

        /* Read payload from reported pipe. */
        NRF24_ReadPayload(&nrf, rx_data, payload_len);
        NRF24_ClearIRQ(&nrf, NRF24_STATUS_RX_DR);

        if ((pipe == 1U) && (payload_len >= sizeof(WS_MeasurementData_t))) {
          /* Expected path: Outdoor measurement data on pipe 1. */
          WS_MeasurementData_t measurement;
          memcpy(&measurement, rx_data, sizeof(WS_MeasurementData_t));
          WS_MarkActiveDataReceived(&wsCtx, &measurement, status);
          WS_LedOn();
        } else {
          /* Diagnostic: frame arrived on unexpected pipe/size. */
          PCD8544_SetCursor(&LCD, 0, 5);
          PCD8544_ClearBufferLine(&LCD, 5);
          sprintf(buffer, "RX P%u L%u", pipe, payload_len);
          PCD8544_WriteString(&LCD, buffer);
          PCD8544_UpdateScreen(&LCD);
        }
    }
    
    if (status & NRF24_STATUS_TX_DS) {
        NRF24_ClearIRQ(&nrf, NRF24_STATUS_TX_DS);
      WS_MarkTxResultFromIrq(&wsCtx, true, status);
    }
    
    if (status & NRF24_STATUS_MAX_RT) {
        NRF24_ClearIRQ(&nrf, NRF24_STATUS_MAX_RT);
        NRF24_FlushTX(&nrf);
      WS_MarkTxResultFromIrq(&wsCtx, false, status);
    }
}

/**
 * @brief  Display received measurement data on LCD.
 */
static void WS_DisplayMeasurements(void) {
  WS_NodeState_t *node = WS_GetActiveNode(&wsCtx);
  char value_text[16];

    PCD8544_ClearScreen(&LCD);
    PCD8544_SetFont(&LCD, &Font_6x8);
    
    /* Line 0: Time */
    PCD8544_SetCursor(&LCD, 0, 0);
    sprintf(buffer, "%02d:%02d:%02d", rtcNow.hours, rtcNow.minutes, rtcNow.seconds);
    PCD8544_WriteString(&LCD, buffer);
    
    /* Line 1: Temperature (Si7021) */
    PCD8544_SetCursor(&LCD, 0, 1);
    WS_FormatFixed(value_text, sizeof(value_text), node->data.si7021_temp, 2);
    snprintf(buffer, sizeof(buffer), "T:%sC", value_text);
    PCD8544_WriteString(&LCD, buffer);
    
    /* Line 2: Humidity */
    PCD8544_SetCursor(&LCD, 0, 2);
    WS_FormatFixed(value_text, sizeof(value_text), node->data.si7021_hum, 2);
    snprintf(buffer, sizeof(buffer), "H:%s%%", value_text);
    PCD8544_WriteString(&LCD, buffer);
    
    /* Line 3: Pressure */
    PCD8544_SetCursor(&LCD, 0, 3);
    WS_FormatFixed(value_text, sizeof(value_text), node->data.bmp280_press, 2);
    snprintf(buffer, sizeof(buffer), "P:%shPa", value_text);
    PCD8544_WriteString(&LCD, buffer);
    
    /* Line 4: Light */
    PCD8544_SetCursor(&LCD, 0, 4);
    WS_FormatFixed(value_text, sizeof(value_text), node->data.tsl2561_lux, 2);
    snprintf(buffer, sizeof(buffer), "L:%slux", value_text);
    PCD8544_WriteString(&LCD, buffer);
    
    /* Line 5: Status */
    PCD8544_SetCursor(&LCD, 0, 5);
    PCD8544_WriteString(&LCD, "OK");
    
    PCD8544_UpdateScreen(&LCD);
}
#endif /* WEATHER_STATION */
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
