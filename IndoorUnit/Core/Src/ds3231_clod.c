/**
 * @file    ds3231.c
 * @brief   Implementacja biblioteki DS3231 dla STM32 HAL
 */

#include "ds3231_clod.h"

/* =========================================================================
 * Funkcje prywatne (statyczne)
 * ========================================================================= */

/**
 * @brief Zapisuje jeden bajt do rejestru DS3231.
 */
static DS3231_Status ds3231_clod_write_reg(DS3231_t *hrtc, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    if (HAL_I2C_Master_Transmit(hrtc->hi2c, hrtc->i2c_addr, buf, 2, DS3231_I2C_TIMEOUT) != HAL_OK) {
        return DS3231_ERR_I2C;
    }
    return DS3231_OK;
}

/**
 * @brief Odczytuje jeden bajt z rejestru DS3231.
 */
static DS3231_Status ds3231_clod_read_reg(DS3231_t *hrtc,uint8_t reg, uint8_t *value)
{
    /* Ustaw wskaźnik rejestru */
    if (HAL_I2C_Master_Transmit(hrtc->hi2c, hrtc->i2c_addr,
                                 &reg, 1, DS3231_I2C_TIMEOUT) != HAL_OK) {
        return DS3231_ERR_I2C;
    }
    /* Odczytaj bajt */
    if (HAL_I2C_Master_Receive(hrtc->hi2c, hrtc->i2c_addr,
                                value, 1, DS3231_I2C_TIMEOUT) != HAL_OK) {
        return DS3231_ERR_I2C;
    }
    return DS3231_OK;
}

/**
 * @brief Odczytuje wiele kolejnych bajtów zaczynając od podanego rejestru.
 */
static DS3231_Status ds3231_clod_read_regs(DS3231_t *hrtc,
                                       uint8_t reg,
                                       uint8_t *buf,
                                       uint8_t len)
{
    if (HAL_I2C_Master_Transmit(hrtc->hi2c, hrtc->i2c_addr,
                                 &reg, 1, DS3231_I2C_TIMEOUT) != HAL_OK) {
        return DS3231_ERR_I2C;
    }
    if (HAL_I2C_Master_Receive(hrtc->hi2c, hrtc->i2c_addr,
                                buf, len, DS3231_I2C_TIMEOUT) != HAL_OK) {
        return DS3231_ERR_I2C;
    }
    return DS3231_OK;
}

/**
 * @brief Modyfikuje wybrane bity rejestru (read-modify-write).
 * @param mask  Maska bitów do modyfikacji (1 = modyfikuj)
 * @param value Nowe wartości bitów (w pozycjach maski)
 */
static DS3231_Status ds3231_clod_modify_reg(DS3231_t *hrtc,uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current;
    DS3231_Status ret;

    ret = ds3231_clod_read_reg(hrtc, reg, &current);
    if (ret != DS3231_OK) return ret;

    current = (current & ~mask) | (value & mask);
    return ds3231_clod_write_reg(hrtc, reg, current);
}

/**
 * @brief Koduje godzinę do formatu wymaganego przez DS3231.
 *        Bit 6 = 12/24, bit 5 = AM/PM (12h) lub 20h (24h).
 */
static uint8_t ds3231_clod_encode_hours(uint8_t hours,
                                    DS3231_HourFormat fmt,
                                    DS3231_AmPm ampm)
{
    uint8_t reg = 0;
    if (fmt == DS3231_FORMAT_12H) {
        reg = (1 << 6);                        /* bit 6 = 1 → tryb 12h */
        if (ampm == DS3231_PM) reg |= (1 << 5); /* bit 5 = PM */
        reg |= DS3231_DEC2BCD(hours);
    } else {
        /* Tryb 24h – bit 5 to bit "20-hour" */
        reg = DS3231_DEC2BCD(hours);
    }
    return reg;
}

/**
 * @brief Dekoduje godzinę z bajtu rejestru DS3231.
 */
