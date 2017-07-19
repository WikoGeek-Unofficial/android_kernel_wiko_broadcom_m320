/*
 * r8a7373 processor support
 *
 * Copyright (C) 2012  Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_data/rmobile_hwsem.h>
#include <linux/platform_data/irq-renesas-irqc.h>
#include <linux/platform_data/sh_cpufreq.h>
#include <linux/platform_device.h>
#include <linux/i2c/i2c-sh_mobile.h>
#include <linux/i2c-gpio.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/memblock.h>
#include <linux/mmc/renesas_mmcif.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/system_info.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach/arch.h>
#include <linux/memblock.h>
#include <mach/common.h>
#include <mach/gpio.h>
#include <mach/irqs.h>
#include <mach/r8a7373.h>
#include <mach/serial.h>
#include <mach/memory.h>
#include <mach/memory-r8a7373.h>
#include <mach/common.h>
#include <mach/pm.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <mach/setup-u2audio.h>
#include <mach/setup-u2ion.h>
#include <mach/setup-u2sdhi.h>
#ifdef CONFIG_SH_RAMDUMP
#include <mach/ramdump.h>
#endif
#if defined CONFIG_CPU_IDLE || defined CONFIG_SUSPEND
#ifndef CONFIG_PM_HAS_SECURE
#include "pm_ram0.h"
#else /*CONFIG_PM_HAS_SECURE*/
#include "pm_ram0_tz.h"
#endif /*CONFIG_PM_HAS_SECURE*/
#endif

#ifdef CONFIG_RENESAS_BT
#include <mach/dev-renesas-bt.h>
#endif

#include <mach/board-bcm4334-bt.h>

#include <linux/regulator/consumer.h>
#include <mach/setup-u2usb.h>
#include <mach/sbsc.h>
#include <linux/spi/sh_msiof.h>
#include <linux/tpu_pwm_board.h>
#include <video/sh_mobile_lcdc.h>
#include <linux/mmc/host.h>

#include "setup-u2sci.h"
#include "sh-pfc.h"

#define LDO_3V 0
#define LDO_1V8 1

#ifdef CONFIG_ARCH_SHMOBILE
void __iomem *dummy_write_mem;
#endif

static unsigned int shmobile_revision;

static int board_rev_proc_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%x", system_rev);

	return 0;
}

static int board_rev_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, board_rev_proc_show, NULL);
}

const struct file_operations board_rev_ops = {
	.open = board_rev_proc_open,
	.read = seq_read,
	.release = single_release,
};

static struct map_desc common_io_desc[] __initdata = {
#ifdef PM_FUNCTION_START
/* We arrange for some of ICRAM 0 to be MT_MEMORY_NONCACHED, so
 * it can be executed from, for the PM code; it is then Normal Uncached memory,
 * with the XN (eXecute Never) bit clear. However, the data area of the ICRAM
 * has to be MT_DEVICE, to satisfy data access size requirements of the ICRAM.
 */
	{
		.virtual	= __IO_ADDRESS(0xe6000000),
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= PM_FUNCTION_START-0xe6000000,
		.type		= MT_DEVICE
	},
	{
		.virtual	= __IO_ADDRESS(PM_FUNCTION_START),
		.pfn		= __phys_to_pfn(PM_FUNCTION_START),
		.length		= PM_FUNCTION_END-PM_FUNCTION_START,
		.type		= MT_MEMORY_NONCACHED
	},
	{
		.virtual	= __IO_ADDRESS(PM_FUNCTION_END),
		.pfn		= __phys_to_pfn(PM_FUNCTION_END),
		.length		= 0xe7000000-PM_FUNCTION_END,
		.type		= MT_DEVICE
	},
#else
	{
		.virtual	= __IO_ADDRESS(0xe6000000),
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	},
#endif
};

static struct map_desc r8a7373_io_desc[] __initdata = {
	{
		.virtual	= __IO_ADDRESS(0xf0000000),
		.pfn		= __phys_to_pfn(0xf0000000),
		.length		= SZ_2M,
		.type		= MT_DEVICE
	},
};

void __init r8a7373_map_io(void)
{
	debug_ll_io_init();
	iotable_init(common_io_desc, ARRAY_SIZE(common_io_desc));
	if (shmobile_is_u2())
		iotable_init(r8a7373_io_desc, ARRAY_SIZE(r8a7373_io_desc));

	shmobile_dt_smp_map_io();
}

