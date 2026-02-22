/**
 * @file    demo_tests.c
 * @brief   Functional demo/test routines for menu and chart system
 * @details All simulated‐measurement and chart demo logic lives here,
 *          keeping main.c clean.  Guarded by DEFAULT_DEMO in main.h/main.c.
 */

#include "demo_tests.h"

#include <PCD_LCD/PCD8544.h>
#include <PCD_LCD/PCD8544_fonts.h>
#include <PCD_LCD/PCD8544_Menu.h>
#include <PCD_LCD/PCD8544_Drawing.h>

#include <stdint.h>
#include <stdio.h>

#include "button_debounce.h"
#include "main.h"

/* ---- External references to globals owned by main.c ---- */
extern PCD8544_t        LCD;
extern Menu_Context_t   menuContext;
extern Button_t         encoderSW;
extern char             buffer[64];

/* ---- Chart data structures (owned here, externed in .h) ---- */
PCD8544_ChartData_t temperatureChart;
PCD8544_ChartData_t humidityChart;
PCD8544_ChartData_t pressureChart;

/* ---- Simulated measurement globals (file‐scope) ---- */
static int16_t  g_tempDeciC = 253;   /* 25.3 °C   */
static uint8_t  g_humidity  = 57;    /* 57 %       */
static uint16_t g_pressure  = 1013;  /* 1013 hPa   */
static uint8_t  g_hour      = 8;
static uint8_t  g_minute    = 0;

/* ================================================================
 *  Private helpers
 * ================================================================ */

/**
 * @brief   Initialize chart data for all three measurement types
 */
static void init_all_charts(void)
{
    /* Temperature */
    PCD8544_InitChartData(&temperatureChart);
    temperatureChart.decimalPlaces = 1;
    temperatureChart.chartType     = PCD8544_CHART_DOT;

    /* Humidity */
    PCD8544_InitChartData(&humidityChart);
    humidityChart.decimalPlaces = 1;
    humidityChart.chartType     = PCD8544_CHART_DOT_LINE;

    /* Pressure */
    PCD8544_InitChartData(&pressureChart);
    pressureChart.decimalPlaces = 0;
    pressureChart.chartType     = PCD8544_CHART_BAR;

    /* Pre‐populate with some seed data */
    PCD8544_AddChartPoint(&temperatureChart, 240, 8, 0);
    PCD8544_AddChartPoint(&temperatureChart, 245, 8, 5);
    PCD8544_AddChartPoint(&temperatureChart, 252, 8, 10);
    PCD8544_AddChartPoint(&temperatureChart, 258, 8, 15);
    PCD8544_AddChartPoint(&temperatureChart, 263, 8, 20);

    PCD8544_AddChartPoint(&humidityChart, 550, 8, 0);
    PCD8544_AddChartPoint(&humidityChart, 560, 8, 5);
    PCD8544_AddChartPoint(&humidityChart, 580, 8, 10);
    PCD8544_AddChartPoint(&humidityChart, 570, 8, 15);
    PCD8544_AddChartPoint(&humidityChart, 540, 8, 20);

    PCD8544_AddChartPoint(&pressureChart, 110, 8, 0);
    PCD8544_AddChartPoint(&pressureChart, 112, 8, 5);
    PCD8544_AddChartPoint(&pressureChart, 115, 8, 10);
    PCD8544_AddChartPoint(&pressureChart, 113, 8, 15);
    PCD8544_AddChartPoint(&pressureChart, 118, 8, 20);
}

/* ================================================================
 *  Public demo functions
 * ================================================================ */

