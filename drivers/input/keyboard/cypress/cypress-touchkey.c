/*
 * Driver for keys on GPIO lines capable of generating interrupts.
 *
 * Copyright 2005 Phil Blundell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <mach/common.h>

#if 0
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>
#include <mach/sec-sysfs.h>
#else
#include <mach/gpio.h>
#endif
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/firmware.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include <linux/i2c/cypress_touchkey.h>

#ifdef  TK_HAS_FIRMWARE_UPDATE
u8 *tk_fw_name = FW_PATH;
static u8 fw_ver_file = 0x10;
static u8 md_ver_file = 0x9;
/*u8 module_divider[] = {0, 0x04, 0x07, 0x08, 0xff};*/
u8 module_divider[] = {0, 0xff};
u8 *firmware_data;
#endif

static int touchkey_keycode[] = { 0,
#if defined(TK_USE_4KEY_TYPE_ATT)
	KEY_MENU, KEY_HOMEPAGE, KEY_BACK, KEY_SEARCH,

#elif defined(TK_USE_4KEY_TYPE_NA)
	KEY_SEARCH, KEY_BACK, KEY_HOMEPAGE, KEY_MENU,

#elif defined(TK_USE_2KEY_TYPE_M0)
	KEY_BACK, KEY_MENU,

#else
	KEY_MENU, KEY_BACK,

#endif
};
static const int touchkey_count = ARRAY_SIZE(touchkey_keycode);

static u16 raw_data0;
static u16 raw_data1;
static u16 raw_data2;
static u16 raw_data3;

static u16 diff_data0;
static u16 diff_data1;
static u16 diff_data2;
static u16 diff_data3;

static u16 baseline_data0;
static u16 baseline_data1;
static u16 baseline_data2;
static u16 baseline_data3;

static u8 Compidac0;
static u8 Compidac1;
static u8 Compidac2;
static u8 Compidac3;

static u8 idac0;
static u8 idac1;
static u8 idac2;
static u8 idac3;

static u8 touchkey_threshold;
static u8 touchkey_threshold2;
static u8 touchkey_threshold3;
static u8 touchkey_threshold4;

static int touchkey_i2c_check(struct touchkey_i2c *tkey_i2c);

static u16 menu_sensitivity;
static u16 back_sensitivity;
#if defined(TK_USE_4KEY)
static u8 home_sensitivity;
static u8 search_sensitivity;
#endif

static bool touchkey_probe;

static const struct i2c_device_id sec_touchkey_id[] = {
	{"sec_touchkey_driver", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sec_touchkey_id);

extern int get_touchkey_firmware(char *version);
static int touchkey_led_status;
static int touchled_cmd_reversed;

#ifdef LED_LDO_WITH_REGULATOR
static void change_touch_key_led_voltage(int vol_mv)
{
	struct regulator *tled_regulator = NULL;

#if 0
	tled_regulator = regulator_get(NULL, TK_LED_REGULATOR_NAME);
#else
	//tled_regulator = regulator_get(NULL, TK_LED_REGULATOR_NAME);

#endif
	if (IS_ERR(tled_regulator)) {
		pr_err("%s: failed to get resource %s\n", __func__,
		       "touchkey_led");
		return;
	}
	regulator_set_voltage(tled_regulator, vol_mv * 1000, vol_mv * 1000);
	regulator_put(tled_regulator);
}

static ssize_t brightness_control(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int data;

	if (sscanf(buf, "%d\n", &data) == 1) {
		dev_err(dev, "%s: %d\n", __func__, data);
		change_touch_key_led_voltage(data);
	} else {
		dev_err(dev, "%s Error\n", __func__);
	}

	return size;
}
#endif

static int i2c_touchkey_read(struct i2c_client *client,
		u8 reg, u8 *val, unsigned int len)
{
	int err = 0;
	int retry = 3;
#if !defined(TK_USE_GENERAL_SMBUS)
	struct i2c_msg msg[1];
#endif
	struct touchkey_i2c *tkey_i2c = i2c_get_clientdata(client);

	if ((client == NULL) || !(tkey_i2c->enabled)) {
		dev_err(&client->dev, "Touchkey is not enabled. %d\n",
		       __LINE__);
		return -ENODEV;
	}

	while (retry--) {
#if defined(TK_USE_GENERAL_SMBUS)
		err = i2c_smbus_read_i2c_block_data(client,
				KEYCODE_REG, len, val);
#else
		msg->addr = client->addr;
		msg->flags = I2C_M_RD;
		msg->len = len;
		msg->buf = val;
		err = i2c_transfer(client->adapter, msg, 1);
#endif

		if (err >= 0)
			return 0;
		dev_err(&client->dev, "%s %d i2c transfer error\n",
		       __func__, __LINE__);
		mdelay(10);
	}
	return err;

}

static int i2c_touchkey_write(struct i2c_client *client,
		u8 *val, unsigned int len)
{
	int err = 0;
	int retry = 3;
#if !defined(TK_USE_GENERAL_SMBUS)
	struct i2c_msg msg[1];
#endif
	struct touchkey_i2c *tkey_i2c = i2c_get_clientdata(client);

	if ((client == NULL) || !(tkey_i2c->enabled)) {
		dev_err(&client->dev, "Touchkey is not enabled. %d\n",
		       __LINE__);
		return -ENODEV;
	}

	while (retry--) {
#if defined(TK_USE_GENERAL_SMBUS)
		err = i2c_smbus_write_i2c_block_data(client,
				BASE_REG, len, val);
#else
		msg->addr = client->addr;
		msg->flags = I2C_M_WR;
		msg->len = len;
		msg->buf = val;
		err = i2c_transfer(client->adapter, msg, 1);
#endif

		if (err >= 0)
			return 0;

		dev_err(&client->dev, "%s %d i2c transfer error\n",
		       __func__, __LINE__);
		mdelay(10);
	}
	return err;
}

#if defined(TK_INFORM_CHARGER)
static int touchkey_ta_setting(struct touchkey_i2c *tkey_i2c)
{
	u8 data[6] = { 0, };
	int count = 0;
	int ret = 0;
	unsigned short retry = 0;

    ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG, data, 2);
    if (ret < 0) {
        dev_err(&tkey_i2c->client->dev, "%s: Failed to read Keycode_reg %d times\n",
            __func__, retry);
        return ret;
    }
    dev_dbg(&tkey_i2c->client->dev,
            "data[0]=%x, data[1]=%x\n",
            data[0], data[1]);

    if (tkey_i2c->charging_mode) {
        dev_info(&tkey_i2c->client->dev, "%s: TA connected!!!\n", __func__);
        data[1] |= TK_BIT_CMD_TA_ON;
    } else {
        dev_info(&tkey_i2c->client->dev, "%s: TA disconnected!!!\n", __func__);
        data[1] &= ~TK_BIT_CMD_TA_ON;
    }

    count = i2c_touchkey_write(tkey_i2c->client, data, 2);


	while (retry < 3) {

		msleep(30);

		ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x07 , data, 1);

		if (tkey_i2c->charging_mode) {
			if (data[0] & TK_BIT_TA_ON) {
				dev_dbg(&tkey_i2c->client->dev, "%s: TA mode is Enabled\n", __func__);
				break;
			} else {
				dev_err(&tkey_i2c->client->dev, "%s: Error to enable TA mode, retry %d\n",
					__func__, retry);
			}
		} else {
			if (!(data[0] & TK_BIT_TA_ON)) {
				dev_dbg(&tkey_i2c->client->dev, "%s: TA mode is Disabled\n", __func__);
				break;
			} else {
				dev_err(&tkey_i2c->client->dev, "%s: Error to disable TA mode, retry %d\n",
					__func__, retry);
			}
		}
		retry = retry + 1;
	}

	if (retry == 3)
		dev_err(&tkey_i2c->client->dev, "%s: Failed to set the TA mode\n", __func__);

	return count;

}

static void touchkey_ta_cb(struct touchkey_callbacks *cb, bool ta_status)
{
	struct touchkey_i2c *tkey_i2c =
			container_of(cb, struct touchkey_i2c, callbacks);
	struct i2c_client *client = tkey_i2c->client;

	tkey_i2c->charging_mode = ta_status;

	if (tkey_i2c->enabled)
		touchkey_ta_setting(tkey_i2c);
}
#endif
#if defined(CONFIG_GLOVE_TOUCH)
static void touchkey_glove_change_work(struct work_struct *work)
{
	u8 data[6] = { 0, };
	int ret = 0;
	unsigned short retry = 0;
	bool value;
	u8 glove_bit;
	struct touchkey_i2c *tkey_i2c =
			container_of(work, struct touchkey_i2c,
			glove_change_work.work);

#ifdef TKEY_FLIP_MODE
	if (tkey_i2c->enabled_flip) {
		dev_info(&tkey_i2c->client->dev,"As flip cover mode enabled, skip glove mode set\n");
		return;
	}
#endif

	mutex_lock(&tkey_i2c->tsk_glove_lock);
	value = tkey_i2c->tsk_glove_mode_status;
	mutex_unlock(&tkey_i2c->tsk_glove_lock);

	if (!tkey_i2c->enabled)
		return;

    ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG, data, 2);
    if (ret < 0) {
        dev_err(&tkey_i2c->client->dev, "%s: Failed to read Keycode_reg %d times\n",
            __func__, retry);
        return;
    }

    dev_dbg(&tkey_i2c->client->dev,
            "data[0]=%x, data[1]=%x\n",
            data[0], data[1]);


    if (value) {
        /* Send glove Command */
        data[1] |= TK_BIT_CMD_GLOVE;
    } else {
        data[1] &= ~TK_BIT_CMD_GLOVE;
    }

    i2c_touchkey_write(tkey_i2c->client, data, 2);

	while (retry < 3) {

        msleep(30);

		ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x07 , data, 1);

		glove_bit = !!(data[0] & TK_BIT_GLOVE);

		if (value == glove_bit) {
			dev_dbg(&tkey_i2c->client->dev, "%s:Glove mode is %s\n",
				__func__, value ? "enabled" : "disabled");
			break;
		} else
			dev_err(&tkey_i2c->client->dev, "%s:Error to set glove_mode val %d, bit %d, retry %d\n",
				__func__, value, glove_bit, retry);

		retry = retry + 1;
	}
	if (retry == 3)
		dev_err(&tkey_i2c->client->dev, "%s: Failed to set the glove mode\n", __func__);
}

