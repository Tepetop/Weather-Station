#ifndef DS3231_H
#define DS3231_H

#include <main.h>
#include <stdint.h>

typedef enum {
    DS3231_BLOKCING_MODE = 0,
    DS3231_IT_MODE,
    DS3231_DMA_MODE
} DS3231_IOMODE_t;

/**
 * @brief DS3231 register addresses. Time and calendar data are in BCD format.
 */
typedef enum {
    DS3231_REG_SECONDS     = 0x00,
    DS3231_REG_MINUTES     = 0x01,
    DS3231_REG_HOURS       = 0x02,
    DS3231_REG_DAY         = 0x03,
    DS3231_REG_DATE        = 0x04,
    DS3231_REG_MONTH       = 0x05,
    DS3231_REG_YEAR        = 0x06,
    DS3231_REG_ALARM1_SEC  = 0x07,
    DS3231_REG_ALARM1_MIN  = 0x08,
    DS3231_REG_ALARM1_HOUR = 0x09,
    DS3231_REG_ALARM1_DAY  = 0x0A,
    DS3231_REG_ALARM2_MIN  = 0x0B,
    DS3231_REG_ALARM2_HOUR = 0x0C,
    DS3231_REG_ALARM2_DAY  = 0x0D,
    DS3231_REG_CONTROL     = 0x0E,
    DS3231_REG_STATUS      = 0x0F,
    DS3231_REG_AGING       = 0x10,
    DS3231_REG_TEMP_MSB    = 0x11,
    DS3231_REG_TEMP_LSB    = 0x12
} DS3231_Registers_t;

/**
 * @brief Control register bits (0x0E).
 */
typedef enum {
    DS3231_CTRL_A1IE  = (1 << 0),
    DS3231_CTRL_A2IE  = (1 << 1),
    DS3231_CTRL_INTCN = (1 << 2),
    DS3231_CTRL_RS1   = (1 << 3),
    DS3231_CTRL_RS2   = (1 << 4),
    DS3231_CTRL_CONV  = (1 << 5),
    DS3231_CTRL_BBSQW = (1 << 6),
    DS3231_CTRL_EOSC  = (1 << 7)
} DS3231_Control_t;

/**
 * @brief Status register bits (0x0F).
 */
typedef enum {
    DS3231_STAT_A1F     = (1 << 0),
    DS3231_STAT_A2F     = (1 << 1),
    DS3231_STAT_BSY     = (1 << 2),
    DS3231_STAT_EN32KHZ = (1 << 3),
    DS3231_STAT_OSF     = (1 << 7)
} DS3231_Status_t;

typedef enum {
    DS3231_SQW_RATE_1HZ = 0,
    DS3231_SQW_RATE_1024HZ = 1,
    DS3231_SQW_RATE_4096HZ = 2,
    DS3231_SQW_RATE_8192HZ = 3
} DS3231_SQWRATE_t;

/**
 * @brief Time and date structure.
 */
typedef struct {
    uint16_t Year;
    uint8_t Month;
    uint8_t Day;
    uint8_t Hour;
    uint8_t Minute;
    uint8_t Second;
    uint8_t DayOfWeek;
} DS3231_RTCDateTime_t;

/**
 * @brief Alarm time structure.
 */
typedef struct {
    uint8_t Day;
    uint8_t Hour;
    uint8_t Minute;
    uint8_t Second;
} DS3231_RTCAlarmTime;

/* Tryby alarmu (możesz używać różnych enumów dla Alarm1 i Alarm2) */
typedef enum {
    DS3231_ALARM_EVERY_SECOND,      // tylko Alarm1
    DS3231_ALARM_EVERY_MINUTE,      // tylko Alarm2
    DS3231_ALARM_SECONDS_MATCH,     // Alarm1
    DS3231_ALARM_MINUTES_MATCH,
    DS3231_ALARM_HOURS_MATCH,
    DS3231_ALARM_DATE_MATCH,        // alarm w konkretny dzień miesiąca
    DS3231_ALARM_DAY_MATCH          // alarm w konkretny dzień tygodnia
} DS3231_AlarmMode_t;

/**
 * @brief Main DS3231 device handle.
 */
