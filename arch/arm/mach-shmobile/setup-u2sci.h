/*
 * r8a7373 processor support - SCIF related configuration
 *
 * Copyright (C) 2012  Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */
#ifndef __SETUP_U2_SCI_H__
#define __SETUP_U2_SCI_H__

#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/serial_sci.h>

#include "sh-pfc.h"

static unsigned long pin_pulloff_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_DISABLE, 0),
};

static unsigned long pin_pullup_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_UP, 1),
};

static struct pinctrl_map scif_pinctrl_map[] __initdata = {
	SH_PFC_MUX_GROUP_DEFAULT("sh-sci.0", "scifa0_data", "scifa0"),
	SH_PFC_CONFIGS_PIN_DEFAULT("sh-sci.0", "PORT128" /* TXD */,
				   pin_pulloff_conf),
	SH_PFC_CONFIGS_PIN_DEFAULT("sh-sci.0", "PORT129" /* RXD */,
				   pin_pullup_conf),
	SH_PFC_MUX_GROUP_DEFAULT("sh-sci.4", "scifb0_data", "scifb0"),
	SH_PFC_MUX_GROUP_DEFAULT("sh-sci.4", "scifb0_ctrl", "scifb0"),
	SH_PFC_CONFIGS_PIN_DEFAULT("sh-sci.4", "PORT137" /* TXD */,
				   pin_pulloff_conf),
	SH_PFC_CONFIGS_PIN_DEFAULT("sh-sci.4", "PORT138" /* RXD */,
				   pin_pullup_conf),
	SH_PFC_CONFIGS_PIN_DEFAULT("sh-sci.4", "PORT37" /* RTS */,
				   pin_pulloff_conf),
	SH_PFC_CONFIGS_PIN_DEFAULT("sh-sci.4", "PORT38" /* CTS */,
				   pin_pullup_conf),
	SH_PFC_MUX_GROUP_DEFAULT("sh-sci.5", "scifb1_data", "scifb1"),
	SH_PFC_MUX_GROUP_DEFAULT("sh-sci.5", "scifb1_ctrl", "scifb1"),
	SH_PFC_CONFIGS_PIN_DEFAULT("sh-sci.5", "PORT78" /* TXD */,
				   pin_pulloff_conf),
	SH_PFC_CONFIGS_PIN_DEFAULT("sh-sci.5", "PORT79" /* RXD */,
				   pin_pullup_conf),
	/* unlike sh-sci.4, no pullup on CTS, as per pre-pinctrl settings (?) */
	SH_PFC_CONFIGS_GROUP_DEFAULT("sh-sci.5", "scifb1_ctrl",
				     pin_pulloff_conf),
};

#define SCIF_COMMON(scif_type, baseaddr, irq)			\
	.type		= scif_type,				\
	.mapbase	= baseaddr,				\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,	\
	.scbrr_algo_id	= SCBRR_ALGO_4,				\
	.irqs		= SCIx_IRQ_MUXED(irq),			\
	.scscr		= SCSCR_RE | SCSCR_TE,			\
	.ops		= &shmobile_sci_port_ops

#define SCIFA_DATA(baseaddr, irq)		\
	SCIF_COMMON(PORT_SCIFA, baseaddr, irq)

#define SCIFB_DATA(baseaddr, irq)		\
	SCIF_COMMON(PORT_SCIFB, baseaddr, irq)

enum { SCIFA0, SCIFA1, SCIFA2, SCIFA3, SCIFB0, SCIFB1, SCIFB2, SCIFB3 };

static const struct plat_sci_port scif[] __initconst = {
[SCIFA0] = {
	SCIFA_DATA(0xe6450000, gic_spi(102)),
},
[SCIFA1] = {
	SCIFA_DATA(0xe6c50000, gic_spi(103)),
},
[SCIFA2] = {
	SCIFA_DATA(0xe6c60000, gic_spi(104)),
},
[SCIFA3] = {
	SCIFA_DATA(0xe6c70000, gic_spi(105)),
},
[SCIFB0] = {
	SCIFB_DATA(0xe6c20000, gic_spi(107)),
	.capabilities	= SCIx_HAVE_RTSCTS,
	#ifdef CONFIG_RENESAS_BT
	.exit_lpm_cb	= bcm_bt_lpm_exit_lpm_locked,
	#endif
},
[SCIFB1] = {
	SCIFB_DATA(0xe6c30000, gic_spi(108)),
	.scbrr_algo_id	= SCBRR_ALGO_4_BIS,
	.capabilities	= SCIx_HAVE_RTSCTS,
#ifdef CONFIG_SERIAL_SH_SCI_DMA
	.dma_slave_tx = SHDMA_SLAVE_SCIF5_TX,
	.dma_slave_rx = SHDMA_SLAVE_SCIF5_RX,
#endif
},
[SCIFB2] = {
	SCIFB_DATA(0xe6ce0000, gic_spi(116)),
},
[SCIFB3] = {
	SCIFB_DATA(0xe6470000, gic_spi(117)),
}
};

static inline void r8a7373_register_scif(int idx)
{
	platform_device_register_data(&platform_bus, "sh-sci", idx, &scif[idx],
				      sizeof(struct plat_sci_port));
}

#endif /* __SETUP_U2_SCI_H__ */
