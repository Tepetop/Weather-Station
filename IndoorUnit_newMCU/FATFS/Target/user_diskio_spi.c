/**
 ******************************************************************************
 * @file user_diskio_spi.c
 * @brief FatFs SPI disk driver for an MMC/SD card.
 ******************************************************************************
 * Portions copyright (C) 2014, ChaN, all rights reserved.
 * Portions copyright (C) 2017, kiwih, all rights reserved.
 *
 * This software is free software and there is NO WARRANTY.
 * No restriction on use. You can use, modify and redistribute it for
 * personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
 * Redistributions of source code must retain the above copyright notice.
 ******************************************************************************
 *
 * Based on kiwih/cubeide-sd-card, itself ported from ChaN's mmc_spi sample.
 * Adapted for STM32F103 SPI1 shared with a PCD8544 display.
 */

#include "user_diskio_spi.h"

#include "main.h"
#include "wwdg.h"

#define CMD0     0U          /* GO_IDLE_STATE */
#define CMD1     1U          /* SEND_OP_COND (MMC) */
#define ACMD41   (0x80U + 41U) /* SEND_OP_COND (SDC) */
#define CMD8     8U          /* SEND_IF_COND */
#define CMD9     9U          /* SEND_CSD */
#define CMD12    12U         /* STOP_TRANSMISSION */
#define ACMD13   (0x80U + 13U) /* SD_STATUS */
#define CMD16    16U         /* SET_BLOCKLEN */
#define CMD17    17U         /* READ_SINGLE_BLOCK */
#define CMD18    18U         /* READ_MULTIPLE_BLOCK */
#define ACMD23   (0x80U + 23U) /* SET_WR_BLK_ERASE_COUNT */
#define CMD24    24U         /* WRITE_BLOCK */
#define CMD25    25U         /* WRITE_MULTIPLE_BLOCK */
#define CMD55    55U         /* APP_CMD */
#define CMD58    58U         /* READ_OCR */

#define USER_SPI_INIT_TIMEOUT_MS 1000U
#define USER_SPI_READY_TIMEOUT_MS 500U
#define USER_SPI_TOKEN_TIMEOUT_MS 200U
#define USER_SPI_XFER_TIMEOUT_MS  200U

/*
 * SPI1 is clocked from APB2 at 72 MHz. /256 gives 281.25 kHz during card
 * initialization (the SD limit is 400 kHz), while /16 gives 4.5 MHz in use.
 */
#define USER_SPI_SLOW_PRESCALER SPI_BAUDRATEPRESCALER_256
#define USER_SPI_FAST_PRESCALER SPI_BAUDRATEPRESCALER_16

#define SD_CS_HIGH() HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET)
#define SD_CS_LOW()                                                            \
  do {                                                                         \
    HAL_GPIO_WritePin(LCD_CE_GPIO_Port, LCD_CE_Pin, GPIO_PIN_SET);              \
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);              \
  } while (0)

extern SPI_HandleTypeDef SD_SPI_HANDLE;

static volatile DSTATUS Stat = STA_NOINIT;
static BYTE CardType = 0U;
static USER_SPI_Error LastError = USER_SPI_ERR_NONE;
static uint8_t HalFailed = 0U;

static void set_error(USER_SPI_Error error)
{
  if (LastError == USER_SPI_ERR_NONE) {
    LastError = error;
  }
}

static void set_spi_prescaler(uint32_t prescaler)
{
  __HAL_SPI_DISABLE(&SD_SPI_HANDLE);
  MODIFY_REG(SD_SPI_HANDLE.Instance->CR1, SPI_CR1_BR, prescaler);
  SD_SPI_HANDLE.Init.BaudRatePrescaler = prescaler;
  __HAL_SPI_ENABLE(&SD_SPI_HANDLE);
}

static BYTE xchg_spi(BYTE value)
{
  BYTE received = 0xFFU;

  if (HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &value, &received, 1U,
                              USER_SPI_XFER_TIMEOUT_MS) != HAL_OK) {
    HalFailed = 1U;
    set_error(USER_SPI_ERR_HAL);
    return 0xFFU;
  }

  return received;
}

static int receive_multi(BYTE *buffer, UINT count)
{
  while (count > 0U) {
    *buffer++ = xchg_spi(0xFFU);
    if (HalFailed != 0U) {
      return 0;
    }
    count--;
  }
  return 1;
}

#if _USE_WRITE == 1
static int transmit_multi(const BYTE *buffer, UINT count)
{
  if (HAL_SPI_Transmit(&SD_SPI_HANDLE, (BYTE *)buffer, count,
                       USER_SPI_XFER_TIMEOUT_MS) != HAL_OK) {
    HalFailed = 1U;
    set_error(USER_SPI_ERR_HAL);
    return 0;
  }
  return 1;
}
#endif