typedef struct {
    I2C_HandleTypeDef *hi2c;
    GPIO_TypeDef *sqw_port;
    uint16_t sqw_pin;
    DS3231_RTCDateTime_t time;
    DS3231_IOMODE_t mode;
    uint8_t address;
    uint8_t DS3231_IRQ_Flag; // Flaga przerwania alarmu
} DS3231_t;


/**
 * @brief Initialize the DS3231 device handle.
 * @param dev Pointer to the DS3231 device handle.
 * @param hi2c Pointer to HAL I2C handle.
 * @param address 7-bit I2C address (typically 0x68).
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_Init(DS3231_t *dev, I2C_HandleTypeDef *hi2c, uint8_t address, GPIO_TypeDef *sqw_port, uint16_t sqw_pin);

/**
 * @brief Read current time/date from the DS3231 into the device handle.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_ReadTime(DS3231_t *dev);

/**
 * @brief Trigger a temperature conversion.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_ConvertTemperature(DS3231_t *dev);

/**
 * @brief Enable or disable the oscillator.
 * @param dev Pointer to the DS3231 device handle.
 * @param enable 1 to enable, 0 to disable.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_Oscillator(DS3231_t *dev, uint8_t enable);

/**
 * @brief Enable or disable battery-backed square wave output.
 * @param dev Pointer to the DS3231 device handle.
 * @param enable 1 to enable, 0 to disable.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_BatteryBackerSQW(DS3231_t *dev, uint8_t enable);

/**
 * @brief Configure the square wave output frequency.
 * @param dev Pointer to the DS3231 device handle.
 * @param rate Desired square wave frequency.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_SetSQWRate(DS3231_t *dev, DS3231_SQWRATE_t rate);

/**
 * @brief Enable or disable interrupt output mode (INTCN bit).
 * @param dev Pointer to the DS3231 device handle.
 * @param enable 1 to enable, 0 to disable.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_EnableInterrupt(DS3231_t *dev, uint8_t enable);

/**
 * @brief Enable or disable a selected alarm interrupt.
 * @param dev Pointer to the DS3231 device handle.
 * @param enable 1 to enable, 0 to disable.
 * @param alarmNumber Alarm index (1 or 2).
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_Alarm_InterruptEnable(DS3231_t *dev, uint8_t enable, uint8_t alarmNumber);

/**
 * @brief Program alarm time registers.
 * @param dev Pointer to the DS3231 device handle.
 * @param alarm Pointer to alarm time structure.
 * @param alarmMode Alarm match mode.
 * @param alarmNumber Alarm index (1 or 2).
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_SetAlarm(DS3231_t *dev, DS3231_RTCAlarmTime *alarm, DS3231_AlarmMode_t alarmMode, uint8_t alarmNumber);

/**
 * @brief Configure oscillator and square wave settings.
 * @param dev Pointer to the DS3231 device handle.
 * @param enable 1 to enable oscillator, 0 to disable.
 * @param batteryBackedSqw 1 to enable battery-backed SQW, 0 to disable.
 * @param frequency Desired SQW output frequency.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_TurnOnOscillator(DS3231_t *dev, uint8_t enable, uint8_t batteryBackedSqw, DS3231_SQWRATE_t frequency);

/**
 * @brief Enable or disable 32 kHz output.
 * @param dev Pointer to the DS3231 device handle.
 * @param enable 1 to enable, 0 to disable.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_Enable32kHzOutput(DS3231_t *dev, uint8_t enable);

/**
 * @brief Clear alarm flags in the DS3231 status register.
 * @param dev Pointer to the DS3231 device handle.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_ClearAlarmFlags(DS3231_t *dev);

/**
 * @brief Read date/time from the DS3231 into the provided structure.
 * @param dev Pointer to the DS3231 device handle.
 * @param dateTime Pointer to the destination structure.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_GetDateTime(DS3231_t *dev);

/**
 * @brief Write date/time to the DS3231 from the provided structure.
 * @param dev Pointer to the DS3231 device handle.
 * @param dateTime Pointer to the source structure.
 * @return HAL status.
 */
HAL_StatusTypeDef DS3231_SetDateTime(DS3231_t *dev, const DS3231_RTCDateTime_t *dateTime);



#endif // DS3231_H