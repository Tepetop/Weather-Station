/*
 * PCD8544_Drawing.c
 *
 *  Created on: Feb 12, 2026
 *      Author: remik
 */
/**
 * --------------------------------------------------------------------------------------------+
 * @desc        LCD PCD8544 Drawing Functions - Advanced graphics primitives
 * --------------------------------------------------------------------------------------------+
 *
 *              Copyright (C) 2026 Remigiusz Pieprzyk
 *              Drawing functions for PCD8544 LCD display including lines and circles
 *
 */

#include "PCD8544_Drawing.h"

// Pixel aspect ratio correction for Nokia 5110 (PCD8544)
// Display: 84x48 pixels on ~43x43mm screen
// Pixels are rectangular: Y axis is stretched
// Correction factor: multiply Y radius by 3/4 (0.75) to get circular appearance
// Adjust these values if circles appear elongated: increase for rounder, decrease for flatter
#define PCD8544_Y_SCALE_NUM     4
#define PCD8544_Y_SCALE_DEN     5

/**
 * @brief   Draw a line between two points using Bresenham's algorithm
 *
 * @details Uses Bresenham's line algorithm for efficient integer-based line drawing.
 *          The algorithm handles all octants and slopes including vertical and horizontal lines.
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x1 - starting X coordinate (0-83)
 * @param   y1 - starting Y coordinate (0-47)
 * @param   x2 - ending X coordinate (0-83)
 * @param   y2 - ending Y coordinate (0-47)
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status PCD8544_DrawLine(PCD8544_t *PCD, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2)
{
    // Determinant for decision parameter
    int16_t D;
    // Delta values
    int16_t delta_x;
    int16_t delta_y;
    // Step direction
    int16_t trace_x = 1;
    int16_t trace_y = 1;

    // Calculate delta X
    delta_x = x2 - x1;
    // Calculate delta Y
    delta_y = y2 - y1;

    // Check if line goes left (x2 < x1)
    if (delta_x < 0) {
        // Make delta positive
        delta_x = -delta_x;
        // Reverse step direction
        trace_x = -trace_x;
    }

    // Check if line goes up (y2 < y1)
    if (delta_y < 0) {
        // Make delta positive
        delta_y = -delta_y;
        // Reverse step direction
        trace_y = -trace_y;
    }

    // Bresenham algorithm for slope < 1 (dx > dy)
    if (delta_y < delta_x) {
        // Initialize determinant
        D = 2 * delta_y - delta_x;
        // Draw starting pixel
        PCD8544_DrawPixel(PCD, x1, y1);
        
        // Iterate through all X coordinates
        while (x1 != x2) {
            // Move to next X
            x1 += trace_x;
            // Check decision parameter
            if (D >= 0) {
                // Move diagonally
                y1 += trace_y;
                // Update determinant
                D -= 2 * delta_x;
            }
            // Update determinant
            D += 2 * delta_y;
            // Draw pixel at current position
            PCD8544_DrawPixel(PCD, x1, y1);
        }
    }
    // Bresenham algorithm for slope >= 1 (dy >= dx)
    else {
        // Initialize determinant
        D = delta_y - 2 * delta_x;
        // Draw starting pixel
        PCD8544_DrawPixel(PCD, x1, y1);
        
        // Iterate through all Y coordinates
        while (y1 != y2) {
            // Move to next Y
            y1 += trace_y;
            // Check decision parameter
            if (D <= 0) {
                // Move diagonally
                x1 += trace_x;
                // Update determinant
                D += 2 * delta_y;
            }
            // Update determinant
            D -= 2 * delta_x;
            // Draw pixel at current position
            PCD8544_DrawPixel(PCD, x1, y1);
        }
    }
    
    // Return success
    return PCD_OK;
}

/**
 * @brief   Draw a circle outline at specified center with given radius
 *
 * @details Uses modified midpoint circle algorithm with aspect ratio correction for
 *          Nokia 5110 rectangular pixels. The algorithm compensates for the stretched
 *          Y-axis to produce visually circular shapes.
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   r - radius in pixels (along X axis)
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status PCD8544_DrawCircle(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t r)
{
    int16_t x = r;
    int16_t y = 0;
    int16_t err = 0;

    // Modified midpoint circle algorithm with Y-axis scaling
    while (x >= y) {
        // Calculate scaled Y positions
        int16_t sy = (y * PCD8544_Y_SCALE_NUM) / PCD8544_Y_SCALE_DEN;
        int16_t sx = (x * PCD8544_Y_SCALE_NUM) / PCD8544_Y_SCALE_DEN;
        
        // Draw pixels in all 8 octants with Y-axis correction
        PCD8544_DrawPixel(PCD, x0 + x, y0 + sy);  // Octant 1
        PCD8544_DrawPixel(PCD, x0 + y, y0 + sx);  // Octant 2
        PCD8544_DrawPixel(PCD, x0 - y, y0 + sx);  // Octant 3
        PCD8544_DrawPixel(PCD, x0 - x, y0 + sy);  // Octant 4
        PCD8544_DrawPixel(PCD, x0 - x, y0 - sy);  // Octant 5
        PCD8544_DrawPixel(PCD, x0 - y, y0 - sx);  // Octant 6
        PCD8544_DrawPixel(PCD, x0 + y, y0 - sx);  // Octant 7
        PCD8544_DrawPixel(PCD, x0 + x, y0 - sy);  // Octant 8

        // Increment y
        y++;
        err += 1 + 2 * y;
        
        // Check and adjust error term
        if (2 * (err - x) + 1 > 0) {
            x--;
            err += 1 - 2 * x;
        }
    }

    return PCD_OK;
}

/**
 * @brief   Draw a filled circle at specified center with given radius
 *
 * @details Draws a solid filled circle by drawing horizontal lines between circle
 *          boundaries. Uses modified midpoint circle algorithm with aspect ratio
 *          correction for rectangular pixels.
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   r - radius in pixels (along X axis)
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status PCD8544_FillCircle(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t r)
{
    int16_t x = r;
    int16_t y = 0;
    int16_t err = 0;

    // Modified midpoint circle algorithm with Y-axis scaling
    while (x >= y) {
        // Calculate scaled Y positions
        int16_t sy = (y * PCD8544_Y_SCALE_NUM) / PCD8544_Y_SCALE_DEN;
        int16_t sx = (x * PCD8544_Y_SCALE_NUM) / PCD8544_Y_SCALE_DEN;
        
        // Draw horizontal lines in all 4 quadrants with Y-axis correction
        // Upper half
        PCD8544_DrawLine(PCD, x0 - x, y0 + sy, x0 + x, y0 + sy);
        PCD8544_DrawLine(PCD, x0 - y, y0 + sx, x0 + y, y0 + sx);
        
        // Lower half
        PCD8544_DrawLine(PCD, x0 - x, y0 - sy, x0 + x, y0 - sy);
        PCD8544_DrawLine(PCD, x0 - y, y0 - sx, x0 + y, y0 - sx);

        // Increment y
        y++;
        err += 1 + 2 * y;
        
        // Check and adjust error term
        if (2 * (err - x) + 1 > 0) {
            x--;
            err += 1 - 2 * x;
        }
    }

    return PCD_OK;
}
