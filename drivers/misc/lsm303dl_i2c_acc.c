/* drivers/misc/lsm303dl_i2c_acc.c
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
#include <linux/module.h>
#include <linux/wakelock.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <mach/irqs.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/lsm303dl.h>
#include "lsm303dl_local.h"

/************************************************
*	Global variable definition section		*
************************************************/
static struct lsm303dl_acc_data *lsm303dl_acc_info;
static struct i2c_client *acc_client;

#if defined RUNTIME_PM
static struct regulator *accm_regltr_18v;
static struct regulator *accm_regltr_3v;
#endif
#define LSM303D_SW_CALIBRATION

#ifdef LSM303D_SW_CALIBRATION
static int lsm303d_offset[3];
#endif

/* Hard-coded GPIO removal for accelerometer */
static struct lsm303dl_acc_port_info *pinfo;
/*Output data rate looked-up table*/
static const struct lsm303dl_output_rate odr_table_acc[] = {
	{	1,		ACC_ODR_5376		},
	{	3,		ACC_ODR_400		},
	{	5,		ACC_ODR_200		},
	{	10,		ACC_ODR_100		},
	{	20,		ACC_ODR_50		},
	{	40,		ACC_ODR_25		},
	{	100,		ACC_ODR_10		},
	{	1000,		ACC_ODR_1		},
};

/*Sensitivity unit looked-up table*/
static const struct lsm303dl_sens_table sens_table_acc[] = {
	{	1,			1,		1	},
	{	2,			2,		2	},
	{	4,			4,		4	},
	{	12,			12,		12	}
};

/*Threshold LSB value*/
static const u8 thres_lsb_val[] = {
	16,
	31,
	63,
	125
};

#ifdef CONFIG_OF
static const struct of_device_id lsm303dl_acc_of_match[] = {
	{.compatible = "st,lsm303dl-acc",},
	{},
};
MODULE_DEVICE_TABLE(of, lsm303dl_acc_of_match);
#endif

/*Default setting for accelerometer*/
static const u8 lsm303dl_acc_setting_default[MAX_SETTING_ACC] = {
	HPF_RESET_READ_FILTER,		/*High pass filter*/
	ACC_SENSITIVITY_EXTREME,	/*Sensitivity*/
	ACC_ENABLE			/*High resolution output*/
};

/*************************************************************************
 *	name	=	lsm303dl_acc_i2c_read
 *	func	=	Read data from specified register of accelerometer
 *	input	=	u8 reg, u8 *val, int len
 *	output	=	u8 *val
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
static int lsm303dl_acc_i2c_read(u8 reg, u8 *val, int len)
{
	int			ret		= 0;
	int			tries		= 0;
	u8				reg_addr	= 0;

	struct i2c_msg	msg[2];

	/*Check input value*/
	if ((NULL == val) || (NULL == acc_client)) {
		lsm303dl_err("Read buffer is NULL\n");
		return -EINVAL;
	}

	if (len > 1)
		reg_addr = reg | ACC_I2C_AUTO_INCREMENT;
	else
		reg_addr = reg;


	/*Initialization for all elements of i2c_msg structure*/
	memset(msg, 0, sizeof(msg));

	msg[0].addr = acc_client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = (u8 *)&reg_addr;

	msg[1].addr = acc_client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = val;

	do {
		ret = i2c_transfer(acc_client->adapter, msg, 2);
		if (ret != 2)
			msleep_interruptible(LSM303DL_I2C_RETRY_DELAY);

	} while ((ret != 2) && (++tries < LSM303DL_I2C_RETRIES));

	if (ret != 2) {
		lsm303dl_err("Read transfer error\n");
		ret = -EIO;
	} else {
		ret = 0;
	}

	return ret;
}

/*************************************************************************
 *	name	=	lsm303dl_acc_i2c_write
 *	func	=	Write data to specified register of accelerometer
 *	input	=	u8 reg, u8 *val, int len
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
static int lsm303dl_acc_i2c_write(u8 reg, u8 *val, int len)
{
	int			ret = 0;
	int			tries = 0;
	struct i2c_msg		msg;
	u8			*data;

	/*Check input value*/
	if ((NULL == val) || (NULL == acc_client)) {
		lsm303dl_err("Write buffer is NULL\n");
		return -EINVAL;
	}

	/*Allocate internal buffer*/
	data = kzalloc(len + 1, GFP_KERNEL);
	if (NULL == data) {
		lsm303dl_err("Allocate internal buffer error\n");
		return -EIO;
	}

	/*Initialization for all elements of i2c_msg structure*/
	memset(&msg, 0, sizeof(msg));

	if (len > 1)
		data[0] = reg | ACC_I2C_AUTO_INCREMENT;
	else
		data[0] = reg;

	memcpy((void *)&data[1], (void *)val, len);

	msg.addr = acc_client->addr;
	msg.flags = 0;
	msg.len = len + 1;
	msg.buf = data;

	/*Write data to a specific register address of
			accelerometer hardware via I2C bus*/
	do {
		ret = i2c_transfer(acc_client->adapter, &msg, 1);
		if (ret != 1)
			msleep_interruptible(LSM303DL_I2C_RETRY_DELAY);
	} while ((ret != 1) && (++tries < LSM303DL_I2C_RETRIES));

	if (ret != 1) {
		lsm303dl_err("Write transfer error\n");
		ret = -EIO;
	} else {
		ret = 0;
	}

	kfree(data);
	return ret;
}

