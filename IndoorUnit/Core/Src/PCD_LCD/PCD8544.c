/*
 * PCD_LCD.c
 *
 *  Created on: Sep 25, 2024
 *      Author: remik
 */
/**
 * --------------------------------------------------------------------------------------------+
 * @desc        LCD driver PCD8544 / Nokia 5110, 3110 /
 * --------------------------------------------------------------------------------------------+
 *
 *              Copyright (C) 2024 Remigiusz Pieprzyk
 *              Library writen for SMT32 with HAL support based on Marian Hrinko (mato.hrinko@gmail.com)
 *              https://github.com/Matiasus/PCD8544/tree/master
 *
 *          VERY IMPORTANT !!! PINS CE AND RST MUST BE IN HIGH STATE AFTER GPIO INIT !!!!!
 *
 *
 *
 *
*/

#include "PCD8544.h"

/**
 * @desc    Initialise pcd8544 controller
 *
 * @param   void
 *
 * @return  void
 */
PCD_Status PCD8544_Init (PCD8544_t *PCD, SPI_HandleTypeDef *hspi, GPIO_TypeDef *dc_port, uint16_t dc_pin,
		GPIO_TypeDef *ce_port, uint16_t ce_pin, GPIO_TypeDef *rst_port, uint16_t rst_pin)
{
  if( NULL == PCD || NULL == hspi)
    {
      return PCD_OutOfBounds;
    }

    PCD_Status state = PCD_OK;
    // Peripherial initialize
    PCD -> PCD8544_SPI = hspi;

    PCD -> DC_GPIOPort = dc_port;
    PCD -> DC_GpioPin = dc_pin;

    PCD -> CE_GPIOPort = ce_port;
    PCD -> CE_GpioPin = ce_pin;

    PCD -> RST_GPIOPort = rst_port;
    PCD -> RST_GpioPin = rst_pin;


    PCD -> buffer.PCD8544_CurrentX = 0;
    PCD -> buffer.PCD8544_CurrentY = 0;

    PCD -> buffer.PCD8544_BUFFER_INDEX = 0;
    // Set default font size (6x8)
    PCD -> font.font_width = 6;
    PCD -> font.font_height = 8;
    // Calculate number of rows and columns based on font size
    PCD -> font.PCD8544_COLS = PCD8544_WIDTH / PCD -> font.font_width;
    PCD -> font.PCD8544_ROWS = PCD8544_HEIGHT / PCD -> font.font_height;
      // Initialize cacheMem and starting index
    memset(PCD -> buffer.PCD8544_BUFFER, 0x00, PCD8544_BUFFER_SIZE);
    // Set default communication mode to blocking
    PCD -> PCD8544_SPI_Mode = PCD_SPI_MODE_BLOCKING;
    
    // Initialize PCD display, 1 ms rst impuls
    PCD8544_ResetImpulse(PCD);
      // extended instruction set
    state = PCD8544_CommandSend (PCD, FUNCTION_SET | EXTEN_INS_SET);
    if (state != PCD_OK) return state;
      // Set Vop
    state = PCD8544_CommandSend (PCD, VOP_SET);
    if (state != PCD_OK) return state;
      // bias 1:48 - optimum bias value
    state = PCD8544_CommandSend (PCD, BIAS_CONTROL | BIAS_1_48);
    if (state != PCD_OK) return state;
      // temperature set - temperature coefficient of IC / correction 3
    state = PCD8544_CommandSend (PCD, TEMP_CONTROL | TEMP_COEF_3);
    if (state != PCD_OK) return state;
      // normal instruction set / horizontal adressing mode
    state = PCD8544_CommandSend (PCD, (FUNCTION_SET | BASIC_INS_SET | HORIZ_ADDR_MODE));
    if (state != PCD_OK) return state;
      // normal mode
    state = PCD8544_CommandSend (PCD, DISPLAY_CONTROL | NORMAL_MODE);

    //PCD8544_SetCursor(PCD, 0, 0);

    return state;
}

