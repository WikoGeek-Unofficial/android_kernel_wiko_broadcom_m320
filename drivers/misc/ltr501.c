/*******************************************************************************
* Copyright 2013 Broadcom Corporation.  All rights reserved.
*
*       @file   drivers/misc/ltr501.c
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
#include <linux/ltr501.h>
#define LTR501_RUNTIME_PM
/* driver data */
struct ltr501_data {
	struct ltr501_pdata *pdata;
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
#ifdef CONFIG_LTR501_MD_TEST
	struct delayed_work	md_test_work;
#endif
	int irq;
};

static int raw_data_debug;
static u16 prox_threshold_hi_def;
static u16 prox_threshold_lo_def;
#ifdef LTR501_RUNTIME_PM
static struct regulator *ltr501_regltr_3v;
static struct regulator *ltr501_regltr_1v8;
static int ltr501_power_on_off(bool flag);
static int ltr501_cs_power_on_off(bool flag);
#endif
static int _ltr501_suspend(struct i2c_client *client, pm_message_t mesg);
static int _ltr501_resume(struct i2c_client *client);
//BEGIN custom_t jaky DACA-921
static u16 g_als_hi = 0;
static bool g_bIS_hi_Disturb = false;   //false means has no disturb
static bool g_bALSState = false;   //false means als disable, true means enable
//static bool g_bEnableALS = false;
//END custom_t jaky DACA-921
static int als_times = 0;
static int als_value[3] = {0};
static int final_lux_val = 0;

//BEGIN custom_t jaky DACA-949
static bool bRunFirst = false;
//END custom_t jaky DACA-949


static int ltr501_i2c_read(struct ltr501_data *ltr501, u8 addr, u8 *val)
{
	int err;
	u8 buf[2] = {addr, 0};
	struct i2c_msg msgs[] = {
		{
			.addr = ltr501->i2c_client->addr,
			.flags = ltr501->i2c_client->flags & I2C_M_TEN,
			.len = 1,
			.buf = buf,
		},
		{
			.addr = ltr501->i2c_client->addr,
			.flags = (ltr501->i2c_client->flags & I2C_M_TEN)
				| I2C_M_RD,
			.len = 1,
			.buf = buf,
		},
	};

	err = i2c_transfer(ltr501->i2c_client->adapter, msgs, 2);
	if (err != 2) {
		dev_err(&ltr501->i2c_client->dev, "read transfer error\n");
		err = -EIO;
	} else {
		err = 0;
		*val = buf[0];
	}
	return err;
}

static int ltr501_i2c_write(struct ltr501_data *ltr501, u8 addr, u8 val)
{
	int err;
	u8 buf[2] = { addr, val };
	struct i2c_msg msgs[] = {
		{
			.addr = ltr501->i2c_client->addr,
			.flags = ltr501->i2c_client->flags & I2C_M_TEN,
			.len = 2,
			.buf = buf,
		},
	};
	err = i2c_transfer(ltr501->i2c_client->adapter, msgs, 1);
	if (err != 1) {
		dev_err(&ltr501->i2c_client->dev, "write transfer error\n");
		err = -EIO;
	} else
		err = 0;
	return err;
}

static int ltr501_drdy(struct ltr501_data *data, u8 drdy_mask)
{
	int tries = 100;
	int ret;

	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->i2c_client,
					       LTR501_ALS_PS_STATUS);
		if (ret < 0)
			return ret;
		if ((ret & drdy_mask) == drdy_mask)
			return 0;
	}
	return -EIO;
}

static int ltr501_read_als(struct ltr501_data *data, __le16 buf[2])
{
	int ret = ltr501_drdy(data, LTR501_STATUS_ALS_RDY);
	if (ret < 0){
                printk("[ltr501] ltr501_read_als, ret=%d \n", ret);
		return ret;
	}
	/* always read both ALS channels in given order */
	return i2c_smbus_read_i2c_block_data(data->i2c_client,
					     LTR501_ALS_DATA1,
					     2 * sizeof(__le16), (u8 *) buf);
}

static int ltr501_read_ps(struct ltr501_data *data)
{
	int ret = ltr501_drdy(data, LTR501_STATUS_PS_RDY);
	if (ret < 0)
		return ret;
	return i2c_smbus_read_word_data(data->i2c_client, LTR501_PS_DATA);
}

static int ltr501_i2c_burst_write(struct ltr501_data *ltr501, u8 addr, u16 val)
{
	int ret;
	ret = 0;
	ret = i2c_smbus_write_word_data(ltr501->i2c_client, addr, val);
	return ret;
}

#ifdef LTR501_RUNTIME_PM
static int ltr501_power_on_off(bool flag)
{
	int ret;
	if (!ltr501_regltr_3v) {
		pr_err("ltr501_regltr is unavailable\n");
		return -1;
	}

	if ((flag == 1)) {
		pr_info("\n LDO on %s ", __func__);
		ret = regulator_enable(ltr501_regltr_3v);
		if (ret)
			pr_err("ltr501 3v Regulator-enable failed\n");
	} else if ((flag == 0)) {
		pr_info("\n LDO off %s ", __func__);
		ret = regulator_disable(ltr501_regltr_3v);
		if (ret)
			pr_err("ltr501 3v Regulator disable failed\n");
	}
	return ret;
}