/*************************************************************************
 *	name	=	lsm303dl_acc_set_hpf
 *	func	=	Change Accelerometer high pass filter status
 *	input	=	u8 val
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
static int lsm303dl_acc_set_hpf(u8 val)
{
	int	ret				= 0;
	u8		reg_value			= 0;
	u8		current_setting		= 0;

	if ((val < HPF_RESET_READ_FILTER) ||
			(val > HPF_AUTORESET_ON_INTERRUPT)) {
		lsm303dl_err("Invalid input argument\n");
		return -EINVAL;
	}

	/*Read the content of CTRL_REG2_A register*/
	ret = lsm303dl_acc_i2c_read(CTRL_REG2_A, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to read data from Accelerometer\n");
		return ret;
	}

	current_setting = (reg_value >> 6) & 0x03;
	if (val == current_setting)
		return 0;


	reg_value = (reg_value & 0x3F) | ((val << 6) & 0xC0);

	/*Write new value to CTRL_REG2_A register*/
	ret = lsm303dl_acc_i2c_write(CTRL_REG2_A, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to write data to Accelerometer\n");
		return ret;
	}

	return 0;
}

/*************************************************************************
 *	name	=	lsm303dl_acc_set_high_res
 *	func	=	Enable/Disable Accelerometer high resolution output
 *	input	=	u8 val
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
static int lsm303dl_acc_set_high_res(u8 val)
{
	int		ret			= 0;
	u8		reg_value		= 0;
	u8		current_setting		= 0;

	if ((val != ACC_ENABLE) && (val != ACC_DISABLE)) {
		lsm303dl_err("Invalid input argument (val = %d)\n", val);
		return -EINVAL;
	}

	/*Read the content of CTRL_REG4_A register*/
	ret = lsm303dl_acc_i2c_read(CTRL_REG4_A, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to read data from Accelerometer\n");
		return ret;
	}

	current_setting = (reg_value >> 3) & 0x01;
	if (val == current_setting)
		return 0;

	reg_value = (reg_value & 0xF7) | ((val << 3) & 0x08);

	/*Write new value to CTRL_REG4_A register*/
	ret = lsm303dl_acc_i2c_write(CTRL_REG4_A, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to write data to Accelerometer\n");
		return ret;
	}

	return 0;
}

/*************************************************************************
 *	name	=	lsm303dl_acc_set_sensitivity
 *	func	=	Change accelerometer sensitivity
 *	input	=	u8 val
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
static int lsm303dl_acc_set_sensitivity(u8 val)
{
	int	ret				= 0;
	u8		reg_value			= 0;
	u8		current_setting		= 0;

	if ((val < ACC_SENSITIVITY_EXTREME) || (val > ACC_SENSITIVITY_LOW)) {
		lsm303dl_err("Invalid input argument\n");
		return -EINVAL;
	}

	/*Read the content of CTRL_REG4_A register*/
	ret = lsm303dl_acc_i2c_read(CTRL_REG4_A, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to read data from Accelerometer\n");
		return ret;
	}

	current_setting = (reg_value >> 4) & 0x03;
	if (val == current_setting) {
		lsm303dl_acc_info->sensitivity = val;
		return 0;
	}

	reg_value = (reg_value & 0xCF) | ((val << 4) & 0x30);

	/*Write new value to CTRL_REG4_A register*/
	ret = lsm303dl_acc_i2c_write(CTRL_REG4_A, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to write data to Accelerometer\n");
		return ret;
	}

	/*Save sensitivity level to global variable for further usage*/
	lsm303dl_acc_info->sensitivity = val;

#ifdef ACCEL_MAG_INTERRUPT_ENABLED
		/*INT1_THS_A*/
		reg_value = thres_lsb_val[val];
		ret = lsm303dl_acc_i2c_write(INT1_THS_A, &reg_value, 1);
		if (ret < 0) {
			lsm303dl_err("Cannot configure for INT1_THS_A reg\n");
			return ret;
		}
#endif

	return 0;
}

