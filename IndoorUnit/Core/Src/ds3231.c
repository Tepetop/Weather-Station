#include "ds3231.h"
#include "stm32f1xx_hal_def.h"


/* Internal functions*/
static uint8_t DS3231_BcdToDec(uint8_t bcd)
{
    return (uint8_t)((((bcd & 0xF0U) >> 4U) * 10U) + (bcd & 0x0FU));
}

static uint8_t DS3231_DecToBcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10U) << 4U) + (dec % 10U));
}


/* Read buffer from a DS3231 register address. */
static HAL_StatusTypeDef DS3231_Read(DS3231_t *dev, uint8_t reg, uint8_t *buffer, uint16_t length)
{
    if ((dev == NULL) || (buffer == NULL) || (length == 0U)) {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(dev->hi2c, dev->address, reg, 1, buffer, length, HAL_MAX_DELAY);
}
/* Write the DS3231 control register value. */
static HAL_StatusTypeDef DS3231_SetControlRegister(DS3231_t *dev, uint8_t controlValue)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Write(dev->hi2c, dev->address, DS3231_REG_CONTROL, 1, &controlValue, 1, HAL_MAX_DELAY);
}
/* Write buffer to a DS3231 register address. */
static HAL_StatusTypeDef DS3231_Write(DS3231_t *dev, uint8_t reg, const uint8_t *buffer, uint16_t length)
{
    if ((dev == NULL) || (buffer == NULL) || (length == 0U)) {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Write(dev->hi2c, dev->address, reg, 1, (uint8_t *)buffer, length, HAL_MAX_DELAY);
}
/* Read the DS3231 control register value. */
static HAL_StatusTypeDef DS3231_GetControlRegister(DS3231_t *dev, uint8_t *controlValue)
{
    if ((dev == NULL) || (controlValue == NULL)) {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(dev->hi2c, dev->address, DS3231_REG_CONTROL, 1, controlValue, 1, HAL_MAX_DELAY);
}

/* Write the DS3231 status register value. */
static HAL_StatusTypeDef DS3231_SetStatusRegister(DS3231_t *dev, uint8_t statusValue)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Write(dev->hi2c, dev->address, DS3231_REG_STATUS, 1, &statusValue, 1, HAL_MAX_DELAY);
}

/* Read the DS3231 status register value. */
static HAL_StatusTypeDef DS3231_GetStatusRegister(DS3231_t *dev, uint8_t *statusValue)
{
    if ((dev == NULL) || (statusValue == NULL)) {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(dev->hi2c, dev->address, DS3231_REG_STATUS, 1, statusValue, 1, HAL_MAX_DELAY);
}

/* Update selected control register bits with a masked value. */
static HAL_StatusTypeDef DS3231_UpdateControlRegister(DS3231_t *dev, uint8_t mask, uint8_t value)
{
    uint8_t controlReg = 0;
    HAL_StatusTypeDef status = DS3231_GetControlRegister(dev, &controlReg);
    if (status != HAL_OK) {
        return status;
    }

    controlReg = (uint8_t)((controlReg & (uint8_t)(~mask)) | (value & mask));
    return DS3231_SetControlRegister(dev, controlReg);
}

/* Set or clear a single control register bit. */
static HAL_StatusTypeDef DS3231_WriteControlBit(DS3231_t *dev, uint8_t bitMask, uint8_t enable)
{
    return DS3231_UpdateControlRegister(dev, bitMask, enable ? bitMask : 0U);
}

/**
 * @brief Trigger a temperature conversion.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_ConvertTemperature(DS3231_t *dev)
{
    return DS3231_WriteControlBit(dev, DS3231_CTRL_CONV, 1);
}

/**
 * @brief Enable the oscillator.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 * @note EOSC bit is inverted: 0=enabled, 1=disabled
 */
HAL_StatusTypeDef DS3231_EnableOscillator(DS3231_t *dev)
{
    return DS3231_WriteControlBit(dev, DS3231_CTRL_EOSC, 0U);
}

/**
 * @brief Disable the oscillator.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 * @note EOSC bit is inverted: 0=enabled, 1=disabled
 */
HAL_StatusTypeDef DS3231_DisableOscillator(DS3231_t *dev)
{
    return DS3231_WriteControlBit(dev, DS3231_CTRL_EOSC, 1U);
}

/**
 * @brief Enable battery-backed square wave output.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_EnableBatteryBackedSQW(DS3231_t *dev)
{
    return DS3231_WriteControlBit(dev, DS3231_CTRL_BBSQW, 1U);
}

/**
 * @brief Disable battery-backed square wave output.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_DisableBatteryBackedSQW(DS3231_t *dev)
{
    return DS3231_WriteControlBit(dev, DS3231_CTRL_BBSQW, 0U);
}

/**
 * @brief Configure the square wave output frequency.
 * @param dev Pointer to the DS3231 device handle.
 * @param rate Desired square wave frequency.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_SetSQWRate(DS3231_t *dev, DS3231_SQWRATE_t rate)
{
    uint8_t rateBits = (uint8_t)((uint8_t)(rate & 0x03U) << 3U);
    return DS3231_UpdateControlRegister(dev, (uint8_t)(DS3231_CTRL_RS1 | DS3231_CTRL_RS2), rateBits);
}

/**
 * @brief Enable interrupt output mode (INTCN bit).
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_EnableInterrupt(DS3231_t *dev)
{
    return DS3231_WriteControlBit(dev, DS3231_CTRL_INTCN, 1U);
}

/**
 * @brief Disable interrupt output mode (INTCN bit).
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_DisableInterrupt(DS3231_t *dev)
{
    return DS3231_WriteControlBit(dev, DS3231_CTRL_INTCN, 0U);
}

/**
 * @brief Enable a selected alarm interrupt.
 * @param dev Pointer to the DS3231 device handle.
 * @param alarmNumber Alarm index (1 or 2).
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_EnableAlarmInterrupt(DS3231_t *dev, uint8_t alarmNumber)
{
    HAL_StatusTypeDef status = HAL_OK;

    if (dev == NULL) {
        return HAL_ERROR;
    }

    status = DS3231_EnableInterrupt(dev);
    if (status != HAL_OK) {
        return status;
    }

    if (alarmNumber == 1) {
        return DS3231_WriteControlBit(dev, DS3231_CTRL_A1IE, 1U);
    }

    if (alarmNumber == 2) {
        return DS3231_WriteControlBit(dev, DS3231_CTRL_A2IE, 1U);
    }

    return HAL_ERROR;
}

/**
 * @brief Disable a selected alarm interrupt.
 * @param dev Pointer to the DS3231 device handle.
 * @param alarmNumber Alarm index (1 or 2).
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_DisableAlarmInterrupt(DS3231_t *dev, uint8_t alarmNumber)
{
    if (dev == NULL) {
        return HAL_ERROR;
    }

    if (alarmNumber == 1) {
        return DS3231_WriteControlBit(dev, DS3231_CTRL_A1IE, 0U);
    }

    if (alarmNumber == 2) {
        return DS3231_WriteControlBit(dev, DS3231_CTRL_A2IE, 0U);
    }

    return HAL_ERROR;
}

/**
 * @brief Program alarm time registers.
 * @param dev Pointer to the DS3231 device handle.
 * @param alarm Pointer to alarm time structure.
 * @param alarmNumber Alarm index (1 or 2).
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_SetAlarm(DS3231_t *dev, DS3231_RTCAlarmTime *alarm, DS3231_AlarmMode_t alarmMode, uint8_t alarmNumber)
{
    uint8_t reg = 0U;
    uint8_t buf[4] = {0U};
    uint8_t len = 0U;
    HAL_StatusTypeDef status = HAL_OK;

    if ((dev == NULL) || (alarm == NULL)) {
        return HAL_ERROR;
    }

    if ((alarmNumber != 1U) && (alarmNumber != 2U)) {
        return HAL_ERROR;
    }

    if (alarmNumber == 1U) {
        reg = DS3231_REG_ALARM1_SEC;
        len = 4U;

        switch (alarmMode)
        {
            case DS3231_ALARM_EVERY_SECOND:
                buf[0] = 0x80; 
                buf[1] = 0x80; 
                buf[2] = 0x80; 
                buf[3] = 0x80;
                break;

            case DS3231_ALARM_SECONDS_MATCH:
                buf[0] = DS3231_DecToBcd(alarm->Second) & 0x7F; 
                buf[1] = 0x80; 
                buf[2] = 0x80; 
                buf[3] = 0x80;
                break;

            case DS3231_ALARM_MINUTES_MATCH:
                buf[0] = DS3231_DecToBcd(alarm->Second) & 0x7F; 
                buf[1] = DS3231_DecToBcd(alarm->Minute) & 0x7F; 
                buf[2] = 0x80; 
                buf[3] = 0x80;
                break;

            case DS3231_ALARM_HOURS_MATCH:
                buf[0] = DS3231_DecToBcd(alarm->Second) & 0x7F;
                buf[1] = DS3231_DecToBcd(alarm->Minute) & 0x7F;
                buf[2] = DS3231_DecToBcd(alarm->Hour) & 0x7F;
                buf[3] = 0x80;
                break;

            case DS3231_ALARM_DATE_MATCH:   // data miesiąca
                buf[0] = DS3231_DecToBcd(alarm->Second) & 0x7F;
                buf[1] = DS3231_DecToBcd(alarm->Minute) & 0x7F;
                buf[2] = DS3231_DecToBcd(alarm->Hour) & 0x7F;
                buf[3] = DS3231_DecToBcd(alarm->Day) & 0x7F;    // A1M4=0, DY/DT=0
                break;

            case DS3231_ALARM_DAY_MATCH:    // dzień tygodnia
                buf[0] = DS3231_DecToBcd(alarm->Second) & 0x7F;
                buf[1] = DS3231_DecToBcd(alarm->Minute) & 0x7F;
                buf[2] = DS3231_DecToBcd(alarm->Hour) & 0x7F;
                buf[3] = (DS3231_DecToBcd(alarm->Day) & 0x7F) | 0x40;  // A1M4=0, DY/DT=1
                break;

            default:
                return HAL_ERROR;
        }
    }
    else {
        reg = DS3231_REG_ALARM2_MIN;
        len = 3U;

        switch (alarmMode)
        {
            case DS3231_ALARM_EVERY_MINUTE:
                buf[0] = 0x80; 
                buf[1] = 0x80; 
                buf[2] = 0x80;
                break;

            case DS3231_ALARM_MINUTES_MATCH:
                buf[0] = DS3231_DecToBcd(alarm->Minute) & 0x7F; 
                buf[1] = 0x80; 
                buf[2] = 0x80;
                break;

            case DS3231_ALARM_HOURS_MATCH:
                buf[0] = DS3231_DecToBcd(alarm->Minute) & 0x7F; 
                buf[1] = DS3231_DecToBcd(alarm->Hour) & 0x7F; 
                buf[2] = 0x80;
                break;

            case DS3231_ALARM_DATE_MATCH:
                buf[0] = DS3231_DecToBcd(alarm->Minute) & 0x7F;
                buf[1] = DS3231_DecToBcd(alarm->Hour) & 0x7F;
                buf[2] = DS3231_DecToBcd(alarm->Day) & 0x7F;
                break;

            case DS3231_ALARM_DAY_MATCH:
                buf[0] = DS3231_DecToBcd(alarm->Minute) & 0x7F;
                buf[1] = DS3231_DecToBcd(alarm->Hour) & 0x7F;
                buf[2] = (DS3231_DecToBcd(alarm->Day) & 0x7F) | 0x40;
                break;

            default:
                return HAL_ERROR;
        }
    }

    /* Zapis rejestrów alarmu */
    status = DS3231_Write(dev, reg, buf, len);
    if (status != HAL_OK) {
        return status;
    }

    /* Wyczyszczenie flagi alarmu (ważne!) */
    uint8_t statusReg = 0U;
    status = DS3231_Read(dev, DS3231_REG_STATUS, &statusReg, 1U);
    if (status != HAL_OK) {
        return status;
    }

    statusReg &= (uint8_t)(~(alarmNumber == 1U ? 0x01U : 0x02U));
    status = DS3231_Write(dev, DS3231_REG_STATUS, &statusReg, 1U);
    if (status != HAL_OK) {
        return status;
    }

    /* Ustawienie Control Register (0x0E) – włączamy alarm + tryb pinu z dev->mode */
    status = DS3231_EnableAlarmInterrupt(dev, alarmNumber);
    if (status != HAL_OK) {
        return status;
    }

    return HAL_OK;
}

/**
 * @brief Configure oscillator and square wave settings.
 * @param dev Pointer to the DS3231 device handle.
 * @param enable 1 to enable oscillator, 0 to disable.
 * @param batteryBackedSqw 1 to enable battery-backed SQW, 0 to disable.
 * @param frequency Desired SQW output frequency.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_TurnOnOscillator(DS3231_t *dev, uint8_t enable, uint8_t batteryBackedSqw, DS3231_SQWRATE_t frequency)
{
    HAL_StatusTypeDef status = HAL_OK;

    if (dev == NULL) {
        return HAL_ERROR;
    }

    status = batteryBackedSqw ? DS3231_EnableBatteryBackedSQW(dev) : DS3231_DisableBatteryBackedSQW(dev);
    if (status != HAL_OK) {
        return status;
    }

    if (enable != 0U) {
        status = DS3231_EnableOscillator(dev);
        if (status != HAL_OK) {
            return status;
        }
        status = DS3231_DisableInterrupt(dev);
        if (status != HAL_OK) {
            return status;
        }
    } else {
        status = DS3231_DisableOscillator(dev);
        if (status != HAL_OK) {
            return status;
        }
    }

    return DS3231_SetSQWRate(dev, frequency);
}

/**
 * @brief Enable 32 kHz output.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_Enable32kHzOutput(DS3231_t *dev)
{
    uint8_t statusReg = 0U;
    HAL_StatusTypeDef status = HAL_OK;

    if (dev == NULL) {
        return HAL_ERROR;
    }

    status = DS3231_GetStatusRegister(dev, &statusReg);
    if (status != HAL_OK) {
        return status;
    }

    statusReg |= DS3231_STAT_EN32KHZ;

    return DS3231_SetStatusRegister(dev, statusReg);
}

/**
 * @brief Disable 32 kHz output.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_Disable32kHzOutput(DS3231_t *dev)
{
    uint8_t statusReg = 0U;
    HAL_StatusTypeDef status = HAL_OK;

    if (dev == NULL) {
        return HAL_ERROR;
    }

    status = DS3231_GetStatusRegister(dev, &statusReg);
    if (status != HAL_OK) {
        return status;
    }

    statusReg &= (uint8_t)(~DS3231_STAT_EN32KHZ);

    return DS3231_SetStatusRegister(dev, statusReg);
}

/**
 * @brief Check which alarm(s) fired, store result in dev->DS3231_IRQ_Flag,
 *        then clear only the triggered alarm flags in the status register.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_CheckAndClearAlarmFlags(DS3231_t *dev)
{
    uint8_t statusReg = 0U;
    HAL_StatusTypeDef status = HAL_OK;

    if (dev == NULL) {
        return HAL_ERROR;
    }

    /* Reset the software flag */
    dev->DS3231_IRQ_Flag = DS3231_IRQ_NONE;

    status = DS3231_GetStatusRegister(dev, &statusReg);
    if (status != HAL_OK) {
        return status;
    }

    /* Detect which alarm(s) fired */
    if ((statusReg & DS3231_STAT_A1F) != 0U) {
        dev->DS3231_IRQ_Flag |= DS3231_IRQ_ALARM1;
    }
    if ((statusReg & DS3231_STAT_A2F) != 0U) {
        dev->DS3231_IRQ_Flag |= DS3231_IRQ_ALARM2;
    }

    /* Clear only the flags that were set */
    if (dev->DS3231_IRQ_Flag != DS3231_IRQ_NONE) {
        statusReg &= (uint8_t)(~(DS3231_STAT_A1F | DS3231_STAT_A2F));
        status = DS3231_SetStatusRegister(dev, statusReg);
    }

    return status;
}


