/*
 * SuperH Pin Function Controller support.
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __SH_PFC_CORE_H__
#define __SH_PFC_CORE_H__

#include <linux/compiler.h>
#include <linux/types.h>

#include "sh_pfc.h"

struct sh_pfc_window {
	phys_addr_t phys;
	void __iomem *virt;
	unsigned long size;
};

struct sh_pfc_chip;
struct sh_pfc_pinctrl;

struct sh_pfc {
	struct device *dev;
	const struct sh_pfc_soc_info *info;
	void *soc_data;
	spinlock_t lock;
	struct hwspinlock *hwlock;

	unsigned int num_windows;
	struct sh_pfc_window *window;

	unsigned int nr_pins;

	struct sh_pfc_chip *gpio;
	struct sh_pfc_chip *func;

	struct sh_pfc_pinctrl *pinctrl;
};

int sh_pfc_register_gpiochip(struct sh_pfc *pfc);
int sh_pfc_register_gpio_pin_mappings(struct sh_pfc *pfc);
int sh_pfc_unregister_gpiochip(struct sh_pfc *pfc);

int sh_pfc_register_pinctrl(struct sh_pfc *pfc);
int sh_pfc_unregister_pinctrl(struct sh_pfc *pfc);

unsigned long sh_pfc_read_raw_reg(void __iomem *mapped_reg,
				  unsigned long reg_width);
void sh_pfc_write_raw_reg(void __iomem *mapped_reg, unsigned long reg_width,
			  unsigned long data);

int sh_pfc_get_pin_index(struct sh_pfc *pfc, unsigned int pin);
int sh_pfc_pin_to_irq(struct sh_pfc *pfc, unsigned int pin);
int sh_pfc_config_mux(struct sh_pfc *pfc, unsigned mark, int pinmux_type);
#ifdef CONFIG_GPIO_SH_PFC
void sh_pfc_pin_set_value(struct sh_pfc *pfc, unsigned int pin, int value);
int sh_pfc_pin_get_value(struct sh_pfc *pfc, unsigned int pin);
#else
static inline void sh_pfc_pin_set_value(struct sh_pfc *pfc,
					unsigned int pin, int value) { }
static inline int sh_pfc_pin_get_value(struct sh_pfc *pfc, unsigned int pin)
					{ return 0; }
#endif

extern const struct sh_pfc_soc_info r8a7373_pinmux_info;
extern const struct sh_pfc_soc_info r8a73a4_pinmux_info;
extern const struct sh_pfc_soc_info r8a7740_pinmux_info;
extern const struct sh_pfc_soc_info r8a7779_pinmux_info;
extern const struct sh_pfc_soc_info sh7203_pinmux_info;
extern const struct sh_pfc_soc_info sh7264_pinmux_info;
extern const struct sh_pfc_soc_info sh7269_pinmux_info;
extern const struct sh_pfc_soc_info sh7372_pinmux_info;
extern const struct sh_pfc_soc_info sh73a0_pinmux_info;
extern const struct sh_pfc_soc_info sh7720_pinmux_info;
extern const struct sh_pfc_soc_info sh7722_pinmux_info;
extern const struct sh_pfc_soc_info sh7723_pinmux_info;
extern const struct sh_pfc_soc_info sh7724_pinmux_info;
extern const struct sh_pfc_soc_info sh7734_pinmux_info;
extern const struct sh_pfc_soc_info sh7757_pinmux_info;
extern const struct sh_pfc_soc_info sh7785_pinmux_info;
extern const struct sh_pfc_soc_info sh7786_pinmux_info;
extern const struct sh_pfc_soc_info shx3_pinmux_info;

#endif /* __SH_PFC_CORE_H__ */
