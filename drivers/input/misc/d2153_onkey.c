/*
 * d2153_onkey.c: ON Key support for Dialog D2153
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: Dialog Semiconductor Ltd. D. Chen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>

#include <linux/d2153/pmic.h>
#include <linux/d2153/d2153_reg.h>
#include <linux/d2153/hwmon.h>
#include <linux/d2153/core.h>

#include <mach/common.h>

#define DRIVER_NAME "d2153-onkey"

static int powerkey_pressed;
static int ponkey_mode;

static struct kobject *ponkey_kobj;
static struct d2153 *pon_d2153;

static ssize_t
ponkey_mode_store(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t n)
{
	unsigned int in_ponkey_mode;
	if (!pon_d2153)
		goto err;
	if (sscanf(buf, "%d", &in_ponkey_mode) == 1) {
		if (in_ponkey_mode > 1)
			goto err;
		if (in_ponkey_mode == 0) {
			d2153_mask_irq(pon_d2153, D2153_IRQ_ENONKEY_HOLDOFF);
			d2153_unmask_irq(pon_d2153, D2153_IRQ_ENONKEY_LO);
		} else {
			d2153_unmask_irq(pon_d2153, D2153_IRQ_ENONKEY_HOLDOFF);
			d2153_mask_irq(pon_d2153, D2153_IRQ_ENONKEY_LO);
		}
		ponkey_mode = in_ponkey_mode;
		return n;
	}
err:
	pr_err("\r\nusage: \r\n"
		"set ponkey_mode : "
		"echo [ponkey_mode (0-1)] > /sys/ponkey/ponkey_mode\r\n");
	return -EINVAL;
}

static ssize_t
ponkey_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (pon_d2153)
		return sprintf(buf, "%d\n", ponkey_mode);
	return 0;
}

static DEVICE_ATTR(ponkey_mode, 0644, ponkey_mode_show, ponkey_mode_store);

static struct attribute *ponkey_attrs[] = {
	&dev_attr_ponkey_mode.attr,
	NULL,
};

static struct attribute_group ponkey_mode_attr_group = {
	.attrs = ponkey_attrs,
};

int d2153_onkey_check(void)
{
	return powerkey_pressed;
}
EXPORT_SYMBOL(d2153_onkey_check);


static irqreturn_t d2153_onkey_event_lo_handler(int irq, void *data)
{
	struct d2153 *d2153 = data;
	struct d2153_onkey *dlg_onkey = &d2153->onkey;

	/* add debug information power key for non-sense error*/
	dev_info(d2153->dev, "Onkey LO Interrupt Event generated\n");

	input_event(dlg_onkey->input, EV_KEY, KEY_POWER, 1);
	input_sync(dlg_onkey->input);

	

	powerkey_pressed = 1;

	return IRQ_HANDLED;
}

static irqreturn_t d2153_onkey_event_holdoff_handler(int irq, void *data)
{
	struct d2153 *d2153 = data;
	struct d2153_onkey *dlg_onkey = &d2153->onkey;

	/* add debug information power key for non-sense error */
	dev_info(d2153->dev, "Onkey HOLDOFF Interrupt Event generated\n");

	input_event(dlg_onkey->input, EV_KEY, KEY_POWER, 1);
	input_sync(dlg_onkey->input);

	powerkey_pressed = 1;

	return IRQ_HANDLED;
}

static irqreturn_t d2153_onkey_event_hi_handler(int irq, void *data)
{
	struct d2153 *d2153 = data;
	struct d2153_onkey *dlg_onkey = &d2153->onkey;

	/* add debug information power key for non-sense error*/
	dev_info(d2153->dev, "Onkey HI Interrupt Event generated\n");

	input_event(dlg_onkey->input, EV_KEY, KEY_POWER, 0);
	input_sync(dlg_onkey->input);

	powerkey_pressed = 0;

	return IRQ_HANDLED;
}