/*************************************************************************
 *	name	=	lsm303dl_acc_power_status
 *	func	=	Power up/down accelerometer
 *	input	=	u8 val
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
static int lsm303dl_acc_power_status(u8 val)
{
	int	ret				= 0;
	u8		reg_value			= 0;
	u8		current_setting		= 0;
	u8		current_odr			= 0;

	if ((val != ACC_NORMAL) && (val != ACC_STANDBY)) {
		lsm303dl_err("Invalid input argument\n");
		return -EINVAL;
	}

	/*Read the content of CTRL_REG1_A register*/
	ret = lsm303dl_acc_i2c_read(CTRL_REG1_A, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to read data from Accelerometer\n");
		return ret;
	}

	current_setting = (reg_value >> 3) & 0x01;
	if (val == current_setting)
		return 0;

	if (ACC_STANDBY == val) {
		/*Get odr from "read buffer"*/
		current_odr = (reg_value >> 4) & 0x0F;
		reg_value = 0x08;
	} else {
		reg_value = 0x07 | (lsm303dl_acc_info->odr << 4);
	}

	/*Write new value to CTRL_REG1_A register*/
	ret = lsm303dl_acc_i2c_write(CTRL_REG1_A, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to write data to Accelerometer\n");
		return ret;
	}

	if (ACC_STANDBY == val)
		lsm303dl_acc_info->odr = current_odr;

	return 0;
}

/*************************************************************************
 *	name	=	lsm303dl_acc_set_odr
 *	func	=	Change accelerometer output data rate
 *	input	=	u32 val
 *	output	=	None
 *	return	=	0, -EIO
 *************************************************************************/
int lsm303dl_acc_set_odr(u32 val)
{
	int	i				= 0;
	int	ret				= 0;
	/*u8		activation_flg		= 0;*/
	u8		reg_value		= 0;
	u8		odr			= 0;

	if (NULL == lsm303dl_acc_info)	{
		lsm303dl_err("Invalid input argument\n");
		return -EINVAL;
	}

	if (lsm303dl_acc_info->suspend_flag) {
		lsm303dl_log("\n suspend_flag - lsm303dl_acc_set_odr");
		return 0;
	}
	lsm303dl_log("\n %s and val = %d", __func__, val);


	/*Power down Accelerometer*/
	ret = lsm303dl_acc_power_status(ACC_STANDBY);
	if (ret < 0) {
		lsm303dl_err("Fail to power down Accel\n");
		return -EIO;
	}

	/*Find the output data rate in look-up table*/
	for (i = ARRAY_SIZE(odr_table_acc) - 1; i > 0; i--) {
		if (odr_table_acc[i].poll_rate_ms <= val)
			break;
	}

	odr = odr_table_acc[i].mask;

	if (val > LSM303DL_POLL_THR)
		odr = ACC_ODR_5376;

	/*Read the content of CTRL_REG1_A register*/
	ret = lsm303dl_acc_i2c_read(CTRL_REG1_A, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to read data from Accelerometer\n");
		return -EIO;
	}

#ifdef ACCEL_MAG_INTERRUPT_ENABLED
	odr = ACC_ODR_100;
#endif
	reg_value = (reg_value & 0x0F) | ((odr << 4) & 0xF0);

	/*Write new value to CTRL_REG1_A register*/
	ret = lsm303dl_acc_i2c_write(CTRL_REG1_A, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to write data to Accelerometer\n");
		return -EIO;
	}

	/*Update output data rate to global variable*/
	lsm303dl_acc_info->odr = odr;

	/*Power on Accelerometer*/
	ret = lsm303dl_acc_power_status(ACC_NORMAL);

	if (ret < 0) {
		lsm303dl_err("Fail to power on Accel\n");
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(lsm303dl_acc_set_odr);

/*************************************************************************
 *	name	=	lsm303dl_acc_activate
 *	func	=	Activate/Deactivate Accelerometer
 *	input	=	u8 val
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
int lsm303dl_acc_activate(u8 val)
{
	int	ret			= 0;

	if ((val != ACC_ENABLE) && (val != ACC_DISABLE))
		return -EINVAL;

	if (NULL == lsm303dl_acc_info) {
		lsm303dl_err("Invalid input argument\n");
		return -EINVAL;
	}

	if (lsm303dl_acc_info->suspend_flag) {
		lsm303dl_log("\n suspend_flag - lsm303dl_acc_activate");
		return 0;
	}

	if (ACC_ENABLE == val) {

		/*Power on Accelerometer*/
		ret = lsm303dl_acc_power_status(ACC_NORMAL);

		return ret;
	} else {

		/*Power down Accelerometer*/
		ret = lsm303dl_acc_power_status(ACC_STANDBY);

		return ret;
	}
}
EXPORT_SYMBOL(lsm303dl_acc_activate);

