/**
 * @note To define ARCH_HAS_READ_CURRENT_TIMER,
 *       it will include arch/arm/mach-shmobile/include/mach/timex.h
 */
#include <linux/timex.h>

#include <mach/cmt.h>
#include <mach/r8a7373.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <linux/sh_timer.h>
#include <linux/spinlock_types.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/rmu2_rwdt.h>
#include <linux/clocksource.h>
#include <asm/io.h>
#include <asm/sched_clock.h>
#include <asm/mach/time.h>

static struct delay_timer cmt_delay_timer __read_mostly;

/* CMT-1 Clock */
#define CMCLKE_PHYS	0xe6131000
#define CMCLKE		IO_ADDRESS(0xE6131000)
#define CMSTR0		IO_ADDRESS(0xE6130000)
#define CMCSR0		IO_ADDRESS(0xE6130010)
#define CMCNT0		IO_ADDRESS(0xE6130014)
#define CMCOR0		IO_ADDRESS(0xE6130018)
#define CMSTR6		IO_ADDRESS(0xE6130600)
#define CMCNT6		IO_ADDRESS(0xE6130614)
#define CMCOR6		IO_ADDRESS(0xE6130618)
#define CMCSR6		IO_ADDRESS(0xE6130610)

void cmt10_start(bool clear)
{
	unsigned long flags;

	spin_lock_irqsave(&cmt_lock, flags);
	__raw_writel(__raw_readl(CMCLKE) | (1 << 0), CMCLKE);
	spin_unlock_irqrestore(&cmt_lock, flags);

	/* stop */
	__raw_writel(0, CMSTR0);

	/* setup */
	if (clear)
		__raw_writel(0, CMCNT0);
	__raw_writel(0x103, CMCSR0); /* Free-running, DBGIVD, cp_clk/1 */
	__raw_writel(0xffffffff, CMCOR0);
	while (__raw_readl(CMCSR0) & (1<<13))
		cpu_relax();

	/* start */
	__raw_writel(1, CMSTR0);
}

void cmt10_stop(void)
{
	unsigned long flags;

	__raw_writel(0, CMSTR0);

	spin_lock_irqsave(&cmt_lock, flags);
	__raw_writel(__raw_readl(CMCLKE) & ~(1 << 0), CMCLKE);
	spin_unlock_irqrestore(&cmt_lock, flags);
}

/* do nothing for !CONFIG_SMP or !CONFIG_HAVE_TWD */
void __init __weak r8a7373_register_twd(void) { }


static unsigned long cmt_read_current_timer(void)
{
	return __raw_readl(CMCNT0);
}

static u32 notrace cmt_read_sched_clock(void)
{
	return __raw_readl(CMCNT0);
}

/**
 * r8a7373_read_persistent_clock -  Return time from a persistent clock.
 *
 * Reads the time from a source which isn't disabled during PM,
 * CMCNT6.  Convert the cycles elapsed since last read into
 * nsecs and adds to a monotonically increasing timespec.
 *
 * Copied from plat-omap. overrides weak definition in timekeeping.c
 */
static struct timespec persistent_ts;
static cycles_t cycles, last_cycles;
static unsigned int persistent_mult, persistent_shift;
static void r8a7373_read_persistent_clock(struct timespec *ts)
{
	unsigned long long nsecs;
	cycles_t delta;
	struct timespec *tsp = &persistent_ts;

	last_cycles = cycles;
	cycles = __raw_readl(CMCNT6);
	delta = cycles - last_cycles;

	nsecs = clocksource_cyc2ns(delta, persistent_mult, persistent_shift);

	timespec_add_ns(tsp, nsecs);
	*ts = *tsp;
}

