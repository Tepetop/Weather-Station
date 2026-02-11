#ifndef ENCODER_H
#define ENCODER_H

#include "main.h"

#define ENC_TICK_PER_TURN 4


typedef struct
{
	TIM_HandleTypeDef 		*EncTim;			// Tim handler for encoder
	int						TickDiff;			// Variable for storage tick diff
	uint16_t 				TickRawCount;		// "Unfiltred" tick count
	int16_t 				TickCount;			// True tick count of encoder depends of resolution
	uint8_t					Resolution;			// Resoluton of encoder
}Encoder_t;


/*				Declaration of functions			*/

HAL_StatusTypeDef Encoder_Init(Encoder_t *encoder, TIM_HandleTypeDef *timer, uint8_t resolution, uint32_t channel1, uint32_t channel2);
HAL_StatusTypeDef Encoder_Get_Ticks(Encoder_t *encoder);


#endif