/**
 * @desc    Command send
 *
 * @param   char
 *
 * @return  void
 */
PCD_Status PCD8544_CommandSend (PCD8544_t *PCD, uint8_t data)
{
  // Select the device, CE - active LOW
  HAL_GPIO_WritePin(PCD->CE_GPIOPort, PCD->CE_GpioPin, GPIO_PIN_RESET);
  // Select command mode, DC - active LOW
  HAL_GPIO_WritePin(PCD->DC_GPIOPort, PCD->DC_GpioPin, GPIO_PIN_RESET);
  // Transmit data via SPI
  if(HAL_ERROR ==  HAL_SPI_Transmit(PCD->PCD8544_SPI, &data, 1, HAL_MAX_DELAY))
  {
    return PCD_TransmitError;
  }
  // Deselect the device, CE - inactive HIGH
  HAL_GPIO_WritePin(PCD->CE_GPIOPort, PCD->CE_GpioPin, GPIO_PIN_SET);
  return PCD_OK;
}

/**
 * @desc    Send data from buffer to PCD8544
 *
 * @param   uint8_t *data
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_SendDataFromBuffer (PCD8544_t *PCD,  uint8_t *data)
{
  // Select the device, CE - active LOW
  HAL_GPIO_WritePin(PCD->CE_GPIOPort, PCD->CE_GpioPin, GPIO_PIN_RESET);
  // Select data mode, DC - active HIGH
  HAL_GPIO_WritePin(PCD->DC_GPIOPort, PCD->DC_GpioPin, GPIO_PIN_SET);
  // Transmit data via SPI
  if(HAL_ERROR == HAL_SPI_Transmit(PCD->PCD8544_SPI, data, PCD8544_BUFFER_SIZE, HAL_MAX_DELAY))
  {
	  return PCD_TransmitError;
  }
  // Disable the device, CE - inactive HIGH
  HAL_GPIO_WritePin(PCD->CE_GPIOPort, PCD->CE_GpioPin, GPIO_PIN_SET);
  return PCD_OK;
}

/**
 * @desc    Display bitmap on screen. DO NOT UPDATE SCREEN AFTER THIS FUNCTION!!
 *
 * @param   PCD - pointer to PCD type struct
 * @param	bitmap - pointer to bitmap array
 * @param   size - sizeof bitmap
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_DrawBitMap(PCD8544_t *PCD, uint8_t *bitmap, uint16_t size)
{
  // Select the device, CE - active LOW
  HAL_GPIO_WritePin(PCD->CE_GPIOPort, PCD->CE_GpioPin, GPIO_PIN_RESET);
  // Select data mode, DC - active HIGH
  HAL_GPIO_WritePin(PCD->DC_GPIOPort, PCD->DC_GpioPin, GPIO_PIN_SET);
  // Transmit data via SPI
  if(HAL_ERROR == HAL_SPI_Transmit(PCD->PCD8544_SPI, bitmap, size, HAL_MAX_DELAY))
  {
	  return PCD_TransmitError;
  }
  // Deselect the device, CE - inactive HIGH
  HAL_GPIO_WritePin(PCD->CE_GPIOPort, PCD->CE_GpioPin, GPIO_PIN_SET);
  return PCD_OK;
}

/**
 * @desc    Set communication mode (blocking or DMA)
 *
 * @param   PCD - pointer to PCD type struct
 * @param   mode - communication mode (PCD_SPI_MODE_BLOCKING or PCD_SPI_MODE_DMA)
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_SetCommunicationMode(PCD8544_t *PCD, PCD_SPI_Mode mode)
{
  if (NULL == PCD)
  {
    return PCD_ERROR;
  }
  
  // Validate mode parameter
  if (mode != PCD_SPI_MODE_BLOCKING && mode != PCD_SPI_MODE_DMA)
  {
    return PCD_ERROR;
  }
  
  PCD->PCD8544_SPI_Mode = mode;
  return PCD_OK;
}

/**
 * @desc    Set font for PCD8544
 *
 * @param   PCD - pointer to PCD type struct
 * @param   Font - pointer to font structure
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_SetFont(PCD8544_t *PCD, const PCD8544_Font_t *Font)
{
  if (NULL == PCD || NULL == Font)
  {
    return PCD_ERROR;
  }
  
  // Set font width, height and pointer to char array from Font structure
  PCD->font.font_width = Font->width;
  PCD->font.font_height = Font->height;
  PCD->font.font = Font->data;
  
  // Calculate number of rows and columns based on font size
  PCD->font.PCD8544_COLS = PCD8544_WIDTH / PCD->font.font_width;
  PCD->font.PCD8544_ROWS = PCD8544_HEIGHT / PCD->font.font_height;
  
  return PCD_OK;
}

/**
 * @desc    Send data from buffer to PCD8544 using DMA
 *
 * @param   PCD - pointer to PCD type struct
 * @param   data - pointer to data buffer
 * @param   size - size of data to send
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_SendDataFromBuffer_DMA (PCD8544_t *PCD, uint8_t *data, uint16_t size)
{
  // Select the device, CE - active LOW
  HAL_GPIO_WritePin(PCD->CE_GPIOPort, PCD->CE_GpioPin, GPIO_PIN_RESET);
  // Select data mode, DC - active HIGH
  HAL_GPIO_WritePin(PCD->DC_GPIOPort, PCD->DC_GpioPin, GPIO_PIN_SET);
  // Transmit data via SPI using DMA
  if(HAL_ERROR == HAL_SPI_Transmit_DMA(PCD->PCD8544_SPI, data, size))
  {
    // On error, deselect the device
    HAL_GPIO_WritePin(PCD->CE_GPIOPort, PCD->CE_GpioPin, GPIO_PIN_SET);
    return PCD_TransmitError;
  }
  // Note: CE pin will be set HIGH in the DMA complete callback
  return PCD_OK;
}

/**
 * @desc    Display bitmap on screen using DMA. DO NOT UPDATE SCREEN AFTER THIS FUNCTION!!
 *
 * @param   PCD - pointer to PCD type struct
 * @param	bitmap - pointer to bitmap array
 * @param   size - sizeof bitmap
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_DrawBitMap_DMA(PCD8544_t *PCD, uint8_t *bitmap, uint16_t size)
{
  // Select the device, CE - active LOW
  HAL_GPIO_WritePin(PCD->CE_GPIOPort, PCD->CE_GpioPin, GPIO_PIN_RESET);
  // Select data mode, DC - active HIGH
  HAL_GPIO_WritePin(PCD->DC_GPIOPort, PCD->DC_GpioPin, GPIO_PIN_SET);
  // Transmit data via SPI using DMA
  if(HAL_ERROR == HAL_SPI_Transmit_DMA(PCD->PCD8544_SPI, bitmap, size))
  {
    // On error, deselect the device
    HAL_GPIO_WritePin(PCD->CE_GPIOPort, PCD->CE_GpioPin, GPIO_PIN_SET);
    return PCD_TransmitError;
  }
  // Note: CE pin will be set HIGH in the DMA complete callback
  return PCD_OK;
}

/**
 * @desc    DMA transfer complete callback
 *          This function should be called from HAL_SPI_TxCpltCallback in user code
 *          User needs to deselect the device (CE pin) after DMA transfer
 *
 * @param   PCD - pointer to PCD type struct
 *
 * @return  void
 */
