/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021 Gabriel Somlo
 *
 * Authors:
 *   Gabriel Somlo <gsomlo@gmail.com>
 */

#include <sbi/sbi_error.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/fdt/fdt_driver.h>
#include <sbi_utils/serial/sunxi-uart.h>

static int serial_sunxi_init(const void *fdt, int nodeoff,
			const struct fdt_match *match)
{
	uint64_t reg_addr, reg_size;
	int rc;

	if (nodeoff < 0 || !fdt)
		return SBI_ENODEV;

	rc = fdt_get_node_addr_size(fdt, nodeoff, 0, &reg_addr, &reg_size);
	if (rc < 0 || !reg_addr || !reg_size)
		return SBI_ENODEV;

	return sunxi_uart_init(reg_addr);
}

static const struct fdt_match serial_sunxi_match[] = {
	{ .compatible = "allwinner,sunxi-uart" },
	{ },
};

const struct fdt_driver fdt_serial_sunxi = {
	.match_table = serial_sunxi_match,
	.init = serial_sunxi_init,
	.experimental = false
};