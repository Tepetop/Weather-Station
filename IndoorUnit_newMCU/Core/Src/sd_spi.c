/**
 * @file sd_spi.c
 * @brief Minimal SPI SD (v1/v2/SDHC) driver for STM32F103 + FatFs
 * @details Shares SPI1 with PCD8544; keeps LCD_CE high during SD access.
 *          Refreshes WWDG while waiting for the card (short WWDG window).
 */

#include "sd_spi.h"

#include "main.h"
#include "wwdg.h"

#include <string.h>

/* SD commands */
#define CMD0   (0U)   /* GO_IDLE_STATE */
#define CMD8   (8U)   /* SEND_IF_COND */
#define CMD9   (9U)   /* SEND_CSD */
#define CMD12  (12U)  /* STOP_TRANSMISSION */
#define CMD16  (16U)  /* SET_BLOCKLEN */
#define CMD17  (17U)  /* READ_SINGLE_BLOCK */
#define CMD18  (18U)  /* READ_MULTIPLE_BLOCK */
#define CMD24  (24U)  /* WRITE_BLOCK */
#define CMD25  (25U)  /* WRITE_MULTIPLE_BLOCK */
#define CMD55  (55U)  /* APP_CMD */
#define CMD58  (58U)  /* READ_OCR */
#define ACMD41 (41U)  /* SEND_OP_COND (SD) */

#define SD_TOKEN_START_BLOCK  0xFEU
#define SD_TOKEN_MULTI_WRITE  0xFCU
#define SD_TOKEN_STOP_TRAN    0xFDU

#define SD_TYPE_NONE  0U
#define SD_TYPE_SD1   1U
#define SD_TYPE_SD2   2U
#define SD_TYPE_SDHC  3U

#define SD_SPI_TIMEOUT_MS     500U
#define SD_SPI_INIT_TIMEOUT_MS 1000U
#define SD_SPI_TIMEOUT_BYTE   0xFFU

static SPI_HandleTypeDef *sd_hspi = NULL;
static volatile DSTATUS sd_status = STA_NOINIT;
static uint8_t sd_card_type = SD_TYPE_NONE;
static uint32_t sd_wwdg_last_kick_tick = 0U;

static void sd_wwdg_kick(void) {
  uint32_t now;

  if (hwwdg.Instance == NULL) {
    return;
  }

  now = HAL_GetTick();
  /* WWDG window ≈ 7 ms; kick at most every 2 ms once counter has ticked. */
  if ((now - sd_wwdg_last_kick_tick) < 2U) {
    return;
  }
  if (HAL_WWDG_Refresh(&hwwdg) == HAL_OK) {
    sd_wwdg_last_kick_tick = now;
  }
}

static void sd_cs_high(void) {
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
}

static void sd_cs_low(void) {
  /* Deselect LCD first — shared SPI1 bus. */
  HAL_GPIO_WritePin(LCD_CE_GPIO_Port, LCD_CE_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
}

static uint8_t sd_xchg(uint8_t tx) {
  uint8_t rx = SD_SPI_TIMEOUT_BYTE;
  if (sd_hspi == NULL) {
    return rx;
  }
  (void)HAL_SPI_TransmitReceive(sd_hspi, &tx, &rx, 1U, 50U);
  return rx;
}

static void sd_tx(const uint8_t *buf, UINT len) {
  if ((sd_hspi == NULL) || (buf == NULL) || (len == 0U)) {
    return;
  }
  (void)HAL_SPI_Transmit(sd_hspi, (uint8_t *)buf, len, 200U);
}

static void sd_rx(uint8_t *buf, UINT len) {
  if ((sd_hspi == NULL) || (buf == NULL) || (len == 0U)) {
    return;
  }
  /* Clock out 0xFF while receiving. */
  while (len > 0U) {
    *buf++ = sd_xchg(0xFFU);
    len--;
  }
}

static void sd_release(void) {
  sd_cs_high();
  (void)sd_xchg(0xFFU); /* one dummy clock with CS high */
}

static int sd_wait_ready(uint32_t timeout_ms) {
  uint32_t start = HAL_GetTick();
  uint8_t d;

  do {
    d = sd_xchg(0xFFU);
    if (d == 0xFFU) {
      return 1;
    }
    sd_wwdg_kick();
  } while ((HAL_GetTick() - start) < timeout_ms);

  return 0;
}

static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg) {
  uint8_t n;
  uint8_t res;
  uint8_t frame[6];

  if (cmd & 0x80U) {
    /* ACMD: prefix with CMD55 */
    cmd &= 0x7FU;
    res = sd_send_cmd(CMD55, 0U);
    if (res > 1U) {
      return res;
    }
  }

  /* Select card and wait ready (except CMD12 stop which may be mid-transfer). */
  sd_cs_low();
  if (cmd != CMD12) {
    if (sd_wait_ready(SD_SPI_TIMEOUT_MS) == 0) {
      return 0xFFU;
    }
  }

  frame[0] = (uint8_t)(0x40U | cmd);
  frame[1] = (uint8_t)(arg >> 24);
  frame[2] = (uint8_t)(arg >> 16);
  frame[3] = (uint8_t)(arg >> 8);
  frame[4] = (uint8_t)(arg);
  /* Valid CRC only required for CMD0/CMD8; otherwise 0x01 stop bit is enough. */
  n = 0x01U;
  if (cmd == CMD0) {
    n = 0x95U;
  }
  if (cmd == CMD8) {
    n = 0x87U;
  }
  frame[5] = n;
  sd_tx(frame, 6U);

  if (cmd == CMD12) {
    (void)sd_xchg(0xFFU); /* discard stuff byte */
  }

  n = 10U;
  do {
    res = sd_xchg(0xFFU);
  } while (((res & 0x80U) != 0U) && (--n > 0U));

  return res;
}