static int ltr501_cs_power_on_off(bool flag)
{
	int ret;
	if (!ltr501_regltr_1v8) {
		pr_err("ltr501_regltr is unavailable\n");
		return -1;
	}
	if ((flag == 1)) {
		pr_info("\n LDO on %s ", __func__);
		ret = regulator_enable(ltr501_regltr_1v8);
		if (ret)
			pr_err("ltr501 1.8v Regulator-enable failed\n");
	} else if ((flag == 0)) {
		pr_info("\n LDO off %s ", __func__);
		ret = regulator_disable(ltr501_regltr_1v8);
		if (ret)
			pr_err("ltr501 1.8v Regulator disable failed\n");
	}
	return ret;
}
#endif
static void ltr501_alp_enable(struct ltr501_data *ltr501, bool enable_als,
			      bool enable_ps)
{
	int ret;
	u8 reg;

	ret = 0;
	pr_info("ltr501_alp_enable: enable_als=%d, enable_ps=%d\n", enable_als, enable_ps);  
	if (enable_als == 1 && enable_ps == 1) {
		ret = ltr501_i2c_read(ltr501, LTR501_INTERRUPT, &reg);
		if (ret < 0)
			pr_err("[ltr501] read LTR501_INTERRUPT err\n");
		reg |= LTR501_INTR_MODE;
		ret = ltr501_i2c_write(ltr501, LTR501_INTERRUPT, reg);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_INTERRUPT err\n");
		ret = ltr501_i2c_read(ltr501, LTR501_ALS_CONTR, &reg);
		if (ret < 0)
			pr_err("[ltr501] read LTR501_ALS_CONTR err\n");
		reg |= LTR501_CONTR_ACTIVE;
		ret = ltr501_i2c_write(ltr501, LTR501_ALS_CONTR, reg);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_ALS_CONTR err\n");
		ret = ltr501_i2c_read(ltr501, LTR501_PS_CONTR, &reg);
		if (ret < 0)
			pr_err("[ltr501] read LTR501_PS_CONTR err\n");
		reg |= LTR501_CONTR_ACTIVE;
		ret = ltr501_i2c_write(ltr501, LTR501_PS_CONTR, reg);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_PS_CONTR err\n");
	} else if (enable_als == 1 && enable_ps == 0) {
		ret = ltr501_i2c_write(ltr501, LTR501_INTERRUPT,
				ltr501->pdata->interrupt);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_INTERRUPT err\n");
		ret = ltr501_i2c_read(ltr501, LTR501_ALS_CONTR, &reg);
		if (ret < 0)
			pr_err("[ltr501] read LTR501_ALS_CONTR err\n");
		reg |= LTR501_CONTR_ACTIVE;
		ret = ltr501_i2c_write(ltr501, LTR501_ALS_CONTR, reg);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_ALS_CONTR err\n");
		ret = ltr501_i2c_write(ltr501, LTR501_PS_CONTR,
				ltr501->pdata->ps_contr);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_PS_CONTR err\n");

	} else if (enable_als == 0 && enable_ps == 1) {
		ret = ltr501_i2c_read(ltr501, LTR501_INTERRUPT, &reg);
		if (ret < 0)
			pr_err("[ltr501] read LTR501_INTERRUPT err\n");
		reg |= LTR501_INTR_MODE;
		ret = ltr501_i2c_write(ltr501, LTR501_INTERRUPT, reg);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_INTERRUPT err\n");
		ret = ltr501_i2c_write(ltr501, LTR501_ALS_CONTR,
					ltr501->pdata->als_contr);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_ALS_CONTR err\n");
		ret = ltr501_i2c_read(ltr501, LTR501_PS_CONTR, &reg);
		if (ret < 0)
			pr_err("[ltr501] read LTR501_PS_CONTR err\n");
		reg |= LTR501_CONTR_ACTIVE;
		ret = ltr501_i2c_write(ltr501, LTR501_PS_CONTR, reg);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_PS_CONTR err\n");

	} else if (enable_als == 0 && enable_ps == 0) {
		ret = ltr501_i2c_write(ltr501, LTR501_INTERRUPT,
				ltr501->pdata->interrupt);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_INTERRUPT err\n");
		ret = ltr501_i2c_write(ltr501, LTR501_ALS_CONTR,
				ltr501->pdata->als_contr);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_ALS_CONTR err\n");
		ret = ltr501_i2c_write(ltr501, LTR501_PS_CONTR,
			ltr501->pdata->ps_contr);
		if (ret < 0)
			pr_err("[ltr501] write LTR501_PS_CONTR err\n");
	}
	/*chip bug, need reading to clear interrupt first*/
	ret = ltr501_i2c_read(ltr501, LTR501_ALS_PS_STATUS, &reg);
	if (ret < 0)
		pr_err("[ltr501] read LTR501_ALS_PS_STATUS err\n");
}

static int ltr501_als_threshold_set(struct ltr501_data *ltr501, u16 tl, u16 th)
{
	int ret;
	ret = 0;
	ret = ltr501_i2c_burst_write(ltr501, LTR501_ALS_THRES_LOW, tl);
	if (ret < 0)
		pr_err("[ltr501] write LTR501_ALS_THRES_LOW err=%d\n", ret);
	ret = ltr501_i2c_burst_write(ltr501, LTR501_ALS_THRES_UP, th);
	if (ret < 0)
		pr_err("[ltr501] write LTR501_ALS_THRES_UP err=%d\n", ret);
	return ret;

}

static int ltr501_ps_threshold_set(struct ltr501_data *ltr501, u16 tl, u16 th)
{
	int ret;
	ret = 0;
	ret = ltr501_i2c_burst_write(ltr501, LTR501_PS_THRES_UP, th);
	if (ret < 0)
		pr_err("[ltr501] write LTR501_PS_THRES_UP err=%d\n", ret);
	ret = ltr501_i2c_burst_write(ltr501, LTR501_PS_THRES_LOW, tl);
	if (ret < 0)
		pr_err("[ltr501] write LTR501_PS_THRES_LOW err=%d\n", ret);
	return ret;
}

int doing_proximity_calibration = 0;
static int ltr501_proximity_calibration(struct ltr501_data *ltr501,
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
	int real_read_time = 0;

	pr_info("[ltr501] ltr501_proximity_calibration E\n"); 	

	if (!ltr501)
		return -EINVAL;

	if (!(ltr501->power_state & PROXIMITY_ENABLED)) {
		ltr501->power_state |= PROXIMITY_ENABLED;
		prox_state = 1;
		if (ltr501->power_state & LIGHT_ENABLED)
			ltr501_alp_enable(ltr501, true, true);
		else
			ltr501_alp_enable(ltr501, false, true);
	}

	ret = ltr501_i2c_read(ltr501, LTR501_INTERRUPT, &int_reg);
	if (ret < 0) {
		pr_err("[ltr501] read LTR501_INTERRUPT err\n");
		return ret;
	}
	ret = ltr501_i2c_write(ltr501, LTR501_INTERRUPT, 0x0);
	if (ret < 0) {
		pr_err("[ltr501], set LTR501_INTERRUPT fail: %d\n", ret);
		return ret;
	}

	ret = ltr501_i2c_read(ltr501, LTR501_PS_MEAS_RATE, &ps_rate);
	if (ret < 0) {
		pr_err("[ltr501], read LTR501_PS_MEAS_RATE fail: %d\n", ret);
		return ret;
	}
	ret = ltr501_i2c_write(ltr501, LTR501_PS_MEAS_RATE, 0x0);
	if (ret < 0) {
		pr_err("[ltr501], set LTR501_PS_MEAS_RATE fail: %d\n", ret);
		return ret;
	}
	//msleep(50);
	doing_proximity_calibration = 1;

	
	msleep(100);

	sum = 0;
	min = 2047;
	max = 0;
	for (i = 0; i < 32; i++) {
		ps_data = ltr501_read_ps(ltr501);

		pr_info("[ltr501] ltr501_proximity_calibration read ps %d\n",(short)ps_data);		

		if ((short)ps_data < 0) 
		{
			pr_err("[ltr501] ltr501_proximity_calibration read ps err\n");
			continue;
		}
		else
		{
			real_read_time ++;
		}
		ps_data &= LTR501_PS_DATA_MASK;
		sum += ps_data;
		if (ps_data < min)
			min = ps_data;
		if (ps_data > max)
			max = ps_data;
		msleep(100);
	}
//	avg = sum >> 5;

	doing_proximity_calibration = 0;

	
//	avg = sum >> 5;
	avg = sum / real_read_time;


	pr_info("(min avg max): %d, %d, %d\n", min, avg, max);
	ltr501->crosstalk = avg;

	ret = ltr501_i2c_write(ltr501, LTR501_INTERRUPT, int_reg);
	if (ret < 0)
		pr_err("[ltr501], set LTR501_INTERRUPT fail: %d\n", ret);

	ret = ltr501_i2c_write(ltr501, LTR501_PS_MEAS_RATE, ps_rate);
	if (ret < 0)
		pr_err("[ltr501], set LTR501_INTERRUPT fail: %d\n", ret);

	if (prox_state) {
		if (ltr501->power_state & LIGHT_ENABLED)
			ltr501_alp_enable(ltr501, true, false);
		else
			ltr501_alp_enable(ltr501, false, false);
		ltr501->power_state &= ~PROXIMITY_ENABLED;
		prox_state = 0;
	}

	pr_info("[ltr501] ltr501_proximity_calibration X\n"); 	
	
	return ret;
}

