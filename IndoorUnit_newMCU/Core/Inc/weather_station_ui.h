#ifndef WEATHER_STATION_UI_H
#define WEATHER_STATION_UI_H

/**
 * @file weather_station_ui.h
 * @brief User interface API for the indoor weather station.
 * @details Declares UI context, view state machine, chart instances, and
 *          rendering/task entry points used by the application loop.
 */

#include <stddef.h>
#include <stdint.h>

#include <PCD_LCD/PCD8544_Drawing.h>
#include <PCD_LCD/PCD8544_Menu.h>
#include <encoder.h>

#include "weather_station.h"

/**
 * @brief View state machine enumeration
 * @details Tracks the active view/screen of the weather station UI
 */
typedef enum {
  WS_VIEW_MENU = 0,               /**< Normal menu navigation */
  WS_VIEW_CHART,                  /**< Chart view active */
  WS_VIEW_STATIONS_STATUS,        /**< Stations status view active */
  WS_VIEW_CENTRAL_STATUS,         /**< Central station status view active */
  WS_VIEW_DEFAULT_MEASUREMENT,    /**< Default measurement display active */
  WS_VIEW_SCREEN_SAVER,           /**< Screen saver view active */
  WS_VIEW_SET_RTC                 /**< Manual RTC edit view active */
} WS_ViewState_t;

/**
 * @brief UI runtime context containing references to all required components
 * @details Must be initialized before calling any WS_UI_* functions
 */
typedef struct {
  WS_Manager_t *ws_ctx;            /**< Weather station manager context */
  WS_RuntimeConfig_t *ws_cfg;      /**< Runtime configuration */
  PCD8544_t *lcd;                  /**< LCD display handle */
  Menu_Context_t *menu_ctx;        /**< Menu context */
  Encoder_t *encoder;              /**< Encoder handle for button input */
  DS3231_DateTime *rtc_now;        /**< Current RTC time */
  DS3231_t *rtc_handle;            /**< RTC device handle for write operations */
  char *text_buffer;               /**< Scratch buffer for text formatting */
  size_t text_buffer_size;         /**< Size of text buffer */
  volatile uint8_t chart_data_dirty; /**< Flag: new chart data available, redraw needed */
  WS_ViewState_t view_state;       /**< Current view state machine state */
  uint32_t last_activity_tick;     /**< Timestamp of last button press for screen saver */
} WS_UIContext_t;

/** @brief Chart instances for all measurement types */
extern PCD8544_ChartData_t WS_TemperatureChart;
extern PCD8544_ChartData_t WS_HumidityChart;
extern PCD8544_ChartData_t WS_PressureChart;
extern PCD8544_ChartData_t WS_LuxChart;

/** @brief Global UI context */
extern WS_UIContext_t WS_UI;

/**
 * @brief Initializes the UI context with all required runtime dependencies.
 * @param[out] ui UI context instance to initialize.
 * @param[in,out] ws_ctx Weather station manager context.
 * @param[in,out] ws_cfg Runtime configuration used by UI helper views.
 * @param[in,out] lcd Display handle.
 * @param[in,out] menu_ctx Menu state/context handle.
 * @param[in,out] encoder Encoder and button input handle.
 * @param[in,out] rtc_now Pointer to current RTC time snapshot.
 * @param[in,out] text_buffer Scratch text buffer used by UI rendering.
 * @param[in] text_buffer_size Size of text_buffer in bytes.
 * @param[in,out] rtc_handle DS3231 handle used to apply RTC edits.
 */
void WS_UI_Init(WS_UIContext_t *ui, WS_Manager_t *ws_ctx, WS_RuntimeConfig_t *ws_cfg,
                PCD8544_t *lcd, Menu_Context_t *menu_ctx, Encoder_t *encoder,
                DS3231_DateTime *rtc_now, char *text_buffer, size_t text_buffer_size,
                DS3231_t *rtc_handle);

/**
 * @brief Initializes chart data containers for all measurement channels.
 */
void WS_UI_InitCharts(void);

/**
 * @brief Appends a measurement sample to all historical UI charts.
 * @param[in] data Measurement payload to convert and enqueue.
 * @param[in] hour Hour associated with the sample timestamp.
 * @param[in] minute Minute associated with the sample timestamp.
 */
void WS_UI_AddMeasurementToCharts(const WS_NodeReadings_t *data, uint8_t hour, uint8_t minute);

/**
 * @brief Renders the default live measurement screen.
 */
void WS_UI_MeasurementDisplay(void);

/**
 * @brief Enters temperature chart view and renders the chart once.
 */
void WS_UI_ChartTemperature(void);

/**
 * @brief Enters humidity chart view and renders the chart once.
 */
void WS_UI_ChartHumidity(void);

/**
 * @brief Enters pressure chart view and renders the chart once.
 */
void WS_UI_ChartPressure(void);

/**
 * @brief Enters light intensity chart view and renders the chart once.
 */
void WS_UI_ChartLux(void);

/**
 * @brief Refreshes the active chart view when new chart data is available.
 */
void WS_UI_ChartViewTask(void);

/**
 * @brief Requests an immediate measurement from the currently active node.
 */
void WS_UI_TakeMeasurement(void);

/**
 * @brief Enters RTC setting view and prepares editable RTC copy.
 */
void WS_UI_SetRTC(void);

/**
 * @brief Enters stations status view and renders current node states.
 */
void WS_UI_StationsStatus(void);

/**
 * @brief Periodic task for stations status view refresh.
 */
void WS_UI_StationsStatusTask(void);

/**
 * @brief Enters central unit status view and renders runtime diagnostics.
 */
void WS_UI_CentralStatus(void);

/**
 * @brief Periodic task for central unit status screen refresh.
 */
void WS_UI_CentralStatusTask(void);

/**
 * @brief Main UI state machine task executed in the application loop.
 */
void WS_UI_ViewTask(void);

#endif