void PCD8544_TxCpltCallback(PCD8544_t *PCD)
{
  // Validate pointer
  if (NULL == PCD)
  {
    return;
  }
  
  // Deselect the device, CE - inactive HIGH
  HAL_GPIO_WritePin(PCD->CE_GPIOPort, PCD->CE_GpioPin, GPIO_PIN_SET);
}


/**
 * @desc    Reset impulse required on init
 *
 * @param   void
 *
 * @return  void
 */
void PCD8544_ResetImpulse (PCD8544_t *PCD)
{
	HAL_GPIO_WritePin(PCD->RST_GPIOPort, PCD->RST_GpioPin, GPIO_PIN_RESET);
  HAL_Delay(10); // 1 ms delay between pin toggle
	HAL_GPIO_WritePin(PCD->RST_GPIOPort, PCD->RST_GpioPin, GPIO_PIN_SET);
}

/**
 * @desc    Clear buffer
 *
 * @param   void
 *
 * @return  void
 */
void PCD8544_ClearBuffer (PCD8544_t *PCD)
{
  memset(PCD -> buffer.PCD8544_BUFFER, 0x00, PCD8544_BUFFER_SIZE);
}

/**
 * @desc    Clear screen
 *
 * @param   PCD - pointer to PCD type struct
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_ClearScreen (PCD8544_t *PCD)
{
	PCD_Status status;
	memset(PCD -> buffer.PCD8544_BUFFER, 0x00, PCD8544_BUFFER_SIZE);
	
	// Use appropriate communication mode
	if (PCD->PCD8544_SPI_Mode == PCD_SPI_MODE_DMA)
	{
		status = PCD8544_SendDataFromBuffer_DMA(PCD, PCD->buffer.PCD8544_BUFFER, PCD8544_BUFFER_SIZE);
	}
	else
	{
		status = PCD8544_SendDataFromBuffer(PCD, PCD->buffer.PCD8544_BUFFER);
	}
	
	return status;
}

/**
 * @desc    Update screen with buffer data
 *
 * @param   PCD - pointer to PCD type struct
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_UpdateScreen (PCD8544_t *PCD)
{
	PCD_Status status;
	
	// Use appropriate communication mode
	if (PCD->PCD8544_SPI_Mode == PCD_SPI_MODE_DMA)
	{
		status = PCD8544_SendDataFromBuffer_DMA(PCD, PCD->buffer.PCD8544_BUFFER, PCD8544_BUFFER_SIZE);
	}
	else
	{
		status = PCD8544_SendDataFromBuffer(PCD, PCD->buffer.PCD8544_BUFFER);
	}
	
	return status;
}

/**
 * @desc   Set cursor on x and y position, depends on font chosen
 *
 * @param   PCD - pointer to PCD type struct
 * @param   x - row position in character units
 * @param   y - col position in character units
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_SetCursor(PCD8544_t *PCD, uint8_t x, uint8_t y)
{
	if (x > PCD->font.PCD8544_COLS || y > PCD->font.PCD8544_ROWS)
	{
		// out of range
		return PCD_OutOfBounds;
	}

	PCD->buffer.PCD8544_CurrentX = x * PCD->font.font_width;
  PCD->buffer.PCD8544_CurrentY = y * PCD->font.font_height;
  return PCD_OK;
}

/**
 * @desc    Draw pixel on x, y position
 *
 * @param   PCD - pointer to struct
 * @param   x - row position in pixels
 * @param   y - col position in pixels
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_DrawPixel(PCD8544_t *PCD, uint8_t x, uint8_t y)
{
  // check if x, y is in range (specific pixel in buffor matrix)
	if ((x >= PCD8544_WIDTH) || (y >= PCD8544_HEIGHT))
	  {
		// out of range
		return PCD_OutOfBounds;
	  }
	PCD->buffer.PCD8544_BUFFER_INDEX = x + (y / PCD->font.font_height) * PCD8544_WIDTH;
	PCD->buffer.PCD8544_BUFFER[PCD->buffer.PCD8544_BUFFER_INDEX] |= 1 << (y % PCD->font.font_height);
	// success return
	return PCD_OK;
}

/**
 * @desc    Write char into PCD buffer
 *
 * @param   PCD - pointer to PCD type struct
 * @param   znak - pointer to char
 * @param   Font - pointer to struct contains font type
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_WriteChar(PCD8544_t *PCD, const char *znak)
{
    if (NULL == znak || NULL == PCD->font.font)
    {
      return PCD_ERROR;
    }

    uint8_t character = *znak - 32;  // Calculate character offset in font array

    // Check if character is within supported range (32 to 127)
    if (character > (0x7f - 32))
    {
      return PCD_OutOfBounds;  // Invalid character
    }

    // Draw character pixels using DrawPixel
    for (uint8_t col = 0; col < PCD->font.font_width; col++)
    {
      uint8_t columnData = PCD->font.font[character * PCD->font.font_width + col];

      for (uint8_t row = 0; row < PCD->font.font_height; row++)
      {
          if (columnData & (1 << row))
          {
              // Draw pixel only if bit is set
              PCD8544_DrawPixel(PCD, PCD->buffer.PCD8544_CurrentX + col, PCD->buffer.PCD8544_CurrentY + row);
          }
      }
    }

    // Increment X for the next character(its needed for string writing)
    PCD->buffer.PCD8544_CurrentX += PCD->font.font_width;

    // Check & handle Y-axis wrapping if the character exceeds the screen's width
    if (PCD->buffer.PCD8544_CurrentX + PCD->font.font_width > PCD8544_WIDTH)
    {
      PCD->buffer.PCD8544_CurrentX = 0;  // Reset X to the beginning of the next line
      PCD->buffer.PCD8544_CurrentY += PCD->font.font_height; // Increment Y with spacing
    }

    return PCD_OK;
}

/**
 * @desc    Write string into PCD buffer
 *
 * @param   PCD - pointer to PCD type struct
 * @param   str - pointer to char
 * @param   Font - pointer to struct contains font type
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_WriteString(PCD8544_t *PCD, const char *str)
{
    // Check if string exists
    if (NULL == str)
    {
      return PCD_ERROR;
    }
    // Loop through the characters in the string
    while (*str != '\0')
    {
    	PCD8544_WriteChar(PCD, str);
    	str++;
    }

    return PCD_OK;
}

/**
 * @desc    Write value to buffer.
 *
 * @param   uint8_t x, uint8_t y - position, int16_t number - value to write
 *
 * @return  PCD Status
 */
