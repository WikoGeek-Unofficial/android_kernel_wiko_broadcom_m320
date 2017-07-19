/*
 * SMP support for R-Mobile / SH-Mobile - r8a7373 portion
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2010  Takashi Yoshii
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
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/sh_clk.h>
#include <linux/of_fdt.h>
#include <mach/common.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/smp_twd.h>
#include <linux/irqchip/arm-gic.h>
#include <mach/pm.h>
#include <linux/delay.h>
#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/cpu.h>
#include <memlog/memlog.h>

#include "pm-r8a7373.h"
#ifndef CONFIG_PM_HAS_SECURE
#include "pm_ram0.h"
#else
#include "pm_ram0_tz.h"
#endif

#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
static bool init_flag = false;
#endif

atomic_t hotplug_active = ATOMIC_INIT(0);

#ifndef CONFIG_U2_SYS_CPU_DORMANT_MODE
static DEFINE_PER_CPU(bool, sec_startup) = true;
#endif

#if defined(CONFIG_OF)
static enum {
	GENERIC_SCU,
	CORTEX_A9_SCU,
} shmobile_dt_scu __initdata = GENERIC_SCU;

static void *shmobile_dt_cortex_a9_scu_base __initdata;

static const char *shmobile_dt_cortex_a9_match[] __initconst = {
	"arm,cortex-a9-scu",
	NULL
};

#ifndef CONFIG_U2_SYS_CPU_DORMANT_MODE
/**
 * Holding pen method to synchronize between cores
 * Secondary cores waits for holding pen to released
 * by primary core to complete the booting process
 */
static void write_pen_release(int val)
{
	pen_release = val;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
#ifdef CONFIG_OUTER_CACHE
	outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));
#endif
}

static void r8a7373_cpu_do_wfi(int cpu)
{
	local_irq_disable();

	for (;;) {
		asm(".word      0xe320f003\n"
				:
				:
				: "memory", "cc");
		if (pen_release == cpu_logical_map(cpu))
			/**
			 * OK. Primary core has released
			 * the pen and now this CPU is
			 * ready to wakeup
			 */
			break;
	}

	local_irq_enable();
}
#endif /* CONFIG_U2_SYS_CPU_DORMANT_MODE */

static int __init shmobile_dt_find_scu(unsigned long node,
		const char *uname, int depth, void *data)
{
	if (of_flat_dt_match(node, shmobile_dt_cortex_a9_match)) {
		phys_addr_t phys_addr;
		__be32 *reg = of_get_flat_dt_prop(node, "reg", NULL);

		if (WARN_ON(!reg))
			return -EINVAL;

		phys_addr = be32_to_cpup(reg);
		shmobile_dt_scu = CORTEX_A9_SCU;

		shmobile_dt_cortex_a9_scu_base = ioremap(phys_addr, SZ_256);
		if (WARN_ON(!shmobile_dt_cortex_a9_scu_base))
			return -EFAULT;
	}
	return 0;
}

void __init shmobile_dt_smp_map_io(void)
{
	if (initial_boot_params)
		WARN_ON(of_scan_flat_dt(shmobile_dt_find_scu, NULL));
}
#endif

#ifdef CONFIG_HAVE_ARM_TWD
static DEFINE_TWD_LOCAL_TIMER(twd_local_timer, 0xF0000600, 29);

void __init r8a7373_register_twd(void)
{
	twd_local_timer_register(&twd_local_timer);
}
#endif

void __cpuinit r8a7373_secondary_init(unsigned int cpu)
{
#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
	if (init_flag) {
		atomic_set(&hotplug_active, 0);
	} else {
		init_flag = true;
	}
#else
	write_pen_release(-1);
#endif
}

int __cpuinit r8a7373_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long status;
#ifndef CONFIG_U2_SYS_CPU_DORMANT_MODE
	unsigned long timeout;
#endif
	cpu = cpu_logical_map(cpu);

#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
	if (init_flag)
		atomic_set(&hotplug_active, 1);

	status = (__raw_readl(SCPUSTR) >> (4 * cpu)) & 3;
	if (status == 3) {
		__raw_writel(CPUSTATUS_RUN, ram0Cpu0Status + (0x4 * cpu));
		__raw_writel(1 << cpu, WUPCR); /* wake up */
	} else if (status == 0) {
		if (!shmobile_is_u2b()) {
			printk(KERN_NOTICE "CPU%d is SRESETed\n", cpu);
			__raw_writel(CPUSTATUS_RUN,
					ram0Cpu0Status + (0x4 * cpu));
			__raw_writel(1 << cpu, SRESCR); /* reset */
		}
	} else {
		printk(KERN_NOTICE "CPU%d has illegal status %08lx\n",\
				cpu, status);
		__raw_writel(1 << cpu, WUPCR); /* wake up */
		if (!shmobile_is_u2b())
			__raw_writel(1 << cpu, SRESCR); /* reset */
	}
