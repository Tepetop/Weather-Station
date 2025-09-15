#ifndef TSL2561_H
#define TSL2561_H

#include "main.h"
#include <stdbool.h>


/* Registers enum */
typedef enum {
	TSL2561_REG_COMMAND        = 0x80,  // Command register (CMD bit set)
    TSL2561_REG_CONTROL        = 0x00,  // Control register
    TSL2561_REG_TIMING         = 0x01,  // Timing register
    TSL2561_REG_THRESHLOWLOW   = 0x02,  // Low threshold low byte
    TSL2561_REG_THRESHLOWHIGH  = 0x03,  // Low threshold high byte
    TSL2561_REG_THRESHHIGHLOW  = 0x04,  // High threshold low byte
    TSL2561_REG_THRESHHIGHHIGH = 0x05,  // High threshold high byte
    TSL2561_REG_INTERRUPT      = 0x06,  // Interrupt control register
    TSL2561_REG_ID             = 0x0A,  // ID register
    TSL2561_REG_DATA0LOW       = 0x0C,  // ADC Channel 0 low byte
    TSL2561_REG_DATA0HIGH      = 0x0D,  // ADC Channel 0 high byte
    TSL2561_REG_DATA1LOW       = 0x0E,  // ADC Channel 1 low byte
    TSL2561_REG_DATA1HIGH      = 0x0F,  // ADC Channel 1 high byte
	TSL2561_REG_BLOCK  		   = 0x10,
	TSL2561_REG_WORD  		   = 0x20,	// Word protocol (bit 5)
	TSL2561_REG_CLEAR  		   = 0x40	// Interrupt clear (bit 6)
} TSL2561_Register_t;

/* Power control */
typedef enum {
    TSL2561_CONTROL_POWEROFF   = 0x00, // Power on
    TSL2561_CONTROL_POWERON    = 0x03  // Power off
} TSL2561_PowerControl_t;

/* Integration time */
typedef enum {
    TSL2561_INTEG_13MS 		    = 0x00, // 13.7ms integration time
    TSL2561_INTEG_101MS 		= 0x01, // 101ms integration time
    TSL2561_INTEG_402MS 		= 0x02, // 402ms integration time
	TSL2561_SET_MANUAL			= 0x08	// Set manual integration time
} TSL2561_IntegrationTime_t;


/* Gain */
typedef enum {
    TSL2561_GAIN_1X				= 0x00,   // 1x gain
    TSL2561_GAIN_16X			= 0x10    // 16x gain
} TSL2561_Gain_t;

// Interrupt Control Settings
typedef enum
{
	TSL2561_INTR_DISABLE 	=	0x00,	// Interrupt disabled
	TSL2561_INTR_LEVEL      =   0x10,	// Level interrupt
	TSL2561_INTR_TEST       =   0x30	// Test mode
}TSL2561_InterruptControl_t;


typedef struct {
	uint16_t 					chan0;	// channel 0 data
	uint16_t 					chan1;	// channel 1 data
	float 						lux;	// output in lux
}TSL2561_Measurement_t;

// Structure to hold TSL2561 sensor information
typedef struct {
    I2C_HandleTypeDef 			*hi2c; 		// I2C handle
    uint8_t 					address;    // I2C address (shifted left by 1)
    TSL2561_IntegrationTime_t 	timing_ms;	// time in ms for integration
	TSL2561_Gain_t 				gain;
    TSL2561_Measurement_t		data;		// Container for measurments
} TSL2561_t;

// Interrupt Persistence Settings
#define TSL2561_PERSIST_EVERY     0x00 // Every ADC cycle
#define TSL2561_PERSIST_OUTSIDE   0x01 // Any value outside threshold
#define TSL2561_PERSIST_2         0x02 // 2 cycles outside threshold
#define TSL2561_PERSIST_3         0x03 // 3 cycles outside threshold
// ... up to 15
#define TSL2561_PERSIST_15        0x0F // 15 cycles outside threshold

// Function prototypes
HAL_StatusTypeDef TSL2561_Init(TSL2561_t *sensor, I2C_HandleTypeDef *hi2c, uint8_t address,TSL2561_IntegrationTime_t timing_ms, TSL2561_Gain_t gain);
HAL_StatusTypeDef TSL2561_PowerOn(TSL2561_t *sensor);
HAL_StatusTypeDef TSL2561_PowerOff(TSL2561_t *sensor);
HAL_StatusTypeDef TSL2561_SetTiming(TSL2561_t *sensor, TSL2561_IntegrationTime_t time, TSL2561_Gain_t gain);
HAL_StatusTypeDef TSL2561_SetInterruptThreshold(TSL2561_t *sensor, uint16_t low_threshold, uint16_t high_threshold);
HAL_StatusTypeDef TSL2561_SetInterruptControl(TSL2561_t *sensor, uint8_t intr_mode, uint8_t persist);
HAL_StatusTypeDef TSL2561_ClearInterrupt(TSL2561_t *sensor);
HAL_StatusTypeDef TSL2561_ReadID(TSL2561_t *sensor, uint8_t *part_no, uint8_t *rev_no);
HAL_StatusTypeDef TSL2561_ReadADC(TSL2561_t *sensor);
HAL_StatusTypeDef TSL2561_CalculateLux(TSL2561_t *sensor);

#endif // TSL2561_H
