/* drivers/misc/l3gd20.c
 *
 * Copyright (C) 2012 Renesas Mobile Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/l3gd20.h>
#include <linux/of.h>
#include "l3gd20_local.h"
#include <linux/regulator/consumer.h>


/* Define output data rate table */
static const struct output_rate odr_table[] = {
	{	2,	ODR760|BW10},
	{	3,	ODR380|BW01},
	{	6,	ODR190|BW00},
	{	11,	ODR095|BW00},
};

/* APIs prototype*/
static int __init l3gd20_init(void);
static void __exit l3gd20_exit(void);
static int l3gd20_i2c_read(unsigned char reg_address,
	unsigned char *buf, int len);
static int l3gd20_i2c_write(unsigned char reg_address,
	unsigned char *buf, int len);
static int l3gd20_register_update(unsigned char reg_address,
	unsigned char mask, unsigned char new_bit_values);
static void l3gd20_early_suspend(struct early_suspend *h);
static void l3gd20_early_resume(struct early_suspend *h);
static int l3gd20_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *devid);
static int l3gd20_i2c_remove(struct i2c_client *client);
static int l3gd20_misc_open(struct inode *ip, struct file *fp);
static int l3gd20_misc_release(struct inode *ip, struct file *fp);
static long l3gd20_misc_ioctl(struct file *filp, unsigned int cmd,
	unsigned long arg);
static enum hrtimer_restart l3gd20_timer_func(struct hrtimer *timer);
static void l3gd20_timer_work_func(struct work_struct *work);
static int l3gd20_hw_init(const unsigned char *buff);
static int l3gd20_get_data(struct l3gd20_triple *data);
static void l3gd20_report_values(struct l3gd20_triple *data);
static int l3gd20_enable(unsigned char enable_state);
static int l3gd20_set_output_data_rate(unsigned long poll_interval);
static int l3gd20_set_bandwidth(unsigned char bw_value);
static int l3gd20_enable_high_pass_filter(unsigned char enable);
static int l3gd20_set_high_pass_filter_mode(unsigned char hpf_mode);
static int l3gd20_set_high_pass_filter_cutoff_frequency(
						unsigned char hpf_cf_freq);
static int l3gd20_set_sensitivity(unsigned char fs_value);

#ifdef RUNTIME_PM
static int l3gd20_power_on_off(bool flag);
static int l3gd20_cs_power_on_off(bool flag);
#endif

/* Define global variable */
static struct l3gd20_data *l3gd20_info;
static struct i2c_client *l3gd20_client;

#ifdef RUNTIME_PM
static struct regulator *gyro_regltr_18v;
static struct regulator *gyro_regltr_3v;
#endif

/* Define driver operation structure */
static const struct i2c_device_id l3gd20_id[] = {
	{"l3gd20", 0	/*I2C channel*/},
	{}
};

MODULE_DEVICE_TABLE(i2c, l3gd20_id);

#ifdef CONFIG_OF
static const struct of_device_id l3gd20_of_match[] = {
	{.compatible = "st,l3gd20",},
	{},
}
MODULE_DEVICE_TABLE(of, l3gd20_of_match);
#endif

static struct i2c_driver l3gd20_i2c_driver = {
	.driver		= {
	.owner	= THIS_MODULE,
	.name	= L3GD20_NAME,
	.of_match_table = of_match_ptr(l3gd20_of_match),
	},
	.probe = l3gd20_i2c_probe,
	.remove = l3gd20_i2c_remove,
	.id_table = l3gd20_id,
};

static const struct file_operations gyroscope_fops = {
	.owner  = THIS_MODULE,
	.llseek = no_llseek,
	.open = l3gd20_misc_open,
	.release = l3gd20_misc_release,
	.unlocked_ioctl = l3gd20_misc_ioctl
};

static struct miscdevice gyroscope_device = {
	.minor  = MISC_DYNAMIC_MINOR,
	.name   = L3GD20_NAME,
	.fops   = &gyroscope_fops
};

static const unsigned char l3gd20_default_setting[L3GD20_SETTING_MAX] = {
	0,		/* Bandwidth */
	0,		/* Sensitivity */
	0,		/* Enable HPF */
	0,		/* HPF mode */
	0,		/* HPF cutoff frequency */
};



/*************************************************************************
 *	name	=	l3gd20_i2c_read
 *	func	=	Read the data from register of L3GD20 chip
 *			(Reading multiple bytes is supported)
 *	input	=	unsigned char reg_address, unsigned char *buf, int len
 *	output	=	None
 *	return	=	0, -EIO(-5), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_i2c_read(unsigned char reg_address,
	unsigned char *buf, int len)
{
	int err;
	struct i2c_msg read_msg[2];
	int tries;

	/* Initialize tries */
	tries = 0;

	/* Check the pointer to memory */
	if (NULL == buf) {
		l3gd20_log("Invalid buffer address\n");
		return -EINVAL;
	}

	if (len > 1)
		buf[0] = (AUTO_INCREMENT | reg_address);
	else
		buf[0] = reg_address;

	/* initialize read message */
	read_msg[0].addr = l3gd20_client->addr;
	read_msg[0].flags = (l3gd20_client->flags & I2C_M_TEN);
	read_msg[0].len = 1;
	read_msg[0].buf = buf;

	read_msg[1].addr = l3gd20_client->addr;
	read_msg[1].flags = ((l3gd20_client->flags & I2C_M_TEN) | I2C_M_RD);
	read_msg[1].len = len;
	read_msg[1].buf = buf;

	/* Loop to read the data */
	do {
		err = i2c_transfer(l3gd20_client->adapter, read_msg, 2);
		if (err != 2)
			msleep_interruptible(L3GD20_RETRIES_DELAY);
	} while ((err != 2) && (++tries < L3GD20_I2C_RETRIES));

	if (err != 2) {
		err = -EIO;
		l3gd20_log("Read failed\n");
	} else {
		err = 0;
		l3gd20_log("Read successfully\n");
	}

	return err;
}