/*sysfs interface support*/
static ssize_t ltr501_proximity_enable_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	return sprintf(buf, "%d\n", ltr501->power_state & PROXIMITY_ENABLED);
}
//BEGIN custom_t jaky DACA-921
static ssize_t ltr501_enable_als(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct ltr501_data *ltr501 = dev_get_drvdata(dev);
	bool new_value;
	int i;

	if (sysfs_streq(buf, "1")){
		new_value = true;
	}
	else if (sysfs_streq(buf, "0")){
		new_value = false;     
	}
	else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}
        printk("[ltr501] ltr501_enable_als_store.size=%d \n", size);
	mutex_lock(&ltr501->power_lock);
	if (new_value && !(ltr501->power_state & LIGHT_ENABLED)) {
		ltr501->power_state |= LIGHT_ENABLED;
		if (ltr501->power_state & PROXIMITY_ENABLED)
			ltr501_alp_enable(ltr501, true, true);
		else
			ltr501_alp_enable(ltr501, true, false);
		mutex_unlock(&ltr501->power_lock);
		hrtimer_start(&ltr501->timer_light,
			      ktime_set(0, LIGHT_SENSOR_START_TIME_DELAY),
			      HRTIMER_MODE_REL);

		//added by fully for als.
		als_times = 0;
		for(i=0;i<3;i++){
			als_value[i] = 0;
		}
		//the end added by fully.
		
	} else if (!new_value && (ltr501->power_state & LIGHT_ENABLED)) {
		if (ltr501->power_state & PROXIMITY_ENABLED)
			ltr501_alp_enable(ltr501, false, true);
		else
			ltr501_alp_enable(ltr501, false, false);
		ltr501->power_state &= ~LIGHT_ENABLED;
		mutex_unlock(&ltr501->power_lock);
		hrtimer_cancel(&ltr501->timer_light);
		cancel_work_sync(&ltr501->work_light);
	} else
		mutex_unlock(&ltr501->power_lock);
	return size;
}
//END custom_t jaky DACA-921
static ssize_t ltr501_proximity_enable_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t size)
{
	struct ltr501_data *ltr501 = dev_get_drvdata(dev);
	bool new_value;
	if (sysfs_streq(buf, "1"))
		new_value = true;
	else if (sysfs_streq(buf, "0"))
		new_value = false;
	else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}
        printk("[ltr501] ltr501_proximity_enable_store.size=%d \n", size);
	mutex_lock(&ltr501->power_lock);
	if (new_value && !(ltr501->power_state & PROXIMITY_ENABLED)) {
		ltr501->power_state |= PROXIMITY_ENABLED;
		wake_lock(&ltr501->prox_wake_lock);
		if (ltr501->power_state & LIGHT_ENABLED){
                        ltr501_alp_enable(ltr501, false, false);
                        ltr501_alp_enable(ltr501, true, true);
		    }
		else
			ltr501_alp_enable(ltr501, false, true);

		input_report_abs(ltr501->proximity_input_dev, ABS_DISTANCE, -1);
		input_sync(ltr501->proximity_input_dev);

		enable_irq(ltr501->irq);
		mutex_unlock(&ltr501->power_lock);
	} else if (!new_value && (ltr501->power_state & PROXIMITY_ENABLED)) {
		if (ltr501->power_state & LIGHT_ENABLED)
			ltr501_alp_enable(ltr501, true, false);
		else
			ltr501_alp_enable(ltr501, false, false);
		ltr501->power_state &= ~PROXIMITY_ENABLED;
		wake_unlock(&ltr501->prox_wake_lock);
		disable_irq_nosync(ltr501->irq);
		mutex_unlock(&ltr501->power_lock);
		cancel_work_sync(&ltr501->work_prox);
	} else
		mutex_unlock(&ltr501->power_lock);
#ifdef CONFIG_LTR501_MD_TEST
	schedule_delayed_work(&ltr501->md_test_work, msecs_to_jiffies(100));
#endif
        //BEGIN custom_t jaky DACA-921
        //user open ps and light sensor disable
        if (new_value && !(ltr501->power_state & LIGHT_ENABLED)){
            msleep(200);
            ltr501_enable_als(dev, 0, "1", 2);
        }
        //user close ps and light always enable
        if (!new_value){
            if (!g_bALSState){
                msleep(200);
                ltr501_enable_als(dev, 0, "0", 2);
            }
        }
        //END custom_t jaky DACA-921
	return size;
}

static ssize_t ltr501_enable_als_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	return sprintf(buf, "%d\n", ltr501->power_state & LIGHT_ENABLED);
}

static ssize_t ltr501_enable_als_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct ltr501_data *ltr501 = dev_get_drvdata(dev);
	bool new_value;

	if (sysfs_streq(buf, "1")){
		new_value = true;
                //BEGIN custom_t jaky DACA-921
                g_bALSState = true;
                //END custom_t jaky DACA-921
	}
	else if (sysfs_streq(buf, "0")){
		new_value = false;
                //BEGIN custom_t jaky DACA-921        
                g_bALSState = false;
                //END custom_t jaky DACA-921                
	}
	else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}
        printk("[ltr501] ltr501_enable_als_store.size=%d \n", size);
	mutex_lock(&ltr501->power_lock);
	if (new_value && !(ltr501->power_state & LIGHT_ENABLED)) {
		ltr501->power_state |= LIGHT_ENABLED;
		if (ltr501->power_state & PROXIMITY_ENABLED)
			ltr501_alp_enable(ltr501, true, true);
		else
			ltr501_alp_enable(ltr501, true, false);
		mutex_unlock(&ltr501->power_lock);
		hrtimer_start(&ltr501->timer_light,
			      ktime_set(0, LIGHT_SENSOR_START_TIME_DELAY),
			      HRTIMER_MODE_REL);

	} else if (!new_value && (ltr501->power_state & LIGHT_ENABLED)) {
		if (ltr501->power_state & PROXIMITY_ENABLED)
			ltr501_alp_enable(ltr501, false, true);
		else
			ltr501_alp_enable(ltr501, false, false);
		ltr501->power_state &= ~LIGHT_ENABLED;
		mutex_unlock(&ltr501->power_lock);
		hrtimer_cancel(&ltr501->timer_light);
		cancel_work_sync(&ltr501->work_light);
	} else
		mutex_unlock(&ltr501->power_lock);
	return size;
}