#if defined(CONFIG_CMA) && defined(CONFIG_MOBICORE_API)
static u64 rt_dmamask = DMA_BIT_MASK(32);
static struct platform_device rt_cma_device = {
	.name = "rt_cma_dev",
	.id = 0,
	.dev = {
		.dma_mask = &rt_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources = 0,
};
#endif

static struct renesas_irqc_config irqc0_data = {
	.irq_base = irq_pin(0), /* IRQ0 -> IRQ31 */
};

static struct renesas_irqc_config irqc1_data = {
	.irq_base = irq_pin(32), /* IRQ32 -> IRQ63 */
};

static struct renesas_irqc_config irqc10_data = {
	.irq_base = rt_irq(0),
};

static struct renesas_irqc_config irqc12_data = {
	.irq_base = modem_irq(0),
};

static struct sh_plat_hp_data cpufreq_hp_data = {
	.in_samples = 3,
	.out_samples = 10,
	.es_samples = 2,
	.thresholds = {
	{.thresh_plugout = 0, .thresh_plugin = 0},
	/* Secondary CPUs filled in by r8a7373_add_standard_devices */
	},
};

static struct sh_cpufreq_plat_data cpufreq_data = {
	.hp_data = &cpufreq_hp_data,
};

static struct platform_device sh_cpufreq_device = {
	.name = "shmobile-cpufreq",
	.id = 0,
	.dev = {
		.platform_data = &cpufreq_data,
	},
};

/* IICM */
static struct i2c_sh_mobile_platform_data i2c8_platform_data = {
	.bus_speed	= 400000,
	.bus_data_delay = MIN_SDA_DELAY,
};

static struct resource i2c8_resources[] = {
	DEFINE_RES_MEM_NAMED(0xe6d20000, 0x9, "IICM"),
	DEFINE_RES_IRQ(gic_spi(191)),
};

static struct platform_device i2c8_device = {
	.name		= "i2c-sh7730",
	.id		= 8,
	.resource	= i2c8_resources,
	.num_resources	= ARRAY_SIZE(i2c8_resources),
	.dev		= {
		.platform_data	= &i2c8_platform_data,
	},
};


/* Transmit sizes and respective CHCR register values */
enum {
	XMIT_SZ_8BIT		= 0,
	XMIT_SZ_16BIT		= 1,
	XMIT_SZ_32BIT		= 2,
	XMIT_SZ_64BIT		= 7,
	XMIT_SZ_128BIT		= 3,
	XMIT_SZ_256BIT		= 4,
	XMIT_SZ_512BIT		= 5,
};

/* log2(size / 8) - used to calculate number of transfers */
#define TS_SHIFT {			\
	[XMIT_SZ_8BIT]		= 0,	\
	[XMIT_SZ_16BIT]		= 1,	\
	[XMIT_SZ_32BIT]		= 2,	\
	[XMIT_SZ_64BIT]		= 3,	\
	[XMIT_SZ_128BIT]	= 4,	\
	[XMIT_SZ_256BIT]	= 5,	\
	[XMIT_SZ_512BIT]	= 6,	\
}

#define TS_INDEX2VAL(i) ((((i) & 3) << 3) | (((i) & 0xc) << (20 - 2)))
#define CHCR_TX(xmit_sz) (DM_FIX | SM_INC | 0x800 | TS_INDEX2VAL((xmit_sz)))
#define CHCR_RX(xmit_sz) (DM_INC | SM_FIX | 0x800 | TS_INDEX2VAL((xmit_sz)))

static const struct sh_dmae_slave_config r8a7373_dmae_slaves[] = {
	{
		.slave_id	= SHDMA_SLAVE_SCIF0_TX,
		.addr		= 0xe6450020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x21,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF0_RX,
		.addr		= 0xe6450024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x22,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF1_TX,
		.addr		= 0xe6c50020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x25,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF1_RX,
		.addr		= 0xe6c50024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x26,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF2_TX,
		.addr		= 0xe6c60020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x29,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF2_RX,
		.addr		= 0xe6c60024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x2a,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF3_TX,
		.addr		= 0xe6c70020,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x2d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF3_RX,
		.addr		= 0xe6c70024,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x2e,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF4_TX,
		.addr		= 0xe6c20040,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x3d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF4_RX,
		.addr		= 0xe6c20060,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x3e,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF5_TX,
		.addr		= 0xe6c30040,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x19,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF5_RX,
		.addr		= 0xe6c30060,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x1a,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF6_TX,
		.addr		= 0xe6ce0040,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x1d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF6_RX,
		.addr		= 0xe6ce0060,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x1e,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF7_TX,
		.addr		= 0xe6470040,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x35,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF7_RX,
		.addr		= 0xe6470060,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x36,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI0_TX,
		.addr		= 0xee100030,
		.chcr		= CHCR_TX(XMIT_SZ_16BIT),
		.mid_rid	= 0xc1,
		.burst_sizes	= (1 << 1) | (1 << 4) | (1 << 5),
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI0_RX,
		.addr		= 0xee100030,
		.chcr		= CHCR_RX(XMIT_SZ_16BIT),
		.mid_rid	= 0xc2,
		.burst_sizes	= (1 << 1) | (1 << 4) | (1 << 5),
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_TX,
		.addr		= 0xee120030,
		.chcr		= CHCR_TX(XMIT_SZ_16BIT),
		.mid_rid	= 0xc5,
		.burst_sizes	= (1 << 1) | (1 << 4) | (1 << 5),
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_RX,
		.addr		= 0xee120030,
		.chcr		= CHCR_RX(XMIT_SZ_16BIT),
		.mid_rid	= 0xc6,
		.burst_sizes	= (1 << 1) | (1 << 4) | (1 << 5),
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI2_TX,
		.addr		= 0xee140030,
		.chcr		= CHCR_TX(XMIT_SZ_16BIT),
		.mid_rid	= 0xc9,
		.burst_sizes	= (1 << 1) | (1 << 4) | (1 << 5),
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI2_RX,
		.addr		= 0xee140030,
		.chcr		= CHCR_RX(XMIT_SZ_16BIT),
		.mid_rid	= 0xca,
		.burst_sizes	= (1 << 1) | (1 << 4) | (1 << 5),
	}, {
		.slave_id	= SHDMA_SLAVE_MMCIF0_TX,
		.addr		= 0xe6bd0034,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd1,
	}, {
		.slave_id	= SHDMA_SLAVE_MMCIF0_RX,
		.addr		= 0xe6bd0034,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd2,
	}, {
		.slave_id	= SHDMA_SLAVE_MMCIF1_TX,
		.addr		= 0xe6be0034,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xe1,
	}, {
		.slave_id	= SHDMA_SLAVE_MMCIF1_RX,
		.addr		= 0xe6be0034,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0xe2,
	}, {
		.slave_id	= SHDMA_SLAVE_FSI2A_TX,
		.addr		= 0xec230024,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd5,
	}, {
		.slave_id	= SHDMA_SLAVE_FSI2A_RX,
		.addr		= 0xec230020,
		.chcr		= CHCR_RX(XMIT_SZ_16BIT),
		.mid_rid	= 0xd6,
	}, {
		.slave_id	= SHDMA_SLAVE_FSI2B_TX,
		.addr		= 0xec230064,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd9,
	}, {
		.slave_id	= SHDMA_SLAVE_FSI2B_RX,
		.addr		= 0xec230060,
		.chcr		= CHCR_RX(XMIT_SZ_16BIT),
		.mid_rid	= 0xda,
	}, {
		.slave_id	= SHDMA_SLAVE_SCUW_FFD_TX,
		.addr		= 0xec700708,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0x79,
	}, {
		.slave_id	= SHDMA_SLAVE_SCUW_FFU_RX,
		.addr		= 0xec700714,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0x7a,
	}, {
		.slave_id	= SHDMA_SLAVE_SCUW_CPUFIFO_0_TX,
		.addr		= 0xec700720,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0x7d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCUW_CPUFIFO_2_RX,
		.addr		= 0xec700738,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0x7e,
	}, {
		.slave_id	= SHDMA_SLAVE_SCUW_CPUFIFO_1_TX,
		.addr		= 0xec70072c,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0x81,
	}, {
		.slave_id	= SHDMA_SLAVE_PCM2PWM_TX,
		.addr		= 0xec380080,
		.chcr		= CHCR_TX(XMIT_SZ_16BIT),
		.mid_rid	= 0x91,
	},
};

