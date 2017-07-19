/*
 * Setup platform devices needed by the renesas host
 * and/or dual-role USB controller modules based on the description
 * in flat device tree.
 *
 * Copyright 2013  Broadcom Corporation
 * Copyright (C) 2006-2009 Renesas Solutions Corp.
 * Copyright (C) 2012 Renesas Mobile Corporation
 *
 * Author : Parasuraman <parasuraman.ramalingam@broadcom.com>
 *
 * This program is free software; you can redistribute it and/ormodify
 *
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/usb/r8a66597.h>
#include <linux/usb/r8a66597_dmac.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

static bool otg_mode;

static struct r8a66597_platdata *
		r8a66597_usb_dr_of_init(struct platform_device *pdev);


struct r8a66597_usb_dev_data {
	char *dr_mode;          /* controller mode */
	char *drivers[3];       /* drivers to instantiate for this mode */
};

struct r8a66597_usb_dev_data dr_mode_data[] = {

	{
		.dr_mode = "peripheral",
		.drivers = { "r8a66597_udc", NULL, NULL, },
	},

	{
		.dr_mode = "host",
		.drivers = { "r8a66597_hcd", "tusb1211_driver", NULL, },
	},

	{
		.dr_mode = "otg",
		.drivers = { "r8a66597_hcd", "tusb1211_driver",
							"r8a66597_udc", },
	},

};

bool is_otg_enabled(void)
{
	return otg_mode;
}
EXPORT_SYMBOL_GPL(is_otg_enabled);

struct r8a66597_usb_dev_data *get_dr_mode_data(struct device_node *np)
{

	const unsigned char *prop;
	int i;

	prop = of_get_property(np, "dr_mode", NULL);
	if (prop) {

		/* update the otg_mode flag to use it for runtime check */
		if (!strcmp(prop, "otg"))
			otg_mode = true;
		else
			otg_mode = false;

		for (i = 0; i < ARRAY_SIZE(dr_mode_data); i++) {
			if (!strcmp(prop, dr_mode_data[i].dr_mode))
				return &dr_mode_data[i];
		}
	}

	pr_warn("%s: Invalid 'dr_mode' property, fallback to peripheral mode\n",
		np->full_name);
	return &dr_mode_data[0]; /* mode not specified, * use peripheral*/
}

struct platform_device *r8a66597_usb_device_register(
	struct platform_device *ofdev, struct r8a66597_platdata *pdata,
			const char *name, int id)
{
	struct platform_device *pdev;
	const struct resource *res = ofdev->resource;
	unsigned int num = ofdev->num_resources;
	int retval;

	pdev = platform_device_alloc(name, id);
	if (!pdev) {
		retval = -ENOMEM;
		goto error;
	}

	pdev->dev.parent = &ofdev->dev;

	retval = platform_device_add_data(pdev, pdata, sizeof(*pdata));
	if (retval)
		goto error;

	if (num) {
		retval = platform_device_add_resources(pdev, res, num);
		if (retval)
			goto error;
	}

	retval = platform_device_add(pdev);
	if (retval)
		goto error;

	return pdev;

error:
	platform_device_put(pdev);
	return ERR_PTR(retval);
}

static int r8a66597_usb_dr_of_probe(struct platform_device *pdev)
{
	struct platform_device *usb_dev;
	struct r8a66597_platdata *pdata = NULL;
	struct r8a66597_usb_dev_data *dev_data;
	int i;
	struct device_node *np;
	static unsigned int idx;

	/* If device tree node is present then parse and populate pdata
	* Or fallback in platform_data */

	if (pdev->dev.of_node) {
		np = pdev->dev.of_node;
		pdata = r8a66597_usb_dr_of_init(pdev);
		if (IS_ERR(pdata)) {
			dev_err(&pdev->dev, "platform data not available\n");
			return PTR_ERR(pdata);
		}
	} else {
		dev_err(&pdev->dev, "platform data not available\n");
		return -ENODEV;
	}

	if (np)
		dev_data = get_dr_mode_data(np);

	for (i = 0; i < ARRAY_SIZE(dev_data->drivers); i++) {
		if (!dev_data->drivers[i])
			continue;
		usb_dev = r8a66597_usb_device_register(pdev, pdata,
			dev_data->drivers[i], idx);
		if (IS_ERR(usb_dev)) {
			dev_err(&pdev->dev, "Can't register usb device\n");
			return PTR_ERR(usb_dev);
		}
	}
	return 0;
}

#ifdef CONFIG_OF
static struct r8a66597_platdata *
	r8a66597_usb_dr_of_init(struct platform_device *pdev) {

	struct r8a66597_platdata *pdata, *aux_pdata;
	struct device_node *np = pdev->dev.of_node;
	int on_chip, buswait, max_bufnum, dmac;

	if (!np) {
		dev_err(&pdev->dev, "device node not found\n");
		return NULL;
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "could not allocate memory for pdata\n");
		return ERR_PTR(-ENOMEM);
	}

	if (!of_property_read_u32(np, "on_chip", &on_chip))
		pdata->on_chip = on_chip;

	if (!of_property_read_u32(np, "buswait", &buswait))
		pdata->buswait = buswait;

	if (!of_property_read_u32(np, "max_bufnum", &max_bufnum))
		pdata->max_bufnum = max_bufnum;

	if (!of_property_read_u32(np, "dmac", &dmac))
		pdata->dmac = dmac;

	pdata->id_irq = of_get_named_gpio(np, "id_irq", 0);
	pdata->gpio_rst = of_get_named_gpio(np, "rst-gpios", 0);
	pdata->gpio_cs = of_get_named_gpio(np, "cs-gpios", 0);

	/* if platform data is supplied using AUXDATA, then get the callbacks
	* and other params from there.
	* TODO:This is a temporary solution until we move all the
	* dependencies from board file */

	if (pdev->dev.platform_data) {
		aux_pdata = pdev->dev.platform_data;
		pdata->is_vbus_powered = aux_pdata->is_vbus_powered;
		pdata->module_start = aux_pdata->module_start;
		pdata->module_stop = aux_pdata->module_stop;
	}
	return pdata;
}

static const struct of_device_id r8a66597_usb_dr_of_match[] = {
	{ .compatible = "renesas,r8a66597-usb" },
	{}
};
MODULE_DEVICE_TABLE(of, r8a66597_usb_dr_of_match);
#endif

static int __unregister_subdev(struct device *dev, void *d)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int r8a66597_usb_dr_of_remove(struct platform_device *ofdev)
{
	device_for_each_child(&ofdev->dev, NULL, __unregister_subdev);
	return 0;
}

static struct platform_driver r8a66597_usb_dr_of_driver = {
	.driver = {
		.name = "r8a66597_usb_dr_of_devices",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(r8a66597_usb_dr_of_match),
	},
	.probe = r8a66597_usb_dr_of_probe,
	.remove = r8a66597_usb_dr_of_remove,
};

module_platform_driver(r8a66597_usb_dr_of_driver);

MODULE_DESCRIPTION("R8A66597 USB DR OF devices driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Parasuraman Ramalingam");
