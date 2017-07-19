/*
 * Suspend-to-RAM support code for SH-Mobile ARM
 * arch/arm/mach-shmobile/suspend.c
 *
 * Copyright (C) 2011 Magnus Damm
 * Copyright (C) 2012 Renesas Mobile Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#define pr_fmt(fmt)	"PM-DBG: " fmt

#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/clk.h>
#include <mach/pm.h>
#include <asm/system.h>
#include <linux/delay.h>
#include <linux/hwspinlock.h>
/* #include <mach/sbsc.h> */
#include <mach/common.h>
#include <linux/wakelock.h>
#include <linux/cpufreq.h>
#ifndef CONFIG_PM_HAS_SECURE
#include "pm_ram0.h"
#else /*CONFIG_PM_HAS_SECURE*/
#include "pm_ram0_tz.h"
#endif /*CONFIG_PM_HAS_SECURE*/
#include "pmRegisterDef.h"

#ifdef CONFIG_SHMOBILE_RAM_DEFRAG
#include <mach/ram_defrag.h>
#endif /* CONFIG_SHMOBILE_RAM_DEFRAG */
#include <memlog/memlog.h>

#define pm_writeb(v, a)			__raw_writeb(v, a)
#define pm_writew(v, a)			__raw_writew(v, a)
#define pm_writel(v, a)			__raw_writel(v, a)
#define pm_readb(a)				__raw_readb(a)
#define pm_readw(a)				__raw_readw(a)
#define pm_readl(a)				__raw_readl(a)
#define DO_SAVE_REGS(array)		do_save_regs(array, ARRAY_SIZE(array))
#define DO_RESTORE_REGS(array)	do_restore_regs(array, ARRAY_SIZE(array))

#ifndef CONFIG_PM_HAS_SECURE
#define RAM_ARM_VECT                   secramBasePhys
#else /*CONFIG_PM_HAS_SECURE*/
#define RAM_ARM_VECT                   ram0ArmVectorPhys
#endif /*CONFIG_PM_HAS_SECURE*/

#define CPG_SET_FREQ_MAX_RETRY (10000)	/* 10ms */

#define RESCSR_HEADER		0xA5A5A500U
#define RWTCSRA_TME_MASK	0x80U
#define RWTCSRA_WOVF_MASK	0x10U
#define RWTCSRA_WRFLG_MASK	0x20U

#define CMSTR15_STR0_MASK	0x1U
#define CMCLKE_CH5_CLKE_MASK	(0x1 << 5)

/* SYS-ARM Core0 IRQ wakeup mask */
#define WUPSMSK_SYSARMIRQ0M_MSK	(0x1 << 28)


enum {
	IRQC_EVENTDETECTOR_BLK0 = 0,
	IRQC_EVENTDETECTOR_BLK1,
	IRQC_EVENTDETECTOR_BLK10,
	IRQC_EVENTDETECTOR_BLK11,
	IRQC_EVENTDETECTOR_BLK12,
	HSGPR,
	SYSGPR,
	HPB,
	SHWYSTATHS,
	SHWYSTATSY,
	SHWYSTATDM,
	SHBUF,
};

static suspend_state_t shmobile_suspend_state;
static int not_core_shutdown;

static unsigned int save_sbar_val;

static int log_wakeupfactor;
module_param(log_wakeupfactor, int, S_IRUGO | S_IWUSR | S_IWGRP);

#ifndef CONFIG_U2_SYS_CPU_DORMANT_MODE
static struct clk *wdt_clk;
#endif

static char xtal1_log_out;

static struct cpufreq_lmt_node cpufreq_node;

/*Change clocks function*/
unsigned int frqcrA_save;
unsigned int frqcrB_save;
unsigned int frqcrD_save;
#define CLOCK_SUSPEND		0
#define CLOCK_RESTORE		1
#define ZB3_CLK_SUSPEND		0

#define I2C_ICCRDVM_DUMMY_READ_LOOP	0x1000


#ifdef CONFIG_PM_DEBUG
/*
 * Dynamic on/off for System Suspend
 *   0: System Suspend is disable
 *   1: System Suspend in enable
 */
static int enable_module = 1;
static DEFINE_SPINLOCK(systemsuspend_lock);
#endif	/* CONFIG_PM_DEBUG */
struct base_map {
	unsigned long phys;	/* phys base  */
	int size;			/* remap size */
	void __iomem *base;	/* virt base  */
};

struct reg_info {
	void __iomem **vbase;
	unsigned long offset;
	int size;
	unsigned int val;
};

#define PM_SAVE_REG(type, of, sz)	\
{					\
	.vbase  = &map[type].base,	\
	.offset = of,			\
	.size   = sz,			\
}

