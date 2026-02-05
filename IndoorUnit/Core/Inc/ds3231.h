#ifndef DS3231_H
#define DS3231_H


#include <main.h>
#include <stdint.h>


typedef enum{
    DS3231_BLOKCING_MODE = 0,
    DS3231_IT_MODE,
    DS3231_DMA_MODE
}DS3231_IOMODE_t;

/**
 * @brief Adresy rejestrów DS3231. Wszystkie dane czasu i kalendarza są przechowywane w formacie BCD (Binary-Coded Decimal)
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
 * @brief Bity rejestru kontrolnego (0Eh)
 * Steruje pracą oscylatora, alarmów i wyjścia SQW.
 */
typedef enum {
    DS3231_CTRL_A1IE  = (1 << 0), // Alarm 1 Interrupt Enable
    DS3231_CTRL_A2IE  = (1 << 1), // Alarm 2 Interrupt Enable
    DS3231_CTRL_INTCN = (1 << 2), // Interrupt Control (1=INT, 0=SQW)
    DS3231_CTRL_RS1   = (1 << 3), // Rate Select 1
    DS3231_CTRL_RS2   = (1 << 4), // Rate Select 2
    DS3231_CTRL_CONV  = (1 << 5), // Convert Temperature
    DS3231_CTRL_BBSQW = (1 << 6), // Battery-Backed Square-Wave Enable
    DS3231_CTRL_EOSC  = (1 << 7)  // Enable Oscillator (active low)
} DS3231_Control_t;

/**
 * @brief Bity rejestru statusowego (0Fh)
 * Zawiera flagi zdarzeń i stan urządzenia.
 */
typedef enum {
    DS3231_STAT_A1F     = (1 << 0), // Alarm 1 Flag
    DS3231_STAT_A2F     = (1 << 1), // Alarm 2 Flag
    DS3231_STAT_BSY     = (1 << 2), // Busy (Busy executing TCXO functions)
    DS3231_STAT_EN32KHZ = (1 << 3), // Enable 32kHz Output
    DS3231_STAT_OSF     = (1 << 7)  // Oscillator Stop Flag
} DS3231_Status_t;

typedef enum
{
	DS3231_SQW_RATE_1HZ = 0,
	DS3231_SQW_RATE_1024HZ = 1,
	DS3231_SQW_RATE_4096HZ = 2,
	DS3231_SQW_RATE_8192HZ = 3
}DS3231_SQWRATE_t;

/**
 * @brief Struktura przechowująca dane czasu i daty
 */
typedef struct {
	uint16_t 	Year;
	uint8_t  	Month;
	uint8_t		Day;
	uint8_t		Hour;
	uint8_t		Minute;
	uint8_t		Second;
	uint8_t		DayOfWeek;
} DS3231_RTCDateTime_t;

/**
 * @brief Struktura przechowująca dane czasu alarmu
 */
typedef struct
{
	uint8_t		Day;
	uint8_t		Hour;
	uint8_t		Minute;
	uint8_t		Second;
} DS3231_RTCAlarmTime;

/**
 * @brief Główny handler urządzenia DS3231
 */
typedef struct {
    I2C_HandleTypeDef 			*hi2c; 		// I2C handle
    uint8_t 					address;    // I2C address (shifted left by 1)
    DS3231_RTCDateTime_t        time; // Struktura z aktualnym czasem
    DS3231_IOMODE_t             mode;     // Tryb komunikacji I/O
} DS3231_t;

HAL_StatusTypeDef DS3231_Init(DS3231_t *dev, I2C_HandleTypeDef *hi2c, uint8_t address);
HAL_StatusTypeDef DS3231_ReadTime(DS3231_t *dev);
#endif // DS3231_H