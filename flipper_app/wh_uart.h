#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct WhUart WhUart;

/** Called from the worker thread for every complete line received from the board. */
typedef void (*WhUartLineCallback)(char* line, void* context);

/** Acquire the USART, start the RX worker. Returns NULL if the USART is busy. */
WhUart* wh_uart_alloc(WhUartLineCallback callback, void* context);

void wh_uart_free(WhUart* uart);

/** Send one line (newline appended). Thread-safe. */
void wh_uart_send(WhUart* uart, const char* line);

/** True if the board answered within the last few seconds. */
bool wh_uart_board_alive(WhUart* uart);