static struct base_map map[] = {
	[IRQC_EVENTDETECTOR_BLK0] = {
		.size = SZ_4K,
		.base = IRQC_EVENTDETECTOR_BLK0_BASE,
	},
	[IRQC_EVENTDETECTOR_BLK1] = {
		.size = SZ_4K,
		.base = IRQC_EVENTDETECTOR_BLK1_BASE,
	},
	[IRQC_EVENTDETECTOR_BLK10] = {
		.size = SZ_4K,
		.base = IRQC_EVENTDETECTOR_BLK10_BASE,
	},
	[IRQC_EVENTDETECTOR_BLK11] = {
		.size = SZ_4K,
		.base = IRQC_EVENTDETECTOR_BLK11_BASE,
	},
	[IRQC_EVENTDETECTOR_BLK12] = {
		.size = SZ_4K,
		.base = IRQC_EVENTDETECTOR_BLK12_BASE,
	},
	[HSGPR] = {
		.phys = HSGPR_BASE_PHYS,
		.size = SZ_4K,
	},
	[SYSGPR] = {
		.phys = SYSGPR_BASE_PHYS,
		.size = SZ_4K,
	},
	[HPB] = {
		.size = SZ_8K,
		.base = HPB_BASE,
	},
	[SHWYSTATHS] = {
		.size = SZ_4K,
		.base = SHWYSTATHS_BASE,
	},
	[SHWYSTATSY] = {
		.size = SZ_4K,
		.base = SHWYSTATSY_BASE,
	},
	[SHWYSTATDM] = {
		.phys = SHWYSTATDM_BASE_PHYS,
		.size = SZ_4K,
	},
	[SHBUF] = {
		.size = SZ_4K,
		.base = SHBUF_BASE,
	},
};

static struct reg_info shwy_regs[] = {
/* SHBUF */
	PM_SAVE_REG(SHBUF, SHBMCTR,    32),
	PM_SAVE_REG(SHBUF, SHBMAR,     32),
	PM_SAVE_REG(SHBUF, SHBARCR11,  32),
	PM_SAVE_REG(SHBUF, SHBARCR12,  32),
	PM_SAVE_REG(SHBUF, SHBMCTR2,   32),
	PM_SAVE_REG(SHBUF, SHBADDR00,  32),
	PM_SAVE_REG(SHBUF, SHBMSKR00,  32),
	PM_SAVE_REG(SHBUF, SHBCHCTR00, 32),
	PM_SAVE_REG(SHBUF, SHBSIZER00, 32),
	PM_SAVE_REG(SHBUF, SHBADDR01,  32),
	PM_SAVE_REG(SHBUF, SHBMSKR01,  32),
	PM_SAVE_REG(SHBUF, SHBCHCTR01, 32),
	PM_SAVE_REG(SHBUF, SHBSIZER01, 32),
	PM_SAVE_REG(SHBUF, SHBADDR02,  32),
	PM_SAVE_REG(SHBUF, SHBMSKR02,  32),
	PM_SAVE_REG(SHBUF, SHBCHCTR02, 32),
	PM_SAVE_REG(SHBUF, SHBSIZER02, 32),
	PM_SAVE_REG(SHBUF, SHBADDR03,  32),
	PM_SAVE_REG(SHBUF, SHBMSKR03,  32),
	PM_SAVE_REG(SHBUF, SHBCHCTR03, 32),
	PM_SAVE_REG(SHBUF, SHBSIZER03, 32),
	PM_SAVE_REG(SHBUF, SHBADDR04,  32),
	PM_SAVE_REG(SHBUF, SHBMSKR04,  32),
	PM_SAVE_REG(SHBUF, SHBCHCTR04, 32),
	PM_SAVE_REG(SHBUF, SHBSIZER04, 32),
	PM_SAVE_REG(SHBUF, SHBADDR05,  32),
	PM_SAVE_REG(SHBUF, SHBMSKR05,  32),
	PM_SAVE_REG(SHBUF, SHBCHCTR05, 32),
	PM_SAVE_REG(SHBUF, SHBSIZER05, 32),
	PM_SAVE_REG(SHBUF, SHBADDR06,  32),
	PM_SAVE_REG(SHBUF, SHBMSKR06,  32),
	PM_SAVE_REG(SHBUF, SHBCHCTR06, 32),
	PM_SAVE_REG(SHBUF, SHBSIZER06, 32),
	PM_SAVE_REG(SHBUF, SHBADDR07,  32),
	PM_SAVE_REG(SHBUF, SHBMSKR07,  32),
	PM_SAVE_REG(SHBUF, SHBCHCTR07, 32),
	PM_SAVE_REG(SHBUF, SHBSIZER07, 32),
/* HS GPR */
	PM_SAVE_REG(HSGPR, HSPRPRICR,    32),
	PM_SAVE_REG(HSGPR, HSPRARCR11,   32),
	PM_SAVE_REG(HSGPR, HSPRARCR12,   32),
	PM_SAVE_REG(HSGPR, HSPRARCR13,   32),
	PM_SAVE_REG(HSGPR, HSPRARCR14,   32),
	PM_SAVE_REG(HSGPR, HSPRARCR31,   32),
	PM_SAVE_REG(HSGPR, HSPRARCR32,   32),
	PM_SAVE_REG(HSGPR, HSPRARCR33,   32),
	PM_SAVE_REG(HSGPR, HSPRARCR34,   32),
	PM_SAVE_REG(HSGPR, HSPRERRMSK,   32),
	PM_SAVE_REG(HSGPR, HSPRPRICNT11, 32),
	PM_SAVE_REG(HSGPR, HSPRPRICNT12, 32),
	PM_SAVE_REG(HSGPR, HSPRPRICNT13, 32),
/* Sys GPR */
	PM_SAVE_REG(SYSGPR, SYPRPRICR,  32),
	PM_SAVE_REG(SYSGPR, SYPRARCR11, 32),
	PM_SAVE_REG(SYSGPR, SYPRARCR12, 32),
	PM_SAVE_REG(SYSGPR, SYPRARCR13, 32),
	PM_SAVE_REG(SYSGPR, SYPRERRMSK, 32),
/* HPB */
	PM_SAVE_REG(HPB, HPBCTRL1,      32),
	PM_SAVE_REG(HPB, HPBCTRL2,      32),
	PM_SAVE_REG(HPB, HPBCTRL4,      32),
	PM_SAVE_REG(HPB, HPBCTRL5,      32),
	PM_SAVE_REG(HPB, HPBCTRL7,      32),
	PM_SAVE_REG(HPB, OCPBRGWIN1,    32),
	PM_SAVE_REG(HPB, OCPBRGWIN2,    32),
	PM_SAVE_REG(HPB, OCPBRGWIN3,    32),
/* SHWYSTAT HS */
	PM_SAVE_REG(SHWYSTATHS, SHSTxCR,    32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxIR,    32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxDMR,   32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxCNT,   32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxTN,    32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxTR,    32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxAM11,  32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxAM12,  32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxTM1,   32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxAM21,  32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxAM22,  32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxTM2,   32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxATRM1, 32),
	PM_SAVE_REG(SHWYSTATHS, SHSTxATRM2, 32),
/* SHWYSTAT SYS */
	PM_SAVE_REG(SHWYSTATSY, SHSTxCR,    32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxIR,    32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxDMR,   32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxCNT,   32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxTN,    32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxTR,    32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxAM11,  32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxAM12,  32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxTM1,   32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxAM21,  32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxAM22,  32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxTM2,   32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxATRM1, 32),
	PM_SAVE_REG(SHWYSTATSY, SHSTxATRM2, 32),
};

