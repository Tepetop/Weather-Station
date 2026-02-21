#ifndef ENCODER_H
#define ENCODER_H

#include "main.h"
#include "PCD8544_Menu.h"

#define ENC_TICK_PER_TURN 1			//reosulution of encoder (number of ticks per full turn)


typedef struct
{
	TIM_HandleTypeDef 		*EncTim;			// Tim handler for encoder
	int						TickDiff;			// Variable for storage tick diff
	uint16_t 				TickRawCount;		// "Unfiltred" tick count
	int16_t 				TickCount;			// True tick count of encoder depends of resolution
	volatile uint8_t		IRQ_Flag;			// Flag indicating that interrupt occured
	volatile uint8_t		ButtonIRQ_Flag;		// Flag indicating that button interrupt occured
}Encoder_t;


/*				Declaration of functions			*/

HAL_StatusTypeDef Encoder_Init(Encoder_t *encoder, TIM_HandleTypeDef *timer, uint32_t channel1, uint32_t channel2);
HAL_StatusTypeDef Encoder_Get_Ticks(Encoder_t *encoder);
void Encoder_ManageCursorPosition(Encoder_t *encoder, Menu_Context_t *context);
void Encoder_Task(Encoder_t *encoder, Menu_Context_t *context);


#endif