static void ds3231_clod_decode_hours(uint8_t reg,
                                  uint8_t *hours,
                                  DS3231_HourFormat *fmt,
                                  DS3231_AmPm *ampm)
{
    if (reg & (1 << 6)) {
        /* Tryb 12h */
        *fmt  = DS3231_FORMAT_12H;
        *ampm = (reg & (1 << 5)) ? DS3231_PM : DS3231_AM;
        *hours = DS3231_BCD2DEC(reg & 0x1F);
    } else {
        /* Tryb 24h */
        *fmt   = DS3231_FORMAT_24H;
        *ampm  = DS3231_AM;
        *hours = DS3231_BCD2DEC(reg & 0x3F);
    }
}

/* =========================================================================
 * Implementacja API publicznego
 * ========================================================================= */

/* --- Inicjalizacja ------------------------------------------------------- */

DS3231_Status DS3231_clod_Init(DS3231_t *hrtc, I2C_HandleTypeDef *hi2c, uint16_t address, DS3231_HourFormat hour_format)
{
    if (hrtc == NULL || hi2c == NULL){
        return DS3231_ERR_PARAM;
    }

    DS3231_Status status;
    uint8_t ctrl;

    /* Wypełnienie struktury */
    hrtc->hi2c        = hi2c;
    hrtc->i2c_addr     = address << 1;
    hrtc->hour_format = hour_format;
    hrtc->initialized = false;
    hrtc->DS3231_IRQ_Flag = 0;
    hrtc->sqw_port = NULL;
    hrtc->sqw_pin = 0;

    /* Sprawdź komunikację – odczytaj rejestr kontrolny */
    status = ds3231_clod_read_reg(hrtc, DS3231_REG_CONTROL, &ctrl);
    if (status != DS3231_OK){
        return status;
    }

    /*
     * Konfiguracja domyślna przy inicjalizacji:
    *  - EOSC = 0  → oscylator uruchomiony
     *  - INTCN = 1 → pin INT/SQW jako wyjście przerwania (POR default)
     *  - A1IE = 0, A2IE = 0 → przerwania alarmów wyłączone
     *  - BBSQW = 0 → SQW wyłączone przy zasilaniu bateryjnym
     *  - CONV = 0
     *  - RS2=1, RS1=1 → SQW 8.192 kHz (nieistotne gdy INTCN=1)
     *
     * Nie resetujemy czasu – zachowujemy dane jeśli oscylator działał.
     */
    ctrl = DS3231_CTRL_INTCN | DS3231_CTRL_RS2 | DS3231_CTRL_RS1;
    status  = ds3231_clod_write_reg(hrtc, DS3231_REG_CONTROL, ctrl);
    if (status != DS3231_OK){
        return status;
    }
    
    /* OSF zostawiamy – użytkownik może sprawdzić ręcznie */
    uint8_t status_reg;
    status = ds3231_clod_read_reg(hrtc, DS3231_REG_STATUS, &status_reg);
    if (status != DS3231_OK) {
        return status;
    }
    status_reg &= (uint8_t)~(DS3231_STAT_A1F | DS3231_STAT_A2F);
    status = ds3231_clod_write_reg(hrtc, DS3231_REG_STATUS, status_reg);

    if (status == DS3231_ERR_I2C){
        return status;
    }

    hrtc->initialized = true;    
    return status;
}

/* --- Czas i data --------------------------------------------------------- */

