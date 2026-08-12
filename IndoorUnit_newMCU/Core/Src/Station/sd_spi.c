/**
 * @file sd_spi.c
 * @brief Minimal SPI SD (v1/v2/SDHC) driver for STM32F103 + FatFs
 * @details Shares SPI1 with PCD8544; keeps LCD_CE high during SD access.
 *          Refreshes WWDG while waiting for the card (short WWDG window).
 *          Init runs at SPI prescaler /256 (~281 kHz at APB2=72 MHz), then
 *          restores the baud saved by SD_SPI_Bind().
 */

#include "sd_spi.h"

#include "main.h"
#include "wwdg.h"

#define CMD0   (0U)   /**< GO_IDLE_STATE */
#define CMD8   (8U)   /**< SEND_IF_COND */
#define CMD9   (9U)   /**< SEND_CSD */
#define CMD12  (12U)  /**< STOP_TRANSMISSION */
#define CMD16  (16U)  /**< SET_BLOCKLEN */
#define CMD17  (17U)  /**< READ_SINGLE_BLOCK */
#define CMD18  (18U)  /**< READ_MULTIPLE_BLOCK */
#define CMD24  (24U)  /**< WRITE_BLOCK */
#define CMD25  (25U)  /**< WRITE_MULTIPLE_BLOCK */
#define CMD55  (55U)  /**< APP_CMD */
#define CMD58  (58U)  /**< READ_OCR */
#define ACMD41 (41U)  /**< SEND_OP_COND (SD) */

#define SD_TOKEN_START_BLOCK  0xFEU
#define SD_TOKEN_MULTI_WRITE  0xFCU
#define SD_TOKEN_STOP_TRAN    0xFDU

#define SD_SPI_TIMEOUT_MS      500U
#define SD_SPI_INIT_TIMEOUT_MS 1000U
#define SD_SPI_TIMEOUT_BYTE    0xFFU
#define SD_SPI_INIT_PRESCALER  SPI_BAUDRATEPRESCALER_256

static SPI_HandleTypeDef *sd_hspi = NULL;
static volatile DSTATUS sd_status = STA_NOINIT;
static uint8_t sd_card_type = SD_SPI_TYPE_NONE;
static uint8_t sd_last_error = SD_SPI_ERR_NO_HANDLE;
static uint32_t sd_spi_app_prescaler = SPI_BAUDRATEPRESCALER_16;
static uint32_t sd_wwdg_last_kick_tick = 0U;
static uint8_t sd_hal_fail = 0U;

/**
 * @brief Record the first error of the current bind/init attempt.
 * @param err One of SD_SPI_ERR_*.
 */
static void sd_set_error(uint8_t err) {
  if (sd_last_error == SD_SPI_ERR_NONE) {
    sd_last_error = err;
  }
}

/**
 * @brief Kick WWDG at most every 2 ms while blocking on the card.
 */
