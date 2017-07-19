/*
 * Copyright 2014 Broadcom Corporation
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/leds.h>
#include <linux/earlysuspend.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/touch_notify.h>
#include <linux/err.h>
#include "../leds.h"
#include <linux/regulator/consumer.h>

struct kpbl_trig_data {
	struct delayed_work del_work;
	struct led_classdev *led_cdev;
	struct list_head list;
	unsigned int duration;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend suspend;
#endif
};

struct delayed_work disable_work;
static struct regulator *keyled_regulator;
static struct class *kpbl_class;
static struct device *kpbl_cmd_dev;
static int kpbl_state = 2;
static int key_value;
static int bl_on_duration = 3000;/*3000ms*/
static int kpbl_brightness;

#define KPBL_STATE_ALWAYS_OFF 1
#define KPBL_STATE_ON_WHEN_TOUCH 2
#define KPBL_STATE_CHANGE_WITH_LCD 3

static DEFINE_MUTEX(kpbl_lock);

static void kpbl_trig_work(struct work_struct *work);
#ifdef CONFIG_HAS_EARLYSUSPEND
static void kpbl_trig_early_suspend(struct early_suspend *h);
static void kpbl_trig_late_resume(struct early_suspend *h);
#endif

static ssize_t kpbl_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", bl_on_duration);

}

static ssize_t kpbl_duration_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	sscanf(buf, "%u", &bl_on_duration);
	return size;
}

static DEVICE_ATTR(duration, 0644, kpbl_duration_show, kpbl_duration_store);

static int touchkey_led_on(bool on)
{
	int ret;

	if (keyled_regulator == NULL) {
		keyled_regulator = regulator_get(NULL, "key_led");
		if (IS_ERR(keyled_regulator)) {
			pr_err("can not get KEY_LED_3.3V\n");
			return -1;
		}
		ret = regulator_set_voltage(keyled_regulator, 3300000, 3300000);
		pr_info("regulator_set_voltage ret = %d\n", ret);
		if (ret < 0)
			goto put;
	}
	if (on) {
		pr_info("Touchkey On\n");
		if (key_value == 0) {
			ret = regulator_enable(keyled_regulator);
			key_value = 1;
			if (ret) {
				pr_err("Enable KEY_LED fail. ret=%d\n", ret);
				goto put;
			}
		}
	} else {
		pr_info("Touchkey Off\n");
		if (key_value == 1) {
			ret = regulator_disable(keyled_regulator);
			key_value = 0;
			if (ret) {
				pr_err("Disable KEY_LED fail. ret=%d\n", ret);
				goto put;
			} else
				pr_info("disable KEY_LED success.\n");
		}
	}
	return 0;
put:
		regulator_put(keyled_regulator);
		keyled_regulator = NULL;
		return ret;

}

int kpbl_active(struct notifier_block *n, unsigned long dat, void *val)
{
	int ret;

	if (kpbl_state == KPBL_STATE_ALWAYS_OFF)
		return 0;
	mutex_lock(&kpbl_lock);
	ret  = touchkey_led_on(1);
	if (kpbl_state == KPBL_STATE_ON_WHEN_TOUCH)
		schedule_delayed_work(&disable_work,
			msecs_to_jiffies(bl_on_duration));
	mutex_unlock(&kpbl_lock);
	return ret;
}

static struct notifier_block kpbl_notify = {
	.notifier_call = kpbl_active,
};

static void kpbl_trig_work(struct work_struct *work)
{
	touchkey_led_on(0);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void kpbl_trig_early_suspend(struct early_suspend *h)
{
	touchkey_led_on(0);
}

static void kpbl_trig_late_resume(struct early_suspend *h)
{
	if (kpbl_state == KPBL_STATE_ALWAYS_OFF)
		return;
	touchkey_led_on(1);
	if (kpbl_state == KPBL_STATE_ON_WHEN_TOUCH)
		schedule_delayed_work(&disable_work,
				msecs_to_jiffies(bl_on_duration));
}
#endif

static ssize_t kpbl_state_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%d\n", kpbl_state);
}

static ssize_t kpbl_state_store(struct device *dev,
			struct device_attribute *attr, const char *buf,
				size_t size)
{
	if (buf[0] == '1')
		kpbl_state = KPBL_STATE_ALWAYS_OFF;

	else if (buf[0] == '2')
		kpbl_state = KPBL_STATE_ON_WHEN_TOUCH;

	else if (buf[0] == '3')
		kpbl_state = KPBL_STATE_CHANGE_WITH_LCD;
	else
		kpbl_state = KPBL_STATE_ON_WHEN_TOUCH;
	return size;
}

static DEVICE_ATTR(kpbl_states, 0644, kpbl_state_show, kpbl_state_store);


static ssize_t kpbl_brightness_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%d\n", kpbl_brightness);
}
static ssize_t kpbl_brightness_store(struct device *dev,
			struct device_attribute *attr, const char *buf,
				size_t size)
{
	int val;
	int err;

	err = kstrtoint(buf, 10, &val);
	if (err)
			return err;
	kpbl_brightness = val;
	if (val == 0)
		touchkey_led_on(0);
	else if (val > 0)
		touchkey_led_on(1);
	else
		pr_info("invalid input.\n");
	return size;
}

static DEVICE_ATTR(brightness, 0644, kpbl_brightness_show, kpbl_brightness_store);

static int __init kpbl_trig_init(void)
{
	struct kpbl_trig_data *data;

	data = kzalloc(sizeof(struct kpbl_trig_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	kpbl_class = class_create(THIS_MODULE, "kpbl");
	if (IS_ERR(kpbl_class)) {
		pr_err("Failed to create class!\n");
		return PTR_ERR(kpbl_class);
	}
	kpbl_cmd_dev = device_create(kpbl_class, NULL, 0, NULL, "button-backlight");
	if (IS_ERR(kpbl_cmd_dev)) {
		pr_err("Failed to create device(kpbl_cmd_dev)!\n");
		return PTR_ERR(kpbl_cmd_dev);
	}
	if (device_create_file(kpbl_cmd_dev, &dev_attr_kpbl_states) < 0)
		pr_err("Failed to create device file(%s)!\n",
			dev_attr_kpbl_states.attr.name);
	if (device_create_file(kpbl_cmd_dev, &dev_attr_duration) < 0)
		pr_err("Failed to create device file(%s)!\n",
			dev_attr_duration.attr.name);
	if (device_create_file(kpbl_cmd_dev, &dev_attr_brightness) < 0)
		pr_err("Failed to create device file(%s)!\n",
			dev_attr_duration.attr.name);
	dev_set_drvdata(kpbl_cmd_dev, NULL);

	touch_register_client(&kpbl_notify);
	INIT_DELAYED_WORK(&disable_work, kpbl_trig_work);
#ifdef CONFIG_HAS_EARLYSUSPEND
	data->suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	data->suspend.suspend = kpbl_trig_early_suspend;
	data->suspend.resume = kpbl_trig_late_resume;
	register_early_suspend(&data->suspend);
#endif
	return 0;
}

static void __exit kpbl_trig_exit(void)
{
	cancel_delayed_work(&disable_work);
	touch_unregister_client(&kpbl_notify);
}

module_init(kpbl_trig_init);
module_exit(kpbl_trig_exit);

MODULE_DESCRIPTION("BCM kpbl LED trigger");
MODULE_LICENSE("GPL");