void demo_measurement_function(void)
{
    static uint32_t lastUpdate   = 0;
    static uint8_t  initialized  = 0;

    static int16_t  tempDeciC = 253;
    static uint8_t  humidity  = 57;
    static uint16_t pressure  = 1013;

    static int8_t tempStep  = 1;
    static int8_t humStep   = 1;
    static int8_t pressStep = 1;

    static uint8_t hour   = 15;
    static uint8_t minute = 48;
    static uint8_t day    = 1;
    static uint8_t month  = 1;
    static uint8_t year   = 25;

    uint32_t now          = HAL_GetTick();
    uint8_t  advanceValues = initialized;

    if (initialized && (now - lastUpdate) < 700)
        return;

    lastUpdate  = now;
    initialized = 1;

    if (advanceValues)
    {
        tempDeciC += tempStep;
        if (tempDeciC >= 299 || tempDeciC <= 214)
        {
            tempStep = -tempStep;
            tempDeciC += tempStep;
        }

        humidity += humStep;
        if (humidity >= 70 || humidity <= 45)
        {
            humStep = -humStep;
            humidity += humStep;
        }

        pressure += pressStep;
        if (pressure >= 1025 || pressure <= 1002)
        {
            pressStep = -pressStep;
            pressure += pressStep;
        }

        minute++;
        if (minute >= 60)
        {
            minute = 0;
            hour++;
            if (hour >= 24)
            {
                hour = 0;
                day++;
                if (day > 30)
                {
                    day = 1;
                    month++;
                    if (month > 12)
                    {
                        month = 1;
                        year++;
                    }
                }
            }
        }
    }

    /* Draw measurement screen */
    PCD8544_ClearScreen(&LCD);
    PCD8544_SetCursor(&LCD, 0, 0);
    PCD8544_WriteString(&LCD, "DANE POMIAROWE");

    snprintf(buffer, sizeof(buffer), "TEMP: %2d.%1dC", tempDeciC / 10, tempDeciC % 10);
    PCD8544_SetCursor(&LCD, 0, 1);
    PCD8544_WriteString(&LCD, buffer);

    snprintf(buffer, sizeof(buffer), "WILG: %2u%%", humidity);
    PCD8544_SetCursor(&LCD, 0, 2);
    PCD8544_WriteString(&LCD, buffer);

    snprintf(buffer, sizeof(buffer), "CISN: %4uhPa", pressure);
    PCD8544_SetCursor(&LCD, 0, 3);
    PCD8544_WriteString(&LCD, buffer);

    snprintf(buffer, sizeof(buffer), "%02u:%02u %02u.%02u.%02u", hour, minute, day, month, year);
    PCD8544_SetCursor(&LCD, 0, 4);
    PCD8544_WriteString(&LCD, buffer);
    PCD8544_UpdateScreen(&LCD);
}

void demo_chart_function(void)
{
    static uint32_t lastUpdate   = 0;
    static uint8_t  initialized  = 0;

    static int16_t tempDeciC = 253;
    static int8_t  tempStep  = 3;

    static uint8_t hour   = 8;
    static uint8_t minute = 0;

    uint32_t now = HAL_GetTick();

    if (!initialized)
    {
        PCD8544_InitChartData(&temperatureChart);
        temperatureChart.decimalPlaces = 1;
        temperatureChart.chartType     = PCD8544_CHART_DOT_LINE;

        /* Seed data */
        PCD8544_AddChartPoint(&temperatureChart, 240, 8,  0);
        PCD8544_AddChartPoint(&temperatureChart, 245, 8,  5);
        PCD8544_AddChartPoint(&temperatureChart, 252, 8, 10);
        PCD8544_AddChartPoint(&temperatureChart, 258, 8, 15);
        PCD8544_AddChartPoint(&temperatureChart, 263, 8, 20);
        PCD8544_AddChartPoint(&temperatureChart, 268, 8, 25);
        PCD8544_AddChartPoint(&temperatureChart, 270, 8, 30);
        PCD8544_AddChartPoint(&temperatureChart, 267, 8, 35);
        tempDeciC = 267;
        hour      = 8;
        minute    = 40;

        initialized = 1;
        lastUpdate  = now;

        PCD8544_ClearBuffer(&LCD);
        PCD8544_DrawChart(&LCD, &temperatureChart);
        PCD8544_UpdateScreen(&LCD);
        return;
    }

    if ((now - lastUpdate) < 1000)
        return;
    lastUpdate = now;

    tempDeciC += tempStep;
    if (tempDeciC >= 299 || tempDeciC <= 214)
    {
        tempStep = -tempStep;
        tempDeciC += tempStep;
    }

    minute += 5;
    if (minute >= 60)
    {
        minute = 0;
        hour++;
        if (hour >= 24)
            hour = 0;
    }

    PCD8544_AddChartPoint(&temperatureChart, tempDeciC, hour, minute);

    PCD8544_ClearBuffer(&LCD);
    PCD8544_DrawChart(&LCD, &temperatureChart);
    PCD8544_UpdateScreen(&LCD);
}