static struct touchkey_i2c *tkey_i2c_global;

void touchkey_glovemode(int on)
{
	struct touchkey_i2c *tkey_i2c = tkey_i2c_global;

	if (!touchkey_probe) {
		dev_err(&tkey_i2c->client->dev, "%s: Touchkey is not probed\n", __func__);
		return;
	}
	if (wake_lock_active(&tkey_i2c->fw_wakelock)) {
		printk(KERN_DEBUG"touchkey:wackelock active\n");
		return ;
	}

	mutex_lock(&tkey_i2c->tsk_glove_lock);

	/* protect duplicated execution */
	if (on == tkey_i2c->tsk_glove_mode_status) {
		dev_info(&tkey_i2c->client->dev, "pass. cmd %d, cur status %d\n",
			on, tkey_i2c->tsk_glove_mode_status);
		goto end_glovemode;
	}

	cancel_delayed_work(&tkey_i2c->glove_change_work);

	tkey_i2c->tsk_glove_mode_status = on;
	schedule_delayed_work(&tkey_i2c->glove_change_work,
		msecs_to_jiffies(TK_GLOVE_DWORK_TIME));

	dev_info(&tkey_i2c->client->dev, "Touchkey glove %s\n", on ? "On" : "Off");

 end_glovemode:
	mutex_unlock(&tkey_i2c->tsk_glove_lock);
}
#endif

#ifdef TKEY_FLIP_MODE
void touchkey_flip_cover(int value)
{
	struct touchkey_i2c *tkey_i2c = tkey_i2c_global;
	u8 data[6] = { 0, };
	int count = 0;
	int ret = 0;
	unsigned short retry = 0;
	u8 flip_status;

	tkey_i2c->enabled_flip = value;

	if (!touchkey_probe) {
		dev_err(&tkey_i2c->client->dev, "%s: Touchkey is not probed\n", __func__);
		return;
	}

	if (!tkey_i2c->enabled) {
		dev_err(&tkey_i2c->client->dev, "%s: Touchkey is not enabled\n", __func__);
		return;
	}
	if (wake_lock_active(&tkey_i2c->fw_wakelock)) {
		printk(KERN_DEBUG"touchkey:wackelock active\n");
		return ;
	}

#if 0
	if (tkey_i2c->firmware_id != TK_MODULE_20065) {
		dev_err(&tkey_i2c->client->dev,
				"%s: Do not support old module\n",
				__func__);
		return;
	}
#endif

    ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG, data, 2);
    if (ret < 0) {
        dev_err(&tkey_i2c->client->dev, "%s: Failed to read Keycode_reg %d times\n",
            __func__, retry);
        return;
    }

    dev_dbg(&tkey_i2c->client->dev,
            "data[0]=%x, data[1]=%x\n",
            data[0], data[1]);

    if (value == 1) {
        /* Send filp mode Command */
            data[1] |= TK_BIT_CMD_FLIP;
    } else {
            data[1] &= ~TK_BIT_CMD_FLIP;
    }

    i2c_touchkey_write(tkey_i2c->client, data, 2);

	while (retry < 3) {

		msleep(20);

		/* Check status */
        ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x07 , data, 1);
		flip_status = !!(data[0] & TK_BIT_FLIP);

		dev_dbg(&tkey_i2c->client->dev,
				"data[0]=%x",data[0] & TK_BIT_FLIP);

		if (value == flip_status) {
			dev_dbg(&tkey_i2c->client->dev, "%s: Flip mode is %s\n", __func__, flip_status ? "enabled" : "disabled");
			break;
		} else
			dev_err(&tkey_i2c->client->dev, "%s: Error to set Flip mode, val %d, flip bit %d, retry %d\n",
				__func__, value, flip_status, retry);

		retry = retry + 1;
	}

	if (retry == 3)
		dev_err(&tkey_i2c->client->dev, "%s: Failed to set the Flip mode\n", __func__);

	return;
}
#endif

static int touchkey_enable_status_update(struct touchkey_i2c *tkey_i2c) // inspection mode
{
	u8 data[2] = { 0, };
	int ret = 0;

    ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG, data, 2);
    if (ret < 0) {
#if 0
        dev_err(&tkey_i2c->client->dev, "%s: Failed to read Keycode_reg %d times\n",
            __func__, retry);
#endif
	    dev_err(&tkey_i2c->client->dev, "%s: Failed to read Keycode_reg\n",
            __func__);

        return ret;
    }

    dev_dbg(&tkey_i2c->client->dev,
            "data[0]=%x, data[1]=%x\n",
            data[0], data[1]);

    data[1] |= TK_BIT_CMD_INSPECTION;

    ret = i2c_touchkey_write(tkey_i2c->client, data, 2);
	if (ret < 0) {
		dev_err(&tkey_i2c->client->dev, "%s, err(%d)\n", __func__, ret);
		tkey_i2c->status_update = false;
		return ret;
	}

	tkey_i2c->status_update = true;
	dev_dbg(&tkey_i2c->client->dev, "%s\n", __func__);

	msleep(20);

	return 0;
}

static ssize_t touchkey_raw_data0_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x1E, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[0] =%x,data[1] = %x\n",
			__func__, data[0], data[1]);
	raw_data0 = ((0x00FF & data[0]) << 8) | data[1]; /* menu*/

	return sprintf(buf, "%d\n", raw_data0);
}

static ssize_t touchkey_raw_data1_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x1E, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[2] =%x,data[3] = %x\n",
			__func__, data[2], data[3]);
	raw_data1 = ((0x00FF & data[2]) << 8) | data[3]; /* back*/

	return sprintf(buf, "%d\n", raw_data1);
}

static ssize_t touchkey_raw_data2_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x1E, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[4] =%x,data[5] = %x\n",
			__func__, data[4], data[5]);
	raw_data2 = ((0x00FF & data[4]) << 8) | data[5];

	return sprintf(buf, "%d\n", raw_data2);
}

static ssize_t touchkey_raw_data3_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x1E, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[6] =%x,data[7] = %x\n",
			__func__, data[6], data[7]);
	raw_data3 = ((0x00FF & data[6]) << 8) | data[7];

	return sprintf(buf, "%d\n", raw_data3);
}

static ssize_t touchkey_diff_data0_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x16, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[0] =%x,data[1] = %x\n",
			__func__, data[0], data[1]);
	diff_data0 = ((0x00FF & data[0]) << 8) | data[1]; /* menu*/

	return sprintf(buf, "%d\n", diff_data0);
}

static ssize_t touchkey_diff_data1_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x16, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[2] =%x,data[3] = %x\n",
			__func__, data[2], data[3]);
	diff_data1 = ((0x00FF & data[2]) << 8) | data[3]; /* back*/

	return sprintf(buf, "%d\n", diff_data1);
}

static ssize_t touchkey_diff_data2_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x16, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[4] =%x,data[5] = %x\n",
			__func__, data[4], data[5]);
	diff_data2 = ((0x00FF & data[4]) << 8) | data[5];

	return sprintf(buf, "%d\n", diff_data2);
}

static ssize_t touchkey_diff_data3_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x16, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[6] =%x,data[7] = %x\n",
			__func__, data[6], data[7]);
	diff_data3 = ((0x00FF & data[6]) << 8) | data[7];

	return sprintf(buf, "%d\n", diff_data3);
}

static ssize_t touchkey_baseline_data0_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x26, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[0] =%x,data[1] = %x\n",
			__func__, data[0], data[1]);
	baseline_data0 = ((0x00FF & data[0]) << 8) | data[1]; /* menu*/

	return sprintf(buf, "%d\n", baseline_data0);
}

static ssize_t touchkey_baseline_data1_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x26, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[2] =%x,data[3] = %x\n",
			__func__, data[2], data[3]);
	baseline_data1 = ((0x00FF & data[2]) << 8) | data[3]; /* back*/

	return sprintf(buf, "%d\n", baseline_data1);
}

static ssize_t touchkey_baseline_data2_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x26, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[4] =%x,data[5] = %x\n",
			__func__, data[4], data[5]);
	baseline_data2 = ((0x00FF & data[4]) << 8) | data[5];

	return sprintf(buf, "%d\n", baseline_data2);
}

