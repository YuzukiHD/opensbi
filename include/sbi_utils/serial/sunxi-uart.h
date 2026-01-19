/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021 Gabriel Somlo
 *
 * Authors:
 *   Gabriel Somlo <gsomlo@gmail.com>
 */

#ifndef __SERIAL_SUNXI_UART_H__
#define __SERIAL_SUNXI_UART_H__

#include <sbi/sbi_types.h>

int sunxi_uart_init(unsigned long base);

#endif