DS3231_Status DS3231_clod_SetDateTime(DS3231_t *hrtc, const DS3231_DateTime *dt)
{
    uint8_t buf[8]; /* adres + 7 bajtów danych */
    uint8_t idx = 0;

    if (hrtc == NULL || dt == NULL)         return DS3231_ERR_PARAM;
    if (!hrtc->initialized)                 return DS3231_ERR_PARAM;
    if (dt->seconds > 59 || dt->minutes > 59) return DS3231_ERR_PARAM;
    if (dt->day < 1 || dt->day > 7)          return DS3231_ERR_PARAM;
    if (dt->date < 1 || dt->date > 31)       return DS3231_ERR_PARAM;
    if (dt->month < 1 || dt->month > 12)     return DS3231_ERR_PARAM;
    if (dt->year > 99)                        return DS3231_ERR_PARAM;

    /* Walidacja godzin */
    if (dt->format == DS3231_FORMAT_24H) {
        if (dt->hours > 23) return DS3231_ERR_PARAM;
    } else {
        if (dt->hours < 1 || dt->hours > 12) return DS3231_ERR_PARAM;
    }

    buf[idx++] = DS3231_REG_SECONDS;           /* adres startowy */
    buf[idx++] = DS3231_DEC2BCD(dt->seconds);
    buf[idx++] = DS3231_DEC2BCD(dt->minutes);
    buf[idx++] = ds3231_clod_encode_hours(dt->hours, dt->format, dt->ampm);
    buf[idx++] = dt->day & 0x07;
    buf[idx++] = DS3231_DEC2BCD(dt->date);

    /* Miesiąc + bit Century (bit 7) */
    uint8_t month_reg = DS3231_DEC2BCD(dt->month);
    if (dt->century) month_reg |= (1 << 7);
    buf[idx++] = month_reg;

    buf[idx++] = DS3231_DEC2BCD(dt->year);

    if (HAL_I2C_Master_Transmit(hrtc->hi2c, hrtc->i2c_addr, buf, idx, DS3231_I2C_TIMEOUT) != HAL_OK) {
        return DS3231_ERR_I2C;
    }

    /* Po zapisie sekund łańcuch odliczania jest resetowany.
       Wszystkie pozostałe rejestry muszą być zapisane w ciągu 1 sekundy
       – operacja powyżej zapisuje wszystko w jednej transakcji I2C, więc
       warunek jest spełniony. */

    hrtc->hour_format = dt->format;
    return DS3231_OK;
}

DS3231_Status DS3231_clod_GetDateTime(DS3231_t *hrtc, DS3231_DateTime *dt)
{
    uint8_t raw[7];
    DS3231_Status ret;

    if (hrtc == NULL || dt == NULL) return DS3231_ERR_PARAM;
    if (!hrtc->initialized)         return DS3231_ERR_PARAM;

    ret = ds3231_clod_read_regs(hrtc, DS3231_REG_SECONDS, raw, 7);
    if (ret != DS3231_OK) return ret;

    dt->seconds = DS3231_BCD2DEC(raw[0] & 0x7F);
    dt->minutes = DS3231_BCD2DEC(raw[1] & 0x7F);

    ds3231_clod_decode_hours(raw[2], &dt->hours, &dt->format, &dt->ampm);
    hrtc->hour_format = dt->format;

    dt->day   = raw[3] & 0x07;
    dt->date  = DS3231_BCD2DEC(raw[4] & 0x3F);

    dt->century = (raw[5] & (1 << 7)) ? true : false;
    dt->month   = DS3231_BCD2DEC(raw[5] & 0x1F);
    dt->year    = DS3231_BCD2DEC(raw[6]);

    return DS3231_OK;
}

/* --- Alarmy -------------------------------------------------------------- */

