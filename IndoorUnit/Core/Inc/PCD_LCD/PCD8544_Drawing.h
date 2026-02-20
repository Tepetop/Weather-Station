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
 * @brief Maximum number of data points in a chart
 */
#define PCD8544_CHART_MAX_POINTS    20
#define PCD8544_REFRESH_RATE_MS  500  // Refresh rate for chart updates in milliseconds

// Pixel aspect ratio correction for Nokia 5110 (PCD8544)
// Display: 84x48 pixels on ~43x43mm screen
// Pixels are rectangular: Y axis is stretched
// Correction factor: multiply Y radius by 3/4 (0.75) to get circular appearance
// Adjust these values if circles appear elongated: increase for rounder, decrease for flatter
#define PCD8544_Y_SCALE_NUM     3
#define PCD8544_Y_SCALE_DEN     4

/**
 * @brief Chart type enumeration
 */
typedef enum {
    PCD8544_CHART_DOT = 0,      // Dot chart (points only)
    PCD8544_CHART_DOT_LINE,     // Dot chart with lines connecting points
    PCD8544_CHART_BAR           // Bar chart (filled vertical bars)
} PCD8544_ChartType_t;

/**
 * @brief Time stamp structure for chart data points
 */
typedef struct {
    uint8_t hour;       // Hour (0-23)
    uint8_t minute;     // Minute (0-59)
} PCD8544_TimeStamp_t;

/**
 * @brief Chart data structure for measurement visualization
 */
typedef struct {
    int16_t dataPoints[PCD8544_CHART_MAX_POINTS];       // Array of data values (e.g., temp in 0.1C units)
    PCD8544_TimeStamp_t timeStamps[PCD8544_CHART_MAX_POINTS];  // Time stamps for each data point
    uint8_t numPoints;                                   // Number of valid data points
    uint8_t decimalPlaces;                               // Number of decimal places (1 = value/10)
    PCD8544_ChartType_t chartType;                       // Chart display type (LINE or BAR)
} PCD8544_ChartData_t;

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

PCD_Status PCD8544_DrawCross(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t size);
/**
 * @brief   Draw an ellipse outline using Bresenham's algorithm
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   rx - X radius in pixels
 * @param   ry - Y radius in pixels
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_DrawEllipse(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t rx, uint8_t ry);

/**
 * @brief   Draw a filled ellipse using Bresenham's algorithm
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   rx - X radius in pixels
 * @param   ry - Y radius in pixels
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_DrawFillEllipse(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t rx, uint8_t ry);

/**
 * @brief   Draw a visually circular outline (corrected for pixel aspect ratio)
 *
 * @details Uses yscale/yscale_den to compute Y radius from X radius.
 *          For Nokia 5110: use yscale=3, yscale_den=4 (or values from PCD8544_Y_SCALE_*)
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   r - radius in pixels (X axis reference)
 * @param   yscale - Y scale numerator (e.g., 3)
 * @param   yscale_den - Y scale denominator (e.g., 4)
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_DrawCircle(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t r);

/**
 * @brief   Draw a visually filled circle (corrected for pixel aspect ratio)
 *
 * @details Uses yscale/yscale_den to compute Y radius from X radius.
 *          For Nokia 5110: use yscale=3, yscale_den=4 (or values from PCD8544_Y_SCALE_*)
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x0 - center X coordinate (0-83)
 * @param   y0 - center Y coordinate (0-47)
 * @param   r - radius in pixels (X axis reference)
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_DrawFillCircle(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t r);

/**
 * @brief   Draw a rectangle outline
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x - top-left X coordinate (0-83)
 * @param   y - top-left Y coordinate (0-47)
 * @param   width - rectangle width in pixels
 * @param   height - rectangle height in pixels
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_DrawRectangle(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height);

/**
 * @brief   Draw a filled rectangle
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x - top-left X coordinate (0-83)
 * @param   y - top-left Y coordinate (0-47)
 * @param   width - rectangle width in pixels
 * @param   height - rectangle height in pixels
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_DrawFillRectangle(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height);

/**
 * @brief   Draw a rounded rectangle outline
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x - top-left X coordinate (0-83)
 * @param   y - top-left Y coordinate (0-47)
 * @param   width - rectangle width in pixels
 * @param   height - rectangle height in pixels
 * @param   r - corner radius in pixels
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_DrawRoundedRect(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t r);

/**
 * @brief   Draw a filled rounded rectangle
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   x - top-left X coordinate (0-83)
 * @param   y - top-left Y coordinate (0-47)
 * @param   width - rectangle width in pixels
 * @param   height - rectangle height in pixels
 * @param   r - corner radius in pixels
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_DrawFillRoundedRect(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t r);

/**
 * @brief   Draw a measurement chart with axes and data points
 *
 * @details Draws a chart on the display with:
 *          - X-axis at the bottom with time markers
 *          - Y-axis on the left with min/max value labels
 *          - Data points displayed as dots or connected lines
 *
 * @param   PCD - pointer to PCD8544 structure
 * @param   chartData - pointer to chart data structure
 *
 * @return  PCD_Status - operation status
 */
PCD_Status PCD8544_DrawChart(PCD8544_t *PCD, PCD8544_ChartData_t *chartData);

/**
 * @brief   Initialize a chart data structure with default values
 *
 * @param   chartData - pointer to chart data structure to initialize
 */
void PCD8544_InitChartData(PCD8544_ChartData_t *chartData);

/**
 * @brief   Add a data point to the chart
 *
 * @details If the chart is full, oldest data points are shifted out.
 *
 * @param   chartData - pointer to chart data structure
 * @param   value - measurement value (e.g., 253 for 25.3 with decimalPlaces=1)
 * @param   hour - hour of measurement (0-23)
 * @param   minute - minute of measurement (0-59)
 */
void PCD8544_AddChartPoint(PCD8544_ChartData_t *chartData, int16_t value, uint8_t hour, uint8_t minute);

/**
 * @brief   Set the chart display type
 *
 * @param   chartData - pointer to chart data structure
 * @param   chartType - chart type (PCD8544_CHART_LINE or PCD8544_CHART_BAR)
 */
void PCD8544_SetChartType(PCD8544_ChartData_t *chartData, PCD8544_ChartType_t chartType);

#endif /* INC_PCD8544_DRAWING_H_ */