static ssize_t touchkey_baseline_data3_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[8] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x26, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[6] =%x,data[7] = %x\n",
			__func__, data[6], data[7]);
	baseline_data3 = ((0x00FF & data[6]) << 8) | data[7];

	return sprintf(buf, "%d\n", baseline_data3);
}

static ssize_t touchkey_idac0_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x0D, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[0] =%d\n", __func__, data[0]);
	idac0 = data[0];
	return sprintf(buf, "%d\n", idac0);
}

static ssize_t touchkey_idac1_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x0D, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[1] =%d\n", __func__, data[1]);

	idac1 = data[1];
	return sprintf(buf, "%d\n", idac1);
}

static ssize_t touchkey_idac2_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x0D, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[2] =%d\n", __func__, data[2]);

	idac2 = data[2];
	return sprintf(buf, "%d\n", idac2);
}

static ssize_t touchkey_idac3_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x0D, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[3] =%d\n", __func__, data[3]);

	idac3 = data[3];
	return sprintf(buf, "%d\n", idac3);
}

static ssize_t touchkey_comp_idac0_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x11, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[0] =%d\n", __func__, data[0]);
	Compidac0 = data[0];
	return sprintf(buf, "%d\n", Compidac0);
}

static ssize_t touchkey_comp_idac1_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x11, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[1] =%d\n", __func__, data[1]);

	Compidac1 = data[1];
	return sprintf(buf, "%d\n", Compidac1);
}

static ssize_t touchkey_comp_idac2_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x11, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[2] =%d\n", __func__, data[2]);

	Compidac2 = data[2];
	return sprintf(buf, "%d\n", Compidac2);
}

static ssize_t touchkey_comp_idac3_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x11, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[3] =%d\n", __func__, data[3]);

	Compidac3 = data[3];
	return sprintf(buf, "%d\n", Compidac3);
}

static ssize_t touchkey_threshold_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x09, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[4] = %d\n", __func__, data[0]);
	touchkey_threshold = data[0];
	return sprintf(buf, "%d\n", touchkey_threshold);
}

static ssize_t touchkey_threshold2_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x09, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[4] = %d\n", __func__, data[1]);
	touchkey_threshold2 = data[1];
	return sprintf(buf, "%d\n", touchkey_threshold2);
}

static ssize_t touchkey_threshold3_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x09, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[4] = %d\n", __func__, data[2]);
	touchkey_threshold3 = data[2];
	return sprintf(buf, "%d\n", touchkey_threshold3);
}

static ssize_t touchkey_threshold4_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[4];
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x09, data, 4);
	dev_dbg(&tkey_i2c->client->dev, "called %s data[4] = %d\n", __func__, data[3]);
	touchkey_threshold4 = data[3];
	return sprintf(buf, "%d\n", touchkey_threshold4);
}


#ifdef TOUCHKEY_BOOSTER

#define set_qos(req, pm_qos_class, value) { \
	if (pm_qos_request_active(req)) \
		pm_qos_update_request(req, value); \
	else \
		pm_qos_add_request(req, pm_qos_class, value); \
}

#define remove_qos(req) { \
	if (pm_qos_request_active(req)) \
	pm_qos_remove_request(req); \
}

static void touchkey_change_dvfs_lock(struct work_struct *work)
{
	struct touchkey_i2c *tkey_i2c =
			container_of(work,
				struct touchkey_i2c, tsk_work_dvfs_chg.work);

	mutex_lock(&tkey_i2c->tsk_dvfs_lock);

#if 0
	if (TKEY_BOOSTER_LEVEL1 == tkey_i2c->boost_level) {
		set_qos(&tkey_i2c->cpu_qos, PM_QOS_CPU_FREQ_MIN, TKEY_BOOSTER_CPU_FREQ1);
		set_qos(&tkey_i2c->mif_qos, PM_QOS_BUS_THROUGHPUT, TKEY_BOOSTER_MIF_FREQ1);
		set_qos(&tkey_i2c->int_qos, PM_QOS_DEVICE_THROUGHPUT, TKEY_BOOSTER_INT_FREQ1);
	} else {
		set_qos(&tkey_i2c->cpu_qos, PM_QOS_CPU_FREQ_MIN, TKEY_BOOSTER_CPU_FREQ2);
		set_qos(&tkey_i2c->mif_qos, PM_QOS_BUS_THROUGHPUT, TKEY_BOOSTER_MIF_FREQ2);
		set_qos(&tkey_i2c->int_qos, PM_QOS_DEVICE_THROUGHPUT, TKEY_BOOSTER_INT_FREQ2);
	}
#endif

	printk(KERN_DEBUG"touchkey:DVFS ON, %d\n", tkey_i2c->boost_level);
	tkey_i2c->tsk_dvfs_lock_status = true;
	tkey_i2c->dvfs_signal = false;
	mutex_unlock(&tkey_i2c->tsk_dvfs_lock);
}

static void touchkey_set_dvfs_off(struct work_struct *work)
{
	struct touchkey_i2c *tkey_i2c =
				container_of(work,
					struct touchkey_i2c, tsk_work_dvfs_off.work);

	mutex_lock(&tkey_i2c->tsk_dvfs_lock);

	remove_qos(&tkey_i2c->cpu_qos);
	remove_qos(&tkey_i2c->mif_qos);
	remove_qos(&tkey_i2c->int_qos);

	tkey_i2c->tsk_dvfs_lock_status = false;
	tkey_i2c->dvfs_signal = false;
	mutex_unlock(&tkey_i2c->tsk_dvfs_lock);

	printk(KERN_DEBUG"touchkey:DVFS Off, %d\n", tkey_i2c->boost_level);
}

static void touchkey_set_dvfs_lock(struct touchkey_i2c *tkey_i2c,
					uint32_t on)
{
	if (TKEY_BOOSTER_DISABLE == tkey_i2c->boost_level)
		return;

	mutex_lock(&tkey_i2c->tsk_dvfs_lock);
	if (on == 0) {
		if (tkey_i2c->dvfs_signal) {
			cancel_delayed_work(&tkey_i2c->tsk_work_dvfs_chg);
			schedule_delayed_work(&tkey_i2c->tsk_work_dvfs_chg, 0);
			schedule_delayed_work(&tkey_i2c->tsk_work_dvfs_off,
				msecs_to_jiffies(TKEY_BOOSTER_OFF_TIME));
		} else if (tkey_i2c->tsk_dvfs_lock_status) {
			schedule_delayed_work(&tkey_i2c->tsk_work_dvfs_off,
				msecs_to_jiffies(TKEY_BOOSTER_OFF_TIME));
		}
	} else if (on == 1) {
		cancel_delayed_work(&tkey_i2c->tsk_work_dvfs_off);
		if (!tkey_i2c->tsk_dvfs_lock_status && !tkey_i2c->dvfs_signal) {
			schedule_delayed_work(&tkey_i2c->tsk_work_dvfs_chg,
							msecs_to_jiffies(TKEY_BOOSTER_ON_TIME));
			tkey_i2c->dvfs_signal = true;
		}
	} else if (on == 2) {
		if (tkey_i2c->tsk_dvfs_lock_status) {
			cancel_delayed_work(&tkey_i2c->tsk_work_dvfs_off);
			cancel_delayed_work(&tkey_i2c->tsk_work_dvfs_chg);
			schedule_work(&tkey_i2c->tsk_work_dvfs_off.work);
		}
	}
	mutex_unlock(&tkey_i2c->tsk_dvfs_lock);
}


static int touchkey_init_dvfs(struct touchkey_i2c *tkey_i2c)
{
	mutex_init(&tkey_i2c->tsk_dvfs_lock);

	INIT_DELAYED_WORK(&tkey_i2c->tsk_work_dvfs_off, touchkey_set_dvfs_off);
	INIT_DELAYED_WORK(&tkey_i2c->tsk_work_dvfs_chg, touchkey_change_dvfs_lock);

	tkey_i2c->tsk_dvfs_lock_status = false;
	return 0;
}
#endif

#if defined(TK_HAS_FIRMWARE_UPDATE)
/*static int get_module_ver(void)
{
	if (likely(system_rev >= TKEY_MODULE07_HWID))
		return 0x09;
	else
		return 0x04;
}
*/
#if 0
static void touchkey_init_fw_name(struct touchkey_i2c *tkey_i2c)
{
#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
	/* check module ver of ic */
/*	if (tkey_i2c->md_ver_ic == 0) {
		tkey_i2c->md_ver_ic = get_module_ver();
		printk(KERN_DEBUG"touchkey:failed to read module_ver. seperate by hwid(ver %x)\n", tkey_i2c->md_ver_ic);
	}
*/
	/*set fw by module_ver*/
	switch(tkey_i2c->md_ver_ic) {
	case 0x08:
	case 0x09:
	case 0x0A:
		break;
	case 0x07:
		tk_fw_name = "cypress/cypress_ha_m07.fw";
		fw_ver_file = 0xA;
		md_ver_file = 0x7;
		break;
	default:
		printk(KERN_DEBUG"touchkey:%s, unknown module ver %x\n", __func__, tkey_i2c->md_ver_ic);
		return ;
	}
#endif
}
#endif
#endif
/* To check firmware compatibility */
int get_module_class(u8 ver)
{
#ifdef  TK_HAS_FIRMWARE_UPDATE

	static int size = ARRAY_SIZE(module_divider);
	int i;

	if (size == 2)
		return 0;

	for (i = size - 1; i > 0; --i) {
		if (ver < module_divider[i] &&
			ver >= module_divider[i-1])
			return i;
	}

	return 0;
#else
	return 0;
#endif
}

