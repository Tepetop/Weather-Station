/**
 * @file    power_mgr.c
 * @brief   Outdoor MCU STOP with nRF24 remaining in RX
 * @details nRF24 IRQ is active-low and level-held until STATUS is cleared.
 *          EXTI4 is edge-triggered, so STOP is entered only when the pin is
 *          high and no EXTI pending bit is set. Interrupts stay masked around
 *          that check and WFI so a packet in the race window pendings NVIC
 *          and returns from WFI instead of being lost.
 */

#include "power_mgr.h"

#include "debug_log.h"

/**
 * @brief True when NRF IRQ already requests a wake (pin low or EXTI pending)
 * @retval 1 IRQ is asserted or EXTI4 pending
 * @retval 0 IRQ is idle
 */
static uint8_t PowerMgr_IrqWakePending(void)
{
  if (HAL_GPIO_ReadPin(NRF_IRQ_GPIO_Port, NRF_IRQ_Pin) == GPIO_PIN_RESET)
  {
    return 1U;
  }

  if (__HAL_GPIO_EXTI_GET_IT(NRF_IRQ_Pin) != RESET)
  {
    return 1U;
  }

  return 0U;
}

/**
 * @brief Enter STOP with nRF24 left in RX, then restore clocks on wake
 * @note Returns without STOP if IRQ is already asserted. After STOP,
 *       `SystemClock_Config()` must run before SPI or DWT delays.
 */
void PowerMgr_EnterIdleStop(void)
{
  HAL_SuspendTick();
  __disable_irq();

  if (PowerMgr_IrqWakePending() != 0U)
  {
    __enable_irq();
    HAL_ResumeTick();
    return;
  }

  Debug_Log("PWR:ENTER");
  PowerMgr_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

  SystemClock_Config();
  __enable_irq();
  HAL_ResumeTick();

  Debug_Log("PWR:EXIT");
}


/**
  * @brief Enters Stop mode. 
  * @note  In Stop mode, all I/O pins keep the same state as in Run mode.
  * @note  When exiting Stop mode by using an interrupt or a wakeup event,
  *        HSI RC oscillator is selected as system clock.
  * @note  When the voltage regulator operates in low power mode, an additional
  *         startup delay is incurred when waking up from Stop mode. 
  *         By keeping the internal regulator ON during Stop mode, the consumption
  *         is higher although the startup time is reduced.    
  * @param Regulator: Specifies the regulator state in Stop mode.
  *          This parameter can be one of the following values:
  *            @arg PWR_MAINREGULATOR_ON: Stop mode with regulator ON
  *            @arg PWR_LOWPOWERREGULATOR_ON: Stop mode with low power regulator ON
  * @param STOPEntry: Specifies if Stop mode in entered with WFI or WFE instruction.
  *          This parameter can be one of the following values:
  *            @arg PWR_STOPENTRY_WFI: Enter Stop mode with WFI instruction
  *            @arg PWR_STOPENTRY_WFE: Enter Stop mode with WFE instruction   
  * @retval None
  */
void PowerMgr_EnterSTOPMode(uint32_t Regulator, uint8_t STOPEntry)
{
  /* Check the parameters */
  assert_param(IS_PWR_REGULATOR(Regulator));
  assert_param(IS_PWR_STOP_ENTRY(STOPEntry));

  /* Clear PDDS bit in PWR register to specify entering in STOP mode when CPU enter in Deepsleep */ 
  CLEAR_BIT(PWR->CR,  PWR_CR_PDDS);

  /* Select the voltage regulator mode by setting LPDS bit in PWR register according to Regulator parameter value */
  MODIFY_REG(PWR->CR, PWR_CR_LPDS, Regulator);

  /* Set SLEEPDEEP bit of Cortex System Control Register */
  SET_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));

  /* Select Stop mode entry --------------------------------------------------*/
  if(STOPEntry == PWR_STOPENTRY_WFI)
  {
    /* Complete outstanding memory accesses before STOP and flush the
       instruction pipeline immediately after wake-up. */
    __DSB();
    __WFI();
    __ISB();
  }
  else
  {
    /* Request Wait For Event */
    __DSB();
    __SEV();
    /* WFE redefined locally to avoid race with EXTI4 pending bit. See PowerMgr_IrqWakePending() */
    __asm volatile( "wfe" );
    __asm volatile( "nop" );
    __ISB();
  }
  /* Reset SLEEPDEEP bit of Cortex System Control Register */
  CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPDEEP_Msk));
}