struct reg_info shwystatdm_regs[] = {
/* SHWYSTAT DM */
	PM_SAVE_REG(SHWYSTATDM, SHSTxCR,    32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxIR,    32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxDMR,   32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxCNT,   32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxTN,    32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxTR,    32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxAM11,  32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxAM12,  32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxTM1,   32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxAM21,  32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxAM22,  32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxTM2,   32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxATRM1, 32),
	PM_SAVE_REG(SHWYSTATDM, SHSTxATRM2, 32),
};

/*
 * Helper functions for system suspend
 */
static void wakeups_factor(void)
{
	unsigned int dummy;

	/* clear */
	dummy = __raw_readl(WUPSFAC);

	if (log_wakeupfactor == 1) {
		pr_info("WakeUpFactor Value = 0x%08x\n", \
					dummy);
		pr_info("WakeUpS Mask Value = 0x%08x\n", \
					__raw_readl(WUPSMSK));

		/* For IRQ0,IRQ1 wakeup factors */
		if ((dummy & 0x40) != 0)
			pr_info(" Wakeup by IRQ[31:0]: 0x%08x\n", \
					__raw_readl(ram0_IRQ0FAC));
		else if ((dummy & 0x80) != 0)
			pr_info(" Wakeup by IRQ[63:32]: 0x%08x\n",\
					__raw_readl(ram0_IRQ1FAC));
		else
			pr_info("Not wakeup by IRQ wakeup factors.\n");
	}
}

/*
 * Helper functions for getting platform suspend state
 */
suspend_state_t get_shmobile_suspend_state(void)
{
	return shmobile_suspend_state;
}

#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
/*
 * Helper functions for saving IP registers value
 */
static void do_save_regs(struct reg_info *regs, int count)
{
	struct reg_info *info = regs;
	int i;

	for (i = 0; i < count; i++, info++) {
		if (!*info->vbase)
			continue;
		switch (info->size) {
		case 8:
			info->val = pm_readb(*info->vbase + \
						info->offset);
			break;
		case 16:
			info->val = pm_readw(*info->vbase + \
						info->offset);
			break;
		case 32:
			info->val = pm_readl(*info->vbase + \
						info->offset);
			break;
		default:
			break;
		}
	}
}

/*
 * Helper functions for restoring IP registers value
 */