PCD_Status PCD8544_WriteNumberToBuffer(PCD8544_t *PCD, uint8_t x, uint8_t y, int16_t number)
{
	if (x > PCD->font.PCD8544_COLS || y > PCD->font.PCD8544_ROWS)
    {
      return PCD_OutOfBounds;
    }

    char str[7];  // Buffer to store number value (max. -32768 to 32767)
    char prevStr[7];
    static int16_t previousNumber = 0;  // Store previous number to detect changes
    static uint8_t previousLength = 0;
    uint8_t length = 0;

    // Convert number to string
    length = sprintf(str, "%d", number);
    previousLength = sprintf(prevStr, "%d", previousNumber);

    // Check if buffer is large enough to store number
    if (7 < length)
    {
      return PCD_OutOfBounds;  // Number too long to fit on screen
    }

    // If number changes from negative to positive or vice versa - clear space after '-' sign
    if ((previousNumber < 0 && number >= 0) || (previousNumber >= 0 && number < 0))
    {
        /* Clear minus'-' sign and next cell on position x+1 because cursor will return to x,y position instead of x+1,y  */
    	PCD8544_ClearBufferRegion(PCD, x, y, 2);
    }

    // If number decreased (e.g. from 100 to 90), clear unnecessary digits
    if (previousLength > length)
    {
    	PCD8544_ClearBufferRegion(PCD, x + length, y, (previousLength - length));
    }

    // Update previous value
    previousNumber = number;

    // Write number to buffer at given position
    // Note: This function is incomplete and requires a function to write string at position
    // For now, return error to indicate incomplete implementation
    return PCD_ERROR;
}