/*************************************************************************
 *	name	=	lsm303dl_acc_hw_init
 *	func	=	Initialize hardware setting for accelerometer
 *	input	=	u8 *val
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
int lsm303dl_acc_hw_init(u8 *val)
{
	int ret = 0;

	if ((NULL == val) | (NULL == lsm303dl_acc_info)) {
		lsm303dl_err("Input value is NULL\n");
		return -EINVAL;
	}

	/*Set high pass filter*/
	ret = lsm303dl_acc_set_hpf(val[0]);
	if (ret < 0) {
		lsm303dl_err("Fail to set high pass filter for Accelerom\n");
		return ret;
	}

	/*Set sensitivity*/
	ret = lsm303dl_acc_set_sensitivity(val[1]);
	if (ret < 0) {
		lsm303dl_err("Fail to set sensitivity for Accelerometer\n");
		return ret;
	}

	/*Set high resolution output mode*/
	ret = lsm303dl_acc_set_high_res(val[2]);
	if (ret < 0) {
		lsm303dl_err("Fail to set high resolution output mode Accel\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(lsm303dl_acc_hw_init);


#ifdef LSM303D_SW_CALIBRATION
static ssize_t lsm303d_get_offset(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%d, %d, %d\n", lsm303d_offset[0],
			lsm303d_offset[1], lsm303d_offset[2]);
}

static ssize_t lsm303d_set_offset(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int x, y, z;
	int err = -EINVAL;
	err = sscanf(buf, "%d %d %d", &x, &y, &z);
	if (err != 3) {
		pr_err("invalid parameter number: %d\n", err);
		return err;
	}
	lsm303d_offset[0] = x;
	lsm303d_offset[1] = y;
	lsm303d_offset[2] = z;
	return count;
}
static DEVICE_ATTR(offset, 0664, lsm303d_get_offset, lsm303d_set_offset);

static struct attribute *lsm303d_attributes[] = {
	&dev_attr_offset.attr,
	NULL
};

static struct attribute_group lsm303d_attr_swcal_grp = {
	.attrs = lsm303d_attributes,
};
#endif
/*************************************************************************
 *	name	=	lsm303dl_acc_get_data
 *	func	=	Get accelerometer values from accelerometer device
 *	input	=	None
 *	output	=	s16 *data
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
int lsm303dl_acc_get_data(s16 *data)
{
	int	ret			= 0;
	u8		reg_value[6]	= { 0 };
	s32		hw_data[3]	= { 0 };
	u8		idx		= 0;

	if (NULL == lsm303dl_acc_info) {
		lsm303dl_err("Invalid input argument\n");
		return -EINVAL;
	}

	idx = lsm303dl_acc_info->sensitivity;

	wake_lock(&lsm303dl_acc_info->wakelock);

	if (NULL == data) {
		lsm303dl_err("Input value is NULL\n");
		ret = -EINVAL;
		goto err;
	}

	/*Get x, y and z axis value from Accelerometer*/
	ret = lsm303dl_acc_i2c_read(OUT_X_L_A, reg_value, 6);
	if (ret < 0) {
		lsm303dl_err("Fail to read data from Accelerometer\n");
		goto err;
	}

	/*Calculate the actual value for accelerometer*/
	hw_data[0] = (((s16) ((reg_value[1] << 8) | reg_value[0])) >> 4);
	hw_data[1] = (((s16) ((reg_value[3] << 8) | reg_value[2])) >> 4);
	hw_data[2] = (((s16) ((reg_value[5] << 8) | reg_value[4])) >> 4);

	lsm303dl_log("\n Final Accel reg values \tx = %d\t y = %d\t z = %d",
			hw_data[0], hw_data[1], hw_data[2]);
	/*Adjust x, y, z-axis value based on sensitivity*/
	hw_data[0] = hw_data[0] * sens_table_acc[idx].x;
	hw_data[1] = hw_data[1] * sens_table_acc[idx].y;
	hw_data[2] = hw_data[2] * sens_table_acc[idx].z;

	data[0] = hw_data[0] * lsm303dl_acc_info->orientation[0] +
		hw_data[1] * lsm303dl_acc_info->orientation[3] +
		hw_data[2] * lsm303dl_acc_info->orientation[6];

	data[1] = hw_data[0] * lsm303dl_acc_info->orientation[1] +
		hw_data[1] * lsm303dl_acc_info->orientation[4] +
		hw_data[2] * lsm303dl_acc_info->orientation[7];

	data[2] = hw_data[0] * lsm303dl_acc_info->orientation[2] +
		hw_data[1] * lsm303dl_acc_info->orientation[5] +
		hw_data[2] * lsm303dl_acc_info->orientation[8];

#ifdef LSM303D_SW_CALIBRATION
	data[0] -= lsm303d_offset[0];
	data[1] -= lsm303d_offset[1];
	data[2] -= lsm303d_offset[2];
#endif

err:
	wake_unlock(&lsm303dl_acc_info->wakelock);
	return ret;
}
EXPORT_SYMBOL(lsm303dl_acc_get_data);

/*************************************************************************
 *	name	=	lsm303dl_acc_isr
 *	func	=	Handler for INT1 interrupt
 *	input	=	int irq, void *dev
 *	output	=	None
 *	return	=	IRQ_HANDLED
 *************************************************************************/
#ifdef ACCEL_MAG_INTERRUPT_ENABLED
static irqreturn_t lsm303dl_acc_isr(int irq, void *dev)
{
	if (irq != -1) {
		disable_irq_nosync(irq);
		queue_delayed_work(lsm303dl_acc_info->irq1_workqueue,
					&lsm303dl_acc_info->irq1_work, 50);
		enable_irq(lsm303dl_acc_info->irq1);
	}
	return IRQ_HANDLED;
}

