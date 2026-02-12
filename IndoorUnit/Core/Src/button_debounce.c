/*
 * button_debounce.c
 *
 *  Created on: Sep 6, 2024
 *      Author: remik
 */
#include "main.h"
#include "button_debounce.h"

/**
 * @desc    Button init
 *
 * @param   Key - button struct, other variables are easy to understand
 * @param   IOMode - BUTTON_MODE_POLLING or BUTTON_MODE_INTERRUPT
 *
 * @return  void
 */
void ButtonInitKey(Button_t* Key, GPIO_TypeDef* GpioPort, uint16_t GpioPin, uint32_t TimerDebounce,
					uint32_t TimerLongPress, uint32_t TimerRepeat, BUTTON_IO_MODE IOMode)
{
	Key->State = IDLE; // Set initial state for the button
	Key->IOMode = IOMode; // Set IO Mode (Polling or Interrupt)

	Key->GpioPort = GpioPort; // Remember GPIO Port for the button
	Key->GpioPin = GpioPin; // Remember GPIO Pin for the button

	Key->TimerDebounce = TimerDebounce; // Remember Debounce Time for the button
	Key->TimerLongPress = TimerLongPress; // Remember Long Press Time for the button
	Key->TimerRepeat = TimerRepeat; // Remember Repeat Time for the button
	
	Key->InterruptFlag = 0; // Clear interrupt flag
}

/**
 * @desc    Setting the time of debounce
 *
 * @param   Key - button struct, other variables are easy to understand
 *
 * @return  void
 */
void ButtonSetDebounceTime(Button_t* Key, uint32_t Milliseconds)
{
	Key->TimerDebounce = Milliseconds;
}

/**
 * @desc    Setting the time of a long button press
 *
 * @param   Key - button struct, other variables are easy to understand
 *
 * @return  void
 */
void ButtonSetLongPressTime(Button_t* Key, uint32_t Milliseconds)
{
	Key->TimerLongPress = Milliseconds;
}

/**
 * @desc    Setting the time of a repeat time delay
 *
 * @param   Key - button struct, other variables are easy to understands
 *
 * @return  void
 */
void ButtonSetRepeatTime(Button_t* Key, uint32_t Milliseconds)
{
	Key->TimerRepeat = Milliseconds;
}

/**
 * @desc    Assigning a callback for the press function
 *
 * @param   Key - button struct, pointer to callback
 *
 * @return  void
 */
void ButtonRegisterPressCallback(Button_t* Key, void (*Callback)(void))
{
	Key->ButtonPressed = Callback;
}

/**
 * @desc    Assigning a callback for the long press function
 *
 * @param   Key - button struct, pointer to callback
 *
 * @return  void
 */
void ButtonRegisterLongPressCallback(Button_t* Key, void (*Callback)(void))
{
	Key->ButtonLongPressed = Callback;
}

/**
 * @desc    Assigning a callback for the repeat press function
 *
 * @param   Key - button struct, pointer to callback
 *
 * @return  void
 */
void ButtonRegisterRepeatCallback(Button_t* Key, void (*Callback)(void))
{
	Key->ButtonRepeat = Callback;
}

/**
 * @desc    Assigning a callback for the button release callback
 *
 * @param   Key - button struct, pointer to callback
 *
 * @return  void
 */
void ButtonRegisterReleaseCalllback(Button_t* Key, void(*Callback)(void))
{
	Key->ButtonRelease = Callback;
}

/**
 * @desc    Check if button is pressed
 *
 * @param   Key - button struct
 *
 * @return  void
 */
static uint8_t ButtonIsPressed(Button_t* Key)
{
	return (GPIO_PIN_RESET == HAL_GPIO_ReadPin(Key->GpioPort, Key->GpioPin)) ? (1) : (0);
}

/**
 * @desc    Button idle routine
 *
 * @param   Key - button struct
 *
 * @return  void
 */
static void ButtonIdleRoutine(Button_t* Key)
{
	// In INTERRUPT mode, wait for interrupt flag
	if(Key->IOMode == BUTTON_MODE_INTERRUPT)
	{
		if(Key->InterruptFlag)
		{
			Key->InterruptFlag = 0; // Clear flag
			Key->State = DEBOUNCE; // Jump to DEBOUNCE State
			Key->LastTick = HAL_GetTick(); // Remember current tick for Debounce software timer
		}
	}
	else // POLLING mode
	{
		// Check if button was pressed
		if(ButtonIsPressed(Key))
		{
			// Button was pressed for the first time
			Key->State = DEBOUNCE; // Jump to DEBOUNCE State
			Key->LastTick = HAL_GetTick(); // Remember current tick for Debounce software timer
		}
	}
}

/**
 * @desc    Button debounce function, if button is sill pressed after delay go to next routine
 *
 * @param   Key - button struct
 *
 * @return  void
 */
