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

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

void UartLog(char *msg);
HAL_StatusTypeDef I2C_CheckAddress(I2C_HandleTypeDef *i2c);
static void NRF_DelayUs(uint32_t us);
static void Outdoor_LedOn(void);
static void Outdoor_LedOff(void);
static void NRF_StartReceive(void);
static void NRF_HandleIRQ(void);
static void NRF_SendMeasurementData(Measurement_Data_t *txData);

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

  /* ---------------------------------------------------------------
   * NRF24L01 Initialization - OutdoorUnit as RX (slave)
   * --------------------------------------------------------------- */

  /* 1. Initialize the nRF24L01 driver */
  if (NRF24_Init(&nrf, &hspi1,NRF_CS_GPIO_Port, NRF_CS_Pin,NRF_CE_GPIO_Port, NRF_CE_Pin, NRF_IRQ_GPIO_Port, NRF_IRQ_Pin, NRF_DelayUs) != HAL_OK) 
    {
      UartLog("NRF24 Init FAILED!\r\n");
      Error_Handler();
    }

  /* 2. Configure radio parameters - MUST match IndoorUnit */
  NRF24_SetChannel(&nrf, NRF_CHANNEL);          // RF channel 76
  NRF24_SetDataRate(&nrf, NRF24_DR_1MBPS);      // 1 Mbps
  NRF24_SetPALevel(&nrf, NRF24_PA_MAX);         // 0 dBm
  NRF24_SetCRC(&nrf, NRF24_CRC_2B);             // 2-byte CRC
  NRF24_SetAddressWidth(&nrf, NRF24_AW_5);      // 5-byte address
  NRF24_SetAutoRetr(&nrf, 1, 10);               // ARD=500us, ARC=10 retries

  /* 3. Configure addresses (multiceiver — derived from NODE_ID) */
  NRF24_SetTXAddress(&nrf, NRF_TX_ADDR, 5);     // TX → Indoor's Pipe (1+NODE_ID)
  NRF24_SetRXAddress(&nrf, 0, NRF_TX_ADDR, 5);  // Pipe 0 = TX_ADDR for auto-ACK
  NRF24_SetRXAddress(&nrf, 1, NRF_RX_ADDR, 5);  // Pipe 1 = receive commands from Indoor

  /* 4. Configure pipes */
  NRF24_SetAutoAck(&nrf, 0, 1);                 // Auto-ACK on pipe 0
  NRF24_SetAutoAck(&nrf, 1, 1);                 // Auto-ACK on pipe 1
  NRF24_EnablePipe(&nrf, 0, 1);                 // Enable pipe 0
  NRF24_EnablePipe(&nrf, 1, 1);                 // Enable pipe 1
  NRF24_SetPayloadSize(&nrf, 0, NRF_PAYLOAD_SIZE);
  NRF24_SetPayloadSize(&nrf, 1, NRF_CMD_SIZE);  // Pipe 1 for commands

  outLink.irq_flag = 0U;
  outLink.cmd_received = 0U;
  outLink.tx_in_progress = 0U;
  outLink.tx_done = 0U;
  outLink.tx_ok = 0U;
  outLink.last_status = 0U;
  outLink.state = OUT_LINK_IDLE;

  UartLog("NRF24 Init OK - Listening for commands...\r\n");
  Outdoor_LedOff();

  /* 5. Start in RX mode, waiting for commands */
  NRF24_FlushRX(&nrf);
  NRF_StartReceive();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

  /* Minimal fallback if EXTI edge is missed while waiting for NRF events. */
  if (!outLink.irq_flag && (outLink.tx_in_progress || outLink.cmd_received)) {
    uint8_t st = NRF24_GetStatus(&nrf);
    if (st & (NRF24_STATUS_RX_DR | NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT)) {
      outLink.irq_flag = 1U;
    }
  }

    /* --- NRF24 RX polling / IRQ handling --- */
  if (outLink.irq_flag) {
    outLink.irq_flag = 0;
        NRF_HandleIRQ();
    }

  /* --- TX completion handling --- */
  if (outLink.tx_done) {
    outLink.tx_done = 0;
    outLink.tx_in_progress = 0;

    if (outLink.tx_ok) {
      UartLog("TX: Data sent OK\r\n");
      outLink.state = OUT_LINK_IDLE;
    } else {
      UartLog("TX: FAILED - no ACK\r\n");
      outLink.state = OUT_LINK_ERROR;
    }

    Outdoor_LedOff();
    NRF_StartReceive();
  }

  if (outLink.tx_in_progress && ((HAL_GetTick() - outLink.tx_start_tick) > NRF_TX_TIMEOUT_MS)) {
    uint8_t st = NRF24_GetStatus(&nrf);
    if (st & (NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT)) {
      outLink.irq_flag = 1U;
      NRF_HandleIRQ();
    }
  }

  if (outLink.tx_in_progress && !outLink.tx_done && ((HAL_GetTick() - outLink.tx_start_tick) > NRF_TX_TIMEOUT_MS)) {
    outLink.tx_in_progress = 0;
    outLink.tx_ok = 0;
    outLink.state = OUT_LINK_ERROR;
    UartLog("TX: IRQ TIMEOUT\r\n");
    Outdoor_LedOff();
    NRF_StartReceive();
  }
    
    /* --- Command received: perform measurement and send data back --- */
  if (outLink.cmd_received && !outLink.tx_in_progress) {
    outLink.cmd_received = 0;
    outLink.state = OUT_LINK_CMD_PENDING;

    Outdoor_LedOn();
        
    /* Perform measurement cycle */
    Measurement_Start();
    
    /* Wait for measurement to complete */
    uint32_t timeout = HAL_GetTick() + 1000; // 1 second timeout

    do {
        Measurement_Process();
    }
    while (Measurement_GetState() != MEAS_SLEEP && HAL_GetTick() < timeout);
    
    
    /* Send measurement data via NRF */
    NRF_SendMeasurementData(&txData);

    sprintf(Message, "Measurement sent\r\n");
    
    UartLog(Message);
    }

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

