/**
 * @file    ds3231.h
 * @brief   Biblioteka obsługi modułu RTC DS3231 dla STM32 (HAL)
 * @details Obsługuje: odczyt/zapis czasu i daty, alarmy, przerwania,
 *          wyjście fali prostokątnej, sensor temperatury, aging offset.
 *
 * Platforma: STM32F103C8T6 + HAL
 * Interfejs: I2C (adres 0x68, 7-bit)
 */

#ifndef DS3231_CLOD_H
#define DS3231_CLOD_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Adres I2C
 * ========================================================================= */
#define DS3231_I2C_ADDR         0x68   /**< Adres 7-bit przesunięty dla HAL */

/* =========================================================================
 * Adresy rejestrów (zgodnie z dokumentacją, str. 11)
 * ========================================================================= */
#define DS3231_REG_SECONDS      0x00
#define DS3231_REG_MINUTES      0x01
#define DS3231_REG_HOURS        0x02
#define DS3231_REG_DAY          0x03
#define DS3231_REG_DATE         0x04
#define DS3231_REG_MONTH        0x05
#define DS3231_REG_YEAR         0x06
#define DS3231_REG_ALM1_SEC     0x07
#define DS3231_REG_ALM1_MIN     0x08
#define DS3231_REG_ALM1_HOUR    0x09
#define DS3231_REG_ALM1_DAY     0x0A
#define DS3231_REG_ALM2_MIN     0x0B
#define DS3231_REG_ALM2_HOUR    0x0C
#define DS3231_REG_ALM2_DAY     0x0D
#define DS3231_REG_CONTROL      0x0E
#define DS3231_REG_STATUS       0x0F
#define DS3231_REG_AGING        0x10
#define DS3231_REG_TEMP_MSB     0x11
#define DS3231_REG_TEMP_LSB     0x12

/* =========================================================================
 * Bity rejestru Control (0x0E)
 * ========================================================================= */
#define DS3231_CTRL_EOSC        (1 << 7)  /**< Enable Oscillator (aktywny LOW) */
#define DS3231_CTRL_BBSQW       (1 << 6)  /**< Battery-Backed Square-Wave Enable */
#define DS3231_CTRL_CONV        (1 << 5)  /**< Convert Temperature */
#define DS3231_CTRL_RS2         (1 << 4)  /**< Rate Select bit 2 */
#define DS3231_CTRL_RS1         (1 << 3)  /**< Rate Select bit 1 */
#define DS3231_CTRL_INTCN       (1 << 2)  /**< Interrupt Control */
#define DS3231_CTRL_A2IE        (1 << 1)  /**< Alarm 2 Interrupt Enable */
#define DS3231_CTRL_A1IE        (1 << 0)  /**< Alarm 1 Interrupt Enable */

/* =========================================================================
 * Bity rejestru Status (0x0F)
 * ========================================================================= */
#define DS3231_STAT_OSF         (1 << 7)  /**< Oscillator Stop Flag */
#define DS3231_STAT_EN32KHZ     (1 << 3)  /**< Enable 32kHz Output */
#define DS3231_STAT_BSY         (1 << 2)  /**< Busy */
#define DS3231_STAT_A2F         (1 << 1)  /**< Alarm 2 Flag */
#define DS3231_STAT_A1F         (1 << 0)  /**< Alarm 1 Flag */

/* =========================================================================
 * Timeout I2C (ms)
 * ========================================================================= */
#define DS3231_I2C_TIMEOUT      100

/* =========================================================================
 * Typy wyliczeniowe
 * ========================================================================= */

/**
 * @brief Kody błędów biblioteki
 */
typedef enum {
    DS3231_OK           = 0,   /**< Operacja zakończona sukcesem */
    DS3231_ERR_I2C      = 1,   /**< Błąd komunikacji I2C */
    DS3231_ERR_PARAM    = 2,   /**< Nieprawidłowy parametr */
    DS3231_ERR_BUSY     = 3,   /**< Urządzenie zajęte (konwersja temperatury) */
} DS3231_Status;