/*************************************************************************
 *	name	=	l3gd20_i2c_write
 *	func	=	Write new value to register of L3GD20 chip set
 *			(Writing multiple bytes is supported)
 *	input	=	unsigned char reg_address, unsigned char *buf, int len
 *	output	=	None
 *	return	=	0, -EIO(-5), -EINVAL(-22), -ENOMEM(-12)
 *************************************************************************/
static int l3gd20_i2c_write(unsigned char reg_address,
	unsigned char *buf, int len)
{
	int err;
	struct i2c_msg write_msg;
	int tries;
	unsigned char *temp_buf;
	int i;

	/* Check the pointer to memory */
	if (NULL == buf)
		return -EINVAL;

	if (len <= 0) {
		l3gd20_log("The length of data is not correct !\n");
		return -EINVAL;
	}

	tries = 0;
	temp_buf = kzalloc(len + 1, GFP_KERNEL);
	if (NULL == temp_buf) {
		l3gd20_log("Out of memory\n");
		return -EIO;
	}

	/* Assign the written register address */
	if (len > 1)
		temp_buf[0] = (AUTO_INCREMENT | reg_address);
	else
		temp_buf[0] = reg_address;

	/* Copy written array to another array */
	for (i = 0; i < len; i++)
		temp_buf[i+1] = buf[i];

	/* Initialize written message */
	write_msg.addr = l3gd20_client->addr;
	write_msg.flags = l3gd20_client->flags & I2C_M_TEN;
	write_msg.len = len + 1;
	write_msg.buf = temp_buf;

	/* Loop to write the data */
	do {
		err = i2c_transfer(l3gd20_client->adapter, &write_msg, 1);
		if (err != 1)
			msleep_interruptible(L3GD20_RETRIES_DELAY);
	} while ((err != 1) && (++tries < L3GD20_I2C_RETRIES));

	if (err != 1) {
		err = -EIO;
		l3gd20_log("Write failed\n");
	} else {
		err = 0;
		l3gd20_log("Write successfully !\n");
	}

	kfree(temp_buf);

	return err;
}



/*************************************************************************
 *	name	=	l3gd20_register_update
 *	func	=	Update a bit or group of bits
			on a register of L3GD20 chip
 *	input	=	unsigned char reg_address, unsigned char mask,
*			unsigned char new_bit_values
 *	output	=	None
 *	return	=	0, -EIO(-5), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_register_update(unsigned char reg_address,
	unsigned char mask, unsigned char new_bit_values)
{
	unsigned char buf;
	unsigned char reg_val;
	int ret;

	/* Read the value of target register */
	ret = l3gd20_i2c_read(reg_address, &buf, 1);
	if (ret != 0) {
		l3gd20_log("Can not read\n");
		return ret;
	}

	/* Save to local variable */
	reg_val = buf;

	/* Compare the new value and old value  */
	if ((reg_val & mask) == (new_bit_values & mask)) {
		l3gd20_log("The value of register doesn't need to change\n");
		return 0;
	}

	/* Clear the target bit */
	reg_val = reg_val & (~mask);

	/* Update the value of local variable base on the bit mask */
	reg_val = (reg_val | (mask & new_bit_values));

	/* Write the value of local variable to target register */
	buf = reg_val;
	ret = l3gd20_i2c_write(reg_address, &buf, 1);
	if (ret != 0) {
		l3gd20_log("Register update Failed !\n");
		return ret;
	}

	l3gd20_log("Register update successfully !\n");
	return 0;
}



/*************************************************************************
 *	name	=	l3gd20_suspend
 *	func	=	Suspend I2C device of Gyroscope device driver
 *	input	=	struct device *dev
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static void l3gd20_early_suspend(struct early_suspend *h)
{

	l3gd20_info->suspend_flag = true;
	l3gd20_log("\nCancel HR timer ");
	hrtimer_cancel(&l3gd20_info->timer);

	if (flush_work(&l3gd20_info->timer_work_func))
		l3gd20_log("\n l3gd20: flush work waited ");

	mutex_lock(&l3gd20_info->lock);

	if (l3gd20_info->poll_cycle <= POWER_POLLING_THRES) {
		/* Power off Gyroscope */
		l3gd20_register_update(L3GD20_CTRL_REG1, 0x0F, 0);
	}

	mutex_unlock(&l3gd20_info->lock);

	l3gd20_power_on_off(0);

	l3gd20_log("l3gd20 early Suspend successfully !\n");
	return;
}



/*************************************************************************
 *	name	=	l3gd20_resume
 *	func	=	Resume I2C device of Gyroscope device driver
 *	input	=	struct device *dev
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static void l3gd20_early_resume(struct early_suspend *h)
{
	int drv_state;

	drv_state = 0;

	mutex_lock(&l3gd20_info->lock);
	l3gd20_info->suspend_flag = false;
	l3gd20_power_on_off(1);

	drv_state = atomic_read(&l3gd20_info->enable);
	if (DISABLE == drv_state) {
		/* Driver is deactivated before */
		mutex_unlock(&l3gd20_info->lock);
		l3gd20_log("The driver was deactivated before !\n");
		return;
	}

	/* Restart HR timer */
	hrtimer_start(&l3gd20_info->timer, ktime_set(0,
		l3gd20_info->real_dly * MSEC_TO_NSEC), HRTIMER_MODE_REL);

	if (l3gd20_info->poll_cycle <= POWER_POLLING_THRES) {
		/* Enable output data & Turn power on */
		l3gd20_register_update(L3GD20_CTRL_REG1, 0x0F, 0x0F);
		msleep(20);
	}

	mutex_unlock(&l3gd20_info->lock);
	l3gd20_log("l3gd20 early Resume successfully !\n");
	return;
}

