/*******************************************************************************
* Copyright 2013 Broadcom Corporation.  All rights reserved.
*
*       @file   drivers/misc/ltr559.c
*
* Unless you and Broadcom execute a separate written software license agreement
* governing use of this software, this software is licensed to you under the
* terms of the GNU General Public License version 2, available at
* http://www.gnu.org/copyleft/gpl.html (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a license
* other than the GPL, without Broadcom's express prior written consent.
*******************************************************************************/
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/timer.h>
#include <linux/wakelock.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>
#include <linux/hrtimer.h>
#include <linux/ltr559als.h>
#define LTR559ALS_RUNTIME_PM
/*#define CONFIG_LTR559ALS_MD_TEST*/
/* driver data */
struct ltr559als_data {
	struct ltr559als_pdata *pdata;
	struct input_dev *proximity_input_dev;
	struct input_dev *light_input_dev;
	struct i2c_client *i2c_client;
	struct work_struct work_light;
	struct work_struct work_prox;
	struct mutex power_lock;
	struct wake_lock prox_wake_lock;
	struct workqueue_struct *light_wq;
	struct workqueue_struct *prox_wq;
	struct hrtimer timer_light;
#if defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
	ktime_t als_delay;
	u16 crosstalk;
	u8 power_state;
#ifdef CONFIG_LTR559ALS_MD_TEST
	struct delayed_work	md_test_work;
#endif
	int irq;
};

static int raw_data_debug;
static u16 prox_threshold_hi_def;
static u16 prox_threshold_lo_def;
#ifdef LTR559ALS_RUNTIME_PM
static struct regulator *ltr559als_regltr_3v;
static struct regulator *ltr559als_regltr_1v8;
static int ltr559als_power_on_off(bool flag);
static int ltr559als_cs_power_on_off(bool flag);
#endif
static int _ltr559als_suspend(struct i2c_client *client, pm_message_t mesg);
static int _ltr559als_resume(struct i2c_client *client);

static int ltr559als_i2c_read(struct ltr559als_data *ltr559als, u8 addr,
		u8 *val)
{
	int err;
	u8 buf[2] = {addr, 0};
	struct i2c_msg msgs[] = {
		{
			.addr = ltr559als->i2c_client->addr,
			.flags = ltr559als->i2c_client->flags & I2C_M_TEN,
			.len = 1,
			.buf = buf,
		},
		{
			.addr = ltr559als->i2c_client->addr,
			.flags = (ltr559als->i2c_client->flags & I2C_M_TEN)
				| I2C_M_RD,
			.len = 1,
			.buf = buf,
		},
	};

	err = i2c_transfer(ltr559als->i2c_client->adapter, msgs, 2);
	if (err != 2) {
		dev_err(&ltr559als->i2c_client->dev, "read transfer error\n");
		err = -EIO;
	} else {
		err = 0;
		*val = buf[0];
	}
	return err;
}

static int ltr559als_i2c_write(struct ltr559als_data *ltr559als, u8 addr,
	u8 val)
{
	int err;
	u8 buf[2] = { addr, val };
	struct i2c_msg msgs[] = {
		{
			.addr = ltr559als->i2c_client->addr,
			.flags = ltr559als->i2c_client->flags & I2C_M_TEN,
			.len = 2,
			.buf = buf,
		},
	};
	err = i2c_transfer(ltr559als->i2c_client->adapter, msgs, 1);
	if (err != 1) {
		dev_err(&ltr559als->i2c_client->dev, "write transfer error\n");
		err = -EIO;
	} else
		err = 0;
	return err;
}

static int ltr559als_drdy(struct ltr559als_data *data, u8 drdy_mask)
{
	int tries = 100;
	int ret;

	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->i2c_client,
					       LTR559ALS_ALS_PS_STATUS);
		if (ret < 0)
			return ret;
		if ((ret & drdy_mask) == drdy_mask)
			return 0;
	}
	return -EIO;
}

static int ltr559als_read_als(struct ltr559als_data *data, __le16 buf[2])
{
	int ret = ltr559als_drdy(data, LTR559ALS_STATUS_ALS_RDY);
	if (ret < 0)
		return ret;
	/* always read both ALS channels in given order */
	return i2c_smbus_read_i2c_block_data(data->i2c_client,
					     LTR559ALS_ALS_DATA1,
					     2 * sizeof(__le16), (u8 *) buf);
}

static int ltr559als_read_ps(struct ltr559als_data *data)
{
	int ret = ltr559als_drdy(data, LTR559ALS_STATUS_PS_RDY);
	if (ret < 0)
		return ret;
	return i2c_smbus_read_word_data(data->i2c_client, LTR559ALS_PS_DATA);
}

static int ltr559als_i2c_burst_write(struct ltr559als_data *ltr559als,
		u8 addr, u16 val)
{
	int ret;
	ret = 0;
	ret = i2c_smbus_write_word_data(ltr559als->i2c_client, addr, val);
	return ret;
}

#ifdef LTR559ALS_RUNTIME_PM
static int ltr559als_power_on_off(bool flag)
{
	int ret;
	if (!ltr559als_regltr_3v) {
		pr_err("ltr559als_regltr is unavailable\n");
		return -1;
	}

	if ((flag == 1)) {
		pr_info("\n LDO on %s ", __func__);
		ret = regulator_enable(ltr559als_regltr_3v);
		if (ret)
			pr_err("ltr559als 3v Regulator-enable failed\n");
	} else if ((flag == 0)) {
		pr_info("\n LDO off %s ", __func__);
		ret = regulator_disable(ltr559als_regltr_3v);
		if (ret)
			pr_err("ltr559als 3v Regulator disable failed\n");
	}
	return ret;
}

