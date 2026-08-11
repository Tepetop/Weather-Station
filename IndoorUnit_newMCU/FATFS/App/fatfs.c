/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
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
#include "fatfs.h"

uint8_t retUSER;    /* Return value for USER */
char USERPath[4];   /* USER logical drive path */
FATFS USERFatFS;    /* File system object for USER logical drive */
FIL USERFile;       /* File object for USER */

/* USER CODE BEGIN Variables */
extern DS3231_DateTime rtcNow;
/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the USER driver ###########################*/
  retUSER = FATFS_LinkDriver(&USER_Driver, USERPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  /* Pack as FatFs: bit31:25 year-1980, 24:21 month, 20:16 day, 15:11 hour, 10:5 min, 4:0 sec/2 */
  uint8_t year = rtcNow.year; /* 0–99 → 2000+year */
  uint16_t fat_year = (uint16_t)(2000U + (uint16_t)year);
  if (fat_year < 1980U) {
    fat_year = 1980U;
  }

  return ((DWORD)(fat_year - 1980U) << 25) |
         ((DWORD)rtcNow.month << 21) |
         ((DWORD)rtcNow.date << 16) |
         ((DWORD)rtcNow.hours << 11) |
         ((DWORD)rtcNow.minutes << 5) |
         ((DWORD)rtcNow.seconds >> 1);
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */
