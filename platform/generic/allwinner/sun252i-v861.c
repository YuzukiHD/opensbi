/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 Samuel Holland <samuel@sholland.org>
 */

#include <platform_override.h>
#include <thead/c9xx_encoding.h>
#include <thead/c9xx_pmu.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_bitops.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hsm.h>
#include <sbi/sbi_system.h>
#include <sbi/sbi_pmu.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_platform.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/irqchip/fdt_irqchip.h>
#include <sbi/sbi_timer.h>

/* CCU */
#define SUN252I_V861_CCU_BASE			((void *)0x02001000)
#define SUN252I_V861_RISCV_CFG_BGR_REG	0x50C
#define SUN252I_V861_CCU_BGR_ENABLE		(BIT(16) | BIT(0))

/* PMC */
#define SUN252I_V861_PMC_BASE_REG		((void *)0x08000000)
#define SUN252I_V861_PMC_BASE(n)		(SUN252I_V861_PMC_BASE_REG + 0x1000 * (n))
#define SUN252I_V861_SW_MODE_CTRL(m)	(0x100 + 0x4 * (m))
#define SUN252I_V861_TIMER_MODE_CONFIG	0x600
#define SUN252I_V861_DELAY_CTRL_CONFIG	0x700

/* RISCV_SYS */
#define SUN252I_V861_RISCV_CFG_BASE		((void *)0x08008000)
#define RESET_ENTRY_LO_REG(n)			(0x100 + 0x80 * (n))
#define RESET_ENTRY_HI_REG(n)			(0x104 + 0x80 * (n))
#define SUN252I_V861_CORE_CFG_REG(n)	(0x108 + 0x80 * (n))
#define SUN252I_V861_C907_MODE_64BIT 	(0b10)
#define SUN252I_V861_C907_MODE_32BIT 	(0b01)

void sun252i_v861_set_hart_boot_addr(u32 hartid, u64 entry)
{
#if __riscv_xlen == 64
	/* if using rv64 mode, set to rv64 core */
	writel((readl(SUN252I_V861_RISCV_CFG_BASE + SUN252I_V861_CORE_CFG_REG(hartid)) &
			~(0x3 << 8)) | (SUN252I_V861_C907_MODE_64BIT << 8),
			SUN252I_V861_RISCV_CFG_BASE + SUN252I_V861_CORE_CFG_REG(hartid));
#else
	/* if using rv32 mode, set to rv32 core */
	writel((readl(SUN252I_V861_RISCV_CFG_BASE + SUN252I_V861_CORE_CFG_REG(hartid)) &
			~(0x3 << 8)) | (SUN252I_V861_C907_MODE_32BIT << 8),
			SUN252I_V861_RISCV_CFG_BASE + SUN252I_V861_CORE_CFG_REG(hartid));
#endif
	writel_relaxed(entry, SUN252I_V861_RISCV_CFG_BASE + RESET_ENTRY_LO_REG(hartid));
	writel_relaxed(entry >> 32, SUN252I_V861_RISCV_CFG_BASE + RESET_ENTRY_HI_REG(hartid));
}

static void sun252i_v861_riscv_cfg_init(void)
{
	u64 entry = sbi_hartid_to_scratch(0)->warmboot_addr;

	/* Enable MMIO access. */
	writel_relaxed(SUN252I_V861_CCU_BGR_ENABLE, SUN252I_V861_CCU_BASE + SUN252I_V861_RISCV_CFG_BGR_REG);

	/* Program the reset entry address. */
	sun252i_v861_set_hart_boot_addr(0, entry);
	csr_write(THEAD_C9XX_CSR_MHINT4, csr_read(THEAD_C9XX_CSR_MHINT4) | (0x1 << 7));

	/* Set cluster power mode to auto mode. */
	writel_relaxed(0x0 << 3, SUN252I_V861_PMC_BASE(4) + SUN252I_V861_SW_MODE_CTRL(0));
	writel_relaxed(0x0 << 3, SUN252I_V861_PMC_BASE(4) + SUN252I_V861_SW_MODE_CTRL(1));

	/* Init the PMC delay timers */
	writel_relaxed(0x1, SUN252I_V861_PMC_BASE(0) + SUN252I_V861_TIMER_MODE_CONFIG);
	writel_relaxed(0x01010100, SUN252I_V861_PMC_BASE(0) + SUN252I_V861_DELAY_CTRL_CONFIG);
	writel_relaxed(0x1, SUN252I_V861_PMC_BASE(1) + SUN252I_V861_TIMER_MODE_CONFIG);
	writel_relaxed(0x01010100, SUN252I_V861_PMC_BASE(1) + SUN252I_V861_DELAY_CTRL_CONFIG);
	writel_relaxed(0x1, SUN252I_V861_PMC_BASE(4) + SUN252I_V861_TIMER_MODE_CONFIG);
	writel_relaxed(0x01010100, SUN252I_V861_PMC_BASE(4) + SUN252I_V861_DELAY_CTRL_CONFIG);
}