static int wait_ready(UINT timeout_ms)
{
  uint32_t start = HAL_GetTick();
  BYTE value;

  do {
    value = xchg_spi(0xFFU);
    if (HalFailed != 0U) {
      return 0;
    }
    if (value == 0xFFU) {
      return 1;
    }
    WWDG_TryRefresh();
  } while ((HAL_GetTick() - start) < timeout_ms);

  set_error(USER_SPI_ERR_TIMEOUT);
  return 0;
}

static void deselect_card(void)
{
  SD_CS_HIGH();
  (void)xchg_spi(0xFFU);
}

static int select_card(void)
{
  SD_CS_LOW();
  (void)xchg_spi(0xFFU);
  if ((HalFailed == 0U) && wait_ready(USER_SPI_READY_TIMEOUT_MS)) {
    return 1;
  }

  deselect_card();
  return 0;
}

static int receive_datablock(BYTE *buffer, UINT length)
{
  uint32_t start = HAL_GetTick();
  BYTE token;

  do {
    token = xchg_spi(0xFFU);
    if (HalFailed != 0U) {
      return 0;
    }
    WWDG_TryRefresh();
  } while ((token == 0xFFU) &&
           ((HAL_GetTick() - start) < USER_SPI_TOKEN_TIMEOUT_MS));

  if (token != 0xFEU) {
    set_error(USER_SPI_ERR_TIMEOUT);
    return 0;
  }
  if (!receive_multi(buffer, length)) {
    return 0;
  }

  (void)xchg_spi(0xFFU);
  (void)xchg_spi(0xFFU);
  return (HalFailed == 0U) ? 1 : 0;
}

#if _USE_WRITE == 1
static int transmit_datablock(const BYTE *buffer, BYTE token)
{
  BYTE response;

  if (!wait_ready(USER_SPI_READY_TIMEOUT_MS)) {
    return 0;
  }

  (void)xchg_spi(token);
  if (token == 0xFDU) {
    return (HalFailed == 0U) ? 1 : 0;
  }
  if ((buffer == NULL) || !transmit_multi(buffer, 512U)) {
    return 0;
  }

  (void)xchg_spi(0xFFU);
  (void)xchg_spi(0xFFU);
  response = xchg_spi(0xFFU);
  if ((response & 0x1FU) != 0x05U) {
    return 0;
  }

  return wait_ready(USER_SPI_READY_TIMEOUT_MS);
}
#endif

static BYTE send_command(BYTE command, DWORD argument)
{
  BYTE attempts;
  BYTE response;

  if ((command & 0x80U) != 0U) {
    command &= 0x7FU;
    response = send_command(CMD55, 0U);
    if (response > 1U) {
      return response;
    }
  }

  if (command != CMD12) {
    deselect_card();
    if (!select_card()) {
      return 0xFFU;
    }
  }

  (void)xchg_spi((BYTE)(0x40U | command));
  (void)xchg_spi((BYTE)(argument >> 24));
  (void)xchg_spi((BYTE)(argument >> 16));
  (void)xchg_spi((BYTE)(argument >> 8));
  (void)xchg_spi((BYTE)argument);

  attempts = 0x01U;
  if (command == CMD0) {
    attempts = 0x95U;
  } else if (command == CMD8) {
    attempts = 0x87U;
  }
  (void)xchg_spi(attempts);

  if (command == CMD12) {
    (void)xchg_spi(0xFFU);
  }
  attempts = 10U;
  do {
    response = xchg_spi(0xFFU);
  } while (((response & 0x80U) != 0U) && (--attempts > 0U));

  return response;
}