static ssize_t ltr501_proximity_threshold_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	return sprintf(buf, "%d,%d\n", ltr501->pdata->prox_threshold_high,
		       ltr501->pdata->prox_threshold_low);
}

static ssize_t ltr501_proximity_offset_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	return sprintf(buf, "%d", ltr501->crosstalk);
}

static ssize_t ltr501_proximity_offset_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int x;
	int hi;
	int lo;
	int i;
	int err = -EINVAL;
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	u16 prox_offset = ltr501->pdata->prox_offset;

	err = sscanf(buf, "%d", &x);
	if (err != 1) {
		pr_err("invalid parameter number: %d\n", err);
		return err;
	}

	ltr501->crosstalk = x;
	hi = prox_threshold_hi_def + ltr501->crosstalk - prox_offset;
	lo = prox_threshold_lo_def + ltr501->crosstalk - prox_offset;

	pr_info("crosstalk=%d,prox_offset=%d, hi =%d, lo=%d\n",
		ltr501->crosstalk, prox_offset, hi, lo);

	if (hi > 0x7ff || lo > 0x7ff) {
		ltr501->crosstalk = 0;
		pr_err("isl290xx sw cali failed\n");
	} else {
		if (lo > ltr501->crosstalk) {
			/*ltr501->pdata->prox_threshold_high  = hi;
			ltr501->pdata->prox_threshold_low = lo;*/
			printk("[ltr501] ltr501_proximity_offset_store, lo > ltr501->crosstalk");
		} else {
			for (i = 0; i < 100; i++) {
				hi++;
				lo++;

				if (lo > ltr501->crosstalk) {
					/*ltr501->pdata->prox_threshold_high  =
						hi + 1;
					ltr501->pdata->prox_threshold_low = lo;*/
					break;
				}
			}
		}
	}

	return count;
}

static ssize_t ltr501_prox_cali_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	ret = ltr501_proximity_calibration(ltr501, 1, 100);
	if (ret < 0)
		return ret;
	return count;

}

static ssize_t ltr501_registers_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	unsigned i, n, reg_count;
	u8 value;
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	ret = 0;
	reg_count = sizeof(ltr501_regs) / sizeof(ltr501_regs[0]);
	for (i = 0, n = 0; i < reg_count; i++) {
		ret = ltr501_i2c_read(ltr501, ltr501_regs[i].reg, &value);
		if (ret < 0)
			return ret;
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "%-20s = 0x%02X\n", ltr501_regs[i].name, value);
	}
	return n;
}

