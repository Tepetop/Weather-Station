#include "weather_station_ui.h"

#include "debug_log.h"

#include <PCD_LCD/PCD8544_fonts.h>

#include <stdio.h>
#include <string.h>

#define WS_UI_DECIMAL_SCALE_BASE 10
#define WS_UI_ROUNDING_OFFSET 0.5f
#define WS_UI_RTC_ROW_DATE 1U
#define WS_UI_RTC_ROW_TIME 2U
#define WS_UI_RTC_ROW_SAVE 3U
#define WS_UI_RTC_ROW_BACK 4U

typedef enum {
  WS_UI_RTC_MODE_SELECT = 0,
  WS_UI_RTC_MODE_DATE_EDIT,
  WS_UI_RTC_MODE_TIME_EDIT
} WS_UI_RtcMode_t;

typedef enum {
  WS_UI_RTC_DATE_FIELD_DAY = 0,
  WS_UI_RTC_DATE_FIELD_MONTH,
  WS_UI_RTC_DATE_FIELD_YEAR,
  WS_UI_RTC_DATE_FIELD_COUNT
} WS_UI_RtcDateField_t;

typedef enum {
  WS_UI_RTC_TIME_FIELD_HOUR = 0,
  WS_UI_RTC_TIME_FIELD_MINUTE,
  WS_UI_RTC_TIME_FIELD_SECOND,
  WS_UI_RTC_TIME_FIELD_COUNT
} WS_UI_RtcTimeField_t;

typedef struct {
  WS_UI_RtcMode_t mode;
  uint8_t cursor_row;
  WS_UI_RtcDateField_t date_field;
  WS_UI_RtcTimeField_t time_field;
  DS3231_DateTime edit_copy;
  uint8_t dirty;
} WS_UI_RtcSetState_t;

/** @brief Global chart data instances */
PCD8544_ChartData_t WS_TemperatureChart;
PCD8544_ChartData_t WS_HumidityChart;
PCD8544_ChartData_t WS_PressureChart;
PCD8544_ChartData_t WS_LuxChart;

/** @brief Global UI context */
WS_UIContext_t WS_UI = {0};
static WS_UI_RtcSetState_t ws_ui_rtc_set = {0};

/**
 * @brief Formats a floating-point value as a fixed-point string
 */
static void ws_ui_format_fixed(char *dst, size_t dst_size, float value, uint8_t decimals) {
  int32_t scale = 1;
  for (uint8_t i = 0U; i < decimals; i++) {
    scale *= WS_UI_DECIMAL_SCALE_BASE;
  }

  float scaled_f = value * (float)scale;
  if (scaled_f >= 0.0f) {
    scaled_f += WS_UI_ROUNDING_OFFSET;
  } else {
    scaled_f -= WS_UI_ROUNDING_OFFSET;
  }

  int32_t scaled = (int32_t)scaled_f;
  int32_t abs_scaled = (scaled < 0) ? -scaled : scaled;
  int32_t int_part = abs_scaled / scale;
  int32_t frac_part = abs_scaled % scale;

  if (decimals == 0U) {
    snprintf(dst, dst_size, "%s%ld", (scaled < 0) ? "-" : "", (long)int_part);
  } else {
    snprintf(dst, dst_size, "%s%ld.%0*ld", (scaled < 0) ? "-" : "", (long)int_part, decimals, (long)frac_part);
  }
}

/**
 * @brief Compute averaged temperature from available sensors
 */
static float ws_avg_temperature(const WS_MeasurementData_t *data) {
  uint8_t si_ok = ((data->sensorStatus & WS_SENSOR_ERR_SI7021) == 0U) ? 1U : 0U;
  uint8_t bmp_ok = ((data->sensorStatus & WS_SENSOR_ERR_BMP280) == 0U) ? 1U : 0U;

  if (si_ok && bmp_ok) {
    return (data->si7021_temp + data->bmp280_temp) * 0.5f;
  }
  if (si_ok) {
    return data->si7021_temp;
  }
  if (bmp_ok) {
    return data->bmp280_temp;
  }
  return 0.0f;
}

/**
 * @brief Convert node state enum to short status string
 */
static const char *ws_node_state_str(WS_NodeStateEnum_t state) {
  switch (state) {
    case WS_NODE_IDLE:
      return "OK";
    case WS_NODE_TX_IN_PROGRESS:
      return "TX";
    case WS_NODE_WAIT_RESPONSE:
      return "WR";
    case WS_NODE_DATA_READY:
      return "DR";
    case WS_NODE_ERROR:
      return "ER";
    default:
      return "??";
  }
}

