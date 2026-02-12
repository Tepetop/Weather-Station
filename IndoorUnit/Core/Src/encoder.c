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
HAL_StatusTypeDef Encoder_Init(Encoder_t *encoder, TIM_HandleTypeDef *timer, uint32_t channel1, uint32_t channel2)
{
	if(NULL == encoder || NULL == timer)
	{
		return HAL_ERROR;
	}
	encoder->EncTim = timer;
	encoder->TickDiff = 0;
	
	// Zmiana: Inicjalizuj RawCount aktualną wartością licznika zamiast 0
	encoder->TickRawCount = __HAL_TIM_GET_COUNTER(encoder->EncTim);
	encoder->TickCount = 0;
	encoder->IRQ_Flag = 0;
	encoder->ButtonIRQ_Flag = 0;

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
	// Rzutownaie na int16_t zapewnia poprawną obsługę przepełnienia (overflow) dla timerów 16-bit
	uint16_t tempcounter = __HAL_TIM_GET_COUNTER(encoder->EncTim);
	uint16_t rawCount = encoder->TickRawCount;
	encoder->TickDiff = (int16_t)(tempcounter - rawCount);

	/*	Check if TickDiff has changed by +-4	*/
	if(encoder->TickDiff >= ENC_TICK_PER_TURN  || encoder->TickDiff <= -ENC_TICK_PER_TURN )
	{
		// Oblicz ile pełnych kroków wykonano
		int8_t ticks = encoder->TickDiff / (int)ENC_TICK_PER_TURN;
		
		encoder->TickCount += ticks;

		// Zmiana: Aktualizuj RawCount o ZUZYTĄ wartość, a nie o aktualny licznik.
		// To utrzymuje synchronizację z fizycznymi "klikami" enkodera.
		encoder->TickRawCount += (ticks * ENC_TICK_PER_TURN);

		return HAL_OK;
	}
	return HAL_OK;
}

/**
 * @desc    Manage cursor position based on encoder ticks
 *
 * @param   encoder - encoder struct
 * @param   context - menu context struct
 *
 * @return  None
 */
void Encoder_ManageCursorPosition(Encoder_t *encoder, Menu_Context_t *context)
{
	if (encoder->TickCount != 0)
	{
		if (encoder->TickCount > 0)
		{
			Menu_SetNextAction(context);
			encoder->TickCount--;
		}
		else
		{
			Menu_SetPrevAction(context);
			encoder->TickCount++;
		}
	}
}

/**
 * @desc    Main loop task for encoder
 *
 * @param   encoder - encoder struct
 * @param   context - menu context struct
 *
 * @return  None
 */
void Encoder_Task(Encoder_t *encoder, Menu_Context_t *context)
{
	if (encoder->ButtonIRQ_Flag)
	{
		encoder->ButtonIRQ_Flag = 0;
		Menu_SetEnterAction(context);
	}

	if (encoder->IRQ_Flag)
	{
		encoder->IRQ_Flag = 0;
		int16_t prevTickCount = encoder->TickCount;
		Encoder_Get_Ticks(encoder);
		if (encoder->TickCount != prevTickCount)
		{
			Encoder_ManageCursorPosition(encoder, context);
		}
	}
}