/*************************************************************************
 *      name    =       l3gd20_power_on_off
 *      func    =       Switch ON/OFF the 3v regualtor
 *      input   =       Boolean flag either 0/1
 *      output  =       None
 *      return  =       0
 **************************************************************************/
#ifdef RUNTIME_PM
static int l3gd20_power_on_off(bool flag)
{
	int ret;

	if (!gyro_regltr_3v) {
		pr_err("Error: gyro_regltr_3v is unavailable\n");
		return -1;
	}

	if ((flag == 1)) {
		l3gd20_log("\n LDO on %s ", __func__);
		ret = regulator_enable(gyro_regltr_3v);
		if (ret)
			pr_err("Error: Gyro 3v Regulator Enable failed\n");
	} else if ((flag == 0)) {
		l3gd20_log("\n LDO off %s ", __func__);
		ret = regulator_disable(gyro_regltr_3v);
		if (ret)
			pr_err("Error: Gyro 3v Regulator disable failed\n");
	}
	return 0;
}
#endif

/*************************************************************************
 *  name    =       l3gd20_power_cs_on_off
 *  func    =       Switch ON/OFF the 1.8v regualtor
 *  input   =       Boolean flag either 0/1
 *  output  =       None
 *  return  =       0
 ***************************************************************************/
#ifdef RUNTIME_PM
static int l3gd20_cs_power_on_off(bool flag)
{
	int ret;

	if (!gyro_regltr_18v) {
		pr_err("Error: gyro_regltr_18v is unavailable\n");
		return -1;
	}

	if ((flag == 1)) {
		l3gd20_log("\n LDO on %s ", __func__);
		ret = regulator_enable(gyro_regltr_18v);
		if (ret)
			pr_err("Error: Gyro 1v8 Regulator enable failed\n");
	} else if ((flag == 0)) {
		l3gd20_log("\n LDO off %s ", __func__);
		ret = regulator_disable(gyro_regltr_18v);
		if (ret)
			pr_err("Error: Gyro 1v8 Regulator disable failed\n");
	}
	return 0;
}
#endif

