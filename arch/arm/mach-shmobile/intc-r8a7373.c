/*
 * r8a7373 processor support - INTC hardware block
 *
 * Copyright (C) 2012  Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/io.h>
#include <linux/sh_intc.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/irqchip/chained_irq.h>
#include <asm/mach/irq.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7373.h>

enum {
	IRQC_INTERVAL_1MS = 0,
	IRQC_INTERVAL_2MS,
	IRQC_INTERVAL_4MS,
	IRQC_INTERVAL_8MS
};

int r8a7373_irqc_set_debounce(int irq, unsigned int debounce)
{
	u32 val, interval, count;
	u32 __iomem *reg;

	irq -= IRQPIN_BASE;
	if (irq < 0 || irq > 63)
		return -EINVAL;

	debounce = (debounce + 999) / 1000;
	if (debounce <= 0x3f) {
		interval = IRQC_INTERVAL_1MS;
		count = debounce;
	} else if (debounce <= 0x3f * 2) {
		interval = IRQC_INTERVAL_2MS;
		count = (debounce + 1) / 2;
	} else if (debounce <= 0x3f * 4) {
		interval = IRQC_INTERVAL_4MS;
		count = (debounce + 3) / 4;
	} else if (debounce <= 0x3f * 8) {
		interval = IRQC_INTERVAL_8MS;
		count = (debounce + 7) / 8;
	} else {
		interval = IRQC_INTERVAL_8MS;
		count = 0x3f;
	}

	reg = (irq >= 32) ? IRQC1_CONFIG_00 : IRQC0_CONFIG_00;
	reg += (irq & 0x1f);

	val = __raw_readl(reg) & ~0x80ff0000;
	if (count) {
		__raw_writel(val | (1 << 31) | (interval << 22) | (count << 16)
					, reg);
	} else {
		__raw_writel(val | (0 << 31) | (interval << 22) | (count << 16)
					, reg);
	}
	return 0;
}

int r8a7373_irqc_get_debounce(int irq, unsigned int *debounce)
{
	u32 val, interval, count;
	u32 __iomem *reg;

	irq -= IRQPIN_BASE;
	if (irq < 0 || irq > 63)
		return -EINVAL;

	reg = (irq >= 32) ? IRQC1_CONFIG_00 : IRQC0_CONFIG_00;
	reg += (irq & 0x1f);

	val = __raw_readl(reg);
	if (val & BIT(31)) {
		interval = (val >> 22) & 3;
		count = (val >> 16) & 0x3F;
		switch (interval) {
		case IRQC_INTERVAL_1MS: *debounce = count * 1000; break;
		case IRQC_INTERVAL_2MS: *debounce = count * 2000; break;
		case IRQC_INTERVAL_4MS: *debounce = count * 4000; break;
		case IRQC_INTERVAL_8MS: *debounce = count * 8000; break;
		}
	} else {
		*debounce = 0;
	}

	return 0;
}

enum {
	UNUSED = 0,
	DISABLED,

	/* interrupt sources INTCS */
	VPU, _2DDM0, TSIF, LMB, JPU_JPEG, LCDC0, CSIRX,
	DSITX0_DSITX0, DSITX0_DSITX1, FSI, LCDC1, VSP, JPU6E, CSIRX1,
	DSITX1_0, DSITX1_1,
	ISP_PRE, ISP_POS, RCU1, RCU0,
	/* interrupt groups INTCS */
	DSITX0,
};

static struct intc_vect intcs_vectors[] = {
	INTCS_VECT(VPU, 0x0980), INTCS_VECT(_2DDM0, 0x0a00),
	INTCS_VECT(TSIF, 0x0f20), INTCS_VECT(LMB, 0x0f60),
	INTCS_VECT(JPU_JPEG, 0x0560), INTCS_VECT(LCDC0, 0x0580),
	INTCS_VECT(CSIRX, 0x17a0), INTCS_VECT(DSITX0_DSITX0, 0x17c0),
	INTCS_VECT(DSITX0_DSITX1, 0x17e0), INTCS_VECT(FSI, 0x1840),
	INTCS_VECT(LCDC1, 0x1880), INTCS_VECT(VSP, 0x1b60),
	INTCS_VECT(JPU6E, 0x1bc0), INTCS_VECT(CSIRX1, 0x1be0),
	INTCS_VECT(DSITX1_0, 0x1c00), INTCS_VECT(DSITX1_1, 0x1c20),
	INTCS_VECT(ISP_PRE, 0x1d40), INTCS_VECT(ISP_POS, 0x1d60),
	INTCS_VECT(RCU1, 0x1dc0), INTCS_VECT(RCU0, 0x1de0),
};

