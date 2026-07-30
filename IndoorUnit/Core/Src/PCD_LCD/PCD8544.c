/**
 * @file PCD8544.c
 * @brief PCD8544 (Nokia 5110) LCD driver implementation.
 * @details SPI/GPIO low-level routines, framebuffer management, text rendering,
 *          and screen update. CE and RST pins must be HIGH after GPIO init.
 */

#include "PCD8544.h"
#include "stm32f1xx_hal_gpio.h"

/**
 * @brief Initializes the PCD8544 controller, framebuffer, and default font.
 * @param[out]    PCD       Display driver instance to initialize.
 * @param[in]     hspi      SPI handle used for display communication.
 * @param[in]     dc_port   GPIO port for the DC (data/command) pin.
 * @param[in]     dc_pin    GPIO pin number for DC.
 * @param[in]     ce_port   GPIO port for the CE (chip enable) pin.
 * @param[in]     ce_pin    GPIO pin number for CE.
 * @param[in]     rst_port  GPIO port for the RST (reset) pin.
 * @param[in]     rst_pin   GPIO pin number for RST.
 * @param[in]     blk_port  GPIO port for the backlight pin.
 * @param[in]     blk_pin   GPIO pin number for backlight.
 * @retval PCD_OK             Initialization successful.
 * @retval PCD_OutOfBounds    PCD or hspi pointer is NULL.
 * @retval PCD_TransmitError  Controller command sequence failed.
 */
PCD_Status PCD8544_Init (PCD8544_t *PCD, SPI_HandleTypeDef *hspi, GPIO_TypeDef *dc_port, uint16_t dc_pin,
		GPIO_TypeDef *ce_port, uint16_t ce_pin, GPIO_TypeDef *rst_port, uint16_t rst_pin, GPIO_TypeDef *blk_port, uint16_t blk_pin)
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

    PCD -> BLK_GPIOPort = blk_port;
    PCD -> BLK_GpioPin = blk_pin;


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

    PCD8544_SetBacklight(PCD);

    //PCD8544_SetCursor(PCD, 0, 0);

    return state;
}

/**
 * @brief Sends a single controller command byte over SPI.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     data Command byte to transmit.
 * @retval PCD_OK             Command sent successfully.
 * @retval PCD_TransmitError  SPI transmit failed.
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
 * @brief Transmits framebuffer bytes to the display using blocking SPI.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     data Pointer to byte buffer (full framebuffer size).
 * @retval PCD_OK             Transfer completed successfully.
 * @retval PCD_TransmitError  SPI transmit failed.
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
 * @brief Transmits raw bitmap data directly to the display (blocking SPI).
 * @param[in,out] PCD    Display driver instance.
 * @param[in]     bitmap Pointer to bitmap byte array.
 * @param[in]     size   Number of bytes to transmit.
 * @retval PCD_OK             Transfer completed successfully.
 * @retval PCD_TransmitError  SPI transmit failed.
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
 * @brief Sets SPI transfer mode for screen updates and bitmap transfers.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     mode Blocking or DMA SPI mode.
 * @retval PCD_OK      Mode applied successfully.
 * @retval PCD_ERROR   NULL pointer or invalid mode value.
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
 * @brief Selects the active font and recalculates character grid dimensions.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     Font Font descriptor with width, height, and glyph data.
 * @retval PCD_OK      Font applied successfully.
 * @retval PCD_ERROR   PCD or Font pointer is NULL.
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
 * @brief Transmits a byte buffer to the display using DMA SPI.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     data Pointer to byte buffer.
 * @param[in]     size Number of bytes to transmit.
 * @retval PCD_OK             DMA transfer started successfully.
 * @retval PCD_TransmitError  SPI DMA setup failed.
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
 * @brief Transmits raw bitmap data directly to the display (DMA SPI).
 * @param[in,out] PCD    Display driver instance.
 * @param[in]     bitmap Pointer to bitmap byte array.
 * @param[in]     size   Number of bytes to transmit.
 * @retval PCD_OK             DMA transfer started successfully.
 * @retval PCD_TransmitError  SPI DMA setup failed.
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
 * @brief DMA transfer-complete callback; deselects CE after SPI DMA TX.
 * @param[in,out] PCD Display driver instance.
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
 * @brief Applies hardware reset pulse to the display controller.
 * @param[in,out] PCD Display driver instance.
 */
