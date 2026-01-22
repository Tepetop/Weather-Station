#include "si7021.h"

/**
 * @brief Odczyt rejestru czujnika
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną
 * @param reg_cmd Komenda odczytu rejestru lub pomiaru (np. SI7021_CMD_READ_USER_REG1, SI7021_CMD_MEASURE_RH_HOLD)
 * @param value Wskaźnik na bufor przechowujący odczytane dane
 * @param size Liczba bajtów do odczytania
 * @return Status operacji
 */
HAL_StatusTypeDef Si7021_ReadRegister(Si7021_t *hsi7021, Si7021_Command_t reg_cmd, uint8_t *value, uint8_t size)
{
    if (hsi7021 == NULL || value == NULL)
        return HAL_ERROR;

    // Dla komend dwubajtowych (EID i firmware)
    if (reg_cmd == SI7021_CMD_READ_EID_1ST || reg_cmd == SI7021_CMD_READ_EID_2ND || reg_cmd == SI7021_CMD_READ_FIRMWARE)
    {
        uint8_t cmd[2] = {(reg_cmd >> 8) & 0xFF, reg_cmd & 0xFF};
        HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(hsi7021->hi2c, hsi7021->address, cmd, 2, HAL_MAX_DELAY);
        if (status != HAL_OK)
            return status;
        return HAL_I2C_Master_Receive(hsi7021->hi2c, hsi7021->address | 1, value, size, HAL_MAX_DELAY);
    }

    // Dla komend jednobajtowych (rejestry i pomiary)
    uint8_t cmd = (uint8_t)reg_cmd;
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(hsi7021->hi2c, hsi7021->address, &cmd, 1, HAL_MAX_DELAY);
    if (status != HAL_OK)
        return status;

    return HAL_I2C_Master_Receive(hsi7021->hi2c, hsi7021->address | 1, value, size, HAL_MAX_DELAY);
}

/**
 * @brief Zapis do rejestru czujnika
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną
 * @param reg_cmd Komenda zapisu rejestru (np. SI7021_CMD_WRITE_USER_REG1)
 * @param value Wartość do zapisania
 * @return Status operacji
 */
HAL_StatusTypeDef Si7021_WriteRegister(Si7021_t *hsi7021, Si7021_Command_t reg_cmd, uint8_t value)
{
    return HAL_I2C_Mem_Write(hsi7021->hi2c, hsi7021->address, reg_cmd, 1, &value, 1, HAL_MAX_DELAY);
}

/**
 * @brief Odczytanie ustawień fabrycznych
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną
 * @return Status operacji
 */
HAL_StatusTypeDef Si7021_ReadFirmware(Si7021_t *hsi7021)
{
	return Si7021_ReadRegister(hsi7021, SI7021_CMD_READ_FIRMWARE, &(hsi7021->firmware), 2);
}

/**
 * @brief Programowy reset czujnika do ustawień fabrycznych
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną
 * @return Status operacji
 */
HAL_StatusTypeDef Si7021_SoftwareReset(Si7021_t *hsi7021)
{
	return Si7021_ReadRegister(hsi7021, SI7021_CMD_RESET, &(hsi7021->firmware), 2);
}

/**
 * @brief Inicjalizacja czujnika Si7021
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną czujnika
 * @param hi2c Wskaźnik na uchwyt I2C
 * @param address Adres I2C czujnika
 * @return Status operacji (HAL_OK jeśli sukces)
 */
HAL_StatusTypeDef Si7021_Init(Si7021_t *hsi7021, I2C_HandleTypeDef *hi2c, uint8_t address, Si7021_Resolution_t resolution)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }

    hsi7021->hi2c = hi2c;
    hsi7021->address = address << 1;
    if(HAL_OK != Si7021_ReadFirmware(hsi7021))
    	return HAL_ERROR;

    Si7021_SetResolution(hsi7021, resolution);
    return HAL_OK;
}

/**
 * @brief Ustawienie rozdzielczości pomiaru
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną
 * @param resolution Wybrana rozdzielczość
 * @return Status operacji
 */
HAL_StatusTypeDef Si7021_SetResolution(Si7021_t *hsi7021, Si7021_Resolution_t resolution)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }
    uint8_t reg;
    HAL_StatusTypeDef status = Si7021_ReadRegister(hsi7021, SI7021_CMD_READ_USER_REG1, &reg, 1);

    if (status != HAL_OK)
    	return status;

    // Wyczyszczenie bitów RES1 (bit 7) i RES0 (bit 0)
    reg &= ~(1 << 7 | 1 << 0);

    // Ustawienie bitów zgodnie z rozdzielczością
    switch (resolution)
    {
        case SI7021_RESOLUTION_RH12_TEMP14:
            // 00: bit7=0, bit0=0
            break;
        case SI7021_RESOLUTION_RH8_TEMP12:
            // 01: bit7=0, bit0=1
            reg |= (1 << 0);
            break;
        case SI7021_RESOLUTION_RH10_TEMP13:
            // 10: bit7=1, bit0=0
            reg |= (1 << 7);
            break;
        case SI7021_RESOLUTION_RH11_TEMP11:
            // 11: bit7=1, bit0=1
            reg |= (1 << 7 | 1 << 0);
            break;
    }

    return Si7021_WriteRegister(hsi7021, SI7021_CMD_WRITE_USER_REG1, reg);
}

/**
 * @brief Odczyt aktualnej rozdzielczości pomiaru
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną
 * @param resolution Wskaźnik na zmienną przechowującą rozdzielczość
 * @return Status operacji
 */