static int ltr559als_cs_power_on_off(bool flag)
{
	int ret;
	if (!ltr559als_regltr_1v8) {
		pr_err("ltr559als_regltr is unavailable\n");
		return -1;
	}
	if ((flag == 1)) {
		pr_info("\n LDO on %s ", __func__);
		ret = regulator_enable(ltr559als_regltr_1v8);
		if (ret)
			pr_err("ltr559als 1.8v Regulator-enable failed\n");
	} else if ((flag == 0)) {
		pr_info("\n LDO off %s ", __func__);
		ret = regulator_disable(ltr559als_regltr_1v8);
		if (ret)
			pr_err("ltr559als 1.8v Regulator disable failed\n");
	}
	return ret;
}
#endif
static void ltr559als_alp_enable(struct ltr559als_data *ltr559als,
				bool enable_als, bool enable_ps)
{
	int ret;
	u8 reg;
	u8 reg1;
	u8 reg2;
	ret = 0;
	pr_info("ltr559als_alp_enable: als=%d, ps=%d\n", enable_als, enable_ps);
	if (enable_als == 1 && enable_ps == 1) {
		ret = ltr559als_i2c_read(ltr559als, LTR559ALS_INTERRUPT, &reg);
		if (ret < 0)
			pr_err("[ltr559als] read LTR559ALS_INTERRUPT err\n");
		reg |= LTR559ALS_INTR_MODE;
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_INTERRUPT, reg);
			pr_err("[ltr559als] write LTR559ALS_INTERRUPT err\n");
#if 0
		ret = ltr559als_i2c_read(ltr559als, LTR559ALS_ALS_CONTR, &reg);
		if (ret < 0)
			pr_err("[ltr559als] read LTR559ALS_ALS_CONTR err\n");
		reg |= LTR559ALS_CONTR_ACTIVE;
#endif
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_ALS_CONTR, 0x01);
		if (ret < 0)
			pr_err("[ltr559als] write LTR559ALS_ALS_CONTR err\n");

		ret = ltr559als_i2c_read(ltr559als, LTR559ALS_PS_CONTR, &reg);
		if (ret < 0)
			pr_err("[ltr559als] read LTR559ALS_PS_CONTR err\n");
		reg |= LTR559ALS_CONTR_ACTIVE;
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_PS_CONTR, reg);
		if (ret < 0)
			pr_err("[ltr559als] write LTR559ALS_PS_CONTR err\n");
	} else if (enable_als == 1 && enable_ps == 0) {
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_INTERRUPT,
				ltr559als->pdata->interrupt);
		if (ret < 0)
			pr_err("[ltr559als] write LTR559ALS_INTERRUPT err\n");
		/*ret = ltr559als_i2c_read(ltr559als, LTR559ALS_ALS_CONTR,
			&reg);
		if (ret < 0)
			pr_err("[ltr559als] read LTR559ALS_ALS_CONTR err\n");
		pr_info("[WYQ] als count =0x%x\n", reg);
		reg |= LTR559ALS_CONTR_ACTIVE;
		pr_info("[WYQ] als count write =0x%x\n", reg);*/
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_ALS_CONTR, 0x01);
		if (ret < 0)
			pr_err("[ltr559als] write LTR559ALS_ALS_CONTR err\n");
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_PS_CONTR,
				ltr559als->pdata->ps_contr);
		if (ret < 0)
			pr_err("[ltr559als] write LTR559ALS_PS_CONTR err\n");

	} else if (enable_als == 0 && enable_ps == 1) {
		ret = ltr559als_i2c_read(ltr559als, LTR559ALS_INTERRUPT, &reg);
		if (ret < 0)
			pr_err("[ltr559als] read LTR559ALS_INTERRUPT err\n");
		reg |= LTR559ALS_INTR_MODE;
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_INTERRUPT, reg);
		if (ret < 0)
			pr_err("[ltr559als] write LTR559ALS_INTERRUPT err\n");
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_ALS_CONTR, 0x0);
		if (ret < 0)
			pr_err("[ltr559als] write LTR559ALS_ALS_CONTR err\n");
		ret = ltr559als_i2c_read(ltr559als, LTR559ALS_PS_CONTR, &reg);
		if (ret < 0)
			pr_err("[ltr559als] read LTR559ALS_PS_CONTR err\n");
		reg |= LTR559ALS_CONTR_ACTIVE;
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_PS_CONTR, reg);
		if (ret < 0)
			pr_err("[ltr559als] write LTR559ALS_PS_CONTR err\n");

	} else if (enable_als == 0 && enable_ps == 0) {
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_INTERRUPT,
				ltr559als->pdata->interrupt);
		if (ret < 0)
			pr_err("[ltr559als] write LTR559ALS_INTERRUPT err\n");
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_ALS_CONTR,
				0x0);
		if (ret < 0)
			pr_err("[ltr559als] write LTR559ALS_ALS_CONTR err\n");
		ret = ltr559als_i2c_write(ltr559als, LTR559ALS_PS_CONTR,
			ltr559als->pdata->ps_contr);
		if (ret < 0)
			pr_err("[ltr559als] write LTR559ALS_PS_CONTR err\n");
	}
	/*chip bug, need reading to clear interrupt first*/
	ret = ltr559als_i2c_read(ltr559als, LTR559ALS_ALS_PS_STATUS, &reg);
	if (ret < 0)
		pr_err("[ltr559als] read LTR559ALS_ALS_PS_STATUS err\n");
	ltr559als_i2c_read(ltr559als, LTR559ALS_INTERRUPT, &reg);
	ltr559als_i2c_read(ltr559als, LTR559ALS_PS_CONTR, &reg1);
	ltr559als_i2c_read(ltr559als, LTR559ALS_ALS_CONTR, &reg2);
}

static int ltr559als_als_threshold_set(struct ltr559als_data *ltr559als,
		u16 tl, u16 th)
{
	int ret;
	ret = 0;
	ret = ltr559als_i2c_burst_write(ltr559als, LTR559ALS_ALS_THRES_LOW, tl);
	if (ret < 0)
		pr_err("[ltr559als] write LTR559ALS_ALS_THRES_LOW err=%d\n",
					ret);
	ret = ltr559als_i2c_burst_write(ltr559als, LTR559ALS_ALS_THRES_UP, th);
	if (ret < 0)
		pr_err("[ltr559als] write LTR559ALS_ALS_THRES_UP err=%d\n",
				ret);
	return ret;

}

static int ltr559als_ps_threshold_set(struct ltr559als_data *ltr559als, u16 tl,
	u16 th)
{
	int ret;
	ret = 0;
	ret = ltr559als_i2c_burst_write(ltr559als, LTR559ALS_PS_THRES_UP, th);
	if (ret < 0)
		pr_err("[ltr559als] write LTR559ALS_PS_THRES_UP err=%d\n", ret);
	ret = ltr559als_i2c_burst_write(ltr559als, LTR559ALS_PS_THRES_LOW, tl);
	if (ret < 0)
		pr_err("[ltr559als] write LTR559ALS_PS_THRES_LOW err=%d\n",
					ret);
	return ret;
}