/*************************************************************************
 *	name	=	l3gd20_i2c_probe
 *	func	=	Initializes register, allocates resource(timer, memory,
 *			work queue, interrupt, input device node)
*			and registers Gyroscope MISC device with Linux kernel
 *	input	=	struct i2c_client *client,
			const struct i2c_device_id *devid
 *	output	=	None
 *	return	=	0, -EIO(-5), -ENOMEM(-12), -ENODEV(-19), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *devid)
{
	int ret;
	int err;
	struct device_node *np;
	signed char orientation[9];
	int i;

	err = -1;

#ifdef RUNTIME_PM
	l3gd20_log("%s: Gyro - preparing regulators\n", __func__);
	gyro_regltr_18v = regulator_get(NULL, "sensor_1v8");
	if (IS_ERR_OR_NULL(gyro_regltr_18v)) {
		pr_err("%s: Gyro - failed to get regulator\n", __func__);
		return -EBUSY;
	}
	regulator_set_voltage(gyro_regltr_18v, 1800000, 1800000);
	ret = regulator_enable(gyro_regltr_18v);
	if (ret)
		pr_err("Error: Gyro 1v8 regulator enable failed");
	gyro_regltr_3v = regulator_get(NULL, "sensor_3v");
	if (IS_ERR_OR_NULL(gyro_regltr_3v)) {
		pr_err("%s: Gyro - failed to get regulator\n", __func__);
		return -EBUSY;
	}
	regulator_set_voltage(gyro_regltr_3v, 3000000, 3000000);
	ret = regulator_enable(gyro_regltr_3v);
	if (ret)
		pr_err("Error Gyro 3v regulator enable failed");
#endif
	/* Check I2C functionality */
	err = i2c_check_functionality(client->adapter, I2C_FUNC_I2C);
	if (err < 0) {
		ret = -ENODEV;
		l3gd20_log("I2C functionality is not supported\n");
		goto err_check_i2c_func;
	}

	if (!client->dev.platform_data) {
		np = client->dev.of_node;
		if (of_property_read_u8_array(np, "orientation",
			orientation, sizeof(orientation)))
			return -EINVAL;
	}

	/* Allocate global variable */
	l3gd20_info = kzalloc(sizeof(struct l3gd20_data), GFP_KERNEL);
	if (NULL == l3gd20_info) {
		ret = -ENOMEM;
		l3gd20_log("Out of memory\n");
		goto err_check_i2c_func;
	}

	memset(l3gd20_info, 0, sizeof(struct l3gd20_data));
	l3gd20_info->suspend_flag = false;
	/* Save I2C client for global variable */
	l3gd20_client = client;

	/* Initialize wake lock, mutex */
	mutex_init(&l3gd20_info->lock);
	wake_lock_init(&l3gd20_info->wakelock, WAKE_LOCK_SUSPEND,
		"l3gd20-wakelock");

	/* Allocate input device */
	l3gd20_info->gyro_input_dev = input_allocate_device();
	if (NULL == l3gd20_info->gyro_input_dev) {
		ret = -ENOMEM;
		l3gd20_log("Allocate input device failed !\n");
		goto err_allocate_input_dev;
	}
	l3gd20_info->gyro_input_dev->name = "gyroscope";

	set_bit(EV_ABS, l3gd20_info->gyro_input_dev->evbit);
	input_set_abs_params(l3gd20_info->gyro_input_dev, EVENT_TYPE_GYRO_X,
		L3GD20_MIN_RANGE, L3GD20_MAX_RANGE, 0/*FUZZ*/, 0/*FLAT*/);
	input_set_abs_params(l3gd20_info->gyro_input_dev, EVENT_TYPE_GYRO_Y,
		L3GD20_MIN_RANGE, L3GD20_MAX_RANGE, 0/*FUZZ*/, 0/*FLAT*/);
	input_set_abs_params(l3gd20_info->gyro_input_dev, EVENT_TYPE_GYRO_Z,
		L3GD20_MIN_RANGE, L3GD20_MAX_RANGE, 0/*FUZZ*/, 0/*FLAT*/);

	l3gd20_info->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	l3gd20_info->early_suspend.suspend = l3gd20_early_suspend;
	l3gd20_info->early_suspend.resume = l3gd20_early_resume;
	/*register early suspend for l3gd20 driver*/
	register_early_suspend(&l3gd20_info->early_suspend);

	for (i = 0; i < 9; i++)
		l3gd20_info->orientation[i] = orientation[i];

	/* Register input device */
	ret = input_register_device(l3gd20_info->gyro_input_dev);
	if (ret < 0) {
		ret = -ENODEV;
		l3gd20_log("Register input device failed\n");
		goto err_register_input_dev;
	}

	/* Initialize high resolution timer */
	hrtimer_init(&l3gd20_info->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	l3gd20_info->timer.function = l3gd20_timer_func;

	/* Initialize work queue */
	INIT_WORK(&l3gd20_info->timer_work_func, l3gd20_timer_work_func);

	/* Register MISC device */
	ret = misc_register(&gyroscope_device);
	if (ret < 0) {
		l3gd20_log("MISC registration failed\n");
		ret = -ENODEV;
		goto err_register_misc_dev;
	}

	/* Hardware initialize */
	ret = l3gd20_hw_init(l3gd20_default_setting);
	if (ret != 0) {
		l3gd20_log("HW initialization failed\n");
		goto err_hw_initialize;
	}

	/* Set default output data rate */
	/* 100ms is set as default output data rate */
	l3gd20_set_output_data_rate(100);

	/* Turn power off */
	ret = l3gd20_register_update(L3GD20_CTRL_REG1, 0x0F, 0);
	if (ret != 0) {
		l3gd20_log("Can not turn power off\n");
		goto err_hw_initialize;
	}

	l3gd20_log("Probe successfully !\n");
	return 0;

err_hw_initialize:

/*err_register_interrupt:*/
	/* Deregister MISC device */
	misc_deregister(&gyroscope_device);

err_register_misc_dev:
	/* Deregister input device */
	input_unregister_device(l3gd20_info->gyro_input_dev);

err_register_input_dev:
	/* Release input device */
	input_free_device(l3gd20_info->gyro_input_dev);

err_allocate_input_dev:
	/* Destroy mutex lock */
	mutex_destroy(&l3gd20_info->lock);

	/* Destroy wake lock */
	wake_lock_destroy(&l3gd20_info->wakelock);

	/* Release global variable */
	kfree(l3gd20_info);

err_check_i2c_func:
	l3gd20_info = NULL;

#ifdef RUNTIME_PM
	l3gd20_power_on_off(0);
	l3gd20_cs_power_on_off(0);
#endif
	return ret;
}



/*************************************************************************
 *	name	=	l3gd20_i2c_remove
 *	func	=	Remove timer, MISC device, work queue,
 *			input device of Gyroscope device driver
 *	input	=	struct i2c_client *client
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static int l3gd20_i2c_remove(struct i2c_client *client)
{

	/* Release MISC device  */
	misc_deregister(&gyroscope_device);

	/* Deregister input device */
	input_unregister_device(l3gd20_info->gyro_input_dev);

	/* Release input device */
	input_free_device(l3gd20_info->gyro_input_dev);

	/* Destroy wake lock */
	wake_lock_destroy(&l3gd20_info->wakelock);

	/* Release global variable */
	kfree(l3gd20_info);

#ifdef RUNTIME_PM
	if (gyro_regltr_18v) {
		l3gd20_cs_power_on_off(0);
		regulator_put(gyro_regltr_18v);
	}
	if (gyro_regltr_3v) {
		l3gd20_power_on_off(0);
		regulator_put(gyro_regltr_3v);
	}
#endif

	return 0;
}



/*************************************************************************
 *	name	=	l3gd20_misc_open
 *	func	=	Open Gyroscope device node
 *	input	=	struct inode *ip, struct file *fp
 *	output	=	None
 *	return	=	0, -EBUSY(-16)
 *************************************************************************/
static int l3gd20_misc_open(struct inode *ip, struct file *fp)
{
	return 0;
}



/*************************************************************************
 *	name	=	l3gd20_misc_release
 *	func	=	Release Gyroscope device driver
 *	input	=	struct inode *ip, struct file *fp
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static int l3gd20_misc_release(struct inode *ip, struct file *fp)
{
	return 0;
}



/*************************************************************************
 *	name	=	l3gd20_misc_ioctl
 *	func	=	I/O control function of Gyroscope device driver
 *	input	=	struct file *filp, unsigned int cmd, unsigned long arg
 *	output	=	None
 *	return	=	0, -EIO(-5), -EFAULT(-14), -EINVAL(-22), -ENOTTY(-25)
 *************************************************************************/
