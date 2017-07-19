/*
 * Copyright 2012 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * A copy of the GPL is available at
 * http://www.broadcom.com/licenses/GPLv2.php, or by writing to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/module.h>


struct cmt_dev_data {
	char *timer_type;
	char *drivers[2];
};

struct cmt_dev_data timer_type_data[] = {
	{
		.timer_type = "rtc",
		.drivers = {"rtc_u2", NULL},
	},
};

static struct cmt_dev_data *get_timer_type_data(struct device_node *np)
{
	const char *type;
	int i;
	int ret;

	ret = of_property_read_string(np, "renesas,linux-timer-type", &type);
	if (!ret) {
		for (i = 0; i < ARRAY_SIZE(timer_type_data); i++) {
			if (!strcmp(type, timer_type_data[i].timer_type))
				return &timer_type_data[i];
		}
	}

	pr_info("%s: unknown linux-timer-type.\n", np->full_name);
	return NULL;
}

static int sh_cmt_device_register(struct platform_device *ofdev,
	const char *name)
{
	struct platform_device *pdev;
	const struct resource *res = ofdev->resource;
	unsigned int num = ofdev->num_resources;
	int ret = 0;

	pdev = platform_device_alloc(name, ofdev->id);
	if (!pdev) {
		ret = -ENOMEM;
		goto err;
	}

	pdev->dev.parent = &ofdev->dev;

	if (num) {
		ret = platform_device_add_resources(pdev, res, num);
		if (ret) {
			dev_err(&ofdev->dev, "failed to add resources\n");
			goto err;
		}
	}

	ret = platform_device_add(pdev);
	if (ret) {
		dev_err(&ofdev->dev, "failed to add device %s\n", name);
		goto err;
	}

	return ret;
err:
	platform_device_put(pdev);
	return ret;
}

static int sh_cmt_of_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct cmt_dev_data *dev_data = NULL;
	int i;
	int ret;

	if (np)
		dev_data = get_timer_type_data(np);

	if (!dev_data)
		return -ENODEV;

	ret = of_property_read_u32_index(np, "renesas,clk-enable-bit",
		0, &pdev->id);
	if (ret < 0) {
		pr_info("%s: no clk-enable-bit.\n", np->full_name);
		pdev->id = -1;
	}

	for (i = 0; i < ARRAY_SIZE(dev_data->drivers); i++) {
		if (!dev_data->drivers[i])
			continue;
		ret = sh_cmt_device_register(pdev, dev_data->drivers[i]);
		if (ret) {
			dev_err(&pdev->dev, "failed to register device %s\n",
				dev_data->drivers[i]);
			return -ENODEV;
		}
		pr_info("%s: cmt.%d registered as %s.\n", np->full_name,
			pdev->id, dev_data->drivers[i]);
	}

	return 0;
}

static int __unregister_subdev(struct device *dev, void *d)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int sh_cmt_of_remove(struct platform_device *ofdev)
{
	device_for_each_child(&ofdev->dev, NULL, __unregister_subdev);
	return 0;
}

static const struct of_device_id sh_cmt_of_match[] = {
	{ .compatible = "renesas,cmt-r8a7373" },
	{ .compatible = "renesas,cmt-shmobile" },
	{}
};
MODULE_DEVICE_TABLE(of, sh_cmt_of_match);

static struct platform_driver sh_cmt_of_driver = {
	.driver = {
		.name = "sh_cmt_of_devices",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sh_cmt_of_match),
	},
	.probe = sh_cmt_of_probe,
	.remove = sh_cmt_of_remove,
};

static int __init sh_cmt_of_init(void)
{
	return platform_driver_register(&sh_cmt_of_driver);
}

static void __exit sh_cmt_of_exit(void)
{
	platform_driver_unregister(&sh_cmt_of_driver);
}

subsys_initcall(sh_cmt_of_init);
module_exit(sh_cmt_of_exit);

MODULE_DESCRIPTION("SH CMT OF devices driver");
MODULE_LICENSE("GPL");