static int ltr559als_proximity_calibration(struct ltr559als_data *ltr559als,
					u8 step, u8 max_steps)
{
	int ret = 0;
	u8 prox_state = 0;
	u16 ps_data;
	u32 avg, min, max;
	u32 sum = 0;
	u8 int_reg = 0;
	u8 ps_rate = 0;
	int i;

	if (!ltr559als)
		return -EINVAL;

	if (!(ltr559als->power_state & PROXIMITY_ENABLED)) {
		ltr559als->power_state |= PROXIMITY_ENABLED;
		prox_state = 1;
		if (ltr559als->power_state & LIGHT_ENABLED)
			ltr559als_alp_enable(ltr559als, true, true);
		else
			ltr559als_alp_enable(ltr559als, false, true);
	}

	ret = ltr559als_i2c_read(ltr559als, LTR559ALS_INTERRUPT, &int_reg);
	if (ret < 0) {
		pr_err("[ltr559als] read LTR559ALS_INTERRUPT err\n");
		return ret;
	}
	ret = ltr559als_i2c_write(ltr559als, LTR559ALS_INTERRUPT, 0x0);
	if (ret < 0) {
		pr_err("[ltr559als], set LTR559ALS_INTERRUPT fail: %d\n", ret);
		return ret;
	}

	ret = ltr559als_i2c_read(ltr559als, LTR559ALS_PS_MEAS_RATE, &ps_rate);
	if (ret < 0) {
		pr_err("[ltr559als], read LTR559ALS_PS_MEAS_RATE fail: %d\n",
				ret);
		return ret;
	}
	ret = ltr559als_i2c_write(ltr559als, LTR559ALS_PS_MEAS_RATE, 0x0);
	if (ret < 0) {
		pr_err("[ltr559als], set LTR559ALS_PS_MEAS_RATE fail: %d\n",
					ret);
		return ret;
	}
	msleep(50);

	sum = 0;
	min = 2047;
	max = 0;
	for (i = 0; i < 32; i++) {
		ps_data = ltr559als_read_ps(ltr559als);
		ps_data &= LTR559ALS_PS_DATA_MASK;
		sum += ps_data;
		if (ps_data < min)
			min = ps_data;
		if (ps_data > max)
			max = ps_data;
		msleep(100);
	}
	avg = sum >> 5;

	pr_info("(min avg max): %d, %d, %d\n", min, avg, max);
	ltr559als->crosstalk = avg;

	ret = ltr559als_i2c_write(ltr559als, LTR559ALS_INTERRUPT, int_reg);
	if (ret < 0)
		pr_err("[ltr559als], set LTR559ALS_INTERRUPT fail: %d\n", ret);

	ret = ltr559als_i2c_write(ltr559als, LTR559ALS_PS_MEAS_RATE, ps_rate);
	if (ret < 0)
		pr_err("[ltr559als], set LTR559ALS_INTERRUPT fail: %d\n", ret);

	if (prox_state) {
		if (ltr559als->power_state & LIGHT_ENABLED)
			ltr559als_alp_enable(ltr559als, true, false);
		else
			ltr559als_alp_enable(ltr559als, false, false);
		ltr559als->power_state &= ~PROXIMITY_ENABLED;
		prox_state = 0;
	}
	return ret;
}

/*sysfs interface support*/
static ssize_t ltr559als_proximity_enable_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	return sprintf(buf, "%d\n", ltr559als->power_state & PROXIMITY_ENABLED);
}

static ssize_t ltr559als_proximity_enable_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t size)
{
	struct ltr559als_data *ltr559als = dev_get_drvdata(dev);
	bool new_value;
	if (sysfs_streq(buf, "1"))
		new_value = true;
	else if (sysfs_streq(buf, "0"))
		new_value = false;
	else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	mutex_lock(&ltr559als->power_lock);
	if (new_value && !(ltr559als->power_state & PROXIMITY_ENABLED)) {
		ltr559als->power_state |= PROXIMITY_ENABLED;
		wake_lock(&ltr559als->prox_wake_lock);
		if (ltr559als->power_state & LIGHT_ENABLED) {
			ltr559als_alp_enable(ltr559als, false, false);
			ltr559als_alp_enable(ltr559als, true, true);
		} else
			ltr559als_alp_enable(ltr559als, false, true);

		input_report_abs(ltr559als->proximity_input_dev,
				ABS_DISTANCE, -1);
		input_sync(ltr559als->proximity_input_dev);

		enable_irq(ltr559als->irq);
		mutex_unlock(&ltr559als->power_lock);
	} else if (!new_value && (ltr559als->power_state & PROXIMITY_ENABLED)) {
		if (ltr559als->power_state & LIGHT_ENABLED)
			ltr559als_alp_enable(ltr559als, true, false);
		else
			ltr559als_alp_enable(ltr559als, false, false);
		ltr559als->power_state &= ~PROXIMITY_ENABLED;
		wake_unlock(&ltr559als->prox_wake_lock);
		disable_irq_nosync(ltr559als->irq);
		mutex_unlock(&ltr559als->power_lock);
		cancel_work_sync(&ltr559als->work_prox);
	} else
		mutex_unlock(&ltr559als->power_lock);
#ifdef CONFIG_LTR559ALS_MD_TEST
	schedule_delayed_work(&ltr559als->md_test_work, msecs_to_jiffies(100));
#endif
	return size;
}

static ssize_t ltr559als_enable_als_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	return sprintf(buf, "%d\n", ltr559als->power_state & LIGHT_ENABLED);
}

static ssize_t ltr559als_enable_als_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct ltr559als_data *ltr559als = dev_get_drvdata(dev);
	bool new_value;

	if (sysfs_streq(buf, "1"))
		new_value = true;
	else if (sysfs_streq(buf, "0"))
		new_value = false;
	else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	mutex_lock(&ltr559als->power_lock);
	if (new_value && !(ltr559als->power_state & LIGHT_ENABLED)) {
		ltr559als->power_state |= LIGHT_ENABLED;
		if (ltr559als->power_state & PROXIMITY_ENABLED)
			ltr559als_alp_enable(ltr559als, true, true);
		else
			ltr559als_alp_enable(ltr559als, true, false);
		mutex_unlock(&ltr559als->power_lock);
		hrtimer_start(&ltr559als->timer_light,
			      ktime_set(0, LIGHT_SENSOR_START_TIME_DELAY),
			      HRTIMER_MODE_REL);

	} else if (!new_value && (ltr559als->power_state & LIGHT_ENABLED)) {
		if (ltr559als->power_state & PROXIMITY_ENABLED)
			ltr559als_alp_enable(ltr559als, false, true);
		else
			ltr559als_alp_enable(ltr559als, false, false);
		ltr559als->power_state &= ~LIGHT_ENABLED;
		mutex_unlock(&ltr559als->power_lock);
		hrtimer_cancel(&ltr559als->timer_light);
		cancel_work_sync(&ltr559als->work_light);
	} else
		mutex_unlock(&ltr559als->power_lock);
	return size;
}

