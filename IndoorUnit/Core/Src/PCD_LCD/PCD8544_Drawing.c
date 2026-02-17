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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

            if (chartData->chartType == PCD8544_CHART_BAR) {
                // BAR CHART: Draw filled vertical bar from bottom to data point
                for (uint8_t y = pointY; y <= chartEndY; y++) {
                    for (uint8_t bw = 0; bw < barWidth; bw++) {
                        uint8_t barX = pointX + bw - barWidth / 2;
                        if (barX < PCD8544_WIDTH) {
                            PCD8544_DrawPixel(PCD, barX, y);
                        }
                    }
                }
            } else {
                // LINE/DOT CHART: Draw the data point (as a small cross or dot)
                PCD8544_DrawPixel(PCD, pointX, pointY);
                // Make point more visible with surrounding pixels
                if (pointX > 0) PCD8544_DrawPixel(PCD, pointX - 1, pointY);
                if (pointX < PCD8544_WIDTH - 1) PCD8544_DrawPixel(PCD, pointX + 1, pointY);
                if (pointY > chartStartY) PCD8544_DrawPixel(PCD, pointX, pointY - 1);
                if (pointY < chartEndY) PCD8544_DrawPixel(PCD, pointX, pointY + 1);

                // // Connect points with a line
                // if (!firstPoint && chartData->connectPoints) {
                //     PCD8544_DrawLine(PCD, prevX, prevY, pointX, pointY);
                // }
            }

            prevX = pointX;
            prevY = pointY;
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
    chartData->connectPoints = 1;
    chartData->chartType = PCD8544_CHART_LINE;  // Default to line chart
    
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

/**
 * @brief   Toggle chart type between LINE and BAR
 *
 * @param   chartData - pointer to chart data structure
 *
 * @return  none
 */
void PCD8544_ToggleChartType(PCD8544_ChartData_t *chartData)
{
    if (chartData == NULL) return;
    
    if (chartData->chartType == PCD8544_CHART_LINE) {
        chartData->chartType = PCD8544_CHART_BAR;
    } else {
        chartData->chartType = PCD8544_CHART_LINE;
    }
}