DS3231_Status DS3231_clod_SetAlarm1(DS3231_t *hrtc, const DS3231_Alarm1 *alarm)
{
    uint8_t buf[5];
    uint8_t idx = 0;
    uint8_t a1m1, a1m2, a1m3, a1m4;

    if (hrtc == NULL || alarm == NULL) return DS3231_ERR_PARAM;
    if (!hrtc->initialized)            return DS3231_ERR_PARAM;

    /*
     * Bity maski (AxMy) kodowane są w bicie 7 każdego rejestru alarmu.
     * Pole mode zawiera 4-bitową mapę masek w bitach [3:0] (A1M4..A1M1)
     * oraz opcjonalnie bit [4] dla DY/DT.
     *
     * Schemat DS3231_Alarm1Mode:
     *   bit0 = A1M1, bit1 = A1M2, bit2 = A1M3, bit3 = A1M4, bit4 = DY/DT
     */
    a1m1 = ((alarm->mode & 0x01) ? (1 << 7) : 0);
    a1m2 = ((alarm->mode & 0x02) ? (1 << 7) : 0);
    a1m3 = ((alarm->mode & 0x04) ? (1 << 7) : 0);
    a1m4 = ((alarm->mode & 0x08) ? (1 << 7) : 0);

    buf[idx++] = DS3231_REG_ALM1_SEC;
    buf[idx++] = a1m1 | DS3231_DEC2BCD(alarm->seconds & 0x7F);
    buf[idx++] = a1m2 | DS3231_DEC2BCD(alarm->minutes & 0x7F);
    buf[idx++] = a1m3 | ds3231_clod_encode_hours(alarm->hours, alarm->format, alarm->ampm);
    
    /* Dzień/data – bit 6: DY/DT */
    uint8_t dy_dt = (alarm->mode & 0x10) ? (1 << 6) : 0;
    buf[idx++] = a1m4 | dy_dt | DS3231_DEC2BCD(alarm->day_date & 0x3F);

    if (HAL_I2C_Master_Transmit(hrtc->hi2c, hrtc->i2c_addr,
                                 buf, idx, DS3231_I2C_TIMEOUT) != HAL_OK) {
        return DS3231_ERR_I2C;
    }

    uint8_t status;
    DS3231_Status ret = ds3231_clod_read_reg(hrtc, DS3231_REG_STATUS, &status);
    if (ret != DS3231_OK) return ret;
    status &= (uint8_t)~DS3231_STAT_A1F;
    return ds3231_clod_write_reg(hrtc, DS3231_REG_STATUS, status);
}

DS3231_Status DS3231_clod_SetAlarm2(DS3231_t *hrtc, const DS3231_Alarm2 *alarm)
{
    uint8_t buf[4];
    uint8_t idx = 0;
    uint8_t a2m2, a2m3, a2m4;

    if (hrtc == NULL || alarm == NULL) return DS3231_ERR_PARAM;
    if (!hrtc->initialized)            return DS3231_ERR_PARAM;

    /*
     * DS3231_Alarm2Mode:
     *   bit0 = A2M2, bit1 = A2M3, bit2 = A2M4, bit3 = DY/DT
     */
    a2m2 = ((alarm->mode & 0x01) ? (1 << 7) : 0);
    a2m3 = ((alarm->mode & 0x02) ? (1 << 7) : 0);
    a2m4 = ((alarm->mode & 0x04) ? (1 << 7) : 0);

    buf[idx++] = DS3231_REG_ALM2_MIN;
    buf[idx++] = a2m2 | DS3231_DEC2BCD(alarm->minutes & 0x7F);
    buf[idx++] = a2m3 | ds3231_clod_encode_hours(alarm->hours, alarm->format, alarm->ampm);

    uint8_t dy_dt = (alarm->mode & 0x08) ? (1 << 6) : 0;
    buf[idx++] = a2m4 | dy_dt | DS3231_DEC2BCD(alarm->day_date & 0x3F);

    if (HAL_I2C_Master_Transmit(hrtc->hi2c, hrtc->i2c_addr,
                                 buf, idx, DS3231_I2C_TIMEOUT) != HAL_OK) {
        return DS3231_ERR_I2C;
    }

    uint8_t status;
    DS3231_Status ret = ds3231_clod_read_reg(hrtc, DS3231_REG_STATUS, &status);
    if (ret != DS3231_OK) return ret;
    status &= (uint8_t)~DS3231_STAT_A2F;
    return ds3231_clod_write_reg(hrtc, DS3231_REG_STATUS, status);
}

