/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026
 */

#include <platform_override.h>
#include <thead/c9xx_encoding.h>
#include <thead/c9xx_pmu.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_bitops.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_hsm.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_system.h>
#include <sbi/sbi_timer.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/fdt/fdt_helper.h>

/* CCMU */
#define SUN252I_F101_CCU_BASE		((void *)0x02001000)
#define RISCV_CFG_BGR_REG		0xd0c
#define CCU_BGR_ENABLE			(BIT(16) | BIT(0))

/* PMC */
#define SUN252I_F101_PMC_BASE		((void *)0x06018000)
#define PMC_BASE(n)			(SUN252I_F101_PMC_BASE + 0x1000 * (n))
#define SW_MODE_CTRL(m)		(0x100 + 0x4 * (m))
#define TIMER_MODE_CONFIG		0x600
#define DELAY_CTRL_CONFIG		0x700

/* RISCV_SYS */
#define SUN252I_F101_RISCV_CFG_BASE	((void *)0x06010000)
#define RESET_ENTRY_LO_REG(n)		(0x100 + 0x80 * (n))
#define RESET_ENTRY_HI_REG(n)		(0x104 + 0x80 * (n))

/* Watchdog */
#define SUN252I_F101_WDT_BASE		((void *)0x06011000)
#define WDT_CFG				(SUN252I_F101_WDT_BASE + 0x14)
#define WDT_MODE				(SUN252I_F101_WDT_BASE + 0x18)
#define WDT_UPDATE_FLAG			(0x16aa << 16)

void sun252i_f101_set_bootaddr(u32 hartid, u64 entry)
{
	writel_relaxed(entry, SUN252I_F101_RISCV_CFG_BASE +
		       RESET_ENTRY_LO_REG(hartid));
	writel_relaxed(entry >> 32, SUN252I_F101_RISCV_CFG_BASE +
		       RESET_ENTRY_HI_REG(hartid));
}

static void sun252i_f101_riscv_cfg_init(void)
{
	u64 entry = sbi_hartid_to_scratch(0)->warmboot_addr;

	/* Enable RISCV_SYS MMIO access. */
	writel_relaxed(CCU_BGR_ENABLE, SUN252I_F101_CCU_BASE + RISCV_CFG_BGR_REG);

	/* Program the reset entry address for the boot hart. */
	sun252i_f101_set_bootaddr(0, entry);

	/* Put the cluster power controller in automatic mode. */
	writel_relaxed(0, PMC_BASE(4) + SW_MODE_CTRL(0));
	writel_relaxed(0, PMC_BASE(4) + SW_MODE_CTRL(1));

	/* Configure PMC delay timers used by CPU power transitions. */
	writel_relaxed(1, PMC_BASE(0) + TIMER_MODE_CONFIG);
	writel_relaxed(0x01010100, PMC_BASE(0) + DELAY_CTRL_CONFIG);
	writel_relaxed(1, PMC_BASE(1) + TIMER_MODE_CONFIG);
	writel_relaxed(0x01010100, PMC_BASE(1) + DELAY_CTRL_CONFIG);
	writel_relaxed(1, PMC_BASE(4) + TIMER_MODE_CONFIG);
	writel_relaxed(0x01010100, PMC_BASE(4) + DELAY_CTRL_CONFIG);
}

static void sun252i_f101_hart_poweroff(u32 hartid, bool wakeup)
{
	/* Flush all dirty D-cache lines before the core is powered down. */
	/* th.dcache.call; .word-encoded so upstream binutils (no xtheadcmo) work. */
	asm volatile(".word 0x0010000b" ::: "memory");

	if (csr_read(THEAD_C9XX_CSR_MXSTATUS) & BIT(22))
		asm volatile("fence iorw, iorw" ::: "memory");
	else
		/* th.sync.s; .word-encoded so upstream binutils (no xtheadsync) work. */
		asm volatile(".word 0x0190000b" ::: "memory");

	/* Stop cache snooping before the PMC removes power from the core. */
	csr_clear(THEAD_C9XX_CSR_MSMPR, BIT(0));

	if (wakeup) {
		/* CPU idle: let the PMC select its automatic transition. */
		writel_relaxed(0, PMC_BASE(hartid) + SW_MODE_CTRL(0));
		writel_relaxed(0, PMC_BASE(hartid) + SW_MODE_CTRL(1));
	} else {
		/* HSM stop: request a manual power-down transition. */
		writel_relaxed(0x2 << 3, PMC_BASE(hartid) + SW_MODE_CTRL(0));
		writel_relaxed(0x2 << 3, PMC_BASE(hartid) + SW_MODE_CTRL(1));
	}

	wfi();
}

static int sun252i_f101_hart_suspend(u32 suspend_type,
				   ulong mmode_resume_addr)
{
	(void)mmode_resume_addr;

	/* Retentive suspend is implemented by the generic HSM code. */
	if (!(suspend_type & SBI_HSM_SUSP_NON_RET_BIT))
		return SBI_ENOTSUPP;

	sun252i_f101_hart_poweroff(current_hartid(), true);

	return SBI_OK;
}

static void sun252i_f101_hart_resume(void)
{
	u32 hartid = current_hartid();

	/* Keep the current core powered after its warm-boot sequence. */
	writel_relaxed(0x3 << 3, PMC_BASE(hartid) + SW_MODE_CTRL(0));
	writel_relaxed(0x11, PMC_BASE(hartid) + SW_MODE_CTRL(1));
}