/**
 * @brief Format godziny
 */
typedef enum {
    DS3231_FORMAT_24H = 0,     /**< Format 24-godzinny */
    DS3231_FORMAT_12H = 1,     /**< Format 12-godzinny (AM/PM) */
} DS3231_HourFormat;

/**
 * @brief AM / PM (tylko w trybie 12h)
 */
typedef enum {
    DS3231_AM = 0,
    DS3231_PM = 1,
} DS3231_AmPm;

/**
 * @brief Częstotliwość wyjścia fali prostokątnej (SQW)
 */
typedef enum {
    DS3231_SQW_1HZ      = 0x00,  /**< 1 Hz     (RS2=0, RS1=0) */
    DS3231_SQW_1024HZ   = 0x08,  /**< 1.024 kHz (RS2=0, RS1=1) */
    DS3231_SQW_4096HZ   = 0x10,  /**< 4.096 kHz (RS2=1, RS1=0) */
    DS3231_SQW_8192HZ   = 0x18,  /**< 8.192 kHz (RS2=1, RS1=1) – POR default */
} DS3231_SqwFreq;

/**
 * @brief Tryb alarmu 1 (tabela 2 z dokumentacji)
 */
typedef enum {
    DS3231_ALM1_EVERY_SECOND        = 0x0F, /**< Co sekundę */
    DS3231_ALM1_MATCH_SECONDS       = 0x0E, /**< Zgodność sekund */
    DS3231_ALM1_MATCH_MIN_SEC       = 0x0C, /**< Zgodność minut i sekund */
    DS3231_ALM1_MATCH_HR_MIN_SEC    = 0x08, /**< Zgodność godz, min, sek */
    DS3231_ALM1_MATCH_DATE          = 0x00, /**< Zgodność daty, godz, min, sek */
    DS3231_ALM1_MATCH_DAY           = 0x10, /**< Zgodność dnia tyg, godz, min, sek */
} DS3231_Alarm1Mode;

/**
 * @brief Tryb alarmu 2 (tabela 2 z dokumentacji)
 */
typedef enum {
    DS3231_ALM2_EVERY_MINUTE        = 0x07, /**< Co minutę (przy :00 sek) */
    DS3231_ALM2_MATCH_MINUTES       = 0x06, /**< Zgodność minut */
    DS3231_ALM2_MATCH_HR_MIN        = 0x04, /**< Zgodność godz i min */
    DS3231_ALM2_MATCH_DATE          = 0x00, /**< Zgodność daty, godz, min */
    DS3231_ALM2_MATCH_DAY           = 0x08, /**< Zgodność dnia tyg, godz, min */
} DS3231_Alarm2Mode;

/* =========================================================================
 * Struktury danych
 * ========================================================================= */

/**
 * @brief Struktura czasu i daty
 */
typedef struct {
    uint8_t         seconds;    /**< Sekundy: 0–59 */
    uint8_t         minutes;    /**< Minuty:  0–59 */
    uint8_t         hours;      /**< Godziny: 0–23 (24h) lub 1–12 (12h) */
    DS3231_AmPm     ampm;       /**< AM/PM (tylko tryb 12h) */
    DS3231_HourFormat format;   /**< Format godziny */
    uint8_t         day;        /**< Dzień tygodnia: 1–7 (definicja zależna od użytkownika) */
    uint8_t         date;       /**< Dzień miesiąca: 1–31 */
    uint8_t         month;      /**< Miesiąc: 1–12 */
    uint8_t         year;       /**< Rok (ostatnie 2 cyfry): 0–99 */
    bool            century;    /**< Bit stulecia (bit 7 rejestru miesiąca) */
} DS3231_DateTime;

/**
 * @brief Struktura alarmu 1
 */