DS3231_Status DS3231_clod_GetAlarm1(DS3231_t *hrtc, DS3231_Alarm1 *alarm)
{
    uint8_t raw[4];
    DS3231_Status ret;

    if (hrtc == NULL || alarm == NULL) return DS3231_ERR_PARAM;
    if (!hrtc->initialized)            return DS3231_ERR_PARAM;

    ret = ds3231_clod_read_regs(hrtc, DS3231_REG_ALM1_SEC, raw, 4);
    if (ret != DS3231_OK) return ret;

    alarm->seconds  = DS3231_BCD2DEC(raw[0] & 0x7F);
    alarm->minutes  = DS3231_BCD2DEC(raw[1] & 0x7F);
    ds3231_clod_decode_hours(raw[2] & 0x7F, &alarm->hours, &alarm->format, &alarm->ampm);
    alarm->day_date = DS3231_BCD2DEC(raw[3] & 0x3F);

    /* Odtwórz tryb z bitów masek */
    uint8_t m = 0;
    if (raw[0] & (1 << 7)) m |= 0x01;
    if (raw[1] & (1 << 7)) m |= 0x02;
    if (raw[2] & (1 << 7)) m |= 0x04;
    if (raw[3] & (1 << 7)) m |= 0x08;
    if (raw[3] & (1 << 6)) m |= 0x10; /* DY/DT */
    alarm->mode = (DS3231_Alarm1Mode)m;

    return DS3231_OK;
}

DS3231_Status DS3231_clod_GetAlarm2(DS3231_t *hrtc, DS3231_Alarm2 *alarm)
{
    uint8_t raw[3];
    DS3231_Status ret;

    if (hrtc == NULL || alarm == NULL) return DS3231_ERR_PARAM;
    if (!hrtc->initialized)            return DS3231_ERR_PARAM;

    ret = ds3231_clod_read_regs(hrtc, DS3231_REG_ALM2_MIN, raw, 3);
    if (ret != DS3231_OK) return ret;

    alarm->minutes  = DS3231_BCD2DEC(raw[0] & 0x7F);
    ds3231_clod_decode_hours(raw[1] & 0x7F, &alarm->hours, &alarm->format, &alarm->ampm);
    alarm->day_date = DS3231_BCD2DEC(raw[2] & 0x3F);

    uint8_t m = 0;
    if (raw[0] & (1 << 7)) m |= 0x01;
    if (raw[1] & (1 << 7)) m |= 0x02;
    if (raw[2] & (1 << 7)) m |= 0x04;
    if (raw[2] & (1 << 6)) m |= 0x08; /* DY/DT */
    alarm->mode = (DS3231_Alarm2Mode)m;

    return DS3231_OK;
}

/* --- Przerwania ---------------------------------------------------------- */

DS3231_Status DS3231_clod_EnableAlarm1Interrupt(DS3231_t *hrtc)
{
    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;
    /* INTCN=1 (pin = interrupt), A1IE=1 */
    return ds3231_clod_modify_reg(hrtc, DS3231_REG_CONTROL, DS3231_CTRL_INTCN | DS3231_CTRL_A1IE, DS3231_CTRL_INTCN | DS3231_CTRL_A1IE);
}

DS3231_Status DS3231_clod_DisableAlarm1Interrupt(DS3231_t *hrtc)
{
    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;
    return ds3231_clod_modify_reg(hrtc, DS3231_REG_CONTROL, DS3231_CTRL_A1IE, 0);
}

DS3231_Status DS3231_clod_EnableAlarm2Interrupt(DS3231_t *hrtc)
{
    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;
    return ds3231_clod_modify_reg(hrtc, DS3231_REG_CONTROL, DS3231_CTRL_INTCN | DS3231_CTRL_A2IE, DS3231_CTRL_INTCN | DS3231_CTRL_A2IE);
}

DS3231_Status DS3231_clod_DisableAlarm2Interrupt(DS3231_t *hrtc)
{
    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;
    return ds3231_clod_modify_reg(hrtc, DS3231_REG_CONTROL, DS3231_CTRL_A2IE, 0);
}

DS3231_Status DS3231_clod_CheckAndClearAlarmFlags(DS3231_t *hrtc)
{
    uint8_t status_reg;
    DS3231_Status status;

    if (hrtc == NULL){
        return DS3231_ERR_PARAM;
    }

    if (!hrtc->initialized) return DS3231_ERR_PARAM;

    hrtc->DS3231_IRQ_Flag = DS3231_IRQ_NONE;

    status = ds3231_clod_read_reg(hrtc, DS3231_REG_STATUS, &status_reg);

    if (status != DS3231_OK){
        return status;
    }

    if ((status_reg & DS3231_STAT_A1F) != 0U) {
        hrtc->DS3231_IRQ_Flag |= DS3231_IRQ_ALARM1;
    }
    if ((status_reg & DS3231_STAT_A2F) != 0U) {
        hrtc->DS3231_IRQ_Flag |= DS3231_IRQ_ALARM2;
    }

    if (hrtc->DS3231_IRQ_Flag != DS3231_IRQ_NONE) {
        status_reg &= (uint8_t)~(DS3231_STAT_A1F | DS3231_STAT_A2F);
        status = ds3231_clod_write_reg(hrtc, DS3231_REG_STATUS, status_reg);
    }

    return status;
}