void PCD8544_ResetImpulse (PCD8544_t *PCD)
{
	HAL_GPIO_WritePin(PCD->RST_GPIOPort, PCD->RST_GpioPin, GPIO_PIN_RESET);
  HAL_Delay(10); // 1 ms delay between pin toggle
	HAL_GPIO_WritePin(PCD->RST_GPIOPort, PCD->RST_GpioPin, GPIO_PIN_SET);
}

/**
 * @brief Clears the internal framebuffer to zero without updating the display.
 * @param[in,out] PCD Display driver instance.
 */
void PCD8544_ClearBuffer (PCD8544_t *PCD)
{
  memset(PCD -> buffer.PCD8544_BUFFER, 0x00, PCD8544_BUFFER_SIZE);
}

/**
 * @brief Clears the framebuffer and pushes a blank screen to the display.
 * @param[in,out] PCD Display driver instance.
 * @retval PCD_OK             Screen cleared successfully.
 * @retval PCD_TransmitError  SPI transfer of cleared buffer failed.
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
 * @brief Pushes the internal framebuffer contents to the display.
 * @param[in,out] PCD Display driver instance.
 * @retval PCD_OK             Framebuffer transferred successfully.
 * @retval PCD_TransmitError  SPI transfer failed.
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
 * @brief Sets the text cursor position in character grid coordinates.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     x   Column index (0 to PCD8544_COLS - 1).
 * @param[in]     y   Row index (0 to PCD8544_ROWS - 1).
 * @retval PCD_OK           Cursor set successfully.
 * @retval PCD_OutOfBounds  x or y exceeds character grid.
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
 * @brief Sets a single pixel in the framebuffer.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     x   X coordinate in pixels (0 to PCD8544_WIDTH - 1).
 * @param[in]     y   Y coordinate in pixels (0 to PCD8544_HEIGHT - 1).
 * @retval PCD_OK           Pixel drawn successfully.
 * @retval PCD_OutOfBounds  Coordinates outside display area.
 */
PCD_Status PCD8544_DrawPixel(PCD8544_t *PCD, uint8_t x, uint8_t y)
{
  // check if x, y is in range (specific pixel in buffor matrix)
	if ((x >= PCD8544_WIDTH) || (y >= PCD8544_HEIGHT))
	  {
		// out of range
		return PCD_OutOfBounds;
	  }
  /*  Instead of font height, use bank height bcs each bank is 8 pixels high */
	PCD->buffer.PCD8544_BUFFER_INDEX = x + (y / PCD8544_BANK_HEIGHT) * PCD8544_WIDTH;
	PCD->buffer.PCD8544_BUFFER[PCD->buffer.PCD8544_BUFFER_INDEX] |= 1 << (y % PCD8544_BANK_HEIGHT);
	// success return
	return PCD_OK;
}

/**
 * @brief Writes one ASCII character at the current cursor using the active font.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     znak Pointer to single-character string.
 * @retval PCD_OK           Character written successfully.
 * @retval PCD_ERROR        NULL character or font data pointer.
 * @retval PCD_OutOfBounds  Character code outside supported range.
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
      uint16_t columnData = PCD->font.font[character * PCD->font.font_width + col];

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
 * @brief Writes one ASCII character using multi-bank tall font rendering.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     znak Pointer to single-character string.
 * @retval PCD_OK           Character written successfully.
 * @retval PCD_ERROR        NULL character or font data pointer.
 * @retval PCD_OutOfBounds  Character code outside supported range.
 */