DSTATUS USER_SPI_initialize(BYTE drive)
{
  BYTE command;
  BYTE n;
  BYTE ocr[4];
  BYTE type = 0U;
  uint32_t start;

  Stat = STA_NOINIT;
  CardType = 0U;
  LastError = USER_SPI_ERR_NONE;
  HalFailed = 0U;

  if (drive != USER_SPI_PDRV) {
    LastError = USER_SPI_ERR_BAD_DRIVE;
    return Stat;
  }
  if ((SD_SPI_HANDLE.Instance == NULL) ||
      (SD_SPI_HANDLE.Init.Mode != SPI_MODE_MASTER) ||
      (SD_SPI_HANDLE.Init.Direction != SPI_DIRECTION_2LINES) ||
      (SD_SPI_HANDLE.Init.DataSize != SPI_DATASIZE_8BIT) ||
      (SD_SPI_HANDLE.Init.CLKPolarity != SPI_POLARITY_LOW) ||
      (SD_SPI_HANDLE.Init.CLKPhase != SPI_PHASE_1EDGE) ||
      (SD_SPI_HANDLE.Init.NSS != SPI_NSS_SOFT) ||
      (SD_SPI_HANDLE.Init.FirstBit != SPI_FIRSTBIT_MSB)) {
    LastError = USER_SPI_ERR_BAD_CONFIG;
    return Stat;
  }

  SD_CS_HIGH();
  HAL_GPIO_WritePin(LCD_CE_GPIO_Port, LCD_CE_Pin, GPIO_PIN_SET);
  set_spi_prescaler(USER_SPI_SLOW_PRESCALER);
  for (n = 10U; n > 0U; n--) {
    (void)xchg_spi(0xFFU);
  }

  if ((HalFailed == 0U) && (send_command(CMD0, 0U) == 1U)) {
    start = HAL_GetTick();
    if (send_command(CMD8, 0x1AAU) == 1U) {
      if (receive_multi(ocr, 4U) &&
          (ocr[2] == 0x01U) && (ocr[3] == 0xAAU)) {
        while (((HAL_GetTick() - start) < USER_SPI_INIT_TIMEOUT_MS) &&
               (send_command(ACMD41, 1UL << 30) != 0U)) {
          WWDG_TryRefresh();
        }
        if ((HAL_GetTick() - start) >= USER_SPI_INIT_TIMEOUT_MS) {
          set_error(USER_SPI_ERR_INIT_TIMEOUT);
        } else if (send_command(CMD58, 0U) == 0U) {
          if (receive_multi(ocr, 4U)) {
            type = USER_SPI_CT_SD2;
            if ((ocr[0] & 0x40U) != 0U) {
              type |= USER_SPI_CT_BLOCK;
            }
          }
        } else {
          set_error(USER_SPI_ERR_CMD58);
        }
      } else {
        set_error(USER_SPI_ERR_IFCOND);
      }
    } else {
      if (send_command(ACMD41, 0U) <= 1U) {
        type = USER_SPI_CT_SD1;
        command = ACMD41;
      } else {
        type = USER_SPI_CT_MMC;
        command = CMD1;
      }

      while (((HAL_GetTick() - start) < USER_SPI_INIT_TIMEOUT_MS) &&
             (send_command(command, 0U) != 0U)) {
        WWDG_TryRefresh();
      }
      if (((HAL_GetTick() - start) >= USER_SPI_INIT_TIMEOUT_MS) ||
          (send_command(CMD16, 512U) != 0U)) {
        type = 0U;
        set_error(((HAL_GetTick() - start) >= USER_SPI_INIT_TIMEOUT_MS)
                      ? USER_SPI_ERR_INIT_TIMEOUT
                      : USER_SPI_ERR_CMD16);
      }
    }
  } else if (HalFailed == 0U) {
    set_error(USER_SPI_ERR_CMD0);
  }

  CardType = type;
  deselect_card();
  set_spi_prescaler(USER_SPI_FAST_PRESCALER);

  if (type != 0U) {
    Stat &= (DSTATUS)~STA_NOINIT;
    LastError = USER_SPI_ERR_NONE;
  }
  return Stat;
}

DSTATUS USER_SPI_status(BYTE drive)
{
  return (drive == USER_SPI_PDRV) ? Stat : STA_NOINIT;
}

DRESULT USER_SPI_read(BYTE drive, BYTE *buffer, DWORD sector, UINT count)
{
  if ((drive != USER_SPI_PDRV) || (buffer == NULL) || (count == 0U)) {
    return RES_PARERR;
  }
  if ((Stat & STA_NOINIT) != 0U) {
    return RES_NOTRDY;
  }

  LastError = USER_SPI_ERR_NONE;
  HalFailed = 0U;
  if ((CardType & USER_SPI_CT_BLOCK) == 0U) {
    sector *= 512U;
  }

  if (count == 1U) {
    if ((send_command(CMD17, sector) == 0U) &&
        receive_datablock(buffer, 512U)) {
      count = 0U;
    }
  } else if (send_command(CMD18, sector) == 0U) {
    do {
      if (!receive_datablock(buffer, 512U)) {
        break;
      }
      buffer += 512U;
      WWDG_TryRefresh();
    } while (--count > 0U);
    (void)send_command(CMD12, 0U);
  }

  deselect_card();
  return (count == 0U) ? RES_OK : RES_ERROR;
}

