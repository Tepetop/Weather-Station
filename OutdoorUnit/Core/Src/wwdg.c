#include "wwdg.h"

void MX_WWDG_Init(void)
{
  /*
   * PCLK1=36 MHz, prescaler=8 and counter=127 give about 58 ms.
   * Window=127 permits a refresh at any time while the counter is valid.
   */
  __HAL_RCC_WWDG_CLK_ENABLE();
  WRITE_REG(WWDG->CFR, WWDG_CFR_WDGTB | WWDG_CFR_W);
  WRITE_REG(WWDG->CR, WWDG_CR_WDGA | WWDG_CR_T);
}

void WWDG_Refresh(void)
{
  if (((WWDG->CR & WWDG_CR_WDGA) != 0U) &&
      ((WWDG->CR & WWDG_CR_T) >= WWDG_CR_T_6))
  {
    WRITE_REG(WWDG->CR, WWDG_CR_WDGA | WWDG_CR_T);
  }
}

void WWDG_Delay(uint32_t delay_ms)
{
  uint32_t start_tick = HAL_GetTick();

  WWDG_Refresh();
  while ((HAL_GetTick() - start_tick) < delay_ms)
  {
    HAL_Delay(1U);
    WWDG_Refresh();
  }
}