static void do_restore_regs(struct reg_info *regs, int count)
{
	struct reg_info *info;
	int i;

	info = regs + count;

	for (i = count; i > 0; i--) {
		info--;
		if (!*info->vbase)
			continue;
		switch (info->size) {
		case 8:
			pm_writeb(info->val, (*info->vbase + \
					info->offset));
			break;
		case 16:
			pm_writew(info->val, (*info->vbase + \
					info->offset));
			break;
		case 32:
			pm_writel(info->val, (*info->vbase + \
					info->offset));
			break;
		default:
			break;
		}
	}
}

static void shwy_regs_save(void)
{
	DO_SAVE_REGS(shwy_regs);
}

void shwystatdm_regs_save(void)
{
	DO_SAVE_REGS(shwystatdm_regs);
}

static void shwy_regs_restore(void)
{
	DO_RESTORE_REGS(shwy_regs);
}

void shwystatdm_regs_restore(void)
{
	DO_RESTORE_REGS(shwystatdm_regs);
}

/*
 * Helper functions for checking CPU status
 */
static int core_shutdown_status(unsigned int cpu)
{
	return (__raw_readl(SCPUSTR) >> (4 * cpu)) & 3;
}
#endif

/*
 * CPU doesn't seem to wakeup from suspend if frequency
 * is anything less than SUSPEND_CPUFREQ. As an workaroud
 * adjust cpu min frequency during device suspend.
 */
static int suspend_cpufreq_notifier(struct notifier_block *nb,
				    unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;
	unsigned long min_freq = SUSPEND_CPUFREQ;

	if (event != CPUFREQ_INCOMPATIBLE || !is_suspend_request)
		return 0;

	if (policy->min != min_freq)
		cpufreq_verify_within_limits(policy,
				min_freq, policy->user_policy.max);

	return 0;
}

static struct notifier_block suspend_cpufreq_notifier_block = {
	.notifier_call = suspend_cpufreq_notifier,
};

/*
 * System suspend callback functions' implementation
 */
static int shmobile_suspend_begin(suspend_state_t state)
{
	int ret;

	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_BEGIN, 1);
	shmobile_suspend_state = state;
	if (get_shmobile_suspend_state() & PM_SUSPEND_MEM)
		is_suspend_request = 1;

	cpufreq_register_notifier(&suspend_cpufreq_notifier_block,
					  CPUFREQ_POLICY_NOTIFIER);
	/* set DFS mode */
	ret = cpufreq_add_lmt_req(&cpufreq_node, "sys-suspend",
				SUSPEND_CPUFREQ, FREQ_MIN_LIMIT);
	if (ret != 0) {
		pr_debug("%s: q() returns cpufreq_add_lmt_req %d.\n",
			__func__, ret);
	}

	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_BEGIN, 0);
	return 0;
}

static void shmobile_suspend_end(void)
{
	int ret;

	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_END, 1);
	shmobile_suspend_state = PM_SUSPEND_ON;
	is_suspend_request = 0;

	if (not_core_shutdown) {
		pr_debug("%s: CPU0 waited until the" \
				"CPU1 power down.\n", __func__);
		not_core_shutdown = 0;
	}

	cpufreq_unregister_notifier(&suspend_cpufreq_notifier_block,
					    CPUFREQ_POLICY_NOTIFIER);

	ret = cpufreq_del_lmt_req(&cpufreq_node);
	if (ret != 0) {
		pr_debug("%s: cpufreq_del_lmt_req() returns %d.\n",
				__func__, ret);
	}

	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_END, 0);
}

#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
/*
 *  setting for going to Low Power Mode without I2CDVM
 */
static void set_regs_for_LPM(void)
{
	unsigned int data;

    /* DVFSCR1 [30:29:28] <= 100 */
	data = __raw_readl(DVFSCR1);
	data = data & 0xCFFFFFFF;
	data = data | 0x40000000;
	__raw_writel(data, DVFSCR1);

	/* SYSC.SYCKENMSK LPMEN=1 */
	__raw_writel(0x402F0000, SYCKENMSK);

	/* SYSC.LPMWUCNT=0000007E */
	__raw_writel(0xA550007E, LPMWUCNT);

	/* EXMSKCNT1    =0000007E */
	__raw_writel(0xA550007E, EXMSKCNT1);

	/* LPMWUMSKCNT  =00000001 */
	__raw_writel(0xA5500001, LPMWUMSKCNT);

	/* LPMR to disable I2CDVM and CPG TIMEOUT ON */
	__raw_writel(0x00800102, LPMR);

	/* disable wakeup of I2CDVM timeout */
	data = __raw_readl(WUPSMSK);

	if (pm_is_force_sleep() &&
			pm_fsleep_no_wakeup())
		data |= ~0;
	else
		data = data | 0x01000000;

	__raw_writel(data, WUPSMSK);

	return;
}

/*
 *  setting for leaving from Low Power Mode without I2CDVM
 */
