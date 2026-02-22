/**
 * @file    ds3231_example.c
 * @brief   Przykłady użycia biblioteki DS3231 na STM32F103C8T6 (HAL)
 *
 * Schemat podłączenia:
 *   DS3231 SCL  → PB6  (I2C1_SCL, wymaga rezystora pullup ~4.7kΩ do 3.3V)
 *   DS3231 SDA  → PB7  (I2C1_SDA, wymaga rezystora pullup ~4.7kΩ do 3.3V)
 *   DS3231 INT  → PA0  (opcjonalnie, do obsługi przerwań alarmów)
 *   DS3231 VCC  → 3.3V
 *   DS3231 GND  → GND
 *   DS3231 VBAT → bateria CR2032 (3V)
 *
 * Konfiguracja I2C w CubeMX:
 *   - I2C1, Standard Mode, 100 kHz (lub Fast Mode 400 kHz)
 *   - No stretched clock
 *   - NVIC: włącz EXTIx_IRQn dla pinu INT jeśli używasz przerwań alarmów
 */

#include "main.h"
#include "ds3231_clod.h"
#include <stdio.h>   /* printf – wymaga przekierowania na UART (np. przez semihosting) */

/* -------------------------------------------------------------------------
 * Zmienne globalne
 * ------------------------------------------------------------------------- */
extern I2C_HandleTypeDef hi2c1;   /* zdefiniowany przez CubeMX */

DS3231_Handle rtc;                 /* Instancja modułu RTC */

/* Flagi ustawiane w handlerze EXTI (przerwanie od INT/SQW) */
volatile bool g_alarm1_fired = false;
volatile bool g_alarm2_fired = false;

/* =========================================================================
 * PRZYKŁAD 1 – Inicjalizacja i ustawienie czasu
 * ========================================================================= */
void Example_Init(void)
{
    DS3231_Status ret;

    /* Inicjalizacja – tryb 24h, standardowy adres I2C */
    ret = DS3231_clod_Init(&rtc, &hi2c1, DS3231_I2C_ADDR, DS3231_FORMAT_24H);
    if (ret != DS3231_OK) {
        /* Obsługa błędu – np. miganie LED, zapis do logu */
        Error_Handler();
    }

    /* Sprawdź flagę OSF – czy oscylator był zatrzymany (np. rozładowana bateria) */
    bool osf;
    DS3231_clod_GetOscillatorStopFlag(&rtc, &osf);
    if (osf) {
        /* Czas jest nieważny – ustaw czas ręcznie */
        DS3231_DateTime dt = {
            .seconds = 0,
            .minutes = 30,
            .hours   = 14,
            .format  = DS3231_FORMAT_24H,
            .day     = 6,          /* 6 = Sobota (definicja użytkownika: 1=Pn ... 7=Nd) */
            .date    = 21,
            .month   = 2,
            .year    = 25,         /* 2025 */
            .century = false
        };

        ret = DS3231_clod_SetDateTime(&rtc, &dt);
        if (ret != DS3231_OK) Error_Handler();

        DS3231_clod_ClearOscillatorStopFlag(&rtc);
    }
}

/* =========================================================================
 * PRZYKŁAD 2 – Odczyt czasu i daty
 * ========================================================================= */
void Example_ReadTime(void)
{
    DS3231_DateTime dt;

    if (DS3231_clod_GetDateTime(&rtc, &dt) == DS3231_OK) {
        printf("Data: %02d/%02d/20%02d  Czas: %02d:%02d:%02d\r\n",
               dt.date, dt.month, dt.year,
               dt.hours, dt.minutes, dt.seconds);
    }
}

/* =========================================================================
 * PRZYKŁAD 3 – Odczyt temperatury
 * ========================================================================= */
void Example_ReadTemperature(void)
{
    float temp;

    if (DS3231_clod_GetTemperature(&rtc, &temp) == DS3231_OK) {
        /* Wypisz z rozdzielczością 0.25°C */
        printf("Temperatura: %.2f C\r\n", temp);
    }

    /* Wymuś natychmiastową konwersję (normalnie co 64s) */
    DS3231_Status ret = DS3231_clod_ForceTemperatureConversion(&rtc);
    if (ret == DS3231_OK) {
        DS3231_clod_GetTemperature(&rtc, &temp);
        printf("Temperatura po konwersji: %.2f C\r\n", temp);
    } else if (ret == DS3231_ERR_BUSY) {
        printf("RTC zajety konwersja!\r\n");
    }
}

