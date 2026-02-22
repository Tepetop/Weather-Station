/**
 * @file    demo_tests.h
 * @brief   Functional demo/test routines for menu and chart system
 * @details Contains simulated measurement display, chart demonstrations,
 *          and chart view task used during development and testing.
 *          Enabled via DEFAULT_DEMO define in main.c.
 */

#ifndef INC_DEMO_TESTS_H_
#define INC_DEMO_TESTS_H_

#include <PCD_LCD/PCD8544.h>
#include <PCD_LCD/PCD8544_Menu.h>
#include <PCD_LCD/PCD8544_Drawing.h>

/* ---------- Chart data (owned by demo_tests.c) ---------- */
extern PCD8544_ChartData_t temperatureChart;
extern PCD8544_ChartData_t humidityChart;
extern PCD8544_ChartData_t pressureChart;

/* ---------- Public demo function prototypes ---------- */

/**
 * @brief   Simulated live measurement display (temp, humidity, pressure, time)
 * @details Periodically updates the LCD with bouncing demo values.
 *          Used as the default‐page menu function.
 */
void demo_measurement_function(void);

/**
 * @brief   Standalone temperature chart demo (pre-populated data)
 * @details Adds a new point every second and redraws the chart.
 */
void demo_chart_function(void);

/**
 * @brief   Enter temperature chart view from menu
 */
void chart_temperature_function(void);

/**
 * @brief   Enter humidity chart view from menu
 */
void chart_humidity_function(void);

/**
 * @brief   Enter pressure chart view from menu
 */
void chart_pressure_function(void);

/**
 * @brief   Chart view main‐loop task (update data, redraw, handle exit)
 * @details Called in the while(1) loop when menuContext.state.InChartView == 1
 */
void chart_view_task(void);

/**
 * @brief   Periodically update simulated measurement values and feed charts
 */
void simulate_measurements(void);

#endif /* INC_DEMO_TESTS_H_ */