static long l3gd20_misc_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	int ret;
	int err;
	int poll_interval;
	unsigned char flag;
	void __user *argp;

	flag = 0;
	ret = 0;
	err = 0;
	poll_interval = 0;
	argp = (void __user *)arg;

	/* Command differ from L3GD20_IOCTL_SET_ENABLE
		and L3GD20_IOCTL_SET_DELAY and L3GD20_IOCTL_NV_DATA_ADDRESS */
	if ((cmd != L3GD20_IOCTL_SET_ENABLE) &&
		(cmd != L3GD20_IOCTL_SET_DELAY)) {
		l3gd20_log("Invalid IOCTL command\n");
		return -ENOTTY;
	}

	/* Mutex lock */
	mutex_lock(&l3gd20_info->lock);

	switch (cmd) {
	case L3GD20_IOCTL_SET_ENABLE:
	/* Command is L3GD20_IOCTL_SET_ENABLE */
		/*Get data from user side*/
		err = copy_from_user(&flag, argp, sizeof(flag));
		if (err != 0) {
			l3gd20_log("Can not copy data from user side\n");
			ret = -EFAULT;
			break;
		}

		/*if (flag)
			l3gd20_power_on_off(1);*/

		/* Enable Gyro device driver */
		ret = l3gd20_enable(flag);

		/*if (!flag)
			l3gd20_power_on_off(0);*/

		break;
	case L3GD20_IOCTL_SET_DELAY:
	/* command is L3GD20_IOCTL_SET_DELAY */
		/* Get data from user side */
		err = copy_from_user(&poll_interval, argp,
			sizeof(poll_interval));
		if (err != 0) {
			l3gd20_log("Can not copy data from user side\n");
			ret = -EFAULT;
			break;
		}
		/* Set the delay period */
		ret = l3gd20_set_output_data_rate(poll_interval);
		break;
	}

	/* Mutex unlock */
	mutex_unlock(&l3gd20_info->lock);

	/* Return saved code */
	l3gd20_log("End of ioctl()\n");
	return ret;
}

/*************************************************************************
 *	name	=	l3gd20_timer_func
 *	func	=	HR timer callback function. This function is used to
 *			schedule work queue function to get
			and report Gyroscope data
 *	input	=	struct hrtimer *timer
 *	output	=	None
 *	return	=	HRTIMER_NORESTART(0)
 *************************************************************************/
static enum hrtimer_restart l3gd20_timer_func(struct hrtimer *timer)
{

	if (!l3gd20_info->suspend_flag)
		schedule_work(&l3gd20_info->timer_work_func);
	l3gd20_log("End of timer callback function\n");
	return HRTIMER_NORESTART;
}



/*************************************************************************
 *	name	=	l3gd20_timer_work_func
 *	func	=	Work queue function for reporting data
			to HAL when timer event occurs
 *	input	=	struct work_struct *work
 *	output	=	None
 *	return	=	None
 *************************************************************************/
static void l3gd20_timer_work_func(struct work_struct *work)
{
	int ret;
	struct l3gd20_triple gyro_data;
	long secs;
	unsigned long nsecs;

	ret = -1;
	secs = 0;
	nsecs = 0;

	/* Mutex lock */
	mutex_lock(&l3gd20_info->lock);

	/* Get wake lock */
	wake_lock(&l3gd20_info->wakelock);

	if (l3gd20_info->poll_cycle > POWER_POLLING_THRES) {
		/* Enable output data & Power on */
		l3gd20_register_update(L3GD20_CTRL_REG1, 0x0F, 0x0F);

		/* Delay a time period to power on */
		msleep(20);
	}

	/* Copy and report data */
	ret = l3gd20_get_data(&gyro_data);
	if (0 == ret)
		l3gd20_report_values(&gyro_data);

	if (l3gd20_info->poll_cycle > POWER_POLLING_THRES) {
		/* Disable output data & power off */
		l3gd20_register_update(L3GD20_CTRL_REG1, 0x0F, 0);
	}

	/* Restart HR timer */
	if (!l3gd20_info->suspend_flag)
		hrtimer_start(&l3gd20_info->timer, ktime_set(0,
		l3gd20_info->real_dly * MSEC_TO_NSEC), HRTIMER_MODE_REL);

	/* Release wake lock */
	wake_unlock(&l3gd20_info->wakelock);

	/* Mutex unlock */
	mutex_unlock(&l3gd20_info->lock);
	l3gd20_log("End of timer work function\n");
}



/*************************************************************************
 *	name	=	l3gd20_hw_init
 *	func	=	Configure registers of L3GD20 chip by
			parameter array
 *	input	=	unsigned char* buff
 *	output	=	None
 *	return	=	0, -EIO(-5), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_hw_init(const unsigned char *buff)
{
	int ret;

	ret = 0;

	/* Check input argument */
	if (NULL == buff) {
		l3gd20_log("Invalid buffer address\n");
		return -EINVAL;
	}

	/* Bandwidth setting */
	ret = l3gd20_set_bandwidth(buff[0]);
	if (ret != 0) {
		l3gd20_log("Set bandwidth failed\n");
		return ret;
	}

	/* Sensitivity setting */
	ret = l3gd20_set_sensitivity(buff[1]);
	if (ret != 0) {
		l3gd20_log("Set sensitivity failed\n");
		return ret;
	}

	/* Enable high pass filter */
	ret = l3gd20_enable_high_pass_filter(buff[2]);
	if (ret != 0) {
		l3gd20_log("Enable high pass filter failed\n");
		return ret;
	}

	/* High pass filter mode setting */
	ret = l3gd20_set_high_pass_filter_mode(buff[3]);
	if (ret != 0) {
		l3gd20_log("Set high pass filter mode failed\n");
		return ret;
	}

	/* High pass filter cutoff frequency setting */
	ret = l3gd20_set_high_pass_filter_cutoff_frequency(buff[4]);
	if (ret != 0) {
		l3gd20_log("Set high pass filter cutoff frequency failed\n");
		return ret;
	}

	l3gd20_log("HW initialization successfully !\n");
	return 0;
}