static void __init cmt_clocksource_init(void)
{
	struct clk *cp_clk, *r_clk;
	unsigned long flags, rate;

	if (clk_enable(clk_get_sys("sh_cmt.10", NULL)) < 0) {
		pr_err("sh_cmt.10 clock enable failed\n");
		return;
	}

	cp_clk = clk_get(NULL, "cp_clk");
	rate = clk_get_rate(cp_clk);
	if (clk_enable(cp_clk) < 0) {
		pr_err("cp_clk clock enable failed\n");
		return;
	}

	cmt10_start(true);

	/* Use CMT10 as a MMIO clocksource */
	clocksource_mmio_init(CMCNT0, "cmt10", rate, 125, 32,
				clocksource_mmio_readl_up);

	/* And as the scheduling clock */
	setup_sched_clock(cmt_read_sched_clock, 32, rate);

	/* And as the delay timer */
	cmt_delay_timer.read_current_timer = cmt_read_current_timer;
	cmt_delay_timer.freq = rate;
	register_current_timer_delay(&cmt_delay_timer);

	/*
	 * Use CMT16 as the persistent timer.
	 * SPUV (Audio DSP) firmware also reads this timer.
	 * DO NOT modify CMT16 configurations without checking
	 * the side effects on SPUV.
	 * SPUV does only read access to CMT16 registers.
	 */

	if (clk_enable(clk_get_sys("sh_cmt.16", NULL)) < 0) {
		pr_err("sh_cmt.16 clock enable failed\n");
		return;
	}

	r_clk = clk_get(NULL, "r_clk");
	if (clk_enable(r_clk) < 0) {
		pr_err("r_clk clock enable failed\n");
		return;
	}


	rate = clk_get_rate(r_clk);

	spin_lock_irqsave(&cmt_lock, flags);
	__raw_writel(__raw_readl(CMCLKE) | (1 << 6), CMCLKE);
	spin_unlock_irqrestore(&cmt_lock, flags);

	/* stop */
	__raw_writel(0, CMSTR6);

	/* setup */
	__raw_writel(0, CMCNT6);
	__raw_writel(0x107, CMCSR6); /* Free-running, debug, RCLK/1 */
	__raw_writel(0xffffffff, CMCOR6);
	while (__raw_readl(CMCNT6) != 0)
		;

	/* start */
	__raw_writel(1, CMSTR6);

	/*
	 * 120000 rough estimate from the calculations in
	 * __clocksource_updatefreq_scale.
	 */
	clocks_calc_mult_shift(&persistent_mult, &persistent_shift,
			32768, NSEC_PER_SEC, 120000);

	clocksource_mmio_init(CMCNT6, "cmt16", rate, 50, 32,
					clocksource_mmio_readl_up);

	register_persistent_clock(NULL, r8a7373_read_persistent_clock);
}


static struct cmt_timer_clock cmt1_cks_table[] = {
	[0] = CKS("cp_clk", 8, 512),
	[1] = CKS("cp_clk", 32, 128),
	[2] = CKS("cp_clk", 128, 32),
	[3] = CKS("cp_clk", 1, 4096), /* 0x1000 <=> 315 usecs */
	[4] = CKS("r_clk", 8, 8),
	[5] = CKS("r_clk", 32, 8),
	[6] = CKS("r_clk", 128, 8),
	[7] = CKS("r_clk", 1, 8), /* 0x8 <=> 244 usecs */
	/* Pseudo 32KHz/1 is omitted */
};


/* Per-CPU clockevent timers */
static struct cmt_timer_config cmt1_timers[] = {
	[0] = {
		.res = {
			DEFINE_RES_MEM(0xe6130100, 0x100),
			DEFINE_RES_IRQ(gic_spi(94)),
		},
		.name		= "sh_cmt.11",
		.timer_bit	= 1,
		.cks_table	= cmt1_cks_table,
		.cks_num	= ARRAY_SIZE(cmt1_cks_table),
		.cks		= 3,
		.cmcsr_init	= 0x120, /* Free-run, request interrupt, debug */
	},
	[1] = {
		.res = {
			DEFINE_RES_MEM(0xe6130200, 0x100),
			DEFINE_RES_IRQ(gic_spi(95)),
		},
		.name		= "sh_cmt.12",
		.timer_bit	= 2,
		.cks_table	= cmt1_cks_table,
		.cks_num	= ARRAY_SIZE(cmt1_cks_table),
		.cks		= 3,
		.cmcsr_init	= 0x120, /* Free-run, request interrupt, debug */
	},
	[2] = {
		.res = {
			DEFINE_RES_MEM(0xe6130300, 0x100),
			DEFINE_RES_IRQ(gic_spi(96)),
		},
		.name		= "sh_cmt.13",
		.timer_bit	= 3,
		.cks_table	= cmt1_cks_table,
		.cks_num	= ARRAY_SIZE(cmt1_cks_table),
		.cks		= 3,
		.cmcsr_init	= 0x120,
	},
	[3] = {
		.res = {
			DEFINE_RES_MEM(0xe6130400, 0x100),
			DEFINE_RES_IRQ(gic_spi(97)),
		},
		.name		= "sh_cmt.14",
		.timer_bit	= 4,
		.cks_table	= cmt1_cks_table,
		.cks_num	= ARRAY_SIZE(cmt1_cks_table),
		.cks		= 3,
		.cmcsr_init	= 0x120,
	},
};

void __init r8a7373_timers_init(void)
{
	r8a7373_clock_init();
	cmt_clocksource_init();
	cmt_clockevent_init(cmt1_timers, ARRAY_SIZE(cmt1_timers),
			0, CMCLKE_PHYS);
}
