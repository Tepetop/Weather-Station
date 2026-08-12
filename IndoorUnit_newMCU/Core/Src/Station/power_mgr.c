/**
 * @file    power_mgr.c
 * @brief   Indoor MCU STOP with NRF in Power Down between cycles
 */

#include "power_mgr.h"

#include "wwdg.h"

static uint8_t radio_asleep = 0U;

void PowerMgr_Init(void)
{
  radio_asleep = 0U;
}

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

void PowerMgr_WakeRadio(NRF24_Handle_t *nrf)
{
  if ((nrf == NULL) || (radio_asleep == 0U))
  {
    return;
  }

  (void)NRF24_PowerUp(nrf);
  radio_asleep = 0U;
}

uint8_t PowerMgr_IsRadioAsleep(void)
{
  return radio_asleep;
}
