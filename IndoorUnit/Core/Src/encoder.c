#include "encoder.h"
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════
 *  Encoder library for STM32 timer in encoder mode.
 *
 *  The hardware timer counts automatically on input edges:
 *    • Pin A (TI1) edges   → counter increments  (DIR = 0, forward)
 *    • Pin B (TI2) level   → determines counting direction
 *  When the encoder reverses, the DIR bit in CR1 flips and the
 *  counter starts decrementing, so the net delta already reflects
 *  correct bidirectional movement.
 *
 *  Direction is read directly from the CR1.DIR bit via
 *  __HAL_TIM_IS_TIM_COUNTING_DOWN(), which maps to:
 *      TIMx->CR1 & TIM_CR1_DIR
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief  Initialise encoder instance and start the timer in encoder mode.
 *
 * @param  encoder   Pointer to Encoder_t structure.
 * @param  timer     HAL timer handle configured in encoder mode.
 * @param  channel1  First  timer channel (e.g. TIM_CHANNEL_1).
 * @param  channel2  Second timer channel (e.g. TIM_CHANNEL_2).
 * @retval HAL_OK on success, HAL_ERROR on NULL pointer.
 */
HAL_StatusTypeDef Encoder_Init(Encoder_t *encoder, TIM_HandleTypeDef *timer, uint32_t channel1, uint32_t channel2)
{
	if (encoder == NULL || timer == NULL)
	{
		return HAL_ERROR;
	}

	encoder->EncTim        = timer;
	encoder->PulseCount    = 0;
	encoder->Direction     = ENCODER_DIR_FORWARD;
	encoder->IRQ_Flag      = 0;
	encoder->ButtonIRQ_Flag = 0;			// nie będzie potrzebny bo mam debounce w innym pliku z callbackami

	/* Start encoder timer with interrupts on both channels */
	HAL_StatusTypeDef status = HAL_TIM_Encoder_Start_IT(timer, channel1 | channel2);

	/* Capture initial counter position AFTER the timer is running */
	__HAL_TIM_SET_COUNTER(encoder->EncTim, ENCODER_TIMER_MIDDLE);
	encoder->PrevCounter = (uint16_t)__HAL_TIM_GET_COUNTER(encoder->EncTim);
	

	return status;
}

/**
 * @brief  Sample the timer counter, compute delta and accumulate pulses.
 *
 * This function reads two pieces of information from the timer registers:
 *   1. CNT   – current counter value (net position of the encoder)
 *   2. CR1.DIR – instantaneous direction bit set by hardware
 *        DIR = 0 → counting up   → pin A pulses (forward)
 *        DIR = 1 → counting down → pin B pulses (backward)
 *
 * The delta is computed as a signed 16-bit difference which correctly
 * handles wrap-around of the 16-bit counter (ARR = 0xFFFF).
 * Direction changes between two calls are inherently handled by the
 * hardware counter: the net delta already accounts for pulses in both
 * directions.
 *
 * @param  encoder  Pointer to Encoder_t structure.
 * @retval HAL_OK always (HAL_ERROR on NULL).
 */
HAL_StatusTypeDef Encoder_Update(Encoder_t *encoder)
{
	if (encoder == NULL)
	{
		return HAL_ERROR;
	}

	/* ── 1. Read current counter value from TIMx->CNT register ── */
	uint16_t currentCounter = (uint16_t)__HAL_TIM_GET_COUNTER(encoder->EncTim);

	/* ── 2. Read direction from TIMx->CR1 DIR bit ────────────── */
	/*  In encoder mode the hardware sets DIR automatically:      */
	/*    DIR = 0 → counting up   (ENCODER_DIR_FORWARD)           */
	/*    DIR = 1 → counting down (ENCODER_DIR_BACKWARD)          */
	encoder->Direction = Encoder_GetDirection(encoder);

	/* ── 3. Compute signed delta ─────────────────────────────── */
	/*  Cast to int16_t gives correct signed result even when     */
	/*  the 16-bit counter wraps around 0 ↔ 0xFFFF.              */
	int16_t delta = (int16_t)(currentCounter - encoder->PrevCounter);

	/* Store current counter for next cycle */
	encoder->PrevCounter = currentCounter;

	if (delta == 0)
	{
		return HAL_OK;
	}

	/* ── 4. Accumulate net pulse count ───────────────────────── */
	/*  delta > 0 → pin A pulses dominated  (forward)            */
	/*  delta < 0 → pin B pulses dominated  (backward)           */
	if (delta > 0) 
	{
		encoder->PulseCount++;
    } else 
	{
		encoder->PulseCount--;
    }

	return HAL_OK;
}

/**
 * @brief  Return the current rotation direction read from the DIR bit.
 * @param  encoder  Pointer to Encoder_t structure (const, not modified).
 * @retval ENCODER_DIR_FORWARD or ENCODER_DIR_BACKWARD.
 */
Encoder_Direction_t Encoder_GetDirection(const Encoder_t *encoder)
{
	return (__HAL_TIM_IS_TIM_COUNTING_DOWN(encoder->EncTim))
	       ? ENCODER_DIR_BACKWARD
	       : ENCODER_DIR_FORWARD;
}

/**
 * @brief  Return accumulated signed pulse count.
 * @param  encoder  Pointer to Encoder_t structure (const, not modified).
 * @retval Current pulse count (positive = net forward, negative = net backward).
 */
int32_t Encoder_GetPulseCount(const Encoder_t *encoder)
{
	return encoder->PulseCount;
}

/**
 * @brief  Reset accumulated pulse count to zero.
 * @param  encoder  Pointer to Encoder_t structure.
 */
void Encoder_ResetPulseCount(Encoder_t *encoder)
{
	encoder->PulseCount = 0;
}

/**
 * @brief  Consume one pulse from PulseCount and set appropriate menu action.
 *
 * Positive PulseCount → Menu_SetNextAction (scroll down / right)
 * Negative PulseCount → Menu_SetPrevAction (scroll up   / left)
 *
 * @param  encoder  Pointer to Encoder_t structure.
 * @param  context  Pointer to active Menu_Context_t.
 */
void Encoder_ManageCursorPosition(Encoder_t *encoder, Menu_Context_t *context)
{
	if (encoder->PulseCount > 0)
	{
		Menu_SetNextAction(context);
		encoder->PulseCount--;
	}
	else if (encoder->PulseCount < 0)
	{
		Menu_SetPrevAction(context);
		encoder->PulseCount++;
	}
}

/**
 * @brief  Main-loop task: process button flag and encoder pulses.
 *
 * Call this function continuously from the super-loop.  It checks the
 * interrupt flags set by the ISR, updates the pulse counter via
 * Encoder_Update(), and drives the menu cursor.
 *
 * @param  encoder  Pointer to Encoder_t structure.
 * @param  context  Pointer to active Menu_Context_t.
 */
void Encoder_Task(Encoder_t *encoder, Menu_Context_t *context)
{
	/* ── Handle encoder button press ──────────────────────────── */
	if (encoder->ButtonIRQ_Flag)
	{
		encoder->ButtonIRQ_Flag = 0;
	}

	/* ── Handle encoder rotation ──────────────────────────────── */
	if (encoder->IRQ_Flag)
	{
		encoder->IRQ_Flag = 0;

		int32_t prevCount = encoder->PulseCount;
		Encoder_Update(encoder);

		if (encoder->PulseCount != prevCount)
		{
			Encoder_ManageCursorPosition(encoder, context);
		}
	}
}

