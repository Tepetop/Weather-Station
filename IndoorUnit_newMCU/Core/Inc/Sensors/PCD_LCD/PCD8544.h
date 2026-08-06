#ifndef INC_PCD8544_H_
#define INC_PCD8544_H_

/**
 * @file PCD8544.h
 * @brief PCD8544 (Nokia 5110) LCD driver API.
 * @details Declares display geometry, controller command constants, framebuffer
 *          management, text rendering, and SPI communication for the indoor unit.
 */

#include <main.h>
#include <stdio.h>
#include <string.h>

#include "PCD8544_fonts.h"

/* ============================================================================
 * Display geometry
 * ============================================================================ */

/** @brief Display width in pixels */
#define PCD8544_WIDTH               84
/** @brief Display height in pixels */
#define PCD8544_HEIGHT              48
/** @brief Minimum row/column index */
#define MIN_ROW_COLS                0
/** @brief Height of one display memory bank in pixels */
#define PCD8544_BANK_HEIGHT         8
/** @brief Framebuffer size in bytes (width × height / bank height) */
#define PCD8544_BUFFER_SIZE         (PCD8544_WIDTH * PCD8544_HEIGHT / PCD8544_BANK_HEIGHT)

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Driver operation status codes
 */
typedef enum
{
  PCD_OK = 0x00U,           /**< Operation completed successfully */
  PCD_ERROR,                /**< Generic driver error */
  PCD_TransmitError,        /**< SPI transmit failed */
  PCD_OutOfBounds           /**< Coordinate or buffer index out of range */
} PCD_Status;

/**
 * @brief SPI transfer mode for display updates
 */
typedef enum
{
  PCD_SPI_MODE_BLOCKING = 0x00U,  /**< Blocking HAL_SPI_Transmit */
  PCD_SPI_MODE_DMA                /**< Non-blocking HAL_SPI_Transmit_DMA */
} PCD_SPI_Mode;

/**
 * @brief Internal display buffer metadata and pixel cache
 */
typedef struct
{
	uint16_t			PCD8544_BUFFER_INDEX;        				/**< Current buffer write index */
	uint8_t				PCD8544_CurrentX;                        /**< Text cursor X in pixels */
	uint8_t				PCD8544_CurrentY;                        /**< Text cursor Y in pixels */
   	uint8_t				PCD8544_BUFFER[PCD8544_BUFFER_SIZE]; 	/**< Monochrome framebuffer */
} PCD8544_BUFFER_INFO_T;

/**
 * @brief Active font metrics and glyph bitmap pointer
 */
typedef struct
{
	uint8_t				font_width;       /**< Current font width in pixels */
	uint8_t				font_height;      /**< Current font height in pixels */
	uint8_t				PCD8544_ROWS;     /**< Character rows derived from font height */
	uint8_t				PCD8544_COLS;     /**< Character columns derived from font width */
	uint16_t     *font;                   /**< Pointer to active font glyph data */
} PCD8544_FONT_INFO_t;

/**
 * @brief PCD8544 display driver instance
 * @details Holds SPI/GPIO handles, font state, framebuffer, and transfer mode.
 */
typedef struct
{
	/*PORTS AND HANDLER*/
	SPI_HandleTypeDef	*PCD8544_SPI; /**< SPI handle for display communication */
	GPIO_TypeDef		*DC_GPIOPort; /**< GPIO port for DC (data/command) pin */
	GPIO_TypeDef		*RST_GPIOPort; /**< GPIO port for RST (reset) pin */
	GPIO_TypeDef		*CE_GPIOPort; /**< GPIO port for CE (chip enable) pin */
	GPIO_TypeDef		*BLK_GPIOPort; /**< GPIO port for backlight pin */

	/*PINS*/
	uint16_t		 	DC_GpioPin;   /**< DC pin number */
	uint16_t			RST_GpioPin;  /**< RST pin number */
	uint16_t			CE_GpioPin;   /**< CE pin number */
	uint16_t			BLK_GpioPin;  /**< Backlight pin number */

	/*INTERNAL STRUCTURES*/
	PCD8544_FONT_INFO_t font;              /**< Active font configuration */
	PCD8544_BUFFER_INFO_T buffer;          /**< Display framebuffer state */

	PCD_SPI_Mode		PCD8544_SPI_Mode; /**< Blocking or DMA SPI transfer mode */
} PCD8544_t;

