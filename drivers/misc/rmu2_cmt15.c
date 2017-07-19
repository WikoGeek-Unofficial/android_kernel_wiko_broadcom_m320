 /*
  * rmu2_cmt15.c
  *
  * Copyright (C) 2013 Renesas Mobile Corporation
  *
  * This program is free software; you can redistribute it and/or
  * modify it under the terms of the GNU General Public License
  * version 2 as published by the Free Software Foundation.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program; if not, write to the Free Software
  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
  * MA  02110-1301, USA.
  */

#include <linux/rmu2_cmt15.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <memlog/memlog.h>
#define ISREQ_TIMEOUT_MONITOR_S		IO_ADDRESS(0xE6150440)
#define PDACK_TIMEOUT_MONITOR_A		IO_ADDRESS(0xE615047C)
#define ISREQ_TIMEOUT_MONITOR_F		IO_ADDRESS(0xE6150490)
#define PDACK_TIMEOUT_MONITOR_F		IO_ADDRESS(0xE615049C)

/* Android provides the fiq_glue API, which hasn't yet made it into core
 * Linux. This works better if the glue is available, but it can still work
 * with the basic fiq API, if we need to run on non-Android Linux.
 */
#ifdef CONFIG_FIQ_GLUE
#include <asm/fiq_glue.h>
#else
#include <asm/fiq.h>
#endif

/* Note this option only controls behaviour on non-secure images. In secure
 * images, it's up to the secure code whether the CMT is a FIQ or not. */
#define CONFIG_RMU2_CMT_FIQ
/* CMT-1 Clock */
#define CMCLKE		IO_ADDRESS(0xE6131000)
#define CMSTR15		IO_ADDRESS(0xe6130500)
#define CMCNT15		IO_ADDRESS(0xe6130514)
#define CMCOR15		IO_ADDRESS(0xe6130518)
#define CMCSR15		IO_ADDRESS(0xe6130510)
#define STBCHR2		IO_ADDRESS(0xE6180002)
#define RWTCNT		IO_ADDRESS(0xE6020000)

#define CMT15_SPI		98U

#ifdef CONFIG_RWDT_CMT15_TEST
#include <linux/proc_fs.h>
#include <asm/cacheflush.h>
#include <asm/traps.h>
#include <asm/fiq.h>

static struct proc_dir_entry *proc_watch_entry;

int test_mode;

void rmu2_cmt_loop(void *info)
{
	uint32_t psr = 0;

	if ((unsigned)info & 1)
		local_irq_disable();
	if ((unsigned)info & 2) {
		struct pt_regs r;
		local_fiq_disable();
		/* On a secure system, the above won't disable FIQs. But
		 * setting R13_fiq can stop our secure code delivering us
		 * the watchdog FIQ. */
		get_fiq_regs(&r);
		r.ARM_sp = 0xDEADDEAD;
		set_fiq_regs(&r);
	}

	asm volatile(
		"       mrs     %0, cpsr"
		: "=r" (psr) : : "memory");

	printk(KERN_ALERT "RWDT test loop (CPU %d, preemption %s, IRQs %s, FIQs %s%s)\n",
			raw_smp_processor_id(),
			preempt_count() ? "off" : "on",
			psr & PSR_I_BIT ? "off" : "on",
			psr & PSR_F_BIT ? "off" : "on",
			(unsigned)info & 4 ? ", touches" : "");

	if ((unsigned)info & 4)
		while (1)
			touch_softlockup_watchdog();

	/* Try to avoid sucking power. Note that cpu_do_idle() etc all use
	 * WFI, which won't do - it proceeds if there's a pending masked
	 * interrupt. WFE only proceeds for unmasked interrupts, which is
	 * what we want. */
	while (1)
		wfe();
}

