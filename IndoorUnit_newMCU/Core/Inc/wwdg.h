/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    wwdg.h
  * @brief   This file contains all the function prototypes for
  *          the wwdg.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __WWDG_H__
#define __WWDG_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern WWDG_HandleTypeDef hwwdg;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_WWDG_Init(void);

/* USER CODE BEGIN Prototypes */

/**
 * @brief Refresh WWDG only after its refresh window has opened.
 *
 * Safe to call frequently and before MX_WWDG_Init().
 */
void WWDG_TryRefresh(void);

/**
 * @brief Block until the refresh window is open, then reload the counter.
 *
 * Use after STOP: PCLK is slow (HSI) during clock restore, so the counter
 * can still sit at 127. Writing CR then is a window violation and resets.
 */
void WWDG_WaitRefresh(void);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __WWDG_H__ */