// Function set
// -----------------------------------
// D7 D6 D5 D4 D3 D2 D1 D0
// 0   0  1  0  0 PD  V  H
//
// PD = {0, 1} => {Chip is active, Power Down}
//  V = {0, 1} => {Horizontal addressing, Vertical adressing}
//  H = {0, 1} => {Basic instruction set, Extended instruction set}
#define FUNCTION_SET      0x20
// PD
#define MODE_ACTIVE       0x00
#define MODE_P_DOWN       0x04
// V
#define HORIZ_ADDR_MODE   0x00
#define VERTI_ADDR_MODE   0x02
// H
#define EXTEN_INS_SET     0x01
#define BASIC_INS_SET     0x00

#define VOP_SET           0xC2

// Display control
// -----------------------------------
// D7 D6 D5 D4 D3 D2 D1 D0
// 0   0  0  0  1  D  0  E
//
// D, E = {0, 0} => Display blank
// D, E = {0, 1} => Normal mode
// D, E = {1, 0} => All display segments on
// D, E = {1, 1} => Inverse video mode
#define DISPLAY_CONTROL   0x08
// D, E
#define DISPLAY_BLANK     0x00
#define ALL_SEGMS_ON      0x01
#define NORMAL_MODE       0x04
#define INVERSE_MODE      0x05

// Temperature coefficient
// -----------------------------------
// D7 D6 D5 D4 D3 D2  D1  D0
// 0   0  0  0  0  1 TC1 TC0
//
// TC1, TC0 = {0, 0} => VLCD temperature coefficient 0
// TC1, TC0 = {0, 1} => VLCD temperature coefficient 1
// TC1, TC0 = {1, 0} => VLCD temperature coefficient 2
// TC1, TC0 = {1, 1} => VLCD temperature coefficient 3
#define TEMP_CONTROL      0x04
// TC1, TC0
#define TEMP_COEF_1       0x00
#define TEMP_COEF_2       0x01
#define TEMP_COEF_3       0x02
#define TEMP_COEF_4       0x03

// Bias control
// -----------------------------------
// D7 D6 D5 D4 D3  D2  D1  D0
// 0   0  0  0  0 BS2 BS1 BS0
//
#define BIAS_CONTROL      0x10
// BS2 BS1 BS0
#define BIAS_1_100        0x00
#define BIAS_1_80         0x01
#define BIAS_1_65         0x02
#define BIAS_1_48         0x03
#define BIAS_1_34         0x04
#define BIAS_1_24         0x05
#define BIAS_1_16         0x06
#define BIAS_1_8          0x07

/* ============================================================================
 * Public API
 * ============================================================================ */

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
		GPIO_TypeDef *ce_port, uint16_t ce_pin, GPIO_TypeDef *rst_port, uint16_t rst_pin, GPIO_TypeDef *blk_port, uint16_t blk_pin);

/**
 * @brief Sets SPI transfer mode for screen updates and bitmap transfers.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     mode Blocking or DMA SPI mode.
 * @retval PCD_OK      Mode applied successfully.
 * @retval PCD_ERROR   NULL pointer or invalid mode value.
 */
PCD_Status PCD8544_SetCommunicationMode(PCD8544_t *PCD, PCD_SPI_Mode mode);

/**
 * @brief Selects the active font and recalculates character grid dimensions.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     Font Font descriptor with width, height, and glyph data.
 * @retval PCD_OK      Font applied successfully.
 * @retval PCD_ERROR   PCD or Font pointer is NULL.
 */
PCD_Status PCD8544_SetFont(PCD8544_t *PCD, const PCD8544_Font_t *Font);

