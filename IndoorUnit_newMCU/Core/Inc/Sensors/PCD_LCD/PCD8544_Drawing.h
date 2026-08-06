/**
 * @file PCD8544_Drawing.h
 * @brief Advanced drawing primitives and chart API for the PCD8544 display.
 * @details Declares geometry helpers, measurement chart data structures, and
 *          chart rendering used by the indoor weather station UI.
 */

#ifndef INC_PCD8544_DRAWING_H_
#define INC_PCD8544_DRAWING_H_

#include <PCD8544.h>

/* ============================================================================
 * Chart configuration
 * ============================================================================ */

/** @brief Maximum number of data points stored per chart */
#define PCD8544_CHART_MAX_POINTS    20
/** @brief Recommended chart view refresh interval in milliseconds */
#define PCD8544_REFRESH_RATE_MS     500

// Pixel aspect ratio correction for Nokia 5110 (PCD8544)
// Display: 84x48 pixels on ~43x43mm screen
// Pixels are rectangular: Y axis is stretched
// Correction factor: multiply Y radius by 3/4 (0.75) to get circular appearance
// Adjust these values if circles appear elongated: increase for rounder, decrease for flatter
/** @brief Y-axis scale numerator for aspect-ratio correction */
#define PCD8544_Y_SCALE_NUM         3
/** @brief Y-axis scale denominator for aspect-ratio correction */
#define PCD8544_Y_SCALE_DEN         4

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Chart display style enumeration
 */
typedef enum {
    PCD8544_CHART_DOT = 0,      /**< Points only */
    PCD8544_CHART_DOT_LINE,     /**< Points connected by lines */
    PCD8544_CHART_BAR           /**< Vertical bar chart */
} PCD8544_ChartType_t;

/**
 * @brief Time stamp for a chart data point
 */
typedef struct {
    uint8_t hour;       /**< Hour component (0–23) */
    uint8_t minute;     /**< Minute component (0–59) */
} PCD8544_TimeStamp_t;

/**
 * @brief Historical measurement series for chart rendering
 */
typedef struct {
    int16_t dataPoints[PCD8544_CHART_MAX_POINTS];              /**< Sample values (e.g. temp in 0.1 °C units) */
    PCD8544_TimeStamp_t timeStamps[PCD8544_CHART_MAX_POINTS];  /**< Timestamp per sample */
    uint8_t numPoints;                                         /**< Number of valid entries in dataPoints */
    uint8_t decimalPlaces;                                     /**< Decimal places when scaling (1 = divide by 10) */
    PCD8544_ChartType_t chartType;                             /**< Active chart drawing style */
} PCD8544_ChartData_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Draws a line between two pixel coordinates using Bresenham's algorithm.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     x1  Start X coordinate (0–83).
 * @param[in]     y1  Start Y coordinate (0–47).
 * @param[in]     x2  End X coordinate (0–83).
 * @param[in]     y2  End Y coordinate (0–47).
 * @retval PCD_OK           Line drawn successfully.
 * @retval PCD_OutOfBounds  Coordinates outside display area.
 */
PCD_Status PCD8544_DrawLine(PCD8544_t *PCD, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);

/**
 * @brief Draws a cross marker centered at (x0, y0).
 * @param[in,out] PCD  Display driver instance.
 * @param[in]     x0   Center X coordinate (0–83).
 * @param[in]     y0   Center Y coordinate (0–47).
 * @param[in]     size Half-length of each cross arm in pixels.
 * @retval PCD_OK           Cross drawn successfully.
 * @retval PCD_OutOfBounds  Coordinates outside display area.
 */
PCD_Status PCD8544_DrawCross(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t size);

/**
 * @brief Draws an ellipse outline using Bresenham's midpoint algorithm.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     x0  Center X coordinate (0–83).
 * @param[in]     y0  Center Y coordinate (0–47).
 * @param[in]     rx  Horizontal radius in pixels.
 * @param[in]     ry  Vertical radius in pixels.
 * @retval PCD_OK           Ellipse drawn successfully.
 * @retval PCD_OutOfBounds  Bounding box outside display area.
 */
PCD_Status PCD8544_DrawEllipse(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t rx, uint8_t ry);

/**
 * @brief Draws a filled ellipse.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     x0  Center X coordinate (0–83).
 * @param[in]     y0  Center Y coordinate (0–47).
 * @param[in]     rx  Horizontal radius in pixels.
 * @param[in]     ry  Vertical radius in pixels.
 * @retval PCD_OK           Filled ellipse drawn successfully.
 * @retval PCD_OutOfBounds  Bounding box outside display area.
 */
PCD_Status PCD8544_DrawFillEllipse(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t rx, uint8_t ry);