static void reset_regs_for_LPM(void)
{
	unsigned char cdata;
	int count;

	/* SYSC.SYCKENMSK LPMEN=0 */
	__raw_writel(0x002F0000, SYCKENMSK);

	/* reset ICCRDVM */
	cdata = __raw_readb(ICCRDVM);
	cdata = cdata & 0x7F;
	__raw_writeb(cdata, ICCRDVM);

	for (count = 0; count < I2C_ICCRDVM_DUMMY_READ_LOOP; count++) {
		cdata = __raw_readb(ICCRDVM);
		if ((cdata & 0x80) == 0)
			break;
	}
	if (count >= I2C_ICCRDVM_DUMMY_READ_LOOP)
		pr_debug("I2C DVM reset error\n");

	cdata = __raw_readb(ICCRDVM);
	cdata = cdata | 0x80;
	__raw_writeb(cdata, ICCRDVM);

	return;
}
#endif

/*
 * suspend_set_clock
 *
 * Arguments:
 *		@is_restore:
 *			0: set clock when suspending
 *			1: restore clock when resuming
 * Return:
 *		0: successful
 */
int suspend_set_clock(unsigned int is_restore)
{
	int clocks_ret = 0;
	int cpg_clocks_ret = 0;
	unsigned int frqcrA_suspend_clock;
	unsigned int frqcrB_suspend_clock;
	unsigned int zb3_clock;
	unsigned int frqcrA_mask;
	unsigned int frqcrB_mask;
	int i;

	frqcrA_suspend_clock = POWERDOWN_FRQCRA_ES2;

	/*
	 * ASIC RMU2B-E102: CA7BRG overflow
	 * Set AXI ZX clock to maximum frequency in suspend
	 * until issue is fixed from modem side.
	 */
	if (shmobile_is_u2b())
		frqcrB_suspend_clock = POWERDOWN_FRQCRB_ES2B;
	else
		frqcrB_suspend_clock = POWERDOWN_FRQCRB_ES2;

	zb3_clock = ZB3_CLK_SUSPEND;
	frqcrA_mask = FRQCRA_MASK_ES2;
	frqcrB_mask = FRQCRB_MASK_ES2;

	if (!is_restore) {
		pr_info("[%s]: Suspend: Set clock for suspending\n",\
			__func__);
		memory_log_dump_int(
			PM_DUMP_ID_SUSPEND_SET_CLOCK_RETRY_1,
			PM_DUMP_START);
		for (i = 0; i < CPG_SET_FREQ_MAX_RETRY; i++) {
			/* Backup FRQCRA/B */
			frqcrA_save = __raw_readl(FRQCRA);
			frqcrB_save = __raw_readl(FRQCRB);

			cpg_clocks_ret = clock_update(
			frqcrA_suspend_clock, frqcrA_mask,
			frqcrB_suspend_clock, frqcrB_mask);
			if (cpg_clocks_ret < 0)
				udelay(1);
			else
				break;
		}
		if (cpg_clocks_ret < 0) {
			memory_log_dump_int(
			PM_DUMP_ID_SUSPEND_SET_CLOCK_RETRY_1,
			cpg_clocks_ret);
			pr_err("[%s]: Set clocks FAILED\n",\
				__func__);
		} else {
			memory_log_dump_int(
			PM_DUMP_ID_SUSPEND_SET_CLOCK_RETRY_1,
			PM_DUMP_END);
			pr_info("[%s]: Set clocks OK(retry %d)\n",
				__func__, i);
		}
#if (defined ZB3_CLK_SUSPEND_ENABLE) && (defined ZB3_CLK_DFS_ENABLE)
		frqcrD_save = suspend_ZB3_backup();
		if (frqcrD_save > 0) {
			clocks_ret = cpg_set_sbsc_freq(zb3_clock);
			if (clocks_ret < 0)
				pr_err("[%s]: Set ZB3 clks FAILED\n", __func__);
			else
				pr_info("[%s]: Set ZB3 clks OK\n", __func__);
		} else {
			pr_err("[%s]: Backup ZB3 clock FAILED\n", __func__);
			clocks_ret = frqcrD_save;
		}
#endif
	} else {
		pr_info("[%s]: Restore clock for resuming\n",
			__func__);

		memory_log_dump_int(
			PM_DUMP_ID_SUSPEND_SET_CLOCK_RETRY_2,
			PM_DUMP_START);
		for (i = 0; i < CPG_SET_FREQ_MAX_RETRY; i++) {
			cpg_clocks_ret = clock_update(frqcrA_save, frqcrA_mask,
					frqcrB_save, frqcrB_mask);
			if (cpg_clocks_ret < 0)
				udelay(1);
			else
				break;
		}

		if (cpg_clocks_ret < 0) {
			memory_log_dump_int(
			PM_DUMP_ID_SUSPEND_SET_CLOCK_RETRY_2,
			cpg_clocks_ret);
			pr_err("[%s]: Restore clocks FAILED\n",
				__func__);
		} else {
			memory_log_dump_int(
			PM_DUMP_ID_SUSPEND_SET_CLOCK_RETRY_2,
			PM_DUMP_END);
			pr_info("[%s]: Restore clocks OK(retry %d)\n",
				__func__, i);
		}
#if (defined ZB3_CLK_SUSPEND_ENABLE) && (defined ZB3_CLK_DFS_ENABLE)
		clocks_ret = cpg_set_sbsc_freq(frqcrD_save);
		if (clocks_ret < 0)
			pr_err("[%s]: Restore ZB3 clocks FAILED\n", __func__);
		else
			pr_info("[%s]: Restore ZB3 clocks OK\n", __func__);
#endif
	}
	return clocks_ret | cpg_clocks_ret;
}