static void loop_processor_vector(const char *name, unsigned long vector)
{
	printk(KERN_EMERG "Overwriting processor %s vector with loop\n", name);

	/* Logic copied from arch/arm/kernel/fiq.c - not sure if
	 * it's correct for all CONFIG variants, but works for ours. */
#ifdef CONFIG_CPU_USE_DOMAINS
	/* Write directly to execution address (why is this okay?)  */
	vector += CONFIG_VECTORS_BASE;
#else
	/* Use address of page in kernel linear mapping (actual
	 * vector page is read-only) */
	vector += (unsigned long) vectors_page;
#endif
#ifdef CONFIG_THUMB2_KERNEL
	*(uint16_t *)vector = 0xE7FE; /* Thumb branch to self */
#else
	*(uint32_t *)vector = 0xEAFFFFFE; /* ARM branch to self */
#endif
#if 0
/* Don't bother cleaning - crashes inside the clean are ugly due to
 *  lack of unwinding, and it will get through eventually anyway.
 */

	/* If we did it through vectors_page, it doesn't need to
	 * actually clean any D-cache, as it's write-through, so the
	 * wrong address for the data doesn't matter. Note that
	 * flush_icache_range() is supposed to work work on all cores -
	 * it does if they're all ARMv7 Inner Shareable, at least.
	 */
	flush_icache_range(CONFIG_VECTORS_BASE,
				CONFIG_VECTORS_BASE+0x20);
#endif
}
#endif

static DEFINE_SPINLOCK(cmt_lock);

#ifndef CONFIG_FIQ_GLUE
static DEFINE_PER_CPU(struct pt_regs, rmu2_cmt_fiq_regs);
#endif

static struct platform_device rmu2_cmt_dev = {
	.name = "cmt15",
	.id = -1,
};

static bool running;

/*
 * cpg_check_init: CPG Check initialization
 * input: none
 * output: none
 * return: none
 */
static void cpg_check_init(void)
{
#ifdef CONFIG_SHBUS_TIMEOUT_CHECK_ENABLE
	__raw_writel(0x3fff3fffU, CPG_CHECK_REG);
	__raw_writel(0x3fff3fffU, CPG_CHECK_REG + 4);
#endif /* CONFIG_SHBUS_TIMEOUT_CHECK_ENABLE */
}

/*
 * cpg_check_check: CPG Check
 * input: none
 * output: none
 * return: none
 */
void cpg_check_check(void)
{
#ifdef CONFIG_SHBUS_TIMEOUT_CHECK_ENABLE
	unsigned int val0;
	unsigned int val1;
	unsigned int addr;

	/* get values */
	val0 = __raw_readl(CPG_CHECK_REG);
	val1 = __raw_readl(CPG_CHECK_REG + 4);

	/* check */
	if ((0U != (val0 & 0x80008000U)) || (0U != (val1 & 0x80008000U))) {
		printk(KERN_EMERG "CPG STSTUS\n");
		printk(KERN_EMERG " %08x=%08x\n", CPG_CHECK_STATUS,
					__raw_readl(CPG_CHECK_STATUS));
		printk(KERN_EMERG " %08x=%08x\n", CPG_CHECK_REG, val0);
		printk(KERN_EMERG " %08x=%08x\n", CPG_CHECK_REG + 4, val1);
		for (addr = ISREQ_TIMEOUT_MONITOR_S;
				addr <= PDACK_TIMEOUT_MONITOR_A; addr += 4U) {
			printk(KERN_EMERG " %08x=%08x\n",
					addr, __raw_readl(addr));
		}
		for (addr = ISREQ_TIMEOUT_MONITOR_F;
				addr <= PDACK_TIMEOUT_MONITOR_F; addr += 4U) {
			printk(KERN_EMERG " %08x=%08x\n",
					addr, __raw_readl(addr));
		}

		printk(KERN_EMERG "Bus timeout occurred!!");
		} else if ((0x3fff3fffU != (val0 & 0x3fff3fffU)) ||
					(0x3fff3fffU != (val1 & 0x3fff3fffU))) {
		printk(KERN_ALERT "CPG CHECK register was modified\n");
		printk(KERN_ALERT "and should be reset\n");
		__raw_writel(0x3fff3fffU, CPG_CHECK_REG);
		__raw_writel(0x3fff3fffU, CPG_CHECK_REG + 4);
	} else {
		/* Do nothing */
	}
#endif /* CONFIG_SHBUS_TIMEOUT_CHECK_ENABLE */
}