typedef struct {
    uint8_t             seconds;  /**< Sekundy: 0–59 */
    uint8_t             minutes;  /**< Minuty:  0–59 */
    uint8_t             hours;    /**< Godziny: 0–23 lub 1–12 */
    DS3231_AmPm         ampm;     /**< AM/PM (tylko tryb 12h) */
    DS3231_HourFormat   format;   /**< Format godziny */
    uint8_t             day_date; /**< Dzień tygodnia (1–7) lub data (1–31) */
    DS3231_Alarm1Mode   mode;     /**< Tryb alarmu */
} DS3231_Alarm1;

/**
 * @brief Struktura alarmu 2
 */
typedef struct {
    uint8_t             minutes;  /**< Minuty:  0–59 */
    uint8_t             hours;    /**< Godziny: 0–23 lub 1–12 */
    DS3231_AmPm         ampm;     /**< AM/PM (tylko tryb 12h) */
    DS3231_HourFormat   format;   /**< Format godziny */
    uint8_t             day_date; /**< Dzień tygodnia (1–7) lub data (1–31) */
    DS3231_Alarm2Mode   mode;     /**< Tryb alarmu */
} DS3231_Alarm2;

/**
 * @brief Główna struktura opisująca moduł DS3231
 *
 * Jedna instancja = jeden fizyczny moduł.
 * Dzięki temu można obsługiwać wiele DS3231 na jednej magistrali I2C
 * (różne adresy) lub na różnych magistralach.
 */
typedef struct {
    I2C_HandleTypeDef   *hi2c;         /**< Wskaźnik na handle I2C (HAL) */
    uint16_t            i2c_addr;     /**< Adres I2C (domyślnie DS3231_I2C_ADDR) */
    GPIO_TypeDef        *sqw_port;
    uint16_t            sqw_pin;
    DS3231_HourFormat   hour_format;  /**< Aktualny format godziny modułu */
    bool                initialized;  /**< Flaga inicjalizacji */
    uint8_t             DS3231_IRQ_Flag; /**< Flaga przerwania alarmu */
    
} DS3231_t;

#define DS3231_IRQ_NONE    0x00U
#define DS3231_IRQ_ALARM1  (1U << 0)
#define DS3231_IRQ_ALARM2  (1U << 1)

typedef DS3231_t DS3231_Handle;

/* =========================================================================
 * API – funkcje publiczne
 * ========================================================================= */

/* --- Inicjalizacja ------------------------------------------------------- */

/**
 * @brief  Inicjalizuje strukturę i moduł DS3231.
 * @param  hrtc        Wskaźnik na strukturę DS3231_t
  (wypełniana przez funkcję)
 * @param  hi2c        Wskaźnik na zainicjalizowany I2C_HandleTypeDef
 * @param  i2c_addr    Adres I2C (użyj DS3231_I2C_ADDR dla standardowego adresu)
 * @param  hour_format Preferowany format godziny
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_Init(DS3231_t *hrtc, I2C_HandleTypeDef *hi2c, uint16_t address, DS3231_HourFormat hour_format);

/* --- Czas i data --------------------------------------------------------- */

/**
 * @brief  Zapisuje datę i godzinę do modułu.
 * @param  hrtc  Wskaźnik na DS3231_t

 * @param  dt    Wskaźnik na strukturę DS3231_DateTime z danymi do zapisu
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_SetDateTime(DS3231_t *hrtc, const DS3231_DateTime *dt);

/**
 * @brief  Odczytuje aktualną datę i godzinę z modułu.
 * @param  hrtc  Wskaźnik na DS3231_t

 * @param  dt    Wskaźnik na strukturę DS3231_DateTime do wypełnienia
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_GetDateTime(DS3231_t *hrtc, DS3231_DateTime *dt);

/* --- Alarmy -------------------------------------------------------------- */