/**
 * @brief Display list of measurement stations with their status
 */
static void ws_render_stations_status(void) {
  if ((WS_UI.lcd == NULL) || (WS_UI.ws_ctx == NULL) || (WS_UI.text_buffer == NULL)) {
    return;
  }

  PCD8544_ClearBuffer(WS_UI.lcd);
  PCD8544_SetFont(WS_UI.lcd, &Font_6x8);

  PCD_8544_DrawCenteredTitle(WS_UI.lcd, "Status");

  uint8_t row = 1U;
  for (uint8_t i = 0U; (i < WS_UI.ws_ctx->node_count) && (row < 6U); i++) {
    const WS_NodeState_t *node = &WS_UI.ws_ctx->nodes[i];

    const char *status_str = ws_node_state_str(node->state);
    char active_marker = (i == WS_UI.ws_ctx->active_node) ? '*' : '!';

    uint8_t has_data = ((node->data.si7021_temp != 0.0f) ||
                        (node->data.si7021_hum != 0.0f) ||
                        (node->data.bmp280_press != 0.0f)) ? 1U : 0U;

    uint8_t sensor_err = has_data ? node->data.sensorStatus : 0U;

    PCD8544_SetCursor(WS_UI.lcd, 0, row);
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size,
             "%cS%u:%s%s%s",
             active_marker, i + 1U, status_str,
             has_data ? "+" : "-",
             sensor_err ? "!" : " ");
    PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);

    if (sensor_err != 0U) {
      uint8_t err_col = 8U;
      char err_line[16] = " ";

      if ((sensor_err & WS_SENSOR_ERR_SI7021) != 0U) {
        strncat(err_line, "Si7 ", sizeof(err_line) - strlen(err_line) - 1U);
      }

      if ((sensor_err & WS_SENSOR_ERR_BMP280) != 0U) {
        strncat(err_line, "BMP ", sizeof(err_line) - strlen(err_line) - 1U);
      }

      if ((sensor_err & WS_SENSOR_ERR_TSL2561) != 0U) {
        strncat(err_line, "TSL", sizeof(err_line) - strlen(err_line) - 1U);
      }

      PCD8544_SetCursor(WS_UI.lcd, err_col, row);
      PCD8544_WriteString(WS_UI.lcd, err_line);
    }

    row++;
  }

  if (WS_UI.ws_ctx->node_count == 0U) {
    PCD8544_SetCursor(WS_UI.lcd, 0, 1);
    PCD8544_WriteString(WS_UI.lcd, "Brak stacji");
  }

  PCD8544_UpdateScreen(WS_UI.lcd);
}

/**
 * @brief Exit a dedicated view (chart or stations status) back to menu
 */
static void ws_exit_dedicated_view(void) {
  WS_UI.encoder->ButtonIRQ_Flag = 0U;
  WS_UI.menu_ctx->state.actionPending = 0U;
  WS_UI.menu_ctx->state.currentAction = MENU_ACTION_IDLE;
  Menu_RefreshDisplay(WS_UI.lcd, WS_UI.menu_ctx);
  WS_UI.view_state = WS_VIEW_MENU;
}

/**
 * @brief Switch UI into the default live measurements view
 */
static void ws_activate_default_measurement_view(void) {
  WS_UI.menu_ctx->state.InChartView = 0U;
  WS_UI.menu_ctx->state.InStationsStatusView = 0U;
  WS_UI.menu_ctx->state.InDetailsView = 0U;
  WS_UI.menu_ctx->state.InDefaultMeasurementsView = 1U;
  WS_UI.menu_ctx->state.ChartViewType = CHART_VIEW_NONE;
  WS_UI.menu_ctx->state.actionPending = 0U;
  WS_UI.menu_ctx->state.currentAction = MENU_ACTION_IDLE;
  WS_UI.menu_ctx->state.CurrentDepth = 0U;
  WS_UI.menu_ctx->state.MenuIndex = 0U;
  WS_UI.menu_ctx->state.CursorPosOnLCD = 0U;

  if (WS_UI.menu_ctx->defaultMenu != NULL) {
    WS_UI.menu_ctx->rootMenu = WS_UI.menu_ctx->defaultMenu;
  }

  WS_UI.chart_data_dirty = 1U;
  WS_UI.view_state = WS_VIEW_DEFAULT_MEASUREMENT;
}