/*
 * rmu2_cmt_start: start CMT
 * input: none
 * output: none
 * return: none
 */
static void rmu2_cmt_start(void)
{
	unsigned long flags = 0;
	unsigned long wrflg = 0;
	unsigned long i = 0;
	printk(KERN_INFO "START < %s >\n", __func__);

	spin_lock_irqsave(&cmt_lock, flags);
	__raw_writel(__raw_readl(CMCLKE) | (1<<5), CMCLKE);
	spin_unlock_irqrestore(&cmt_lock, flags);

	mdelay(8);

	__raw_writel(0, CMSTR15);
	__raw_writel(0U, CMCNT15);
	__raw_writel(0x000001a6U, CMCSR15);     /* Int enable */
	__raw_writel(CMT_OVF, CMCOR15);

	do {
		wrflg = ((__raw_readl(CMCSR15) >> 13) & 0x1);
		i++;
	} while (wrflg != 0x00 && i < 0xffffffff);

	__raw_writel(1, CMSTR15);

	printk(KERN_INFO "< %s >CMCLKE=%08x\n", __func__,
					__raw_readl(CMCLKE));
	printk(KERN_INFO "< %s >CMSTR15=%08x\n", __func__,
					__raw_readl(CMSTR15));
	printk(KERN_INFO "< %s >CMCSR15=%08x\n", __func__,
					__raw_readl(CMCSR15));
	printk(KERN_INFO "< %s >CMCNT15=%08x\n", __func__,
					__raw_readl(CMCNT15));
	printk(KERN_INFO "< %s >CMCOR15=%08x\n", __func__,
					__raw_readl(CMCOR15));

	running = true;
}

/*
 * rmu2_cmt_stop: stop CMT
 * input: none
 * output: none
 * return: none
 */
void rmu2_cmt_stop(void)
{
	unsigned long flags = 0;
	unsigned long wrflg = 0;
	unsigned long i = 0;

	running = false;

	__raw_readl(CMCSR15);
	__raw_writel(0x00000186U, CMCSR15);     /* Int disable */
	__raw_writel(0U, CMCNT15);
	__raw_writel(0, CMSTR15);

	do {
		wrflg = ((__raw_readl(CMCSR15) >> 13) & 0x1);
		i++;
	} while (wrflg != 0x00 && i < 0xffffffff);

	mdelay(12);
	spin_lock_irqsave(&cmt_lock, flags);
	__raw_writel(__raw_readl(CMCLKE) & ~(1<<5), CMCLKE);
	spin_unlock_irqrestore(&cmt_lock, flags);
	printk(KERN_INFO "START < %s >\n", __func__);
}

/*
 * rmu2_cmt_clear: CMT counter clear
 * input: none
 * output: none
 * return: none
 */