static uint8_t DS3231_DayOfWeek(uint8_t day, uint8_t month, uint16_t year)
{
    int y = (int)year;
    int m = (int)month;
    int d = (int)day;
    int yy = y - ((m <= 2) ? 1 : 0);
    int mm = m + ((m <= 2) ? 12 : 0);
    int k = yy % 100;
    int j = yy / 100;
    int h = (d + (13 * (mm + 1)) / 5 + k + k / 4 + j / 4 + (5 * j)) % 7;
    int dow = (h + 6) % 7;

    return (uint8_t)dow;
}

static void DS3231_DecodeDateTime(DS3231_t *dev, const uint8_t *buffer)
{
    dev->time.Second = DS3231_BcdToDec(buffer[0]);
    dev->time.Minute = DS3231_BcdToDec(buffer[1]);
    dev->time.Hour = DS3231_BcdToDec((uint8_t)(buffer[2] & 0x3FU));
    dev->time.DayOfWeek = buffer[3];
    dev->time.Day = DS3231_BcdToDec(buffer[4]);
    dev->time.Month = DS3231_BcdToDec((uint8_t)(buffer[5] & 0x1FU));
    dev->time.Year = (uint16_t)(2000U + DS3231_BcdToDec(buffer[6]));
}

/**
 * @brief Read date/time from the DS3231 into the provided structure.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_GetDateTime(DS3231_t *dev)
{
    if (dev == NULL)
    {
        return HAL_ERROR;
    }
    uint8_t buffer[7] = {0U};
    HAL_StatusTypeDef status = HAL_OK;

    status = DS3231_Read(dev, DS3231_REG_SECONDS, buffer, 7U);
    if (status != HAL_OK) {
        return status;
    }

    DS3231_DecodeDateTime(dev, buffer);
    return HAL_OK;
}

/**
 * @brief Write date/time to the DS3231 from the provided structure.
 * @param dev Pointer to the DS3231 device handle.
 * @param dateTime Pointer to the source structure.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_SetDateTime(DS3231_t *dev, const DS3231_RTCDateTime_t *dateTime)
{
    DS3231_RTCDateTime_t tmp = {0U};
    uint8_t buffer[7] = {0U};

    if ((dev == NULL) || (dateTime == NULL)) {
        return HAL_ERROR;
    }

    tmp = *dateTime;
    if (tmp.Second > 59U) tmp.Second = 59U;
    if (tmp.Minute > 59U) tmp.Minute = 59U;
    if (tmp.Hour > 23U) tmp.Hour = 23U;
    if (tmp.Day > 31U) tmp.Day = 31U;
    if (tmp.Month > 12U) tmp.Month = 12U;
    if (tmp.Year < 2000U) tmp.Year = 2000U;
    if (tmp.Year > 2099U) tmp.Year = 2099U;

    buffer[0] = DS3231_DecToBcd(tmp.Second);
    buffer[1] = DS3231_DecToBcd(tmp.Minute);
    buffer[2] = DS3231_DecToBcd(tmp.Hour);
    buffer[3] = (uint8_t)(DS3231_DayOfWeek(tmp.Day, tmp.Month, tmp.Year) + 1U);
    buffer[4] = DS3231_DecToBcd(tmp.Day);
    buffer[5] = DS3231_DecToBcd(tmp.Month);
    buffer[6] = DS3231_DecToBcd((uint8_t)(tmp.Year - 2000U));

    return DS3231_Write(dev, DS3231_REG_SECONDS, buffer, 7U);
}


/**
 * @brief Initialize DS3231 device handle.
 * @param dev Pointer to the DS3231 device handle.
 * @param hi2c Pointer to HAL I2C handle.
 * @param address 7-bit I2C address (typically 0x68).
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_Init(DS3231_t *dev, I2C_HandleTypeDef *hi2c, uint8_t address, GPIO_TypeDef *sqw_port, uint16_t sqw_pin)
{
    if ((dev == NULL) || (hi2c == NULL)) {
        return HAL_ERROR;
    }

    /* Fill the handle struct BEFORE any I2C communication */
    dev->hi2c = hi2c;
    dev->address = (uint8_t)(address << 1);
    dev->mode = DS3231_BLOKCING_MODE;
    dev->DS3231_IRQ_Flag = DS3231_IRQ_NONE;
    dev->sqw_port = sqw_port;
    dev->sqw_pin = sqw_pin;

    /* Verify I2C communication by reading the control register */
    uint8_t ctrlReg = 0U;
    HAL_StatusTypeDef status = DS3231_GetControlRegister(dev, &ctrlReg);
    if (status != HAL_OK) {
        return status;
    }

    /*
     * Default configuration at init:
     *  - EOSC  = 0  -> oscillator running
     *  - INTCN = 1  -> INT/SQW pin as interrupt output (POR default)
     *  - A1IE  = 0, A2IE = 0 -> alarm interrupts disabled
     *  - BBSQW = 0  -> SQW disabled on battery
     *  - RS2=1, RS1=1 -> SQW 8.192 kHz (irrelevant when INTCN=1)
     *
     * Time registers are NOT reset — preserved if oscillator was running.
     */
    ctrlReg = DS3231_CTRL_INTCN | DS3231_CTRL_RS2 | DS3231_CTRL_RS1;
    status = DS3231_SetControlRegister(dev, ctrlReg);
    if (status != HAL_OK) {
        return status;
    }

    /* Clear any pending alarm flags */
    status = DS3231_CheckAndClearAlarmFlags(dev);

    return status;
}