static ssize_t ltr501_registers_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	unsigned i, reg_count, value;
	int ret;
	char name[30];
	struct i2c_client *client = to_i2c_client(dev);
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	if (count >= 30)
		return -EFAULT;
	if (sscanf(buf, "%30s %x", name, &value) != 2) {
		pr_err("input invalid\n");
		return -EFAULT;
	}
	reg_count = sizeof(ltr501_regs) / sizeof(ltr501_regs[0]);
	for (i = 0; i < reg_count; i++) {
		if (!strcmp(name, ltr501_regs[i].name)) {
			ret = ltr501_i2c_write(ltr501, ltr501_regs[i].reg,
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

static ssize_t ltr501_raw_data_debug_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", raw_data_debug);
}

static ssize_t ltr501_raw_data_debug_store(struct device *dev,
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
		   ltr501_enable_als_show, ltr501_enable_als_store);
static DEVICE_ATTR(enable_ps, 0644,
		   ltr501_proximity_enable_show, ltr501_proximity_enable_store);
static DEVICE_ATTR(offset, 0644, ltr501_proximity_offset_show,
		ltr501_proximity_offset_store);
static DEVICE_ATTR(prox_cali, 0644,
		   ltr501_proximity_threshold_show, ltr501_prox_cali_store);
static DEVICE_ATTR(registers, 0644, ltr501_registers_show,
		   ltr501_registers_store);
static DEVICE_ATTR(raw_data_debug, 0644, ltr501_raw_data_debug_show,
		   ltr501_raw_data_debug_store);


static struct attribute *ltr501_attributes[] = {
	&dev_attr_enable_als.attr,
	&dev_attr_enable_ps.attr,
	&dev_attr_offset.attr,
	&dev_attr_prox_cali.attr,
	&dev_attr_registers.attr,
	&dev_attr_raw_data_debug.attr,
	NULL
};

static const struct attribute_group ltr501_attr_group = {
	.attrs = ltr501_attributes,
};

/* we don't do any filter about lux, it can be done by framwork*/
#if 0
static unsigned int ltr501_als_data_filter(unsigned int als_data)
{
	u16 als_level[LTR501_ALS_LEVEL_NUM] = {
		0, 50, 100, 150, 200,
		250, 300, 350, 400, 450,
		550, 650, 750, 900, 1100
	};
	u16 als_value[LTR501_ALS_LEVEL_NUM] = {
		0, 50, 100, 150, 200,
		250, 300, 350, 1000, 1200,
		1900, 2500, 7500, 15000, 25000
	};
	u8 idx;
	for (idx = 0; idx < LTR501_ALS_LEVEL_NUM; idx++) {
		if (als_data < als_value[idx])
			break;
	}
	if (idx >= LTR501_ALS_LEVEL_NUM) {
		pr_err("ltr501 als data to level: exceed range.\n");
		idx = LTR501_ALS_LEVEL_NUM - 1;
	}

	return als_level[idx];
}
#endif

static int ltr501_get_als(struct ltr501_data *ltr501, u16 *als_data)
{
	u16 raw_als_lo;
	u16 raw_als_hi;
	__le16 buf[2];
	int ratio;
	int ret = 0;
	int als_flt = 0;
	int als_int;
	u8 reg = 0;
	u8 scale_factor;

	ret = ltr501_read_als(ltr501, buf);
	if (ret < 0) {
		pr_err("[ltr501] read als err\n");
		return ret;
	}
	raw_als_lo = le16_to_cpu(buf[1]);
	raw_als_hi = le16_to_cpu(buf[0]);
        //BEGIN custom_t jaky DACA-921
        g_als_hi = raw_als_hi;
        g_bIS_hi_Disturb = false;
        //END custom_t jaky DACA-921
        printk("[ltr501] raw_als_lo=%d, raw_als_hi=%d \n", raw_als_lo, raw_als_hi);
	if ((raw_als_lo + raw_als_hi) == 0) {
		pr_err("als dataare zero\n");
		ratio = 1000;
	} else {
		ratio = (raw_als_hi * 1000) / (raw_als_lo + raw_als_hi);
	}

        if(raw_als_lo < 60000) 
	{
            if (ratio < 450) {
            	als_flt = (17743 * raw_als_lo) - (11059 * raw_als_hi);
            	als_flt = als_flt / 10000;
            } else if ((ratio >= 450) && (ratio < 640)) {
            	als_flt = (37725 * raw_als_lo) - (13363 * raw_als_hi);
            	als_flt = als_flt / 10000;
            } else if ((ratio >= 640) && (ratio < 850)) {
            	als_flt = (16900 * raw_als_lo) - (1690 * raw_als_hi);
            	als_flt = als_flt / 10000;
            } else {
            		if(raw_als_hi > 500){
				als_flt = final_lux_val;
                                //BEGIN custom_t jaky DACA-921
                                g_bIS_hi_Disturb = true;
                                //END custom_t jaky DACA-921
            		}else{			
            			als_flt = 0;
            		}
            }
        }
        
        //if(((raw_als_lo <5 )&&(raw_als_hi >= 2000))||(raw_als_lo >= 60000) ||(raw_als_hi >= 60000))
        if(((raw_als_lo <5 )&&(raw_als_hi >= 2000))||((raw_als_lo > 500 )&&(raw_als_hi >= 5000) )||(raw_als_lo >= 60000)
        ||(raw_als_hi >= 60000))
        {
            als_flt = 65534;
        }

	//----------added by fully for als--------------------------
#if 1		
        if(als_times >= 2){
        	als_value[0] = als_value[1];
          	als_value[1] = als_value[2];
          	als_value[2] = als_flt;
        	
        	if(((als_value[2]-als_value[1])>500) || ((als_value[1]-als_value[2])>500) ||((als_value[2]-als_value[0])>500) || ((als_value[0]-als_value[2])>500) ||((als_value[1]-als_value[0])>500) ||((als_value[0]-als_value[1])>500) ){
        		*als_data = (u16)final_lux_val;
        		return 0;
        	}

            als_flt = ((als_value[0] + als_value[1] + als_value[2]) / 3);
            final_lux_val = als_flt;	
        }
        else if(als_times == 1){
          	als_value[1] = als_value[2];
          	als_value[2] = als_flt;
        	als_times++;
            als_flt = ((als_value[1] + als_value[2]) / 2);	
        }
        else if(als_times == 0){
          	als_value[2] = als_flt;
        	als_times++;
        }
#endif
	//------------the end added by fully.-------------------------
	
	als_int = als_flt;
	pr_info
	    ("[ltr501] raw_als_lo=%d, raw_als_hi=%d, als_flt=%d,als_int=%d\n",
	     raw_als_lo, raw_als_hi, als_flt, als_int);
	if ((als_flt - als_int) > 0.5)
		als_int = als_int + 1;
	else
		als_int = als_flt;

	scale_factor = ltr501->pdata->scale_factor;
	ret = ltr501_i2c_read(ltr501, LTR501_ALS_PS_STATUS, &reg);
	if (ret < 0) {
		pr_err("[ltr501] read LTR501_ALS_PS_STATUS err\n");
		return ret;
	}
        printk("[ltr501 als] ltr501_get_als.scale_factor=%d\n", scale_factor);
	if (!(reg & 0x10))
		*als_data = (u16)(als_int * scale_factor);
	else
		*als_data = (u16)als_int;
	return ret;
}

static void ltr501_work_func_light(struct work_struct *work)
{
	u16 als = 0;
	int ret;
	struct ltr501_data *ltr501 = container_of(work, struct ltr501_data,
						  work_light);
	ret = 0;
	ret = ltr501_get_als(ltr501, &als);
        printk("[ltr501] ltr501_work_func_light.als=%d\n", als);
	if (ret < 0)
		return;
        //BEGIN custom_t jaky DACA-949
        if (false == bRunFirst){
            bRunFirst = true;
            als = -1;
            printk("[ltr501] first run \n");
        }
        //BEGIN custom_t jaky DACA-949
	input_report_abs(ltr501->light_input_dev, ABS_MISC, als);
	input_sync(ltr501->light_input_dev);
}

/* light timer function */
static enum hrtimer_restart ltr501_light_timer_func(struct hrtimer *timer)
{
	struct ltr501_data *ltr501 =
	    container_of(timer, struct ltr501_data, timer_light);
	queue_work(ltr501->light_wq, &ltr501->work_light);
	hrtimer_forward_now(&ltr501->timer_light, ltr501->als_delay);
	return HRTIMER_RESTART;
}

#ifdef CONFIG_LTR501_MD_TEST
static void ltr501_mdtest_work_func(struct work_struct *work)
{
	u16 pdata;
	int ret;
	struct ltr501_data *ltr501 = container_of(work, struct ltr501_data,
			md_test_work.work);
	ret = 0;
	pdata = ltr501_read_ps(ltr501);
	pr_info("[md test] ltr501 prox data = %d\n", pdata);
	schedule_delayed_work(&ltr501->md_test_work, msecs_to_jiffies(100));
}
#else
static void ltr501_work_func_prox(struct work_struct *work)
{
	u16 pdata;
	u8 ps;
	int ret;
	struct ltr501_data *ltr501 = container_of(work, struct ltr501_data,
						  work_prox);
	ret = 0;
/*
        printk("doing_proximity_calibration =%d\n",doing_proximity_calibration);
	if(doing_proximity_calibration)
{
               enable_irq(ltr501->irq);
		return;
}*/
	pdata = ltr501_read_ps(ltr501);
	if (pdata < 0) {
		pr_err("[ltr501] ltr501_work_func_prox read ps err\n");
		return ;
	}
        
	ps = 0;
	if (pdata > ltr501->pdata->prox_threshold_high) {
		ps = 0;
		ltr501_ps_threshold_set(ltr501,
					ltr501->pdata->prox_threshold_low,
					ltr501->pdata->prox_threshold_high);
	} else if (pdata < ltr501->pdata->prox_threshold_low) {
		ps = 1;
		ltr501_ps_threshold_set(ltr501,
					ltr501->pdata->prox_threshold_low,
					ltr501->pdata->prox_threshold_high);
	}
        //BEGIN custom_t jaky
        if (g_als_hi > 1000){
        //if (g_bIS_hi_Disturb){
            ps = 1;
            printk("[ltr501] als > 1000, ps must be 1 \n");
        }
        //END custom_t jaky

	printk
	    ("[ltr501] raw_ps_lo=%d, raw_ps_hi=%d, raw_ps=%d,ps=%d\n",
	    ltr501->pdata->prox_threshold_low,ltr501->pdata->prox_threshold_high,
	    pdata,ps);
	
	if (raw_data_debug)
		input_report_abs(ltr501->proximity_input_dev,
						ABS_DISTANCE, pdata);
	else
		input_report_abs(ltr501->proximity_input_dev, ABS_DISTANCE, ps);
	input_sync(ltr501->proximity_input_dev);
	enable_irq(ltr501->irq);

}
static irqreturn_t ltr501_irq_thread_fn(int irq, void *data)
{
	struct ltr501_data *ltr501 = data;
	disable_irq_nosync(ltr501->irq);
	queue_work(ltr501->prox_wq, &ltr501->work_prox);
	return IRQ_HANDLED;
}
#endif
#ifndef CONFIG_LTR501_MD_TEST
static int ltr501_setup_irq(struct ltr501_data *ltr501)
{
	int ret = -EIO;
	pr_info("ltr501_setup_irq =%d\n", ltr501->i2c_client->irq);
	ltr501->irq = ltr501->i2c_client->irq;
	if (ltr501->irq)
		ret = request_threaded_irq(ltr501->irq, NULL,
					&ltr501_irq_thread_fn,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"ltr501", ltr501);
	else
		ret = -ENODEV;

	if (ret < 0) {
		pr_err("%s: request_irq(%d) failed for irq %d\n",
		       __func__, ltr501->irq, ret);
		return ret;
	}

	/* start with interrupts disabled */
	disable_irq(ltr501->irq);

	return ret;
}
#endif
static int ltr501_chip_init(struct ltr501_data *ltr501)
{
	int ret;

	ret = 0;

	ret = ltr501_als_threshold_set(ltr501, 0, 0xFFFF);
	if (ret < 0)
		pr_err("[ltr501]fail to configure als thresold\n");

	ret = ltr501_ps_threshold_set(ltr501,
				ltr501->pdata->prox_threshold_low,
				ltr501->pdata->prox_threshold_high);
	if (ret < 0)
		pr_err("[ltr501]fail to configure prox threshold\n");
	ret = ltr501_i2c_write(ltr501, LTR501_ALS_CONTR,
					ltr501->pdata->als_contr);
	if (ret < 0)
		pr_err("[ltr501] init LTR501_ALS_CONTR  err=%d\n", ret);
	ret = ltr501_i2c_write(ltr501, LTR501_PS_CONTR,
					ltr501->pdata->ps_contr);
	if (ret < 0)
		pr_err("[ltr501] init LTR501_PS_CONTR  err=%d\n", ret);
	ret = ltr501_i2c_write(ltr501, LTR501_PS_LED, ltr501->pdata->ps_led);
	if (ret < 0)
		pr_err("[ltr501] init LTR501_PS_LED  err=%d\n", ret);

	ret = ltr501_i2c_write(ltr501, LTR501_PS_N_PULSES,
					ltr501->pdata->ps_n_pulses);
	if (ret < 0)
		pr_err("[ltr501] init LTR501_PS_N_PULSES err=%d\n", ret);
	ret =
	    ltr501_i2c_write(ltr501, LTR501_PS_MEAS_RATE,
					ltr501->pdata->ps_meas_rate);
	if (ret < 0)
		pr_err("[ltr501] init LTR501_PS_MEAS_RATE err=%d\n", ret);

	ret = ltr501_i2c_write(ltr501, LTR501_ALS_MEAS_RATE,
					ltr501->pdata->als_meas_rate);
	if (ret < 0)
		pr_err("[ltr501] init LTR501_ALS_MEAS_RATE err=%d\n", ret);

	ret = ltr501_i2c_write(ltr501, LTR501_INTERRUPT,
					ltr501->pdata->interrupt);
	if (ret < 0)
		pr_err("[ltr501] init LTR501_INTERRUPT err=%d\n", ret);
	ret =
	    ltr501_i2c_write(ltr501, LTR501_INTERRUPT_PERSIST,
			     ltr501->pdata->interrupt_persist);
	if (ret < 0)
		pr_err("[ltr501] init LTR501_INTERRUPT_PERSIST err=%d\n", ret);

	return ret;

}

#ifdef CONFIG_HAS_EARLYSUSPEND
void ltr501_early_suspend(struct early_suspend *h)
{
	struct ltr501_data *ltr501 = container_of(h,
						  struct ltr501_data,
						  early_suspend);
	pm_message_t mesg = {
		.event = PM_EVENT_SUSPEND,
	};
	_ltr501_suspend(ltr501->i2c_client, mesg);
}

static void ltr501_late_resume(struct early_suspend *desc)
{
	struct ltr501_data *ltr501 = container_of(desc,
						  struct ltr501_data,
						  early_suspend);
	_ltr501_resume(ltr501->i2c_client);
}
#endif

static int ltr501_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret = -ENODEV;
	struct input_dev *input_dev;
	struct ltr501_data *ltr501;
	struct device_node *np;
	u32 val;
	np = NULL;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s: i2c functionality check failed!\n", __func__);
		return ret;
	}
#ifdef LTR501_RUNTIME_PM
	ltr501_regltr_3v = regulator_get(NULL, "sensor_3v");
	ltr501_regltr_1v8 = regulator_get(NULL, "sensor_1v8");
	if (IS_ERR_OR_NULL(ltr501_regltr_3v) ||
					IS_ERR_OR_NULL(ltr501_regltr_1v8)) {
		pr_err("%s: Get ltr501 regulator Failed\n", __func__);
		return -EBUSY;
	}
	regulator_set_voltage(ltr501_regltr_3v, 3000000, 3000000);
	ret = regulator_enable(ltr501_regltr_3v);
	if (ret)
		pr_err("ltr501 3v Regulator Enable Failed\n");

	regulator_set_voltage(ltr501_regltr_1v8, 1800000, 1800000);
	ret = regulator_enable(ltr501_regltr_1v8);
	if (ret)
		pr_err("ltr501 1v8 Regulator enable failed\n");

#endif
	ltr501 = kzalloc(sizeof(struct ltr501_data), GFP_KERNEL);
	if (!ltr501) {
		pr_err("%s: failed to alloc memory for module data\n",
		       __func__);
		return -ENOMEM;
	}
	ltr501->i2c_client = client;
	i2c_set_clientdata(client, ltr501);

	if (!client->dev.platform_data) {
		ltr501->pdata = kzalloc(sizeof(struct ltr501_pdata),
					GFP_KERNEL);
		if (!ltr501->pdata) {
			pr_err("%s: failed to alloc memory for module data\n",
					__func__);
			goto err_alloc_mem;
		}
		np = ltr501->i2c_client->dev.of_node;
		ret = of_property_read_u32(np, "gpio-irq-pin", &val);
		if (!ret) {
			gpio_request(val, "prox_int");
			gpio_direction_input(val);
			client->irq = gpio_to_irq(val);
		} else {
			pr_info("[ltr501] gpio-irq-pin is not found in dts\n");
			client->irq = 0;
		}
		/* chip configuration */
		ret = of_property_read_u32(np, "als_contr", &val);
		if (!ret)
			ltr501->pdata->als_contr = val;
		else {
			pr_info("[ltr501] als_contr is not found in dts\n");
			ltr501->pdata->als_contr = 0x0;
		}
		ret = of_property_read_u32(np, "ps_contr", &val);
		if (!ret)
			ltr501->pdata->ps_contr = val;
		else {
			pr_info("[ltr501] ps_contr is not found in dts\n");
			ltr501->pdata->ps_contr = 0x04;
		}
		ret = of_property_read_u32(np, "ps_led", &val);
		if (!ret)
			ltr501->pdata->ps_led = val;
		else {
			pr_info("[ltr501] ps_led is not found in dts\n");
			ltr501->pdata->ps_led = 0x6B;
		}

		ret = of_property_read_u32(np, "ps_n_pusles", &val);
		if (!ret)
			ltr501->pdata->ps_n_pulses = val;
		else {
			pr_info("[ltr501] ps_n_pusles is not found in dts\n");
			ltr501->pdata->ps_n_pulses = 0x08;
		}

		ret = of_property_read_u32(np, "ps_meas_rate", &val);
		if (!ret)
			ltr501->pdata->ps_meas_rate = val;
		else {
			pr_info("[ltr501] ps_meas_rate is not found in dts\n");
			ltr501->pdata->ps_meas_rate = 0x02;
		}

		ret = of_property_read_u32(np, "als_meas_rate", &val);
		if (!ret)
			ltr501->pdata->als_meas_rate = val;
		else {
			pr_info("[ltr501] als_meas_rate is not in dts\n");
			ltr501->pdata->als_meas_rate = 0x01;
		}
		ret = of_property_read_u32(np, "interrupt", &val);
		if (!ret)
			ltr501->pdata->interrupt = val;
		else {
			pr_info("[ltr501] interrupt is not in dts\n");
			ltr501->pdata->interrupt = 0x08;
		}
		ret = of_property_read_u32(np, "interrupt_persist", &val);
		if (!ret)
			ltr501->pdata->interrupt_persist = val;
		else {
			pr_info("[ltr501] interrupt_persist is not in dts\n");
			ltr501->pdata->interrupt_persist = 0x00;
		}
		ret = of_property_read_u32(np, "prox_threshold_low", &val);
		if (!ret)
			ltr501->pdata->prox_threshold_low = val;
		else {
			pr_info("[ltr501] low prox threshold is not in dts\n");
			ltr501->pdata->prox_threshold_low = 80;
		}
		prox_threshold_lo_def = ltr501->pdata->prox_threshold_low;

		ret = of_property_read_u32(np, "prox_threshold_high", &val);
		if (!ret)
			ltr501->pdata->prox_threshold_high = val;
		else {
			pr_info("[ltr501] high pthreshold is not in dts\n");
			ltr501->pdata->prox_threshold_high = 90;
		}
		prox_threshold_hi_def = ltr501->pdata->prox_threshold_high;

		ret = of_property_read_u32(np, "prox_offset_param", &val);
		if (!ret)
			ltr501->pdata->prox_offset = val;
		else
			ltr501->pdata->prox_offset = 0;

		ret = of_property_read_u32(np, "scale_factor", &val);
		if (!ret)
			ltr501->pdata->scale_factor = val;
		else
			ltr501->pdata->scale_factor = 1;
	}

	/* wake lock init */
	wake_lock_init(&ltr501->prox_wake_lock, WAKE_LOCK_SUSPEND,
		       "prox_wake_lock");
	mutex_init(&ltr501->power_lock);
	ret = ltr501_chip_init(ltr501);
	if (ret < 0) {
		pr_err("chip init failed\n");
		goto err_chip_init;
	}
#ifdef CONFIG_LTR501_MD_TEST
	INIT_DELAYED_WORK(&ltr501->md_test_work, ltr501_mdtest_work_func);
#else
	ret = ltr501_setup_irq(ltr501);
	if (ret) {
		pr_err("%s: could not setup irq\n", __func__);
		goto err_setup_irq;
	}
#endif
	/* hrtimer settings.  we poll for light values using a timer. */
	hrtimer_init(&ltr501->timer_light, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ltr501->als_delay = ns_to_ktime(500 * NSEC_PER_MSEC);
	ltr501->timer_light.function = ltr501_light_timer_func;

	/* allocate proximity input_device */
	input_dev = input_allocate_device();
	if (!input_dev) {
		pr_err("%s: could not allocate input device\n", __func__);
		goto err_input_allocate_device_proximity;
	}
	ltr501->proximity_input_dev = input_dev;
	input_set_drvdata(input_dev, ltr501);
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
	ltr501->light_wq = create_singlethread_workqueue("ltr501_light_wq");
	if (!ltr501->light_wq) {
		ret = -ENOMEM;
		pr_err("%s: could not create light workqueue\n", __func__);
		goto err_create_light_workqueue;
	}
	ltr501->prox_wq = create_singlethread_workqueue("ltr501_prox_wq");
	if (!ltr501->prox_wq) {
		ret = -ENOMEM;
		pr_err("%s: could not create prox workqueue\n", __func__);
		goto err_create_prox_workqueue;
	}

	/* this is the thread function we run on the work queue */
	INIT_WORK(&ltr501->work_light, ltr501_work_func_light);
#ifndef CONFIG_LTR501_MD_TEST
	INIT_WORK(&ltr501->work_prox, ltr501_work_func_prox);
#endif

	/* allocate lightsensor-level input_device */
	input_dev = input_allocate_device();
	if (!input_dev) {
		pr_err("%s: could not allocate input device\n", __func__);
		ret = -ENOMEM;
		goto err_input_allocate_device_light;
	}
	input_set_drvdata(input_dev, ltr501);
	input_dev->name = "lightsensor";
	input_set_capability(input_dev, EV_ABS, ABS_MISC);
	input_set_abs_params(input_dev, ABS_MISC, 0, 25000, 0, 0);

	ret = input_register_device(input_dev);
	if (ret < 0) {
		pr_err("%s: could not register input device\n", __func__);
		input_free_device(input_dev);
		goto err_input_register_device_light;
	}
	ltr501->light_input_dev = input_dev;
	ret = sysfs_create_group(&ltr501->i2c_client->dev.kobj,
				 &ltr501_attr_group);
#ifdef CONFIG_HAS_EARLYSUSPEND
	ltr501->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	ltr501->early_suspend.suspend = ltr501_early_suspend;
	ltr501->early_suspend.resume = ltr501_late_resume;
	register_early_suspend(&ltr501->early_suspend);
#endif /* CONFIG_HAS_EARLYSUSPEND */
#ifdef CONFIG_LTR501_MD_TEST
	schedule_delayed_work(&ltr501->md_test_work, msecs_to_jiffies(100));
#endif
	goto done;

/* error, unwind it all */
err_input_register_device_light:
err_input_allocate_device_light:
	unregister_early_suspend(&ltr501->early_suspend);
	input_unregister_device(ltr501->light_input_dev);
	destroy_workqueue(ltr501->prox_wq);
err_create_prox_workqueue:
	hrtimer_cancel(&ltr501->timer_light);
	destroy_workqueue(ltr501->light_wq);
err_create_light_workqueue:
	input_unregister_device(ltr501->proximity_input_dev);
err_input_register_device_proximity:
err_input_allocate_device_proximity:
	free_irq(ltr501->irq, 0);
#ifndef CONFIG_LTR501_MD_TEST
err_setup_irq:
#endif
err_chip_init:
	mutex_destroy(&ltr501->power_lock);
	wake_lock_destroy(&ltr501->prox_wake_lock);
	kfree(ltr501->pdata);
err_alloc_mem:
	kfree(ltr501);
#ifdef LTR501_RUNTIME_PM
	if (ltr501_regltr_1v8) {
		ltr501_cs_power_on_off(0);
		regulator_put(ltr501_regltr_1v8);
	}
	if (ltr501_regltr_3v) {
		ltr501_power_on_off(0);
		regulator_put(ltr501_regltr_3v);
	}
#endif
done:
	pr_info("ltr501 probe success\n");
	return ret;
}

static int _ltr501_suspend(struct i2c_client *client, pm_message_t mesg)
{
	/* We disable power only if proximity is disabled.  If proximity
	   is enabled, we leave power on because proximity is allowed
	   to wake up device.  We remove power without changing
	   ltr501->power_state because we use that state in resume.
	 */
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	printk("_ltr501_suspend: als/ps status = %d/%d\n",
		ltr501->power_state & LIGHT_ENABLED,
		ltr501->power_state & PROXIMITY_ENABLED);
	if ((ltr501->power_state & LIGHT_ENABLED)
	    && (ltr501->power_state & PROXIMITY_ENABLED)) {
		hrtimer_cancel(&ltr501->timer_light);
		cancel_work_sync(&ltr501->work_light);
		ltr501_alp_enable(ltr501, false, true);
	} else if ((ltr501->power_state & LIGHT_ENABLED)
		   && !(ltr501->power_state & PROXIMITY_ENABLED)) {
		hrtimer_cancel(&ltr501->timer_light);
		cancel_work_sync(&ltr501->work_light);
		cancel_work_sync(&ltr501->work_prox);
		ltr501_alp_enable(ltr501, false, false);
	}
#ifdef LTR501_SUSPEND_PM
	if (!(ltr501->power_state & PROXIMITY_ENABLED)) {
		ltr501_power_on_off(0);
		ltr501_cs_power_on_off(0);
	}
#endif
	return 0;
}

static int _ltr501_resume(struct i2c_client *client)
{
	/* Turn power back on if we were before suspend. */
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	printk("_ltr501_resume: als/ps status = %d/%d\n",
		ltr501->power_state & LIGHT_ENABLED,
		ltr501->power_state & PROXIMITY_ENABLED);
#ifdef LTR501_SUSPEND_PM
	if (!(ltr501->power_state & PROXIMITY_ENABLED)) {
		ltr501_power_on_off(1);
		ltr501_cs_power_on_off(1);
		ltr501_chip_init(ltr501);
	}
#endif
	if ((ltr501->power_state & LIGHT_ENABLED)
	    && (ltr501->power_state & PROXIMITY_ENABLED)) {
		ltr501_alp_enable(ltr501, true, true);
		hrtimer_start(&ltr501->timer_light,
			      ktime_set(0, LIGHT_SENSOR_START_TIME_DELAY),
			      HRTIMER_MODE_REL);
	} else if ((ltr501->power_state & LIGHT_ENABLED)
		   && !(ltr501->power_state & PROXIMITY_ENABLED)) {
		ltr501_alp_enable(ltr501, true, false);
		hrtimer_start(&ltr501->timer_light,
			      ktime_set(0, LIGHT_SENSOR_START_TIME_DELAY),
			      HRTIMER_MODE_REL);
	}

	return 0;
}

static int ltr501_remove(struct i2c_client *client)
{
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	unregister_early_suspend(&ltr501->early_suspend);
	input_unregister_device(ltr501->light_input_dev);
	input_unregister_device(ltr501->proximity_input_dev);
	free_irq(ltr501->irq, NULL);
	destroy_workqueue(ltr501->light_wq);
	destroy_workqueue(ltr501->prox_wq);
	mutex_destroy(&ltr501->power_lock);
	wake_lock_destroy(&ltr501->prox_wake_lock);
	kfree(ltr501->pdata);
	kfree(ltr501);
#ifdef LTR501_RUNTIME_PM
	if (ltr501_regltr_1v8) {
		ltr501_cs_power_on_off(0);
		regulator_put(ltr501_regltr_1v8);
	}
	if (ltr501_regltr_3v) {
		ltr501_power_on_off(0);
		regulator_put(ltr501_regltr_3v);
	}
#endif
	return 0;
}

static void ltr501_shutdown(struct i2c_client *client)
{
	int ret;
	struct ltr501_data *ltr501 = i2c_get_clientdata(client);
	ret = ltr501_i2c_write(ltr501, LTR501_ALS_CONTR, 0x0);
	if (ret < 0)
		pr_err("ltr501: shutdown fail with %d\n", ret);
}

static const struct i2c_device_id ltr501_device_id[] = {
	{"ltr501", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ltr501_device_id);

static const struct of_device_id ltr501_of_match[] = {
	{.compatible = "bcm,ltr501",},
	{},
};

MODULE_DEVICE_TABLE(of, ltr501_of_match);

static struct i2c_driver ltr501_i2c_driver = {
	.driver = {
		   .name = "ltr501",
		   .owner = THIS_MODULE,
		   .of_match_table = ltr501_of_match,
		   },
	.probe = ltr501_probe,
	.remove = ltr501_remove,
	.id_table = ltr501_device_id,
	.shutdown = ltr501_shutdown,
};

static int __init ltr501_init(void)
{
	return i2c_add_driver(&ltr501_i2c_driver);
}

static void __exit ltr501_exit(void)
{
	i2c_del_driver(&ltr501_i2c_driver);
}

module_init(ltr501_init);
module_exit(ltr501_exit);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("LTR501 Ambient Light and Proximity Sensor Driver");
MODULE_LICENSE("GPL v2");