/**
 * @brief  Ustawia alarm 1.
 * @param  hrtc   Wskaźnik na DS3231_t

 * @param  alarm  Wskaźnik na strukturę DS3231_Alarm1 z konfiguracją alarmu
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_SetAlarm1(DS3231_t *hrtc, const DS3231_Alarm1 *alarm);

/**
 * @brief  Ustawia alarm 2.
 * @param  hrtc   Wskaźnik na DS3231_t

 * @param  alarm  Wskaźnik na strukturę DS3231_Alarm2 z konfiguracją alarmu
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_SetAlarm2(DS3231_t *hrtc, const DS3231_Alarm2 *alarm);

/**
 * @brief  Odczytuje konfigurację alarmu 1.
 * @param  hrtc   Wskaźnik na DS3231_t

 * @param  alarm  Wskaźnik na strukturę DS3231_Alarm1 do wypełnienia
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_GetAlarm1(DS3231_t *hrtc, DS3231_Alarm1 *alarm);

/**
 * @brief  Odczytuje konfigurację alarmu 2.
 * @param  hrtc   Wskaźnik na DS3231_t

 * @param  alarm  Wskaźnik na strukturę DS3231_Alarm2 do wypełnienia
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_GetAlarm2(DS3231_t *hrtc, DS3231_Alarm2 *alarm);

/* --- Przerwania ---------------------------------------------------------- */

/**
 * @brief  Włącza przerwanie od alarmu 1 (pin INT/SQW).
 * @note   Ustawia INTCN=1, A1IE=1. Wyjście SQW jest wyłączone.
 * @param  hrtc  Wskaźnik na DS3231_t

 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_EnableAlarm1Interrupt(DS3231_t *hrtc);

/**
 * @brief  Wyłącza przerwanie od alarmu 1.
 * @param  hrtc  Wskaźnik na DS3231_t

 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_DisableAlarm1Interrupt(DS3231_t *hrtc);

/**
 * @brief  Włącza przerwanie od alarmu 2 (pin INT/SQW).
 * @note   Ustawia INTCN=1, A2IE=1.
 * @param  hrtc  Wskaźnik na DS3231_t

 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_EnableAlarm2Interrupt(DS3231_t *hrtc);

/**
 * @brief  Wyłącza przerwanie od alarmu 2.
 * @param  hrtc  Wskaźnik na DS3231_t

 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_DisableAlarm2Interrupt(DS3231_t *hrtc);

/**
 * @brief  Sprawdza i czyści flagi alarmów. Wywoływać w handlerze przerwania.
 * @param  hrtc       Wskaźnik na DS3231_t
 * @note   Wynik zapisywany jest do hrtc->DS3231_IRQ_Flag
 *         (DS3231_IRQ_ALARM1 i/lub DS3231_IRQ_ALARM2).
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_CheckAndClearAlarmFlags(DS3231_t *hrtc);

/* --- Wyjście fali prostokątnej (SQW) ------------------------------------ */

/**
 * @brief  Włącza wyjście fali prostokątnej na pinie INT/SQW.
 * @note   Ustawia INTCN=0. Przerwania alarmów są wyłączone (pin = SQW).
 * @param  hrtc  Wskaźnik na DS3231_t
 * @param  freq  Żądana częstotliwość
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_EnableSQW(DS3231_t *hrtc, DS3231_SqwFreq freq);

/**
 * @brief  Wyłącza wyjście fali prostokątnej (przywraca tryb interrupt).
 * @param  hrtc  Wskaźnik na DS3231_t
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_DisableSQW(DS3231_t *hrtc);

/**
 * @brief  Włącza/wyłącza wyjście SQW w trybie zasilania bateryjnego (BBSQW).
 * @param  hrtc   Wskaźnik na DS3231_t
 * @param  enable true = SQW aktywne gdy VCC < VPF; false = INT/SQW w stanie Hi-Z
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_SetBatterySQW(DS3231_t *hrtc, bool enable);

/* --- Wyjście 32kHz ------------------------------------------------------- */

/**
 * @brief  Włącza/wyłącza wyjście 32kHz.
 * @param  hrtc   Wskaźnik na DS3231_t
 * @param  enable true = wyjście aktywne
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_Set32kHzOutput(DS3231_t *hrtc, bool enable);

/* --- Temperatura --------------------------------------------------------- */

