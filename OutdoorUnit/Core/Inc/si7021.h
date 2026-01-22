#ifndef SI7021_H
#define SI7021_H

#include "main.h"

/**
 * @brief Struktura konfiguracyjna dla czujnika Si7021
 */

typedef struct {
    float humidity;
    float temperature;
    uint8_t resolution;
    uint8_t heater_current;
} Si7021_Measurement_t;

typedef struct {
    I2C_HandleTypeDef *hi2c;  	// Uchwyt I2C z HAL
    uint8_t address;          	// Adres I2C czujnika (domyślnie 0x40)
    uint8_t firmware;
    Si7021_Measurement_t data; // wskaźnik do danych pomiarowych
} Si7021_t;


/**
 * @brief Enumeracja możliwych rozdzielczości pomiaru
 */
typedef enum {
    SI7021_RESOLUTION_RH12_TEMP14 = 0,  // RH: 12-bit, Temp: 14-bit
    SI7021_RESOLUTION_RH8_TEMP12  = 1,  // RH: 8-bit, Temp: 12-bit
    SI7021_RESOLUTION_RH10_TEMP13 = 2,  // RH: 10-bit, Temp: 13-bit
    SI7021_RESOLUTION_RH11_TEMP11 = 3   // RH: 11-bit, Temp: 11-bit
} Si7021_Resolution_t;

/**
 * @brief Enumeracja komend I2C dla Si7021 (Tabela 11, str. 18)
 */
typedef enum {
    SI7021_CMD_MEASURE_RH_HOLD    = 0xE5,  // Measure Relative Humidity, Hold Master Mode
    SI7021_CMD_MEASURE_RH_NOHOLD  = 0xF5,  // Measure Relative Humidity, No Hold Master Mode
    SI7021_CMD_MEASURE_TEMP_HOLD  = 0xE3,  // Measure Temperature, Hold Master Mode
    SI7021_CMD_MEASURE_TEMP_NOHOLD = 0xF3, // Measure Temperature, No Hold Master Mode
    SI7021_CMD_READ_TEMP_PREV_RH  = 0xE0,  // Read Temperature Value from Previous RH Measurement
    SI7021_CMD_RESET              = 0xFE,  // Reset
    SI7021_CMD_WRITE_USER_REG1    = 0xE6,  // Write RH/T User Register 1
    SI7021_CMD_READ_USER_REG1     = 0xE7,  // Read RH/T User Register 1
    SI7021_CMD_WRITE_HEATER_REG   = 0x51,  // Write Heater Control Register
    SI7021_CMD_READ_HEATER_REG    = 0x11,  // Read Heater Control Register
    SI7021_CMD_READ_EID_1ST       = 0xFC0F,// Read Electronic ID 1st Byte
    SI7021_CMD_READ_EID_2ND       = 0xFCC9,// Read Electronic ID 2nd Byte
    SI7021_CMD_READ_FIRMWARE      = 0x84B8 // Read Firmware Revision
} Si7021_Command_t;

/**
 * @brief Stałe do obliczenia nastawy prądu grzałki
 * Wartości dla bitów D3:fChan0 w rejestrze Heater Control Register. Prąd wyznaczony dla VDD = 3.3V
 *  mA      D3-fChan0
 *  3.09    0000
 *  9.18    0001
 *  15.24   0010
 *  21.32   0011
 *  27.395  0100
 *  33.47   0101
 *  39.545  0110
 *  45.62   0111
 *  51.695  1000
 *  57.77   1001
 *  63.845  1010
 *  69.92   1011
 *  75.995  1100
 *  82.07   1101
 *  88.145  1110
 *  94.22   1111
 */
typedef enum {
    SI7021_HEATER_MIN_CURRENT      = 3,
    SI7021_HEATER_CURRENT_OFFSET   = 6
} Si7021_Heater_t;

/**
 * @brief Funkcje biblioteki
 */

HAL_StatusTypeDef Si7021_ReadRegister(Si7021_t *hsi7021, Si7021_Command_t reg_cmd, uint8_t *value, uint8_t size);
HAL_StatusTypeDef Si7021_WriteRegister(Si7021_t *hsi7021, Si7021_Command_t reg_cmd, uint8_t value);

HAL_StatusTypeDef Si7021_ReadFirmware(Si7021_t *hsi7021);
HAL_StatusTypeDef Si7021_SoftwareReset(Si7021_t *hsi7021);

HAL_StatusTypeDef Si7021_Init(Si7021_t *hsi7021, I2C_HandleTypeDef *hi2c, uint8_t address, Si7021_Resolution_t resolution);

HAL_StatusTypeDef Si7021_SetResolution(Si7021_t *hsi7021, Si7021_Resolution_t resolution);
HAL_StatusTypeDef Si7021_GetResolution(Si7021_t *hsi7021);

HAL_StatusTypeDef Si7021_SetHeaterCurrent(Si7021_t *hsi7021, uint8_t current);
HAL_StatusTypeDef Si7021_GetHeaterCurrent(Si7021_t *hsi7021);

HAL_StatusTypeDef Si7021_ReadHumidity(Si7021_t *hsi7021);
HAL_StatusTypeDef Si7021_ReadTemperature(Si7021_t *hsi7021);
HAL_StatusTypeDef Si7021_ReadHumidityAndTemperature(Si7021_t *hsi7021);

#endif
