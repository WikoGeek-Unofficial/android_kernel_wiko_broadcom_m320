/*
 * CPUIdle support code for SH-Mobile ARM
 * arch/arm/mach-shmobile/cpuidle.c
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
 */

#include <linux/pm.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <mach/pm.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <mach/system.h>
#include <linux/spinlock_types.h>
#include <linux/cpu.h>
#include <memlog/memlog.h>
#include <linux/irqchip/arm-gic.h>
#include <video/sh_mobile_lcdc.h>

#ifndef CONFIG_PM_HAS_SECURE
#include "pm_ram0.h"
#else /*CONFIG_PM_HAS_SECURE*/
#include "pm_ram0_tz.h"
#endif /*CONFIG_PM_HAS_SECURE*/
#include "pmRegisterDef.h"

#define ZB3_CLK_EARLY_SUSPEND		(97500)

#define CSTANDBY_WUP_INT_DBG_MSK	(1 << 0)
#define CSTANDBY_2_WUP_INT_DBG_MSK	(1 << 1)

#define CPU_SHUTDOWN_MODE			3
#define CPU_SHUTDOWN_MODE_MASK		3
#define CPU_SHUTDOWN_MODE_OFFSET	4

#ifdef CONFIG_PM_HAS_SECURE
static int sec_hal_fail_cpu0;
module_param(sec_hal_fail_cpu0, int, S_IRUGO | S_IWUSR | S_IWGRP);

static int sec_hal_fail_cpu1;
module_param(sec_hal_fail_cpu1, int, S_IRUGO | S_IWUSR | S_IWGRP);
#endif /*CONFIG_PM_HAS_SECURE*/

static int get_sem_fail_ebusy;
module_param(get_sem_fail_ebusy, int, S_IRUGO | S_IWUSR | S_IWGRP);
static int get_sem_fail_einval;
module_param(get_sem_fail_einval, int, S_IRUGO | S_IWUSR | S_IWGRP);

static int dbg_wakeup_int;
module_param_named(dbg_wakeup_int, dbg_wakeup_int, int,
		S_IRUGO | S_IWUSR | S_IWGRP);

static DEFINE_PER_CPU(unsigned int, sh_sleep_state);

spinlock_t cpuinfo_stats_notify_lock;

#ifdef CONFIG_PM_DEBUG
static bool is_enable_cpuidle = true; /* Status of CPU's idle PM */
static DEFINE_SPINLOCK(cpuidle_debug_lock);
#endif

/*
 * ********************************************************************
 *     Drivers interface
 * ********************************************************************
 */
static inline void pm_state_update(unsigned int pm_state)
{
	this_cpu_write(sh_sleep_state, pm_state);
}

#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
static int pm_state_enter_notify(unsigned int pm_state)
{
	pm_state_update(pm_state);
	return cpu_pm_enter();
}

static int pm_state_exit_notify(unsigned int pm_state)
{
	pm_state_update(pm_state);
	return cpu_pm_exit();
}
#endif /* CONFIG_U2_SYS_CPU_DORMANT_MODE */

void debug_log_wakeup_int(int mask)
{
	int i;
	char gic_pending[100];
	char gic_active[100];
	int len1 = 0;
	int len2 = 0;
	void __iomem *base1, *base2;

	if (likely(!(dbg_wakeup_int & mask)))
		return;

	base1 = GIC_DIST_BASE + GIC_DIST_PENDING_SET;
	base2 = GIC_DIST_BASE + GIC_DIST_ACTIVE_SET;

	memset(gic_pending, 0, sizeof(gic_pending));
	memset(gic_active, 0, sizeof(gic_active));

	for (i = 0; i < 7; i++) {
		len1 += snprintf(gic_pending + len1, 10, "%x ",
				__raw_readl(base1 + i * 4));

		len2 += snprintf(gic_active + len2, 10, "%x ",
				__raw_readl(base2 + i * 4));
	}

	pr_info("GIC PENDING[0 - 7]: %s\n", gic_pending);
	pr_info("GIC ACTIVE[0 - 7]: %s\n", gic_active);
	pr_info("WUPSFAC: %x\n", __raw_readl(WUPSFAC));
}

/*
 * ********************************************************************
 *     Drivers interface end.
 * ********************************************************************
 */
static int shmobile_enter_wfi(struct cpuidle_device *dev,
	struct cpuidle_driver *drv, int index);
#ifdef CONFIG_U2_SYS_CPU_WFI_LOWFREQ
static int shmobile_enter_wfi_lowfreq(struct cpuidle_device *dev,
	struct cpuidle_driver *drv, int index);
#endif