static struct intc_mask_reg intcs_mask_registers[] = {
	{ 0xffd20188, 0xffd201c8, 8, /* IMR2SA / IMCR2SA */
	  { 0, 0, 0, VPU,
	    0, 0, 0, 0 } },
	{ 0xffd2018c, 0xffd201cc, 8, /* IMR3SA / IMCR3SA */
	  { 0, 0, 0, _2DDM0,
	    0, 0, 0, 0 } },
	{ 0xffd20190, 0xffd201d0, 8, /* IMR4SA / IMCR4SA */
	  { 0, 0, 0, 0,
	    JPU_JPEG, 0, 0, LCDC0 } },
	{ 0xffd201ac, 0xffd201ec, 8, /* IMR11SA / IMCR11SA */
	  { 0, 0, 0, 0,
	    0, 0, LMB, TSIF } },
	{ 0xffd50190, 0xffd501d0, 8, /* IMR4SA3 / IMCR4SA3 */
	  { 0, 0, 0, 0,
	    0, CSIRX, DSITX0_DSITX0, DSITX0_DSITX1 } },
	{ 0xffd50194, 0xffd501d4, 8, /* IMR5SA3 / IMCR5SA3 */
	  { 0, 0, FSI, 0,
	    LCDC1, 0, 0, 0 } },
	{ 0xffd501a0, 0xffd501e0, 8, /* IMR8SA3 / IMCR8SA3 */
	  { 0, 0, 0, VSP,
	    0, 0, JPU6E, CSIRX1 } },
	{ 0xffd501a4, 0xffd501e4, 8, /* IMR9SA3 / IMCR9SA3 */
	  { DSITX1_0, DSITX1_1, 0, 0,
	    0, 0, 0, 0 } },
	{ 0xffd501a8, 0xffd501e8, 8, /* IMR10SA3 / IMCR10SA3 */
	  { 0, 0, ISP_PRE, ISP_POS,
	    0, 0, RCU1, RCU0 } },
};

static struct intc_group intcs_groups[] __initdata = {
	INTC_GROUP(DSITX0, DSITX0_DSITX0, DSITX0_DSITX1),
};

static struct intc_prio_reg intcs_prio_registers[] = {
	{ 0xffd20000, 0, 16, 4, /* IPRAS */ { 0, 0, _2DDM0, 0 } },
	{ 0xffd20004, 0, 16, 4, /* IPRBS */ { JPU_JPEG, LCDC0, 0, 0 } },
	{ 0xffd20010, 0, 16, 4, /* IPRES */ { 0, 0, 0, VPU } },
	{ 0xffd20020, 0, 16, 4, /* IPRIS */ { 0, 0, TSIF, 0 } },
	{ 0xffd20028, 0, 16, 4, /* IPRKS */ { 0, 0, LMB, 0 } },
	{ 0xffd50024, 0, 16, 4, /* IPRJS3 */ { 0, CSIRX, DSITX0, 0 } },
	{ 0xffd50028, 0, 16, 4, /* IPRKS3 */ { 0, 0, FSI, 0 } },
	{ 0xffd5002c, 0, 16, 4, /* IPRLS3 */ { LCDC1, 0, 0, 0 } },
	{ 0xffd50040, 0, 16, 4, /* IPRQS3 */ { 0, 0, 0, VSP } },
	{ 0xffd50044, 0, 16, 4, /* IPRRS3 */ { 0, 0, JPU6E, CSIRX1 } },
	{ 0xffd50048, 0, 16, 4, /* IPRSS3 */ { DSITX1_0, DSITX1_1, 0, 0 } },
	{ 0xffd50050, 0, 16, 4, /* IPRUS3 */ { 0, 0, ISP_PRE, ISP_POS } },
	{ 0xffd50054, 0, 16, 4, /* IPRVS3 */ { 0, 0, RCU1, RCU0 } },
};

static struct resource intcs_resources[] __initdata = {
	[0] = DEFINE_RES_MEM(0xffd20000, 0x200),
	[1] = DEFINE_RES_MEM(0xffd50000, 0x200),
};

static struct intc_desc intcs_desc __initdata = {
	.name = "intcs",
	.skip_syscore_suspend = true,
	.resource = intcs_resources,
	.num_resources = ARRAY_SIZE(intcs_resources),
	.hw = INTC_HW_DESC(intcs_vectors, intcs_groups, intcs_mask_registers,
			   intcs_prio_registers, NULL, NULL),
};

static void intcs_demux(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_get_chip(irq);
	void __iomem *reg = (void __iomem *)irq_get_handler_data(irq);
	unsigned int evtcodeas = __raw_readl(reg);

	chained_irq_enter(chip, desc);
	generic_handle_irq(intcs_evt2irq(evtcodeas));
	chained_irq_exit(chip, desc);
}

void __init r8a7373_init_irq(void)
{
	irqchip_init();
	gic_arch_extn.flags = IRQCHIP_SKIP_SET_WAKE;
}

void __init r8a7373_irqc_init(void)
{
	void __iomem *intevtsa = ioremap_nocache(0xffd20100, PAGE_SIZE);
	BUG_ON(!intevtsa);

	/* Setup INTCS cascade_irq */
	register_intc_controller(&intcs_desc);
	irq_set_handler_data(gic_spi(223), (void __force *)intevtsa);
	irq_set_chained_handler(gic_spi(223), intcs_demux);

#ifdef CONFIG_FIQ
	init_FIQ(FIQ_START);
#endif
}