static uint8_t ws_ui_wrap_u8(uint8_t value, uint8_t min, uint8_t max, int32_t delta) {
  int32_t span = (int32_t)max - (int32_t)min + 1;
  if (span <= 0) {
    return value;
  }

  int32_t normalized = (int32_t)value - (int32_t)min;
  normalized = (normalized + delta) % span;
  if (normalized < 0) {
    normalized += span;
  }

  return (uint8_t)(normalized + min);
}

static void ws_ui_adjust_rtc_date(int32_t delta) {
  switch (ws_ui_rtc_set.date_field) {
    case WS_UI_RTC_DATE_FIELD_DAY:
      ws_ui_rtc_set.edit_copy.date = ws_ui_wrap_u8(ws_ui_rtc_set.edit_copy.date, 1U, 31U, delta);
      break;
    case WS_UI_RTC_DATE_FIELD_MONTH:
      ws_ui_rtc_set.edit_copy.month = ws_ui_wrap_u8(ws_ui_rtc_set.edit_copy.month, 1U, 12U, delta);
      break;
    case WS_UI_RTC_DATE_FIELD_YEAR:
      ws_ui_rtc_set.edit_copy.year = ws_ui_wrap_u8(ws_ui_rtc_set.edit_copy.year, 0U, 99U, delta);
      break;
    default:
      break;
  }
}

static void ws_ui_adjust_rtc_time(int32_t delta) {
  switch (ws_ui_rtc_set.time_field) {
    case WS_UI_RTC_TIME_FIELD_HOUR:
      ws_ui_rtc_set.edit_copy.hours = ws_ui_wrap_u8(ws_ui_rtc_set.edit_copy.hours, 0U, 23U, delta);
      break;
    case WS_UI_RTC_TIME_FIELD_MINUTE:
      ws_ui_rtc_set.edit_copy.minutes = ws_ui_wrap_u8(ws_ui_rtc_set.edit_copy.minutes, 0U, 59U, delta);
      break;
    case WS_UI_RTC_TIME_FIELD_SECOND:
      ws_ui_rtc_set.edit_copy.seconds = ws_ui_wrap_u8(ws_ui_rtc_set.edit_copy.seconds, 0U, 59U, delta);
      break;
    default:
      break;
  }
}

