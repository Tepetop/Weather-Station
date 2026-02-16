/*
 * PCD8544_Drawing.h
 *
 *  Created on: Feb 12, 2026
 *      Author: remik
 */

#ifndef INC_PCD8544_DRAWING_H_
#define INC_PCD8544_DRAWING_H_

#include <PCD8544.h>

/**
 * @brief   Draw a line between two points using Bresenham's algorithm
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x1 - starting X coordinate (0-83)
 * @param   y1 - starting Y coordinate (0-47)
 * @param   x2 - ending X coordinate (0-83)
 * @param   y2 - ending Y coordinate (0-47)
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_DrawLine(PCD8544_t *PCD, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);

/**
 * @brief   Draw a circle outline at specified center with given radius
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   r - radius in pixels
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_DrawCircle(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t r);

/**
 * @brief   Draw a filled circle at specified center with given radius
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   r - radius in pixels
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_FillCircle(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t r);

#endif /* INC_PCD8544_DRAWING_H_ */
