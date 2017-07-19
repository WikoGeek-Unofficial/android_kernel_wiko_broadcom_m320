/*
 * GPS GPIO	control
 *
 *	Copyright (C) 2012 Samsung,	Inc.
 *	Copyright (C) 2012 Google, Inc.
 *
 *	This program is	free software; you can redistribute	it and/or modify
 *	it under the terms of the GNU General Public License as	published by
 *	the	Free Software Foundation; either version 2 of the License, or
 *	(at	your option) any later version.
 *
 *	This program is	distributed	in the hope	that it	will be	useful,
 *	but	WITHOUT	ANY	WARRANTY; without even the implied warranty	of
 *	MERCHANTABILITY	or FITNESS FOR A PARTICULAR	PURPOSE.  See the
 *	GNU	General	Public License for more	details.
 *
 *	You	should have	received a copy	of the GNU General Public License
 *	along with this	program; if	not, write to the Free Software
 *	Foundation,	Inc., 59 Temple	Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <mach/r8a7373.h>
#include <mach/dev-gps.h>
#include <linux/bug.h>

#include <linux/sysfs.h>
#include <linux/proc_fs.h>

#include "sh-pfc.h"

struct class *gps_class;

static unsigned long pin_pulloff_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_DISABLE, 0),
};

static struct pinctrl_map gps_map[] __initdata = {
	SH_PFC_CONFIGS_PIN_HOG_DEFAULT("PORT11", pin_pulloff_conf),
	SH_PFC_CONFIGS_PIN_HOG_DEFAULT("PORT48", pin_pulloff_conf),
};

void __init gps_pinconf_init(void)
{
	sh_pfc_register_mappings(gps_map, ARRAY_SIZE(gps_map));
}

void __init gps_gpio_init(void)
{
	struct device *gps_dev;
	unsigned long flags;

	gps_class =	class_create(THIS_MODULE, "gps");
	if (IS_ERR(gps_class)) {
		pr_err("Failed to create class(sec)!\n");
		return;
	}
	BUG_ON(!gps_class);

	gps_dev	= device_create(gps_class, NULL, 0,	NULL, "bcm4752");
	BUG_ON(!gps_dev);

	printk("gps_gpio_init!");

	/* GPS Settings	*/
	flags = GPIOF_OUT_INIT_LOW | GPIOF_EXPORT_DIR_CHANGEABLE;
	gpio_request_one(GPIO_PORT11, flags, "GPS_PWR_EN");
	gpio_request_one(GPIO_PORT48, flags, "GPS_ECLK_26M");

	gpio_export_link(gps_dev, "GPS_PWR_EN",	GPIO_PORT11);
	gpio_export_link(gps_dev, "GPS_ECLK_26M", GPIO_PORT48);

	printk("gps_gpio_init done!");
}