/**
 * @brief Draws a visually circular outline corrected for pixel aspect ratio.
 * @details Y radius is derived from X radius using PCD8544_Y_SCALE_NUM/DEN.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     x0  Center X coordinate (0–83).
 * @param[in]     y0  Center Y coordinate (0–47).
 * @param[in]     r   Reference radius in pixels (X axis).
 * @retval PCD_OK           Circle drawn successfully.
 * @retval PCD_OutOfBounds  Bounding box outside display area.
 */
PCD_Status PCD8544_DrawCircle(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t r);

/**
 * @brief Draws a visually filled circle corrected for pixel aspect ratio.
 * @details Y radius is derived from X radius using PCD8544_Y_SCALE_NUM/DEN.
 * @param[in,out] PCD Display driver instance.
 * @param[in]     x0  Center X coordinate (0–83).
 * @param[in]     y0  Center Y coordinate (0–47).
 * @param[in]     r   Reference radius in pixels (X axis).
 * @retval PCD_OK           Filled circle drawn successfully.
 * @retval PCD_OutOfBounds  Bounding box outside display area.
 */
PCD_Status PCD8544_DrawFillCircle(PCD8544_t *PCD, uint8_t x0, uint8_t y0, uint8_t r);

/**
 * @brief Draws a rectangle outline.
 * @param[in,out] PCD    Display driver instance.
 * @param[in]     x      Top-left X coordinate (0–83).
 * @param[in]     y      Top-left Y coordinate (0–47).
 * @param[in]     width  Rectangle width in pixels.
 * @param[in]     height Rectangle height in pixels.
 * @retval PCD_OK           Rectangle drawn successfully.
 * @retval PCD_OutOfBounds  Bounding box outside display area.
 */
PCD_Status PCD8544_DrawRectangle(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height);

/**
 * @brief Draws a filled rectangle.
 * @param[in,out] PCD    Display driver instance.
 * @param[in]     x      Top-left X coordinate (0–83).
 * @param[in]     y      Top-left Y coordinate (0–47).
 * @param[in]     width  Rectangle width in pixels.
 * @param[in]     height Rectangle height in pixels.
 * @retval PCD_OK           Rectangle filled successfully.
 * @retval PCD_OutOfBounds  Bounding box outside display area.
 */
PCD_Status PCD8544_DrawFillRectangle(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height);

/**
 * @brief Draws a rounded rectangle outline.
 * @param[in,out] PCD    Display driver instance.
 * @param[in]     x      Top-left X coordinate (0–83).
 * @param[in]     y      Top-left Y coordinate (0–47).
 * @param[in]     width  Rectangle width in pixels.
 * @param[in]     height Rectangle height in pixels.
 * @param[in]     r      Corner radius in pixels.
 * @retval PCD_OK           Rounded rectangle drawn successfully.
 * @retval PCD_OutOfBounds  Bounding box outside display area.
 */
PCD_Status PCD8544_DrawRoundedRect(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t r);

/**
 * @brief Draws a filled rounded rectangle.
 * @param[in,out] PCD    Display driver instance.
 * @param[in]     x      Top-left X coordinate (0–83).
 * @param[in]     y      Top-left Y coordinate (0–47).
 * @param[in]     width  Rectangle width in pixels.
 * @param[in]     height Rectangle height in pixels.
 * @param[in]     r      Corner radius in pixels.
 * @retval PCD_OK           Rounded rectangle filled successfully.
 * @retval PCD_OutOfBounds  Bounding box outside display area.
 */
PCD_Status PCD8544_DrawFillRoundedRect(PCD8544_t *PCD, uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t r);

/**
 * @brief Draws a measurement chart with axes, labels, and data series.
 * @param[in,out] PCD       Display driver instance.
 * @param[in]     chartData Chart samples and display options.
 * @retval PCD_OK    Chart rendered to framebuffer successfully.
 * @retval PCD_ERROR NULL pointer or empty/invalid chart data.
 */
PCD_Status PCD8544_DrawChart(PCD8544_t *PCD, PCD8544_ChartData_t *chartData);

/**
 * @brief Initializes a chart data structure with default values.
 * @param[out] chartData Chart structure to reset.
 */
void PCD8544_InitChartData(PCD8544_ChartData_t *chartData);

/**
 * @brief Appends a timestamped sample to the chart ring buffer.
 * @details When full, oldest samples are shifted out.
 * @param[in,out] chartData Chart series to update.
 * @param[in]     value     Sample value (scaled per decimalPlaces).
 * @param[in]     hour      Sample hour (0–23).
 * @param[in]     minute    Sample minute (0–59).
 */
void PCD8544_AddChartPoint(PCD8544_ChartData_t *chartData, int16_t value, uint8_t hour, uint8_t minute);

/**
 * @brief Sets the chart drawing style.
 * @param[in,out] chartData Chart series to update.
 * @param[in]     chartType Display style (dot, line, or bar).
 */
void PCD8544_SetChartType(PCD8544_ChartData_t *chartData, PCD8544_ChartType_t chartType);

#endif /* INC_PCD8544_DRAWING_H_ */