/*************************************************************************
 *	name	=	l3gd20_get_data
 *	func	=	Get the Gyroscope data from L3GD20 chip
 *	input	=	struct l3gd20_triple *data
 *	output	=	struct l3gd20_triple *data
 *	return	=	0, -EIO(-5), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_get_data(struct l3gd20_triple *data)
{
	unsigned char gyro_data[6];
	int ret;
	short hw_data[3];

	ret = 0;
	memset(gyro_data, 0, sizeof(gyro_data));
	memset(hw_data, 0, sizeof(hw_data));

	/* Check the input argument */
	if (NULL == data) {
		l3gd20_log("Invalid buffer address\n");
		return -EINVAL;
	}

	/* Read Gyro data */
	ret = l3gd20_i2c_read(L3GD20_OUT_X_L, gyro_data, 6);
	if (ret != 0) {
		l3gd20_log("Can not read data from register\n");
		return ret;
	}

	/* Convert 8 bit data to 16 bit data */
	hw_data[0] = (short) (((gyro_data[1]) << 8) | gyro_data[0]);
	hw_data[1] = (short) (((gyro_data[3]) << 8) | gyro_data[2]);
	hw_data[2] = (short) (((gyro_data[5]) << 8) | gyro_data[4]);

	/* Convert Gyro data base on sensitivity */
	switch (l3gd20_info->sensitivity) {
	case SENSITIVE_1:
		hw_data[0] = (short)((hw_data[0] *
			CONVERT_SENSITY_250_NUMERATOR)
			/ CONVERT_SENSITY_250_DENOMINATOR);

		hw_data[1] = (short)((hw_data[1] *
				CONVERT_SENSITY_250_NUMERATOR)
				/ CONVERT_SENSITY_250_DENOMINATOR);

		hw_data[2] = (short)((hw_data[2] *
				CONVERT_SENSITY_250_NUMERATOR)
			/ CONVERT_SENSITY_250_DENOMINATOR);
		break;

	case SENSITIVE_2:
		hw_data[0] = (short)((hw_data[0] *
				CONVERT_SENSITY_500_NUMERATOR)
			/ CONVERT_SENSITY_500_DENOMINATOR);

		hw_data[1] = (short)((hw_data[1] *
				CONVERT_SENSITY_500_NUMERATOR)
			/ CONVERT_SENSITY_500_DENOMINATOR);

		hw_data[2] = (short)((hw_data[2] *
				CONVERT_SENSITY_500_NUMERATOR)
			/ CONVERT_SENSITY_500_DENOMINATOR);
		break;
	case SENSITIVE_3:
	case SENSITIVE_4:
		hw_data[0] = (short)((hw_data[0] *
				CONVERT_SENSITY_2000_NUMERATOR)
			/ CONVERT_SENSITY_2000_DENOMINATOR);

		hw_data[1] = (short)((hw_data[1] *
				CONVERT_SENSITY_2000_NUMERATOR)
			/ CONVERT_SENSITY_2000_DENOMINATOR);

		hw_data[2] = (short)((hw_data[2] *
				CONVERT_SENSITY_2000_NUMERATOR)
			/ CONVERT_SENSITY_2000_DENOMINATOR);
		break;
	}

	/* multiply the orientation matrix based to align with position */
	data->x = hw_data[0] * l3gd20_info->orientation[0] +
		hw_data[1] * l3gd20_info->orientation[3] +
		hw_data[2] * l3gd20_info->orientation[6];

	data->y = hw_data[0] * l3gd20_info->orientation[1] +
		hw_data[1] * l3gd20_info->orientation[4] +
		hw_data[2] * l3gd20_info->orientation[7];

	data->z = hw_data[0] * l3gd20_info->orientation[2] +
		hw_data[1] * l3gd20_info->orientation[5] +
		hw_data[2] * l3gd20_info->orientation[8];

	l3gd20_log("Get data successfully !\n");
	return 0;
}



/*************************************************************************
 *	name	=	l3gd20_report_values
 *	func	=	Send Gyroscope data to HAL layer
 *	input	=	struct l3gd20_triple *data
 *	output	=	None
 *	return	=	None
 *************************************************************************/
static void l3gd20_report_values(struct l3gd20_triple *data)
{

	if (data != NULL) {
		/* Send Gyro data to input device */

		input_report_abs(l3gd20_info->gyro_input_dev,
			EVENT_TYPE_GYRO_X, (int)data->x);
		input_report_abs(l3gd20_info->gyro_input_dev,
			EVENT_TYPE_GYRO_Y, (int)data->y);
		input_report_abs(l3gd20_info->gyro_input_dev,
			EVENT_TYPE_GYRO_Z, (int)data->z);

		/* sync is removed in Kernel 3.10 */
		/*l3gd20_info->gyro_input_dev->sync = 0;*/
		input_sync(l3gd20_info->gyro_input_dev);
	}
	l3gd20_log("End of report data !\n");
}