/*************************************************************************
 *	name	=	lsm303dl_acc_irq_work_func
 *	func	=	Work function for getting accelerometer data and
 *				reporting them to HAL when INT1 interrupt occurs
 *	input	=	struct work_struct *work
 *	output	=	None
 *	return	=	None
 *************************************************************************/
static void lsm303dl_acc_irq_work_func(struct work_struct *work)
{
	u8 reg_value = 0;
	mutex_lock(&lsm303dl_acc_info->lock);

	lsm303dl_acc_i2c_read(INT1_SRC_A, &reg_value, 1);

	/*Get x, y, z axis value and report to HAL layer*/
	lsm303dl_acc_report_values();
	lsm303dl_mag_report_values();
	mutex_unlock(&lsm303dl_acc_info->lock);
}
#endif

/*************************************************************************
 *   name    =       lsm303dl_acc_power_on_off
 *   func    =       switch on/off the accm_regltr_3v
 *   input   =       boolean flag 1 or 0
 *   output  =       None
 *   return  =       0
 ***************************************************************************/
int lsm303dl_acc_power_on_off(bool flag)
{

#ifdef RUNTIME_PM
	int ret;

	if (!accm_regltr_3v) {
		lsm303dl_err("Error: accm_regltr_3v is unavailable\n");
		return -1;
	}

	if ((flag == 1)) {
		lsm303dl_log("\n LDO on %s ", __func__);
		ret = regulator_enable(accm_regltr_3v);
		if (ret)
			lsm303dl_err("Error:Accel 3v regulator enable failed\n");

	} else if ((flag == 0)) {
		lsm303dl_log("\n LDO off %s ", __func__);
		ret = regulator_disable(accm_regltr_3v);
		if (ret)
			lsm303dl_err("Error:Accel 3v Regulator diable failed\n");
	}
#endif
	return 0;
}
EXPORT_SYMBOL(lsm303dl_acc_power_on_off);


/*************************************************************************
 *   name    =       lsm303dl_acc_cs_power_on_off
 *   func    =       switch on/off the accm_regltr_18v
 *   input   =       boolean flag 1 or 0
 *   output  =       None
 *   return  =       0
 ****************************************************************************/
int lsm303dl_acc_cs_power_on_off(bool flag)
{
#ifdef RUNTIME_PM
	int ret;

	if (!accm_regltr_18v) {
		lsm303dl_err("Error: accm_regltr_18v is unavailable\n");
		return -1;
	}

	if ((flag == 1)) {
		lsm303dl_log("\n LDO on %s ", __func__);
		ret = regulator_enable(accm_regltr_18v);
		if (ret)
			lsm303dl_err("Error:Accel 1v8 regulator enable failed\n");
	} else if ((flag == 0)) {
		lsm303dl_log("\n LDO on %s ", __func__);
		ret = regulator_disable(accm_regltr_18v);
		if (ret)
			lsm303dl_err("Error:Accel 1v8 regulator disable failed\n");
	}
#endif
	return 0;
}


/*************************************************************************
 *	name	=	lsm303dl_acc_suspend
 *	func	=	Suspend Accelerometer device
 *	input	=	struct device *dev
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static int lsm303dl_acc_suspend(struct device *dev)
{
	lsm303dl_acc_info->suspend_flag = true;
	lsm303dl_acc_power_on_off(false);
	return 0;
}

/*************************************************************************
 *	name	=	lsm303dl_acc_resume
 *	func	=	Resume Accelerometer device
 *	input	=	struct device *dev
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static int lsm303dl_acc_resume(struct device *dev)
{
	lsm303dl_log("lsm303dl_acc_resume is called\n");
	lsm303dl_acc_power_on_off(true);

	mutex_lock(&lsm303dl_acc_info->lock);
	lsm303dl_acc_activate(ACC_ENABLE);

	mutex_unlock(&lsm303dl_acc_info->lock);
	lsm303dl_acc_info->suspend_flag = false;
	return 0;
}

/**************************************************************************
 *	name	=	lsm303dl_acc_int_status
 *	func	=	Status of Interrupt after Suspend/ Resume
 *	input	=	val
 *	output	=	none
 *************************************************************************/
#ifdef ACCEL_MAG_INTERRUPT_ENABLED
void lsm303dl_acc_int_status(u8 val)
{
	if (val) {
		enable_irq(lsm303dl_acc_info->irq1);
		queue_delayed_work(lsm303dl_acc_info->irq1_workqueue,
					&lsm303dl_acc_info->irq1_work, 50);
	} else {
		disable_irq(lsm303dl_acc_info->irq1);
		if (flush_delayed_work(&lsm303dl_acc_info->irq1_work))
			lsm303dl_log("flush work waited");
	}
}
EXPORT_SYMBOL(lsm303dl_acc_int_status);
#endif

