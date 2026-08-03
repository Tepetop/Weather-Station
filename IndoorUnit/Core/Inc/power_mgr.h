/**
 * @file    power_mgr.h
 * @brief   Indoor MCU STOP + NRF PowerDown between measurement cycles
 */

#ifndef POWER_MGR_H
#define POWER_MGR_H

#include "main.h"
#include "NRF24L01.h"

/**
 * @brief   No-op placeholder (WWDG freezes in STOP — no RTC pet needed)
 */
void PowerMgr_Init(void);

/**
 * @brief   PowerDown NRF (if awake), enter STOP, restore clocks on wake
 * @param   nrf  Radio handle
 */
void PowerMgr_EnterIdleStop(NRF24_Handle_t *nrf);

/**
 * @brief   PowerUp NRF after idle PowerDown (register config retained)
 * @param   nrf  Radio handle
 */
void PowerMgr_WakeRadio(NRF24_Handle_t *nrf);

/**
 * @brief   1 if NRF was put to PowerDown by PowerMgr_EnterIdleStop
 */
uint8_t PowerMgr_IsRadioAsleep(void);

#endif /* POWER_MGR_H */
