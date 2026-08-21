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
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

  SystemClock_Config();
  __enable_irq();
  HAL_ResumeTick();

  Debug_Log("PWR:EXIT");
}