/*************************************************************************
 *	name	=	lsm303dl_acc_i2c_probe
 *	func	=	Probe I2C slave device of accelerometer
 *	input	=	struct i2c_client *client, const struct i2c_device_id
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static int lsm303dl_acc_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *devid)
{
	int	ret = 0;
	int	i = 0;
	u8	reg_default[5] = {0};
	signed char orientation[9];
	struct device_node *np;
	const struct of_device_id *match = NULL;
	int irq = 0;

	lsm303dl_log("lsm303dl_acc_i2c_probe is called\n");
#ifdef LSM303D_SW_CALIBRATION
	lsm303d_offset[0] = 0;
	lsm303d_offset[1] = 0;
	lsm303d_offset[2] = 0;
#endif

#ifdef RUNTIME_PM
	lsm303dl_log("%s: Accelrometer - Setting up regulators\n", __func__);
	accm_regltr_18v = regulator_get(NULL, "sensor_1v8");
	if (IS_ERR_OR_NULL(accm_regltr_18v)) {
		lsm303dl_err("Failed to acquire 1.8V regulator");
		return -EBUSY;
	}
	accm_regltr_3v = regulator_get(NULL, "sensor_3v");
	if (IS_ERR_OR_NULL(accm_regltr_3v)) {
		lsm303dl_err("Failed to acquire 3V regulator");
		ret =  -EBUSY;
		goto err_request_regulator;
	}

	regulator_set_voltage(accm_regltr_18v, 1800000, 1800000);
	regulator_set_voltage(accm_regltr_3v, 3000000, 3000000);
	ret = regulator_enable(accm_regltr_18v);
	if (ret)
		lsm303dl_err("Error:Accel 1v8 Regulator Enable failed\n");

	ret = regulator_enable(accm_regltr_3v);
	if (ret)
		lsm303dl_err("Error:Accel 3v Regulator Enable failed\n");
#endif
/* PM runtime regulator setup*/
#ifdef CONFIG_OF
	match = of_match_device(lsm303dl_acc_of_match, &client->dev);
	if (!match) {
		lsm303dl_err("\n no match device found for I2C ACCEL");
		return -EINVAL;
	}
#endif
	pinfo = kzalloc(sizeof(struct lsm303dl_acc_port_info),
						GFP_KERNEL);
	if (NULL == pinfo) {
		lsm303dl_err("\nCan't allocate memmory for lsm303dl_acc_port_info struct\n");
		return -ENOMEM;
	}

	if (!match && client->dev.platform_data) {
		pinfo = client->dev.platform_data;
		irq = client->irq;
	} else if (match && !client->dev.platform_data) {
		np = client->dev.of_node;
		pinfo->lsm303dl_acc_port = of_get_named_gpio(np,
					"lsm303dl_acc-gpio", 0);
		irq = gpio_to_irq(pinfo->lsm303dl_acc_port);
		if (of_property_read_u8_array(np, "orientation",
					orientation, sizeof(orientation)))
			return -EINVAL;
	}

# ifdef ACCEL_MAG_INTERRUPT_ENABLED
	/* Hard-coded GPIO removal for accelerometer */
	ret = gpio_request(pinfo->lsm303dl_acc_port, NULL);
	if (ret < 0) {
		lsm303dl_err("Can't request GPIO_PORT110 for Accelerometer\n");
		ret = -ENOTSUPP;
	}

	/* Set direction for GPIO_PORT110 */
	/* Hard-coded GPIO removal for accelerometer */
	ret = gpio_direction_input(pinfo->lsm303dl_acc_port);
	if (ret < 0) {
		lsm303dl_err("Can't set direction of GPIO_PORT110 for Accel\n");
		ret = -ENOTSUPP;
		goto handle_gpio;
	}
#endif

	/*Check functionalities of I2C adapter*/
	ret = i2c_check_functionality(client->adapter, I2C_FUNC_I2C);
	if (ret == 0) {
		lsm303dl_err("Accelerometer I2C is malfunction\n");
		return -EIO;
	}

	/*Allocate memory for lsm303dl_acc_data structure*/
	lsm303dl_acc_info = kzalloc(sizeof(struct lsm303dl_acc_data),
						GFP_KERNEL);
	if (NULL == lsm303dl_acc_info) {
		lsm303dl_err("Can't allocate memmory for lsm303dl_acc_data struct\n");
		return -ENOMEM;
	}

	/*Initialize values for lsm303dl_acc_data structure*/
	lsm303dl_acc_info->sensitivity	= 0;
	lsm303dl_acc_info->odr		= 2;
	lsm303dl_acc_info->suspend_flag = false;
	lsm303dl_acc_info->delay	= 100;
	lsm303dl_acc_info->poll_interval = 100;
	lsm303dl_acc_info->report_ignore_cnt = 0;
	for (i = 0; i < 9; i++)
		lsm303dl_acc_info->orientation[i] = orientation[i];

	acc_client = client;

	/*Initialize mutex and wake lock*/
	mutex_init(&lsm303dl_acc_info->lock);
	wake_lock_init(&lsm303dl_acc_info->wakelock,
			WAKE_LOCK_SUSPEND, "lsm303dl-acc-wakelock");

	mutex_lock(&lsm303dl_acc_info->lock);

	/*Initialize Accelerometer fixed setting*/
	/*CTRL_REG1_A: ODR: 100 ms, 10Hz low power and disable xyz axis*/
	reg_default[0] = 0x27;
	ret = lsm303dl_acc_i2c_write(CTRL_REG1_A, reg_default, 1);
	if (ret < 0) {
		lsm303dl_err("Cannot configure for CTRL_REG1_A\n");
		ret = -EIO;
		goto hw_init_err;
	}

	/*FIFO_CTRL_REG_A*/
	reg_default[0] = 0x3F;
	ret = lsm303dl_acc_i2c_write(FIFO_CTRL_REG_A, reg_default, 1);
	if (ret < 0) {
		lsm303dl_err("Cannot configure for FIFO_CTRL_REG_A\n");
		ret = -EIO;
		goto hw_init_err;
	}