#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
static int shmobile_enter_corestandby(struct cpuidle_device *dev,
	struct cpuidle_driver *drv, int index);
static int shmobile_enter_corestandby_2(struct cpuidle_device *dev,
	struct cpuidle_driver *drv, int index);
#endif

static DEFINE_PER_CPU(struct cpuidle_device, shmobile_cpuidle_dev);

/* Available C-states */
enum {
	CSTATE_WFI,
#ifdef CONFIG_U2_SYS_CPU_WFI_LOWFREQ
	CSTATE_WFI_LOWFREQ,
#endif /* CONFIG_U2_SYS_CPU_WFI_LOWFREQ */
#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
	CSTATE_CORESTANDBY,
	CSTATE_CORESTANDBY_2,
#endif /* CONFIG_U2_SYS_CPU_DORMANT_MODE */
	CSTATE_U2_MAX,
};

struct cpuidle_driver shmobile_cpuidle_driver = {
	.name = "shmobile_cpuidle",
	.owner = THIS_MODULE,
	.states = {
		[CSTATE_WFI] = {
			.enter			= shmobile_enter_wfi,
			.exit_latency		= 1,
			.target_residency	= 1,
			.flags			= CPUIDLE_FLAG_TIME_VALID,
			.name			= "WFI",
			.desc			= "Wait for interrupt",
		},
#ifdef CONFIG_U2_SYS_CPU_WFI_LOWFREQ
		[CSTATE_WFI_LOWFREQ] = {
			.enter			= shmobile_enter_wfi_lowfreq,
			.exit_latency		= 100,
			.target_residency	= 1,
			.flags			= CPUIDLE_FLAG_TIME_VALID,
			.name			= "WFI(low-freq)",
			.desc			= "Wait for interrupt(lowfreq)",
		},
#endif /* CONFIG_U2_SYS_CPU_WFI_LOWFREQ */
#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
		[CSTATE_CORESTANDBY] = {
			.enter			= shmobile_enter_corestandby,
			.exit_latency		= 300,
			.target_residency	= 500,
			.flags			= CPUIDLE_FLAG_TIME_VALID,
			.name			= "CoreStandby",
			.desc			= "Core Standby",
		},
		[CSTATE_CORESTANDBY_2] = {
			.enter			= shmobile_enter_corestandby_2,
			.exit_latency		= 400,
			.target_residency	= 600,
			.flags			= CPUIDLE_FLAG_TIME_VALID,
			.name			= "CoreStandby_2",
			.desc			= "Core Standby 2",
		},
#endif /* CONFIG_U2_SYS_CPU_DORMANT_MODE */
	},
	.state_count = CSTATE_U2_MAX,
};

static void disable_dormant_cstates(struct cpuidle_driver *drv, bool val)
{
	int cstate;

	for (cstate = CSTATE_WFI + 1; cstate < CSTATE_U2_MAX; cstate++)
		drv->states[cstate].disabled = val;
}

/*
 * shmobile_enter_wfi: executes idle PM for a CPU - WFI state
 * @dev: cpuidle device for this cpu
 * @drv: cpuidle driver for this cpu
 * @index: index into drv->states of the state to enter
 * return:
 *		int: index into drv->states of the state to exit
 */
static int shmobile_enter_wfi(struct cpuidle_device *dev,
	struct cpuidle_driver *drv, int index)
{
	/* Transition to WFI setting */
	pm_state_update(PM_STATE_NOTIFY_SLEEP);
	memory_log_func(PM_FUNC_ID_START_WFI, 1);
	start_wfi();
	memory_log_func(PM_FUNC_ID_START_WFI, 0);
	pm_state_update(PM_STATE_NOTIFY_WAKEUP);

	local_fiq_enable();

	return CSTATE_WFI;
}

#ifdef CONFIG_U2_SYS_CPU_WFI_LOWFREQ
/*
 * shmobile_enter_wfi_lowfreq: executes idle PM for a CPU - WFI(low-freq) state
 * @dev: cpuidle device for this cpu
 * @drv: cpuidle driver for this cpu
 * @index: index into drv->states of the state to enter
 * return:
 *		int: index into drv->states of the state to exit
 */
static int shmobile_enter_wfi_lowfreq(struct cpuidle_device *dev,
	struct cpuidle_driver *drv, int index)
{
	/* Transition to WFI standby with low-frequency setting	*/
	pm_state_update(PM_STATE_NOTIFY_SLEEP_LOWFREQ);
	memory_log_func(PM_FUNC_ID_START_WFI2, 1);
	start_wfi2();
	memory_log_func(PM_FUNC_ID_START_WFI2, 0);
	pm_state_update(PM_STATE_NOTIFY_WAKEUP);

