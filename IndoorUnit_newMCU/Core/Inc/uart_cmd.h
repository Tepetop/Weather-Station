/**
 * @file uart_cmd.h
 * @brief UART command parser for Pico W / remote measure requests
 */

#ifndef UART_CMD_H
#define UART_CMD_H

#include <stdint.h>

#include "usart.h"
#include "weather_station.h"

#define UART_CMD_TARGET_ACTIVE 0xFFU
#define UART_CMD_TARGET_ALL    0xFEU

void UartCmd_Init(UART_HandleTypeDef *huart, WS_Manager_t *ws);
void UartCmd_FlushReply(void);

#endif /* UART_CMD_H */