#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
static int shmobile_suspend(void)
{
	unsigned int bankState;
	unsigned int workBankState2Area;
	unsigned int dramPasrSettingsArea0;
	unsigned int dramPasrSettingsArea1;
	unsigned int cpu;

	/* check cpu#1 power down */
	if (core_shutdown_status(1) != 3) {
		not_core_shutdown = 1;
		udelay(1000);
		barrier();
		if (core_shutdown_status(1) != 3)
			return -EBUSY;
	}

	/* Backup IP registers */
	/* irqx_eventdetectors_regs_save(); */
	shwy_regs_save();

#if ((defined CONFIG_SHMOBILE_PASR_SUPPORT) \
		&& (defined CONFIG_SHMOBILE_RAM_DEFRAG))
	/* Get ram bank status */
	bankState = get_ram_banks_status();
	if (bankState == -ENOTSUPP)		/* Ram Defrag is disabled */
		bankState = 0xFFFF;
#else
	bankState = 0xFFFF;
#endif
	/*
	 * Get OP of DRAM area 0 and area 1
	 * Bit[0->7] : OP of area 0
	 * Bit[8->15]: OP of area 1
	 */
	workBankState2Area = (SbscDramPasr2Area & ~bankState);
	if (workBankState2Area != 0) {
		/* Get setting OP, MA of DRAM area 0
		 *(OP = Bit[0->7] of workBankState2Area)
		 */
		dramPasrSettingsArea0 = ((workBankState2Area & 0x00FF) << 8) \
								| MRW_MA_PASR;
		dramPasrSettingsArea1 = (workBankState2Area & 0xFF00) \
								| MRW_MA_PASR;
	} else { /* workBankState2Area == 0 */
		dramPasrSettingsArea0 = 0;
		dramPasrSettingsArea1 = 0;
	}

	pr_debug("%s: RAM bank status:\n", __func__);
	pr_debug("bankState: 0x%x workBankState2Area: 0x%08x\n" \
				, bankState, workBankState2Area);
	pr_debug("dramPasrSettingsArea0: 0x%08x ", dramPasrSettingsArea0);
	pr_debug("dramPasrSettingsArea1: 0x%08x\n", dramPasrSettingsArea1);

	/* Save setting value to ram0 */
	pm_writel(dramPasrSettingsArea0, ram0DramPasrSettingArea0);
	pm_writel(dramPasrSettingsArea1, ram0DramPasrSettingArea1);

	xtal1_log_out = 1;

	__raw_writel((__raw_readl(WUPSMSK) | (1 << 28)), WUPSMSK);

#ifndef CONFIG_PM_HAS_SECURE
	pm_writel(1, ram0ZQCalib);
#endif	/*CONFIG_PM_HAS_SECURE*/

	/* Update clocks setting */
	if (suspend_set_clock(CLOCK_SUSPEND) != 0) {
			pr_debug("%s: Suspend without "\
			"updating clock setting\n", __func__);
	}

	set_regs_for_LPM();
#ifdef CONFIG_ARCH_R8A7373
	if (pmdbg_get_enable_dump_suspend())
		pmdbg_dump_suspend();
	if (pmdbg_get_clk_dump_suspend())
		r8a7373_clk_print_active_clks();
#endif /* CONFIG_ARCH_R8A7373 */
	/*
	 * do cpu suspend ...
	 */
	if (pm_is_force_sleep())
		pm_fsleep_disable_pds();

	pr_err("[%s]: do cpu suspend ...\n\n", __func__);
	memory_log_func(PM_FUNC_ID_JUMP_SYSTEMSUSPEND, 1);
	jump_systemsuspend();
	memory_log_func(PM_FUNC_ID_JUMP_SYSTEMSUSPEND, 0);

	/* Update clocks setting */
	if (suspend_set_clock(CLOCK_RESTORE) != 0)
		pr_debug("%s: Resume after "\
			"restoring clock setting\n", __func__);

#ifndef CONFIG_PM_HAS_SECURE
	pm_writel(0, ram0ZQCalib);
#endif	/*CONFIG_PM_HAS_SECURE*/

	wakeups_factor();
	reset_regs_for_LPM();

#ifdef CONFIG_PM_HAS_SECURE
	for_each_possible_cpu(cpu)
		pr_info("%s: SEC HAL return cpu%d: 0x%08x\n", __func__, cpu,
			__raw_readl(ram0SecHalReturnCpu0 + (0x4 * cpu)));
