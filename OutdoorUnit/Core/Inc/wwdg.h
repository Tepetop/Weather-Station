#ifndef __WWDG_H__
#define __WWDG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void MX_WWDG_Init(void);

/**
 * @brief Refresh the window watchdog if it is running.
 */
void WWDG_Refresh(void);

/**
 * @brief Blocking delay which keeps the window watchdog serviced.
 * @param delay_ms Delay in milliseconds.
 */
void WWDG_Delay(uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif /* __WWDG_H__ */