/* =========================================================================
 * PRZYKŁAD 4 – Alarm 1: przerwanie o konkretnej godzinie
 *
 * Cel: wygenerowanie impulsu na INT/SQW o 07:00:00
 * ========================================================================= */
void Example_Alarm1_AtTime(void)
{
    DS3231_Alarm1 alm1 = {
        .seconds  = 0,
        .minutes  = 0,
        .hours    = 7,
        .format   = DS3231_FORMAT_24H,
        .mode     = DS3231_ALM1_MATCH_HR_MIN_SEC  /* dopasuj godz + min + sek */
    };

    DS3231_clod_SetAlarm1(&rtc, &alm1);
    DS3231_clod_EnableAlarm1Interrupt(&rtc);

    printf("Alarm 1 ustawiony na 07:00:00\r\n");
}

/* =========================================================================
 * PRZYKŁAD 5 – Alarm 2: przerwanie co minutę
 * ========================================================================= */
void Example_Alarm2_EveryMinute(void)
{
    DS3231_Alarm2 alm2 = {
        .mode = DS3231_ALM2_EVERY_MINUTE
    };

    DS3231_clod_SetAlarm2(&rtc, &alm2);
    DS3231_clod_EnableAlarm2Interrupt(&rtc);

    printf("Alarm 2: co minute\r\n");
}

/* =========================================================================
 * PRZYKŁAD 6 – Oba alarmy jednocześnie
 *
 * Alarm 1: co sekundę (tryb DS3231_ALM1_EVERY_SECOND)
 * Alarm 2: o 12:30 każdego dnia
 * ========================================================================= */
void Example_BothAlarms(void)
{
    /* Alarm 1 – co sekundę */
    DS3231_Alarm1 alm1 = {
        .mode = DS3231_ALM1_EVERY_SECOND
    };
    DS3231_clod_SetAlarm1(&rtc, &alm1);
    DS3231_clod_EnableAlarm1Interrupt(&rtc);

    /* Alarm 2 – o 12:30 */
    DS3231_Alarm2 alm2 = {
        .minutes = 30,
        .hours   = 12,
        .format  = DS3231_FORMAT_24H,
        .mode    = DS3231_ALM2_MATCH_HR_MIN
    };
    DS3231_clod_SetAlarm2(&rtc, &alm2);
    DS3231_clod_EnableAlarm2Interrupt(&rtc);
}

/* =========================================================================
 * PRZYKŁAD 7 – Wyjście fali prostokątnej 1 Hz na pinie INT/SQW
 *
 * Uwaga: Wyjście SQW i przerwania alarmów są wzajemnie wykluczające się
 *        (sterowane bitem INTCN). Gdy SQW jest włączone, alarmy nie
 *        generują impulsów na pinie INT/SQW (choć flagi A1F/A2F są
 *        nadal ustawiane).
 * ========================================================================= */
void Example_SquareWave(void)
{
    /* Włącz SQW 1 Hz */
    DS3231_clod_EnableSQW(&rtc, DS3231_SQW_1HZ);
    printf("SQW 1Hz wlaczone\r\n");

    HAL_Delay(5000);

    /* Zmień na 4.096 kHz */
    DS3231_clod_EnableSQW(&rtc, DS3231_SQW_4096HZ);
    printf("SQW 4096Hz wlaczone\r\n");

    HAL_Delay(1000);

    /* Wyłącz SQW, przywróć tryb interrupt */
    DS3231_clod_DisableSQW(&rtc);

    /* Włącz SQW nawet przy zasilaniu bateryjnym */
    DS3231_clod_SetBatterySQW(&rtc, true);
}

/* =========================================================================
 * PRZYKŁAD 8 – Aging Offset (korekcja częstotliwości oscylatora)
 * ========================================================================= */
void Example_AgingOffset(void)
{
    int8_t current_offset;
    DS3231_clod_GetAgingOffset(&rtc, &current_offset);
    printf("Aktualny aging offset: %d\r\n", current_offset);

    /* Ustaw korektę +5 (spowalnia oscylator o ~0.5 ppm przy 25°C) */
    DS3231_clod_SetAgingOffset(&rtc, 5);

    /*
     * Aby efekt był natychmiastowy widoczny na wyjściu 32kHz,
     * wymuś konwersję temperatury – DS3231 załaduje nowy offset.
     */
    DS3231_clod_ForceTemperatureConversion(&rtc);
    printf("Aging offset ustawiony na +5\r\n");
}

