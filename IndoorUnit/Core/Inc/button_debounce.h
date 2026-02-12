/*
 * button_debounce.h
 *
 *  Created on: Sep 6, 2024
 *      Author: remik
 */

#ifndef INC_BUTTON_DEBOUNCE_H_
#define INC_BUTTON_DEBOUNCE_H_

/*  	Toggle value to add release routine support ( 1 -> HIGH, 0 -> LOW) */
#define BUTTON_RELEASE_ACTION 0

// Button IO Mode - POLLING or INTERRUPT
typedef enum
{
	BUTTON_MODE_POLLING = 0,
	BUTTON_MODE_INTERRUPT = 1
} BUTTON_IO_MODE;

// States for state machine
typedef enum
{
	IDLE = 0,
	DEBOUNCE,
	PRESSED,
	REPEAT,
	RELEASE
} BUTTON_STATE;

// Struct for button

typedef struct
{
	BUTTON_STATE 	State; // Button current state
	BUTTON_IO_MODE 	IOMode; // IO Mode - Polling or Interrupt

	GPIO_TypeDef* 	GpioPort; // GPIO Port for a button
	uint16_t		GpioPin; // GPIO Pin for a button

	uint32_t		LastTick; // Last remembered time before steps
	uint32_t		TimerDebounce; // Fixed, settable time for debounce timer
	uint32_t		TimerLongPress; // Fixed, settable time for long press timer
	uint32_t		TimerRepeat; // Fixed, settable time for repeat timer

	uint8_t			InterruptFlag; // Flag set in interrupt, cleared in task

	void(*ButtonPressed)(void); // A callback for buttos pressed
	void(*ButtonLongPressed)(void); // A callback for long press
	void(*ButtonRepeat)(void); // A callback for repeat
	void(*ButtonRelease)(void); // A callback for release
} Button_t;

/* 				Functions declarations								*/
void ButtonTask(Button_t* Key);
void ButtonIRQHandler(Button_t* Key, uint16_t GPIO_Pin);
void ButtonSetDebounceTime(Button_t* Key, uint32_t Milliseconds);
void ButtonSetLongPressTime(Button_t* Key, uint32_t Milliseconds);
void ButtonSetRepeatTime(Button_t* Key, uint32_t Milliseconds);
void ButtonRegisterPressCallback(Button_t* Key, void (*Callback)(void));
void ButtonRegisterLongPressCallback(Button_t* Key, void (*Callback)(void));
void ButtonRegisterRepeatCallback(Button_t* Key, void (*Callback)(void));
void ButtonRegisterReleaseCalllback(Button_t* Key, void(*Callback)(void));
void ButtonInitKey(Button_t* Key, GPIO_TypeDef* GpioPort, uint16_t GpioPin, 
					uint32_t TimerDebounce, uint32_t TimerLongPress, uint32_t TimerRepeat, 
					BUTTON_IO_MODE IOMode);



#endif /* INC_BUTTON_DEBOUNCE_H_ */