#ifdef ACCEL_MAG_INTERRUPT_ENABLED
		reg_default[0] = 0x00; /*CTRL_REG2_A*/
		reg_default[1] = 0x10; /*CTRL_REG3_A*/
		reg_default[2] = 0x08; /*CTRL_REG4_A*/
		reg_default[3] = 0x00; /*CTRL_REG5_A*/
		reg_default[4] = 0x02; /*CTRL_REG6_A*/

		ret = lsm303dl_acc_i2c_write(CTRL_REG2_A, reg_default, 5);
		if (ret < 0) {
			lsm303dl_err("Can't configure for CTRL_REG2_A->CTRL_REG6_A reg\n");
			ret = -EIO;
			goto hw_init_err;
		}

		reg_default[0] = 0x2A; /*INT1_CFG_A*/
		ret = lsm303dl_acc_i2c_write(INT1_CFG_A, reg_default, 1);
		if (ret < 0) {
			lsm303dl_err("Can't configure for INT1_CFG_A reg\n");
			ret = -EIO;
			goto hw_init_err;
		}

		reg_default[0] = thres_lsb_val[lsm303dl_acc_info->sensitivity];
		reg_default[1] = 0x30; /*INT1_DURATION_A*/
		ret = lsm303dl_acc_i2c_write(INT1_THS_A, reg_default, 2);
		if (ret < 0) {
			lsm303dl_err("Can't configure for INT1_THS_A & INT1_DURATION_A\n");
			ret = -EIO;
			goto hw_init_err;
		}
#endif

	/*Initialize Accelerometer default setting*/
	ret = lsm303dl_acc_hw_init((u8 *)&lsm303dl_acc_setting_default[0]);
	if (ret < 0) {
		lsm303dl_err("Can't initialize Accelerometer default setting\n");
		ret = -EIO;
		goto hw_init_err;
	}

#ifdef ACCEL_MAG_INTERRUPT_ENABLED

		/*Create work queue for handling bottom-half interrupt*/
		lsm303dl_acc_info->irq1_workqueue =
			create_singlethread_workqueue("lsm303dl_acc_wq");
		if (NULL == lsm303dl_acc_info->irq1_workqueue) {
			lsm303dl_err("Can't create work queue struct for Accel\n");
			ret = -ENOMEM;
			goto hw_init_err;
		}

		INIT_DELAYED_WORK(&lsm303dl_acc_info->irq1_work,
			lsm303dl_acc_irq_work_func);

		/*Register Accelerometer interrupt handler*/
		ret = request_threaded_irq(irq, NULL, lsm303dl_acc_isr,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"lsm303dl_acc_int", lsm303dl_acc_info);
		if (ret < 0) {
			lsm303dl_err("Can't register Accel interrupt handler\n");
			ret = -EIO;
			goto req_irq_err;
		}
		lsm303dl_acc_info->irq1 = irq;
#endif

	/*Change to standby mode*/
	lsm303dl_acc_power_status(ACC_STANDBY);
	mutex_unlock(&lsm303dl_acc_info->lock);
#ifdef LSM303D_SW_CALIBRATION
	ret = sysfs_create_group(&client->dev.kobj, &lsm303d_attr_swcal_grp);
	if (ret != 0)
		goto probe_err_create_sysfs;
#endif

	return 0;
#ifdef LSM303D_SW_CALIBRATION
probe_err_create_sysfs:
	sysfs_remove_group(&client->dev.kobj,
			&lsm303d_attr_swcal_grp);
#endif
#ifdef ACCEL_MAG_INTERRUPT_ENABLED
req_irq_err:
	destroy_workqueue(lsm303dl_acc_info->irq1_workqueue);

handle_gpio:
	/* Hard-coded GPIO removal for accelerometer */
	gpio_free(pinfo->lsm303dl_acc_port);
#endif