/**
 * @desc    Clear selected region of buffer (clear one row in screen)
 *
 * @param   uint8_t x, uint8_t y - x and y position, uint8_t NumOfChars - number of chars to delete (from single line 14 is MAX)
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_ClearBufferRegion(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t NumOfChars)
{
	if (x > PCD->font.PCD8544_COLS || y > PCD->font.PCD8544_ROWS)
    {
      return PCD_OutOfBounds;
    }

	if(NumOfChars > (PCD->font.PCD8544_COLS - x))
	{
		NumOfChars = PCD->font.PCD8544_COLS - x;
	}
    // Calculate starting index in the buffer for the given x, y position
    uint16_t startIndex = y * PCD8544_WIDTH + (x * PCD->font.font_width);

    // Clear the specified region by setting it to 0x00
    for (uint8_t i = 0; i < (NumOfChars * PCD->font.font_width); i++)
    {
      if (startIndex + i < PCD8544_BUFFER_SIZE)
      {
        PCD->buffer.PCD8544_BUFFER[startIndex + i] = 0x00;
      }
    }
    return PCD_OK;
}

/**
 * @desc    Clear one line from buffer
 *
 * @param   uint8_t y - row
 *
 * @return  void
 */
PCD_Status PCD8544_ClearBufferLine(PCD8544_t *PCD, uint8_t y)
{
    if (y > PCD->font.PCD8544_ROWS)
    {
      return PCD_OutOfBounds;
    }
    // Calculate starting index in the buffer for the given y position
    uint16_t startIndex = y * PCD8544_WIDTH;

    // Clear the entire line by setting it to 0x00
    for (uint8_t i = 0; i < PCD8544_WIDTH; i++)
    {
    	PCD->buffer.PCD8544_BUFFER[startIndex + i] = 0x00;
    }
    return PCD_OK;
}