static ssize_t ltr559als_proximity_threshold_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	return sprintf(buf, "%d,%d\n", ltr559als->pdata->prox_threshold_high,
		       ltr559als->pdata->prox_threshold_low);
}

static ssize_t ltr559als_proximity_offset_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	return sprintf(buf, "%d", ltr559als->crosstalk);
}

static ssize_t ltr559als_proximity_offset_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int x;
	int hi;
	int lo;
	int i;
	int err = -EINVAL;
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	u16 prox_offset = ltr559als->pdata->prox_offset;

	err = sscanf(buf, "%d", &x);
	if (err != 1) {
		pr_err("invalid parameter number: %d\n", err);
		return err;
	}

	ltr559als->crosstalk = x;
	hi = prox_threshold_hi_def + ltr559als->crosstalk - prox_offset;
	lo = prox_threshold_lo_def + ltr559als->crosstalk - prox_offset;

	pr_info("crosstalk=%d,prox_offset=%d, hi =%d, lo=%d\n",
		ltr559als->crosstalk, prox_offset, hi, lo);

	if (hi > 0x7ff || lo > 0x7ff) {
		ltr559als->crosstalk = 0;
		pr_err("isl290xx sw cali failed\n");
	} else {
		if (lo > ltr559als->crosstalk) {
			ltr559als->pdata->prox_threshold_high  = hi;
			ltr559als->pdata->prox_threshold_low = lo;
		} else {
			for (i = 0; i < 100; i++) {
				hi++;
				lo++;

				if (lo > ltr559als->crosstalk) {
					ltr559als->pdata->prox_threshold_high =
						hi + 1;
					ltr559als->pdata->prox_threshold_low =
						lo;
					break;
				}
			}
		}
	}

	return count;
}

static ssize_t ltr559als_prox_cali_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	ret = ltr559als_proximity_calibration(ltr559als, 1, 100);
	if (ret < 0)
		return ret;
	return count;

}

static ssize_t ltr559als_registers_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	unsigned i, n, reg_count;
	u8 value;
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	ret = 0;
	reg_count = sizeof(ltr559als_regs) / sizeof(ltr559als_regs[0]);
	for (i = 0, n = 0; i < reg_count; i++) {
		ret = ltr559als_i2c_read(ltr559als, ltr559als_regs[i].reg,
				&value);
		if (ret < 0)
			return ret;
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "%-20s = 0x%02X\n", ltr559als_regs[i].name,
					value);
	}
	return n;
}

static ssize_t ltr559als_registers_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	unsigned i, reg_count, value;
	int ret;
	char name[30];
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	if (count >= 30)
		return -EFAULT;
	if (sscanf(buf, "%30s %x", name, &value) != 2) {
		pr_err("input invalid\n");
		return -EFAULT;
	}
	reg_count = sizeof(ltr559als_regs) / sizeof(ltr559als_regs[0]);
	for (i = 0; i < reg_count; i++) {
		if (!strcmp(name, ltr559als_regs[i].name)) {
			ret = ltr559als_i2c_write(ltr559als,
						ltr559als_regs[i].reg,
						value);
			if (ret) {
				pr_err("Failed to write register %s\n", name);
				return -EFAULT;
			}
			return count;
		}
	}
	pr_err("no such register %s\n", name);
	return -EFAULT;
}

static ssize_t ltr559als_raw_data_debug_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", raw_data_debug);
}

static ssize_t ltr559als_raw_data_debug_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	if (sysfs_streq(buf, "1"))
		raw_data_debug = 1;
	else if (sysfs_streq(buf, "0"))
		raw_data_debug = 0;

	return size;
}

static DEVICE_ATTR(enable_als, 0644,
		   ltr559als_enable_als_show, ltr559als_enable_als_store);
static DEVICE_ATTR(enable_ps, 0644,
		ltr559als_proximity_enable_show,
		ltr559als_proximity_enable_store);
static DEVICE_ATTR(offset, 0644, ltr559als_proximity_offset_show,
		ltr559als_proximity_offset_store);
static DEVICE_ATTR(prox_cali, 0644,
		ltr559als_proximity_threshold_show,
		ltr559als_prox_cali_store);
static DEVICE_ATTR(registers, 0644, ltr559als_registers_show,
		ltr559als_registers_store);
static DEVICE_ATTR(raw_data_debug, 0644, ltr559als_raw_data_debug_show,
		ltr559als_raw_data_debug_store);


static struct attribute *ltr559als_attributes[] = {
	&dev_attr_enable_als.attr,
	&dev_attr_enable_ps.attr,
	&dev_attr_offset.attr,
	&dev_attr_prox_cali.attr,
	&dev_attr_registers.attr,
	&dev_attr_raw_data_debug.attr,
	NULL
};

static const struct attribute_group ltr559als_attr_group = {
	.attrs = ltr559als_attributes,
};

/* we don't do any filter about lux, it can be done by framwork*/
#if 0
static unsigned int ltr559als_als_data_filter(unsigned int als_data)
{
	u16 als_level[LTR559ALS_ALS_LEVEL_NUM] = {
		0, 50, 100, 150, 200,
		250, 300, 350, 400, 450,
		550, 650, 750, 900, 1100
	};
	u16 als_value[LTR559ALS_ALS_LEVEL_NUM] = {
		0, 50, 100, 150, 200,
		250, 300, 350, 1000, 1200,
		1900, 2500, 7500, 15000, 25000
	};
	u8 idx;
	for (idx = 0; idx < LTR559ALS_ALS_LEVEL_NUM; idx++) {
		if (als_data < als_value[idx])
			break;
	}
	if (idx >= LTR559ALS_ALS_LEVEL_NUM) {
		pr_err("ltr559als als data to level: exceed range.\n");
		idx = LTR559ALS_ALS_LEVEL_NUM - 1;
	}

	return als_level[idx];
}
#endif