/**
 * @brief Sends a single controller command byte over SPI.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     data Command byte to transmit.
 * @retval PCD_OK             Command sent successfully.
 * @retval PCD_TransmitError  SPI transmit failed.
 */
PCD_Status PCD8544_CommandSend (PCD8544_t *PCD, uint8_t data);

/**
 * @brief Transmits raw bitmap data directly to the display (blocking SPI).
 * @param[in,out] PCD    Display driver instance.
 * @param[in]     bitmap Pointer to bitmap byte array.
 * @param[in]     size   Number of bytes to transmit.
 * @retval PCD_OK             Transfer completed successfully.
 * @retval PCD_TransmitError  SPI transmit failed.
 * @details Does not flush the internal framebuffer; call PCD8544_UpdateScreen separately if needed.
 */
PCD_Status PCD8544_DrawBitMap(PCD8544_t *PCD, uint8_t *bitmap, uint16_t size);

/**
 * @brief Transmits raw bitmap data directly to the display (DMA SPI).
 * @param[in,out] PCD    Display driver instance.
 * @param[in]     bitmap Pointer to bitmap byte array.
 * @param[in]     size   Number of bytes to transmit.
 * @retval PCD_OK             DMA transfer started successfully.
 * @retval PCD_TransmitError  SPI DMA setup failed.
 * @details CE pin is released in PCD8544_TxCpltCallback when transfer completes.
 */
PCD_Status PCD8544_DrawBitMap_DMA(PCD8544_t *PCD, uint8_t *bitmap, uint16_t size);

/**
 * @brief Applies hardware reset pulse to the display controller.
 * @param[in,out] PCD Display driver instance.
 */
void PCD8544_ResetImpulse (PCD8544_t *PCD);

/**
 * @brief Clears the internal framebuffer to zero without updating the display.
 * @param[in,out] PCD Display driver instance.
 */
void PCD8544_ClearBuffer (PCD8544_t *PCD);

/**
 * @brief Clears the framebuffer and pushes a blank screen to the display.
 * @param[in,out] PCD Display driver instance.
 * @retval PCD_OK             Screen cleared successfully.
 * @retval PCD_TransmitError  SPI transfer of cleared buffer failed.
 */
PCD_Status PCD8544_ClearScreen (PCD8544_t *PCD);

/**
 * @brief Pushes the internal framebuffer contents to the display.
 * @param[in,out] PCD Display driver instance.
 * @retval PCD_OK             Framebuffer transferred successfully.
 * @retval PCD_TransmitError  SPI transfer failed.
 */
PCD_Status PCD8544_UpdateScreen (PCD8544_t *PCD);

/**
 * @brief DMA transfer-complete callback; deselects CE after SPI DMA TX.
 * @param[in,out] PCD Display driver instance.
 * @details Call from HAL_SPI_TxCpltCallback in user code when using DMA mode.
 */
void PCD8544_TxCpltCallback(PCD8544_t *PCD);

/**
 * @brief Sets the text cursor position in character grid coordinates.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     x   Column index (0 to PCD8544_COLS - 1).
 * @param[in]     y   Row index (0 to PCD8544_ROWS - 1).
 * @retval PCD_OK           Cursor set successfully.
 * @retval PCD_OutOfBounds  x or y exceeds character grid.
 */
PCD_Status PCD8544_SetCursor(PCD8544_t *PCD, uint8_t x, uint8_t y);

/**
 * @brief Sets a single pixel in the framebuffer.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     x   X coordinate in pixels (0 to PCD8544_WIDTH - 1).
 * @param[in]     y   Y coordinate in pixels (0 to PCD8544_HEIGHT - 1).
 * @retval PCD_OK           Pixel drawn successfully.
 * @retval PCD_OutOfBounds  Coordinates outside display area.
 */
PCD_Status PCD8544_DrawPixel (PCD8544_t *PCD, uint8_t x, uint8_t y);