static void ws_render_rtc_set(void) {
  if (WS_UI.lcd == NULL) {
    return;
  }

  char date_line[20];
  char time_line[20];
  uint16_t full_year = (ws_ui_rtc_set.edit_copy.century ? 2100U : 2000U) + (uint16_t)ws_ui_rtc_set.edit_copy.year;

  if (ws_ui_rtc_set.mode == WS_UI_RTC_MODE_DATE_EDIT) {
    switch (ws_ui_rtc_set.date_field) {
      case WS_UI_RTC_DATE_FIELD_DAY:
        snprintf(date_line, sizeof(date_line), "[%02u]-%02u-%04u", ws_ui_rtc_set.edit_copy.date,
                 ws_ui_rtc_set.edit_copy.month, full_year);
        break;
      case WS_UI_RTC_DATE_FIELD_MONTH:
        snprintf(date_line, sizeof(date_line), "%02u-[%02u]-%04u", ws_ui_rtc_set.edit_copy.date,
                 ws_ui_rtc_set.edit_copy.month, full_year);
        break;
      case WS_UI_RTC_DATE_FIELD_YEAR:
        snprintf(date_line, sizeof(date_line), "%02u-%02u-[%04u]", ws_ui_rtc_set.edit_copy.date,
                 ws_ui_rtc_set.edit_copy.month, full_year);
        break;
      default:
        snprintf(date_line, sizeof(date_line), "%02u-%02u-%04u", ws_ui_rtc_set.edit_copy.date,
                 ws_ui_rtc_set.edit_copy.month, full_year);
        break;
    }
  } else {
    snprintf(date_line, sizeof(date_line), "%02u-%02u-%04u", ws_ui_rtc_set.edit_copy.date,
             ws_ui_rtc_set.edit_copy.month, full_year);
  }

  if (ws_ui_rtc_set.mode == WS_UI_RTC_MODE_TIME_EDIT) {
    switch (ws_ui_rtc_set.time_field) {
      case WS_UI_RTC_TIME_FIELD_HOUR:
        snprintf(time_line, sizeof(time_line), "[%02u]:%02u:%02u", ws_ui_rtc_set.edit_copy.hours,
                 ws_ui_rtc_set.edit_copy.minutes, ws_ui_rtc_set.edit_copy.seconds);
        break;
      case WS_UI_RTC_TIME_FIELD_MINUTE:
        snprintf(time_line, sizeof(time_line), "%02u:[%02u]:%02u", ws_ui_rtc_set.edit_copy.hours,
                 ws_ui_rtc_set.edit_copy.minutes, ws_ui_rtc_set.edit_copy.seconds);
        break;
      case WS_UI_RTC_TIME_FIELD_SECOND:
        snprintf(time_line, sizeof(time_line), "%02u:%02u:[%02u]", ws_ui_rtc_set.edit_copy.hours,
                 ws_ui_rtc_set.edit_copy.minutes, ws_ui_rtc_set.edit_copy.seconds);
        break;
      default:
        snprintf(time_line, sizeof(time_line), "%02u:%02u:%02u", ws_ui_rtc_set.edit_copy.hours,
                 ws_ui_rtc_set.edit_copy.minutes, ws_ui_rtc_set.edit_copy.seconds);
        break;
    }
  } else {
    snprintf(time_line, sizeof(time_line), "%02u:%02u:%02u", ws_ui_rtc_set.edit_copy.hours,
             ws_ui_rtc_set.edit_copy.minutes, ws_ui_rtc_set.edit_copy.seconds);
  }

  PCD8544_ClearBuffer(WS_UI.lcd);
  PCD8544_SetFont(WS_UI.lcd, &Font_6x8);
  PCD_8544_DrawCenteredTitle(WS_UI.lcd, "Ustaw RTC");

  for (uint8_t row = WS_UI_RTC_ROW_DATE; row <= WS_UI_RTC_ROW_BACK; row++) {
    PCD8544_SetCursor(WS_UI.lcd, 0, row);
    PCD8544_WriteString(WS_UI.lcd, " ");
  }

  PCD8544_SetCursor(WS_UI.lcd, 1, WS_UI_RTC_ROW_DATE);
  PCD8544_WriteString(WS_UI.lcd, date_line);

  PCD8544_SetCursor(WS_UI.lcd, 1, WS_UI_RTC_ROW_TIME);
  PCD8544_WriteString(WS_UI.lcd, time_line);

  PCD8544_SetCursor(WS_UI.lcd, 1, WS_UI_RTC_ROW_SAVE);
  PCD8544_WriteString(WS_UI.lcd, "Zapisz");

  PCD8544_SetCursor(WS_UI.lcd, 1, WS_UI_RTC_ROW_BACK);
  PCD8544_WriteString(WS_UI.lcd, "Powrot");

  if (ws_ui_rtc_set.mode == WS_UI_RTC_MODE_SELECT) {
    PCD8544_SetCursor(WS_UI.lcd, 0, ws_ui_rtc_set.cursor_row);
    PCD8544_WriteString(WS_UI.lcd, ">");
  }

  PCD8544_UpdateScreen(WS_UI.lcd);
}

void WS_UI_Init(WS_UIContext_t *ui, WS_Manager_t *ws_ctx, WS_RuntimeConfig_t *ws_cfg,
                PCD8544_t *lcd, Menu_Context_t *menu_ctx, Encoder_t *encoder,
                DS3231_DateTime *rtc_now, char *text_buffer, size_t text_buffer_size,
                DS3231_t *rtc_handle) {
  if (ui == NULL) {
    return;
  }

  ui->ws_ctx = ws_ctx;
  ui->ws_cfg = ws_cfg;
  ui->lcd = lcd;
  ui->menu_ctx = menu_ctx;
  ui->encoder = encoder;
  ui->rtc_now = rtc_now;
  ui->rtc_handle = rtc_handle;
  ui->text_buffer = text_buffer;
  ui->text_buffer_size = text_buffer_size;
  ui->view_state = WS_VIEW_DEFAULT_MEASUREMENT;
  ui->last_activity_tick = HAL_GetTick();
}

void WS_UI_InitCharts(void) {
  PCD8544_InitChartData(&WS_TemperatureChart);
  WS_TemperatureChart.decimalPlaces = 1;
  WS_TemperatureChart.chartType = PCD8544_CHART_DOT_LINE;

  PCD8544_InitChartData(&WS_HumidityChart);
  WS_HumidityChart.decimalPlaces = 1;
  WS_HumidityChart.chartType = PCD8544_CHART_DOT_LINE;

  PCD8544_InitChartData(&WS_PressureChart);
  WS_PressureChart.decimalPlaces = 0;
  WS_PressureChart.chartType = PCD8544_CHART_DOT_LINE;

  PCD8544_InitChartData(&WS_LuxChart);
  WS_LuxChart.decimalPlaces = 0;
  WS_LuxChart.chartType = PCD8544_CHART_DOT_LINE;
}

