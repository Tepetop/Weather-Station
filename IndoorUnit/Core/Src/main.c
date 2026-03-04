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

#include <encoder.h>
#include <button_debounce.h>

#include <demo_tests.h>

#include "ds3231_clod.h"
#include "NRF24L01.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEFAULT_DEMO 0  // Set to 1 to enable drawing demo instead of menu
#define DRAWING_DEMO 0
#define RTC_DEMO 1
#define NRF_DEMO 0    // Set to 1 to enable NRF24L01 TX/RX demo

#define IRQ_FLAG_SET 1
#define IRQ_FLAG_CLEAR 0

/* NRF24L01 demo pin assignments (adjust to your hardware) */

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
#endif

char buffer[64];
uint8_t counter = 1;
uint32_t softTimer = 0;

#if NRF_DEMO
NRF24_Handle_t nrf;
volatile uint8_t nrf_irq_flag = 0;   // Set in EXTI callback

/* NRF demo configuration */
static const uint8_t NRF_TX_ADDR[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
static const uint8_t NRF_RX_ADDR[5] = {0xC2, 0xC2, 0xC2, 0xC2, 0xC2};
#define NRF_CHANNEL      76      // 2476 MHz
#define NRF_PAYLOAD_SIZE 8
#endif

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void EncoderButtonFlag(void);

#if NRF_DEMO
static void NRF_DelayUs(uint32_t us);
static void NRF_Demo_Transmit(void);
static void NRF_Demo_StartReceive(void);
static void NRF_Demo_HandleIRQ(void);
#endif
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#if NRF_DEMO
/**
 * @brief Microsecond delay using DWT cycle counter.
 *        Requires DWT to be enabled (Cortex-M3 has it).
 */
static void NRF_DelayUs(uint32_t us) {
    /* Enable DWT if not already enabled */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t cycles = (SystemCoreClock / 1000000U) * us;
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles);
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
  DS3231_Alarm2 alm2 = {
    .mode = DS3231_ALM2_EVERY_MINUTE
  };

   DS3231_Alarm1 alm1 = {
    .mode = DS3231_ALM1_EVERY_SECOND
  };

  DS3231_clod_SetAlarm1(&rtc2, &alm1);
  DS3231_clod_EnableAlarm1Interrupt(&rtc2);

  // DS3231_clod_SetAlarm2(&rtc2, &alm2);
  // DS3231_clod_EnableAlarm2Interrupt(&rtc2);

  DS3231_clod_GetDateTime(&rtc2, &rtcNow);
  
  PCD8544_SetFont(&LCD, &Font_6x8);
  PCD8544_SetCursor(&LCD, 0, 0);
  PCD8544_WriteString(&LCD, "dzialam");
  PCD8544_UpdateScreen(&LCD);

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


#if NRF_DEMO
  /* ---------------------------------------------------------------
   * NRF24L01 Demo Initialization
   * ---------------------------------------------------------------
   * NOTE: This demo assumes:
   *   - A SEPARATE full-duplex SPI peripheral (e.g. hspi2) is configured
   *     for nRF24L01 (SPI1 is TX-only simplex for the LCD).
   *   - CSN, CE, IRQ GPIO pins are configured in CubeMX.
   *   - Replace 'hspi1' below with your actual nRF SPI handle.
   * --------------------------------------------------------------- */

  /* 1. Initialize the nRF24L01 driver */
  if (NRF24_Init(&nrf, &hspi2,   /* TODO: replace with nRF SPI handle */
                 SPI2_CS_GPIO_Port, SPI2_CS_Pin,
                 SPI2_CE_GPIO_Port, SPI2_CE_Pin,
                 SPI2_IRQ_GPIO_Port, SPI2_IRQ_Pin,
                 NRF_DelayUs) != HAL_OK) {
      /* Init failed - display error on LCD */
      PCD8544_SetFont(&LCD, &Font_6x8);
      PCD8544_SetCursor(&LCD, 0, 0);
      PCD8544_WriteString(&LCD, "NRF FAIL");
      PCD8544_UpdateScreen(&LCD);
      Error_Handler();
  }

  /* 2. Configure radio parameters */
  NRF24_SetChannel(&nrf, NRF_CHANNEL);          // RF channel
  NRF24_SetDataRate(&nrf, NRF24_DR_1MBPS);      // 1 Mbps
  NRF24_SetPALevel(&nrf, NRF24_PA_MAX);         // 0 dBm
  NRF24_SetCRC(&nrf, NRF24_CRC_2B);             // 2-byte CRC
  NRF24_SetAddressWidth(&nrf, NRF24_AW_5);      // 5-byte address
  NRF24_SetAutoRetr(&nrf, 1, 10);               // ARD=500us, ARC=10 retries

  /* 3. Configure addresses */
  NRF24_SetTXAddress(&nrf, NRF_TX_ADDR, 5);
  NRF24_SetRXAddress(&nrf, 0, NRF_TX_ADDR, 5);  // Pipe 0 = TX_ADDR for auto-ACK
  NRF24_SetRXAddress(&nrf, 1, NRF_RX_ADDR, 5);  // Pipe 1 for receiving

  /* 4. Configure pipes */
  NRF24_SetAutoAck(&nrf, 0, 1);                 // Auto-ACK on pipe 0
  NRF24_SetAutoAck(&nrf, 1, 1);                 // Auto-ACK on pipe 1
  NRF24_EnablePipe(&nrf, 0, 1);                 // Enable pipe 0
  NRF24_EnablePipe(&nrf, 1, 1);                 // Enable pipe 1
  NRF24_SetPayloadSize(&nrf, 0, NRF_PAYLOAD_SIZE);
  NRF24_SetPayloadSize(&nrf, 1, NRF_PAYLOAD_SIZE);

  /* 5. Display NRF ready status */
  PCD8544_SetFont(&LCD, &Font_6x8);
  PCD8544_SetCursor(&LCD, 0, 0);
  PCD8544_WriteString(&LCD, "NRF OK");
  PCD8544_UpdateScreen(&LCD);
  HAL_Delay(500);

  /* 6. Send a test packet (TX mode) */
  NRF_Demo_Transmit();

  /* 7. Switch to receive mode and wait for data */
  NRF_Demo_StartReceive();
#endif

  /*  Soft timer for LED toggle */
  softTimer = HAL_GetTick();

#if DRAWING_DEMO
  PCD8544_SetFont(&LCD, &Font_6x8);
  PCD8544_SetCursor(&LCD, 0, 0);
  PCD8544_WriteString(&LCD, "demo1");
  //PCD8544_DrawRectangle(&LCD, 10, 10, 10, 12);
  PCD8544_UpdateScreen(&LCD);  
#endif

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if(HAL_GetTick() - softTimer > 1000)
    {
      HAL_GPIO_TogglePin(USER_LED_GPIO_Port, USER_LED_Pin);
      softTimer = HAL_GetTick();
    }

#if NRF_DEMO
    /* --- NRF24 RX polling / IRQ handling --- */
    if (nrf_irq_flag) {
        nrf_irq_flag = 0;
        NRF_Demo_HandleIRQ();
    }
#endif

#if RTC_DEMO
    if (rtc2.DS3231_IRQ_Flag)
    {
      if(rtc2.DS3231_IRQ_Alarm & DS3231_IRQ_ALARM2) {
        // Alarm 2 triggered
        // Handle alarm event here (e.g., toggle an LED, update display, etc.)
        // Clear the alarm flag
      }
      else if(rtc2.DS3231_IRQ_Alarm & DS3231_IRQ_ALARM1) {
        // Alarm 1 triggered
        // Handle alarm event here (e.g., toggle an LED, update display, etc.)
        // Clear the alarm flag
        
      }
      else {
      
      }
      rtc2.DS3231_IRQ_Flag = 0;
      DS3231_clod_CheckAndClearAlarmFlags(&rtc2);
      DS3231_clod_GetDateTime(&rtc2, &rtcNow);
      PCD8544_SetCursor(&LCD, 0, 2);
      sprintf(buffer, "%02d:%02d:%02d", rtcNow.hours, rtcNow.minutes, rtcNow.seconds);
      PCD8544_ClearBufferLine(&LCD, 2);
      PCD8544_WriteString(&LCD, buffer);
      PCD8544_UpdateScreen(&LCD);
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

#if NRF_DEMO
  /* NRF24L01 IRQ pin (active low) */
  if (GPIO_Pin == SPI2_IRQ_Pin)
  {
    nrf_irq_flag = IRQ_FLAG_SET;
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

#if NRF_DEMO
/* ---------------------------------------------------------------
 * NRF24L01 Demo Functions
 * --------------------------------------------------------------- */

/**
 * @brief  Transmit a test packet via nRF24L01.
 *         Sends an 8-byte payload and displays the result on LCD.
 */
static void NRF_Demo_Transmit(void) {
    uint8_t tx_data[NRF_PAYLOAD_SIZE] = {'H', 'E', 'L', 'L', 'O', ' ', 'N', 'R'};

    /* Load payload into TX FIFO */
    NRF24_WritePayload(&nrf, tx_data, NRF_PAYLOAD_SIZE);

    /* Trigger transmission (CE pulse) */
    NRF24_SetMode(&nrf, NRF24_MODE_TX);

    /* Wait for TX_DS or MAX_RT interrupt (with timeout) */
    uint32_t timeout = HAL_GetTick() + 100; // 100ms timeout
    while (!nrf_irq_flag && HAL_GetTick() < timeout);

    uint8_t status = NRF24_GetStatus(&nrf);

    PCD8544_ClearScreen(&LCD);
    PCD8544_SetCursor(&LCD, 0, 0);

    if (status & NRF24_STATUS_TX_DS) {
        /* Transmission success - ACK received */
        PCD8544_WriteString(&LCD, "TX: OK");
        NRF24_ClearIRQ(&nrf, NRF24_STATUS_TX_DS);
    } else if (status & NRF24_STATUS_MAX_RT) {
        /* Max retransmits - no ACK received */
        PCD8544_WriteString(&LCD, "TX: NO ACK");
        NRF24_ClearIRQ(&nrf, NRF24_STATUS_MAX_RT);
        NRF24_FlushTX(&nrf);
    } else {
        PCD8544_WriteString(&LCD, "TX: TIMEOUT");
    }

    /* Show OBSERVE_TX diagnostics */
    uint8_t obs = NRF24_GetObserveTX(&nrf);
    PCD8544_SetCursor(&LCD, 0, 1);
    sprintf(buffer, "Lost:%d Rt:%d", (obs >> 4) & 0x0F, obs & 0x0F);
    PCD8544_WriteString(&LCD, buffer);
    PCD8544_UpdateScreen(&LCD);

    nrf_irq_flag = 0;
    HAL_Delay(1000);
}

/**
 * @brief  Switch nRF24L01 to RX mode and start listening.
 */
static void NRF_Demo_StartReceive(void) {
    NRF24_FlushRX(&nrf);
    NRF24_ClearIRQ(&nrf, NRF24_STATUS_IRQ_MASK);
    NRF24_SetMode(&nrf, NRF24_MODE_RX);

    PCD8544_ClearScreen(&LCD);
    PCD8544_SetCursor(&LCD, 0, 0);
    PCD8544_WriteString(&LCD, "RX: Listen..");
    PCD8544_UpdateScreen(&LCD);
}

/**
 * @brief  Handle nRF24L01 IRQ: read received data or handle errors.
 */
static void NRF_Demo_HandleIRQ(void) {
    uint8_t status = NRF24_GetStatus(&nrf);

    if (status & NRF24_STATUS_RX_DR) {
        /* Data received */
        uint8_t pipe = (status >> 1) & 0x07;
        uint8_t rx_data[NRF24_MAX_PAYLOAD_SIZE];

        NRF24_ReadPayload(&nrf, rx_data, NRF_PAYLOAD_SIZE);
        NRF24_ClearIRQ(&nrf, NRF24_STATUS_RX_DR);

        /* Display received data on LCD */
        PCD8544_ClearScreen(&LCD);
        PCD8544_SetCursor(&LCD, 0, 0);
        sprintf(buffer, "RX P%d:", pipe);
        PCD8544_WriteString(&LCD, buffer);

        PCD8544_SetCursor(&LCD, 0, 1);
        /* Display as hex bytes */
        for (uint8_t i = 0; i < NRF_PAYLOAD_SIZE && i < 8; i++) {
            sprintf(buffer + (i * 3), "%02X ", rx_data[i]);
        }
        buffer[NRF_PAYLOAD_SIZE * 3] = '\0';
        PCD8544_WriteString(&LCD, buffer);

        PCD8544_SetCursor(&LCD, 0, 3);
        /* Also display as ASCII (printable chars only) */
        for (uint8_t i = 0; i < NRF_PAYLOAD_SIZE; i++) {
            buffer[i] = (rx_data[i] >= 0x20 && rx_data[i] <= 0x7E) ? rx_data[i] : '.';
        }
        buffer[NRF_PAYLOAD_SIZE] = '\0';
        PCD8544_WriteString(&LCD, buffer);

        /* Check if more data in FIFO (per datasheet recommended procedure) */
        uint8_t fifo = NRF24_GetFIFOStatus(&nrf);
        PCD8544_SetCursor(&LCD, 0, 5);
        if (fifo & NRF24_FIFO_RX_EMPTY) {
            PCD8544_WriteString(&LCD, "FIFO: empty");
        } else {
            PCD8544_WriteString(&LCD, "FIFO: more");
        }

        PCD8544_UpdateScreen(&LCD);
    }

    if (status & NRF24_STATUS_TX_DS) {
        /* ACK sent (PRX mode) or ACK received (PTX mode) */
        NRF24_ClearIRQ(&nrf, NRF24_STATUS_TX_DS);
    }

    if (status & NRF24_STATUS_MAX_RT) {
        NRF24_ClearIRQ(&nrf, NRF24_STATUS_MAX_RT);
        NRF24_FlushTX(&nrf);
    }
}
#endif /* NRF_DEMO */
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