void rmu2_cmt_clear(void)
{
	int wrflg = 0;
	int i = 0;
	unsigned long flags;

	if (!running)
		return;

#ifdef CONFIG_RWDT_CMT15_TEST
	if (test_mode == TEST_NO_KICK)
		return;
#endif
	spin_lock_irqsave(&cmt_lock, flags);
	__raw_writel(0, CMSTR15);       /* Stop counting */
	__raw_writel(0U, CMCNT15);      /* Clear the count value */

	do {
		wrflg = ((__raw_readl(CMCSR15) >> 13) & 0x1);
		i++;
	} while (wrflg != 0x00 && i < 0xffffffff);
	memory_log_timestamp(CMT15_TIMESTAMP);
	/*
	 * PREEMPT_MASK -> 8, we are currently only interested in logging preempt_count
	 * preempt_count will occupy 0 - 7 bits
	 * pid will start occupy 8 - 31 bits
	 */
	memory_log_dump_int(RWDT_DUMP_ID,
			(preempt_count() & PREEMPT_MASK) |
			(current->pid << PREEMPT_BITS));
	__raw_writel(1, CMSTR15);       /* Enable counting again */
	spin_unlock_irqrestore(&cmt_lock, flags);
	/*printk(KERN_INFO "START < %s >\n", __func__);*/
}

/*
 * rmu2_cmt_irq: IRQ handler for CMT
 * input:
 * @irq: interrupt number
 * @dev_id: device ID
 * output: none
 * return:
 * IRQ_HANDLED: irq handled
 */
static irqreturn_t rmu2_cmt_irq(int irq, void *dev_id)
{
	unsigned int reg_val = __raw_readl(CMCSR15);
#ifdef CONFIG_ARM_TZ
	unsigned char *killer = 0;
	printk(KERN_ERR "TRUST ZONE ENABLED : CMT15 counter overflow occur!\n");
#else

	printk(KERN_ERR "TRUST ZONE DISABLED : CMT15 counter overflow occur!\n");
#endif/* CONFIG_ARM_TZ */

	reg_val &= ~0x0000c000U;
	__raw_writel(reg_val, CMCSR15);

#ifdef CONFIG_ARM_TZ
	printk(KERN_ERR "Watchdog will trigger in 5 sec... Generating panic to collect logs...");
	*killer = 1;
#endif /* CONFIG_ARM_TZ */

	return IRQ_HANDLED;
}

static DEFINE_RAW_SPINLOCK(fiq_lock);
static int fiq_count;

/*
 * rmu2_cmt_fiq: FIQ handler for CMT. Called on all CPUs simultaneously.
 * return: must not return
 */
#ifdef CONFIG_FIQ_GLUE
static void rmu2_cmt_fiq(struct fiq_glue_handler *h, void *r, void *svc_sp)
{
	struct pt_regs *regs = r; /* Layout matches + orig_SPSR in orig_r0 */

	/* If we come in via the FIQ glue, then we are in SVC mode, but running
	 * on a FIQ stack. svc_sp points to the original SVC stack pointer.
	 * Good for reliability of getting here, bad for stack unwinding.
	 * The trick to transplant current_thread_info() copied from
	 * the FIQ debugger.
	 */
#define THREAD_INFO(sp) ((struct thread_info *) \
		((unsigned long)(sp) & ~(THREAD_SIZE - 1)))
	struct thread_info *real_thread_info = THREAD_INFO(svc_sp);
	int err = (int) svc_sp;

	/* This is SMP-safe-ish, at least for the FIQ handler being entered
	 * simultaneously - the bit will end up set, maybe more than once.
	 */
	u8 stbchr2 = __raw_readb(STBCHR2);
	__raw_writeb(stbchr2 | APE_RESETLOG_RWDT_CMT_FIQ, STBCHR2);

	*current_thread_info() = *real_thread_info;
#else
asmlinkage void rmu2_cmt_fiq(struct pt_regs *regs)
{
	/* If we come in through our assembler, we're on the original SVC
	 * mode stack, with nothing extra pushed. Good for unwinding, bad if
	 * the stack was broken. Our assembler does the STBCHR2 write.
	 */
	int err = 0;
#endif
	memory_log_timestamp(FIQ_TIMESTAMP);
	memory_log_dump_int(PM_DUMP_ID_RWDT_COUNTER, __raw_readw(RWTCNT));
	memlog_capture = 0;

	/*
	 * We could be in big trouble if we ever try to claim any spinlocks
	 * that were held at the instant the FIQ triggered. die() makes some
	 * effort to avoid problems with printk(), but try a bit harder to
	 * bust some more locks here.
	 */
	raw_spin_lock(&fiq_lock);
	if (fiq_count++ == 0)
		outer_bust_locks();
	raw_spin_unlock(&fiq_lock);

	console_verbose();

	die("Watchdog FIQ", regs, err);
	panic("Watchdog FIQ");
}

