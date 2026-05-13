#ifndef WEATHER_STATION_UI_H
#define WEATHER_STATION_UI_H

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

void WS_UI_Init(WS_UIContext_t *ui, WS_Manager_t *ws_ctx, WS_RuntimeConfig_t *ws_cfg,
                PCD8544_t *lcd, Menu_Context_t *menu_ctx, Encoder_t *encoder,
                DS3231_DateTime *rtc_now, char *text_buffer, size_t text_buffer_size,
                DS3231_t *rtc_handle);
void WS_UI_InitCharts(void);
void WS_UI_AddMeasurementToCharts(const WS_MeasurementData_t *data, uint8_t hour, uint8_t minute);
void WS_UI_MeasurementDisplay(void);
void WS_UI_ChartTemperature(void);
void WS_UI_ChartHumidity(void);
void WS_UI_ChartPressure(void);
void WS_UI_ChartLux(void);
void WS_UI_ChartViewTask(void);
void WS_UI_TakeMeasurement(void);
void WS_UI_SetRTC(void);
void WS_UI_StationsStatus(void);
void WS_UI_StationsStatusTask(void);
void WS_UI_ViewTask(void);

#endif