static void ButtonDebounceRoutine(Button_t* Key)
{
	// Wait for Debounce Timer elapsed
	if((HAL_GetTick() - Key->LastTick) > Key->TimerDebounce)
	{
		// After Debounce Timer elapsed check if button is still pressed
		if(ButtonIsPressed(Key))
		{
			// Still pressed
			Key->State = PRESSED; // Jump to PRESSED state
			Key->LastTick = HAL_GetTick(); // Remember current tick for Long Press action

			if(Key->ButtonPressed != NULL) // Check if callback for pressed button exists
			{
				Key->ButtonPressed(); // If exists - do the callback function
			}
		}
		else
		{
			// If button was released during debounce time
			Key->State = IDLE; // Go back do IDLE state
		}
	}
}

/**
 * @desc    Button pressed routine, if button is pressed go to repeat routine or do longpress routine
 *
 * @param   Key - button struct
 *
 * @return  void
 */
static void ButtonPressedRoutine(Button_t* Key)
{
	// Check if button was released
	if(!ButtonIsPressed(Key))
	{
#if BUTTON_RELEASE_ACTION
		// If released go to RELEASE state
		Key->State = RELEASE;
#else
		// If released - go back to IDLE state
		Key->State = IDLE;
#endif
	}
	else
	{
		// If button is still pressed
		if((HAL_GetTick() - Key->LastTick) > Key->TimerLongPress) // Check if Long Press Timer elapsed
		{
			Key->State = REPEAT; // Jump to REPEAT State
			Key->LastTick = HAL_GetTick(); // Remember current tick for Repeat Timer

			if(Key->ButtonLongPressed != NULL) // Check if callback for Long Press exists
			{
				Key->ButtonLongPressed(); // If exists - do the callback function
			}
		}
	}
}

/**
 * @desc   Button repeat routine, if button is held for certain time execute this function
 *
 * @param   Key - button struct
 *
 * @return  void
 */
static void ButtonRepeatRoutine(Button_t* Key)
{
	// Check if button was released
	if(!ButtonIsPressed(Key))
	{
#if BUTTON_RELEASE_ACTION
		// If released go to RELEASE state
		Key->State = RELEASE;
#else
		// If released - go back to IDLE state
		Key->State = IDLE;
#endif
	}
	else
	{
		// If button is still pressed
		if((HAL_GetTick() - Key->LastTick) > Key->TimerRepeat) // Check if Repeat Timer elapsed
		{
			Key->LastTick = HAL_GetTick(); // Reload last tick for next Repeat action

			if(Key->ButtonRepeat != NULL) // Check if callback for repeat action exists
			{
				Key->ButtonRepeat(); // If exists - do the callback function
			}
		}
	}
}

/**
 * @desc   Button release routine, execute after release the button
 *
 * @param   Key - button struct
 *
 * @return  void
 */
static void ButtonReleaseRoutine(Button_t* Key)
{
	Key->State = IDLE;

	if(Key->ButtonRelease != NULL) // Check if callback for release action exists
		{
			Key->ButtonRelease(); // If exists - do the callback function
		}
}

/**
 * @desc   Button state machine
 *
 * @param   Key - button struct
 *
 * @return  void
 */
void ButtonTask(Button_t* Key)
{
	switch(Key->State)
	{
		case IDLE:
			// do IDLE
			ButtonIdleRoutine(Key);
			break;

		case DEBOUNCE:
			// do DEBOUNCE
			ButtonDebounceRoutine(Key);
			break;

		case PRESSED:
			// do PRESSED
			ButtonPressedRoutine(Key);
			break;

		case REPEAT:
			// do REPEAT
			ButtonRepeatRoutine(Key);
			break;

		case RELEASE:
			// do RELEASE
			ButtonReleaseRoutine(Key);
			break;

		default:
			break;
	}
}

/**
 * @desc   Button interrupt handler - call this function from GPIO EXTI interrupt
 *         This function should be called from HAL_GPIO_EXTI_Callback
 *         Automatically checks if the interrupt is for the button's GPIO pin
 *
 * @param   Key - button struct
 * @param   GPIO_Pin - pin that triggered the interrupt (from HAL_GPIO_EXTI_Callback)
 *
 * @return  void
 */
void ButtonIRQHandler(Button_t* Key, uint16_t GPIO_Pin)
{
	// Check if interrupt is for this button's pin and button is in INTERRUPT mode
	if(Key->IOMode == BUTTON_MODE_INTERRUPT && GPIO_Pin == Key->GpioPin)
	{
		// Set flag to be processed in ButtonTask
		// Flag is set regardless of current state to not lose interrupts
		// It will be processed when button returns to IDLE state
		Key->InterruptFlag = 1;
	}
}