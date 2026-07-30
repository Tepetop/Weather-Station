/**
 * @file PCD8544_fonts.h
 * @brief Bitmap font declarations for the PCD8544 display.
 * @details Declares the font descriptor type and extern font instances gated
 *          by PCD8544_config.h compile-time font selection macros.
 */

#ifndef INC_PCD8544_FONTS_H_
#define INC_PCD8544_FONTS_H_

#include "main.h"
#include <PCD8544_config.h>

/**
 * @brief Bitmap font descriptor
 * @details Width, height, and pointer to column-major glyph bitmap data.
 */
typedef struct
{
	const uint8_t width;        /**< Font width in pixels */
	const uint8_t height;       /**< Font height in pixels */
	uint16_t *const data;       /**< Pointer to font glyph bitmap array */
} PCD8544_Font_t;

#ifdef PCD8544_INCLUDE_FONT6x8
/** @brief 6×8 pixel ASCII font */
extern const PCD8544_Font_t Font_6x8;
#endif

#ifdef PCD8544_INCLUDE_FONT11x15
/** @brief 11×15 pixel ASCII font */
extern const PCD8544_Font_t Font_11x15;
#endif

#ifdef SSD1306_INCLUDE_FONT_7x10
/** @brief 7×10 pixel ASCII font */
extern const PCD8544_Font_t Font_7x10;
#endif

#ifdef SSD1306_INCLUDE_FONT_11x18
/** @brief 11×18 pixel ASCII font */
extern const PCD8544_Font_t Font_11x18;
#endif

#ifdef SSD1306_INCLUDE_FONT_16x26
/** @brief 16×26 pixel ASCII font */
extern const PCD8544_Font_t Font_16x26;
#endif

#endif /* INC_PCD8544_FONTS_H_ */
