/**
 * @file PCD8544_config.h
 * @brief Build-time configuration for the PCD8544 LCD stack.
 * @details Selects compiled fonts, encoder menu behaviour, and optional
 *          details-view support for the indoor unit display subsystem.
 */

#ifndef INC_PCD8544_CONFIG_H_
#define INC_PCD8544_CONFIG_H_

/* ============================================================================
 * Font selection
 * ============================================================================ */

/** @brief Include 6×8 pixel font glyph data in the build */
#define PCD8544_INCLUDE_FONT6x8

/* ============================================================================
 * Menu behaviour
 * ============================================================================ */

/** @brief Enable encoder mode with "Return" option in submenus (0 = disabled) */
#define PCD8544_ENCODER_MODE   0

/** @brief Enable details view for leaf menu items (comment to disable) */
//#define PCD8544_SHOW_DETAILS

#include <main.h>

#endif /* INC_PCD8544_CONFIG_H_ */
