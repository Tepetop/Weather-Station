/**
 * @file    power_mgr.h
 * @brief   Outdoor MCU STOP while NRF remains in RX
 */

#ifndef POWER_MGR_H
#define POWER_MGR_H

#include "main.h"

/**
 * @brief   Init RTC (LSI) used only to pet IWDG during idle STOP
 */
void PowerMgr_Init(void);

/**
 * @brief   Enter STOP; wake on NRF IRQ (EXTI) or RTC alarm (IWDG pet)
 * @details Restores system clock after wake. Caller must re-check CanSleep.
 */
void PowerMgr_EnterIdleStop(void);

#endif /* POWER_MGR_H */
