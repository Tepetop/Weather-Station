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
#include "PCD8544.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int16_t PCD8544_ClampI16(int16_t value, int16_t min, int16_t max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static uint8_t PCD8544_IsPointInBounds(int16_t x, int16_t y)
{
    return (x >= 0 && x < PCD8544_WIDTH && y >= 0 && y < PCD8544_HEIGHT) ? 1U : 0U;
}

static void PCD8544_DrawPixelSafe(PCD8544_t *PCD, int16_t x, int16_t y)
{
    if (PCD8544_IsPointInBounds(x, y)) {
        PCD8544_DrawPixel(PCD, (uint8_t)x, (uint8_t)y);
    }
}

static void PCD8544_DrawLineSafe(PCD8544_t *PCD, int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    if (PCD == NULL) {
        return;
    }

    if ((x1 < 0 && x2 < 0) ||
        (x1 >= PCD8544_WIDTH && x2 >= PCD8544_WIDTH) ||
        (y1 < 0 && y2 < 0) ||
        (y1 >= PCD8544_HEIGHT && y2 >= PCD8544_HEIGHT)) {
        return;
    }

    x1 = PCD8544_ClampI16(x1, 0, PCD8544_WIDTH - 1);
    y1 = PCD8544_ClampI16(y1, 0, PCD8544_HEIGHT - 1);
    x2 = PCD8544_ClampI16(x2, 0, PCD8544_WIDTH - 1);
    y2 = PCD8544_ClampI16(y2, 0, PCD8544_HEIGHT - 1);

    PCD8544_DrawLine(PCD, (uint8_t)x1, (uint8_t)y1, (uint8_t)x2, (uint8_t)y2);
}

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
 * @brief   Draw an ellipse outline using Bresenham's algorithm
 *
 * @details Uses midpoint ellipse algorithm for efficient integer-based ellipse drawing.
 *          Draws in all 4 quadrants simultaneously.
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   rx - X radius in pixels
 * @param   ry - Y radius in pixels
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status  PCD8544_DrawEllipse(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t rx, uint8_t ry)
{
    if (rx == 0 || ry == 0) {
        return PCD_ERROR;
    }

    int32_t x = 0;
    int32_t y = ry;
    
    // Pre-compute squared values
    int32_t rx2 = (int32_t)rx * rx;
    int32_t ry2 = (int32_t)ry * ry;
    int32_t tworx2 = 2 * rx2;
    int32_t twory2 = 2 * ry2;
    
    // Decision parameters
    int32_t px = 0;
    int32_t py = tworx2 * y;
    
    // Region 1: slope < 1 (move along X axis)
    int32_t p = ry2 - (rx2 * ry) + (rx2 / 4);
    
    while (px < py) {
        // Draw pixels in all 4 quadrants
        PCD8544_DrawPixelSafe(PCD, (int16_t)x0 + (int16_t)x, (int16_t)y0 + (int16_t)y);
        PCD8544_DrawPixelSafe(PCD, (int16_t)x0 - (int16_t)x, (int16_t)y0 + (int16_t)y);
        PCD8544_DrawPixelSafe(PCD, (int16_t)x0 + (int16_t)x, (int16_t)y0 - (int16_t)y);
        PCD8544_DrawPixelSafe(PCD, (int16_t)x0 - (int16_t)x, (int16_t)y0 - (int16_t)y);
        
        x++;
        px += twory2;
        
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= tworx2;
            p += ry2 + px - py;
        }
    }
    
    // Region 2: slope >= 1 (move along Y axis)
    p = ry2 * (x * x + x) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    
    while (y >= 0) {
        // Draw pixels in all 4 quadrants
        PCD8544_DrawPixelSafe(PCD, (int16_t)x0 + (int16_t)x, (int16_t)y0 + (int16_t)y);
        PCD8544_DrawPixelSafe(PCD, (int16_t)x0 - (int16_t)x, (int16_t)y0 + (int16_t)y);
        PCD8544_DrawPixelSafe(PCD, (int16_t)x0 + (int16_t)x, (int16_t)y0 - (int16_t)y);
        PCD8544_DrawPixelSafe(PCD, (int16_t)x0 - (int16_t)x, (int16_t)y0 - (int16_t)y);
        
        y--;
        py -= tworx2;
        
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += twory2;
            p += rx2 - py + px;
        }
    }
    
    return PCD_OK;
}