bool is_same_module_class(struct touchkey_i2c *tkey_i2c)
{
#ifdef  TK_HAS_FIRMWARE_UPDATE
	int class_ic, class_file;

	if (md_ver_file == tkey_i2c->md_ver_ic)
		return true;

	class_file = get_module_class(md_ver_file);
	class_ic = get_module_class(tkey_i2c->md_ver_ic);

	printk(KERN_DEBUG"touchkey:module class, IC %d, File %d\n", class_ic, class_file);

	if (class_file == class_ic)
		return true;

	return false;
#else
	return false;
#endif
}

int tkey_load_fw_built_in(struct touchkey_i2c *tkey_i2c)
{

#ifdef  TK_HAS_FIRMWARE_UPDATE
	int retry = 3;
	int ret;

	while (retry--) {
		ret =
			request_firmware(&tkey_i2c->update_info.firm_data, tk_fw_name,
			&tkey_i2c->client->dev);
		if (ret < 0) {
			printk(KERN_ERR
				"touchkey:Unable to open firmware. ret %d retry %d\n",
				ret, retry);
			continue;
		}
		break;
	}

	firmware_data = (u8 *)tkey_i2c->update_info.firm_data->data;

	return ret;
#else
	return 0;
#endif
}

#ifdef  TK_HAS_FIRMWARE_UPDATE
int tkey_load_fw_sdcard(struct touchkey_i2c *tkey_i2c)
{
	struct file *fp;
	mm_segment_t old_fs;
	long fsize, nread;
	int ret = 0;
/*	unsigned int nSize;*/
	u8 **ums_data = &firmware_data;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(TKEY_FW_PATH, O_RDONLY, S_IRUSR);

	if (IS_ERR(fp)) {
		printk(KERN_ERR "touchkey:failed to open %s.\n", TKEY_FW_PATH);
		ret = -ENOENT;
		set_fs(old_fs);
		return ret;
	}

	fsize = fp->f_path.dentry->d_inode->i_size;
	printk(KERN_NOTICE
		"touchkey:start, file path %s, size %ld Bytes\n",
		TKEY_FW_PATH, fsize);

	*ums_data = kmalloc(fsize, GFP_KERNEL);
	if (IS_ERR(*ums_data)) {
		printk(KERN_ERR
			"touchkey:%s, kmalloc failed\n", __func__);
		ret = -EFAULT;
		goto malloc_error;
	}

	nread = vfs_read(fp, (char __user *)*ums_data,
		fsize, &fp->f_pos);
	printk(KERN_NOTICE "touchkey:nread %ld Bytes\n", nread);
	if (nread != fsize) {
		printk(KERN_ERR
			"touchkey:failed to read firmware file, nread %ld Bytes\n",
			nread);
		ret = -EIO;
		kfree(*ums_data);
		goto read_err;
	}

	filp_close(fp, current->files);
	set_fs(old_fs);

	return 0;

read_err:
malloc_error:
//size_error:
	filp_close(fp, current->files);
	set_fs(old_fs);
	return ret;
}



int touchkey_load_fw(struct touchkey_i2c *tkey_i2c, u8 fw_path)
{
	int ret = 0;

	switch (fw_path) {
	case FW_BUILT_IN:
		ret = tkey_load_fw_built_in(tkey_i2c);
		break;
	case FW_IN_SDCARD:
		ret = tkey_load_fw_sdcard(tkey_i2c);
		break;
	default:
		printk(KERN_DEBUG"touchkey:unknown path(%d)\n", fw_path);
		break;
	}

	return ret;
}

void touchkey_unload_fw(struct touchkey_i2c *tkey_i2c)
{
	switch (tkey_i2c->update_info.fw_path) {
	case FW_BUILT_IN:
		release_firmware(tkey_i2c->update_info.firm_data);
		tkey_i2c->update_info.firm_data = NULL;
		break;
	case FW_IN_SDCARD:
		kfree(firmware_data);
		firmware_data = NULL;
		break;
	default:
		break;
	}
	tkey_i2c->update_info.fw_path = FW_NONE;
}
#endif

#ifdef  TK_HAS_FIRMWARE_UPDATE
int touchkey_fw_update(struct touchkey_i2c *tkey_i2c, u8 fw_path, bool bforced)
{
	int ret;

	ret = touchkey_load_fw(tkey_i2c, fw_path);
	if (ret < 0) {
		printk(KERN_DEBUG"touchkey:failed to load fw data\n");
		return ret;
	}
	tkey_i2c->update_info.fw_path = fw_path;

	/* f/w info */
	dev_info(&tkey_i2c->client->dev, "fw ver %#x, new fw ver %#x\n",
		tkey_i2c->fw_ver_ic, fw_ver_file);
	dev_info(&tkey_i2c->client->dev, "module ver %#x, new module ver %#x\n",
		tkey_i2c->md_ver_ic, md_ver_file);

	if (unlikely(bforced))
		goto run_fw_update;

	/* check update condition */
	if (is_same_module_class(tkey_i2c)) {
		if (tkey_i2c->md_ver_ic != md_ver_file)
			goto run_fw_update;
#ifdef CONFIG_SAMSUNG_PRODUCT_SHIP
		if (tkey_i2c->fw_ver_ic != fw_ver_file)
#else
		if (tkey_i2c->fw_ver_ic < fw_ver_file)
#endif
			goto run_fw_update;
	}

	/* pass update */
	dev_info(&tkey_i2c->client->dev, "pass fw update\n");
	touchkey_unload_fw(tkey_i2c);
 run_fw_update:
	queue_work(tkey_i2c->fw_wq, &tkey_i2c->update_work);
	return 0;
}

#endif

#if 0
static void touchkey_i2c_update_work(struct work_struct *work)
{
	struct touchkey_i2c *tkey_i2c =
		container_of(work, struct touchkey_i2c, update_work);
/*	const struct firmware *firm_data = NULL;*/
	int ret;
	int retry = 3;

	disable_irq(tkey_i2c->irq);
	wake_lock(&tkey_i2c->fw_wakelock);

	if (tkey_i2c->update_info.fw_path == FW_NONE)
		goto end_fw_update;

	printk(KERN_DEBUG"touchkey:%s\n", __func__);
	tkey_i2c->update_status = TK_UPDATE_DOWN;

	while (retry--) {
		ret = ISSP_main(tkey_i2c);
		if (ret != 0) {
			msleep(50);
			dev_err(&tkey_i2c->client->dev, "failed to update f/w. retry\n");
			continue;
		}

		dev_info(&tkey_i2c->client->dev, "finish f/w update\n");
		tkey_i2c->update_status = TK_UPDATE_PASS;
		break;
	}
	if (retry <= 0) {
		tkey_i2c->pdata->power_on(0);
		tkey_i2c->update_status = TK_UPDATE_FAIL;
		dev_err(&tkey_i2c->client->dev, "failed to update f/w\n");
		goto err_fw_update;
	}

	ret = touchkey_i2c_check(tkey_i2c);
	if (ret < 0)
		goto err_fw_update;

	dev_info(&tkey_i2c->client->dev, "f/w ver = %#X, module ver = %#X\n",
		tkey_i2c->fw_ver_ic, tkey_i2c->md_ver_ic);

 end_fw_update:
	enable_irq(tkey_i2c->irq);
 err_fw_update:
	touchkey_unload_fw(tkey_i2c);
	wake_unlock(&tkey_i2c->fw_wakelock);
}

#endif

static irqreturn_t touchkey_interrupt(int irq, void *dev_id)
{
	struct touchkey_i2c *tkey_i2c = dev_id;
	u8 data[3];
	int ret;
	int keycode_type = 0;
	int pressed;

	printk("touchkey_interrupt !!!\n");

	if (unlikely(!touchkey_probe)) {
		dev_err(&tkey_i2c->client->dev, "%s: Touchkey is not probed\n", __func__);
		return IRQ_HANDLED;
	}

	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 1);
	if (ret < 0)
		return IRQ_HANDLED;

	keycode_type = (data[0] & TK_BIT_KEYCODE);
	pressed = !(data[0] & TK_BIT_PRESS_EV);

	if (keycode_type <= 0 || keycode_type >= touchkey_count) {
		dev_dbg(&tkey_i2c->client->dev, "keycode_type err\n");
		return IRQ_HANDLED;
	}

	input_report_key(tkey_i2c->input_dev,
			 touchkey_keycode[keycode_type], pressed);
	input_sync(tkey_i2c->input_dev);


#if !defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
	dev_info(&tkey_i2c->client->dev, "keycode:%d pressed:%d %d\n",
	touchkey_keycode[keycode_type], pressed, tkey_i2c->tsk_glove_mode_status);
#else
	dev_info(&tkey_i2c->client->dev, "pressed:%d %d\n",
		pressed, tkey_i2c->tsk_glove_mode_status);
#endif



#ifdef TOUCHKEY_BOOSTER
	touchkey_set_dvfs_lock(tkey_i2c, !!pressed);
#endif
#ifdef CONFIG_INPUT_BOOSTER
	INPUT_BOOSTER_SEND_EVENT(touchkey_keycode[keycode_type],
		!!pressed);
