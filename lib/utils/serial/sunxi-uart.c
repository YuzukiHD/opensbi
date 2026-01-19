/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021 Gabriel Somlo
 *
 * Authors:
 *   Gabriel Somlo <gsomlo@gmail.com>
 */

#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_timer.h>
#include <sbi/sbi_bitops.h>

struct sunxi_serial_reg {
	union {
		volatile uint32_t rbr; /* Receiver Buffer Register (offset 0) */
		volatile uint32_t thr; /* Transmitter Holding Register (offset 0) */
		volatile uint32_t dll; /* Divisor Latch LSB (offset 0) */
	};
	union {
		volatile uint32_t ier; /* Interrupt Enable Register (offset 1) */
		volatile uint32_t dlh; /* Divisor Latch MSB (offset 1) */
	};
	union {
		volatile uint32_t fcr; /* FIFO Control Register (offset 2) */
		volatile uint32_t iir; /* Interrupt Identification Register (offset 2) */
	};
	volatile uint32_t lcr; /* Line Control Register (offset 3) */
	volatile uint32_t mcr; /* Modem Control Register (offset 4) */
	volatile uint32_t lsr; /* Line Status Register (offset 5) */
	volatile uint32_t msr; /* Modem Status Register (offset 6) */
	volatile uint32_t sch; /* Scratch Register (offset 7) */
};
#define SUNXI_UART_LSR_THRE	0x20

static volatile struct sunxi_serial_reg *sunxi_serial_device;

static void sunxi_uart_putc(char ch)
{
	while ((sunxi_serial_device->lsr & SUNXI_UART_LSR_THRE) == 0)
	 	;
	sunxi_serial_device->thr = ch;
}

static int sunxi_uart_getc(void)
{
	if (sunxi_serial_device->lsr & BIT(1))
		return sunxi_serial_device->rbr;
	return -1;
}

static struct sbi_console_device sunxi_console = {
	.name = "sunxi-uart",
	.console_putc = sunxi_uart_putc,
	.console_getc = sunxi_uart_getc
};

int sunxi_uart_init(unsigned long base)
{
	sunxi_serial_device = (volatile struct sunxi_serial_reg*)base;
	sbi_console_set_device(&sunxi_console);
	return 0;
}