PCD_Status PCD8544_DrawCross(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t size)
{
    if (size == 0) {
        return PCD_ERROR;
    }

    int16_t left = (int16_t)x0 - (int16_t)size;
    int16_t right = (int16_t)x0 + (int16_t)size;
    int16_t top = (int16_t)y0 - (int16_t)size;
    int16_t bottom = (int16_t)y0 + (int16_t)size;

    PCD8544_DrawLineSafe(PCD, left, (int16_t)y0, right, (int16_t)y0);
    PCD8544_DrawLineSafe(PCD, (int16_t)x0, top, (int16_t)x0, bottom);
    
    return PCD_OK;
}
/**
 * @brief   Draw a visually circular outline (corrected for pixel aspect ratio)
 *
 * @details Computes Y radius from X radius using yscale/yscale_den ratio,
 *          then calls PCD8544_DrawEllipse. This produces a visually round
 *          circle on displays with non-square pixels.
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   r - radius in pixels (X axis reference)
 * @param   yscale - Y scale numerator
 * @param   yscale_den - Y scale denominator
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status PCD8544_DrawCircle(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t r)
{    
    // Calculate corrected Y radius
    uint8_t ry = (r * PCD8544_Y_SCALE_NUM) / PCD8544_Y_SCALE_DEN;
    if (ry == 0) ry = 1;
    
    return PCD8544_DrawEllipse(PCD, x0, y0, r, ry);
}

/**
 * @brief   Draw a filled ellipse using Bresenham's algorithm
 *
 * @details Draws horizontal scan lines for each Y position using
 *          computed X boundaries from midpoint ellipse algorithm.
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   rx - X radius in pixels
 * @param   ry - Y radius in pixels
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status PCD8544_DrawFillEllipse(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t rx, uint8_t ry)
{
    if (rx == 0 || ry == 0) {
        return PCD_ERROR;
    }

    int32_t x = 0;
    int32_t y = ry;
    
    // Pre-compute squared values
    int32_t rx2 = (int32_t)rx * rx;
    int32_t ry2 = (int32_t)ry * ry;
    int32_t tworx2 = 2 * rx2;
    int32_t twory2 = 2 * ry2;
    
    // Track last drawn Y to avoid overdraw
    int32_t lastY = y + 1;
    
    // Decision parameters
    int32_t px = 0;
    int32_t py = tworx2 * y;
    
    // Region 1: slope < 1
    int32_t p = ry2 - (rx2 * ry) + (rx2 / 4);
    
    while (px < py) {
        if (y != lastY) {
            // Draw horizontal lines for both halves
            PCD8544_DrawLineSafe(PCD, (int16_t)x0 - (int16_t)x, (int16_t)y0 + (int16_t)y, (int16_t)x0 + (int16_t)x, (int16_t)y0 + (int16_t)y);
            PCD8544_DrawLineSafe(PCD, (int16_t)x0 - (int16_t)x, (int16_t)y0 - (int16_t)y, (int16_t)x0 + (int16_t)x, (int16_t)y0 - (int16_t)y);
            lastY = y;
        }
        
        x++;
        px += twory2;
        
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= tworx2;
            p += ry2 + px - py;
        }
    }
    
    // Region 2: slope >= 1
    p = ry2 * (x * x + x) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    
    while (y >= 0) {
        // Draw horizontal lines for both halves (avoid duplicate at y=0)
        if (y != lastY) {
            PCD8544_DrawLineSafe(PCD, (int16_t)x0 - (int16_t)x, (int16_t)y0 + (int16_t)y, (int16_t)x0 + (int16_t)x, (int16_t)y0 + (int16_t)y);
            if (y > 0) {
                PCD8544_DrawLineSafe(PCD, (int16_t)x0 - (int16_t)x, (int16_t)y0 - (int16_t)y, (int16_t)x0 + (int16_t)x, (int16_t)y0 - (int16_t)y);
            }
            lastY = y;
        }
        
        y--;
        py -= tworx2;
        
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += twory2;
            p += rx2 - py + px;
        }
    }
    
    return PCD_OK;
}

/**
 * @brief   Draw a visually filled circle (corrected for pixel aspect ratio)
 *
 * @details Computes Y radius from X radius using yscale/yscale_den ratio,
 *          then calls PCD8544_FillEllipse. This produces a visually round
 *          filled circle on displays with non-square pixels.
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   r - radius in pixels (X axis reference)
 * @param   yscale - Y scale numerator
 * @param   yscale_den - Y scale denominator
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status PCD8544_DrawFillCircle(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t r)
{
    // Calculate corrected Y radius
    uint8_t ry = (r * PCD8544_Y_SCALE_NUM) / PCD8544_Y_SCALE_DEN;
    if (ry == 0) ry = 1;
    
    return PCD8544_DrawFillEllipse(PCD, x0, y0, r, ry);
}

/**
 * @brief   Draw a rectangle outline
 *
 * @details Draws four lines forming a rectangle outline.
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x - top-left X coordinate (0-83)
 * @param   y - top-left Y coordinate (0-47)
 * @param   width - rectangle width in pixels
 * @param   height - rectangle height in pixels
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status PCD8544_DrawRectangle(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    if (width == 0 || height == 0) {
        return PCD_ERROR;
    }

    int16_t x1 = x;
    int16_t y1 = y;
    int16_t x2 = (int16_t)x + (int16_t)width - 1;
    int16_t y2 = (int16_t)y + (int16_t)height - 1;

    PCD8544_DrawLineSafe(PCD, x1, y1, x2, y1);
    PCD8544_DrawLineSafe(PCD, x1, y2, x2, y2);
    PCD8544_DrawLineSafe(PCD, x1, y1, x1, y2);
    PCD8544_DrawLineSafe(PCD, x2, y1, x2, y2);
    
    return PCD_OK;
}

/**
 * @brief   Draw a filled rectangle
 *
 * @details Fills the rectangle area by drawing horizontal lines.
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x - top-left X coordinate (0-83)
 * @param   y - top-left Y coordinate (0-47)
 * @param   width - rectangle width in pixels
 * @param   height - rectangle height in pixels
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status PCD8544_DrawFillRectangle(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    if (width == 0 || height == 0) {
        return PCD_ERROR;
    }

    int16_t x1 = x;
    int16_t x2 = (int16_t)x + (int16_t)width - 1;
    int16_t y1 = y;
    int16_t y2 = (int16_t)y + (int16_t)height - 1;

    for (int16_t row = y1; row <= y2; row++) {
        PCD8544_DrawLineSafe(PCD, x1, row, x2, row);
    }
    
    return PCD_OK;
}

/**
 * @brief   Draw a rounded rectangle outline
 *
 * @details Draws a rectangle with rounded corners using quarter-circle arcs
 *          at each corner connected by straight lines.
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x - top-left X coordinate (0-83)
 * @param   y - top-left Y coordinate (0-47)
 * @param   width - rectangle width in pixels
 * @param   height - rectangle height in pixels
 * @param   r - corner radius in pixels
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status PCD8544_DrawRoundedRect(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t r)
{
    if (width == 0 || height == 0) {
        return PCD_ERROR;
    }
    
    // Limit radius to half of smallest dimension
    if (r > width / 2) r = width / 2;
    if (r > height / 2) r = height / 2;
    
    int16_t x2 = (int16_t)x + (int16_t)width - 1;
    int16_t y2 = (int16_t)y + (int16_t)height - 1;
    
    // Draw four straight edges (excluding corners)
    // Top edge
    if (width > 2 * r) {
        PCD8544_DrawLineSafe(PCD, (int16_t)x + (int16_t)r, y, x2 - (int16_t)r, y);
    }
    // Bottom edge
    if (width > 2 * r) {
        PCD8544_DrawLineSafe(PCD, (int16_t)x + (int16_t)r, y2, x2 - (int16_t)r, y2);
    }
    // Left edge
    if (height > 2 * r) {
        PCD8544_DrawLineSafe(PCD, x, (int16_t)y + (int16_t)r, x, y2 - (int16_t)r);
    }
    // Right edge
    if (height > 2 * r) {
        PCD8544_DrawLineSafe(PCD, x2, (int16_t)y + (int16_t)r, x2, y2 - (int16_t)r);
    }
    
    // Draw four corner arcs using Bresenham circle algorithm
    int16_t cx, cy;
    int16_t px = 0;
    int16_t py = r;
    int16_t d = 3 - 2 * r;
    
    while (px <= py) {
        // Top-left corner
        cx = x + r;
        cy = y + r;
        PCD8544_DrawPixelSafe(PCD, cx - px, cy - py);
        PCD8544_DrawPixelSafe(PCD, cx - py, cy - px);
        
        // Top-right corner
        cx = (int16_t)x2 - (int16_t)r;
        cy = y + r;
        PCD8544_DrawPixelSafe(PCD, cx + px, cy - py);
        PCD8544_DrawPixelSafe(PCD, cx + py, cy - px);
        
        // Bottom-left corner
        cx = x + r;
        cy = (int16_t)y2 - (int16_t)r;
        PCD8544_DrawPixelSafe(PCD, cx - px, cy + py);
        PCD8544_DrawPixelSafe(PCD, cx - py, cy + px);
        
        // Bottom-right corner
        cx = (int16_t)x2 - (int16_t)r;
        cy = (int16_t)y2 - (int16_t)r;
        PCD8544_DrawPixelSafe(PCD, cx + px, cy + py);
        PCD8544_DrawPixelSafe(PCD, cx + py, cy + px);
        
        px++;
        if (d > 0) {
            py--;
            d += 4 * (px - py) + 10;
        } else {
            d += 4 * px + 6;
        }
    }
    
    return PCD_OK;
}

/**
 * @brief   Draw a filled rounded rectangle
 *
 * @details Fills the rounded rectangle by drawing horizontal lines
 *          and filling corner areas with quarter-circle fills.
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x - top-left X coordinate (0-83)
 * @param   y - top-left Y coordinate (0-47)
 * @param   width - rectangle width in pixels
 * @param   height - rectangle height in pixels
 * @param   r - corner radius in pixels
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status PCD8544_DrawFillRoundedRect(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t r)
{
    if (width == 0 || height == 0) {
        return PCD_ERROR;
    }
    
    // Limit radius to half of smallest dimension
    if (r > width / 2) r = width / 2;
    if (r > height / 2) r = height / 2;
    
    int16_t x2 = (int16_t)x + (int16_t)width - 1;
    int16_t y2 = (int16_t)y + (int16_t)height - 1;
    
    // Fill center rectangle (full width, excluding top and bottom rounded areas)
    if (height > 2 * r) {
        for (int16_t row = (int16_t)y + (int16_t)r; row <= y2 - (int16_t)r; row++) {
            PCD8544_DrawLineSafe(PCD, x, row, x2, row);
        }
    }
    
    // Fill top and bottom strips with rounded corners
    int16_t px = 0;
    int16_t py = r;
    int16_t d = 3 - 2 * r;
    int16_t lastPy = r + 1;  // Track to avoid overdraw
    
    while (px <= py) {
        // Fill horizontal spans for rounded corners
        // When py changes, draw spans for all four corners
        if (py != lastPy) {
            // Top area: from y to y + r - 1
            // For y-offset = (r - py), draw from x + r - px to x2 - r + px
            int16_t yTop = (int16_t)y + (int16_t)r - py;
            int16_t yBottom = y2 - (int16_t)r + py;
            
            // Draw horizontal line spanning across top (with corners)
            PCD8544_DrawLineSafe(PCD, (int16_t)x + (int16_t)r - px, yTop, x2 - (int16_t)r + px, yTop);
            // Draw horizontal line spanning across bottom (with corners)
            PCD8544_DrawLineSafe(PCD, (int16_t)x + (int16_t)r - px, yBottom, x2 - (int16_t)r + px, yBottom);
            
            lastPy = py;
        }
        
        // Also handle the symmetric case (swap px and py)
        {
            int16_t yTop = (int16_t)y + (int16_t)r - px;
            int16_t yBottom = y2 - (int16_t)r + px;
            
            PCD8544_DrawLineSafe(PCD, (int16_t)x + (int16_t)r - py, yTop, x2 - (int16_t)r + py, yTop);
            PCD8544_DrawLineSafe(PCD, (int16_t)x + (int16_t)r - py, yBottom, x2 - (int16_t)r + py, yBottom);
        }
        
        px++;
        if (d > 0) {
            py--;
            d += 4 * (px - py) + 10;
        } else {
            d += 4 * px + 6;
        }
    }
    
    return PCD_OK;
}

/**
 * @brief   Draw a measurement chart with data points and labels
 *
 * @details Draws a chart on the PCD8544 display with:
 *          - First row: MAX value (left) and MIN value (right)
 *          - Bottom row: oldest timestamp (left) and newest timestamp (right)
 *          - Data points displayed in the chart area between rows 1-4
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   chartData - pointer to chart data structure containing measurements
 *
 * @return  PCD_Status - PCD_OK on success, error code otherwise
 */