#endif
	return IRQ_HANDLED;
}

static int touchkey_stop(struct touchkey_i2c *tkey_i2c)
{
	int i;

	mutex_lock(&tkey_i2c->lock);

	if (!tkey_i2c->enabled) {
		dev_err(&tkey_i2c->client->dev, "Touch key already disabled\n");
		goto err_stop_out;
	}
	if (wake_lock_active(&tkey_i2c->fw_wakelock)) {
		printk(KERN_DEBUG"touchkey:wake_lock active\n");
		goto err_stop_out;
	}

	disable_irq(tkey_i2c->irq);

	/* release keys */
	for (i = 1; i < touchkey_count; ++i) {
		input_report_key(tkey_i2c->input_dev,
				 touchkey_keycode[i], 0);
#ifdef CONFIG_INPUT_BOOSTER
		INPUT_BOOSTER_SEND_EVENT(touchkey_keycode[i],
			BOOSTER_MODE_FORCE_OFF);
#endif
	}
	input_sync(tkey_i2c->input_dev);

#if defined(CONFIG_GLOVE_TOUCH)
	/*cancel or waiting before pwr off*/
	tkey_i2c->tsk_glove_mode_status = false;
	cancel_delayed_work(&tkey_i2c->glove_change_work);
#endif

	/* disable ldo11 */
	tkey_i2c->pdata->led_power_on(0);

	/* disable external ldo */
	tkey_i2c->pdata->power_on(0);

	tkey_i2c->enabled = false;
	tkey_i2c->status_update = false;
#ifdef TOUCHKEY_BOOSTER
	touchkey_set_dvfs_lock(tkey_i2c, 2);
#endif

 err_stop_out:
	mutex_unlock(&tkey_i2c->lock);

	return 0;
}

static int touchkey_start(struct touchkey_i2c *tkey_i2c)
{
	mutex_lock(&tkey_i2c->lock);

	if (tkey_i2c->enabled) {
		dev_err(&tkey_i2c->client->dev, "Touch key already enabled\n");
		goto err_start_out;
	}
	if (wake_lock_active(&tkey_i2c->fw_wakelock)) {
		printk(KERN_DEBUG"touchkey:wake_lock active\n");
		goto err_start_out;
	}

	/* enable external ldo */
	tkey_i2c->pdata->power_on(1);
	msleep(50);
	/* enable ldo11 */
    tkey_i2c->pdata->led_power_on(1);

	tkey_i2c->enabled = true;

	if (touchled_cmd_reversed) {
		touchled_cmd_reversed = 0;
		i2c_touchkey_write(tkey_i2c->client,
			(u8 *) &touchkey_led_status, 1);
		dev_err(&tkey_i2c->client->dev, "%s: Turning LED is reserved\n", __func__);
		msleep(30);
	}

#ifdef TEST_JIG_MODE
    touchkey_enable_status_update(tkey_i2c);
#endif

#if defined(TK_INFORM_CHARGER)
	touchkey_ta_setting(tkey_i2c);
#endif

#if defined(CONFIG_GLOVE_TOUCH)
	tkey_i2c->tsk_glove_lock_status = false;
	touchkey_glovemode(tkey_i2c->tsk_glove_mode_status);
#endif
	enable_irq(tkey_i2c->irq);
 err_start_out:
	mutex_unlock(&tkey_i2c->lock);

	return 0;
}

#ifdef TK_USE_OPEN_DWORK
static void touchkey_open_work(struct work_struct *work)
{
	int retval;
	struct touchkey_i2c *tkey_i2c =
			container_of(work, struct touchkey_i2c,
			open_work.work);

	if (tkey_i2c->enabled) {
		dev_err(&tkey_i2c->client->dev, "Touch key already enabled\n");
		return;
	}

	retval = touchkey_start(tkey_i2c);
	if (retval < 0)
		dev_err(&tkey_i2c->client->dev,
				"%s: Failed to start device\n", __func__);
}
#endif

static int touchkey_input_open(struct input_dev *dev)
{
	struct touchkey_i2c *data = input_get_drvdata(dev);
	int ret;

/*	ret = wait_for_completion_interruptible_timeout(&data->init_done,
			msecs_to_jiffies(90 * MSEC_PER_SEC));

	if (ret < 0) {
		dev_err(&data->client->dev,
			"error while waiting for device to init (%d)\n", ret);
		ret = -ENXIO;
		goto err_open;
	}
	if (ret == 0) {
		dev_err(&data->client->dev,
			"timedout while waiting for device to init\n");
		ret = -ENXIO;
		goto err_open;
	}*/
#ifdef TK_USE_OPEN_DWORK
	schedule_delayed_work(&data->open_work,
					msecs_to_jiffies(TK_OPEN_DWORK_TIME));
#else
	ret = touchkey_start(data);
	if (ret)
		goto err_open;
#endif

	dev_info(&data->client->dev, "%s\n", __func__);
	return 0;

err_open:
	return ret;
}

static void touchkey_input_close(struct input_dev *dev)
{
	struct touchkey_i2c *data = input_get_drvdata(dev);

#ifdef TK_USE_OPEN_DWORK
	cancel_delayed_work(&data->open_work);
#endif
	touchkey_stop(data);

	dev_dbg(&data->client->dev, "%s\n", __func__);
}

#ifdef CONFIG_PM
#ifdef CONFIG_HAS_EARLYSUSPEND
#define touchkey_suspend	NULL
#define touchkey_resume	NULL

static int sec_touchkey_early_suspend(struct early_suspend *h)
{

	struct touchkey_i2c *tkey_i2c =
		container_of(h, struct touchkey_i2c, early_suspend);

	touchkey_stop(tkey_i2c);

	dev_dbg(&tkey_i2c->client->dev, "%s\n", __func__);

	return 0;
}

static int sec_touchkey_late_resume(struct early_suspend *h)
{

	struct touchkey_i2c *tkey_i2c =
		container_of(h, struct touchkey_i2c, early_suspend);

	dev_dbg(&tkey_i2c->client->dev, "%s\n", __func__);

	touchkey_start(tkey_i2c);

	return 0;
}
#else
static int touchkey_suspend(struct device *dev)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);

	if (touchkey_probe != true) {
		printk(KERN_ERR "%s Touchkey is not enabled. \n",
		       __func__);
		return 0;
	}
	mutex_lock(&tkey_i2c->input_dev->mutex);

	if (tkey_i2c->input_dev->users)
		touchkey_stop(tkey_i2c);

	mutex_unlock(&tkey_i2c->input_dev->mutex);

	dev_dbg(&tkey_i2c->client->dev, "%s\n", __func__);

	return 0;
}

static int touchkey_resume(struct device *dev)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);

	if (touchkey_probe != true) {
		printk(KERN_ERR "%s Touchkey is not enabled. \n",
		       __func__);
		return 0;
	}
	mutex_lock(&tkey_i2c->input_dev->mutex);

	if (tkey_i2c->input_dev->users)
		touchkey_start(tkey_i2c);

	mutex_unlock(&tkey_i2c->input_dev->mutex);

	dev_dbg(&tkey_i2c->client->dev, "%s\n", __func__);

	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(touchkey_pm_ops, touchkey_suspend, touchkey_resume);

#endif

static int touchkey_i2c_check(struct touchkey_i2c *tkey_i2c)
{
	char data[2] = { 0, };
	int ret = 0;

	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x04, data, 2);
	if (ret < 0) {
		dev_err(&tkey_i2c->client->dev, "Failed to read Module version\n");
		tkey_i2c->fw_ver_ic = 0;
		tkey_i2c->md_ver_ic = 0;
		return ret;
	}

	tkey_i2c->fw_ver_ic = data[0];
	tkey_i2c->md_ver_ic = data[1];

	return ret;
}

static ssize_t touchkey_led_control(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int data;
	int ret;
	static const int ledCmd[] = {TK_CMD_LED_OFF, TK_CMD_LED_ON};

	if (wake_lock_active(&tkey_i2c->fw_wakelock)) {
		printk(KERN_DEBUG"touchkey:%s, wakelock active\n", __func__);
		return size;
	}

	ret = sscanf(buf, "%d", &data);

	if (ret != 1) {
		dev_err(&tkey_i2c->client->dev, "%s, %d err\n",
			__func__, __LINE__);
		return size;
	}

	if (data != 0 && data != 1) {
		dev_err(&tkey_i2c->client->dev, "%s wrong cmd %x\n",
			__func__, data);
		return size;
	}

	data = ledCmd[data];

	if (!tkey_i2c->enabled) {
		touchled_cmd_reversed = 1;
		goto out;
	}

	ret = i2c_touchkey_write(tkey_i2c->client, (u8 *) &data, 1);
	if (ret < 0) {
		dev_err(&tkey_i2c->client->dev, "%s: Error turn on led %d\n",
			__func__, ret);
		touchled_cmd_reversed = 1;
		goto out;
	}
	msleep(30);

out:
	touchkey_led_status = data;

	return size;
}

#if defined(TK_USE_4KEY)
static ssize_t touchkey_menu_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[18] = { 0, };
	int ret;

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 18);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[10] =%d,data[11] = %d\n", __func__,
	       data[10], data[11]);
	menu_sensitivity = ((0x00FF & data[10]) << 8) | data[11];

	return sprintf(buf, "%d\n", menu_sensitivity);
}