#else
	if (per_cpu(sec_startup, cpu)) {
		per_cpu(sec_startup, cpu) = false;
		status = (__raw_readl(SCPUSTR) >> (4 * cpu)) & 3;
		if (status == 3)
			__raw_writel(CPUSTATUS_RUN,
					ram0Cpu0Status + (0x4 * cpu));
			__raw_writel(1 << cpu, WUPCR); /* wake up */
	}
	/*
	 * release the holding pen to boot secondary
	 */
	write_pen_release(cpu_logical_map(cpu));

	/**
	 * if secondary core is in simple WFI, send
	 * WAKEUP IPI and for secondary core to complete
	 * the boot
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));
	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		smp_rmb();
		if (pen_release == -1)
			break;
		udelay(10);
	}
#endif
	return 0;
}

void __init r8a7373_smp_prepare_cpus(unsigned int max_cpus)
{
	__raw_writel(0, SBAR2);

	/* Map the reset vector (in headsmp-scu.S) */
	__raw_writel(0, APARMBAREA);      /* 4k */

	switch (shmobile_dt_scu) {
	case GENERIC_SCU:
		__raw_writel(virt_to_phys(shmobile_secondary_vector), SBAR);
		/* BAREN */
		__raw_writel((__raw_readl(SBAR) | 0x10), SBAR);
		break;
	case CORTEX_A9_SCU:
		__raw_writel(virt_to_phys(shmobile_secondary_vector_scu), SBAR);
		scu_enable(shmobile_dt_cortex_a9_scu_base);
		/* enable cache coherency on booting CPU */
		scu_power_mode(shmobile_dt_cortex_a9_scu_base, SCU_PM_NORMAL);
		break;
	default:
		WARN_ON(1);
		break;
	}
}

#ifdef CONFIG_HOTPLUG_CPU
static int r8a7373_cpu_kill(unsigned int cpu)
{
#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
	unsigned long status;
	int timeout = 1000;
#endif

	/* If panic is in progress, do NOT kill cpu */
	u8 reg = __raw_readb(STBCHR2);
	if (reg & APE_RESETLOG_PANIC_START)
		return 1;

	/* skip powerdown check */
	if ((system_state == SYSTEM_RESTART) ||
		(system_state == SYSTEM_HALT) ||
		(system_state == SYSTEM_POWER_OFF))
		return 1;

	cpu = cpu_logical_map(cpu);

#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE

	while (0 < timeout) {
		timeout--;
		status = __raw_readl(SCPUSTR);
		if (((status >> (4 * cpu)) & 2) == 2) {
			pr_debug("CPUSTR:0x%08lx\n", status);
			return 1;
		}
		mdelay(1);
	}
	panic("timeout r8a7373_cpu_kill block\n");
#endif
	return 1;
}


static void __ref r8a7373_cpu_die(unsigned int cpu)
{
	u8 reg;

	pr_debug("smp-r8a7373: smp_cpu_die is called\n");
	/* hardware shutdown code running on the CPU that is being offlined */
	flush_cache_all();
	dsb();

	/*
	 * Don't believe this is necessary (if it was, wouldn't the GIC driver
	 * do it?). Remove to avoid the need to worry about differing GIC
	 * addresses.
	 */
#if 0
	/* disable gic cpu_if */
	__raw_writel(0, GIC_CPU_BASE + GIC_CPU_CTRL);
#endif

	/* If PANIC is in progress, do NOT call jump_systemsuspend(); */
	reg = __raw_readb(STBCHR2);
	if (reg & APE_RESETLOG_PANIC_START)
		return;

#ifdef CONFIG_SUSPEND
	/**
	 * if CPU dormant mode is not enabled
	 * put this cpu to WFI state
	 */
#ifdef CONFIG_U2_SYS_CPU_DORMANT_MODE
	memory_log_func(PM_FUNC_ID_HOTPLUG, 1);
	jump_systemsuspend();
	memory_log_func(PM_FUNC_ID_HOTPLUG, 0);
#else
	r8a7373_cpu_do_wfi(cpu);
#endif
#endif
}

static int r8a7373_cpu_disable(unsigned int cpu)
{
	/*
	 * we don't allow CPU 0 to be shutdown (it is still too special
	 * e.g. clock tick interrupts)
	 */
	return cpu == 0 ? -EPERM : 0;
}
#endif

struct smp_operations r8a7373_smp_ops  __initdata = {
	.smp_prepare_cpus	= r8a7373_smp_prepare_cpus,
	.smp_boot_secondary	= r8a7373_boot_secondary,
	.smp_secondary_init	= r8a7373_secondary_init,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= r8a7373_cpu_kill,
	.cpu_die		= r8a7373_cpu_die,
	.cpu_disable		= r8a7373_cpu_disable,
#endif
};