void WS_UI_AddMeasurementToCharts(const WS_MeasurementData_t *data, uint8_t hour, uint8_t minute) {
  if (data == NULL) {
    return;
  }

  float avg_temp = ws_avg_temperature(data);
  int16_t tempVal = (int16_t)(avg_temp * 10.0f);
  int16_t humVal = (int16_t)(data->si7021_hum * 10.0f);
  int16_t pressVal = (int16_t)(data->bmp280_press);
  int16_t luxVal = (int16_t)(data->tsl2561_lux);

  PCD8544_AddChartPoint(&WS_TemperatureChart, tempVal, hour, minute);
  PCD8544_AddChartPoint(&WS_HumidityChart, humVal, hour, minute);
  PCD8544_AddChartPoint(&WS_PressureChart, pressVal, hour, minute);
  PCD8544_AddChartPoint(&WS_LuxChart, luxVal, hour, minute);

  WS_UI.chart_data_dirty = 1U;
}

void WS_UI_MeasurementDisplay(void) {
  if ((WS_UI.lcd == NULL) || (WS_UI.ws_ctx == NULL) || (WS_UI.menu_ctx == NULL)) {
    return;
  }

  if ((WS_UI.view_state != WS_VIEW_DEFAULT_MEASUREMENT) ||
      (WS_UI.menu_ctx->state.InDefaultMeasurementsView == 0U)) {
    WS_UI.chart_data_dirty = 1U;
    WS_UI.menu_ctx->state.InDefaultMeasurementsView = 1U;
    WS_UI.view_state = WS_VIEW_DEFAULT_MEASUREMENT;
    Debug_LogMenuAction("ENTER_DEFAULT_VIEW");
  }

  if (WS_UI.chart_data_dirty == 0U) {
    return;
  }
  WS_UI.chart_data_dirty = 0U;

  WS_MeasurementData_t measurement;
  uint8_t hasMeasurement = WS_GetLatestMeasurement(WS_UI.ws_ctx, &measurement) ? 1U : 0U;
  char value_text[16];

  PCD8544_ClearScreen(WS_UI.lcd);
  PCD8544_SetFont(WS_UI.lcd, &Font_6x8);

  PCD8544_SetCursor(WS_UI.lcd, 0, 0);
  if (WS_UI.rtc_now != NULL) {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "%02u:%02u:%02u",
             WS_UI.rtc_now->hours, WS_UI.rtc_now->minutes, WS_UI.rtc_now->seconds);
  } else {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "--:--:--");
  }
  PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);

  PCD8544_SetCursor(WS_UI.lcd, 0, 1);
  if (hasMeasurement != 0U) {
    float avg_temp = ws_avg_temperature(&measurement);
    ws_ui_format_fixed(value_text, sizeof(value_text), avg_temp, 2U);
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "T:%sC", value_text);
  } else {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "T:--.--C");
  }
  PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);

  PCD8544_SetCursor(WS_UI.lcd, 0, 2);
  if (hasMeasurement != 0U) {
    ws_ui_format_fixed(value_text, sizeof(value_text), measurement.si7021_hum, 2U);
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "H:%s%%", value_text);
  } else {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "H:--.--%%");
  }
  PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);

  PCD8544_SetCursor(WS_UI.lcd, 0, 3);
  if (hasMeasurement != 0U) {
    ws_ui_format_fixed(value_text, sizeof(value_text), measurement.bmp280_press, 2U);
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "P:%shPa", value_text);
  } else {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "P:--.--hPa");
  }
  PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);

  PCD8544_SetCursor(WS_UI.lcd, 0, 4);
  if (hasMeasurement != 0U) {
    ws_ui_format_fixed(value_text, sizeof(value_text), measurement.tsl2561_lux, 2U);
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "L:%slux", value_text);
  } else {
    snprintf(WS_UI.text_buffer, WS_UI.text_buffer_size, "L:--.--lux");
  }
  PCD8544_WriteString(WS_UI.lcd, WS_UI.text_buffer);

  PCD8544_UpdateScreen(WS_UI.lcd);
}

