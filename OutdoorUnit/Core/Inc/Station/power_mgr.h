/**
 * @file    power_mgr.h
 * @brief   Outdoor MCU STOP while nRF24 stays in RX
 * @details Enters STM32 STOP between measurement commands. The radio is left
 *          in RX (`PWR_UP=1`, `PRIM_RX=1`, `CE=1`) so an incoming packet can
 *          pull IRQ low and wake the MCU via EXTI4.
 */

#ifndef POWER_MGR_H
#define POWER_MGR_H

#include "main.h"

/**
 * @brief Enter STOP with nRF24 left in RX, then restore clocks on wake
 * @details Suspends SysTick, skips STOP if NRF IRQ is already asserted or
 *          EXTI4 is pending, otherwise enters STOP (WFI). After wake,
 *          `SystemClock_Config()` re-enables HSI/PLL because STOP disables
 *          them. Does not change nRF mode, CE, or PWR_UP.
 */
void PowerMgr_EnterIdleStop(void);

#endif /* POWER_MGR_H */