static int sun252i_f101_hart_start(u32 hartid, ulong saddr)
{
	sun252i_f101_set_bootaddr(hartid, saddr);

	/* Select manual mode and power on the requested core. */
	writel_relaxed(0x3 << 3, PMC_BASE(hartid) + SW_MODE_CTRL(0));
	writel_relaxed(0x11, PMC_BASE(hartid) + SW_MODE_CTRL(1));

	return SBI_OK;
}

static int sun252i_f101_hart_stop(void)
{
	sun252i_f101_hart_poweroff(current_hartid(), false);

	return SBI_OK;
}

static const struct sbi_hsm_device sun252i_f101_ppu = {
	.name		= "sun252i-f101-ppu",
	.hart_suspend	= sun252i_f101_hart_suspend,
	.hart_resume	= sun252i_f101_hart_resume,
	.hart_start	= sun252i_f101_hart_start,
	.hart_stop	= sun252i_f101_hart_stop,
};

static const struct sbi_cpu_idle_state sun252i_f101_cpu_idle_states[] = {
	{
		.name			= "cpu-nonretentive",
		.suspend_param		= SBI_HSM_SUSPEND_NON_RET_DEFAULT,
		.local_timer_stop	= true,
		.entry_latency_us	= 40,
		.exit_latency_us	= 67,
		.min_residency_us	= 1100,
		.wakeup_latency_us	= 67,
	},
	{ },
};

static int sun252i_f101_final_init(bool cold_boot)
{
	int rc;

	if (cold_boot) {
		sun252i_f101_riscv_cfg_init();
		sbi_hsm_set_device(&sun252i_f101_ppu);

		rc = fdt_add_cpu_idle_states(fdt_get_address_rw(),
					     sun252i_f101_cpu_idle_states);
		if (rc)
			return rc;
	}

	return generic_final_init(cold_boot);
}

static int sun252i_f101_extensions_init(bool cold_boot)
{
	int rc;

	rc = generic_extensions_init(cold_boot);
	if (rc)
		return rc;

	thead_c9xx_register_pmu_device();

	/* C907 CSR values required by the F101 power-management sequence. */
	csr_write(THEAD_C9XX_CSR_MSMPR, BIT(0));
	csr_write(THEAD_C9XX_CSR_MCCR2, 0xa0420002);
	csr_write(THEAD_C9XX_CSR_MXSTATUS, 0x438000);
	csr_write(THEAD_C9XX_CSR_MHINT, 0x3a1aa10c);
	csr_write(THEAD_C9XX_CSR_MHCR, 0x10011bf);

#if __riscv_xlen == 64
	csr_write(CSR_MENVCFG, 0x4000000000000000ULL);
#endif

	return SBI_OK;
}

static int sun252i_f101_system_reset_check(u32 type, u32 reason)
{
	(void)reason;

	switch (type) {
	case SBI_SRST_RESET_TYPE_SHUTDOWN:
		return 1;
	case SBI_SRST_RESET_TYPE_COLD_REBOOT:
	case SBI_SRST_RESET_TYPE_WARM_REBOOT:
		return 255;
	default:
		return 0;
	}
}

static void sun252i_f101_wdt_reset_system(void)
{
	/* Disable the watchdog before programming its reset mode. */
	writel((readl(WDT_CFG) & ~BIT(0)) | WDT_UPDATE_FLAG, WDT_CFG);
	sbi_timer_mdelay(1);

	/* Reset after the hardware's 0.5-second watchdog timeout. */
	writel(WDT_UPDATE_FLAG | BIT(0), WDT_CFG);
	writel(WDT_UPDATE_FLAG, WDT_MODE);
	writel(readl(WDT_MODE) | BIT(0) | WDT_UPDATE_FLAG, WDT_MODE);
}

static void sun252i_f101_system_reset(u32 type, u32 reason)
{
	(void)reason;

	switch (type) {
	case SBI_SRST_RESET_TYPE_SHUTDOWN:
		while (1)
			wfi();
	case SBI_SRST_RESET_TYPE_COLD_REBOOT:
	case SBI_SRST_RESET_TYPE_WARM_REBOOT:
		sun252i_f101_wdt_reset_system();
		while (1)
			wfi();
	default:
		sbi_hart_hang();
	}
}

static struct sbi_system_reset_device sun252i_f101_reset = {
	.name = "sun252i-f101-reset",
	.system_reset_check = sun252i_f101_system_reset_check,
	.system_reset = sun252i_f101_system_reset,
};

static int sun252i_f101_early_init(bool cold_boot)
{
	if (cold_boot)
		sbi_system_reset_add_device(&sun252i_f101_reset);

	return generic_early_init(cold_boot);
}

static int sun252i_f101_platform_init(const void *fdt, int nodeoff,
				     const struct fdt_match *match)
{
	(void)fdt;
	(void)nodeoff;
	(void)match;

	generic_platform_ops.early_init = sun252i_f101_early_init;
	generic_platform_ops.final_init = sun252i_f101_final_init;
	generic_platform_ops.extensions_init = sun252i_f101_extensions_init;

	return SBI_OK;
}

static const struct fdt_match sun252i_f101_match[] = {
	{ .compatible = "allwinner,sun252i-f101" },
	{ },
};

const struct fdt_driver sun252i_f101 = {
	.match_table = sun252i_f101_match,
	.init = sun252i_f101_platform_init,
};
