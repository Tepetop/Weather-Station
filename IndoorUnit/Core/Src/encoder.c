#include "encoder.h"
#include "stm32f1xx_hal_def.h"
#include "stm32f1xx_hal_tim.h"
#include <stdint.h>

/**
 * @desc    Initialize encoder
 *
 * @param   Enkoder_t *encoder - encoder type struct, TIM_TypeDef *timer - TIM handler for encoder, uint8_t resolution
 *
 * @return  Encoder_Status
 */
HAL_StatusTypeDef Encoder_Init(Encoder_t *encoder, TIM_HandleTypeDef *timer, uint8_t resolution, uint32_t channel1, uint32_t channel2)
{
    if(NULL == encoder || NULL == timer)
    {
        return HAL_ERROR;
    }
	encoder->EncTim = timer;
	encoder->TickDiff = 0;
	encoder->TickRawCount = 0;
	encoder->TickCount = 0;
	encoder->Resolution = resolution;

    return HAL_TIM_Encoder_Start_IT(timer, channel1 | channel2);
}

/**
 * @desc    Check if any tick occured
 *
 * @param  *encoder - encoder type struct
 *
 * @return  Encoder_Status
 */
HAL_StatusTypeDef Encoder_Get_Ticks(Encoder_t *encoder)
{
	// Get current CNT value from timer and subctract it with tick raw count
	encoder->TickDiff = __HAL_TIM_GET_COUNTER(encoder->EncTim) - encoder->TickRawCount;

	/*	Check if TickDiff has changed by +-4	*/
	if(encoder->TickDiff >= ENC_TICK_PER_TURN  || encoder->TickDiff <= -ENC_TICK_PER_TURN )
	{
		encoder->TickDiff /= ENC_TICK_PER_TURN;
		encoder->TickCount += (int8_t)(encoder->TickDiff);
		encoder->TickRawCount = __HAL_TIM_GET_COUNTER(encoder->EncTim);		//Uptate raw count
		return HAL_OK;
	}
	return HAL_OK;
}