static int ltr559als_get_als(struct ltr559als_data *ltr559als, u16 *als_data)
{
	u16 raw_als_lo;
	u16 raw_als_hi;
	__le16 buf[2];
	int ratio;
	int ret = 0;
	int als_flt;
	int als_int;
	u8 reg = 0;
	u8 scale_factor;

	ret = ltr559als_read_als(ltr559als, buf);
	if (ret < 0) {
		pr_err("[ltr559als] read als err\n");
		return ret;
	}
	raw_als_lo = le16_to_cpu(buf[1]);
	raw_als_hi = le16_to_cpu(buf[0]);
	if ((raw_als_lo + raw_als_hi) == 0) {
		pr_err("als dataare zero\n");
		ratio = 1000;
	} else {
		ratio = (raw_als_hi * 1000) / (raw_als_lo + raw_als_hi);
	}
	if (ratio < 450) {
		als_flt = (17743 * raw_als_lo) + (11059 * raw_als_hi);
		als_flt = als_flt / 10000;
	} else if ((ratio >= 450) && (ratio < 640)) {
		als_flt = (42765 * raw_als_lo) - (19548 * raw_als_hi);
		als_flt = als_flt / 10000;
	} else if ((ratio >= 640) && (ratio < 900)) {
		als_flt = (5926 * raw_als_lo) + (1185 * raw_als_hi);
		als_flt = als_flt / 10000;
	} else {
		als_flt = 0;
	}
	als_int = als_flt;
	if ((als_flt - als_int) > 0.5)
		als_int = als_int + 1;
	else
		als_int = als_flt;

	scale_factor = ltr559als->pdata->scale_factor;
	ret = ltr559als_i2c_read(ltr559als, LTR559ALS_ALS_PS_STATUS, &reg);
	if (ret < 0) {
		pr_err("[ltr559als] read LTR559ALS_ALS_PS_STATUS err\n");
		return ret;
	}
	if (!(reg & 0x10))
		*als_data = (u16)(als_int * scale_factor);
	else
		*als_data = (u16)als_int;
	return ret;
}

static void ltr559als_work_func_light(struct work_struct *work)
{
	u16 als;
	int ret;
	struct ltr559als_data *ltr559als = container_of(work,
					struct ltr559als_data,
					work_light);
	ret = 0;
	ret = ltr559als_get_als(ltr559als, &als);
	if (ret < 0)
		return;
	input_report_abs(ltr559als->light_input_dev, ABS_MISC, als);
	input_sync(ltr559als->light_input_dev);
}

/* light timer function */
static enum hrtimer_restart ltr559als_light_timer_func(struct hrtimer *timer)
{
	struct ltr559als_data *ltr559als =
			container_of(timer, struct ltr559als_data, timer_light);
	queue_work(ltr559als->light_wq, &ltr559als->work_light);
	hrtimer_forward_now(&ltr559als->timer_light, ltr559als->als_delay);
	return HRTIMER_RESTART;
}

#ifdef CONFIG_LTR559ALS_MD_TEST
static void ltr559als_mdtest_work_func(struct work_struct *work)
{
	u16 pdata;
	int ret;
	struct ltr559als_data *ltr559als = container_of(work,
					struct ltr559als_data,
					md_test_work.work);
	ret = 0;
	pdata = ltr559als_read_ps(ltr559als);
	pr_info("[md test] ltr559als prox data = %d\n", pdata);
	schedule_delayed_work(&ltr559als->md_test_work, msecs_to_jiffies(100));
}
#else
static void ltr559als_work_func_prox(struct work_struct *work)
{
	u16 pdata;
	u8 ps;
	int ret;
	struct ltr559als_data *ltr559als = container_of(work,
					struct ltr559als_data,
					work_prox);
	ret = 0;
	pdata = ltr559als_read_ps(ltr559als);
	ps = 0;
	if (pdata > ltr559als->pdata->prox_threshold_high) {
		ps = 0;
		ltr559als_ps_threshold_set(ltr559als,
					ltr559als->pdata->prox_threshold_low,
					ltr559als->pdata->prox_threshold_high);
	} else if (pdata < ltr559als->pdata->prox_threshold_low) {
		ps = 1;
		ltr559als_ps_threshold_set(ltr559als,
					ltr559als->pdata->prox_threshold_low,
					ltr559als->pdata->prox_threshold_high);
	}
	if (raw_data_debug)
		input_report_abs(ltr559als->proximity_input_dev,
						ABS_DISTANCE, pdata);
	else
		input_report_abs(ltr559als->proximity_input_dev,
						ABS_DISTANCE, ps);
	input_sync(ltr559als->proximity_input_dev);
	enable_irq(ltr559als->irq);

}
static irqreturn_t ltr559als_irq_thread_fn(int irq, void *data)
{
	struct ltr559als_data *ltr559als = data;
	disable_irq_nosync(ltr559als->irq);
	queue_work(ltr559als->prox_wq, &ltr559als->work_prox);
	return IRQ_HANDLED;
}
#endif
#ifndef CONFIG_LTR559ALS_MD_TEST
static int ltr559als_setup_irq(struct ltr559als_data *ltr559als)
{
	int ret = -EIO;
	pr_info("ltr559als_setup_irq =%d\n", ltr559als->i2c_client->irq);
	ltr559als->irq = ltr559als->i2c_client->irq;
	if (ltr559als->irq)
		ret = request_threaded_irq(ltr559als->irq, NULL,
					&ltr559als_irq_thread_fn,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"ltr559als", ltr559als);
	else
		ret = -ENODEV;

	if (ret < 0) {
		pr_err("%s: request_irq(%d) failed for irq %d\n",
		       __func__, ltr559als->irq, ret);
		return ret;
	}

	/* start with interrupts disabled */
	disable_irq(ltr559als->irq);

	return ret;
}
#endif
static int ltr559als_chip_init(struct ltr559als_data *ltr559als)
{
	int ret;

	ret = 0;

	ret = ltr559als_als_threshold_set(ltr559als, 0, 0xFFFF);
	if (ret < 0)
		pr_err("[ltr559als]fail to configure als thresold\n");

	ret = ltr559als_ps_threshold_set(ltr559als,
				ltr559als->pdata->prox_threshold_low,
				ltr559als->pdata->prox_threshold_high);
	if (ret < 0)
		pr_err("[ltr559als]fail to configure prox threshold\n");
	ret = ltr559als_i2c_write(ltr559als, LTR559ALS_ALS_CONTR,
				0x0);
	if (ret < 0)
		pr_err("[ltr559als] init LTR559ALS_ALS_CONTR  err=%d\n", ret);
	ret = ltr559als_i2c_write(ltr559als, LTR559ALS_PS_CONTR,
					ltr559als->pdata->ps_contr);
	if (ret < 0)
		pr_err("[ltr559als] init LTR559ALS_PS_CONTR  err=%d\n", ret);
	ret = ltr559als_i2c_write(ltr559als, LTR559ALS_PS_LED,
					ltr559als->pdata->ps_led);
	if (ret < 0)
		pr_err("[ltr559als] init LTR559ALS_PS_LED  err=%d\n", ret);

	ret = ltr559als_i2c_write(ltr559als, LTR559ALS_PS_N_PULSES,
					ltr559als->pdata->ps_n_pulses);
	if (ret < 0)
		pr_err("[ltr559als] init LTR559ALS_PS_N_PULSES err=%d\n", ret);
	ret =
	    ltr559als_i2c_write(ltr559als, LTR559ALS_PS_MEAS_RATE,
					ltr559als->pdata->ps_meas_rate);
	if (ret < 0)
		pr_err("[ltr559als] init LTR559ALS_PS_MEAS_RATE err=%d\n", ret);

	ret = ltr559als_i2c_write(ltr559als, LTR559ALS_ALS_MEAS_RATE,
					ltr559als->pdata->als_meas_rate);
	if (ret < 0)
		pr_err("[ltr559als] init LTR559ALS_ALS_MEAS_RATE err=%d\n",
					ret);

	ret = ltr559als_i2c_write(ltr559als, LTR559ALS_INTERRUPT,
					ltr559als->pdata->interrupt);
	if (ret < 0)
		pr_err("[ltr559als] init LTR559ALS_INTERRUPT err=%d\n", ret);
	ret =
	    ltr559als_i2c_write(ltr559als, LTR559ALS_INTERRUPT_PERSIST,
			     ltr559als->pdata->interrupt_persist);
	if (ret < 0)
		pr_err("[ltr559als]LTR559ALS_INTERRUPT_PERSIST err=%d\n", ret);

	return ret;

}