	local_fiq_enable();

	return CSTATE_WFI_LOWFREQ;
}
#endif

#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
/*
 * shmobile_enter_corestandby: executes idle PM for a CPU - Corestandby state
 * @dev: cpuidle device for this cpu
 * @drv: cpuidle driver for this cpu
 * @index: index into drv->states of the state to enter
 * return:
 *		int: index into drv->states of the state to exit
 */
static int shmobile_enter_corestandby(struct cpuidle_device *dev,
	struct cpuidle_driver *drv, int index)
{

	if (index != CSTATE_CORESTANDBY_2)
		if (is_hotplug_in_progress() ||
			pm_state_enter_notify(PM_STATE_NOTIFY_CORESTANDBY))
			return shmobile_enter_wfi(dev, drv, index);

	memory_log_func(PM_FUNC_ID_START_CORESTANDBY, 1);
	start_corestandby(); /* CoreStandby(A1SL0 or A1SL1 Off) */
	memory_log_func(PM_FUNC_ID_START_CORESTANDBY, 0);

	debug_log_wakeup_int(CSTANDBY_WUP_INT_DBG_MSK);
	pm_state_exit_notify(PM_STATE_NOTIFY_WAKEUP);

	local_fiq_enable();

	return CSTATE_CORESTANDBY;
}

/*
 * check_peripheral_module_status
 *
 * return:
 *		0	: peripheral module is no busy.
 *		-EBUSY	: peripheral module is busy.
 */
static int check_peripheral_module_status(void)
{
	if ((__raw_readl(MSTPSR1) & MSTPST1_PLL1) != MSTPST1_PLL1)
		return -EBUSY;

	if ((__raw_readl(MSTPSR2) & MSTPST2_PLL1) != MSTPST2_PLL1)
		return -EBUSY;

	if ((__raw_readl(MSTPSR3) & MSTPST3_PLL1) != MSTPST3_PLL1)
		return -EBUSY;

	if ((__raw_readl(MSTPSR4) & MSTPST4_PLL1) != MSTPST4_PLL1)
		return -EBUSY;

	return 0;
}

#if (defined ZB3_CLK_IDLE_ENABLE) && (defined ZB3_CLK_DFS_ENABLE)
static unsigned int ddr_freq_save(void)
{
	unsigned int ddr_freq;

	/* RT domain(A3R) is not off */
	if ((check_peripheral_module_status()) ||
	((__raw_readl(PSTR) & (POWER_A3R)) && (sh_mobile_vsync_status() == 1)))
		return 0;

	ddr_freq = suspend_ZB3_backup();
	if (!ddr_freq) {
		pr_info("[%s]: Backup ZB3 clocks FAILED\n", __func__);
		return 0;
	}

	if (ddr_freq == ZB3_CLK_EARLY_SUSPEND)
		return 0;

	if (cpg_set_sbsc_freq(ZB3_CLK_EARLY_SUSPEND) < 0)
		pr_info("[%s]: Set ZB3 clocks FAILED\n", __func__);

	return ddr_freq;
}

static void ddr_freq_restore(unsigned int ddr_freq)
{
	if (!ddr_freq)
		return;

	if (cpg_set_sbsc_freq(ddr_freq) < 0)
		pr_info("[%s]: Restore ZB3 clocks FAILED\n", __func__);
}
#else
static inline unsigned int ddr_freq_save(void) { return 0; }
static inline void ddr_freq_restore(unsigned int ddr_freq) { }
#endif /*(defined ZB3_CLK_IDLE_ENABLE) && (defined ZB3_CLK_DFS_ENABLE)*/

static bool non_boot_cpus_online(void)
{
	unsigned int cpu, cpu_status, cpu_pwds;

	for_each_possible_cpu(cpu) {
		if (!cpu)
			continue;

		cpu_status = __raw_readl(ram0Cpu0Status + (0x4 * cpu));
		cpu_pwds = (__raw_readl(SCPUSTR) >>
					(CPU_SHUTDOWN_MODE_OFFSET * cpu))
					& CPU_SHUTDOWN_MODE_MASK;

		if (cpu_status != CPUSTATUS_HOTPLUG ||
				cpu_pwds != CPU_SHUTDOWN_MODE)
			return true;
	}

	return false;
}

/*
 * shmobile_enter_corestandby_2: executes idle PM for a CPU - Corestandby state
 * @dev: cpuidle device for this cpu
 * @drv: cpuidle driver for this cpu
 * @index: index into drv->states of the state to enter
 * return:
 *		int: index into drv->states of the state to exit
 */
