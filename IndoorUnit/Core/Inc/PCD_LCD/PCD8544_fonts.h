/*
 * PCD8544_fonts.h
 *
 *  Created on: Apr 17, 2025
 *      Author: Remik
 */

#ifndef INC_PCD8544_FONTS_H_
#define INC_PCD8544_FONTS_H_

#include "main.h"
#include <PCD8544_config.h>

typedef struct
{
	const uint8_t width;                /**< Font width in pixels */
	const uint8_t height;               /**< Font height in pixels */
	uint16_t *const data;         /**< Pointer to font data array */
}PCD8544_Font_t;


#ifdef PCD8544_INCLUDE_FONT6x8
extern const PCD8544_Font_t Font_6x8;
#endif

#ifdef PCD8544_INCLUDE_FONT11x15
extern const PCD8544_Font_t Font_11x15;
#endif

#ifdef SSD1306_INCLUDE_FONT_7x10
extern const PCD8544_Font_t Font_7x10;	
#endif

#ifdef SSD1306_INCLUDE_FONT_11x18
extern const PCD8544_Font_t Font_11x18;
#endif

#ifdef SSD1306_INCLUDE_FONT_16x26
extern const PCD8544_Font_t Font_16x26;
#endif



#endif /* __PCD8544_FONTS_H_ */