int sun252i_v861_hart_start(u32 hartid, ulong saddr)
{
	/* Program the reset entry address. */
	sun252i_v861_set_hart_boot_addr(hartid, saddr);

	/* Set core power mode to manal mode and power on */
	writel_relaxed(0x3 << 3, SUN252I_V861_PMC_BASE(hartid) + SUN252I_V861_SW_MODE_CTRL(0));
	writel_relaxed(0x11, SUN252I_V861_PMC_BASE(hartid) + SUN252I_V861_SW_MODE_CTRL(1));
	return 0;
}

int sun252i_v861_hart_stop(void)
{
	u32 hartid = current_hartid();
	/* Hotplug power down, set trans bit to manual mode */
	writel_relaxed(0x2 << 3, SUN252I_V861_PMC_BASE(hartid) + SUN252I_V861_SW_MODE_CTRL(0));
	writel_relaxed(0x2 << 3, SUN252I_V861_PMC_BASE(hartid) + SUN252I_V861_SW_MODE_CTRL(1));

	/* Close the Prefetch */
	csr_clear(THEAD_C9XX_CSR_MHINT, (1 << 2) | (1 << 15) | (1 <<20));

	/* Clean and invalidate core's dcache */
	asm volatile("dcache.ciall");
	asm volatile("sync.is");

	/* Close the Dcache */
	csr_clear(THEAD_C9XX_CSR_MHCR, 0x1 << 1);

	/* Close the snoop */
	csr_clear(THEAD_C9XX_CSR_MSMPR, 0x1);

	csr_set(CSR_MIE, MIP_MTIP | MIP_STIP | MIP_SEIP | MIP_MEIP);

	wfi();
	return 0;
}

/* Watchdog configs regs */
#define SUN252I_V861_WDT_BASE			((void *)0x08009000)
#define SUN252I_V861_WDT_CFG			(SUN252I_V861_WDT_BASE + 0x14)
#define SUN252I_V861_WDT_MODE			(SUN252I_V861_WDT_BASE + 0x18)
#define SUN252I_V861_WDT_UPDATE_FLAG	(0x16aa << 16)

static void sun252i_v861_wdt_reset_system()
{
	writel((readl(SUN252I_V861_WDT_CFG) & (~(0x1 << 0))) | SUN252I_V861_WDT_UPDATE_FLAG,
			SUN252I_V861_WDT_CFG);
	/* this delay time is essential. otherwise, it will affect clock synchronization. */
	sbi_timer_mdelay(1);

	/* Set watchdog mode to reset system */
	writel(SUN252I_V861_WDT_UPDATE_FLAG | 0x1, SUN252I_V861_WDT_CFG);
	/* set restart time 0.5s */
	writel(SUN252I_V861_WDT_UPDATE_FLAG, SUN252I_V861_WDT_MODE);
	writel((readl(SUN252I_V861_WDT_MODE) | 0x1 | SUN252I_V861_WDT_UPDATE_FLAG), SUN252I_V861_WDT_MODE);
}

static void sun252i_v861_system_reset(u32 type, u32 reason)
{
	switch (type) {
	case SBI_SRST_RESET_TYPE_SHUTDOWN:
		while (1)
			;
		break;
	case SBI_SRST_RESET_TYPE_COLD_REBOOT:
		sun252i_v861_wdt_reset_system();
		while (1)
			;
		break;
	default:
		sbi_hart_hang();
	}
}