void WS_UI_ChartTemperature(void) {
  if ((WS_UI.menu_ctx == NULL) || (WS_UI.lcd == NULL)) {
    return;
  }

  WS_UI.menu_ctx->state.InChartView = 1U;
  WS_UI.menu_ctx->state.ChartViewType = CHART_VIEW_TEMPERATURE;

  PCD8544_ClearBuffer(WS_UI.lcd);
  PCD8544_DrawChart(WS_UI.lcd, &WS_TemperatureChart);
  PCD8544_UpdateScreen(WS_UI.lcd);
}

void WS_UI_ChartHumidity(void) {
  if ((WS_UI.menu_ctx == NULL) || (WS_UI.lcd == NULL)) {
    return;
  }

  WS_UI.menu_ctx->state.InChartView = 1U;
  WS_UI.menu_ctx->state.ChartViewType = CHART_VIEW_HUMIDITY;

  PCD8544_ClearBuffer(WS_UI.lcd);
  PCD8544_DrawChart(WS_UI.lcd, &WS_HumidityChart);
  PCD8544_UpdateScreen(WS_UI.lcd);
}

void WS_UI_ChartPressure(void) {
  if ((WS_UI.menu_ctx == NULL) || (WS_UI.lcd == NULL)) {
    return;
  }

  WS_UI.menu_ctx->state.InChartView = 1U;
  WS_UI.menu_ctx->state.ChartViewType = CHART_VIEW_PRESSURE;

  PCD8544_ClearBuffer(WS_UI.lcd);
  PCD8544_DrawChart(WS_UI.lcd, &WS_PressureChart);
  PCD8544_UpdateScreen(WS_UI.lcd);
}

void WS_UI_ChartLux(void) {
  if ((WS_UI.menu_ctx == NULL) || (WS_UI.lcd == NULL)) {
    return;
  }

  WS_UI.menu_ctx->state.InChartView = 1U;
  WS_UI.menu_ctx->state.ChartViewType = CHART_VIEW_LUX;

  PCD8544_ClearBuffer(WS_UI.lcd);
  PCD8544_DrawChart(WS_UI.lcd, &WS_LuxChart);
  PCD8544_UpdateScreen(WS_UI.lcd);
}

void WS_UI_ChartViewTask(void) {
  if ((WS_UI.menu_ctx == NULL) || (WS_UI.lcd == NULL)) {
    return;
  }

  if (WS_UI.chart_data_dirty == 0U) {
    return;
  }
  WS_UI.chart_data_dirty = 0U;

  PCD8544_ClearBuffer(WS_UI.lcd);

  switch (WS_UI.menu_ctx->state.ChartViewType) {
    case CHART_VIEW_TEMPERATURE:
      PCD8544_DrawChart(WS_UI.lcd, &WS_TemperatureChart);
      break;
    case CHART_VIEW_HUMIDITY:
      PCD8544_DrawChart(WS_UI.lcd, &WS_HumidityChart);
      break;
    case CHART_VIEW_PRESSURE:
      PCD8544_DrawChart(WS_UI.lcd, &WS_PressureChart);
      break;
    case CHART_VIEW_LUX:
      PCD8544_DrawChart(WS_UI.lcd, &WS_LuxChart);
      break;
    default:
      break;
  }

  PCD8544_UpdateScreen(WS_UI.lcd);
}

