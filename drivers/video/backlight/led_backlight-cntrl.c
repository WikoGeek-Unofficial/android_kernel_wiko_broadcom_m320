/*
 * drivers/video/backlight/led_backlight-cntrl.c 
 *
 * Copyright (C) 2013 Renesas Mobile Corp.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <mach/r8a7373.h>

#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/wakelock.h>

#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#include <linux/tpu_pwm.h>
#include <linux/module.h>
#include <linux/mfd/tc3589x.h>
#include <linux/backlight.h>
#include <linux/led_backlight-cntrl.h>

/* Functions prototype */
static int  set_backlight_brightness(struct backlight_device *bd);
static int control_led_backlight(struct device *dev, int value);

/* Macro */

#define CLK_SOURCE_CP 0 /* CP clock Common Peripheral*/
#define CLK_SOURCE_CP_DIV4 1 /* CP/4 */
#define CLK_SOURCE_CP_DIV16 2 /* CP/16 */
#define CLK_SOURCE_CP_DIV64 3 /* CP/64 */

/* Global variables */
static struct wake_lock wakelock;
static const int periodical_cycle = 260; /* PWM periodic cycle */
static const char *tpu_channel;
DEFINE_MUTEX(led_mutex);  /* Initialize mutex */

/*
 * set_backlight_brightness: Control LCD backlight
 * @led_cdev: Pointer to LCD backlight device
 * @value: Brightness value
 * return: None
 */
static int  set_backlight_brightness(struct backlight_device *bd)
{
	int value = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK)
		value = 0;

	if (bd->props.state & BL_CORE_FBBLANK)
		value = 0;

	control_led_backlight(bd->dev.parent, value);
	return 0;
}

/*
 * control_led_backlight: Control LCD backlight using PWM signal
 * @dev: Pointer to led_backlight device
 * @value: Brightness value
 * return:
 *   + 0: Success
 *   + Others: Errors of TPU functions
 */
static int control_led_backlight(struct device *dev, int value)
{
	int ret = 0;
	int duty_cycle;
	static void *handle;
	printk("control_led_backlight %d\n", value);
	/* Calculate duty cycle basing on brightness value */
	duty_cycle = (periodical_cycle + 1) -
			(value * (periodical_cycle + 1) / 255);
	/* Enable PWM signal */
	if (value) {
		/* Open TPU channel to use, clock source is CP/4 */
		if (!handle) {
			pinctrl_pm_select_default_state(dev);
			ret = tpu_pwm_open(tpu_channel, CLK_SOURCE_CP, &handle);

			if (ret) {
				printk(KERN_ERR "tpu_pwm_open() %d\n", ret);
				return ret;
			}
		}

		/* Enable PWM signal with specific duty cycle and periodic cycle */
		ret = tpu_pwm_enable(handle, TPU_PWM_START,
					duty_cycle, periodical_cycle);
		if (ret) {
			return ret;
		}
		return ret;
	} else {
		if (handle) {
			/* Disable PWM signal */
			ret = tpu_pwm_enable(handle, TPU_PWM_STOP,
						duty_cycle, periodical_cycle);
			if (ret) {
				printk(KERN_ERR "Disable PWM signal %d\n", ret);
				return ret;
			}
			/* Close TPU channel */
			ret = tpu_pwm_close(handle);

			if (ret) {
				printk(KERN_ERR "tpu_pwm_close() %d\n", ret);
				return ret;
			}
			handle = NULL;
			pinctrl_pm_select_idle_state(dev);
		}

		return ret;
	}
}


static struct backlight_ops led_backlight_ops = {
        .update_status  = set_backlight_brightness,
};

static int parse_dt_data(struct device_node *np,
	struct backlight_properties *props)
{
	unsigned int val;
	if (of_property_read_string(np,
		"renesas,tpu-channel", &tpu_channel)) {
		pr_err("failed to get tpu_channel\n");
		return -1;
	}
	if (of_property_read_u32(np, "max-brightness", &val)) {
		pr_err("failed to get max-brightness from dt\n");
		return -1;
	}
	props->max_brightness = val;
	if (of_property_read_u32(np,
			"default-brightness", &val)) {
		pr_err("failed to get dft-brightness from dt\n");
		return -1;
	}
	props->brightness = val;

	return 0;
}

static const struct of_device_id led_backlight_of_match[] = {
	{.compatible = "renesas,tpu-led-backlight",},
	{},
};

MODULE_DEVICE_TABLE(of, led_backlight_of_match);

/*probe*/
static int led_probe(struct platform_device *pdev)
{
	struct platform_led_backlight_data *data = pdev->dev.platform_data;
	struct backlight_device *bl;
	struct backlight_properties props;
	int ret;

	memset(&props, 0, sizeof(struct backlight_properties));
	if (data) {
		props.max_brightness = data->max_brightness;
		props.brightness = data->dft_brightness;
		tpu_channel = data->tpu_channel;
	} else if (pdev->dev.of_node) {
		if (parse_dt_data(pdev->dev.of_node, &props)) {
			dev_err(&pdev->dev, "failed parse_dt_data error\n");
			return -EINVAL;
		}
	} else {
		pr_err("failed to get backlight data\n");
		return 0;
	}

	/* This will return -EPROBE_DEFER if TPU isn't ready yet */
	ret = control_led_backlight(&pdev->dev, props.brightness);
	if (ret)
		return ret;

	props.type = BACKLIGHT_PLATFORM;
	bl = backlight_device_register("panel", &pdev->dev,
			pdev, &led_backlight_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}
	platform_set_drvdata(pdev, bl);
	wake_lock_init(&wakelock, WAKE_LOCK_SUSPEND, "led_wakelock");
	return 0;
}

static int led_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);

	wake_lock_destroy(&wakelock);
	backlight_device_unregister(bl);
	control_led_backlight(&pdev->dev, 0);

	return 0;
}

static struct platform_driver led_drv = {
	.driver = {
		.name = "led-backlight",
		.of_match_table = led_backlight_of_match,
	},
	.probe = led_probe,
	.remove = led_remove,
};

module_platform_driver(led_drv);
MODULE_DESCRIPTION("led device");
MODULE_LICENSE("GPL");