PCD_Status PCD8544_DrawChart(PCD8544_t *PCD, PCD8544_ChartData_t *chartData)
{
    if (PCD == NULL || chartData == NULL) {
        return PCD_ERROR;
    }

    // Get current font dimensions
    uint8_t fontWidth = PCD->font.font_width;
    uint8_t fontHeight = PCD->font.font_height;
    
    // Use defaults if font not set
    if (fontWidth == 0) fontWidth = 6;
    if (fontHeight == 0) fontHeight = 8;

    // Chart layout constants - data points area (rows 1-4, full width)
    const uint8_t CHART_TOP_ROW = 1;      // First row for data (row 0 is for MAX/MIN labels)
    const uint8_t CHART_BOTTOM_ROW = 4;   // Last row for data (row 5 is for timestamps)
    
    // Calculate chart area in pixels using font height
    uint8_t chartStartX = 0;
    uint8_t chartEndX = PCD8544_WIDTH - 1;
    uint8_t chartStartY = CHART_TOP_ROW * fontHeight;     // Row 1 starts after first text row
    uint8_t chartEndY = (CHART_BOTTOM_ROW + 1) * fontHeight - 1;  // Row 4 ends before timestamp row
    
    uint8_t chartWidth = chartEndX - chartStartX;
    uint8_t chartHeight = chartEndY - chartStartY;

    // Find min and max values in data
    int16_t minVal = chartData->dataPoints[0];
    int16_t maxVal = chartData->dataPoints[0];
    
    for (uint8_t i = 1; i < chartData->numPoints; i++) {
        if (chartData->dataPoints[i] < minVal) {
            minVal = chartData->dataPoints[i];
        }
        if (chartData->dataPoints[i] > maxVal) {
            maxVal = chartData->dataPoints[i];
        }
    }

    // Add some margin to min/max for better visualization
    int16_t valueRange = maxVal - minVal;
    if (valueRange == 0) {
        valueRange = 10; // Avoid division by zero
        minVal -= 5;
        maxVal += 5;
    }

    // Display MAX label and value at row 0, left side (position 0,0)
    char labelBuf[12];
    if (chartData->decimalPlaces > 0) {
        int16_t intPart = maxVal / 10;
        int16_t decPart = abs(maxVal % 10);
        snprintf(labelBuf, sizeof(labelBuf), "H:%d.%d", intPart, (int)decPart);
    } else {
        snprintf(labelBuf, sizeof(labelBuf), "H:%d", maxVal);
    }
    PCD8544_SetCursor(PCD, 0, 0);
    PCD8544_WriteString(PCD, labelBuf);

    // Display MIN label and value at row 0, right side
    if (chartData->decimalPlaces > 0) {
        int16_t intPart = minVal / 10;
        int16_t decPart = abs(minVal % 10);
        snprintf(labelBuf, sizeof(labelBuf), "L:%d.%d", intPart, (int)decPart);
    } else {
        snprintf(labelBuf, sizeof(labelBuf), "L:%d", minVal);
    }
    // Calculate position for right alignment using actual font width
    uint8_t labelLen = strlen(labelBuf);
    uint8_t minLabelX = (PCD8544_WIDTH - (labelLen * fontWidth)) / fontWidth;  // Position in characters
    PCD8544_SetCursor(PCD, minLabelX, 0);
    PCD8544_WriteString(PCD, labelBuf);

    // Display time markers on bottom row (row 5)
    if (chartData->numPoints > 0) {
        // Oldest timestamp (first data point) at left side of row 5
        snprintf(labelBuf, sizeof(labelBuf), "%02d:%02d", 
                 chartData->timeStamps[0].hour, 
                 chartData->timeStamps[0].minute);
        PCD8544_SetCursor(PCD, 0, 5);
        PCD8544_WriteString(PCD, labelBuf);

        // Newest timestamp (last data point) at right side of row 5
        if (chartData->numPoints > 1) {
            snprintf(labelBuf, sizeof(labelBuf), "%02d:%02d", 
                     chartData->timeStamps[chartData->numPoints - 1].hour, 
                     chartData->timeStamps[chartData->numPoints - 1].minute);
            // "HH:MM" = 5 chars, position at right edge using font width
            uint8_t timePixels = 5 * fontWidth;
            uint8_t timeX = (PCD8544_WIDTH - timePixels) / fontWidth;       // Position in characters
            PCD8544_SetCursor(PCD, timeX, 5);
            PCD8544_WriteString(PCD, labelBuf);
        }
    }

    // Draw data points based on chart type
    if (chartData->numPoints > 0) {
        uint8_t prevX = 0, prevY = 0;
        uint8_t firstPoint = 1;

        // Calculate bar width for bar chart mode
        uint8_t barWidth = 1;
        if (chartData->chartType == PCD8544_CHART_BAR && chartData->numPoints > 1) {
            barWidth = chartWidth / chartData->numPoints;
            if (barWidth < 1) barWidth = 1;
            if (barWidth > 5) barWidth = 5;  // Max bar width
        }

        for (uint8_t i = 0; i < chartData->numPoints; i++) {
            // Calculate X position for this data point
            uint8_t pointX;
            if (chartData->numPoints == 1) {
                pointX = chartStartX + chartWidth / 2;
            } else {
                pointX = chartStartX + ((uint32_t)i * chartWidth) / (chartData->numPoints - 1);
            }

            // Calculate Y position (invert because Y=0 is at top)
            // Map data value from [minVal, maxVal] to [chartEndY, chartStartY]
            int32_t scaledVal = ((int32_t)(chartData->dataPoints[i] - minVal) * chartHeight) / valueRange;
            uint8_t pointY = chartEndY - (uint8_t)scaledVal;

            // Clamp Y position to chart bounds
            if (pointY < chartStartY) pointY = chartStartY;
            if (pointY > chartEndY) pointY = chartEndY;

            // Keep markers fully inside chart area to avoid overlapping text rows
            uint8_t drawY = pointY;
            uint8_t markerMarginY = 0;
            if (chartData->chartType == PCD8544_CHART_DOT ||
                chartData->chartType == PCD8544_CHART_DOT_LINE) {
                markerMarginY = 2; // Cross/circle size used in dot modes
            }

            if (markerMarginY > 0 && (chartEndY - chartStartY) > (2 * markerMarginY)) {
                uint8_t minMarkerY = chartStartY + markerMarginY;
                uint8_t maxMarkerY = chartEndY - markerMarginY;
                if (drawY < minMarkerY) drawY = minMarkerY;
                if (drawY > maxMarkerY) drawY = maxMarkerY;
            }

            switch (chartData->chartType) {

                case PCD8544_CHART_DOT:
                    // Draw line from previous point to current point
                    //PCD8544_DrawCircle(PCD, pointX, pointY, 3);
                    PCD8544_DrawCross(PCD, pointX, drawY, 2);
                break;

                case PCD8544_CHART_DOT_LINE:
                    // Draw line from previous point to current point
                    PCD8544_DrawCircle(PCD, pointX, drawY, 2);
                     if (!firstPoint){
                        PCD8544_DrawLine(PCD, prevX, prevY, pointX, drawY);
                    }
                break;

                case PCD8544_CHART_BAR:
                    // Draw filled vertical bar from bottom to data point  
                    // BAR CHART: Draw filled vertical bar from bottom to data point
                    for (uint8_t y = pointY; y <= chartEndY; y++) {
                        for (uint8_t bw = 0; bw < barWidth; bw++) {
                            int16_t barX = (int16_t)pointX + (int16_t)bw - ((int16_t)barWidth / 2);
                            PCD8544_DrawPixelSafe(PCD, barX, y);
                        }
                    } 
                break;
            }            
            prevX = pointX;
            prevY = drawY;
            firstPoint = 0;
        }
    }

    return PCD_OK;
}

