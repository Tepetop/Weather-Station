#include "ds3231.h"
#include "stm32f1xx_hal_def.h"


static HAL_StatusTypeDef DS3231_SetControlRegister(DS3231_t *dev, uint8_t controlValue) 
{
    if(NULL == dev) 
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Write(dev->hi2c, dev->address, DS3231_REG_CONTROL, 1, &controlValue, 1, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef DS3231_GetControlRegister(DS3231_t *dev, uint8_t *controlValue) 
{
    if(NULL == dev || NULL == controlValue) {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(dev->hi2c, dev->address, DS3231_REG_CONTROL, 1, controlValue, 1, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef DS3231_SetStatusRegister(DS3231_t *dev, uint8_t statusValue) 
{
    if(NULL == dev) {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Write(dev->hi2c, dev->address, DS3231_REG_STATUS, 1, &statusValue, 1, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef DS3231_GetStatusRegister(DS3231_t *dev, uint8_t *statusValue) 
{
    if(NULL == dev || NULL == statusValue) {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(dev->hi2c, dev->address, DS3231_REG_STATUS, 1, statusValue, 1, HAL_MAX_DELAY);
}

static HAL_StatusTypeDef writeBitToControlRegister(DS3231_t *dev, uint8_t bitMask, uint8_t value) 
{
    uint8_t controlReg;
    HAL_StatusTypeDef status = DS3231_GetControlRegister(dev, &controlReg);
    if(status != HAL_OK) 
    {
        return status;
    }

    if(value) 
    {
        controlReg |= bitMask; // Ustaw bit
    } else 
    {
        controlReg &= ~bitMask; // Wyczyść bit
    }

    return DS3231_SetControlRegister(dev, controlReg);
}

HAL_StatusTypeDef DS3231_ConvertTemperature(DS3231_t *dev) 
{
    return writeBitToControlRegister(dev, DS3231_CTRL_CONV, 1);
}

HAL_StatusTypeDef DS3231_Oscillator(DS3231_t *dev, uint8_t enable) 
{
    return writeBitToControlRegister(dev, DS3231_CTRL_EOSC, enable);
}

HAL_StatusTypeDef DS3231_BatteryBackerSQW(DS3231_t *dev, uint8_t enable) 
{
    return writeBitToControlRegister(dev, DS3231_CTRL_BBSQW, enable);
}

HAL_StatusTypeDef DS3231_SetSQWRate(DS3231_t *dev, DS3231_SQWRATE_t rate) 
{
    uint8_t rateBits = (uint8_t)(rate & 0x03); // Upewnij się, że tylko dwa bity są używane
    return writeBitToControlRegister(dev, DS3231_CTRL_RS1 | DS3231_CTRL_RS2, rateBits);
}

HAL_StatusTypeDef DS3231_EnableInterrupt(DS3231_t *dev, uint8_t enable) 
{
    return writeBitToControlRegister(dev, DS3231_CTRL_INTCN, enable);
}

HAL_StatusTypeDef DS3231_Alarm_InterruptEnable(DS3231_t *dev, uint8_t enable, uint8_t alarmNumber) 
{
    if(alarmNumber == 1) 
    {
        return writeBitToControlRegister(dev, DS3231_CTRL_A1IE, enable);
    } 
    else if(alarmNumber == 2) 
    {
        return writeBitToControlRegister(dev, DS3231_CTRL_A2IE, enable);
    } 
    else 
    {
        return HAL_ERROR; // Nieprawidłowy numer alarmu
    }
}




/**
* brief Inicjalizacja DS3231
* @param dev Wskaźnik na strukturę urządzenia DS3231
* @param hi2c Wskaźnik na handler I2C (np. I2C_HandleTypeDef dla STM32)
* @param address Adres I2C urządzenia DS3231 (zazwyczaj 0x68)
* @return HAL status    
**/
HAL_StatusTypeDef DS3231_Init(DS3231_t *dev, I2C_HandleTypeDef *hi2c, uint8_t address) {
    if(NULL == dev || NULL == hi2c) {
        return HAL_ERROR;
    }

    dev->hi2c = hi2c;
    dev->address = address << 1; // Shift left for STM32 HAL
    dev->mode = DS3231_BLOKCING_MODE; // Domyślny tryb komunikacji


    return HAL_OK;
}