#ifdef CONFIG_FIQ_GLUE
static struct fiq_glue_handler fh = {
	.fiq = rmu2_cmt_fiq
};

#else
static struct fiq_handler fh = {
	.name = "rmu2_rwdt_cmt"
};

/*
 * rmu2_cmt_fiq_reg_setup: Called on each CPU to set up FIQ handler regs
 * input: unused
 * output: none
 * return: none
 */
static void rmu2_cmt_fiq_reg_setup(void *info)
{
	/* NB - secure code may mean handler doesn't see these registers */
	struct pt_regs r;
	get_fiq_regs(&r);
	r.ARM_sp = (unsigned long) this_cpu_ptr(rmu2_cmt_fiq_regs);
	set_fiq_regs(&r);
}
#endif

#ifdef CONFIG_RMU2_CMT_FIQ
/*
 * rmu2_cmt_set_gic_to_fiq: Configure specified GIC interrupt as a FIQ
 * input:
 * @irq: interrupt number
 * output: none
 * return: none
 */
static void rmu2_cmt_set_gic_to_fiq(unsigned int irq)
{
	/*
	 * In a secure system, CMT15 will already be set as a FIQ or not
	 * in the GIC by the secure code. Our non-secure writes will be
	 * ignored.
	 */
	struct device_node *np;
	void __iomem *gic_dist;
	unsigned int val;

	/*
	 * Find the GIC by looking for the root interrupt controller. If/when
	 * we are converted to DT, we should just look for own own (ie CMT15's)
	 * interrupt parent with of_irq_find_parent(our_node).
	 */
	for_each_node_by_name(np, "interrupt-controller") {
		struct device_node *parent;
		if (!of_property_read_bool(np, "interrupt-controller"))
			continue;
		parent = of_irq_find_parent(np);
		if (!parent || parent == np)
			break;
	}

	/*
	 * We assume the root interrupt controller is a GIC. And if
	 * we don't find it, just assume U2, and map its known address.
	 */
	if (np) {
		gic_dist = of_iomap(np, 0);
		of_node_put(np);
	} else {
		gic_dist = ioremap(0xF0001000, 0x1000);
	}

	if (!gic_dist) {
		pr_err("%s:GIC not found\n", __func__);
		return;
	}

	/* Poke the GIC to make CMT15 interrupt a FIQ */
	val = readl_relaxed(gic_dist + GIC_DIST_IGROUP + 4 * (irq / 32));
	val &= ~(1 << (irq % 32));
	writel_relaxed(val, gic_dist + GIC_DIST_IGROUP + 4 * (irq / 32));
	pr_debug("GIC_DIST_IGROUPR%d = %08x\n", irq,
		readl_relaxed(gic_dist + GIC_DIST_IGROUP + 4 * (irq / 32)));

	/*
	 * Poke the GIC to send it to all CPUs.
	 * (Linux irq_set_affinity API will only send it to one)
	 */
	writeb_relaxed(0xff, gic_dist + GIC_DIST_TARGET + irq);
	pr_debug("GIC_DIST_TARGETSR%d = %08x\n", irq,
		readb_relaxed(gic_dist + GIC_DIST_TARGET + irq));

	/* Make sure the interrupt is highest (0) priority */
	writeb_relaxed(0, gic_dist + GIC_DIST_PRI + irq);

	iounmap(gic_dist);
}
#endif

/*
 * rmu2_cmt_init_irq: IRQ initialization handler for CMT
 * input: none
 * output: none
 * return: none
 */