/*************************************************************************
 *	name	=	l3g4200d_enable
 *	func	=	Activating/ Deactivating functionality
			of Gyroscope device driver
 *	input	=	unsigned char enable_state
 *	output	=	None
 *	return	=	0, -EIO(-5), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_enable(unsigned char enable_state)
{
	int ret;
	int drv_state;
	long secs;
	unsigned long nsecs;

	drv_state = 0;
	ret = -1;
	secs = 0;
	nsecs = 0;
	if (l3gd20_info->suspend_flag) {
		l3gd20_log("suspend_flag: l3gd20_enable");
		return 0;
	}

	/* Get the status of Gyroscope */
	drv_state = atomic_read(&l3gd20_info->enable);
	if (enable_state != 0) {

		/* Enable Gyroscope driver */
		/* Check wether Gyroscope has been activated? */
		if (drv_state != 0) {
			l3gd20_log("Driver has been activated\n");
			return 0;
		}

		if (l3gd20_info->poll_cycle <= POWER_POLLING_THRES) {
			/* Turn power on & Enable output data */
			ret = l3gd20_register_update(L3GD20_CTRL_REG1,
				0x0F, 0x0F);
			if (ret != 0) {
				l3gd20_log("Can not turn power on\n");
				return -EIO;
			}
			msleep(20);
			/* Start high resolution timer timer */
			hrtimer_start(&l3gd20_info->timer, ktime_set(0,
			l3gd20_info->real_dly * MSEC_TO_NSEC),
					HRTIMER_MODE_REL);

		} else {
			/* Start high resolution timer */
			hrtimer_start(&l3gd20_info->timer, ktime_set(0,
		l3gd20_info->real_dly * MSEC_TO_NSEC), HRTIMER_MODE_REL);
		}
			/* Set status flag to ACTIVATE */
			atomic_set(&l3gd20_info->enable, 1);
	} else {
		/* Disable Gyroscope driver */
		/* Check wether Gyroscope has been deactivated? */
		if (0 == drv_state) {
			l3gd20_log("Driver has been deactivated\n");
			return 0;
		}

		/* Cancel high resolution timer */
		hrtimer_cancel(&l3gd20_info->timer);

		/* Power off & Disable output data */
		ret = l3gd20_register_update(L3GD20_CTRL_REG1, 0x0F, 0);
		if (ret != 0) {
			l3gd20_log("Can not turn power off\n");
			return -EIO;
		}

		/* Set status flag to DEACTIVATE */
		atomic_set(&l3gd20_info->enable, 0);
	}

	l3gd20_log("Enable Gyroscope driver successfully !\n");
	return 0;
}


/*************************************************************************
 *	name	=	l3gd20_set_output_data_rate
 *	func	=	Set the output data rate of L3GD20 chip, calculate and
 *			create timer handler to copy Gyro data periodically
 *	input	=	unsigned long poll_interval
 *	output	=	None
 *	return	=	0, -EIO(-5), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_set_output_data_rate(unsigned long poll_interval)
{
	int drv_state;
	unsigned char odr_idx;
	long secs;
	unsigned long nsecs;
	int ret;

	ret = -1;
	drv_state = -1;
	odr_idx = -1;
	secs = 0;
	nsecs = 0;

	if (l3gd20_info->suspend_flag) {
		l3gd20_log("Suspend_flag: l3gd20_set_output_data_rate");
		return 0;
	}

	/* Get the activation status of the Gyroscope driver */
	drv_state = atomic_read(&l3gd20_info->enable);

	/* Cancel high resolution timer */
	hrtimer_cancel(&l3gd20_info->timer);

	if ((l3gd20_info->poll_cycle < POWER_POLLING_THRES)
		&& (ENABLE == drv_state)) {
		/* Disable output data & Power off */
		ret = l3gd20_register_update(L3GD20_CTRL_REG1, 0x0F, 0);
		if (ret != 0) {
			l3gd20_log("Can not turn power off\n");
			return -EIO;
		}
	}

	if (poll_interval > POWER_POLLING_THRES) {
		/* Set local variable "setODR" to min ODR */
		odr_idx = 0;
	} else {
		/* Calculate  ODR and save to local variable "setODR" */
		if (poll_interval <= 3)
			odr_idx = 0;
		else if ((poll_interval > 3) && (poll_interval <= 6))
			odr_idx = 1;
		else if ((poll_interval > 6) && (poll_interval <= 11))
			odr_idx = 2;
		else
			odr_idx = 0;
	}

	/* Write "setODR" value to register */
	ret = l3gd20_register_update(L3GD20_CTRL_REG1,
		0xF0, odr_table[odr_idx].mask);
	if (ret != 0) {
		l3gd20_log("Can not set output data rate\n");
		return -EIO;
	}

	/* Save "delay" to "real_dly" field of global variable */
	l3gd20_info->real_dly = poll_interval;
	/* Save "delay" to "poll_cycle" field of global variable */
	l3gd20_info->poll_cycle = poll_interval;

	if (ENABLE == drv_state) {
		if (l3gd20_info->poll_cycle <= POWER_POLLING_THRES) {
			/* Power on */
			ret =
			l3gd20_register_update(L3GD20_CTRL_REG1, 0x0F, 0x0F);

			if (ret != 0) {
				l3gd20_log("Can not turn power on\n");
				return -EIO;
			}
			msleep(20);
		} else {
			/* Power off */
			ret = l3gd20_register_update(L3GD20_CTRL_REG1, 0x0F, 0);
			if (ret != 0) {
				l3gd20_log("Can not turn power off\n");
				return -EIO;
			}
		}
		/* Start high resolution timer */
		hrtimer_start(&l3gd20_info->timer, ktime_set(0,
		l3gd20_info->real_dly * MSEC_TO_NSEC), HRTIMER_MODE_REL);
	}

	l3gd20_log("Set output data rate successfully !\n");
	return 0;
}