/* ---------------------------------------------------------------
 * NRF24L01 Functions - OutdoorUnit (Slave/Receiver)
 * --------------------------------------------------------------- */

/**
 * @brief  Switch nRF24L01 to RX mode and start listening for commands.
 */
static void NRF_StartReceive(void) {
  NRF24_SetMode(&nrf, NRF24_MODE_STANDBY);
    NRF24_ClearIRQ(&nrf, NRF24_STATUS_IRQ_MASK);
    NRF24_SetMode(&nrf, NRF24_MODE_RX);
}

/**
 * @brief  Handle nRF24L01 IRQ: receive command from IndoorUnit.
 */
static void NRF_HandleIRQ(void) {
  
  uint8_t status = NRF24_GetStatus(&nrf);
  outLink.last_status = status;

  if (status & NRF24_STATUS_RX_DR) {
      /* Data received - check if it's a measurement command */
      uint8_t rx_data[NRF_CMD_SIZE];
      NRF24_ReadPayload(&nrf, rx_data, NRF_CMD_SIZE);
      NRF24_ClearIRQ(&nrf, NRF24_STATUS_RX_DR);

      /* Check command byte */
      if (rx_data[0] == CMD_MEASURE) {
          outLink.cmd_received = 1;
          outLink.state = OUT_LINK_CMD_PENDING;
          UartLog("CMD: Measure request received\r\n");
      }
  }

  if (status & NRF24_STATUS_TX_DS) {
      /* ACK sent (PRX mode) or TX complete (PTX mode) */
      NRF24_ClearIRQ(&nrf, NRF24_STATUS_TX_DS);
    outLink.tx_ok = 1;
    outLink.tx_done = 1;
  }

  if (status & NRF24_STATUS_MAX_RT) {
      NRF24_ClearIRQ(&nrf, NRF24_STATUS_MAX_RT);
      NRF24_FlushTX(&nrf);
    outLink.tx_ok = 0;
    outLink.tx_done = 1;
  }
}

/**
 * @brief  Send measurement data to IndoorUnit via nRF24L01.
 */
static void NRF_SendMeasurementData(Measurement_Data_t *txData) {
  if(txData == NULL) {
      return; 
  }
    if (outLink.tx_in_progress) {
      return;
    }

  /* Get measurement data directly */
  Measurement_GetData(txData);

  outLink.irq_flag = 0;
  outLink.tx_done = 0;
  outLink.tx_ok = 0;
  outLink.tx_in_progress = 1;
  outLink.tx_start_tick = HAL_GetTick();
  outLink.state = OUT_LINK_TX_IN_PROGRESS;

  NRF24_SetMode(&nrf, NRF24_MODE_STANDBY);
  NRF24_FlushTX(&nrf);
  NRF24_ClearIRQ(&nrf, NRF24_STATUS_IRQ_MASK);

  /* Load payload into TX FIFO */
  NRF24_WritePayload(&nrf, (uint8_t*)txData, sizeof(Measurement_Data_t));

  /* Trigger transmission (CE pulse) */
  NRF24_SetMode(&nrf, NRF24_MODE_TX);
}

/**
 * @brief  GPIO EXTI callback for NRF24L01 IRQ pin.
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == NRF_IRQ_Pin) {
  outLink.irq_flag = 1;
  }
}


/*      PRIVATE FUNCTIONS      */

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

static void Outdoor_LedOn(void) {
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_RESET);
}

static void Outdoor_LedOff(void) {
  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_SET);
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