static void rmu2_cmt_init_irq(void)
{
	int ret = 0;
	unsigned int irq;
	printk(KERN_INFO "START < %s >\n", __func__);

	irq = gic_spi(CMT15_SPI);
	set_irq_flags(irq, IRQF_VALID | IRQF_NOAUTOEN);
	ret = request_irq(irq, rmu2_cmt_irq, IRQF_DISABLED,
				"CMT15_RWDT0", (void *)irq);
	if (0 > ret) {
		printk(KERN_ERR "%s:%d request_irq failed err=%d\n",
				__func__, __LINE__, ret);
		free_irq(irq, (void *)irq);
	}
#ifdef CONFIG_RMU2_CMT_FIQ
	rmu2_cmt_set_gic_to_fiq(irq);
#endif

	/* Always claim the FIQ vector - the secure code may
	 * or may not pass through the interrupt as a FIQ...
	 *
	 * Note that we rely on the platform preserving FIQ registers across
	 * power management events.
	 */
#ifdef CONFIG_FIQ_GLUE
	ret = fiq_glue_register_handler(&fh);
	if (0 > ret) {
		printk(KERN_ERR "%s:%d fiq_glue_register_handler failed err=%d\n",
				__func__, __LINE__, ret);
		free_irq(irq, (void *)irq);
		return;
	}

#else
	ret = claim_fiq(&fh);
	if (0 > ret) {
		printk(KERN_ERR "%s:%d claim_fiq failed err=%d\n",
				__func__, __LINE__, ret);
		free_irq(irq, (void *)irq);
		return;
	}

	on_each_cpu(rmu2_cmt_fiq_reg_setup, NULL, true);
	set_fiq_handler(&rmu2_cmt_fiq_handler,
			&rmu2_cmt_fiq_handler_end - &rmu2_cmt_fiq_handler);
#endif

	/* And always set up in case it's an IRQ */
	enable_irq(irq);
}

/*
 * rmu2_cmt15_probe:
 * input:
 * @pdev: platform device
 * output:
 * return:
 */
static int rmu2_cmt_probe(struct platform_device *pdev)
{
	printk(KERN_ALERT "START < %s >\n", __func__);

	cpg_check_init();
	rmu2_cmt_init_irq();
	rmu2_cmt_start();
	return 0;
}

/*
 * rmu2_cmt15_remove:
 * input:
 * @pdev: platform device
 * output:none
 * return:
 * 0:sucessful
 */
static int rmu2_cmt_remove(struct platform_device *pdev)
{
	printk(KERN_ALERT "START < %s >\n", __func__);

	rmu2_cmt_stop();
	return 0;
}

/*
 * rmu2_cmt15_suspend:
 * input:
 * @pdev: platform device
 * @state: power management
 * output:none
 * return:
 * 0:sucessful
 * -EAGAIN: try again
 */
static int rmu2_cmt_suspend_noirq(struct device *dev)
{
	printk(KERN_ALERT "START < %s >\n", __func__);

	rmu2_cmt_clear();
	/* CMT1_5 is stopped just before we go to system suspend */
	return 0;
}

/*
 * rmu2_cmt15_resume:
 * input:
 * @pdev: platform device
 * output:none
 * return:
 * 0:sucessful
 * -EAGAIN: try again
 */
static int rmu2_cmt_resume_noirq(struct device *dev)
{
	printk(KERN_ALERT "START < %s >\n", __func__);

	/* CMT1_5 is start immediately after we resume from system suspend */
	rmu2_cmt_clear();
	return 0;
}

static struct dev_pm_ops rmu2_cmt_driver_pm_ops = {
	.suspend_noirq = rmu2_cmt_suspend_noirq,
	.resume_noirq = rmu2_cmt_resume_noirq,
};