#ifdef CONFIG_HAS_EARLYSUSPEND
void ltr559als_early_suspend(struct early_suspend *h)
{
	struct ltr559als_data *ltr559als = container_of(h,
						  struct ltr559als_data,
						  early_suspend);
	pm_message_t mesg = {
		.event = PM_EVENT_SUSPEND,
	};
	_ltr559als_suspend(ltr559als->i2c_client, mesg);
}

static void ltr559als_late_resume(struct early_suspend *desc)
{
	struct ltr559als_data *ltr559als = container_of(desc,
						  struct ltr559als_data,
						  early_suspend);
	_ltr559als_resume(ltr559als->i2c_client);
}
#endif

static int ltr559als_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret = -ENODEV;
	struct input_dev *input_dev;
	struct ltr559als_data *ltr559als;
	struct device_node *np;
	u32 val;
	np = NULL;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: i2c functionality check failed!\n", __func__);
		return ret;
	}
#ifdef LTR559ALS_RUNTIME_PM
	ltr559als_regltr_3v = regulator_get(NULL, "sensor_3v");
	ltr559als_regltr_1v8 = regulator_get(NULL, "sensor_1v8");
	if (IS_ERR_OR_NULL(ltr559als_regltr_3v) ||
					IS_ERR_OR_NULL(ltr559als_regltr_1v8)) {
		pr_err("%s: Get ltr559als regulator Failed\n", __func__);
		return -EBUSY;
	}
	regulator_set_voltage(ltr559als_regltr_3v, 3000000, 3000000);
	ret = regulator_enable(ltr559als_regltr_3v);
	if (ret)
		pr_err("ltr559als 3v Regulator Enable Failed\n");

	regulator_set_voltage(ltr559als_regltr_1v8, 1800000, 1800000);
	ret = regulator_enable(ltr559als_regltr_1v8);
	if (ret)
		pr_err("ltr559als 1v8 Regulator enable failed\n");

#endif
	ltr559als = kzalloc(sizeof(struct ltr559als_data), GFP_KERNEL);
	if (!ltr559als) {
		pr_err("%s: failed to alloc memory for module data\n",
		       __func__);
		return -ENOMEM;
	}
	ltr559als->i2c_client = client;
	i2c_set_clientdata(client, ltr559als);

	if (!client->dev.platform_data) {
		ltr559als->pdata = kzalloc(sizeof(struct ltr559als_pdata),
					GFP_KERNEL);
		if (!ltr559als->pdata) {
			pr_err("%s: failed to alloc memory for module data\n",
					__func__);
			goto err_alloc_mem;
		}
		np = ltr559als->i2c_client->dev.of_node;
		ret = of_property_read_u32(np, "gpio-irq-pin", &val);
		if (!ret) {
			gpio_request(val, "prox_int");
			gpio_direction_input(val);
			client->irq = gpio_to_irq(val);
		} else {
			pr_info("[ltr559]gpio-irq-pin is not found in dts\n");
			client->irq = 0;
		}
		/* chip configuration */
		ret = of_property_read_u32(np, "als_contr", &val);
		if (!ret)
			ltr559als->pdata->als_contr = val;
		else {
			pr_info("[ltr559als] als_contr is not found in dts\n");
			ltr559als->pdata->als_contr = 0x01;
		}
		ret = of_property_read_u32(np, "ps_contr", &val);
		if (!ret)
			ltr559als->pdata->ps_contr = val;
		else {
			pr_info("[ltr559als] ps_contr is not found in dts\n");
			ltr559als->pdata->ps_contr = 0x03;
		}
		ret = of_property_read_u32(np, "ps_led", &val);
		if (!ret)
			ltr559als->pdata->ps_led = val;
		else {
			pr_info("[ltr559als] ps_led is not found in dts\n");
			ltr559als->pdata->ps_led = 0x7F;
		}

		ret = of_property_read_u32(np, "ps_n_pusles", &val);
		if (!ret)
			ltr559als->pdata->ps_n_pulses = val;
		else {
			pr_info("[ltr559] ps_n_pusles is not found in dts\n");
			ltr559als->pdata->ps_n_pulses = 0x08;
		}

		ret = of_property_read_u32(np, "ps_meas_rate", &val);
		if (!ret)
			ltr559als->pdata->ps_meas_rate = val;
		else {
			pr_info("[ltr559] ps_meas_rate is not found in dts\n");
			ltr559als->pdata->ps_meas_rate = 0x02;
		}

		ret = of_property_read_u32(np, "als_meas_rate", &val);
		if (!ret)
			ltr559als->pdata->als_meas_rate = val;
		else {
			pr_info("[ltr559als] als_meas_rate is not in dts\n");
			ltr559als->pdata->als_meas_rate = 0x03;
		}
		ret = of_property_read_u32(np, "interrupt", &val);
		if (!ret)
			ltr559als->pdata->interrupt = val;
		else {
			pr_info("[ltr559als] interrupt is not in dts\n");
			ltr559als->pdata->interrupt = 0x01;
		}
		ret = of_property_read_u32(np, "interrupt_persist", &val);
		if (!ret)
			ltr559als->pdata->interrupt_persist = val;
		else {
			pr_info("[ltr559] interrupt_persist is not in dts\n");
			ltr559als->pdata->interrupt_persist = 0x02;
		}
		ret = of_property_read_u32(np, "prox_threshold_low", &val);
		if (!ret)
			ltr559als->pdata->prox_threshold_low = val;
		else {
			pr_info("[ltr559] low prox threshold is not in dts\n");
			ltr559als->pdata->prox_threshold_low = 140;
		}
		prox_threshold_lo_def = ltr559als->pdata->prox_threshold_low;

		ret = of_property_read_u32(np, "prox_threshold_high", &val);
		if (!ret)
			ltr559als->pdata->prox_threshold_high = val;
		else {
			pr_info("[ltr559als] high pthreshold is not in dts\n");
			ltr559als->pdata->prox_threshold_high = 1000;
		}
		prox_threshold_hi_def = ltr559als->pdata->prox_threshold_high;

		ret = of_property_read_u32(np, "prox_offset_param", &val);
		if (!ret)
			ltr559als->pdata->prox_offset = val;
		else
			ltr559als->pdata->prox_offset = 0;

		ret = of_property_read_u32(np, "scale_factor", &val);
		if (!ret)
			ltr559als->pdata->scale_factor = val;
		else
			ltr559als->pdata->scale_factor = 1;
	}

	/* wake lock init */
	wake_lock_init(&ltr559als->prox_wake_lock, WAKE_LOCK_SUSPEND,
		       "prox_wake_lock");
	mutex_init(&ltr559als->power_lock);
	ret = ltr559als_chip_init(ltr559als);
	if (ret < 0) {
		pr_err("chip init failed\n");
		goto err_chip_init;
	}