#if _USE_WRITE == 1
DRESULT USER_SPI_write(BYTE drive, const BYTE *buffer, DWORD sector, UINT count)
{
  if ((drive != USER_SPI_PDRV) || (buffer == NULL) || (count == 0U)) {
    return RES_PARERR;
  }
  if ((Stat & STA_NOINIT) != 0U) {
    return RES_NOTRDY;
  }
  if ((Stat & STA_PROTECT) != 0U) {
    return RES_WRPRT;
  }

  LastError = USER_SPI_ERR_NONE;
  HalFailed = 0U;
  if ((CardType & USER_SPI_CT_BLOCK) == 0U) {
    sector *= 512U;
  }

  if (count == 1U) {
    if ((send_command(CMD24, sector) == 0U) &&
        transmit_datablock(buffer, 0xFEU)) {
      count = 0U;
    }
  } else {
    if ((CardType & (USER_SPI_CT_SD1 | USER_SPI_CT_SD2)) != 0U) {
      (void)send_command(ACMD23, count);
    }
    if (send_command(CMD25, sector) == 0U) {
      do {
        if (!transmit_datablock(buffer, 0xFCU)) {
          break;
        }
        buffer += 512U;
        WWDG_TryRefresh();
      } while (--count > 0U);
      if (!transmit_datablock(NULL, 0xFDU)) {
        count = 1U;
      }
    }
  }

  deselect_card();
  return (count == 0U) ? RES_OK : RES_ERROR;
}
#endif

#if _USE_IOCTL == 1
DRESULT USER_SPI_ioctl(BYTE drive, BYTE command, void *buffer)
{
  BYTE csd[16];
  BYTE n;
  DWORD csize;
  DRESULT result = RES_ERROR;

  if (drive != USER_SPI_PDRV) {
    return RES_PARERR;
  }
  if ((Stat & STA_NOINIT) != 0U) {
    return RES_NOTRDY;
  }

  LastError = USER_SPI_ERR_NONE;
  HalFailed = 0U;
  switch (command) {
  case CTRL_SYNC:
    if (select_card()) {
      result = RES_OK;
    }
    break;

  case GET_SECTOR_COUNT:
    if (buffer == NULL) {
      return RES_PARERR;
    }
    if ((send_command(CMD9, 0U) == 0U) &&
        receive_datablock(csd, sizeof(csd))) {
      if ((csd[0] >> 6) == 1U) {
        csize = (DWORD)csd[9] + ((DWORD)csd[8] << 8) +
                ((DWORD)(csd[7] & 0x3FU) << 16) + 1U;
        *(DWORD *)buffer = csize << 10;
      } else {
        n = (BYTE)((csd[5] & 0x0FU) + ((csd[10] & 0x80U) >> 7) +
                   ((csd[9] & 0x03U) << 1) + 2U);
        csize = (DWORD)(csd[8] >> 6) + ((DWORD)csd[7] << 2) +
                ((DWORD)(csd[6] & 0x03U) << 10) + 1U;
        *(DWORD *)buffer = csize << (n - 9U);
      }
      result = RES_OK;
    }
    break;

  case GET_SECTOR_SIZE:
    if (buffer == NULL) {
      return RES_PARERR;
    }
    *(WORD *)buffer = 512U;
    result = RES_OK;
    break;

  case GET_BLOCK_SIZE:
    if (buffer == NULL) {
      return RES_PARERR;
    }
    if ((CardType & USER_SPI_CT_SD2) != 0U) {
      if (send_command(ACMD13, 0U) == 0U) {
        (void)xchg_spi(0xFFU);
        if (receive_datablock(csd, sizeof(csd))) {
          for (n = 48U; n > 0U; n--) {
            (void)xchg_spi(0xFFU);
          }
          *(DWORD *)buffer = 16UL << (csd[10] >> 4);
          result = RES_OK;
        }
      }
    } else if ((send_command(CMD9, 0U) == 0U) &&
               receive_datablock(csd, sizeof(csd))) {
      if ((CardType & USER_SPI_CT_SD1) != 0U) {
        *(DWORD *)buffer =
            (DWORD)((((csd[10] & 0x3FU) << 1) +
                     ((WORD)(csd[11] & 0x80U) >> 7) + 1U)
                    << ((csd[13] >> 6) - 1U));
      } else {
        *(DWORD *)buffer =
            (DWORD)(((WORD)((csd[10] & 0x7CU) >> 2) + 1U) *
                    (((csd[11] & 0x03U) << 3) +
                     ((csd[11] & 0xE0U) >> 5) + 1U));
      }
      result = RES_OK;
    }
    break;

  default:
    result = RES_PARERR;
    break;
  }

  deselect_card();
  return result;
}
#endif

USER_SPI_Error USER_SPI_get_last_error(void)
{
  return LastError;
}

BYTE USER_SPI_get_card_type(void)
{
  return CardType;
}