HAL_StatusTypeDef Si7021_GetResolution(Si7021_t *hsi7021)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }
    uint8_t reg;
    HAL_StatusTypeDef status = Si7021_ReadRegister(hsi7021, SI7021_CMD_READ_USER_REG1, &reg, 1);

    if (status != HAL_OK)
    	return status;

    // Wyodrębnienie bitów RES1 (bit 7) i RES0 (bit 0)
    uint8_t res_bits = ((reg >> 7) & 1) << 1 | (reg & 1);
    hsi7021->data.resolution = (Si7021_Resolution_t)res_bits;
    return HAL_OK;
}

/**
 * @brief Ustawienie grzałki na wskazaną wartość prądu
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną
 * @param current Wartość nastawialnego prądu. Przy 3.3V maksymalny prąd to 94mA
 * @return Status operacji
 */
HAL_StatusTypeDef Si7021_SetHeaterCurrent(Si7021_t *hsi7021, uint8_t current)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }
    uint8_t reg_value = (current - SI7021_HEATER_MIN_CURRENT) / SI7021_HEATER_CURRENT_OFFSET;
    if (reg_value > 0x0F)
    {
        reg_value = 0x0F;
    }
    return Si7021_WriteRegister(hsi7021, SI7021_CMD_WRITE_HEATER_REG, reg_value);
}

/**
 * @brief Pobranie aktualnej nastawy prądu grzałki
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną
 * @return Status operacji
 */
HAL_StatusTypeDef Si7021_GetHeaterCurrent(Si7021_t *hsi7021)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }
    uint8_t reg;
    HAL_StatusTypeDef status = Si7021_ReadRegister(hsi7021, SI7021_CMD_READ_HEATER_REG, &reg, 1);
    if (status != HAL_OK)
    	return status;

    hsi7021->data.heater_current = ((reg & 0x0F) * SI7021_HEATER_CURRENT_OFFSET) + SI7021_HEATER_MIN_CURRENT;

    return HAL_OK;
}

/**
 * @brief Funkcja pomocnicza do obliczania CRC-8 z polynomem 0x31
 * @param data Wskaźnik na dane do obliczenia CRC
 * @param len Długość danych
 * @return Obliczona suma kontrolna
 */
static uint8_t Si7021_ComputeCRC8(uint8_t *data, uint8_t len) 
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) 
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) 
        {
            if (crc & 0x80) 
            {
                crc = (crc << 1) ^ 0x31;
            } else 
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief Odczyt wilgotności z weryfikacją sumy kontrolnej
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną
 * @param humidity Wskaźnik na zmienną przechowującą wilgotność w %RH
 * @return Status operacji
 */
HAL_StatusTypeDef Si7021_ReadHumidity(Si7021_t *hsi7021)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }
    uint8_t data[3];

    HAL_StatusTypeDef status = Si7021_ReadRegister(hsi7021, SI7021_CMD_MEASURE_RH_HOLD, data, 3);

    if (status != HAL_OK)
    	return status;

    uint8_t crc = Si7021_ComputeCRC8(data, 2);

    if (crc != data[2])
    	return HAL_ERROR;

    uint16_t rh_code = (data[0] << 8) | data[1];
    hsi7021->data.humidity = (125.0f * rh_code) / 65536.0f - 6.0f;

    if (hsi7021->data.humidity < 0)
    	hsi7021->data.humidity = 0;

    if (hsi7021->data.humidity > 100)
    	hsi7021->data.humidity = 100;

    return HAL_OK;
}

/**
 * @brief Odczyt temperatury z weryfikacją sumy kontrolnej
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną
 * @param temperature Wskaźnik na zmienną przechowującą temperaturę w °C
 * @return Status operacji
 */
HAL_StatusTypeDef Si7021_ReadTemperature(Si7021_t *hsi7021)
{
	if (hsi7021 == NULL)
	{
        return HAL_ERROR;
    }

    uint8_t data[3];
    HAL_StatusTypeDef status = Si7021_ReadRegister(hsi7021, SI7021_CMD_MEASURE_TEMP_HOLD, data, 3);

    if (status != HAL_OK)
    	return status;

    uint8_t crc = Si7021_ComputeCRC8(data, 2);

    if (crc != data[2])
    	return HAL_ERROR;

    uint16_t temp_code = (data[0] << 8) | data[1];
    hsi7021->data.temperature = (175.72f * temp_code) / 65536.0f - 46.85f;

    return HAL_OK;
}

/**
 * @brief Odczyt wilgotności i temperatury. Odczyt temperatury z poprzedniego pomiaru wilgotności (bez sumy kontrolnej dla komendy 0xE0)
 * @param hsi7021 Wskaźnik na strukturę konfiguracyjną
 * @param measurement Wskaźnik na strukturę przechowującą wilgotność i temperaturę
 * @return Status operacji
 */
HAL_StatusTypeDef Si7021_ReadHumidityAndTemperature(Si7021_t *hsi7021)
{

	HAL_StatusTypeDef status = Si7021_ReadHumidity(hsi7021);
    uint8_t temp_data[2];

//    uint8_t cmd = SI7021_CMD_READ_TEMP_PREV_RH;
//    if(HAL_OK != HAL_I2C_Master_Transmit(hsi7021->hi2c, hsi7021->address, &cmd, 1, HAL_MAX_DELAY))
//    	return 0;
//
//    if(HAL_OK != HAL_I2C_Master_Receive(hsi7021->hi2c, hsi7021->address, temp_data, 2, HAL_MAX_DELAY))
//    	return 0;

    status = Si7021_ReadRegister(hsi7021, SI7021_CMD_READ_TEMP_PREV_RH, temp_data, 2);
    if (status != HAL_OK)
    	return status;

    uint16_t temp_code = (temp_data[0] << 8) | temp_data[1];
    hsi7021->data.temperature = (175.72f * temp_code) / 65536.0f - 46.85f;

    return HAL_OK;
}