#define DMAE_CHANNEL(_offset)					\
	{							\
		.offset		= _offset - 0x20,		\
		.dmars		= _offset - 0x20 + 0x40,	\
	}

static const struct sh_dmae_channel r8a7373_dmae_channels[] = {
	DMAE_CHANNEL(0x8000),
	DMAE_CHANNEL(0x8080),
	DMAE_CHANNEL(0x8100),
	DMAE_CHANNEL(0x8180),
	DMAE_CHANNEL(0x8200),
	DMAE_CHANNEL(0x8280),
	DMAE_CHANNEL(0x8300),
	DMAE_CHANNEL(0x8380),
	DMAE_CHANNEL(0x8400),
	DMAE_CHANNEL(0x8480),
	DMAE_CHANNEL(0x8500),
	DMAE_CHANNEL(0x8580),
	DMAE_CHANNEL(0x8600),
	DMAE_CHANNEL(0x8680),
	DMAE_CHANNEL(0x8700),
	DMAE_CHANNEL(0x8780),
	DMAE_CHANNEL(0x8800),
	DMAE_CHANNEL(0x8880),
};

static const unsigned int ts_shift[] = TS_SHIFT;

static struct sh_dmae_pdata r8a7373_dmae_platform_data = {
	.slave		= r8a7373_dmae_slaves,
	.slave_num	= ARRAY_SIZE(r8a7373_dmae_slaves),
	.channel	= r8a7373_dmae_channels,
	.channel_num	= ARRAY_SIZE(r8a7373_dmae_channels),
	.ts_low_shift	= 3,
	.ts_low_mask	= 0x18,
	.ts_high_shift	= (20 - 2),	/* 2 bits for shifted low TS */
	.ts_high_mask	= 0x00300000,
	.ts_shift	= ts_shift,
	.ts_shift_num	= ARRAY_SIZE(ts_shift),
	.dmaor_init	= DMAOR_DME,
};