/**
 * @brief Writes one ASCII character at the current cursor using the active font.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     znak Pointer to single-character string.
 * @retval PCD_OK           Character written successfully.
 * @retval PCD_ERROR        NULL character or font data pointer.
 * @retval PCD_OutOfBounds  Character code outside supported range.
 */
PCD_Status PCD8544_WriteChar(PCD8544_t *PCD, const char *znak);

/**
 * @brief Writes one ASCII character using multi-bank tall font rendering.
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     znak Pointer to single-character string.
 * @retval PCD_OK           Character written successfully.
 * @retval PCD_ERROR        NULL character or font data pointer.
 * @retval PCD_OutOfBounds  Character code outside supported range.
 */
PCD_Status PCD8544_WriteCharBig(PCD8544_t *PCD, const char *znak);

/**
 * @brief Writes a null-terminated string at the current cursor position.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     str Null-terminated ASCII string.
 * @retval PCD_OK    String written successfully.
 * @retval PCD_ERROR str pointer is NULL.
 */
PCD_Status PCD8544_WriteString(PCD8544_t *PCD, const char *str);

/**
 * @brief Writes a null-terminated string using tall-font character rendering.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     str Null-terminated ASCII string.
 * @retval PCD_OK    String written successfully.
 * @retval PCD_ERROR str pointer is NULL.
 */
PCD_Status PCD8544_WriteStringBig(PCD8544_t *PCD, const char *str);

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
PCD_Status PCD8544_WriteNumberToBuffer(PCD8544_t *PCD, uint8_t x, uint8_t y, int16_t number);

/**
 * @brief Clears a horizontal region of the framebuffer in character units.
 * @param[in,out] PCD        Display driver instance.
 * @param[in]     x          Starting column index.
 * @param[in]     y          Row index.
 * @param[in]     NumOfChars Number of character cells to clear.
 * @retval PCD_OK           Region cleared successfully.
 * @retval PCD_OutOfBounds  Grid position out of range.
 */
PCD_Status PCD8544_ClearBufferRegion(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t NumOfChars);

/**
 * @brief Clears one full text row in the framebuffer.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     y   Row index to clear.
 * @retval PCD_OK           Line cleared successfully.
 * @retval PCD_OutOfBounds  Row index out of range.
 */
PCD_Status PCD8544_ClearBufferLine(PCD8544_t *PCD, uint8_t y);

/**
 * @brief Inverts a horizontal region of the framebuffer in character units.
 * @param[in,out] PCD        Display driver instance.
 * @param[in]     x          Starting column index.
 * @param[in]     y          Row index.
 * @param[in]     NumOfChars Number of character cells to invert.
 * @retval PCD_OK           Region inverted successfully.
 * @retval PCD_OutOfBounds  Grid position out of range.
 */
PCD_Status PCD8544_InvertSelectedRegion(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t NumOfChars);

/**
 * @brief Inverts one full text row in the framebuffer.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     y   Row index to invert.
 * @retval PCD_OK           Line inverted successfully.
 * @retval PCD_OutOfBounds  Row index out of range.
 */
PCD_Status PCD8544_InvertLine(PCD8544_t *PCD, uint8_t y);

/**
 * @brief Draws a centered title row formatted as "-TITLE-".
 * @param[in,out] PCD   Display driver instance.
 * @param[in]     title Null-terminated title string (without dash padding).
 * @retval PCD_OK    Title drawn successfully.
 * @retval PCD_ERROR PCD or title pointer is NULL.
 */
PCD_Status PCD_8544_DrawCenteredTitle(PCD8544_t *PCD, const char *title);

/**
 * @brief Turns the display backlight on.
 * @param[in,out] PCD Display driver instance.
 */
void PCD8544_SetBacklight(PCD8544_t *PCD);

/**
 * @brief Turns the display backlight off.
 * @param[in,out] PCD Display driver instance.
 */
void PCD8544_ResetBacklight(PCD8544_t *PCD);

#endif /* INC_PCD8544_H_ */