static ssize_t touchkey_home_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[18] = { 0, };
	int ret;

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 18);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[12] =%d,data[13] = %d\n", __func__,
	       data[12], data[13]);
	home_sensitivity = ((0x00FF & data[12]) << 8) | data[13];

	return sprintf(buf, "%d\n", home_sensitivity);
}

static ssize_t touchkey_back_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[18] = { 0, };
	int ret;

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 18);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[14] =%d,data[15] = %d\n", __func__,
	       data[14], data[15]);
	back_sensitivity = ((0x00FF & data[14]) << 8) | data[15];

	return sprintf(buf, "%d\n", back_sensitivity);
}

static ssize_t touchkey_search_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[18] = { 0, };
	int ret;

	dev_dbg(&tkey_i2c->client->dev, "called %s\n", __func__);
	ret = i2c_touchkey_read(tkey_i2c->client, KEYCODE_REG, data, 18);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[16] =%d,data[17] = %d\n", __func__,
	       data[16], data[17]);
	search_sensitivity = ((0x00FF & data[16]) << 8) | data[17];

	return sprintf(buf, "%d\n", search_sensitivity);
}
#else
static ssize_t touchkey_menu_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[14] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x16, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[0] = %d, data[1] =%d\n", __func__,
			data[0], data[1]);
	menu_sensitivity = ((0x00FF & data[0]) << 8) | data[1];

	return sprintf(buf, "%d\n", menu_sensitivity);
}

static ssize_t touchkey_back_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	u8 data[14] = { 0, };
	int ret;

	if (!tkey_i2c->status_update) {
		ret = touchkey_enable_status_update(tkey_i2c);
		if (ret < 0)
			return ret;
	}

	ret = i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x16, data, 8);

	dev_dbg(&tkey_i2c->client->dev, "called %s data[0] = %d, data[1] =%d\n", __func__,
			data[4], data[5]);
	back_sensitivity = ((0x00FF & data[4]) << 8) | data[5];

	return sprintf(buf, "%d\n", back_sensitivity);
}
#endif

#if defined(CONFIG_GLOVE_TOUCH)
static ssize_t glove_mode_enable(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int data;

	sscanf(buf, "%d\n", &data);
	dev_dbg(&tkey_i2c->client->dev, "%s %d\n", __func__, data);

	touchkey_glovemode(data);

	return size;
}
#endif

#ifdef TKEY_FLIP_MODE
static ssize_t flip_cover_mode_enable(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int data;

	sscanf(buf, "%d\n", &data);
	dev_info(&tkey_i2c->client->dev, "%s %d\n", __func__, data);

	touchkey_flip_cover(data);

	return size;
}
#endif

static ssize_t touch_sensitivity_control(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int ret;

    ret = touchkey_enable_status_update(tkey_i2c);
    if (ret < 0)
        return ret;
    dev_dbg(&tkey_i2c->client->dev, "%s\n", __func__);

	return size;
}

static ssize_t set_touchkey_firm_version_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
#ifdef TK_HAS_FIRMWARE_UPDATE
	printk(KERN_DEBUG"touchkey:firm_ver_bin %0#4x\n", fw_ver_file);
	return sprintf(buf, "%0#4x\n", fw_ver_file);
#else
return sprintf(buf, "%0#4x\n", 0x0);

#endif
}

static ssize_t set_touchkey_update_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t size)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);

	u8 fw_path;

	switch(*buf) {
	case 's':
	case 'S':
		fw_path = FW_BUILT_IN;
		break;
	case 'i':
	case 'I':
		fw_path = FW_IN_SDCARD;
		break;
	default:
		return size;
	}
#ifdef TK_HAS_FIRMWARE_UPDATE
	touchkey_fw_update(tkey_i2c, fw_path, true);
#endif
	msleep(3000);
	cancel_work_sync(&tkey_i2c->update_work);

	return size;
}

static ssize_t set_touchkey_firm_version_read_show(struct device *dev,
						   struct device_attribute
						   *attr, char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	char data[3] = { 0, };
	int count;

	i2c_touchkey_read(tkey_i2c->client, BASE_REG + 0x04, data, 2);
	count = sprintf(buf, "0x%02x\n", data[0]);

	dev_info(&tkey_i2c->client->dev, "Touch_version_read 0x%02x\n", data[0]);
	dev_info(&tkey_i2c->client->dev, "Module_version_read 0x%02x\n", data[1]);
	return count;
}

static ssize_t set_touchkey_firm_status_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int count = 0;

	dev_info(&tkey_i2c->client->dev, "Touch_update_read: update_status %d\n",
	       tkey_i2c->update_status);

	if (tkey_i2c->update_status == TK_UPDATE_PASS)
		count = sprintf(buf, "PASS\n");
	else if (tkey_i2c->update_status == TK_UPDATE_DOWN)
		count = sprintf(buf, "Downloading\n");
	else if (tkey_i2c->update_status == TK_UPDATE_FAIL)
		count = sprintf(buf, "Fail\n");

	return count;
}
#ifdef TOUCHKEY_BOOSTER
static ssize_t touchkey_boost_level(struct device *dev,
						struct device_attribute *attr, const char *buf,
						size_t count)
{
	struct touchkey_i2c *tkey_i2c = dev_get_drvdata(dev);
	int level;

	sscanf(buf, "%d", &level);

	if (level < 0 || level > 2) {
		dev_err(&tkey_i2c->client->dev, "err to set boost_level %d\n", level);
		return count;
	}

	tkey_i2c->boost_level = level;
	dev_info(&tkey_i2c->client->dev, "%s %d\n", __func__, level);

	return count;
}
#endif


static DEVICE_ATTR(brightness, S_IRUGO | S_IWUSR | S_IWGRP, NULL,
		   touchkey_led_control);
static DEVICE_ATTR(touchkey_menu, S_IRUGO | S_IWUSR | S_IWGRP,
		   touchkey_menu_show, NULL);
static DEVICE_ATTR(touchkey_back, S_IRUGO | S_IWUSR | S_IWGRP,
		   touchkey_back_show, NULL);

#if defined(TK_USE_4KEY)
static DEVICE_ATTR(touchkey_home, S_IRUGO, touchkey_home_show, NULL);
static DEVICE_ATTR(touchkey_search, S_IRUGO, touchkey_search_show, NULL);
#endif

static DEVICE_ATTR(touch_sensitivity, S_IRUGO | S_IWUSR | S_IWGRP,
		   NULL, touch_sensitivity_control);
static DEVICE_ATTR(touchkey_firm_update, S_IRUGO | S_IWUSR | S_IWGRP,
		   NULL, set_touchkey_update_store);
static DEVICE_ATTR(touchkey_firm_update_status, S_IRUGO | S_IWUSR | S_IWGRP,
		   set_touchkey_firm_status_show, NULL);
static DEVICE_ATTR(touchkey_firm_version_phone, S_IRUGO | S_IWUSR | S_IWGRP,
		   set_touchkey_firm_version_show, NULL);
static DEVICE_ATTR(touchkey_firm_version_panel, S_IRUGO | S_IWUSR | S_IWGRP,
		   set_touchkey_firm_version_read_show, NULL);
#ifdef LED_LDO_WITH_REGULATOR
static DEVICE_ATTR(touchkey_brightness, S_IRUGO | S_IWUSR | S_IWGRP,
		   NULL, brightness_control);
#endif

static DEVICE_ATTR(touchkey_raw_data0, S_IRUGO, touchkey_raw_data0_show, NULL);
static DEVICE_ATTR(touchkey_raw_data1, S_IRUGO, touchkey_raw_data1_show, NULL);
static DEVICE_ATTR(touchkey_raw_data2, S_IRUGO, touchkey_raw_data2_show, NULL);
static DEVICE_ATTR(touchkey_raw_data3, S_IRUGO, touchkey_raw_data3_show, NULL);

static DEVICE_ATTR(touchkey_diff_data0, S_IRUGO, touchkey_diff_data0_show, NULL);
static DEVICE_ATTR(touchkey_diff_data1, S_IRUGO, touchkey_diff_data1_show, NULL);
static DEVICE_ATTR(touchkey_diff_data2, S_IRUGO, touchkey_diff_data2_show, NULL);
static DEVICE_ATTR(touchkey_diff_data3, S_IRUGO, touchkey_diff_data3_show, NULL);

static DEVICE_ATTR(touchkey_baseline_data0, S_IRUGO, touchkey_baseline_data0_show, NULL);
static DEVICE_ATTR(touchkey_baseline_data1, S_IRUGO, touchkey_baseline_data1_show, NULL);
static DEVICE_ATTR(touchkey_baseline_data2, S_IRUGO, touchkey_baseline_data2_show, NULL);
static DEVICE_ATTR(touchkey_baseline_data3, S_IRUGO, touchkey_baseline_data3_show, NULL);

static DEVICE_ATTR(touchkey_idac0, S_IRUGO, touchkey_idac0_show, NULL);
static DEVICE_ATTR(touchkey_idac1, S_IRUGO, touchkey_idac1_show, NULL);
static DEVICE_ATTR(touchkey_idac2, S_IRUGO, touchkey_idac2_show, NULL);
static DEVICE_ATTR(touchkey_idac3, S_IRUGO, touchkey_idac3_show, NULL);