/* =========================================================================
 * PRZYKŁAD 9 – Użycie wielu modułów DS3231 na jednej magistrali I2C
 *
 * Uwaga: DS3231 ma stały adres I2C (0x68). Aby użyć wielu modułów,
 *        należy umieścić je na oddzielnych magistralach I2C lub
 *        skorzystać z multipleksera I2C (np. TCA9548A).
 *        Poniższy przykład pokazuje obsługę dwóch magistral I2C.
 * ========================================================================= */
#ifdef USE_TWO_I2C_BUSES
extern I2C_HandleTypeDef hi2c2;

DS3231_Handle rtc2;

void Example_MultipleModules(void)
{
    /* Moduł 1 na I2C1 */
    DS3231_clod_Init(&rtc,  &hi2c1, DS3231_I2C_ADDR, DS3231_FORMAT_24H);
    /* Moduł 2 na I2C2 */
    DS3231_clod_Init(&rtc2, &hi2c2, DS3231_I2C_ADDR, DS3231_FORMAT_24H);

    DS3231_DateTime dt1, dt2;
    DS3231_clod_GetDateTime(&rtc,  &dt1);
    DS3231_clod_GetDateTime(&rtc2, &dt2);

    printf("RTC1: %02d:%02d:%02d\r\n", dt1.hours, dt1.minutes, dt1.seconds);
    printf("RTC2: %02d:%02d:%02d\r\n", dt2.hours, dt2.minutes, dt2.seconds);
}
#endif

/* =========================================================================
 * PRZYKŁAD 10 – Tryb 12h (AM/PM)
 * ========================================================================= */
void Example_12HourMode(void)
{
    DS3231_DateTime dt = {
        .seconds = 0,
        .minutes = 45,
        .hours   = 11,
        .ampm    = DS3231_AM,
        .format  = DS3231_FORMAT_12H,
        .day     = 1,
        .date    = 1,
        .month   = 1,
        .year    = 25,
        .century = false
    };
    DS3231_clod_SetDateTime(&rtc, &dt);

    DS3231_DateTime read_dt;
    DS3231_clod_GetDateTime(&rtc, &read_dt);
    printf("Czas (12h): %02d:%02d:%02d %s\r\n",
           read_dt.hours, read_dt.minutes, read_dt.seconds,
           (read_dt.ampm == DS3231_PM) ? "PM" : "AM");
}

/* =========================================================================
 * Handler przerwania EXTI – wywoływany gdy DS3231 pociągnie INT/SQW w dół
 *
 * W CubeMX:
 *   1. Ustaw PA0 jako GPIO_EXTI0, wyzwalanie opadającym zboczem (falling edge)
 *   2. Włącz EXTI0_IRQn w NVIC
 *   3. Poniższy kod umieść w stm32f1xx_it.c lub wywołaj z HAL_GPIO_EXTI_Callback
 * ========================================================================= */

/**
 * @brief Callback wywoływany przez HAL dla wszystkich linii EXTI.
 *        Umieść w main.c lub w pliku z callbackami HAL.
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_0) {  /* PA0 = INT/SQW DS3231 */
        bool a1, a2;

        /*
         * Odczytaj i wyczyść flagi alarmów.
         * Uwaga: wywołanie I2C w przerwaniu – upewnij się, że I2C
         * nie używa DMA/przerwań w tle lub użyj semafora (RTOS).
         */
        if (DS3231_clod_CheckAndClearAlarmFlags(&rtc, &a1, &a2) == DS3231_OK) {
            if (a1) g_alarm1_fired = true;
            if (a2) g_alarm2_fired = true;
        }
    }
}

/* =========================================================================
 * Pętla główna – sprawdzanie flag alarmów (polling zamiast przerwań)
 *
 * Alternatywa dla podejścia z przerwaniami – przydatna gdy pin INT
 * nie jest podłączony do MCU.
 * ========================================================================= */
void Example_MainLoop_Polling(void)
{
    while (1) {
        /* Obsługa flag ustawionych w przerwaniu */
        if (g_alarm1_fired) {
            g_alarm1_fired = false;
            printf("ALARM 1 wystrzelil!\r\n");
            /* ... akcja użytkownika ... */
        }
        if (g_alarm2_fired) {
            g_alarm2_fired = false;
            printf("ALARM 2 wystrzelil!\r\n");
        }

        /* Alternatywnie – polling bez przerwań (gdy INT niepodłączone) */
        /* bool a1, a2;
           DS3231_CheckAndClearAlarmFlags(&rtc, &a1, &a2);
           if (a1) { ... }  */

        Example_ReadTime();
        HAL_Delay(1000);
    }
}