static void sd_set_spi_prescaler(uint32_t prescaler) {
  if (sd_hspi == NULL) {
    return;
  }
  __HAL_SPI_DISABLE(sd_hspi);
  sd_hspi->Instance->CR1 &= ~(SPI_CR1_BR_Msk);
  sd_hspi->Instance->CR1 |= prescaler;
  __HAL_SPI_ENABLE(sd_hspi);
}

static int sd_read_datablock(uint8_t *buff, UINT len) {
  uint8_t token;
  uint32_t start = HAL_GetTick();

  do {
    token = sd_xchg(0xFFU);
    sd_wwdg_kick();
  } while ((token == 0xFFU) && ((HAL_GetTick() - start) < SD_SPI_TIMEOUT_MS));

  if (token != SD_TOKEN_START_BLOCK) {
    return 0;
  }

  sd_rx(buff, len);
  (void)sd_xchg(0xFFU); /* CRC */
  (void)sd_xchg(0xFFU);
  return 1;
}

static int sd_write_datablock(const uint8_t *buff, uint8_t token) {
  uint8_t resp;

  if (sd_wait_ready(SD_SPI_TIMEOUT_MS) == 0) {
    return 0;
  }

  (void)sd_xchg(token);
  if (token != SD_TOKEN_STOP_TRAN) {
    sd_tx(buff, 512U);
    (void)sd_xchg(0xFFU); /* dummy CRC */
    (void)sd_xchg(0xFFU);
    resp = sd_xchg(0xFFU);
    if ((resp & 0x1FU) != 0x05U) {
      return 0;
    }
    if (sd_wait_ready(SD_SPI_TIMEOUT_MS) == 0) {
      return 0;
    }
  }
  return 1;
}

void SD_SPI_Bind(SPI_HandleTypeDef *hspi) {
  sd_hspi = hspi;
}

DSTATUS SD_SPI_disk_initialize(BYTE pdrv) {
  uint8_t n;
  uint8_t cmd;
  uint8_t ocr[4];
  uint32_t start;

  (void)pdrv;

  if (sd_hspi == NULL) {
    return STA_NOINIT;
  }

  sd_status = STA_NOINIT;
  sd_card_type = SD_TYPE_NONE;

  sd_cs_high();
  /* Slow clock for card init (<=400 kHz). APB2=72 MHz → /256 ≈ 281 kHz. */
  sd_set_spi_prescaler(SPI_BAUDRATEPRESCALER_256);

  for (n = 10U; n > 0U; n--) {
    (void)sd_xchg(0xFFU);
  }

  if (sd_send_cmd(CMD0, 0U) == 0x01U) {
    start = HAL_GetTick();
    if (sd_send_cmd(CMD8, 0x1AAUL) == 0x01U) {
      /* SDv2 */
      sd_rx(ocr, 4U);
      if ((ocr[2] == 0x01U) && (ocr[3] == 0xAAU)) {
        do {
          sd_wwdg_kick();
          if (sd_send_cmd((uint8_t)(0x80U | ACMD41), 1UL << 30) == 0U) {
            break;
          }
        } while ((HAL_GetTick() - start) < SD_SPI_INIT_TIMEOUT_MS);

        if (((HAL_GetTick() - start) < SD_SPI_INIT_TIMEOUT_MS) &&
            (sd_send_cmd(CMD58, 0U) == 0U)) {
          sd_rx(ocr, 4U);
          sd_card_type = ((ocr[0] & 0x40U) != 0U) ? SD_TYPE_SDHC : SD_TYPE_SD2;
        }
      }
    } else {
      /* SDv1 or MMC */
      if (sd_send_cmd((uint8_t)(0x80U | ACMD41), 0U) <= 0x01U) {
        sd_card_type = SD_TYPE_SD1;
        cmd = (uint8_t)(0x80U | ACMD41);
      } else {
        sd_card_type = SD_TYPE_NONE;
        cmd = 1U; /* CMD1 unused — fail */
      }

      if (sd_card_type != SD_TYPE_NONE) {
        do {
          sd_wwdg_kick();
          if (sd_send_cmd(cmd, 0U) == 0U) {
            break;
          }
        } while ((HAL_GetTick() - start) < SD_SPI_INIT_TIMEOUT_MS);

        if (((HAL_GetTick() - start) >= SD_SPI_INIT_TIMEOUT_MS) ||
            (sd_send_cmd(CMD16, 512U) != 0U)) {
          sd_card_type = SD_TYPE_NONE;
        }
      }
    }
  }

  /* Force 512-byte blocks on byte-addressed cards. */
  if ((sd_card_type != SD_TYPE_NONE) && (sd_card_type != SD_TYPE_SDHC)) {
    if (sd_send_cmd(CMD16, 512U) != 0U) {
      sd_card_type = SD_TYPE_NONE;
    }
  }

  sd_release();

  /* Restore application SPI1 speed (prescaler /16 ≈ 4.5 Mbit/s). */
  sd_set_spi_prescaler(SPI_BAUDRATEPRESCALER_16);

  if (sd_card_type != SD_TYPE_NONE) {
    sd_status = 0;
  } else {
    sd_status = STA_NOINIT;
  }

  return sd_status;
}