static int sun252i_v861_system_reset_check(u32 type, u32 reason)
{
	switch (type) {
	case SBI_SRST_RESET_TYPE_SHUTDOWN:
		return 1;
	case SBI_SRST_RESET_TYPE_COLD_REBOOT:
	case SBI_SRST_RESET_TYPE_WARM_REBOOT:
		return 255;
	}
	return 0;
}

static const struct sbi_hsm_device sun252i_v861_ppu = {
	.name		= "sun252i-v861-ppu",
	.hart_start = sun252i_v861_hart_start,
	.hart_stop 	= sun252i_v861_hart_stop,
};

static int sun252i_v861_final_init(bool cold_boot)
{
	if (cold_boot) {
		sun252i_v861_riscv_cfg_init();
		sbi_hsm_set_device(&sun252i_v861_ppu);
	}
	return generic_final_init(cold_boot);
}

static int sun252i_v861_extensions_init(struct sbi_hart_features *hfeatures)
{
	int rc;

	rc = generic_extensions_init(hfeatures);
	if (rc)
		return rc;

	thead_c9xx_register_pmu_device();

	/* Set custom CSRs */
	csr_write(THEAD_C9XX_CSR_MSMPR, 0x1);
	csr_write(THEAD_C9XX_CSR_MCCR2, 0xA0420002);
	csr_write(THEAD_C9XX_CSR_MXSTATUS, 0x438000);
	csr_write(THEAD_C9XX_CSR_MHINT, 0x3A1AA10C);
	csr_write(THEAD_C9XX_CSR_MHCR, 0x10011BF);
#if __riscv_xlen == 64
	csr_write(CSR_MENVCFG, 0x4000000000000000);
#ifdef CONFIG_THEAD_64ILP32_ENHANCE
	/* Enable 64ILP32-enhance mode XTSV32 */
	csr_write(THEAD_C9XX_CSR_MXSTATUS, csr_read(THEAD_C9XX_CSR_MXSTATUS) | (1ULL << 63));
#endif
#endif

	return 0;
}

static int sun252i_v861_early_init(bool cold_boot)
{
	if (cold_boot) {
		/* Add system reset device */
		static struct sbi_system_reset_device sun252i_v861_reset = {
			.name = "sun252i-v861-reset",
			.system_reset_check = sun252i_v861_system_reset_check,
			.system_reset = sun252i_v861_system_reset
		};
		sbi_system_reset_add_device(&sun252i_v861_reset);
	}

	return generic_early_init(cold_boot);
}

static const struct sbi_cpu_idle_state sun252i_v861_cpu_idle_states[] = {
	{
		.name			= "cpu-nonretentive",
		.suspend_param		= SBI_HSM_SUSPEND_NON_RET_DEFAULT,
		.local_timer_stop	= true,
		.entry_latency_us	= 40,
		.exit_latency_us	= 67,
		.min_residency_us	= 1100,
		.wakeup_latency_us	= 67,
	},
	{ }
};

static int sun252i_v861_platform_driver_init(const void *fdt, int nodeoff,
						   const struct fdt_match *match)
{
	/* Add CPU idle states */
	void *rw_fdt = fdt_get_address_rw();
	if (rw_fdt) {
		fdt_add_cpu_idle_states(rw_fdt, sun252i_v861_cpu_idle_states);
	}

	/* Override platform operations */
	generic_platform_ops.early_init = sun252i_v861_early_init;
	generic_platform_ops.final_init = sun252i_v861_final_init;
	generic_platform_ops.extensions_init = sun252i_v861_extensions_init;

	return 0;
}

static const struct fdt_match sun252i_v861_match[] = {
	{ .compatible = "allwinner,sun252i-v861" },
	/* for bsp kernel */
	{ .compatible = "allwinner,sun252iw1p1" },
	{ },
};

const struct fdt_driver sun252i_v861 = {
	.match_table = sun252i_v861_match,
	.init = sun252i_v861_platform_driver_init,
};