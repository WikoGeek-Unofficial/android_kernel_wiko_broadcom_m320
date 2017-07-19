/*********************************************************************
 * *  arch/arm/mach-shmobile/force_sleep.c
 * *  PM force sleep implementation
 * *
 * *  Copyright 2013 Broadcom Corporation
 * *
 * *  Unless you and Broadcom execute a separate written software license
 * *  agreement governing use of this software, this software is licensed
 * *  to you under the terms of the GNU
 * *  General Public License version 2 (the GPL), available at
 * *  http://www.broadcom.com/licenses/GPLv2.php with the following added
 * *  to such license:
 * *  As a special exception, the copyright holders of this software give
 * *  you permission to link this software with independent modules, and
 * *  to copy and distribute the resulting executable under terms of your
 * *  choice, provided that you also meet, for each linked independent module,
 * *  the terms and conditions of the license of that module. An independent
 * *  module is a module which is not derived from this software.  The special
 * *  exception does not apply to any modifications of the software.
 * *  Notwithstanding the above, under no circumstances may you combine this
 * *  software in any way with any other Broadcom software provided under a
 * *  license other than the GPL, without Broadcom's express prior written
 * *  consent.
 * ***********************************************************************/

#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/suspend.h>
#include <mach/pm.h>
#include <asm/system.h>
#include <linux/delay.h>
#include <linux/wakelock.h>
#include <memlog/memlog.h>
#include "pmRegisterDef.h"

#define PDC_PWR_DWN_INTERVAL_US		10
#define PDC_PWR_DWN_RETRIES		50

struct pm_force_sleep {
	int dummy;
};

#define __param_check_pm_force_sleep(name, p, type) \
	static inline struct type *__check_##name(void) { return (p); }

#define param_check_pm_force_sleep(name, p) \
	__param_check_pm_force_sleep(name, p, pm_force_sleep)

static int param_set_pm_force_sleep(const char *val,
		const struct kernel_param *kp);
static struct kernel_param_ops param_ops_pm_force_sleep = {
	.set = param_set_pm_force_sleep,
};

static struct pm_force_sleep pm_force_sleep;
module_param_named(pm_force_sleep, pm_force_sleep, pm_force_sleep,
		S_IWUSR | S_IWGRP);


struct pm_force_sleep_init {
	int dummy;
};

#define __param_check_pm_force_sleep_init(name, p, type) \
	static inline struct type *__check_##name(void) { return (p); }

#define param_check_pm_force_sleep_init(name, p) \
	__param_check_pm_force_sleep_init(name, p, pm_force_sleep_init)

static int param_set_pm_force_sleep_init(const char *val,
		const struct kernel_param *kp);
static struct kernel_param_ops param_ops_pm_force_sleep_init = {
	.set = param_set_pm_force_sleep_init,
};

static struct pm_force_sleep_init pm_force_sleep_init;
module_param_named(pm_force_sleep_init, pm_force_sleep_init,
		pm_force_sleep_init, S_IWUSR | S_IWGRP);

static int force_sleep_en;
static int force_sleep_no_wakeup = 1;
static int wakelock_init;
static int pm_notifier_init;

static struct wake_lock force_sleep_lock;

static int power_domain_disable(unsigned int area)
{
	u32 reg_val, spdcr;
	int retries = PDC_PWR_DWN_RETRIES;
	int spdcr_retries = 10000;

	memory_log_dump_int(PM_DUMP_ID_FSLEEP_PD_DISABLE,
			(int)area);

	BUG_ON(!(area & POWER_ALL));

	pr_err("%s: area: %d\n", __func__, area);

	/**
	 * Any power area still powering down??
	 * Wait for it to complete
	 */
	do {
		reg_val = __raw_readl(SPDCR);
		udelay(PDC_PWR_DWN_INTERVAL_US);
	} while ((reg_val != 0) && retries--);

	BUG_ON(retries == 0);

	/**
	 * Request Power down
	 */
	__raw_writel(area, SPDCR);

	retries = PDC_PWR_DWN_RETRIES;
	do {
		udelay(PDC_PWR_DWN_INTERVAL_US);
		reg_val = __raw_readl(PSTR);

		pr_err("%s: PSTR: %x\n", __func__, reg_val);

		if (!(reg_val & area))
			break;

		spdcr_retries = 10000;

		/**
		 * Write to SPDCR each time
		 */
		do {
			__raw_writel(area, SPDCR);
			spdcr = __raw_readl(SPDCR);
		} while (spdcr && spdcr_retries--);

	} while ((reg_val & area) && retries--);

	BUG_ON(retries == 0);

	return 0;
}
/**
 * Forcefully power down any active domain
 * Power down sequence:
 * A3R --> A4RM --> A4MP --> A4LC --> A3SP --> A3SG
 */
void pm_fsleep_disable_pds(void)
{
	u32 reg_val;

	reg_val = __raw_readl(PSTR);

	pr_err("%s++: PSTR: 0x%x\n", __func__, reg_val);

	memory_log_dump_int(PM_DUMP_ID_FSLEEP_PSTR, (int)reg_val);

	if (reg_val & POWER_A3R)
		power_domain_disable(POWER_A3R);
	if (reg_val & POWER_A4RM)
		power_domain_disable(POWER_A4RM);
	if (reg_val & POWER_A4MP)
		power_domain_disable(POWER_A4MP);
	if (reg_val & POWER_A4LC)
		power_domain_disable(POWER_A4LC);

	if (reg_val & POWER_A3SP)
		(void)sec_hal_pm_a3sp_state_request(false);

	if (reg_val & POWER_A3SG)
		power_domain_disable(POWER_A3SG);
}

static int fsleep_pm_callback(struct notifier_block *nb,
		unsigned long action, void *ptr)
{
	pr_info("%s: action: %d\n", __func__, (int)action);

	if (action == PM_SUSPEND_PREPARE)
		suspend_cpufreq_hlg_work();

	return 0;
}

static int param_set_pm_force_sleep(const char *val,
		const struct kernel_param *kp)
{
	int state;

	pr_err("%s..\n", __func__);
	sscanf(val, "%d", &state);

	if (state != PM_SUSPEND_MEM)
		return -EINVAL;

	wake_unlock(&force_sleep_lock);

	return 0;
}

static int param_set_pm_force_sleep_init(const char *val,
		const struct kernel_param *kp)
{
	int init;

	pr_err("%s..\n", __func__);

	sscanf(val, "%d", &init);

	if (init != 1)
		return 0;

	if (!wakelock_init) {
		wake_lock_init(&force_sleep_lock, WAKE_LOCK_SUSPEND,
				"pm_force_sleep");
		wakelock_init = 1;
	}

	if (!pm_notifier_init) {
		pm_notifier(fsleep_pm_callback, 0);
		pm_notifier_init = 1;
	}

	wake_lock(&force_sleep_lock);
	force_sleep_en = 1;

	return 0;
}

int pm_is_force_sleep(void)
{
	return force_sleep_en;
}
EXPORT_SYMBOL(pm_is_force_sleep);

int pm_fsleep_no_wakeup(void)
{
	return force_sleep_no_wakeup;
}

