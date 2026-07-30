/**
 * @file    button_debounce.c
 * @brief   GPIO button debounce library implementation
 * @details Active-low button state machine with debounce, long-press, and
 *          repeat timing. Supports polling and EXTI interrupt modes.
 */

#include "main.h"
#include "button_debounce.h"

/**
 * @brief   Initializes a button instance
 * @param   Key             Button handle to initialize
 * @param   GpioPort        GPIO port of the button
 * @param   GpioPin         GPIO pin of the button
 * @param   TimerDebounce   Debounce time in milliseconds
 * @param   TimerLongPress  Long-press threshold in milliseconds
 * @param   TimerRepeat     Repeat interval in milliseconds
 * @param   IOMode          BUTTON_MODE_POLLING or BUTTON_MODE_INTERRUPT
 * @retval  None
 */
void ButtonInitKey(Button_t* Key, GPIO_TypeDef* GpioPort, uint16_t GpioPin, uint32_t TimerDebounce,
					uint32_t TimerLongPress, uint32_t TimerRepeat, BUTTON_IO_MODE IOMode)
{
	Key->State = IDLE;
	Key->IOMode = IOMode;

	Key->GpioPort = GpioPort;
	Key->GpioPin = GpioPin;

	Key->TimerDebounce = TimerDebounce;
	Key->TimerLongPress = TimerLongPress;
	Key->TimerRepeat = TimerRepeat;
	
	Key->InterruptFlag = 0;
}

/**
 * @brief   Sets debounce duration
 * @param   Key          Button handle
 * @param   Milliseconds Debounce time in milliseconds
 * @retval  None
 */
void ButtonSetDebounceTime(Button_t* Key, uint32_t Milliseconds)
{
	Key->TimerDebounce = Milliseconds;
}

/**
 * @brief   Sets long-press threshold
 * @param   Key          Button handle
 * @param   Milliseconds Long-press time in milliseconds
 * @retval  None
 */
void ButtonSetLongPressTime(Button_t* Key, uint32_t Milliseconds)
{
	Key->TimerLongPress = Milliseconds;
}

/**
 * @brief   Sets repeat interval while button is held
 * @param   Key          Button handle
 * @param   Milliseconds Repeat interval in milliseconds
 * @retval  None
 */
void ButtonSetRepeatTime(Button_t* Key, uint32_t Milliseconds)
{
	Key->TimerRepeat = Milliseconds;
}

/**
 * @brief   Registers callback for stable press event
 * @param   Key       Button handle
 * @param   Callback  Function to call on press (may be NULL)
 * @retval  None
 */
void ButtonRegisterPressCallback(Button_t* Key, void (*Callback)(void))
{
	Key->ButtonPressed = Callback;
}

/**
 * @brief   Registers callback for long-press event
 * @param   Key       Button handle
 * @param   Callback  Function to call on long press (may be NULL)
 * @retval  None
 */
void ButtonRegisterLongPressCallback(Button_t* Key, void (*Callback)(void))
{
	Key->ButtonLongPressed = Callback;
}

/**
 * @brief   Registers callback for repeat-while-held event
 * @param   Key       Button handle
 * @param   Callback  Function to call on each repeat (may be NULL)
 * @retval  None
 */
void ButtonRegisterRepeatCallback(Button_t* Key, void (*Callback)(void))
{
	Key->ButtonRepeat = Callback;
}

/**
 * @brief   Registers callback for release event
 * @param   Key       Button handle
 * @param   Callback  Function to call on release (may be NULL)
 * @retval  None
 */
void ButtonRegisterReleaseCalllback(Button_t* Key, void(*Callback)(void))
{
	Key->ButtonRelease = Callback;
}

/**
 * @brief   Returns whether the button is currently pressed (active low)
 * @param   Key  Button handle
 * @retval  1 if pressed, 0 if released
 */
static uint8_t ButtonIsPressed(Button_t* Key)
{
	return (GPIO_PIN_RESET == HAL_GPIO_ReadPin(Key->GpioPort, Key->GpioPin)) ? (1) : (0);
}

/**
 * @brief   IDLE state handler: detect press or interrupt flag
 * @param   Key  Button handle
 * @retval  None
 */