#endif /*CONFIG_PM_HAS_SECURE*/

	/* - set Wake up factor unmask to GIC.CPU0,CPU1 */
	__raw_writel((__raw_readl(WUPSMSK) &  ~(1 << 28)), WUPSMSK);

	/* Restore IP registers */
	shwy_regs_restore();
	/* irqx_eventdetectors_regs_restore(); */

	return 0;

}
#endif

#ifndef CONFIG_U2_SYS_CPU_DORMANT_MODE
static void suspend_wtd_clear(void)
{
#define RTWD_WRFLG_RETRIES	10000
	u8 reg8;
	u32 retries = RTWD_WRFLG_RETRIES;


	do {
		reg8 = __raw_readb(RWDTCSRA);
	} while ((reg8 & RWTCSRA_WRFLG_MASK) && retries--);

	BUG_ON(retries <= 0);

	__raw_writel(RWTCNT_CLEAR, RWTCNT);
}

static void suspend_disable_wtd(bool disable)
{
	BUG_ON(wdt_clk == NULL);

	if (disable)
		clk_disable(wdt_clk);
	else
		clk_enable(wdt_clk);
}

static void suspend_disable_cmt(bool disable)
{
	u32 reg32;

	reg32 = __raw_readl(CMCLKE);

	/**
	 * Halt CMT it its runnig
	 */
	if (disable)
		reg32 &= ~CMCLKE_CH5_CLKE_MASK;
	else
		reg32 |= CMCLKE_CH5_CLKE_MASK;

	__raw_writel(reg32, CMCLKE);
}

static inline bool is_cmt15_enabled(void)
{
	return __raw_readl(CMCLKE) & CMCLKE_CH5_CLKE_MASK;
}

static inline bool is_wdt_enabled(void)
{
	return __raw_readb(RWDTCSRA) & RWTCSRA_TME_MASK;
}

static int shmobile_suspend_enter_wfi(void)
{
	bool cmt15_en = false;
	bool wdt_en = false;

	/* STOP CMT15 if enabled */
	if (is_cmt15_enabled()) {
		suspend_disable_cmt(true);
		cmt15_en = true;
	}

	/* Stop RTWD if enabled */
	if (is_wdt_enabled()) {
		suspend_wtd_clear();
		suspend_disable_wtd(true);
		wdt_en = true;
	}

	/**
	 * Wakeup only from external IRQC
	 */
	__raw_writel((__raw_readl(WUPSMSK) | WUPSMSK_SYSARMIRQ0M_MSK),
			WUPSMSK);

	memory_log_func(PM_FUNC_ID_JUMP_SYSTEMSUSPEND, 1);
	/* execute WFI */
	start_wfi();
	memory_log_func(PM_FUNC_ID_JUMP_SYSTEMSUSPEND, 0);
	wakeups_factor();

	if (wdt_en)
		suspend_disable_wtd(false);

	if (cmt15_en)
		suspend_disable_cmt(false);

	__raw_writel((__raw_readl(WUPSMSK) & ~WUPSMSK_SYSARMIRQ0M_MSK),
			WUPSMSK);

	return 0;
}
#endif

static int shmobile_suspend_enter(suspend_state_t unused)
{
	int ret = 0;

	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_ENTER, 1);
	switch (shmobile_suspend_state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
		ret = shmobile_suspend();
#else
		ret = shmobile_suspend_enter_wfi();
#endif
		break;
	default:
		ret = -EINVAL;
	}
	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_ENTER, 0);
	return ret;
}

static int shmobile_suspend_valid(suspend_state_t state)
{
	return ((state > PM_SUSPEND_ON) && (state <= PM_SUSPEND_MAX));
}

static int shmobile_suspend_prepare(void)
{
	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_PREPARE, 1);
#ifdef CONFIG_SHMOBILE_RAM_DEFRAG
	int ret;

	/* Execute RAM Defragmentation */
	ret = defrag();

	if (0 != ret) {
		pr_debug("%s: RAM defragment is not supported.\n", \
				__func__);
	}
#endif /* CONFIG_SHMOBILE_RAM_DEFRAG */

	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_PREPARE, 0);
	return 0;
}

static int shmobile_suspend_prepare_late(void)
{
	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_PREPARE_LATE, 1);
	cpu_idle_poll_ctrl(true);

	/* backup sys boot address */
	save_sbar_val = __raw_readl(SBAR);

	/* set RAM1 vector */
	__raw_writel(RAM_ARM_VECT, SBAR);

	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_PREPARE_LATE, 0);
	return 0;
}

static void shmobile_suspend_wake(void)
{
#ifdef __EXTAL1_INFO__
	unsigned int reg_val = 0;
#endif
	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_WAKE, 1);
	cpu_idle_poll_ctrl(false);

	/* restore sys boot address */
	__raw_writel(save_sbar_val, SBAR);

	/* Log information for disabling EXTAL1 */
