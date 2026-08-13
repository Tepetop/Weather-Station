/**
 * @file    power_mgr.c
 * @brief   Indoor MCU STOP with NRF in Power Down between cycles
 * @details Puts the nRF24 into Power Down, enters STOP (WFI), then restores
 *          the system clock and refreshes WWDG on wake. Radio register
 *          configuration is retained across Power Down.
 */

#include "power_mgr.h"

#include "wwdg.h"

/** 1 if NRF was powered down by `PowerMgr_EnterIdleStop()`. */
static uint8_t radio_asleep = 0U;

/**
 * @brief Reset local power-manager state
 * @details Clears the radio-asleep flag. WWDG freezes in STOP, so no RTC
 *          keepalive is required here.
 */
void PowerMgr_Init(void)
{
  radio_asleep = 0U;
}

/**
 * @brief Power down NRF (if awake), enter STOP, restore clocks on wake
 * @param[in] nrf Radio handle
 * @note Returns immediately if `nrf` is NULL. After STOP, `SystemClock_Config()`
 *       must re-enable HSE/PLL because STOP disables them.
 */
void PowerMgr_EnterIdleStop(NRF24_Handle_t *nrf)
{
  if (nrf == NULL)
  {
    return;
  }

  if (radio_asleep == 0U)
  {
    (void)NRF24_PowerDown(nrf);
    radio_asleep = 1U;
  }

  HAL_SuspendTick();
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

  SystemClock_Config();
  HAL_ResumeTick();
  (void)HAL_WWDG_Refresh(&hwwdg);
}

/**
 * @brief Power up NRF after idle Power Down
 * @param[in] nrf Radio handle
 * @details No-op if `nrf` is NULL or the radio was not put to sleep by
 *          `PowerMgr_EnterIdleStop()`.
 */
void PowerMgr_WakeRadio(NRF24_Handle_t *nrf)
{
  if ((nrf == NULL) || (radio_asleep == 0U))
  {
    return;
  }

  (void)NRF24_PowerUp(nrf);
  radio_asleep = 0U;
}

/**
 * @brief Query whether NRF is in Power Down after idle STOP
 * @retval 1 Radio was powered down by `PowerMgr_EnterIdleStop()`
 * @retval 0 Radio is considered awake
 */
uint8_t PowerMgr_IsRadioAsleep(void)
{
  return radio_asleep;
}