PCD_Status PCD8544_WriteCharBig(PCD8544_t *PCD, const char *znak)
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

    uint8_t num_banks = (PCD->font.font_height + 7) / 8;

    // Draw character pixels using DrawPixel
    for (uint8_t bank = 0; bank < num_banks; bank++)
    {
        for (uint8_t col = 0; col < PCD->font.font_width; col++)
        {
            uint16_t bankData = PCD->font.font[character * PCD->font.font_width * num_banks + bank * PCD->font.font_width + col];

            for (uint8_t row = 0; row < 8; row++)
            {
                uint8_t actual_row = bank * 8 + row;
                if (actual_row >= PCD->font.font_height) break;

                if (bankData & (1 << row))
                {
                    // Draw pixel only if bit is set
                    PCD8544_DrawPixel(PCD, PCD->buffer.PCD8544_CurrentX + col, PCD->buffer.PCD8544_CurrentY + actual_row);
                }
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
 * @brief Writes a null-terminated string at the current cursor position.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     str Null-terminated ASCII string.
 * @retval PCD_OK    String written successfully.
 * @retval PCD_ERROR str pointer is NULL.
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
 * @brief Writes a null-terminated string using tall-font character rendering.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     str Null-terminated ASCII string.
 * @retval PCD_OK    String written successfully.
 * @retval PCD_ERROR str pointer is NULL.
 */
PCD_Status PCD8544_WriteStringBig(PCD8544_t *PCD, const char *str)
{
    // Check if string exists
    if (NULL == str)
    {
      return PCD_ERROR;
    }
    // Loop through the characters in the string
    while (*str != '\0')
    {
    	PCD8544_WriteCharBig(PCD, str);
    	str++;
    }

    return PCD_OK;
}

/**
 * @brief Writes a signed integer at a character grid position.
 * @param[in,out] PCD    Display driver instance.
 * @param[in]     x      Column index for the number.
 * @param[in]     y      Row index for the number.
 * @param[in]     number Value to format and write.
 * @retval PCD_OK           Operation completed (may return PCD_ERROR if incomplete).
 * @retval PCD_OutOfBounds  Grid position or formatted length out of range.
 * @retval PCD_ERROR        Internal write path not fully implemented.
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
 * @brief Clears a horizontal region of the framebuffer in character units.
 * @param[in,out] PCD        Display driver instance.
 * @param[in]     x          Starting column index.
 * @param[in]     y          Row index.
 * @param[in]     NumOfChars Number of character cells to clear.
 * @retval PCD_OK           Region cleared successfully.
 * @retval PCD_OutOfBounds  Grid position out of range.
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
 * @brief Clears one full text row in the framebuffer.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     y   Row index to clear.
 * @retval PCD_OK           Line cleared successfully.
 * @retval PCD_OutOfBounds  Row index out of range.
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
 * @brief Inverts a horizontal region of the framebuffer in character units.
 * @param[in,out] PCD        Display driver instance.
 * @param[in]     x          Starting column index.
 * @param[in]     y          Row index.
 * @param[in]     NumOfChars Number of character cells to invert.
 * @retval PCD_OK           Region inverted successfully.
 * @retval PCD_OutOfBounds  Grid position out of range.
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
 * @brief Inverts one full text row in the framebuffer.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     y   Row index to invert.
 * @retval PCD_OK           Line inverted successfully.
 * @retval PCD_OutOfBounds  Row index out of range.
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

/**
 * @brief Draws a centered title row formatted as "-TITLE-".
 * @param[in,out] PCD   Display driver instance.
 * @param[in]     title Null-terminated title string (without dash padding).
 * @retval PCD_OK    Title drawn successfully.
 * @retval PCD_ERROR PCD or title pointer is NULL.
 */
PCD_Status PCD_8544_DrawCenteredTitle(PCD8544_t *PCD, const char *title)
{
  if(PCD == NULL || title == NULL)
  {
    return PCD_ERROR;
  }
  const char* titleString = title;
  PCD_Status status;
  PCD8544_SetCursor(PCD, 0, 0); 
  
  // Calculate centering: (Screen_Width - Text_Width) / 2
  // Text is "-TITLE-"
  uint8_t textPixelWidth = (strlen(titleString) + 2) * PCD->font.font_width;
  
  if(textPixelWidth < PCD8544_WIDTH)
  {
      PCD->buffer.PCD8544_CurrentX = (PCD8544_WIDTH - textPixelWidth) / 2;
  }

  status = PCD8544_WriteString(PCD, "-");
  status = PCD8544_WriteString(PCD, (char*)titleString);
  status = PCD8544_WriteString(PCD, "-");
  return status;
}

/**
 * @brief Turns the display backlight on.
 * @param[in,out] PCD Display driver instance.
 */
void PCD8544_SetBacklight(PCD8544_t *PCD)
{
  if (PCD == NULL)
  {
    return;
  }
  HAL_GPIO_WritePin(PCD->BLK_GPIOPort, PCD->BLK_GpioPin, GPIO_PIN_SET);
}

/**
 * @brief Turns the display backlight off.
 * @param[in,out] PCD Display driver instance.
 */
void PCD8544_ResetBacklight(PCD8544_t *PCD)
{
  if (PCD == NULL)
  {
    return;
  }
  HAL_GPIO_WritePin(PCD->BLK_GPIOPort, PCD->BLK_GpioPin, GPIO_PIN_RESET);
}