DSTATUS SD_SPI_disk_status(BYTE pdrv) {
  (void)pdrv;
  return sd_status;
}

DRESULT SD_SPI_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
  (void)pdrv;

  if ((sd_status & STA_NOINIT) != 0U) {
    return RES_NOTRDY;
  }
  if ((buff == NULL) || (count == 0U)) {
    return RES_PARERR;
  }

  if (sd_card_type != SD_TYPE_SDHC) {
    sector *= 512U;
  }

  if (count == 1U) {
    if ((sd_send_cmd(CMD17, sector) == 0U) && sd_read_datablock(buff, 512U)) {
      count = 0U;
    }
  } else {
    if (sd_send_cmd(CMD18, sector) == 0U) {
      do {
        if (sd_read_datablock(buff, 512U) == 0) {
          break;
        }
        buff += 512U;
        sd_wwdg_kick();
      } while (--count > 0U);
      (void)sd_send_cmd(CMD12, 0U);
    }
  }

  sd_release();
  return (count == 0U) ? RES_OK : RES_ERROR;
}

DRESULT SD_SPI_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count) {
  (void)pdrv;

  if ((sd_status & STA_NOINIT) != 0U) {
    return RES_NOTRDY;
  }
  if ((buff == NULL) || (count == 0U)) {
    return RES_PARERR;
  }

  if (sd_card_type != SD_TYPE_SDHC) {
    sector *= 512U;
  }

  if (count == 1U) {
    if ((sd_send_cmd(CMD24, sector) == 0U) &&
        sd_write_datablock(buff, SD_TOKEN_START_BLOCK)) {
      count = 0U;
    }
  } else {
    if (sd_send_cmd(CMD25, sector) == 0U) {
      do {
        if (sd_write_datablock(buff, SD_TOKEN_MULTI_WRITE) == 0) {
          break;
        }
        buff += 512U;
        sd_wwdg_kick();
      } while (--count > 0U);
      if (sd_write_datablock(NULL, SD_TOKEN_STOP_TRAN) == 0) {
        count = 1U;
      }
    }
  }

  sd_release();
  return (count == 0U) ? RES_OK : RES_ERROR;
}

DRESULT SD_SPI_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
  DRESULT res = RES_ERROR;
  uint8_t csd[16];
  DWORD csize;

  (void)pdrv;

  if ((sd_status & STA_NOINIT) != 0U) {
    return RES_NOTRDY;
  }

  switch (cmd) {
  case CTRL_SYNC:
    sd_cs_low();
    if (sd_wait_ready(SD_SPI_TIMEOUT_MS) != 0) {
      res = RES_OK;
    }
    sd_release();
    break;

  case GET_SECTOR_COUNT:
    if ((sd_send_cmd(CMD9, 0U) == 0U) && sd_read_datablock(csd, 16U)) {
      if ((csd[0] >> 6) == 1U) {
        /* SDv2 CSD */
        csize = (DWORD)csd[9] + ((DWORD)csd[8] << 8) +
                ((DWORD)(csd[7] & 0x3FU) << 16) + 1U;
        *(DWORD *)buff = csize << 10;
      } else {
        /* SDv1 CSD */
        csize = ((DWORD)(csd[6] & 0x03U) << 10) + ((DWORD)csd[7] << 2) +
                ((DWORD)(csd[8] & 0xC0U) >> 6) + 1U;
        *(DWORD *)buff =
            csize << (((csd[5] & 0x0FU) + ((csd[10] & 0x80U) >> 7) +
                       ((csd[9] & 0x03U) << 1)) +
                      2U - 9U);
      }
      res = RES_OK;
    }
    sd_release();
    break;

  case GET_SECTOR_SIZE:
    *(WORD *)buff = 512U;
    res = RES_OK;
    break;

  case GET_BLOCK_SIZE:
    *(DWORD *)buff = 1U;
    res = RES_OK;
    break;

  default:
    res = RES_PARERR;
    break;
  }

  return res;
}