/**
 * @desc    Invert selected region in one line in bufer
 *
 * @param   uint8_t x, uint8_t y - x and y position, uint8_t NumOfChars - number of chars to delete (from signle line 14 is MAX)
 *
 * @return  PCD_Status
 */
PCD_Status PCD8544_InvertSelectedRegion(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t NumOfChars)
{
	if (x > PCD->font.PCD8544_COLS || y > PCD->font.PCD8544_ROWS)
    {
      return PCD_OutOfBounds;
    }

	if(NumOfChars > (PCD->font.PCD8544_COLS - x))
	{
		NumOfChars = PCD->font.PCD8544_COLS - x;
	}
    // Calculate starting index in the buffer for the given x, y position
    uint16_t startIndex = y * PCD8544_WIDTH + (x * PCD->font.font_width);

  // Clear the specified region by setting it to 0x00
  for (uint8_t i = 0; i < (NumOfChars * PCD->font.font_width); i++)
  {
    if (startIndex + i < PCD8544_BUFFER_SIZE)
    {
      // XOR buffer with 0XFF to invert it
      PCD->buffer.PCD8544_BUFFER[startIndex + i] ^= 0xFF;
    }
  }
    return PCD_OK;
}

/**
 * @desc    Inverty one line from buffer
 *
 * @param   uint8_t y - row
 *
 * @return  void
 */
PCD_Status PCD8544_InvertLine(PCD8544_t *PCD, uint8_t y)
{
	// TODO: when buffer is empty, inverting doesnt work. Same goes for overwriting data
    if (y > PCD->font.PCD8544_ROWS)
    {
        return PCD_OutOfBounds;
    }
    // Calculate starting index in the buffer for the given y position
    uint16_t startIndex = y * PCD8544_WIDTH;

    // XOR buffer with 0XFF to invert it
    for (uint8_t i = 0; i < PCD8544_WIDTH; i++)
    {
    	PCD->buffer.PCD8544_BUFFER[startIndex + i] ^= 0xFF;
    }
    return PCD_OK;
}