#ifdef __EXTAL1_INFO__
	if (xtal1_log_out == 1) {
		pr_info("EXTAL1: Log information\n");
		pr_info("---[Before suspend]---\n");

		reg_val = pm_readl(ram0SaveEXMSKCNT1_suspend);
		pr_info("EXMSKCNT1: 0x%08x\n", reg_val);

		reg_val = pm_readl(ram0SaveAPSCSTP_suspend);
		pr_info("APSCSTP: 0x%08x\n", reg_val);

		reg_val = pm_readl(ram0SaveSYCKENMSK_suspend);
		pr_info("SYCKENMSK: 0x%08x\n", reg_val);

		reg_val = pm_readl(ram0SaveC4POWCR_suspend);
		pr_info("C4POWCR: 0x%08x\n", reg_val);

		reg_val = pm_readl(ram0SavePDNSEL_suspend);
		pr_info("PDNSEL: 0x%08x\n", reg_val);

		reg_val = pm_readl(ram0SavePSTR_suspend);
		pr_info("PSTR: 0x%08x\n", reg_val);

		pr_info("---[After suspend]---\n");

		reg_val = pm_readl(ram0SaveEXMSKCNT1_resume);
		pr_info("EXMSKCNT1: 0x%08x\n", reg_val);

		reg_val = pm_readl(ram0SaveAPSCSTP_resume);
		pr_info("APSCSTP: 0x%08x\n", reg_val);

		reg_val = pm_readl(ram0SaveSYCKENMSK_resume);
		pr_info("SYCKENMSK: 0x%08x\n", reg_val);

		reg_val = pm_readl(ram0SaveC4POWCR_resume);
		pr_info("C4POWCR: 0x%08x\n", reg_val);

		reg_val = pm_readl(ram0SavePDNSEL_resume);
		pr_info("PDNSEL: 0x%08x\n", reg_val);

		reg_val = pm_readl(ram0SavePSTR_resume);
		pr_info("PSTR: 0x%08x\n", reg_val);

		xtal1_log_out = 0;
	}
#endif
	memory_log_func(PM_FUNC_ID_SHMOBILE_SUSPEND_WAKE, 0);
}

#ifdef CONFIG_PM_DEBUG
int control_systemsuspend(int is_enabled)
{
	unsigned long irqflags;
	spin_lock_irqsave(&systemsuspend_lock, irqflags);

	enable_module = is_enabled;
	spin_unlock_irqrestore(&systemsuspend_lock, irqflags);
	return 0;
}
EXPORT_SYMBOL(control_systemsuspend);

int is_systemsuspend_enable(void)
{
	return enable_module;
}
EXPORT_SYMBOL(is_systemsuspend_enable);
#endif /* CONFIG_PM_DEBUG */

struct platform_suspend_ops shmobile_suspend_ops = {
	.begin			= shmobile_suspend_begin,
	.end			= shmobile_suspend_end,
	.enter			= shmobile_suspend_enter,
	.valid			= shmobile_suspend_valid,
	.prepare		= shmobile_suspend_prepare,
	.prepare_late	= shmobile_suspend_prepare_late,
	.wake			= shmobile_suspend_wake,
};

static int __init rmobile_suspend_init(void)
{
	int i;
	void __iomem *virt;
	struct base_map *tbl = map;
#ifdef CONFIG_PM_DEBUG
	log_wakeupfactor = 1;
#else /*CONFIG_PM_DEBUG*/
	log_wakeupfactor = 0;
#endif /*CONFIG_PM_DEBUG*/
	pr_debug("%s: initialize\n", __func__);

	if (!icram_pm_setup) {
		printk (KERN_ERR "%s: Unable to setup icram for pm ops\n", __func__);
		return -EIO;
	}

	/* create address table */
	for (i = 0; i < ARRAY_SIZE(map); i++) {
		if (tbl->base) {
			tbl++;
			continue;
		}
		virt = ioremap_nocache(tbl->phys, tbl->size);
		if (!virt) {
			pr_emerg("%s: ioremap failed. base 0x%lx\n", \
					__func__, tbl->phys);
			tbl++;
			continue;
		}
		tbl->base = virt;
		pr_debug("%s: ioremap phys 0x%lx, virt 0x%p, size %d\n", \
			__func__, tbl->phys, tbl->base, tbl->size);
		tbl++;
	}

	wakeups_factor();

	suspend_set_ops(&shmobile_suspend_ops);

	shmobile_suspend_state = PM_SUSPEND_ON;
#ifndef CONFIG_PM_HAS_SECURE
	pm_writel(0, ram0ZQCalib);
#endif	/*CONFIG_PM_HAS_SECURE*/

#ifndef CONFIG_U2_SYS_CPU_DORMANT_MODE
	wdt_clk = clk_get(NULL, "rwdt0");
	if (!wdt_clk) {
		pr_err("%s: failed to get rwdt_clk\n", __func__);
		return -ENODEV;
	}
#endif
	return 0;
}
device_initcall(rmobile_suspend_init);