static int shmobile_enter_corestandby_2(struct cpuidle_device *dev,
	struct cpuidle_driver *drv, int index)
{
	unsigned int ddr_freq;

	if (is_hotplug_in_progress())
		return shmobile_enter_wfi(dev, drv, index);

	if ((dev->cpu != 0) || non_boot_cpus_online())
		return shmobile_enter_corestandby(dev, drv, CSTATE_CORESTANDBY);

	if (pm_state_enter_notify(PM_STATE_NOTIFY_CORESTANDBY_2))
		return shmobile_enter_wfi(dev, drv, index);

	if (cpu_cluster_pm_enter()) {
		pm_state_update(PM_STATE_NOTIFY_CORESTANDBY);
		return shmobile_enter_corestandby(dev, drv, index);
	}

	ddr_freq = ddr_freq_save();

	memory_log_func(PM_FUNC_ID_START_CORESTANDBY2, 1);
	start_corestandby_2(); /* CoreStandby(A2SL Off) */
	memory_log_func(PM_FUNC_ID_START_CORESTANDBY2, 0);

	debug_log_wakeup_int(CSTANDBY_2_WUP_INT_DBG_MSK);
	ddr_freq_restore(ddr_freq);

	cpu_cluster_pm_exit();
	pm_state_exit_notify(PM_STATE_NOTIFY_WAKEUP);

	local_fiq_enable();

	return CSTATE_CORESTANDBY_2;
}
#endif /* CONFIG_U2_SYS_CPU_DORMANT_MODE */

#ifdef CONFIG_PM_DEBUG
/*
 * control_cpuidle: Enable/Disable of CPU's idle PM
 * @is_enable: input value to control Enable/Disable
 *			false: Disable, true: Enable
 */
int control_cpuidle(int is_enable)
{
	unsigned long flags;
	struct cpuidle_driver *drv = &shmobile_cpuidle_driver;

	is_enable = !!is_enable;

	spin_lock_irqsave(&cpuidle_debug_lock, flags);

	if (is_enable_cpuidle != is_enable) {
		disable_dormant_cstates(drv, !is_enable);
		is_enable_cpuidle = is_enable;
	}

	spin_unlock_irqrestore(&cpuidle_debug_lock, flags);

	return 0;
}
EXPORT_SYMBOL(control_cpuidle);

/*
 * is_cpuidle_enable: Status of CPU's idle PM
 * return:
 *		0: Disable
 *		1: Enable
 */
int is_cpuidle_enable(void)
{
	return is_enable_cpuidle;
}
EXPORT_SYMBOL(is_cpuidle_enable);
#endif

/*
 * sh_get_sleep_state: Current PM state
 * return: Current PM state
 */
unsigned int sh_get_sleep_state(void)
{
	return __this_cpu_read(sh_sleep_state);
}

/*
 * rmobile_cpuidle_init: Initialization of CPU's idle PM
 * return:
 *		0: successful
 *		-EIO: failed ioremap, or failed registering a CPU's idle PM
 */
static int __init rmobile_cpuidle_init(void)
{
	struct cpuidle_device *dev;
	struct cpuidle_driver *drv = &shmobile_cpuidle_driver;
	unsigned int cpu;
	int ret;

	/* Unable to setup icram for pm ops? */
	BUG_ON(!icram_pm_setup);

	ret = cpuidle_register_driver(drv);
	if (ret) {
		pr_err("%s: driver registration failed\n", __func__);
		return ret;
	}

	for_each_possible_cpu(cpu) {
		dev = &per_cpu(shmobile_cpuidle_dev, cpu);
		dev->cpu = cpu;
		dev->state_count = drv->state_count;
#ifdef CONFIG_PM_BOOT_SYSFS
		is_enable_cpuidle = false;
		/* Make sure that only WFI state is running */
		disable_dormant_cstates(drv, true);
#endif
		ret = cpuidle_register_device(dev);
		if (ret) {
			pr_err("%s:[CPU%u] device registration failed\n",
					__func__, cpu);
			return ret;
		}
	}

	/* - set the legacy mode to LPCKCR */
	__raw_writel(LPCKCR_LEGACY, LPCKCR);
	/* - set PLL0 stop conditon to A2SL state by CPG.PLL0STPCR */
	__raw_writel(A2SLSTP, PLL0STPCR);

	/* - set Wake up factor unmask to GIC.CPU0 by SYS.WUPSMSK */
	__raw_writel((__raw_readl(WUPSMSK) &  ~(1 << 28)), WUPSMSK);

	return 0;
}
device_initcall(rmobile_cpuidle_init);