static void sd_wwdg_kick(void) {
  uint32_t now;

  if (hwwdg.Instance == NULL) {
    return;
  }

  now = HAL_GetTick();
  /* WWDG timeout ≈ 58 ms (Prescaler 8, Counter 127); kick at most every 2 ms. */
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

/**
 * @brief Select the SD card after deselecting the LCD on the shared SPI1 bus.
 */
static void sd_cs_low(void) {
  HAL_GPIO_WritePin(LCD_CE_GPIO_Port, LCD_CE_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
}

/**
 * @brief Exchange one SPI byte.
 * @param tx Byte to transmit.
 * @retval Received byte, or 0xFF on HAL/handle failure.
 */
static uint8_t sd_xchg(uint8_t tx) {
  uint8_t rx = SD_SPI_TIMEOUT_BYTE;
  if (sd_hspi == NULL) {
    sd_set_error(SD_SPI_ERR_NO_HANDLE);
    return rx;
  }
  if (HAL_SPI_TransmitReceive(sd_hspi, &tx, &rx, 1U, 50U) != HAL_OK) {
    sd_hal_fail = 1U;
    sd_set_error(SD_SPI_ERR_HAL);
    return SD_SPI_TIMEOUT_BYTE;
  }
  return rx;
}

/**
 * @brief Transmit a buffer over SPI.
 * @param buf Source bytes.
 * @param len Number of bytes.
 * @retval 1 on success, 0 on HAL/handle failure.
 */
static int sd_tx(const uint8_t *buf, UINT len) {
  if ((sd_hspi == NULL) || (buf == NULL) || (len == 0U)) {
    sd_set_error(SD_SPI_ERR_NO_HANDLE);
    return 0;
  }
  if (HAL_SPI_Transmit(sd_hspi, (uint8_t *)buf, len, 200U) != HAL_OK) {
    sd_hal_fail = 1U;
    sd_set_error(SD_SPI_ERR_HAL);
    return 0;
  }
  return 1;
}

/**
 * @brief Receive @p len bytes while clocking 0xFF.
 * @param buf Destination.
 * @param len Number of bytes.
 * @retval 1 on success, 0 if the handle is missing.
 */
static int sd_rx(uint8_t *buf, UINT len) {
  if ((sd_hspi == NULL) || (buf == NULL) || (len == 0U)) {
    sd_set_error(SD_SPI_ERR_NO_HANDLE);
    return 0;
  }
  while (len > 0U) {
    *buf++ = sd_xchg(0xFFU);
    if (sd_hal_fail != 0U) {
      return 0;
    }
    len--;
  }
  return 1;
}

/**
 * @brief Deselect the card and issue one dummy clock with CS high.
 */
static void sd_release(void) {
  sd_cs_high();
  (void)sd_xchg(0xFFU);
}

/**
 * @brief Wait until the card releases the MISO line (0xFF).
 * @param timeout_ms Wait budget in milliseconds.
 * @retval 1 if ready, 0 on timeout.
 */
static int sd_wait_ready(uint32_t timeout_ms) {
  uint32_t start = HAL_GetTick();
  uint8_t d;

  do {
    d = sd_xchg(0xFFU);
    if (sd_hal_fail != 0U) {
      return 0;
    }
    if (d == 0xFFU) {
      return 1;
    }
    sd_wwdg_kick();
  } while ((HAL_GetTick() - start) < timeout_ms);

  sd_set_error(SD_SPI_ERR_TIMEOUT);
  return 0;
}

/**
 * @brief Send a command (or ACMD if bit 7 is set) and return R1.
 * @param cmd Command index; OR 0x80 for ACMD (prefix CMD55).
 * @param arg 32-bit argument.
 * @retval R1 response, or 0xFF on timeout/HAL error.
 */
static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg) {
  uint8_t n;
  uint8_t res;
  uint8_t frame[6];

  if (cmd & 0x80U) {
    cmd &= 0x7FU;
    res = sd_send_cmd(CMD55, 0U);
    if (res > 1U) {
      return res;
    }
  }

  sd_cs_low();
  if (cmd != CMD12) {
    if (sd_wait_ready(SD_SPI_TIMEOUT_MS) == 0) {
      sd_release();
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
  if (sd_tx(frame, 6U) == 0) {
    sd_release();
    return 0xFFU;
  }

  if (cmd == CMD12) {
    (void)sd_xchg(0xFFU);
  }

  n = 10U;
  do {
    res = sd_xchg(0xFFU);
  } while (((res & 0x80U) != 0U) && (--n > 0U));

  return res;
}

/**
 * @brief Change SPI baud by writing CR1 BR bits.
 * @param prescaler One of SPI_BAUDRATEPRESCALER_*.
 */
static void sd_set_spi_prescaler(uint32_t prescaler) {
  if (sd_hspi == NULL) {
    return;
  }
  __HAL_SPI_DISABLE(sd_hspi);
  sd_hspi->Instance->CR1 &= ~(SPI_CR1_BR_Msk);
  sd_hspi->Instance->CR1 |= prescaler;
  sd_hspi->Init.BaudRatePrescaler = prescaler;
  __HAL_SPI_ENABLE(sd_hspi);
}

/**
 * @brief Read a data block after a read command.
 * @param buff Destination.
 * @param len  Byte count (16 for CSD, 512 for a sector).
 * @retval 1 on success, 0 on token/HAL failure.
 */
static int sd_read_datablock(uint8_t *buff, UINT len) {
  uint8_t token;
  uint32_t start = HAL_GetTick();

  do {
    token = sd_xchg(0xFFU);
    if (sd_hal_fail != 0U) {
      return 0;
    }
    sd_wwdg_kick();
  } while ((token == 0xFFU) && ((HAL_GetTick() - start) < SD_SPI_TIMEOUT_MS));

  if (token != SD_TOKEN_START_BLOCK) {
    return 0;
  }

  if (sd_rx(buff, len) == 0) {
    return 0;
  }
  (void)sd_xchg(0xFFU);
  (void)sd_xchg(0xFFU);
  return 1;
}

/**
 * @brief Write a 512-byte block or a stop token.
 * @param buff  Source sector, ignored for SD_TOKEN_STOP_TRAN.
 * @param token Start or stop token.
 * @retval 1 on success, 0 on busy/data-response failure.
 */
static int sd_write_datablock(const uint8_t *buff, uint8_t token) {
  uint8_t resp;

  if (sd_wait_ready(SD_SPI_TIMEOUT_MS) == 0) {
    return 0;
  }

  (void)sd_xchg(token);
  if (token != SD_TOKEN_STOP_TRAN) {
    if ((buff == NULL) || (sd_tx(buff, 512U) == 0)) {
      return 0;
    }
    (void)sd_xchg(0xFFU);
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

uint8_t SD_SPI_Bind(SPI_HandleTypeDef *hspi) {
  sd_hspi = NULL;
  sd_status = STA_NOINIT;
  sd_card_type = SD_SPI_TYPE_NONE;
  sd_last_error = SD_SPI_ERR_NO_HANDLE;

  if ((hspi == NULL) || (hspi->Instance == NULL)) {
    return 1U;
  }

  if ((hspi->Init.Mode != SPI_MODE_MASTER) ||
      (hspi->Init.Direction != SPI_DIRECTION_2LINES) ||
      (hspi->Init.DataSize != SPI_DATASIZE_8BIT) ||
      (hspi->Init.CLKPolarity != SPI_POLARITY_LOW) ||
      (hspi->Init.CLKPhase != SPI_PHASE_1EDGE) ||
      (hspi->Init.NSS != SPI_NSS_SOFT) ||
      (hspi->Init.FirstBit != SPI_FIRSTBIT_MSB)) {
    sd_last_error = SD_SPI_ERR_BAD_SPI_CFG;
    return 1U;
  }

  sd_hspi = hspi;
  sd_spi_app_prescaler = hspi->Init.BaudRatePrescaler;
  sd_last_error = SD_SPI_ERR_NONE;
  return 0U;
}

uint8_t SD_SPI_GetLastError(void) {
  return sd_last_error;
}

uint8_t SD_SPI_GetCardType(void) {
  return sd_card_type;
}

DSTATUS SD_SPI_disk_initialize(BYTE pdrv) {
  uint8_t n;
  uint8_t cmd;
  uint8_t ocr[4];
  uint8_t acmd_ok = 0U;
  uint32_t start;

  sd_status = STA_NOINIT;
  sd_card_type = SD_SPI_TYPE_NONE;
  sd_last_error = SD_SPI_ERR_NONE;
  sd_hal_fail = 0U;

  if (pdrv != SD_SPI_PDRV) {
    sd_last_error = SD_SPI_ERR_BAD_PDRV;
    return STA_NOINIT;
  }

  if (sd_hspi == NULL) {
    sd_last_error = SD_SPI_ERR_NO_HANDLE;
    return STA_NOINIT;
  }

  sd_cs_high();
  /* Slow clock for card init (<=400 kHz). APB2=72 MHz → /256 ≈ 281 kHz. */
  sd_set_spi_prescaler(SD_SPI_INIT_PRESCALER);

  for (n = 10U; n > 0U; n--) {
    (void)sd_xchg(0xFFU);
  }

  if (sd_send_cmd(CMD0, 0U) == 0x01U) {
    start = HAL_GetTick();
    if (sd_send_cmd(CMD8, 0x1AAUL) == 0x01U) {
      if (sd_rx(ocr, 4U) == 0) {
        sd_set_error(SD_SPI_ERR_HAL);
      } else if ((ocr[2] == 0x01U) && (ocr[3] == 0xAAU)) {
        do {
          sd_wwdg_kick();
          if (sd_send_cmd((uint8_t)(0x80U | ACMD41), 1UL << 30) == 0U) {
            acmd_ok = 1U;
            break;
          }
        } while ((HAL_GetTick() - start) < SD_SPI_INIT_TIMEOUT_MS);

        if (acmd_ok == 0U) {
          sd_set_error(SD_SPI_ERR_ACMD41);
        } else if (sd_send_cmd(CMD58, 0U) == 0U) {
          if (sd_rx(ocr, 4U) != 0) {
            sd_card_type = ((ocr[0] & 0x40U) != 0U) ? SD_SPI_TYPE_SDHC
                                                    : SD_SPI_TYPE_SD2;
          }
        } else {
          sd_set_error(SD_SPI_ERR_CMD58);
        }
      } else {
        sd_set_error(SD_SPI_ERR_IFCOND);
      }
    } else {
      if (sd_send_cmd((uint8_t)(0x80U | ACMD41), 0U) <= 0x01U) {
        sd_card_type = SD_SPI_TYPE_SD1;
        cmd = (uint8_t)(0x80U | ACMD41);
      } else {
        sd_card_type = SD_SPI_TYPE_NONE;
        cmd = 1U;
        sd_set_error(SD_SPI_ERR_CMD8);
      }

      if (sd_card_type != SD_SPI_TYPE_NONE) {
        acmd_ok = 0U;
        do {
          sd_wwdg_kick();
          if (sd_send_cmd(cmd, 0U) == 0U) {
            acmd_ok = 1U;
            break;
          }
        } while ((HAL_GetTick() - start) < SD_SPI_INIT_TIMEOUT_MS);

        if (acmd_ok == 0U) {
          sd_card_type = SD_SPI_TYPE_NONE;
          sd_set_error(SD_SPI_ERR_ACMD41);
        }
      }
    }
  } else {
    sd_set_error(SD_SPI_ERR_CMD0);
  }

  /* Force 512-byte blocks on byte-addressed cards (once, including SDv1). */
  if ((sd_card_type != SD_SPI_TYPE_NONE) && (sd_card_type != SD_SPI_TYPE_SDHC)) {
    if (sd_send_cmd(CMD16, 512U) != 0U) {
      sd_card_type = SD_SPI_TYPE_NONE;
      sd_set_error(SD_SPI_ERR_CMD16);
    }
  }

  sd_release();
  sd_set_spi_prescaler(sd_spi_app_prescaler);

  if (sd_card_type != SD_SPI_TYPE_NONE) {
    sd_status = 0;
    sd_last_error = SD_SPI_ERR_NONE;
  } else {
    sd_status = STA_NOINIT;
    if (sd_last_error == SD_SPI_ERR_NONE) {
      sd_last_error = SD_SPI_ERR_TIMEOUT;
    }
  }

  return sd_status;
}

DSTATUS SD_SPI_disk_status(BYTE pdrv) {
  if (pdrv != SD_SPI_PDRV) {
    return STA_NOINIT;
  }
  return sd_status;
}

DRESULT SD_SPI_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
  if (pdrv != SD_SPI_PDRV) {
    return RES_PARERR;
  }
  if ((sd_status & STA_NOINIT) != 0U) {
    return RES_NOTRDY;
  }
  if ((buff == NULL) || (count == 0U)) {
    return RES_PARERR;
  }

  sd_hal_fail = 0U;

  if (sd_card_type != SD_SPI_TYPE_SDHC) {
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
  if (pdrv != SD_SPI_PDRV) {
    return RES_PARERR;
  }
  if ((sd_status & STA_NOINIT) != 0U) {
    return RES_NOTRDY;
  }
  if ((buff == NULL) || (count == 0U)) {
    return RES_PARERR;
  }

  sd_hal_fail = 0U;

  if (sd_card_type != SD_SPI_TYPE_SDHC) {
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

  if (pdrv != SD_SPI_PDRV) {
    return RES_PARERR;
  }
  if ((sd_status & STA_NOINIT) != 0U) {
    return RES_NOTRDY;
  }

  sd_hal_fail = 0U;

  switch (cmd) {
  case CTRL_SYNC:
    sd_cs_low();
    if (sd_wait_ready(SD_SPI_TIMEOUT_MS) != 0) {
      res = RES_OK;
    }
    sd_release();
    break;

  case GET_SECTOR_COUNT:
    if (buff == NULL) {
      return RES_PARERR;
    }
    if ((sd_send_cmd(CMD9, 0U) == 0U) && sd_read_datablock(csd, 16U)) {
      if ((csd[0] >> 6) == 1U) {
        csize = (DWORD)csd[9] + ((DWORD)csd[8] << 8) +
                ((DWORD)(csd[7] & 0x3FU) << 16) + 1U;
        *(DWORD *)buff = csize << 10;
      } else {
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
    if (buff == NULL) {
      return RES_PARERR;
    }
    *(WORD *)buff = 512U;
    res = RES_OK;
    break;

  case GET_BLOCK_SIZE:
    if (buff == NULL) {
      return RES_PARERR;
    }
    *(DWORD *)buff = 1U;
    res = RES_OK;
    break;

  default:
    res = RES_PARERR;
    break;
  }

  return res;
}