/**
 * @brief  Odczytuje temperaturę z wewnętrznego sensora DS3231.
 * @note   Rozdzielczość 0.25°C. Aktualizacja co 64 s lub po konwersji ręcznej.
 * @param  hrtc  Wskaźnik na DS3231_t
 * @param  temp  Wskaźnik na float, wynik w °C
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_GetTemperature(DS3231_t *hrtc, float *temp);

/**
 * @brief  Wymusza natychmiastową konwersję temperatury i aktualizację TCXO.
 * @note   Blokuje do momentu zakończenia (~200ms) lub zwraca DS3231_ERR_BUSY.
 * @param  hrtc  Wskaźnik na DS3231_t
 * @return DS3231_OK lub DS3231_ERR_BUSY / DS3231_ERR_I2C
 */
DS3231_Status DS3231_clod_ForceTemperatureConversion(DS3231_t *hrtc);

/* --- Aging Offset -------------------------------------------------------- */

/**
 * @brief  Ustawia rejestr Aging Offset (korekcja częstotliwości oscylatora).
 * @note   Wartość w kodzie U2 (–128 do +127). +1 LSB ≈ +0.1 ppm przy 25°C.
 *         Wartości dodatnie zwalniają oscylator, ujemne przyspieszają.
 * @param  hrtc   Wskaźnik na DS3231_t

 * @param  offset Wartość korekcji (int8_t)
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_SetAgingOffset(DS3231_t*hrtc, int8_t offset);

/**
 * @brief  Odczytuje rejestr Aging Offset.
 * @param  hrtc   Wskaźnik na DS3231_t

 * @param  offset Wskaźnik na int8_t do wypełnienia
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_GetAgingOffset(DS3231_t*hrtc, int8_t *offset);

/* --- Status i diagnostyka ------------------------------------------------ */

/**
 * @brief  Sprawdza, czy oscylator był zatrzymany (flaga OSF).
 * @param  hrtc     Wskaźnik na DS3231_t

 * @param  osf_set  Wskaźnik na bool – true jeśli OSF=1 (czas może być nieważny)
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_GetOscillatorStopFlag(DS3231_t *hrtc, bool *osf_set);

/**
 * @brief  Kasuje flagę OSF.
 * @param  hrtc  Wskaźnik na DS3231_t

 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_ClearOscillatorStopFlag(DS3231_t *hrtc);

/**
 * @brief  Włącza/wyłącza oscylator (bit EOSC).
 * @note   EOSC=0 → oscylator uruchomiony (POR default).
 *         EOSC=1 → oscylator zatrzymany gdy działa z VBAT (oszczędzanie baterii).
 * @param  hrtc   Wskaźnik na DS3231_t

 * @param  enable true = oscylator uruchomiony
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_SetOscillator(DS3231_t *hrtc, bool enable);

/**
 * @brief  Odczytuje bajt rejestru kontrolnego.
 * @param  hrtc   Wskaźnik na DS3231_t

 * @param  value  Wskaźnik na uint8_t do wypełnienia
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_ReadControlReg(DS3231_t *hrtc, uint8_t *value);

/**
 * @brief  Odczytuje bajt rejestru statusu.
 * @param  hrtc   Wskaźnik na DS3231_t

 * @param  value  Wskaźnik na uint8_t do wypełnienia
 * @return DS3231_OK lub kod błędu
 */
DS3231_Status DS3231_clod_ReadStatusReg(DS3231_t *hrtc, uint8_t *value);

/* =========================================================================
 * Pomocnicze makra BCD
 * ========================================================================= */
#define DS3231_BCD2DEC(bcd)   ((((bcd) >> 4) * 10) + ((bcd) & 0x0F))
#define DS3231_DEC2BCD(dec)   ((((dec) / 10) << 4) | ((dec) % 10))

#endif /* DS3231_H */