#ifndef ENCODER_H
#define ENCODER_H

#include "main.h"
#include "PCD8544_Menu.h"


#define ENCODER_TIMER_MIDDLE  0x7FFF
/**
 * @brief Encoder rotation direction (mirrors hardware DIR bit in TIMx->CR1).
 *
 * In encoder mode the timer hardware sets the DIR bit automatically:
 *   DIR = 0 → counter counts UP   → forward  (pin A pulses)
 *   DIR = 1 → counter counts DOWN → backward (pin B pulses)
 */
typedef enum
{
	ENCODER_DIR_FORWARD  = 0,   /**< Counting up   – pin A impulses dominate */
	ENCODER_DIR_BACKWARD = 1    /**< Counting down  – pin B impulses dominate */
} Encoder_Direction_t;

/**
 * @brief Encoder instance structure.
 */
typedef struct
{
	TIM_HandleTypeDef      *EncTim;          /**< Timer handle (encoder mode)            */
	uint16_t                PrevCounter;     /**< Previous CNT snapshot for delta calc    */
	int32_t                 PulseCount;      /**< Net accumulated pulse count (+/-)       */
	Encoder_Direction_t     Direction;       /**< Current direction from CR1.DIR bit      */
	volatile uint8_t        IRQ_Flag;        /**< Set in TIM capture-compare ISR          */
	volatile uint8_t        ButtonIRQ_Flag;  /**< Set in EXTI (button) ISR                */
} Encoder_t;

/*  ──────────────────  Public API  ──────────────────  */

HAL_StatusTypeDef       Encoder_Init(Encoder_t *encoder, TIM_HandleTypeDef *timer, uint32_t channel1, uint32_t channel2);
HAL_StatusTypeDef       Encoder_Update(Encoder_t *encoder);
Encoder_Direction_t     Encoder_GetDirection(const Encoder_t *encoder);
int32_t                 Encoder_GetPulseCount(const Encoder_t *encoder);
void                    Encoder_ResetPulseCount(Encoder_t *encoder);
void                    Encoder_ManageCursorPosition(Encoder_t *encoder, Menu_Context_t *context);
void                    Encoder_Task(Encoder_t *encoder, Menu_Context_t *context);

#endif /* ENCODER_H */