/*************************************************************************
 *	name	=	l3gd20_set_bandwidth
 *	func	=	Set the bandwidth mode of L3GD20 chip
 *	input	=	unsigned char bw_value
 *	output	=	None
 *	return	=	0, -EIO(-5), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_set_bandwidth(unsigned char bw_value)
{
	int ret;

	ret = -1;

	/* Check bandwidth mode */
	if (bw_value > BW_MODE4) {
		l3gd20_log("Invalid bandwidth mode\n");
		return -EINVAL;
	}

	/* Update bits 4,5 of CTRL_REG1 */
	ret = l3gd20_register_update(L3GD20_CTRL_REG1, 0x30, (bw_value << 4));


	l3gd20_log("End of set bandwidth\n");
	return ret;
}



/*************************************************************************
 *	name	=	l3gd20_enable_high_pass_filter
 *	func	=	Enable/disable high-pass filter of L3GD20 chip
 *	input	=	unsigned char enable
 *	output	=	None
 *	return	=	0, -EIO(-5), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_enable_high_pass_filter(unsigned char enable)
{
	int ret;

	ret = -1;

	if (0 == enable) {
		/* Update bit 4 of CTRL_REG5 with 0 */
		ret = l3gd20_register_update(L3GD20_CTRL_REG5, 0x10, 0);
	} else {
		/* Update bit 4 of CTRL_REG5 with 1 */
		ret = l3gd20_register_update(L3GD20_CTRL_REG5, 0x10, 0x10);
	}

	l3gd20_log("End of enabling  high pass filter\n");
	return ret;
}



/*************************************************************************
 *	name	=	l3gd20_set_high_pass_filter_mode
 *	func	=	Set high-pass filter mode of L3GD20 chip
 *	input	=	unsigned char hpf_mode
 *	output	=	None
 *	return	=	0, -EIO(-5), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_set_high_pass_filter_mode(unsigned char hpf_mode)
{
	int ret;

	ret = -1;

	if (hpf_mode > HPF_MODE4) {
		l3gd20_log("Invalid HPF mode\n");
		return -EINVAL;
	}

	/* Update bits 4,5 of CTRL_REG2 */
	ret = l3gd20_register_update(L3GD20_CTRL_REG2, 0x30, hpf_mode << 4);

	l3gd20_log("End of setting HPF mode\n");
	return ret;
}



/*************************************************************************
 *	name	=	l3gd20_set_high_pass_filter_cutoff_frequency
 *	func	=	Set high-pass filter cutoff frequency of L3GD20 chip
 *	input	=	unsigned char hpf_cf_freq
 *	output	=	None
 *	return	=	0, -EIO(-5), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_set_high_pass_filter_cutoff_frequency(
						unsigned char hpf_cf_freq)
{
	int ret;

	ret = -1;

	if (hpf_cf_freq > HPF_FRE_SEL10) {
		l3gd20_log("Invalid HPF cutoff frequency\n");
		return -EINVAL;
	}
	/* Update bits 0,1,2,3 of CTRL_REG2 */
	ret = l3gd20_register_update(L3GD20_CTRL_REG2, 0x0F, hpf_cf_freq);

	l3gd20_log("End of set high pass filter cut off frequency\n");
	return ret;
}



/*************************************************************************
 *	name	=	l3gd20_set_sensitivity
 *	func	=	Set sensitivity of L3GD20 chip
 *	input	=	unsigned char fs_value
 *	output	=	None
 *	return	=	0, -EIO(-5), -EINVAL(-22)
 *************************************************************************/
static int l3gd20_set_sensitivity(unsigned char fs_value)
{
	int ret;

	ret = -1;

	if (fs_value > SENSITIVE_4) {
		l3gd20_log("Invalid sensitivity\n");
		return -EINVAL;
	}

	/* Update bits 4,5 of CTRL_REG4 */
	ret = l3gd20_register_update(L3GD20_CTRL_REG4, 0x30, fs_value << 4);

	/* Save value to global variable */
	if (0 == ret)
		l3gd20_info->sensitivity = fs_value;

	l3gd20_log("End of setting sensitivity\n");
	return ret;
}



/*************************************************************************
 *	name	=	l3gd20_get_nv
 *	func	=	Read NV value from NV memory address
 *	input	=	unsigned long addr
 *	output	=	None
 *	return	=	None
 *************************************************************************/

/*************************************************************************
 *	name	=	l3gd20_init
 *	func	=	Set GPIO port, create I2C slave device
			for Gyroscope device driver
 *	input	=	None
 *	output	=	None
 *	return	=	0, -ENOTSUPP (-524)
 *************************************************************************/
static int __init l3gd20_init(void)
{
	int ret;

	ret = 0;

	/* Register I2C driver */
	if (i2c_add_driver(&l3gd20_i2c_driver) < 0) {
		l3gd20_log(" Register I2C driver failed\n");
		goto error_i2c_add_driver;
	}

	l3gd20_log("Init successfully !\n");
	return 0;

error_i2c_add_driver:
	return -ENOTSUPP;
}


/*************************************************************************
 *	name	=	l3gd20_exit
 *	func	=	Remove I2C slave device of Gyroscope device driver
 *	input	=	None
 *	output	=	None
 *	return	=	None
 *************************************************************************/
static void __exit l3gd20_exit(void)
{
	/* Remove I2C driver */
	i2c_del_driver(&l3gd20_i2c_driver);

	if (l3gd20_client != NULL) {
		i2c_unregister_device(l3gd20_client);
		l3gd20_client = NULL;
	}

	l3gd20_log("Exit successfully !\n");
}



module_init(l3gd20_init);
module_exit(l3gd20_exit);


MODULE_AUTHOR("Renesas");
MODULE_DESCRIPTION("Gyroscope Sensor driver for l3gd20 chip");
MODULE_LICENSE("GPL v2");
