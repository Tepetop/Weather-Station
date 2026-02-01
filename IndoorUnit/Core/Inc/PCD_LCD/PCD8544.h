/*
 * PCD_LCD.h
 *
 *  Created on: Sep 25, 2024
 *      Author: remik
 */

#ifndef INC_PCD8544_H_
#define INC_PCD8544_H_

#include <main.h>
#include <stdio.h>
#include <string.h>

#include "PCD8544_fonts.h"


// AREA definition
// -----------------------------------
#define PCD8544_WIDTH      			84
#define PCD8544_HEIGHT				48
#define PCD8544_CHAR_PIXEL_X			6 											/* 6 pix in X axis per char (space included) */
#define PCD8544_CHAR_PIXEL_Y			8											/* 8 pix in Y axis per char (space included) */
#define MIN_ROW_COLS				0
#define PCD8544_BUFFER_SIZE    			(PCD8544_WIDTH * PCD8544_HEIGHT / 8)


// Status
typedef enum
{
  PCD_OK	= 0x00U,
  PCD_ERROR,
  PCD_TransmitError,
  PCD_OutOfBounds
} PCD_Status;

// Communication Mode
typedef enum
{
  PCD_SPI_MODE_BLOCKING = 0x00U,
  PCD_SPI_MODE_DMA
} PCD_SPI_Mode;

typedef struct
{
	uint16_t			PCD8544_BUFFER_INDEX;        				//  array Cache memory char index
	uint8_t				PCD8544_CurrentX;
	uint8_t				PCD8544_CurrentY;
   	uint8_t				PCD8544_BUFFER[PCD8544_BUFFER_SIZE]; 	// array Cache memory Lcd 6 * 84 = 504 bytes
}PCD8544_BUFFER_INFO_T;

typedef struct
{
	uint8_t				font_width;  // Current font width in pixels
	uint8_t				font_height; // Current font height in pixels
	uint8_t				PCD8544_ROWS; // Number of character rows (calculated from font height)
	uint8_t				PCD8544_COLS; // Number of character columns (calculated from font width)
	uint16_t     *font;
}PCD8544_FONT_INFO_t;

typedef struct
{
	/*PORTS AND HANDLER*/
	SPI_HandleTypeDef	*PCD8544_SPI; // SPI hadler for PCD8544
	GPIO_TypeDef		*DC_GPIOPort; // GPIO DC Port for a button
	GPIO_TypeDef		*RST_GPIOPort; // GPIO RST Port for a button
	GPIO_TypeDef		*CE_GPIOPort; // GPIO CE Port for a button

	/*PINS*/
	uint16_t		 	DC_GpioPin; // GPIO DC Pin for a button
	uint16_t			RST_GpioPin; // GPIO RST Pin for a button
	uint16_t			CE_GpioPin; // GPIO CE Pin for a button
	/*INTERNAL STRUCTURES*/
	PCD8544_FONT_INFO_t font;
	PCD8544_BUFFER_INFO_T buffer;

	PCD_SPI_Mode		PCD8544_SPI_Mode; // Communication mode (blocking or DMA)
}PCD8544_t;


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

/*				Functions declarations						*/

PCD_Status PCD8544_Init (PCD8544_t *PCD, SPI_HandleTypeDef *hspi, GPIO_TypeDef *dc_port, uint16_t dc_pin,
		GPIO_TypeDef *ce_port, uint16_t ce_pin, GPIO_TypeDef *rst_port, uint16_t rst_pin);

PCD_Status PCD8544_SetCommunicationMode(PCD8544_t *PCD, PCD_SPI_Mode mode);

PCD_Status PCD8544_SetFont(PCD8544_t *PCD, const PCD8544_Font_t *Font);

PCD_Status PCD8544_CommandSend (PCD8544_t *PCD, uint8_t data);

PCD_Status PCD8544_DrawBitMap(PCD8544_t *PCD, uint8_t *bitmap, uint16_t size);

PCD_Status PCD8544_DrawBitMap_DMA(PCD8544_t *PCD, uint8_t *bitmap, uint16_t size);

void PCD8544_ResetImpulse (PCD8544_t *PCD);

void PCD8544_ClearBuffer (PCD8544_t *PCD);

PCD_Status PCD8544_ClearScreen (PCD8544_t *PCD);

PCD_Status PCD8544_UpdateScreen (PCD8544_t *PCD);

void PCD8544_TxCpltCallback(PCD8544_t *PCD);


PCD_Status PCD8544_SetCursor(PCD8544_t *PCD, uint8_t x, uint8_t y);

PCD_Status PCD8544_DrawPixel (PCD8544_t *PCD, uint8_t x, uint8_t y);

PCD_Status PCD8544_WriteChar(PCD8544_t *PCD, const char *znak);

PCD_Status PCD8544_WriteString(PCD8544_t *PCD, const char *str);

PCD_Status PCD8544_WriteNumberToBuffer(PCD8544_t *PCD, uint8_t x, uint8_t y, int16_t number);



PCD_Status PCD8544_ClearBufferRegion(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t NumOfChars);

PCD_Status PCD8544_ClearBufferLine(PCD8544_t *PCD, uint8_t y);

PCD_Status PCD8544_InvertSelectedRegion(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t NumOfChars);

PCD_Status PCD8544_InvertLine(PCD8544_t *PCD, uint8_t y);




PCD_Status PCD8544_DrawLine (PCD8544_t *PCD, uint8_t x1, uint8_t x2, uint8_t y1, uint8_t y2);

#endif /* INC_PCD8544_H_ */