#ifdef CONFIG_LTR559ALS_MD_TEST
	INIT_DELAYED_WORK(&ltr559als->md_test_work, ltr559als_mdtest_work_func);
#else
	ret = ltr559als_setup_irq(ltr559als);
	if (ret) {
		pr_err("%s: could not setup irq\n", __func__);
		goto err_setup_irq;
	}
#endif
	/* hrtimer settings.  we poll for light values using a timer. */
	hrtimer_init(&ltr559als->timer_light, CLOCK_MONOTONIC,
				HRTIMER_MODE_REL);
	ltr559als->als_delay = ns_to_ktime(500 * NSEC_PER_MSEC);
	ltr559als->timer_light.function = ltr559als_light_timer_func;

	/* allocate proximity input_device */
	input_dev = input_allocate_device();
	if (!input_dev) {
		pr_err("%s: could not allocate input device\n", __func__);
		goto err_input_allocate_device_proximity;
	}
	ltr559als->proximity_input_dev = input_dev;
	input_set_drvdata(input_dev, ltr559als);
	input_dev->name = "proximity";
	input_set_capability(input_dev, EV_ABS, ABS_DISTANCE);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	ret = input_register_device(input_dev);
	if (ret < 0) {
		pr_err("%s: could not register input device\n", __func__);
		input_free_device(input_dev);
		goto err_input_register_device_proximity;
	}

	/* the timer just fires off a work queue request.  we need a thread
	   to read the i2c (can be slow and blocking). */
	ltr559als->light_wq =
			create_singlethread_workqueue("ltr559als_light_wq");
	if (!ltr559als->light_wq) {
		ret = -ENOMEM;
		pr_err("%s: could not create light workqueue\n", __func__);
		goto err_create_light_workqueue;
	}
	ltr559als->prox_wq = create_singlethread_workqueue("ltr559als_prox_wq");
	if (!ltr559als->prox_wq) {
		ret = -ENOMEM;
		pr_err("%s: could not create prox workqueue\n", __func__);
		goto err_create_prox_workqueue;
	}

	/* this is the thread function we run on the work queue */
	INIT_WORK(&ltr559als->work_light, ltr559als_work_func_light);
#ifndef CONFIG_LTR559ALS_MD_TEST
	INIT_WORK(&ltr559als->work_prox, ltr559als_work_func_prox);
#endif

	/* allocate lightsensor-level input_device */
	input_dev = input_allocate_device();
	if (!input_dev) {
		pr_err("%s: could not allocate input device\n", __func__);
		ret = -ENOMEM;
		goto err_input_allocate_device_light;
	}
	input_set_drvdata(input_dev, ltr559als);
	input_dev->name = "lightsensor";
	input_set_capability(input_dev, EV_ABS, ABS_MISC);
	input_set_abs_params(input_dev, ABS_MISC, 0, 25000, 0, 0);

	ret = input_register_device(input_dev);
	if (ret < 0) {
		pr_err("%s: could not register input device\n", __func__);
		input_free_device(input_dev);
		goto err_input_register_device_light;
	}
	ltr559als->light_input_dev = input_dev;
	ret = sysfs_create_group(&ltr559als->i2c_client->dev.kobj,
				 &ltr559als_attr_group);
#ifdef CONFIG_HAS_EARLYSUSPEND
	ltr559als->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	ltr559als->early_suspend.suspend = ltr559als_early_suspend;
	ltr559als->early_suspend.resume = ltr559als_late_resume;
	register_early_suspend(&ltr559als->early_suspend);
#endif /* CONFIG_HAS_EARLYSUSPEND */
#ifdef CONFIG_LTR559ALS_MD_TEST
	schedule_delayed_work(&ltr559als->md_test_work, msecs_to_jiffies(100));
#endif
	goto done;

/* error, unwind it all */
err_input_register_device_light:
err_input_allocate_device_light:
	unregister_early_suspend(&ltr559als->early_suspend);
	input_unregister_device(ltr559als->light_input_dev);
	destroy_workqueue(ltr559als->prox_wq);
err_create_prox_workqueue:
	hrtimer_cancel(&ltr559als->timer_light);
	destroy_workqueue(ltr559als->light_wq);