static struct resource r8a7373_dmae_resources[] = {
	{
		/* DescriptorMEM */
		.start  = 0xFE00A000,
		.end    = 0xFE00A7FC,
		.flags  = IORESOURCE_MEM,
	},
	{
		/* Registers including DMAOR and channels including DMARSx */
		.start	= 0xfe000020,
		.end	= 0xfe008a00 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* DMA error IRQ */
		.start	= gic_spi(167),
		.end	= gic_spi(167),
		.flags	= IORESOURCE_IRQ,
	},
	{
		/* IRQ for channels 0-17 */
		.start	= gic_spi(147),
		.end	= gic_spi(164),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dma0_device = {
	.name		= "sh-dma-engine",
	.id		= 0,
	.resource	= r8a7373_dmae_resources,
	.num_resources	= ARRAY_SIZE(r8a7373_dmae_resources),
	.dev		= {
		.platform_data	= &r8a7373_dmae_platform_data,
	},
};

/*
 * These three HPB semaphores will be requested at board-init timing,
 * and globally available (even for out-of-tree loadable modules).
 */
struct hwspinlock *r8a7373_hwlock_gpio;
EXPORT_SYMBOL(r8a7373_hwlock_gpio);
struct hwspinlock *r8a7373_hwlock_cpg;
EXPORT_SYMBOL(r8a7373_hwlock_cpg);
struct hwspinlock *r8a7373_hwlock_sysc;
EXPORT_SYMBOL(r8a7373_hwlock_sysc);

#ifdef CONFIG_SMECO
static struct resource smc_resources[] = {
	[0] = DEFINE_RES_IRQ(gic_spi(193)),
	[1] = DEFINE_RES_IRQ(gic_spi(194)),
	[2] = DEFINE_RES_IRQ(gic_spi(195)),
	[3] = DEFINE_RES_IRQ(gic_spi(196)),
};

static struct platform_device smc_netdevice0 = {
	.name		= "smc_net_device",
	.id		= 0,
	.resource	= smc_resources,
	.num_resources	= ARRAY_SIZE(smc_resources),
};

static struct platform_device smc_netdevice1 = {
	.name		= "smc_net_device",
	.id		= 1,
	.resource	= smc_resources,
	.num_resources	= ARRAY_SIZE(smc_resources),
};
#endif /* CONFIG_SMECO */

/* Bus Semaphores 0 */
static struct hwsem_desc r8a7373_hwsem0_descs[] = {
	HWSEM(SMGPIO, 0x20),
	HWSEM(SMCPG, 0x50),
	HWSEM(SMSYSC, 0x70),
};

static struct hwsem_pdata r8a7373_hwsem0_platform_data = {
	.base_id	= SMGPIO,
	.descs		= r8a7373_hwsem0_descs,
	.nr_descs	= ARRAY_SIZE(r8a7373_hwsem0_descs),
};

static struct resource r8a7373_hwsem0_resources[] = {
	{
		.start	= 0xe6001800,
		.end	= 0xe600187f,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device hwsem0_device = {
	.name		= "rmobile_hwsem",
	.id		= 0,
	.resource	= r8a7373_hwsem0_resources,
	.num_resources	= ARRAY_SIZE(r8a7373_hwsem0_resources),
	.dev = {
		.platform_data = &r8a7373_hwsem0_platform_data,
	},
};

/* Bus Semaphores 1 */
static struct hwsem_desc r8a7373_hwsem1_descs[] = {
	HWSEM(SMGP000, 0x30), HWSEM(SMGP001, 0x30),
	HWSEM(SMGP002, 0x30), HWSEM(SMGP003, 0x30),
	HWSEM(SMGP004, 0x30), HWSEM(SMGP005, 0x30),
	HWSEM(SMGP006, 0x30), HWSEM(SMGP007, 0x30),
	HWSEM(SMGP008, 0x30), HWSEM(SMGP009, 0x30),
	HWSEM(SMGP010, 0x30), HWSEM(SMGP011, 0x30),
	HWSEM(SMGP012, 0x30), HWSEM(SMGP013, 0x30),
	HWSEM(SMGP014, 0x30), HWSEM(SMGP015, 0x30),
	HWSEM(SMGP016, 0x30), HWSEM(SMGP017, 0x30),
	HWSEM(SMGP018, 0x30), HWSEM(SMGP019, 0x30),
	HWSEM(SMGP020, 0x30), HWSEM(SMGP021, 0x30),
	HWSEM(SMGP022, 0x30), HWSEM(SMGP023, 0x30),
	HWSEM(SMGP024, 0x30), HWSEM(SMGP025, 0x30),
	HWSEM(SMGP026, 0x30), HWSEM(SMGP027, 0x30),
	HWSEM(SMGP028, 0x30), HWSEM(SMGP029, 0x30),
	HWSEM(SMGP030, 0x30), HWSEM(SMGP031, 0x30),
};

static struct hwsem_pdata r8a7373_hwsem1_platform_data = {
	.base_id	= SMGP000,
	.descs		= r8a7373_hwsem1_descs,
	.nr_descs	= ARRAY_SIZE(r8a7373_hwsem1_descs),
};

static struct resource r8a7373_hwsem1_resources[] = {
	{
		.start	= 0xe6001800,
		.end	= 0xe600187f,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* software extension base */
		.start	= SDRAM_SOFT_SEMAPHORE_TVRF_START_ADDR,
		.end	= SDRAM_SOFT_SEMAPHORE_TVRF_END_ADDR,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device hwsem1_device = {
	.name		= "rmobile_hwsem",
	.id		= 1,
	.resource	= r8a7373_hwsem1_resources,
	.num_resources	= ARRAY_SIZE(r8a7373_hwsem1_resources),
	.dev		= {
		.platform_data	= &r8a7373_hwsem1_platform_data,
	},
};

/* Bus Semaphores 2 */
static struct hwsem_desc r8a7373_hwsem2_descs[] = {
	HWSEM(SMGP100, 0x40), HWSEM(SMGP101, 0x40),
	HWSEM(SMGP102, 0x40), HWSEM(SMGP103, 0x40),
	HWSEM(SMGP104, 0x40), HWSEM(SMGP105, 0x40),
	HWSEM(SMGP106, 0x40), HWSEM(SMGP107, 0x40),
	HWSEM(SMGP108, 0x40), HWSEM(SMGP109, 0x40),
	HWSEM(SMGP110, 0x40), HWSEM(SMGP111, 0x40),
	HWSEM(SMGP112, 0x40), HWSEM(SMGP113, 0x40),
	HWSEM(SMGP114, 0x40), HWSEM(SMGP115, 0x40),
	HWSEM(SMGP116, 0x40), HWSEM(SMGP117, 0x40),
	HWSEM(SMGP118, 0x40), HWSEM(SMGP119, 0x40),
	HWSEM(SMGP120, 0x40), HWSEM(SMGP121, 0x40),
	HWSEM(SMGP122, 0x40), HWSEM(SMGP123, 0x40),
	HWSEM(SMGP124, 0x40), HWSEM(SMGP125, 0x40),
	HWSEM(SMGP126, 0x40), HWSEM(SMGP127, 0x40),
	HWSEM(SMGP128, 0x40), HWSEM(SMGP129, 0x40),
	HWSEM(SMGP130, 0x40), HWSEM(SMGP131, 0x40),
};

static struct hwsem_pdata r8a7373_hwsem2_platform_data = {
	.base_id	= SMGP100,
	.descs		= r8a7373_hwsem2_descs,
	.nr_descs	= ARRAY_SIZE(r8a7373_hwsem2_descs),
};

static struct resource r8a7373_hwsem2_resources[] = {
	{
		.start	= 0xe6001800,
		.end	= 0xe600187f,
		.flags	= IORESOURCE_MEM,
	},
	{
		/* software bit extension */
		.start	= SDRAM_SOFT_SEMAPHORE_E20_START_ADDR,
		.end	= SDRAM_SOFT_SEMAPHORE_E20_END_ADDR,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device hwsem2_device = {
	.name		= "rmobile_hwsem",
	.id			= 2,
	.resource	= r8a7373_hwsem2_resources,
	.num_resources	= ARRAY_SIZE(r8a7373_hwsem2_resources),
	.dev = {
		.platform_data = &r8a7373_hwsem2_platform_data,
	},
};


static struct resource sgx_resources[] = {
	DEFINE_RES_MEM(0xfd000000, 0xc000),
	DEFINE_RES_IRQ(gic_spi(92)),
};

static struct platform_device sgx_device = {
	.name		= "pvrsrvkm",
	.id		= -1,
	.resource	= sgx_resources,
	.num_resources	= ARRAY_SIZE(sgx_resources),
};

#ifdef CONFIG_SH_RAMDUMP
static struct hw_register_range ramdump_res[] __initdata = {
	DEFINE_RES_RAMDUMP(0xE6180080, 0xE6180080, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6020000, 0xE6020000, HW_REG_16BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6020004, 0xE6020004, HW_REG_8BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6020008, 0xE6020008, HW_REG_8BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6030000, 0xE6030000, HW_REG_16BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6030004, 0xE6030004, HW_REG_8BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6030008, 0xE6030008, HW_REG_8BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6100020, 0xE6100020, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6100028, 0xE610002c, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6130500, 0xE6130500, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6130510, 0xE6130510, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6130514, 0xE6130514, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6130518, 0xE6130518, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6150000, 0xE6150200, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6151000, 0xE6151180, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6180000, 0xE61800FC, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE6180200, 0xE618027C, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE618801C, 0xE6188024, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE61C0100, 0xE61C0104, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE61C0300, 0xE61C0304, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE61D0000, 0xE61D0000, HW_REG_16BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE61D0040, 0xE61D0040, HW_REG_16BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE61D0044, 0xE61D0048, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE623000C, 0xE623000C, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE623200C, 0xE623200C, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xE62A0004, 0xE62A0008, HW_REG_8BIT, 0, MSTPSR5,
			MSTPST525),
	DEFINE_RES_RAMDUMP(0xE62A002C, 0xE62A002C, HW_REG_8BIT, 0, MSTPSR5,
			MSTPST525),
	/*NOTE: at the moment address increment is done by 4 byte steps
	 * so this will read one byte from 004 and one byte form 008 */
	DEFINE_RES_RAMDUMP(0xE6820004, 0xE6820008, HW_REG_8BIT, POWER_A3SP,
			MSTPSR1, MSTPST116),
	DEFINE_RES_RAMDUMP(0xE682002C, 0xE682002C, HW_REG_8BIT, POWER_A3SP,
			MSTPSR1, MSTPST116),
	DEFINE_RES_RAMDUMP(0xF000010C, 0xF0000110, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xFE400000, 0xFE400000, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xFE400200, 0xFE400240, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xFE400358, 0xFE400358, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xFE401000, 0xFE401004, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xFE4011F4, 0xFE4011F4, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xFE401050, 0xFE4010BC, HW_REG_32BIT, 0, NULL, 0),
	DEFINE_RES_RAMDUMP(0xFE951000, 0xFE9510FC, HW_REG_32BIT, POWER_A3R,
			SMSTPCR0, MSTO007),
};

struct ramdump_plat_data ramdump_pdata __initdata = {
	.reg_dump_base = SDRAM_REGISTER_DUMP_AREA_START_ADDR,
	.reg_dump_size = SDRAM_REGISTER_DUMP_AREA_END_ADDR -
			SDRAM_REGISTER_DUMP_AREA_START_ADDR + 1,
	/* size of reg dump of each core */
	.core_reg_dump_size = SZ_1K,
	.num_resources = ARRAY_SIZE(ramdump_res),
	.hw_register_range = ramdump_res,
};

/* platform_device structure can not be marked as __initdata as
 * it is used by platform_uevent etc. That is why __refdata needs
 * to be used. platform_data pointer is nulled in probe */
static struct platform_device ramdump_device __refdata = {
	.name = "ramdump",
	.id = -1,
	.dev.platform_data = &ramdump_pdata,
};
#endif

static struct resource mtd_res[] = {
                [0] = {
                                .name   = "mtd_trace",
                                .start     = SDRAM_STM_TRACE_BUFFER_START_ADDR,
                                .end       = SDRAM_STM_TRACE_BUFFER_END_ADDR,
                                .flags     = IORESOURCE_MEM,
                },
};
static struct platform_device mtd_device = {
                .name = "mtd_trace",
                .num_resources               = ARRAY_SIZE(mtd_res),
                .resource             = mtd_res,
};

static struct sh_mmcif_plat_data renesas_mmcif_plat = {
	.sup_pclk	= 0,
	.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA |
		MMC_CAP_1_8V_DDR | MMC_CAP_UHS_DDR50 | MMC_CAP_NONREMOVABLE,
	.slave_id_tx	= SHDMA_SLAVE_MMCIF0_TX,
	.slave_id_rx	= SHDMA_SLAVE_MMCIF0_RX,
	.max_clk	= 52000000,
};

static struct fb_board_cfg lcdc_info;

static struct resource mfis_resources[] = {
	[0] = {
		.name   = "MFIS",
		.start  = gic_spi(126),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device mfis_device = {
	.name           = "mfis",
	.id             = 0,
	.resource       = mfis_resources,
	.num_resources  = ARRAY_SIZE(mfis_resources),
};

static struct resource mdm_reset_resources[] = {
	[0] = {
		.name	= "MODEM_RESET",
		.start	= 0xE6190000,
		.end	= 0xE61900FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(219), /* EPMU_int1 */
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device mdm_reset_device = {
	.name		= "rmc_wgm_reset_int",
	.id			= 0,
	.resource	= mdm_reset_resources,
	.num_resources	= ARRAY_SIZE(mdm_reset_resources),
};

#ifdef CONFIG_SPI_SH_MSIOF
/* SPI */
static struct sh_msiof_spi_info sh_msiof0_info = {
	.rx_fifo_override = 256,
	.num_chipselect = 1,
};

static struct resource sh_msiof0_resources[] = {
	[0] = {
		.start  = 0xe6e20000,
		.end    = 0xe6e20064 - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = gic_spi(109),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sh_msiof0_device = {
	.name = "spi_sh_msiof",
	.id   = 0,
	.dev  = {
		.platform_data  = &sh_msiof0_info,
	},
	.num_resources  = ARRAY_SIZE(sh_msiof0_resources),
	.resource       = sh_msiof0_resources,
};
#endif

static struct resource	tpu_resources[] = {
	[TPU_MODULE_0] = DEFINE_RES_MEM_NAMED(0xe6600000, 0x200, "tpu0_map"),
};

static struct platform_device	tpu_devices[] = {
	{
		.name		= "tpu-renesas-sh_mobile",
		.id		= TPU_MODULE_0,
		.num_resources	= 1,
		.resource	= &tpu_resources[TPU_MODULE_0],
	},
};

static struct platform_device *u2_devices[] __initdata = {
#ifdef CONFIG_SH_RAMDUMP
	&ramdump_device,
#endif
	&i2c8_device, /* IICM  */
	&dma0_device,
#ifdef CONFIG_SMECO
	&smc_netdevice0,
	&smc_netdevice1,
#endif
	&sgx_device,
	&mtd_device,
	&sh_cpufreq_device,
	&mfis_device,
	&tpu_devices[TPU_MODULE_0],
	&mdm_reset_device,
#ifdef CONFIG_SPI_SH_MSIOF
	&sh_msiof0_device,
#endif
#if defined(CONFIG_CMA) && defined(CONFIG_MOBICORE_API)
	&rt_cma_device,
#endif
};

static struct platform_device *r8a7373_hwsem_devices[] __initdata = {
	&hwsem0_device,
	&hwsem1_device,
	&hwsem2_device,
};

static const struct resource pfc_resources[] = {
	DEFINE_RES_MEM(0xe6050000, 0x9000),
};

static const struct of_dev_auxdata r8a7373_auxdata_lookup[] __initconst = {
	/* Have to pass pdata to get fixed IRQ numbering for non-DT drivers */
	OF_DEV_AUXDATA("renesas,irqc", 0xe61c0000, NULL, &irqc0_data),
	OF_DEV_AUXDATA("renesas,irqc", 0xe61c0200, NULL, &irqc1_data),
	OF_DEV_AUXDATA("renesas,irqc", 0xe61c1400, NULL, &irqc10_data),
	OF_DEV_AUXDATA("renesas,irqc", 0xe61c1800, NULL, &irqc12_data),
	OF_DEV_AUXDATA("renesas,lcdc-shmobile", 0xfe940000, NULL, &lcdc_info),
	/* mmcif pdata is required to manage platform callbacks */
	OF_DEV_AUXDATA("renesas,renesas-mmcif", 0xe6bd0000, NULL,
		       &renesas_mmcif_plat),
	OF_DEV_AUXDATA("renesas,sdhi-r8a7373", 0xee100000, NULL,
			&sdhi0_info),
	OF_DEV_AUXDATA("renesas,sdhi-r8a7373", 0xee120000, NULL,
			&sdhi1_info),
	OF_DEV_AUXDATA("renesas,r8a66597-usb", 0xe6890000, NULL,
			&usbhs_func_data_d2153),
	{},
};

void r8a7373_set_mode(bool vmode)
{
	struct platform_device *p_dev;
	struct device_node *np;
	struct rt_boot_platform_data *rt_pdata;

	np = of_find_node_by_path("/rtboot");
	if (np) {
		if (of_device_is_available(np)) {
			p_dev = of_find_device_by_node(np);
			if (p_dev) {
				rt_pdata = (struct rt_boot_platform_data *)
						p_dev->dev.platform_data;
				lcdc_info.vmode = vmode;
				rt_pdata->screen0.mode = !vmode;
			} else {
				pr_err("no RT device found\n");
			}
		}
		of_node_put(np);
	}
}

void __init r8a7373_pinmux_init(void)
{
	sh_pfc_register_mappings(scif_pinctrl_map,
				 ARRAY_SIZE(scif_pinctrl_map));

	/* We need hwspinlocks ready now for the pfc driver */
	platform_add_devices(r8a7373_hwsem_devices,
			ARRAY_SIZE(r8a7373_hwsem_devices));
}

static const struct of_device_id gic_matches[] __initconst = {
	{ .compatible ="arm,cortex-a9-gic" },
	{ .compatible ="arm,cortex-a7-gic" },
	{ .compatible ="arm,cortex-a15-gic" },
};

void __init r8a7373_patch_ramdump_addresses(void)
{
	int i;
	struct device_node *gic;
	struct resource dist, cpu;

	gic = of_find_matching_node(NULL, gic_matches);
	if (!gic)
		return;

	of_address_to_resource(gic, 0, &dist);
	of_address_to_resource(gic, 1, &cpu);
	of_node_put(gic);

	/* fix up ramdump register ranges for u2b */
	for (i=0; i< ARRAY_SIZE(ramdump_res); i++) {
		if ((ramdump_res[i].start &~ 0xFF) == 0xF0000100) {
			/* GIC CPU address in U2 */
			ramdump_res[i].start += cpu.start - 0xF0000100;
			ramdump_res[i].end += cpu.start - 0xF0000100;
		} else if ((ramdump_res[i].start &~ 0xFFF) == 0xF0001000) {
			/* GIC dist address in U2 */
			ramdump_res[i].start += dist.start - 0xF0001000;
			ramdump_res[i].end += dist.start - 0xF0001000;
		}
	}
}

static void __init cpufreq_init_hp_thresholds(void)
{
	/* cpu1 */
	cpufreq_hp_data.thresholds[1].thresh_plugout = 373750;
	cpufreq_hp_data.thresholds[1].thresh_plugin = 373750;

#if !(CONFIG_NR_CPUS == 2)
	if (shmobile_is_u2b()) {
		/* cpu1 */
		cpufreq_hp_data.thresholds[1].thresh_plugout = 373750;
		cpufreq_hp_data.thresholds[1].thresh_plugin = 373750;

		/* cpu2 */
		cpufreq_hp_data.thresholds[2].thresh_plugout = 747500;
		cpufreq_hp_data.thresholds[2].thresh_plugin = 747500;

		/* cpu3 */
		cpufreq_hp_data.thresholds[3].thresh_plugout = 1046500;
		cpufreq_hp_data.thresholds[3].thresh_plugin = 1046500;
	}
#endif /* !(CONFIG_NR_CPUS == 2) */
}

void __init r8a7373_add_standard_devices(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
				r8a7373_auxdata_lookup, NULL);
	r8a7373_irqc_init(); /* Actually just INTCS and FIQ init now... */

	r8a7373_register_scif(SCIFA0);
	r8a7373_register_scif(SCIFB0);
	r8a7373_register_scif(SCIFB1);

	r8a7373_patch_ramdump_addresses();

	/* Fill in cpufreq hotplug thresholds */
	cpufreq_init_hp_thresholds();

	platform_add_devices(u2_devices, ARRAY_SIZE(u2_devices));
	gpiopd_hwspinlock_init(r8a7373_hwlock_gpio);
}
/*Do Dummy write in L2 cache to avoid A2SL turned-off
	just after L2-sync operation */
#ifdef CONFIG_ARCH_SHMOBILE
void __init r8a7373_avoid_a2slpowerdown_afterL2sync(void)
{
	dummy_write_mem = __arm_ioremap(
	(unsigned long)(get_mem_resource("non_secure_spinlock")->start +
			0x00000400), 0x00000400/*1k*/, MT_UNCACHED);

	if (dummy_write_mem == NULL)
		printk(KERN_ERR "97373_a2slpowerdown_workaround Failed\n");
}
#endif
/* do nothing for !CONFIG_SMP or !CONFIG_HAVE_TWD */

/* Lock used while modifying register */
static DEFINE_SPINLOCK(io_lock);

void sh_modify_register8(void __iomem *addr, u8 clear, u8 set)
{
	unsigned long flags;
	u8 val;
	spin_lock_irqsave(&io_lock, flags);
	val = __raw_readb(addr);
	val &= ~clear;
	val |= set;
	__raw_writeb(val, addr);
	spin_unlock_irqrestore(&io_lock, flags);
}
EXPORT_SYMBOL_GPL(sh_modify_register8);

void sh_modify_register16(void __iomem *addr, u16 clear, u16 set)
{
	unsigned long flags;
	u16 val;
	spin_lock_irqsave(&io_lock, flags);
	val = __raw_readw(addr);
	val &= ~clear;
	val |= set;
	__raw_writew(val, addr);
	spin_unlock_irqrestore(&io_lock, flags);
}
EXPORT_SYMBOL_GPL(sh_modify_register16);

void sh_modify_register32(void __iomem *addr, u32 clear, u32 set)
{
	unsigned long flags;
	u32 val;
	spin_lock_irqsave(&io_lock, flags);
	val = __raw_readl(addr);
	val &= ~clear;
	val |= set;
	__raw_writel(val, addr);
	spin_unlock_irqrestore(&io_lock, flags);
}
EXPORT_SYMBOL_GPL(sh_modify_register32);

void __iomem *sbsc_sdmracr1a;

static void SBSC_Init_520Mhz(void)
{
	unsigned long work;

	printk(KERN_ALERT "START < %s >\n", __func__);

	/* Check PLL3 status */
	work = __raw_readl(PLLECR);
	if (!(work & PLLECR_PLL3ST)) {
		printk(KERN_ALERT "PLLECR_PLL3ST is 0\n");
		return;
	}

	/* Set PLL3 = 1040 Mhz*/
	__raw_writel(PLL3CR_1040MHZ, PLL3CR);

	/* Wait PLL3 status on */
	while (1) {
		work = __raw_readl(PLLECR);
		work &= PLLECR_PLL3ST;
		if (work == PLLECR_PLL3ST)
			break;
	}

	/* Dummy read */
	__raw_readl(sbsc_sdmracr1a);
}

void __init r8a7373_zq_calibration(void)
{
	/* ES2.02 / LPDDR2 ZQ Calibration Issue WA */
	void __iomem *sbsc_sdmra_28200 = NULL;
	void __iomem *sbsc_sdmra_38200 = NULL;
	u8 reg8 = __raw_readb(STBCHRB3);

	if ((reg8 & 0x80) && !shmobile_is_older(U2_VERSION_2_2)) {
		pr_err("< %s >Apply for ZQ calibration\n", __func__);
		pr_err("< %s > Before CPG_PLL3CR 0x%8x\n",
				__func__, __raw_readl(PLL3CR));
		sbsc_sdmracr1a   = ioremap(SBSC_BASE + 0x000088, 0x4);
		sbsc_sdmra_28200 = ioremap(SBSC_BASE + 0x128200, 0x4);
		sbsc_sdmra_38200 = ioremap(SBSC_BASE + 0x138200, 0x4);
		if (sbsc_sdmracr1a && sbsc_sdmra_28200 && sbsc_sdmra_38200) {
			SBSC_Init_520Mhz();
			__raw_writel(SBSC_SDMRACR1A_ZQ, sbsc_sdmracr1a);
			__raw_writel(SBSC_SDMRA_DONE, sbsc_sdmra_28200);
			__raw_writel(SBSC_SDMRA_DONE, sbsc_sdmra_38200);
		} else {
			pr_err("%s: ioremap failed.\n", __func__);
		}
		pr_err("< %s > After CPG_PLL3CR 0x%8x\n",
				__func__, __raw_readl(PLL3CR));
		if (sbsc_sdmracr1a)
			iounmap(sbsc_sdmracr1a);
		if (sbsc_sdmra_28200)
			iounmap(sbsc_sdmra_28200);
		if (sbsc_sdmra_38200)
			iounmap(sbsc_sdmra_38200);
	}
}

static void shmobile_check_rev(void)
{
	shmobile_revision = __raw_readl(IOMEM(CCCR));
}

inline unsigned int shmobile_rev(void)
{
	unsigned int chiprev;

	if (!shmobile_revision)
		shmobile_check_rev();
	chiprev = (shmobile_revision & CCCR_VERSION_MASK);
	return chiprev;
}
EXPORT_SYMBOL(shmobile_rev);

void __init r8a7373_l2cache_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	/*
	 * [30] Early BRESP enable
	 * [27] Non-secure interrupt access control
	 * [26] Non-secure lockdown enable
	 * [22] Shared attribute override enable
	 * [19:17] Way-size: b011 = 64KB
	 * [16] Accosiativity: 0 = 8-way
	 */
	l2x0_of_init(0x4c460000, 0x820f0fff);
#endif
}


void __init r8a7373_init_early(void)
{
	sh_primary_pfc = "e6050000.pfc";

#ifdef CONFIG_ARM_TZ
	r8a7373_l2cache_init();
#endif

}

void __init r8a7373_init_late(void)
{
	if (!proc_create("board_revision", 0444, NULL, &board_rev_ops))
		pr_warn("creation of /proc/board_revision failed\n");
	PRINT_HW_REV(system_rev);

#ifdef CONFIG_PM
	rmobile_pm_late_init();
#endif
}

void __init declare_cma(char *name, struct device *dev, phys_addr_t start,
		phys_addr_t end)
{
	int ret;

	ret = dma_declare_contiguous(dev, (end-start)+1, start, 0);

	pr_alert("%s dma_declare_contiguous: 0x%x-0x%x = %d\n",
			name, start, end, ret);
}

/*
 * Common reserve for R8 A7373 - for memory carveouts
 */
void __init r8a7373_reserve(void)
{
	struct resource *rp;

	/* init memory layout according to memory size */
	init_memory_layout();

#if defined(CONFIG_CMA) && defined(CONFIG_MOBICORE_API)
	/* Declare a CMA area for secure DRM buffer */
	declare_cma("SDRAM_DRM",
		&rt_cma_device.dev,
		SDRAM_DRM_AREA_START_ADDR,
		SDRAM_DRM_AREA_END_ADDR);
#endif
	
	/* update static resource from the memory layout */
	rp = get_mem_resource("ramdump");
	ramdump_pdata.reg_dump_base = rp->start;
	ramdump_pdata.reg_dump_size = resource_size(rp);

	rp = get_mem_resource("soft_semaphore_tvrf");
	r8a7373_hwsem1_resources[1].start = rp->start;
	r8a7373_hwsem1_resources[1].end = rp->end;

	rp = get_mem_resource("soft_semaphore_e20");
	r8a7373_hwsem2_resources[1].start = rp->start;
	r8a7373_hwsem2_resources[1].end = rp->end;

	rp = get_mem_resource("SDRAM_CP_MODEMTR");
	mtd_res[0].start = rp->start;
	mtd_res[0].end = rp->end;
}

static const char *r8a7373_dt_compat[] __initdata = {
	"renesas,r8a7373",
	"renesas,r8a73a7",
	NULL,
};

DT_MACHINE_START(DT_R8A7373, "R8a7373 SOC DT Support")
	.dt_compat	= r8a7373_dt_compat,
MACHINE_END