static void WS_UI_SetRTCTask(void) {
  if ((WS_UI.encoder == NULL) || (WS_UI.menu_ctx == NULL) || (WS_UI.rtc_handle == NULL)) {
    return;
  }

  if (WS_UI.encoder->IRQ_Flag != 0U) {
    WS_UI.encoder->IRQ_Flag = 0U;
    (void)Encoder_Update(WS_UI.encoder);

    int32_t delta = Encoder_GetPulseCount(WS_UI.encoder);
    Encoder_ResetPulseCount(WS_UI.encoder);

    if (delta != 0) {
      WS_UI.last_activity_tick = HAL_GetTick();

      if (ws_ui_rtc_set.mode == WS_UI_RTC_MODE_SELECT) {
        ws_ui_rtc_set.cursor_row = ws_ui_wrap_u8(ws_ui_rtc_set.cursor_row,
                                                 WS_UI_RTC_ROW_DATE,
                                                 WS_UI_RTC_ROW_BACK,
                                                 delta);
      } else if (ws_ui_rtc_set.mode == WS_UI_RTC_MODE_DATE_EDIT) {
        ws_ui_adjust_rtc_date(delta);
      } else {
        ws_ui_adjust_rtc_time(delta);
      }

      ws_ui_rtc_set.dirty = 1U;
    }
  }

  if (WS_UI.encoder->ButtonIRQ_Flag != 0U) {
    WS_UI.encoder->ButtonIRQ_Flag = 0U;
    WS_UI.last_activity_tick = HAL_GetTick();

    if (ws_ui_rtc_set.mode == WS_UI_RTC_MODE_SELECT) {
      if (ws_ui_rtc_set.cursor_row == WS_UI_RTC_ROW_DATE) {
        ws_ui_rtc_set.mode = WS_UI_RTC_MODE_DATE_EDIT;
        ws_ui_rtc_set.date_field = WS_UI_RTC_DATE_FIELD_DAY;
      } else if (ws_ui_rtc_set.cursor_row == WS_UI_RTC_ROW_TIME) {
        ws_ui_rtc_set.mode = WS_UI_RTC_MODE_TIME_EDIT;
        ws_ui_rtc_set.time_field = WS_UI_RTC_TIME_FIELD_HOUR;
      } else if (ws_ui_rtc_set.cursor_row == WS_UI_RTC_ROW_SAVE) {
        if (DS3231_SetDateTime(WS_UI.rtc_handle, &ws_ui_rtc_set.edit_copy) == DS3231_OK) {
          if (WS_UI.rtc_now != NULL) {
            *WS_UI.rtc_now = ws_ui_rtc_set.edit_copy;
          }
        }
        ws_exit_dedicated_view();
        return;
      } else {
        ws_exit_dedicated_view();
        return;
      }

      ws_ui_rtc_set.dirty = 1U;
    } else if (ws_ui_rtc_set.mode == WS_UI_RTC_MODE_DATE_EDIT) {
      if (ws_ui_rtc_set.date_field < (WS_UI_RTC_DATE_FIELD_COUNT - 1U)) {
        ws_ui_rtc_set.date_field = (WS_UI_RtcDateField_t)(ws_ui_rtc_set.date_field + 1U);
      } else {
        ws_ui_rtc_set.mode = WS_UI_RTC_MODE_SELECT;
      }
      ws_ui_rtc_set.dirty = 1U;
    } else {
      if (ws_ui_rtc_set.time_field < (WS_UI_RTC_TIME_FIELD_COUNT - 1U)) {
        ws_ui_rtc_set.time_field = (WS_UI_RtcTimeField_t)(ws_ui_rtc_set.time_field + 1U);
      } else {
        ws_ui_rtc_set.mode = WS_UI_RTC_MODE_SELECT;
      }
      ws_ui_rtc_set.dirty = 1U;
    }
  }

  if (ws_ui_rtc_set.dirty != 0U) {
    ws_render_rtc_set();
    ws_ui_rtc_set.dirty = 0U;
  }
}

void WS_UI_SetRTC(void) {
  if ((WS_UI.lcd == NULL) || (WS_UI.encoder == NULL) || (WS_UI.menu_ctx == NULL) ||
      (WS_UI.rtc_now == NULL) || (WS_UI.rtc_handle == NULL)) {
    return;
  }

  ws_ui_rtc_set.mode = WS_UI_RTC_MODE_SELECT;
  ws_ui_rtc_set.cursor_row = WS_UI_RTC_ROW_DATE;
  ws_ui_rtc_set.date_field = WS_UI_RTC_DATE_FIELD_DAY;
  ws_ui_rtc_set.time_field = WS_UI_RTC_TIME_FIELD_HOUR;
  ws_ui_rtc_set.edit_copy = *WS_UI.rtc_now;
  ws_ui_rtc_set.dirty = 1U;

  WS_UI.menu_ctx->state.actionPending = 0U;
  WS_UI.menu_ctx->state.currentAction = MENU_ACTION_IDLE;
  WS_UI.encoder->ButtonIRQ_Flag = 0U;
  WS_UI.view_state = WS_VIEW_SET_RTC;

  ws_render_rtc_set();
  ws_ui_rtc_set.dirty = 0U;
}

void WS_UI_TakeMeasurement(void) {
  if (WS_UI.ws_ctx == NULL) {
    return;
  }
  WS_RequestMeasurementForActiveNode(WS_UI.ws_ctx);
}

