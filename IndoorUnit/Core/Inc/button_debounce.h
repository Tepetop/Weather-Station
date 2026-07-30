/**
 * @file    button_debounce.h
 * @brief   GPIO button debounce library with press, long-press, and repeat
 * @details Software state machine for active-low buttons. Supports polling
 *          or EXTI interrupt mode. Call ButtonTask() from the main loop and
 *          ButtonIRQHandler() from HAL_GPIO_EXTI_Callback when using
 *          interrupt mode.
 */

#ifndef INC_BUTTON_DEBOUNCE_H_
#define INC_BUTTON_DEBOUNCE_H_

/** @brief Set to 1 to enable release-state handling and release callback */
#define BUTTON_RELEASE_ACTION 0

/**
 * @brief Button input sampling mode
 */
typedef enum
{
	BUTTON_MODE_POLLING = 0,   /**< Sample GPIO in ButtonTask() */
	BUTTON_MODE_INTERRUPT = 1  /**< Latch EXTI events, process in ButtonTask() */
} BUTTON_IO_MODE;

/**
 * @brief Button state machine states
 */
typedef enum
{
	IDLE = 0,     /**< Waiting for press or interrupt */
	DEBOUNCE,     /**< Debounce timer running */
	PRESSED,      /**< Stable press detected */
	REPEAT,       /**< Long-press threshold reached, repeat active */
	RELEASE       /**< Release handling (when BUTTON_RELEASE_ACTION is 1) */
} BUTTON_STATE;

/**
 * @brief Button instance with timing and callback configuration
 */
typedef struct
{
	volatile BUTTON_STATE State; /**< Current state machine state */
	BUTTON_IO_MODE      IOMode;  /**< Polling or interrupt mode */

	GPIO_TypeDef *GpioPort; /**< GPIO port of the button pin */
	uint16_t      GpioPin;  /**< GPIO pin number */

	uint32_t LastTick;       /**< HAL_GetTick() snapshot for timers */
	uint32_t TimerDebounce;  /**< Debounce duration in milliseconds */
	uint32_t TimerLongPress; /**< Long-press threshold in milliseconds */
	uint32_t TimerRepeat;    /**< Repeat interval in milliseconds */

	volatile uint8_t InterruptFlag; /**< Set in EXTI ISR, cleared in task */

	void (*ButtonPressed)(void);     /**< Callback on stable press */
	void (*ButtonLongPressed)(void); /**< Callback on long press */
	void (*ButtonRepeat)(void);      /**< Callback on repeat while held */
	void (*ButtonRelease)(void);     /**< Callback on release */
} Button_t;

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
void ButtonInitKey(Button_t *Key, GPIO_TypeDef *GpioPort, uint16_t GpioPin,
                   uint32_t TimerDebounce, uint32_t TimerLongPress, uint32_t TimerRepeat,
                   BUTTON_IO_MODE IOMode);

/**
 * @brief   Runs one step of the button state machine (call from main loop)
 * @param   Key  Button handle
 * @retval  None
 */
void ButtonTask(Button_t *Key);

/**
 * @brief   EXTI callback helper for interrupt mode
 * @param   Key       Button handle
 * @param   GPIO_Pin  Pin that triggered the interrupt (from EXTI callback)
 * @retval  None
 * @details Call from HAL_GPIO_EXTI_Callback. Latches an event only when idle.
 */
void ButtonIRQHandler(Button_t *Key, uint16_t GPIO_Pin);

/**
 * @brief   Sets debounce duration
 * @param   Key          Button handle
 * @param   Milliseconds Debounce time in milliseconds
 * @retval  None
 */
void ButtonSetDebounceTime(Button_t *Key, uint32_t Milliseconds);

/**
 * @brief   Sets long-press threshold
 * @param   Key          Button handle
 * @param   Milliseconds Long-press time in milliseconds
 * @retval  None
 */
void ButtonSetLongPressTime(Button_t *Key, uint32_t Milliseconds);

/**
 * @brief   Sets repeat interval while button is held
 * @param   Key          Button handle
 * @param   Milliseconds Repeat interval in milliseconds
 * @retval  None
 */
void ButtonSetRepeatTime(Button_t *Key, uint32_t Milliseconds);

/**
 * @brief   Registers callback for stable press event
 * @param   Key       Button handle
 * @param   Callback  Function to call on press (may be NULL)
 * @retval  None
 */
void ButtonRegisterPressCallback(Button_t *Key, void (*Callback)(void));

/**
 * @brief   Registers callback for long-press event
 * @param   Key       Button handle
 * @param   Callback  Function to call on long press (may be NULL)
 * @retval  None
 */
void ButtonRegisterLongPressCallback(Button_t *Key, void (*Callback)(void));

/**
 * @brief   Registers callback for repeat-while-held event
 * @param   Key       Button handle
 * @param   Callback  Function to call on each repeat (may be NULL)
 * @retval  None
 */
void ButtonRegisterRepeatCallback(Button_t *Key, void (*Callback)(void));

/**
 * @brief   Registers callback for release event
 * @param   Key       Button handle
 * @param   Callback  Function to call on release (may be NULL)
 * @retval  None
 */
void ButtonRegisterReleaseCalllback(Button_t *Key, void (*Callback)(void));

#endif /* INC_BUTTON_DEBOUNCE_H_ */