void simulate_measurements(void)
{
    static uint32_t lastUpdate = 0;
    static int8_t  tempStep  = 3;
    static int8_t  humStep   = 1;
    static int8_t  pressStep = 2;

    uint32_t now = HAL_GetTick();

    if ((now - lastUpdate) < 1000)
        return;
    lastUpdate = now;

    g_tempDeciC += tempStep;
    if (g_tempDeciC >= 299 || g_tempDeciC <= 214)
    {
        tempStep = -tempStep;
        g_tempDeciC += tempStep;
    }

    g_humidity += humStep;
    if (g_humidity >= 75 || g_humidity <= 40)
    {
        humStep = -humStep;
        g_humidity += humStep;
    }

    g_pressure += pressStep;
    if (g_pressure >= 1030 || g_pressure <= 1000)
    {
        pressStep = -pressStep;
        g_pressure += pressStep;
    }

    g_minute += 5;
    if (g_minute >= 60)
    {
        g_minute = 0;
        g_hour++;
        if (g_hour >= 24)
            g_hour = 0;
    }

    PCD8544_AddChartPoint(&temperatureChart, g_tempDeciC, g_hour, g_minute);
    PCD8544_AddChartPoint(&humidityChart, (int16_t)g_humidity * 10, g_hour, g_minute);
    PCD8544_AddChartPoint(&pressureChart, (int16_t)(g_pressure - 900), g_hour, g_minute);
}

void chart_temperature_function(void)
{
    static uint8_t chartsInitialized = 0;

    if (!chartsInitialized)
    {
        init_all_charts();
        chartsInitialized = 1;
    }

    menuContext.state.InChartView   = 1;
    menuContext.state.ChartViewType = CHART_VIEW_TEMPERATURE;

    PCD8544_ClearBuffer(&LCD);
    PCD8544_DrawChart(&LCD, &temperatureChart);
    PCD8544_UpdateScreen(&LCD);
}

void chart_humidity_function(void)
{
    static uint8_t chartsInitialized = 0;

    if (!chartsInitialized)
    {
        init_all_charts();
        chartsInitialized = 1;
    }

    menuContext.state.InChartView   = 1;
    menuContext.state.ChartViewType = CHART_VIEW_HUMIDITY;

    PCD8544_ClearBuffer(&LCD);
    PCD8544_DrawChart(&LCD, &humidityChart);
    PCD8544_UpdateScreen(&LCD);
}

void chart_pressure_function(void)
{
    static uint8_t chartsInitialized = 0;

    if (!chartsInitialized)
    {
        init_all_charts();
        chartsInitialized = 1;
    }

    menuContext.state.InChartView   = 1;
    menuContext.state.ChartViewType = CHART_VIEW_PRESSURE;

    PCD8544_ClearBuffer(&LCD);
    PCD8544_DrawChart(&LCD, &pressureChart);
    PCD8544_UpdateScreen(&LCD);
}

void chart_view_task(void)
{
    static uint32_t lastRedraw = 0;
    uint32_t now = HAL_GetTick();

    /* Button press → exit chart view */
    if (encoderSW.InterruptFlag)
    {
        menuContext.state.InChartView   = 0;
        menuContext.state.ChartViewType = CHART_VIEW_NONE;
        Menu_RefreshDisplay(&LCD, &menuContext);
        return;
    }

    simulate_measurements();

    if ((now - lastRedraw) >= PCD8544_REFRESH_RATE_MS)
    {
        lastRedraw = now;

        PCD8544_ClearBuffer(&LCD);

        switch (menuContext.state.ChartViewType)
        {
        case CHART_VIEW_TEMPERATURE:
            PCD8544_DrawChart(&LCD, &temperatureChart);
            break;
        case CHART_VIEW_HUMIDITY:
            PCD8544_DrawChart(&LCD, &humidityChart);
            break;
        case CHART_VIEW_PRESSURE:
            PCD8544_DrawChart(&LCD, &pressureChart);
            break;
        default:
            break;
        }

        PCD8544_UpdateScreen(&LCD);
    }
}
