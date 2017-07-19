/*
 * hwmon.h  --  HWMON driver for Dialog Semiconductor D2153 PMIC
 *
 * Copyright 2012 Dialog Semiconductor Ltd
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __LINUX_LEOPARD_HWMON_H
#define __LINUX_LEOPARD_HWMON_H

#include <linux/platform_device.h>
#include <linux/d2153/d2153_battery.h>

struct d2153_hwmon {
	struct d2153 *d2153;
	struct device *classdev;
	struct platform_device *pdev;
	struct completion man_adc_complete;
};

#define ADC_RES_MASK_LSB					(0x0F)
#define ADC_RES_MASK_MSB					(0xF0)

int d2153_set_adc_mode(struct d2153_battery * const bdata, adc_mode const type,
					enum d2153_adc_config_mode const mode);

#endif /* __LINUX_LEOPARD_HWMON_H */