void WS_UI_StationsStatus(void) {
  if ((WS_UI.menu_ctx == NULL) || (WS_UI.lcd == NULL)) {
    return;
  }

  WS_UI.menu_ctx->state.InStationsStatusView = 1U;
  ws_render_stations_status();
}

void WS_UI_StationsStatusTask(void) {
  if ((WS_UI.menu_ctx == NULL) || (WS_UI.lcd == NULL)) {
    return;
  }

  if (WS_UI.chart_data_dirty == 0U) {
    return;
  }

  ws_render_stations_status();
}

void WS_UI_ViewTask(void) {
  if ((WS_UI.menu_ctx == NULL) || (WS_UI.lcd == NULL) || (WS_UI.encoder == NULL)) {
    return;
  }

  uint32_t now = HAL_GetTick();

  if ((WS_UI.menu_ctx->state.InScreenSaver != 0U) && (WS_UI.encoder->ButtonIRQ_Flag != 0U)) {
    WS_UI.encoder->ButtonIRQ_Flag = 0U;
    WS_UI.last_activity_tick = now;
    WS_UI.menu_ctx->state.InScreenSaver = 0U;
    PCD8544_SetBacklight(WS_UI.lcd);
  }

  if ((WS_UI.menu_ctx->state.InScreenSaver == 0U) &&
      ((now - WS_UI.last_activity_tick) > SCREEN_SAVER_TIMEOUT_MS)) {
    WS_UI.menu_ctx->state.InScreenSaver = 1U;
    ws_activate_default_measurement_view();
    PCD8544_ResetBacklight(WS_UI.lcd);
  }

  switch (WS_UI.view_state) {
    case WS_VIEW_MENU:
      if ((WS_UI.encoder->ButtonIRQ_Flag != 0U) || (WS_UI.encoder->IRQ_Flag != 0U)) {
        WS_UI.last_activity_tick = now;
      }

      Menu_Task(WS_UI.lcd, WS_UI.menu_ctx);
      Encoder_Task(WS_UI.encoder, WS_UI.menu_ctx);

      if (WS_UI.menu_ctx->state.InChartView != 0U) {
        WS_UI.view_state = WS_VIEW_CHART;
      } else if (WS_UI.menu_ctx->state.InStationsStatusView != 0U) {
        WS_UI.view_state = WS_VIEW_STATIONS_STATUS;
      } else if (WS_UI.menu_ctx->state.InDefaultMeasurementsView != 0U) {
        WS_UI.view_state = WS_VIEW_DEFAULT_MEASUREMENT;
      }
      break;

    case WS_VIEW_CHART:
      WS_UI_ChartViewTask();

      if (WS_UI.encoder->ButtonIRQ_Flag != 0U) {
        WS_UI.last_activity_tick = now;
        WS_UI.menu_ctx->state.InChartView = 0U;
        WS_UI.menu_ctx->state.ChartViewType = CHART_VIEW_NONE;
        ws_exit_dedicated_view();
      }
      break;

    case WS_VIEW_STATIONS_STATUS:
      WS_UI_StationsStatusTask();

      if (WS_UI.encoder->ButtonIRQ_Flag != 0U) {
        WS_UI.last_activity_tick = now;
        WS_UI.menu_ctx->state.InStationsStatusView = 0U;
        ws_exit_dedicated_view();
      }
      break;

    case WS_VIEW_SET_RTC:
      WS_UI_SetRTCTask();
      break;

    case WS_VIEW_DEFAULT_MEASUREMENT:
      if ((WS_UI.encoder->ButtonIRQ_Flag != 0U) || (WS_UI.encoder->IRQ_Flag != 0U)) {
        WS_UI.last_activity_tick = now;
      }

      Menu_Task(WS_UI.lcd, WS_UI.menu_ctx);
      Encoder_Task(WS_UI.encoder, WS_UI.menu_ctx);

      if (WS_UI.menu_ctx->state.InChartView != 0U) {
        WS_UI.view_state = WS_VIEW_CHART;
      } else if (WS_UI.menu_ctx->state.InStationsStatusView != 0U) {
        WS_UI.view_state = WS_VIEW_STATIONS_STATUS;
      } else if (WS_UI.menu_ctx->state.InDefaultMeasurementsView == 0U) {
        WS_UI.view_state = WS_VIEW_MENU;
      } else {
        WS_UI_MeasurementDisplay();
      }
      break;

    default:
      WS_UI.view_state = WS_VIEW_MENU;
      break;
  }
}