static int __init d2153_onkey_probe(struct platform_device *pdev)
{
	struct d2153 *d2153 = platform_get_drvdata(pdev);
	struct d2153_onkey *dlg_onkey = &d2153->onkey;
	int ret = 0;
	int error = 0;
	u8 reg =0;

	dev_info(d2153->dev, "%s() Starting Onkey Driver\n",  __FUNCTION__);

	dlg_onkey->input = input_allocate_device();
    if (!dlg_onkey->input) {
		dev_err(&pdev->dev, "failed to allocate data device\n");
		return -ENOMEM;
	}

	dlg_onkey->input->name = DRIVER_NAME;
	dlg_onkey->input->phys = "d2153-onkey/input0";
	dlg_onkey->input->id.bustype = BUS_HOST;
	dlg_onkey->input->dev.parent = &pdev->dev;
	ponkey_mode = 0;
	pon_d2153 = d2153;
	input_set_capability(dlg_onkey->input, EV_KEY, KEY_POWER);

	ret = input_register_device(dlg_onkey->input);
	if (ret) {
		dev_err(&pdev->dev, "Unable to register input device,error: %d\n", ret);
		input_free_device(dlg_onkey->input);
		return ret;
	}
        if(0 == d2153_reg_read(d2153, D2153_STATUS_B_REG, &reg))
        {
            dev_info(d2153->dev, "%s() D2153_STATUS_B_REG is  %d isPress %d\n",  __FUNCTION__,reg,!(reg  & D2153_NONKEY_MASK));

            if(!(reg  & D2153_NONKEY_MASK))
            {
                input_event(dlg_onkey->input, EV_KEY, KEY_POWER, 1);
                input_sync(dlg_onkey->input);
            }
        }        
        
	d2153_register_irq(d2153, D2153_IRQ_ENONKEY_HI,
					d2153_onkey_event_hi_handler, 0, DRIVER_NAME, d2153);
	d2153_register_irq(d2153, D2153_IRQ_ENONKEY_LO,
					d2153_onkey_event_lo_handler, 0, DRIVER_NAME, d2153);
	d2153_register_irq(d2153, D2153_IRQ_ENONKEY_HOLDOFF,
			   d2153_onkey_event_holdoff_handler, 0, DRIVER_NAME,
			   d2153);
	d2153_mask_irq(d2153, D2153_IRQ_ENONKEY_HOLDOFF);
	dev_info(d2153->dev, "Onkey Driver registered\n");
	ponkey_kobj = kobject_create_and_add("ponkey", NULL);
	if (!ponkey_kobj) {
		error = -EINVAL;
		goto err;
	}

	error = sysfs_create_group(ponkey_kobj, &ponkey_mode_attr_group);
	if (error) {
		dev_err(&pdev->dev, "failed to create attribute group: %d\n",
			error);
		kobject_put(ponkey_kobj);
		goto err;
	}
	return 0;

err:
	return error;

}

static int __exit d2153_onkey_remove(struct platform_device *pdev)
{
	struct d2153 *d2153 = platform_get_drvdata(pdev);
	struct d2153_onkey *dlg_onkey = &d2153->onkey;

#if 0	// 20130720 remove
	d2153_free_irq(d2153, D2153_IRQ_ENONKEY_LO);
	d2153_free_irq(d2153, D2153_IRQ_ENONKEY_HI);
#endif
	sysfs_remove_group(ponkey_kobj, &ponkey_mode_attr_group);
	input_unregister_device(dlg_onkey->input);
	kobject_put(ponkey_kobj);
	return 0;
}

static struct platform_driver d2153_onkey_driver = {
	.remove		= __exit_p(d2153_onkey_remove),
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	}
};

static int __init d2153_onkey_init(void)
{
	return platform_driver_probe(&d2153_onkey_driver, d2153_onkey_probe);
}

static void __exit d2153_onkey_exit(void)
{
	platform_driver_unregister(&d2153_onkey_driver);
}

module_init(d2153_onkey_init);
module_exit(d2153_onkey_exit);

MODULE_AUTHOR("Dialog Semiconductor Ltd < james.ban@diasemi.com >");
MODULE_DESCRIPTION("Onkey driver for the Dialog D2153 PMIC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