/**
 * @brief   Initialize a chart data structure with default values
 *
 * @param   chartData - pointer to chart data structure to initialize
 *
 * @return  none
 */
void PCD8544_InitChartData(PCD8544_ChartData_t *chartData)
{
    if (chartData == NULL) return;
    
    chartData->numPoints = 0;
    chartData->decimalPlaces = 1;
    chartData->chartType = PCD8544_CHART_DOT_LINE;  // Default to line chart
    
    for (uint8_t i = 0; i < PCD8544_CHART_MAX_POINTS; i++) {
        chartData->dataPoints[i] = 0;
        chartData->timeStamps[i].hour = 0;
        chartData->timeStamps[i].minute = 0;
    }
}

/**
 * @brief   Add a data point to the chart
 *
 * @details Adds a new data point with timestamp to the chart. If the chart
 *          is full, oldest data points are shifted out.
 *
 * @param   chartData - pointer to chart data structure
 * @param   value - measurement value (can be in tenths, e.g., 253 for 25.3)
 * @param   hour - hour of measurement (0-23)
 * @param   minute - minute of measurement (0-59)
 *
 * @return  none
 */
void PCD8544_AddChartPoint(PCD8544_ChartData_t *chartData, int16_t value, uint8_t hour, uint8_t minute)
{
    if (chartData == NULL) return;
    
    // If full, shift data to make room for new point
    if (chartData->numPoints >= PCD8544_CHART_MAX_POINTS) {
        for (uint8_t i = 0; i < PCD8544_CHART_MAX_POINTS - 1; i++) {
            chartData->dataPoints[i] = chartData->dataPoints[i + 1];
            chartData->timeStamps[i] = chartData->timeStamps[i + 1];
        }
        chartData->numPoints = PCD8544_CHART_MAX_POINTS - 1;
    }
    
    // Add new point at the end
    chartData->dataPoints[chartData->numPoints] = value;
    chartData->timeStamps[chartData->numPoints].hour = hour;
    chartData->timeStamps[chartData->numPoints].minute = minute;
    chartData->numPoints++;
}

/**
 * @brief   Set the chart display type
 *
 * @param   chartData - pointer to chart data structure
 * @param   chartType - chart type (PCD8544_CHART_LINE or PCD8544_CHART_BAR)
 *
 * @return  none
 */
void PCD8544_SetChartType(PCD8544_ChartData_t *chartData, PCD8544_ChartType_t chartType)
{
    if (chartData == NULL) return;
    chartData->chartType = chartType;
}