static void ButtonIdleRoutine(Button_t* Key)
{
	if(Key->IOMode == BUTTON_MODE_INTERRUPT)
	{
		if(Key->InterruptFlag)
		{
			Key->State = DEBOUNCE;
			Key->LastTick = HAL_GetTick();
		}
	}
	else
	{
		if(ButtonIsPressed(Key))
		{
			Key->State = DEBOUNCE;
			Key->LastTick = HAL_GetTick();
		}
	}
}

/**
 * @brief   DEBOUNCE state handler: wait for stable press
 * @param   Key  Button handle
 * @retval  None
 */
static void ButtonDebounceRoutine(Button_t* Key)
{
	if((HAL_GetTick() - Key->LastTick) > Key->TimerDebounce)
	{
		if(ButtonIsPressed(Key))
		{
			if(Key->IOMode == BUTTON_MODE_INTERRUPT)
			{
				Key->InterruptFlag = 0;
			}

			Key->State = PRESSED;
			Key->LastTick = HAL_GetTick();

			if(Key->ButtonPressed != NULL)
			{
				Key->ButtonPressed();
			}
		}
		else
		{
			if(Key->IOMode == BUTTON_MODE_INTERRUPT)
			{
				Key->InterruptFlag = 0;
			}

			Key->State = IDLE;
		}
	}
}

/**
 * @brief   PRESSED state handler: detect release or long press
 * @param   Key  Button handle
 * @retval  None
 */
static void ButtonPressedRoutine(Button_t* Key)
{
	if(!ButtonIsPressed(Key))
	{
		Key->InterruptFlag = 0;
#if BUTTON_RELEASE_ACTION
		Key->State = RELEASE;
#else
		Key->State = IDLE;
#endif
	}
	else
	{
		if((HAL_GetTick() - Key->LastTick) > Key->TimerLongPress)
		{
			Key->State = REPEAT;
			Key->LastTick = HAL_GetTick();

			if(Key->ButtonLongPressed != NULL)
			{
				Key->ButtonLongPressed();
			}
		}
	}
}

/**
 * @brief   REPEAT state handler: fire repeat callback while held
 * @param   Key  Button handle
 * @retval  None
 */
static void ButtonRepeatRoutine(Button_t* Key)
{
	if(!ButtonIsPressed(Key))
	{
		Key->InterruptFlag = 0;
#if BUTTON_RELEASE_ACTION
		Key->State = RELEASE;
#else
		Key->State = IDLE;
#endif
	}
	else
	{
		if((HAL_GetTick() - Key->LastTick) > Key->TimerRepeat)
		{
			Key->LastTick = HAL_GetTick();

			if(Key->ButtonRepeat != NULL)
			{
				Key->ButtonRepeat();
			}
		}
	}
}

/**
 * @brief   RELEASE state handler: invoke release callback
 * @param   Key  Button handle
 * @retval  None
 */
static void ButtonReleaseRoutine(Button_t* Key)
{
	Key->State = IDLE;

	if(Key->ButtonRelease != NULL)
		{
			Key->ButtonRelease();
		}
}

/**
 * @brief   Runs one step of the button state machine
 * @param   Key  Button handle
 * @retval  None
 */
void ButtonTask(Button_t* Key)
{
	switch(Key->State)
	{
		case IDLE:
			ButtonIdleRoutine(Key);
			break;

		case DEBOUNCE:
			ButtonDebounceRoutine(Key);
			break;

		case PRESSED:
			ButtonPressedRoutine(Key);
			break;

		case REPEAT:
			ButtonRepeatRoutine(Key);
			break;

		case RELEASE:
			ButtonReleaseRoutine(Key);
			break;

		default:
			break;
	}
}

/**
 * @brief   EXTI callback helper for interrupt mode
 * @param   Key       Button handle
 * @param   GPIO_Pin  Pin that triggered the interrupt
 * @retval  None
 * @details Call from HAL_GPIO_EXTI_Callback. Latches an event only when idle.
 */
void ButtonIRQHandler(Button_t* Key, uint16_t GPIO_Pin)
{
	if(Key->IOMode == BUTTON_MODE_INTERRUPT && GPIO_Pin == Key->GpioPin && Key->State == IDLE)
	{
		Key->InterruptFlag = 1;
	}
}
