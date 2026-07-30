/**
 * @file    power_mgr.c
 * @brief   Outdoor MCU STOP + RTC alarm IWDG pet (NRF stays in RX)
 */

#include "power_mgr.h"

#include "iwdg.h"
#include "measurement_unit_config.h"

RTC_HandleTypeDef hrtc;

static void PowerMgr_ScheduleIwdgPet(void);

void PowerMgr_Init(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_PeriphCLKInitTypeDef periph = {0};
  RTC_TimeTypeDef time = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
  osc.LSIState = RCC_LSI_ON;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    Error_Handler_WithName("PowerMgr_LSI");
  }

  periph.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  periph.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK)
  {
    Error_Handler_WithName("PowerMgr_RTCClk");
  }

  __HAL_RCC_RTC_ENABLE();

  hrtc.Instance = RTC;
  hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
  hrtc.Init.OutPut = RTC_OUTPUTSOURCE_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler_WithName("PowerMgr_RTCInit");
  }

  if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler_WithName("PowerMgr_SetTime");
  }
}

void PowerMgr_EnterIdleStop(void)
{
  PowerMgr_ScheduleIwdgPet();

  HAL_SuspendTick();
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

  SystemClock_Config();
  HAL_ResumeTick();
  (void)HAL_IWDG_Refresh(&hiwdg);
}

static void PowerMgr_ScheduleIwdgPet(void)
{
  RTC_TimeTypeDef now = {0};
  RTC_AlarmTypeDef alarm = {0};
  uint32_t total_sec;

  if (HAL_RTC_GetTime(&hrtc, &now, RTC_FORMAT_BIN) != HAL_OK)
  {
    return;
  }

  total_sec = ((uint32_t)now.Hours * 3600U) +
              ((uint32_t)now.Minutes * 60U) +
              (uint32_t)now.Seconds +
              (uint32_t)OUTDOOR_IWDG_PET_SEC;
  total_sec %= 86400U;

  alarm.AlarmTime.Hours = (uint8_t)(total_sec / 3600U);
  alarm.AlarmTime.Minutes = (uint8_t)((total_sec % 3600U) / 60U);
  alarm.AlarmTime.Seconds = (uint8_t)(total_sec % 60U);
  alarm.Alarm = RTC_ALARM_A;

  (void)HAL_RTC_DeactivateAlarm(&hrtc, RTC_ALARM_A);
  (void)HAL_RTC_SetAlarm_IT(&hrtc, &alarm, RTC_FORMAT_BIN);
}

void HAL_RTC_MspInit(RTC_HandleTypeDef *rtc)
{
  if (rtc->Instance != RTC)
  {
    return;
  }

  HAL_NVIC_SetPriority(RTC_Alarm_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(RTC_Alarm_IRQn);
}

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *rtc)
{
  (void)rtc;
  (void)HAL_IWDG_Refresh(&hiwdg);
}