/* --- SQW ----------------------------------------------------------------- */

DS3231_Status DS3231_clod_EnableSQW(DS3231_t *hrtc, DS3231_SqwFreq freq)
{
    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;

    /* INTCN=0 (pin = SQW), ustaw RS2:RS1 */
    uint8_t mask  = DS3231_CTRL_INTCN | DS3231_CTRL_RS2 | DS3231_CTRL_RS1;
    uint8_t value = (uint8_t)freq; /* freq zawiera RS2:RS1 w bitach 4:3 */
    return ds3231_clod_modify_reg(hrtc, DS3231_REG_CONTROL, mask, value);
}

DS3231_Status DS3231_clod_DisableSQW(DS3231_t *hrtc)
{
    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;
    /* INTCN=1 → pin przełączony z powrotem na interrupt */
    return ds3231_clod_modify_reg(hrtc, DS3231_REG_CONTROL,
                              DS3231_CTRL_INTCN, DS3231_CTRL_INTCN);
}

DS3231_Status DS3231_clod_SetBatterySQW(DS3231_t *hrtc, bool enable)
{
    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;
    uint8_t value = enable ? DS3231_CTRL_BBSQW : 0;
    return ds3231_clod_modify_reg(hrtc, DS3231_REG_CONTROL, DS3231_CTRL_BBSQW, value);
}

/* --- 32kHz --------------------------------------------------------------- */

DS3231_Status DS3231_clod_Set32kHzOutput(DS3231_t *hrtc, bool enable)
{
    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;
    uint8_t value = enable ? DS3231_STAT_EN32KHZ : 0;
    return ds3231_clod_modify_reg(hrtc, DS3231_REG_STATUS, DS3231_STAT_EN32KHZ, value);
}

/* --- Temperatura --------------------------------------------------------- */

DS3231_Status DS3231_clod_GetTemperature(DS3231_t *hrtc, float *temp)
{
    uint8_t raw[2];
    DS3231_Status ret;

    if (hrtc == NULL || temp == NULL) return DS3231_ERR_PARAM;
    if (!hrtc->initialized)           return DS3231_ERR_PARAM;

    ret = ds3231_clod_read_regs(hrtc, DS3231_REG_TEMP_MSB, raw, 2);
    if (ret != DS3231_OK) return ret;

    /*
     * Rejestr 11h: [7]=znak, [6:0]=część całkowita (kod U2)
     * Rejestr 12h: [7:6]=część ułamkowa, kroki 0.25°C
     *
     * Przykład z dokumentacji: 00011001 01b = +25.25°C
     */
    int8_t  integer  = (int8_t)raw[0];          /* sign-extended */
    uint8_t fraction = (raw[1] >> 6) & 0x03;    /* 2 bity ułamkowe */

    *temp = (float)integer + (float)fraction * 0.25f;
    return DS3231_OK;
}

DS3231_Status DS3231_clod_ForceTemperatureConversion(DS3231_t *hrtc)
{
    DS3231_Status ret;
    uint8_t status;

    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;

    /* Sprawdź, czy nie trwa już konwersja */
    ret = ds3231_clod_read_reg(hrtc, DS3231_REG_STATUS, &status);
    if (ret != DS3231_OK) return ret;
    if (status & DS3231_STAT_BSY) return DS3231_ERR_BUSY;

    /* Ustaw bit CONV */
    ret = ds3231_clod_modify_reg(hrtc, DS3231_REG_CONTROL, DS3231_CTRL_CONV, DS3231_CTRL_CONV);
    if (ret != DS3231_OK) return ret;

    /*
     * Czekaj na zakończenie konwersji.
     * Bit CONV wraca do 0 gdy konwersja jest gotowa (max ~200ms wg spec).
     * BSY jest ustawiany z ~2ms opóźnieniem po wpisaniu CONV=1.
     */
    uint32_t t_start = HAL_GetTick();
    while (1) {
        HAL_Delay(5);

        uint8_t ctrl;
        ret = ds3231_clod_read_reg(hrtc, DS3231_REG_CONTROL, &ctrl);
        if (ret == DS3231_ERR_I2C) 
            return ret;

        if (!(ctrl & DS3231_CTRL_CONV)) {
            /* Konwersja zakończona */
            break;
        }
        if ((HAL_GetTick() - t_start) > 300) {
            /* Timeout – nie powinno wystąpić przy sprawnym module */
            return DS3231_ERR_BUSY;
        }
    }
    return DS3231_OK;
}

/* --- Aging Offset -------------------------------------------------------- */

DS3231_Status DS3231_clod_SetAgingOffset(DS3231_t *hrtc, int8_t offset)
{
    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;
    return ds3231_clod_write_reg(hrtc, DS3231_REG_AGING, (uint8_t)offset);
}

DS3231_Status DS3231_clod_GetAgingOffset(DS3231_t *hrtc, int8_t *offset)
{
    uint8_t raw;
    DS3231_Status ret;

    if (hrtc == NULL || offset == NULL) return DS3231_ERR_PARAM;
    if (!hrtc->initialized)             return DS3231_ERR_PARAM;

    ret = ds3231_clod_read_reg(hrtc, DS3231_REG_AGING, &raw);
    if (ret != DS3231_OK) return ret;

    *offset = (int8_t)raw;
    return DS3231_OK;
}

/* --- Status i diagnostyka ------------------------------------------------ */

DS3231_Status DS3231_clod_GetOscillatorStopFlag(DS3231_t *hrtc, bool *osf_set)
{
    uint8_t status;
    DS3231_Status ret;

    if (hrtc == NULL || osf_set == NULL) return DS3231_ERR_PARAM;
    if (!hrtc->initialized)              return DS3231_ERR_PARAM;

    ret = ds3231_clod_read_reg(hrtc, DS3231_REG_STATUS, &status);
    if (ret != DS3231_OK) return ret;

    *osf_set = (status & DS3231_STAT_OSF) ? true : false;
    return DS3231_OK;
}

DS3231_Status DS3231_clod_ClearOscillatorStopFlag(DS3231_t *hrtc)
{
    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;
    return ds3231_clod_modify_reg(hrtc, DS3231_REG_STATUS, DS3231_STAT_OSF, 0);
}

DS3231_Status DS3231_clod_SetOscillator(DS3231_t *hrtc, bool enable)
{
    if (hrtc == NULL || !hrtc->initialized) return DS3231_ERR_PARAM;
    /*
     * EOSC=0 → oscylator uruchomiony (enable=true → EOSC=0)
     * EOSC=1 → oscylator zatrzymany w trybie VBAT
     */
    uint8_t value = enable ? 0 : DS3231_CTRL_EOSC;
    return ds3231_clod_modify_reg(hrtc, DS3231_REG_CONTROL, DS3231_CTRL_EOSC, value);
}

DS3231_Status DS3231_clod_ReadControlReg(DS3231_t *hrtc, uint8_t *value)
{
    if (hrtc == NULL || value == NULL) return DS3231_ERR_PARAM;
    if (!hrtc->initialized)            return DS3231_ERR_PARAM;
    return ds3231_clod_read_reg(hrtc, DS3231_REG_CONTROL, value);
}

DS3231_Status DS3231_clod_ReadStatusReg(DS3231_t *hrtc, uint8_t *value)
{
    if (hrtc == NULL || value == NULL) return DS3231_ERR_PARAM;
    if (!hrtc->initialized)            return DS3231_ERR_PARAM;
    return ds3231_clod_read_reg(hrtc, DS3231_REG_STATUS, value);
}