static struct platform_driver rmu2_cmt_driver = {
	.probe		= rmu2_cmt_probe,
	.remove		= rmu2_cmt_remove,
	.driver		= {
		.name	= "cmt15",
		.pm	= &rmu2_cmt_driver_pm_ops,
	}
};

#ifdef CONFIG_RWDT_CMT15_TEST

static int read_proc_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%x\n", test_mode);
	return 0;
}

static int proc_watch_open(struct inode *inode, struct file *file)
{
	return single_open(file, read_proc_show, NULL);
}

static int write_proc(struct file *file, const char __user *buf,
						size_t count, loff_t *pos)
{
	char buffer[4];

	if (count > sizeof buffer)
		return -EFAULT;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	sscanf(buffer, "%u", &test_mode);

	switch (test_mode) {
	case TEST_LOOP:
		rmu2_cmt_loop((void *)0);
		break;
	case TEST_PREEMPT_LOOP:
		preempt_disable();
		rmu2_cmt_loop((void *)0);
		break;
	case TEST_IRQOFF_LOOP:
		rmu2_cmt_loop((void *)1);
		break;
	case TEST_IRQOFF_LOOP_ALL:
		on_each_cpu(rmu2_cmt_loop, (void *)1, false);
		break;
	case TEST_IRQHANDLER_LOOP:
		loop_processor_vector("IRQ", 0x18);
		break;
	case TEST_FIQOFF_LOOP:
		rmu2_cmt_loop((void *)3);
		break;
	case TEST_FIQOFF_1_LOOP_ALL:
		local_fiq_disable();
		on_each_cpu(rmu2_cmt_loop, (void *)1, false);
		break;
	case TEST_FIQOFF_LOOP_ALL:
		on_each_cpu(rmu2_cmt_loop, (void *)3, false);
		break;
	case TEST_TOUCH_LOOP:
		rmu2_cmt_loop((void *)4);
		break;
	case TEST_IRQOFF_TOUCH_LOOP_ALL:
		on_each_cpu(rmu2_cmt_loop, (void *)5, false);
		break;
	}

	return test_mode;
}

static const struct file_operations proc_watch_entry_ops = {
	.open = proc_watch_open,
	.read = seq_read,
	.write = write_proc,
	.release = single_release,
};

static void create_new_proc_entry(void)
{
	proc_watch_entry = proc_create("proc_watch_entry", 0666, NULL,
							&proc_watch_entry_ops);
	if (!proc_watch_entry) {
		printk(KERN_INFO "Error creating proc entry");
		return;
	}
}

#endif

/*
 * init routines
 */
static int __init rmu2_cmt_init(void)
{
	int ret = 0;
	printk(KERN_ALERT "START < %s >\n", __func__);
	ret = platform_device_register(&rmu2_cmt_dev);
	if (0 != ret) {
		printk(KERN_ERR "< %s > platform_device_register() = %d\n",
					__func__, ret);
		return ret;
	}
	ret = platform_driver_register(&rmu2_cmt_driver);
	if (0 > ret) {
		printk(KERN_ERR "< %s > platform_driver_register() = %d\n",
					__func__, ret);
		return ret;
	}

#ifdef CONFIG_RWDT_CMT15_TEST
	create_new_proc_entry();
#endif

	return ret;
}

/*
 * exit routines
 */
static void __exit rmu2_cmt_exit(void)
{
	printk(KERN_ALERT "START < %s >\n", __func__);
	platform_driver_unregister(&rmu2_cmt_driver);
	platform_device_unregister(&rmu2_cmt_dev);
#ifdef CONFIG_RWDT_CMT15_TEST
	remove_proc_entry("proc_watch_entry", NULL);
#endif
}

subsys_initcall(rmu2_cmt_init);
module_exit(rmu2_cmt_exit);

MODULE_AUTHOR("Renesas Mobile Corp.");
MODULE_DESCRIPTION("CMT15 driver for R-Mobile-U2");
MODULE_LICENSE("GPL");