err_create_light_workqueue:
	input_unregister_device(ltr559als->proximity_input_dev);
err_input_register_device_proximity:
err_input_allocate_device_proximity:
	free_irq(ltr559als->irq, 0);
#ifndef CONFIG_LTR559ALS_MD_TEST
err_setup_irq:
#endif
err_chip_init:
	mutex_destroy(&ltr559als->power_lock);
	wake_lock_destroy(&ltr559als->prox_wake_lock);
	kfree(ltr559als->pdata);
err_alloc_mem:
	kfree(ltr559als);
#ifdef LTR559ALS_RUNTIME_PM
	if (ltr559als_regltr_1v8) {
		ltr559als_cs_power_on_off(0);
		regulator_put(ltr559als_regltr_1v8);
	}
	if (ltr559als_regltr_3v) {
		ltr559als_power_on_off(0);
		regulator_put(ltr559als_regltr_3v);
	}
#endif
done:
	pr_info("ltr559als probe success\n");
	return ret;
}

static int _ltr559als_suspend(struct i2c_client *client, pm_message_t mesg)
{
	/* We disable power only if proximity is disabled.  If proximity
	   is enabled, we leave power on because proximity is allowed
	   to wake up device.  We remove power without changing
	   ltr559als->power_state because we use that state in resume.
	 */
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	pr_info("_ltr559als_suspend: als/ps status = %d/%d\n",
		ltr559als->power_state & LIGHT_ENABLED,
		ltr559als->power_state & PROXIMITY_ENABLED);
	if ((ltr559als->power_state & LIGHT_ENABLED)
	    && (ltr559als->power_state & PROXIMITY_ENABLED)) {
		hrtimer_cancel(&ltr559als->timer_light);
		cancel_work_sync(&ltr559als->work_light);
		ltr559als_alp_enable(ltr559als, false, true);
	} else if ((ltr559als->power_state & LIGHT_ENABLED)
		   && !(ltr559als->power_state & PROXIMITY_ENABLED)) {
		hrtimer_cancel(&ltr559als->timer_light);
		cancel_work_sync(&ltr559als->work_light);
		cancel_work_sync(&ltr559als->work_prox);
		ltr559als_alp_enable(ltr559als, false, false);
	}
#ifdef LTR559ALS_SUSPEND_PM
	if (!(ltr559als->power_state & PROXIMITY_ENABLED)) {
		ltr559als_power_on_off(0);
		ltr559als_cs_power_on_off(0);
	}
#endif
	return 0;
}

static int _ltr559als_resume(struct i2c_client *client)
{
	/* Turn power back on if we were before suspend. */
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	pr_info("_ltr559als_resume: als/ps status = %d/%d\n",
		ltr559als->power_state & LIGHT_ENABLED,
		ltr559als->power_state & PROXIMITY_ENABLED);
#ifdef LTR559ALS_SUSPEND_PM
	if (!(ltr559als->power_state & PROXIMITY_ENABLED)) {
		ltr559als_power_on_off(1);
		ltr559als_cs_power_on_off(1);
		ltr559als_chip_init(ltr559als);
	}
#endif
	if ((ltr559als->power_state & LIGHT_ENABLED)
	    && (ltr559als->power_state & PROXIMITY_ENABLED)) {
		ltr559als_alp_enable(ltr559als, true, true);
		hrtimer_start(&ltr559als->timer_light,
			      ktime_set(0, LIGHT_SENSOR_START_TIME_DELAY),
			      HRTIMER_MODE_REL);
	} else if ((ltr559als->power_state & LIGHT_ENABLED)
		   && !(ltr559als->power_state & PROXIMITY_ENABLED)) {
		ltr559als_alp_enable(ltr559als, true, false);
		hrtimer_start(&ltr559als->timer_light,
			      ktime_set(0, LIGHT_SENSOR_START_TIME_DELAY),
			      HRTIMER_MODE_REL);
	}

	return 0;
}

static int ltr559als_remove(struct i2c_client *client)
{
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	unregister_early_suspend(&ltr559als->early_suspend);
	input_unregister_device(ltr559als->light_input_dev);
	input_unregister_device(ltr559als->proximity_input_dev);
	free_irq(ltr559als->irq, NULL);
	destroy_workqueue(ltr559als->light_wq);
	destroy_workqueue(ltr559als->prox_wq);
	mutex_destroy(&ltr559als->power_lock);
	wake_lock_destroy(&ltr559als->prox_wake_lock);
	kfree(ltr559als->pdata);
	kfree(ltr559als);
#ifdef LTR559ALS_RUNTIME_PM
	if (ltr559als_regltr_1v8) {
		ltr559als_cs_power_on_off(0);
		regulator_put(ltr559als_regltr_1v8);
	}
	if (ltr559als_regltr_3v) {
		ltr559als_power_on_off(0);
		regulator_put(ltr559als_regltr_3v);
	}
#endif
	return 0;
}

static void ltr559als_shutdown(struct i2c_client *client)
{
	int ret;
	struct ltr559als_data *ltr559als = i2c_get_clientdata(client);
	ret = ltr559als_i2c_write(ltr559als, LTR559ALS_ALS_CONTR, 0x0);
	if (ret < 0)
		pr_err("ltr559als: shutdown fail with %d\n", ret);
}

static const struct i2c_device_id ltr559als_device_id[] = {
	{"ltr55xals", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ltr559als_device_id);

static const struct of_device_id ltr559als_of_match[] = {
	{.compatible = "bcm,ltr55xals",},
	{},
};

MODULE_DEVICE_TABLE(of, ltr559als_of_match);

static struct i2c_driver ltr559als_i2c_driver = {
	.driver = {
		   .name = "ltr55xals",
		   .owner = THIS_MODULE,
		   .of_match_table = ltr559als_of_match,
		   },
	.probe = ltr559als_probe,
	.remove = ltr559als_remove,
	.id_table = ltr559als_device_id,
	.shutdown = ltr559als_shutdown,
};

static int __init ltr559als_init(void)
{
	return i2c_add_driver(&ltr559als_i2c_driver);
}

static void __exit ltr559als_exit(void)
{
	i2c_del_driver(&ltr559als_i2c_driver);
}

module_init(ltr559als_init);
module_exit(ltr559als_exit);

MODULE_AUTHOR("Yongqiang Wang");
MODULE_DESCRIPTION("ltr559als Ambient Light and Proximity Sensor Driver");
MODULE_LICENSE("GPL v2");