static DEVICE_ATTR(touchkey_comp_idac0, S_IRUGO, touchkey_comp_idac0_show, NULL);
static DEVICE_ATTR(touchkey_comp_idac1, S_IRUGO, touchkey_comp_idac1_show, NULL);
static DEVICE_ATTR(touchkey_comp_idac2, S_IRUGO, touchkey_comp_idac2_show, NULL);
static DEVICE_ATTR(touchkey_comp_idac3, S_IRUGO, touchkey_comp_idac3_show, NULL);

static DEVICE_ATTR(touchkey_threshold, S_IRUGO, touchkey_threshold_show, NULL);
static DEVICE_ATTR(touchkey_threshold2, S_IRUGO, touchkey_threshold2_show, NULL);
static DEVICE_ATTR(touchkey_threshold3, S_IRUGO, touchkey_threshold3_show, NULL);
static DEVICE_ATTR(touchkey_threshold4, S_IRUGO, touchkey_threshold4_show, NULL);

//#endif

#if defined(CONFIG_GLOVE_TOUCH)
static DEVICE_ATTR(glove_mode, S_IRUGO | S_IWUSR | S_IWGRP, NULL,
		   glove_mode_enable);
#endif
#ifdef TKEY_FLIP_MODE
static DEVICE_ATTR(flip_mode, S_IRUGO | S_IWUSR | S_IWGRP, NULL,
		   flip_cover_mode_enable);
#endif
#ifdef TOUCHKEY_BOOSTER
static DEVICE_ATTR(boost_level, S_IWUSR | S_IWGRP, NULL, touchkey_boost_level);
#endif

static struct attribute *touchkey_attributes[] = {
	&dev_attr_brightness.attr,
	&dev_attr_touchkey_menu.attr,
	&dev_attr_touchkey_back.attr,
#if defined(TK_USE_4KEY)
	&dev_attr_touchkey_home.attr,
	&dev_attr_touchkey_search.attr,
#endif
	&dev_attr_touch_sensitivity.attr,
	&dev_attr_touchkey_firm_update.attr,
	&dev_attr_touchkey_firm_update_status.attr,
	&dev_attr_touchkey_firm_version_phone.attr,
	&dev_attr_touchkey_firm_version_panel.attr,
#ifdef LED_LDO_WITH_REGULATOR
	&dev_attr_touchkey_brightness.attr,
#endif

	&dev_attr_touchkey_raw_data0.attr,
	&dev_attr_touchkey_raw_data1.attr,
	&dev_attr_touchkey_raw_data2.attr,
	&dev_attr_touchkey_raw_data3.attr,
	&dev_attr_touchkey_diff_data0.attr,
	&dev_attr_touchkey_diff_data1.attr,
	&dev_attr_touchkey_diff_data2.attr,
	&dev_attr_touchkey_diff_data3.attr,
	&dev_attr_touchkey_baseline_data0.attr,
	&dev_attr_touchkey_baseline_data1.attr,
	&dev_attr_touchkey_baseline_data2.attr,
	&dev_attr_touchkey_baseline_data3.attr,
	&dev_attr_touchkey_idac0.attr,
	&dev_attr_touchkey_idac1.attr,
	&dev_attr_touchkey_idac2.attr,
	&dev_attr_touchkey_idac3.attr,
	&dev_attr_touchkey_comp_idac0.attr,
	&dev_attr_touchkey_comp_idac1.attr,
	&dev_attr_touchkey_comp_idac2.attr,
	&dev_attr_touchkey_comp_idac3.attr,

	&dev_attr_touchkey_threshold.attr,
	&dev_attr_touchkey_threshold2.attr,
	&dev_attr_touchkey_threshold3.attr,
	&dev_attr_touchkey_threshold4.attr,

#if defined(CONFIG_GLOVE_TOUCH)
	&dev_attr_glove_mode.attr,
#endif
#ifdef TKEY_FLIP_MODE
	&dev_attr_flip_mode.attr,
#endif
#ifdef TOUCHKEY_BOOSTER
	&dev_attr_boost_level.attr,
#endif
	NULL,
};

static struct attribute_group touchkey_attr_group = {
	.attrs = touchkey_attributes,
};

static int touchkey_power_on(bool on)
{
	struct regulator *regulator = NULL;
	int ret;

	if (on) {
		regulator = regulator_get(NULL, TK_REGULATOR_NAME);
		if (IS_ERR(regulator))
			return 0;
		ret = regulator_enable(regulator);
		regulator_put(regulator);
	} else {
		regulator = regulator_get(NULL, TK_REGULATOR_NAME);
		if (IS_ERR(regulator))
			return 0;
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);
	}
	printk(KERN_ERR "%s: %s",__func__,(on)?"on":"off");

	return 1;
}
static int touchkey_led_power_on(bool on)
{
	struct regulator *regulator = NULL;
	int ret;

	if (on) {
/*		regulator = regulator_get(NULL, TK_REGULATOR_NAME);*/
		if (IS_ERR(regulator))
			return 0;
		ret = regulator_enable(regulator);
		regulator_put(regulator);
	} else {
/*		regulator = regulator_get(NULL, TK_REGULATOR_NAME);*/
		if (IS_ERR(regulator))
			return 0;
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);
	}
	printk(KERN_ERR "%s: %s",__func__,(on)?"on":"off");

	return 1;
}

static int cypress_touchkey_power_on(bool on)
{

	if (on) {
		pr_info("%s :ON\n", __func__);
		/* As we are setting this GPIO21 in dev-touchpanel.c*/
		/*so commented out here*/
		/*gpio_set_value(TOUCH_1V8_EN,TOUCH_1V8_ON);*/
	} else {
		pr_info("%s :OFF\n", __func__);
		/* As we are setting this GPIO21 in dev-touchpanel.c*/
		/*so commented out here*/
		/*gpio_set_value(TOUCH_1V8_EN,TOUCH_1V8_OFF);*/
	}

	return 0;
}

static int cypress_touchkey_led_power_on(bool on)
{
	struct regulator *regulator;
	int ret;

	if (on) {
		regulator = regulator_get(NULL, "key_led");
		if (IS_ERR(regulator))
			return 0;
		ret = regulator_enable(regulator);
		pr_info("%s - regulator_enable: %d\n", __func__, ret);
		regulator_put(regulator);
	} else {
		regulator = regulator_get(NULL, "key_led");
		if (IS_ERR(regulator))
			return 0;
		if (regulator_is_enabled(regulator)) {
			ret = regulator_disable(regulator);
			pr_info("%s - regulator_disable: %d\n",
					__func__, ret);
		}
			regulator_put(regulator);
	}
	pr_err("%s: %s\n", __func__, (on) ? "on" : "off");

	return 1;
}

#ifdef CONFIG_OF
static struct touchkey_platform_data *cypress_parse_dt(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct touchkey_platform_data *pdata;
	struct device_node *np = dev->of_node;

	if (!np)
		return ERR_PTR(-ENOENT);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "failed to allocate platform data\n");
		return ERR_PTR(-ENOMEM);
	}
	dev->platform_data = pdata;
	pdata->power_on = cypress_touchkey_power_on;
	pdata->led_power_on = cypress_touchkey_led_power_on;

	return pdata;
}
#else
//we have to implement this code for GPOI INT
static void cypress_request_gpio(struct touchkey_platform_data *pdata)
{
	int ret;

	ret = gpio_request(pdata->gpio_scl, "touchkey_scl");
	if (ret) {
		printk(KERN_ERR "%s: unable to request touchkey_scl [%d]\n",
				__func__, pdata->gpio_scl);
		return;
	}

//	gpio_tlmm_config(GPIO_CFG(pdata->gpio_scl, 3, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);

	ret = gpio_request(pdata->gpio_sda, "touchkey_sda");
	if (ret) {
		printk(KERN_ERR "%s: unable to request touchkey_sda [%d]\n",
				__func__, pdata->gpio_sda);
		return;
	}

//	gpio_tlmm_config(GPIO_CFG(pdata->gpio_sda, 3, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA), 1);

	ret = gpio_request(pdata->gpio_int, "touchkey_irq");
	if (ret) {
		printk(KERN_ERR "%s: unable to request touchkey_irq [%d]\n",
				__func__, pdata->gpio_int);
		return;
	}
}

#endif



static int i2c_touchkey_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct touchkey_platform_data *pdata = client->dev.platform_data;
	struct touchkey_i2c *tkey_i2c = NULL;
/*	bool bforced = false;*/
	struct input_dev *input_dev;
	int i;
	int ret = 0;

	printk("%s\n",__func__);
	if (!pdata) {
#ifdef CONFIG_OF
		pdata = cypress_parse_dt(client);
		if (!pdata) {
			dev_err(&client->dev, "%s: no pdata\n", __func__);
			return -EINVAL;
		}
#else
		printk("%s - Error:There is no pdata\n",__func__);
		return -EINVAL;
#endif
	}

	if(client->irq) {
		ret = devm_request_irq(&client->dev, client->irq, NULL,
			IRQF_TRIGGER_FALLING, "cypress-touchkey", tkey_i2c);
	} else{
		pr_info("%s - HW I2C Bus Num:%d\n",
			__func__, client->adapter->nr);
	}

	/*Check I2C functionality */
	ret = i2c_check_functionality(client->adapter, I2C_FUNC_I2C);
	if (ret == 0) {
		dev_err(&client->dev, "No I2C functionality found\n");
		return -ENODEV;
	}

	/*Obtain kernel memory space for touchkey i2c */
	tkey_i2c = kzalloc(sizeof(struct touchkey_i2c), GFP_KERNEL);
	if (NULL == tkey_i2c) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