hw_init_err:
	mutex_unlock(&lsm303dl_acc_info->lock);
	wake_lock_destroy(&lsm303dl_acc_info->wakelock);

	kfree(lsm303dl_acc_info);
	lsm303dl_acc_info = NULL;
	kfree(pinfo);

#ifdef RUNTIME_PM
	if (accm_regltr_3v) {
		lsm303dl_acc_power_on_off(false);
		regulator_put(accm_regltr_3v);
	}
err_request_regulator:
	if (accm_regltr_18v) {
		lsm303dl_acc_cs_power_on_off(false);
		regulator_put(accm_regltr_18v);
	}
#endif

	return ret;
}

/*************************************************************************
 *	name	=	lsm303dl_acc_i2c_remove
 *	func	=	Remove I2C slave device of accelerometer
 *	input	=	struct i2c_client *client
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static int lsm303dl_acc_i2c_remove(struct i2c_client *client)
{
#ifdef LSM303D_SW_CALIBRATION
	sysfs_remove_group(&client->dev.kobj, &lsm303d_attr_swcal_grp);
#endif

#ifdef ACCEL_MAG_INTERRUPT_ENABLED
	destroy_workqueue(lsm303dl_acc_info->irq1_workqueue);
	free_irq(lsm303dl_acc_info->irq1, lsm303dl_acc_info);
#endif
	wake_lock_destroy(&lsm303dl_acc_info->wakelock);
	kfree(lsm303dl_acc_info);
	kfree(pinfo);
	lsm303dl_acc_info = NULL;

#ifdef RUNTIME_PM
	if (accm_regltr_18v) {
		lsm303dl_acc_cs_power_on_off(false);
		regulator_put(accm_regltr_18v);
	}
	if (accm_regltr_3v) {
		lsm303dl_acc_power_on_off(false);
		regulator_put(accm_regltr_3v);
	}
#endif
	return 0;
}

/****************************************************
*	I2C device id structure definition				*
*****************************************************/
static const struct i2c_device_id lsm303dl_acc_id[] = {
	{	LSM303DL_ACC_NAME,		0			},
	{								}
};

/****************************************************
*	Power management structure definition		*
*****************************************************/
static const struct dev_pm_ops lsm303dl_acc_pm_ops = {
	.suspend = lsm303dl_acc_suspend,
	.resume = lsm303dl_acc_resume,
};

MODULE_DEVICE_TABLE(i2c, lsm303dl_acc_id);

/****************************************************
*	I2C driver structure definition				*
*****************************************************/
static struct i2c_driver acc_driver = {
	.probe			= lsm303dl_acc_i2c_probe,
	.remove			= lsm303dl_acc_i2c_remove,
	.id_table		= lsm303dl_acc_id,
	.driver			= {
		.owner		= THIS_MODULE,
		.name		= LSM303DL_ACC_NAME,
		.of_match_table = of_match_ptr(lsm303dl_acc_of_match),
		.pm		= &lsm303dl_acc_pm_ops,
	},
};

/*************************************************************************
 *	name	=	lsm303dl_acc_init
 *	func	=	Initialize I2C slave device of accelerometer
 *	input	=	None
 *	output	=	None
 *	return	=	0, -ENOTSUPP
 *************************************************************************/
static int __init lsm303dl_acc_init(void)
{
	int ret	= 0;
	lsm303dl_log("lsm303dl_acc_init is called\n");

	/*Register accelerometer driver to I2C core*/
	ret = i2c_add_driver(&acc_driver);
	if (ret < 0) {
		lsm303dl_err("Cannot register accelerometer driver to I2C core\n");
		ret = -ENOTSUPP;
		goto handle_error;
	}
	return 0;

handle_error:
	/*Remove accelerometer driver from I2C core*/
	i2c_del_driver(&acc_driver);

#ifdef ACCEL_MAG_INTERRUPT_ENABLED
		/* Hard-coded GPIO removal for accelerometer */
		gpio_free(pinfo->lsm303dl_acc_port);
#endif
	return ret;
}

/*************************************************************************
 *	name	=	lsm303dl_acc_exit
 *	func	=	Finalize I2C slave device of accelerometer
 *	input	=	None
 *	output	=	None
 *	return	=	None
 *************************************************************************/
static void __exit lsm303dl_acc_exit(void)
{
	/*Remove accelerometer driver from I2C core*/
	i2c_del_driver(&acc_driver);

	/*Delete accelerometer client from I2C core*/
	if (acc_client != NULL)
		i2c_unregister_device(acc_client);

#ifdef ACCEL_MAG_INTERRUPT_ENABLED
		/* Hard-coded GPIO removal for accelerometer */
		gpio_free(pinfo->lsm303dl_acc_port);
#endif

}

module_init(lsm303dl_acc_init);
module_exit(lsm303dl_acc_exit);

MODULE_DESCRIPTION("LSM303DL Accelerometer Driver");
MODULE_AUTHOR("Renesas");
MODULE_LICENSE("GPL v2");