#ifdef CONFIG_GLOVE_TOUCH
	tkey_i2c_global = tkey_i2c;
#endif

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		ret = -ENOMEM;
		goto err_allocate_input_device;
	}

	input_dev->name = "sec_touchkey";
	input_dev->phys = "sec_touchkey/input0";
	input_dev->id.bustype = BUS_HOST;
	input_dev->dev.parent = &client->dev;
	input_dev->open = touchkey_input_open;
	input_dev->close = touchkey_input_close;

	/*tkey_i2c*/
	tkey_i2c->pdata = pdata;
	tkey_i2c->input_dev = input_dev;
	tkey_i2c->client = client;
	tkey_i2c->irq = client->irq;
	tkey_i2c->name = "sec_touchkey";
	tkey_i2c->status_update = false;

	if(!tkey_i2c->pdata->power_on)
		tkey_i2c->pdata->power_on = touchkey_power_on;
	if(!tkey_i2c->pdata->led_power_on)
		tkey_i2c->pdata->led_power_on = touchkey_led_power_on;

	init_completion(&tkey_i2c->init_done);
	mutex_init(&tkey_i2c->lock);
#ifdef TK_USE_OPEN_DWORK
	INIT_DELAYED_WORK(&tkey_i2c->open_work, touchkey_open_work);
#endif
	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_LED, input_dev->evbit);
	set_bit(LED_MISC, input_dev->ledbit);
	set_bit(EV_KEY, input_dev->evbit);
#ifdef CONFIG_VT_TKEY_SKIP_MATCH
	set_bit(EV_TOUCHKEY, input_dev->evbit);
#endif

#ifdef  TK_HAS_FIRMWARE_UPDATE
	INIT_WORK(&tkey_i2c->update_work, touchkey_i2c_update_work);
	wake_lock_init(&tkey_i2c->fw_wakelock, WAKE_LOCK_SUSPEND, "touchkey");
#endif

	for (i = 1; i < touchkey_count; i++)
		set_bit(touchkey_keycode[i], input_dev->keybit);

	input_set_drvdata(input_dev, tkey_i2c);
	i2c_set_clientdata(client, tkey_i2c);

	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&client->dev, "Failed to register input device\n");
		goto err_register_device;
	}
	// turn on touchkey.
	printk("1. power on TouchKey\n");
	tkey_i2c->pdata->power_on(1);
	msleep(50);
	/* enable ldo11 */
	tkey_i2c->pdata->led_power_on(1);

	tkey_i2c->enabled = true;

#if 1
	/*sysfs*/
	//tkey_i2c->dev = sec_device_create(NULL, "sec_touchkey");
	tkey_i2c->dev = device_create(sec_class, NULL, 0, tkey_i2c, "sec_touchkey");


	ret = IS_ERR(tkey_i2c->dev);
	if (ret) {
		dev_err(&client->dev, "Failed to create device(tkey_i2c->dev)!\n");
		goto err_device_create;
	}

	dev_set_drvdata(tkey_i2c->dev, tkey_i2c);

	ret = sysfs_create_group(&tkey_i2c->dev->kobj,
				&touchkey_attr_group);
	if (ret) {
		dev_err(&client->dev, "Failed to create sysfs group\n");
		goto err_sysfs_init;
	}
#endif

	tkey_i2c->fw_wq = create_singlethread_workqueue(client->name);
	if (!tkey_i2c->fw_wq) {
		dev_err(&client->dev, "fail to create workqueue for fw_wq\n");
		ret = -ENOMEM;
		goto err_create_fw_wq;
	}

	ret = touchkey_i2c_check(tkey_i2c);
	if (ret < 0) {
		dev_err(&client->dev, "i2c_check failed\n");
		if (system_rev != BOARD_REV_050)
			goto err_i2c_check;
	}

#ifdef TOUCHKEY_BOOSTER
	ret = touchkey_init_dvfs(tkey_i2c);
	if (ret < 0) {
		dev_err(&client->dev, "Fail get dvfs level for touch booster\n");
		goto err_i2c_check;
	}
	tkey_i2c->boost_level = TKEY_BOOSTER_LEVEL2;
#endif

#if defined(CONFIG_GLOVE_TOUCH)
		mutex_init(&tkey_i2c->tsk_glove_lock);
		INIT_DELAYED_WORK(&tkey_i2c->glove_change_work, touchkey_glove_change_work);
		tkey_i2c->tsk_glove_mode_status = false;
#endif

	ret =
		request_threaded_irq(tkey_i2c->irq, NULL, touchkey_interrupt,
				IRQF_DISABLED | IRQF_TRIGGER_FALLING |
				IRQF_ONESHOT, tkey_i2c->name, tkey_i2c);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to request irq(%d) - %d\n",
			tkey_i2c->irq, ret);
		goto err_request_threaded_irq;
	}

/* Calls to regulator_enable() must be balanced with calls to regulator_disable(). So this is blocked.
	tkey_i2c->pdata->led_power_on(1);
*/

#if defined(TK_HAS_FIRMWARE_UPDATE)
/*tkey_firmupdate_retry_byreboot:*/
/*	touchkey_init_fw_name(tkey_i2c);
	ret = touchkey_fw_update(tkey_i2c, FW_BUILT_IN, bforced);
	if (ret < 0) {
		dev_err(&client->dev, "fw update fail\n");
		goto err_firmware_update;
	}*/
#endif

#if defined(TK_INFORM_CHARGER)
	tkey_i2c->callbacks.inform_charger = touchkey_ta_cb;
	if (tkey_i2c->pdata->register_cb) {
		dev_info(&client->dev, "Touchkey TA information\n");
		tkey_i2c->pdata->register_cb(&tkey_i2c->callbacks);
	}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	tkey_i2c->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	tkey_i2c->early_suspend.suspend =
		(void *)sec_touchkey_early_suspend;
	tkey_i2c->early_suspend.resume =
		(void *)sec_touchkey_late_resume;
	register_early_suspend(&tkey_i2c->early_suspend);
#endif

/*	touchkey_stop(tkey_i2c); */
	complete_all(&tkey_i2c->init_done);
	touchkey_probe = true;

	return 0;

#if defined(TK_HAS_FIRMWARE_UPDATE)
/*err_firmware_update:
	tkey_i2c->pdata->led_power_on(0);
	disable_irq(tkey_i2c->irq);
	free_irq(tkey_i2c->irq, tkey_i2c);*/
#endif
err_request_threaded_irq:
	mutex_destroy(&tkey_i2c->tsk_glove_lock);
//	mutex_destroy(&tkey_i2c->tsk_dvfs_lock);
err_i2c_check:
	destroy_workqueue(tkey_i2c->fw_wq);
err_create_fw_wq:
	//sysfs_remove_group(&tkey_i2c->dev->kobj, &touchkey_attr_group);
err_sysfs_init:
	//sec_device_destroy(tkey_i2c->dev->devt);
err_device_create:
	//tkey_i2c->pdata->power_on(0);
	input_unregister_device(input_dev);
	input_dev = NULL;
err_register_device:
#ifdef  TK_HAS_FIRMWARE_UPDATE
	wake_lock_destroy(&tkey_i2c->fw_wakelock);
#endif
	mutex_destroy(&tkey_i2c->lock);
	input_free_device(input_dev);
err_allocate_input_device:
	kfree(tkey_i2c);
	return ret;
}

void touchkey_shutdown(struct i2c_client *client)
{
	struct touchkey_i2c *tkey_i2c = i2c_get_clientdata(client);

	tkey_i2c->pdata->power_on(0);
	tkey_i2c->pdata->led_power_on(0);

	printk(KERN_DEBUG"touchkey:%s\n", __func__);
}

static struct of_device_id cypress_touchkey_dt_ids_of_match[] = {
	{ .compatible = "cypress,cypress-touchkey" },
	{ }
};

MODULE_DEVICE_TABLE(of, cypress_touchkey_dt_ids_of_match);

struct i2c_driver touchkey_i2c_driver = {
	.driver = {
		.name = "sec_touchkey_driver",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &touchkey_pm_ops,
#endif
		.of_match_table = cypress_touchkey_dt_ids_of_match,

	},
	.id_table = sec_touchkey_id,
	.probe = i2c_touchkey_probe,
	.shutdown = &touchkey_shutdown,
};

static int __init touchkey_init(void)
{
	int ret_val=0;
#ifdef TEST_JIG_MODE
	int ret;
#endif

	printk("%s\n",__func__);

	ret_val = i2c_add_driver(&touchkey_i2c_driver);
	printk("%s : ret_val = %d\n", __func__, ret_val);

#ifdef TEST_JIG_MODE
    ret = touchkey_enable_status_update(tkey_i2c);
	if (ret < 0)
		return ret;
#endif
	return 0;
}

static void __exit touchkey_exit(void)
{
	i2c_del_driver(&touchkey_i2c_driver);
	touchkey_probe = false;
}

late_initcall(touchkey_init);
module_exit(touchkey_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("@@@");
MODULE_DESCRIPTION("touch keypad");
