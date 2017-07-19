/* drivers/misc/lsm303dl_i2c_mag.c
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
#include <mach/irqs.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/lsm303dl.h>
#include <linux/of.h>
#include "lsm303dl_local.h"

/************************************************
*	Global variable definition section		*
************************************************/
#define M_MIN	-2048
#define M_MAX	2047
#define SATURATED_CODE -4096

static struct lsm303dl_mag_data *lsm303dl_mag_info;
static struct i2c_client *mag_client;

#ifdef RUNTIME_PM
static struct regulator *mag_regltr_18v;
static struct regulator *mag_regltr_3v;
#endif

/* Global variables for Hard-iron calibration */
struct vector {
	s16 x, y, z;
};
/* maximum magnetometer data used for calibration */
static struct vector m_min = {M_MIN, M_MIN, M_MIN};
/* minimum magnetometer data used for calibration */
static struct vector m_max = {M_MAX, M_MAX, M_MAX};

/*Output data rate looked-up table*/
static const struct lsm303dl_output_rate odr_table_mag[] = {
	{	5,	MAG_ODR_220			},
	{	13,	MAG_ODR_75			},
	{	34,	MAG_ODR_30			},
	{	67,	MAG_ODR_15			},
	{	134,	MAG_ODR_7_5			},
	{	334,	MAG_ODR_3_0			},
	{	667,	MAG_ODR_1_5			},
	{	1334,	MAG_ODR_0_75			},
};

/*Sensitivity unit looked-up table*/
static const struct lsm303dl_sens_table sens_table_mag[] = {
	{	0,		0,		0	},	/*Unused*/
	{	1100,		1100,		980	},
	{	855,		855,		760	},
	{	670,		670,		600	},
	{	450,		450,		400	},
	{	400,		400,		355	},
	{	330,		330,		295	},
	{	230,		230,		205	}
};

#define AVERAGE 5
static s16 past_hw_datax[AVERAGE];
static s16 past_hw_datay[AVERAGE];
static s16 past_hw_dataz[AVERAGE];
static int n_past;

/*************************************************************************
 *	name	=	lsm303dl_mag_i2c_read
 *	func	=	Read data from specified register of magnetometer
 *	input	=	u8 reg, u8 *val, int len
 *	output	=	u8 *val
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
static int lsm303dl_mag_i2c_read(u8 reg, u8 *val, int len)
{
	int			ret = 0;
	int			tries = 0;
	struct i2c_msg	msg[2];

	/*Check input value*/
	if ((NULL == val) || (NULL == mag_client)) {
		lsm303dl_err("Read buffer is NULL\n");
		return -EINVAL;
	}

	/*Initialization for all elements of i2c_msg structure*/
	memset(msg, 0, sizeof(msg));

	msg[0].addr = mag_client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = (u8 *)&reg;

	msg[1].addr = mag_client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = val;

	do {
		ret = i2c_transfer(mag_client->adapter, msg, 2);
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
 *	name	=	lsm303dl_mag_i2c_write
 *	func	=	Write data to specified register of magnetometer
 *	input	=	u8 reg, u8 *val, int len
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
static int lsm303dl_mag_i2c_write(u8 reg, u8 *val, int len)
{
	int			ret = 0;
	int			tries = 0;
	struct i2c_msg	msg;
	u8				*data;

	/*Check input value*/
	if ((NULL == val) || (NULL == mag_client)) {
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

	data[0] = reg;
	memcpy((void *)&data[1], (void *)val, len);

	msg.addr = mag_client->addr;
	msg.flags = 0;
	msg.len = len + 1;
	msg.buf = data;

	/*Write data to a specific register address of
	accelerometer hardware via I2C bus*/
	do {
		ret = i2c_transfer(mag_client->adapter, &msg, 1);
		if (ret != 1)
			msleep_interruptible(LSM303DL_I2C_RETRY_DELAY);
	} while ((ret != 1) && (++tries < LSM303DL_I2C_RETRIES));

	if (ret != 1) {
		lsm303dl_err("Write transfer error\n");
		ret = -EIO;
	} else
		ret = 0;

	kfree(data);
	return ret;
}

/*************************************************************************
 *	name	=	lsm303dl_mag_set_sensitivity
 *	func	=	Change magnetometer sensitivity
 *	input	=	u8 val
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
static int lsm303dl_mag_set_sensitivity(u8 val)
{
	int	ret				= 0;
	u8		reg_value			= 0;
	u8		current_setting		= 0;

	if ((val < MAG_SENS_1_3) || (val > MAG_SENS_8_1)) {
		lsm303dl_err("Invalid input argument\n");
		return -EINVAL;
	}

	/*Read the content of CRB_REG_M register*/
	ret = lsm303dl_mag_i2c_read(CRB_REG_M, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to read data from Magnetometer\n");
		return ret;
	}

	current_setting = (reg_value >> 5) & 0x07;
	if (val == current_setting) {
		lsm303dl_mag_info->sensitivity = val;
		return 0;
	}

	reg_value = (reg_value & 0x1F) | ((val << 5) & 0xE0);

	/*Write new value to CRB_REG_M register*/
	ret = lsm303dl_mag_i2c_write(CRB_REG_M, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to write data to Magnetometer\n");
		return ret;
	}

	/*Save sensitivity level to global variable for further usage*/
	lsm303dl_mag_info->sensitivity = val;

	return 0;
}

/*************************************************************************
 *	name	=	lsm303dl_mag_power_status
 *	func	=	Power up/down magnetometer
 *	input	=	u8 val
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
static int lsm303dl_mag_power_status(u8 val)
{
	int	ret				= 0;
	u8		reg_value			= 0;
	u8		current_setting		= 0;

	if ((val != MAG_NORMAL) && (val != MAG_STANDBY)) {
		lsm303dl_err("Invalid input argument\n");
		return -EINVAL;
	}

	/*Read the content of MR_REG_M register*/
	ret = lsm303dl_mag_i2c_read(MR_REG_M, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to read data from Magnetometer\n");
		return ret;
	}

	current_setting = reg_value & 0x03;
	if (val == current_setting)
		return 0;

	reg_value = (reg_value & 0xFC) | val;

	/*Write new value to MR_REG_M register*/
	ret = lsm303dl_mag_i2c_write(MR_REG_M, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to write data to Magnetometer\n");
		return ret;
	}

	return 0;
}

/*************************************************************************
 *	name	=	lsm303dl_mag_set_odr
 *	func	=	Change magnetometer output data rate
 *	input	=	u32 val
 *	output	=	None
 *	return	=	0, -EIO
 *************************************************************************/
int lsm303dl_mag_set_odr(u32 val)
{
	int	i				= 0;
	int	ret				= 0;
	/*u8		activation_flg		= 0;*/
	u8		reg_value		= 0;
	u8		odr			= 0;

	if (NULL == lsm303dl_mag_info) {
		lsm303dl_err("Invalid input argument\n");
		return -EINVAL;
	}

	if (lsm303dl_mag_info->suspend_flag) {
		lsm303dl_log("\n suspend_flag : lsm303dl_mag_set_odr");
		return 0;
	}

	/*Power down Magnetometer*/
	ret = lsm303dl_mag_power_status(MAG_STANDBY);
	if (ret < 0) {
		lsm303dl_err("Fail to power down Magnetometer\n");
		return -EIO;
	}

	/*Find the output data rate in look-up table*/
	for (i = ARRAY_SIZE(odr_table_mag) - 1; i > 0; i--) {
		if (odr_table_mag[i].poll_rate_ms <= val)
			break;
	}

	odr = odr_table_mag[i].mask;

	if (val > LSM303DL_POLL_THR)
		odr = MAG_ODR_220;

	/*Read the content of CRA_REG_M register*/
	ret = lsm303dl_mag_i2c_read(CRA_REG_M, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to read data from Magnetometer\n");
		return -EIO;
	}

	reg_value = (reg_value & 0xE3) | ((odr << 2) & 0x1C);

	/*Write new value to CRA_REG_M register*/
	ret = lsm303dl_mag_i2c_write(CRA_REG_M, &reg_value, 1);
	if (ret < 0) {
		lsm303dl_err("Fail to write data to Magnetometer\n");
		return -EIO;
	}

	/*Power on Magnetometer*/
	ret = lsm303dl_mag_power_status(MAG_NORMAL);
	if (ret < 0) {
		lsm303dl_err("Fail to power on Magnetometer\n");
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(lsm303dl_mag_set_odr);

/*************************************************************************
 *	name	=	lsm303dl_mag_activate
 *	func	=	Activate/Deactivate Magnetometer
 *	input	=	u8 val
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
int lsm303dl_mag_activate(u8 val)
{
	int	ret			= 0;

	if ((val != MAG_ENABLE) && (val != MAG_DISABLE))
		return -EINVAL;


	if (NULL == lsm303dl_mag_info) {
		lsm303dl_err("Invalid input argument\n");
		return -EINVAL;
	}

	if (lsm303dl_mag_info->suspend_flag) {
		lsm303dl_log("\n suspend_flag : lsm303dl_mag_activate");
		return 0;
	}

	if (MAG_ENABLE == val) {

		/*Power on Magnetometer*/
		ret = lsm303dl_mag_power_status(MAG_NORMAL);
		return ret;
	} else {

		/*Power down Magnetometer*/
		ret = lsm303dl_mag_power_status(MAG_STANDBY);

		return ret;
	}
}
EXPORT_SYMBOL(lsm303dl_mag_activate);

/*************************************************************************
 *	name	=	lsm303dl_mag_hw_init
 *	func	=	Initialize hardware setting for magnetometer
 *	input	=	u8 *val
 *	output	=	None
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
int lsm303dl_mag_hw_init(u8 *val)
{
	int ret = 0;

	if ((NULL == val) | (NULL == lsm303dl_mag_info)) {
		lsm303dl_err("Input value is NULL\n");
		return -EINVAL;
	}

	ret = lsm303dl_mag_set_sensitivity(val[0]);

	return ret;
}
EXPORT_SYMBOL(lsm303dl_mag_hw_init);

/*************************************************************************
 *	name	=	lsm303dl_mag_get_data
 *	func	=	Get magnetometer values from magnetometer device
 *	input	=	None
 *	output	=	s16 *data
 *	return	=	0, -EIO, -EINVAL
 *************************************************************************/
int lsm303dl_mag_get_data(s16 *data)
{
	int	ret			= 0;
	u8		reg_value[6]	= { 0 };
	s32		hw_data[3]	= { 0 };
	u8		idx		= 0;
	struct	vector m;
	int	d_x;
	int	d_y;
	int	d_z;
	int     i = 0;

	if (NULL == lsm303dl_mag_info) {
		lsm303dl_err("Invalid input argument\n");
		return -EINVAL;
	}
	idx = lsm303dl_mag_info->sensitivity;

	wake_lock(&lsm303dl_mag_info->wakelock);

	if (NULL == data) {
		lsm303dl_err("Input value is NULL\n");
		ret = -EINVAL;
		goto err;
	}

	/*Get x, y and z axis value from Magnetometer*/
	ret = lsm303dl_mag_i2c_read(OUT_X_H_M, reg_value, 6);
	if (ret < 0) {
		lsm303dl_err("Fail to read data from Magnetometer\n");
		goto err;
	}

	/*Calculate the actual value for magnetometer*/
	hw_data[0] = (int) (((reg_value[0]) << 8) | reg_value[1]);
	hw_data[1] = (int) (((reg_value[4]) << 8) | reg_value[5]);
	hw_data[2] = (int) (((reg_value[2]) << 8) | reg_value[3]);
	hw_data[0] = (hw_data[0] & 0x8000) ? (hw_data[0] | 0xFFFF0000)
								: (hw_data[0]);
	hw_data[1] = (hw_data[1] & 0x8000) ? (hw_data[1] | 0xFFFF0000)
								: (hw_data[1]);
	hw_data[2] = (hw_data[2] & 0x8000) ? (hw_data[2] | 0xFFFF0000)
								: (hw_data[2]);

#ifdef MAG_HARD_IRON_CALIBRATION
	/* Hard iron Calibration */
	m.x = hw_data[0];
	m.y = hw_data[1];
	m.z = hw_data[2];

	if ((m_min.x == SATURATED_CODE) ||
		(m_min.y == SATURATED_CODE) ||
		(m_min.z == SATURATED_CODE)) {
		m_min.x = m_min.y = m_min.z = M_MIN;
		m_max.x = m_max.y = m_max.z = M_MAX;
	}

	/* adjusting min(x,y,z) values */
	m_min.x = (m_min.x > m.x) ? m.x : m_min.x;
	m_min.y = (m_min.y > m.y) ? m.y : m_min.y;
	m_min.z = (m_min.z > m.z) ? m.z : m_min.z;

	/* adjusting max(x,y,z) values */
	m_max.x = (m_max.x < m.x) ? m.x : m_max.x;
	m_max.y = (m_max.y < m.y) ? m.y : m_max.y;
	m_max.z = (m_max.z < m.z) ? m.z : m_max.z;

	/* Calc half point x,y,z */
	d_x = m_max.x - ((m_max.x - m_min.x) / 2);
	d_y = m_max.y - ((m_max.y - m_min.y) / 2);
	d_z = m_max.z - ((m_max.z - m_min.z) / 2);

	hw_data[0] = m.x - d_x;
	hw_data[1] = m.y - d_y;
	hw_data[2] = m.z - d_z;
	/* LSM303DLHC_HARD_IRON_CALIB_ENABLE */
#endif

	past_hw_datax[n_past] = hw_data[0];
	past_hw_datay[n_past] = hw_data[1];
	past_hw_dataz[n_past] = hw_data[2];
	n_past++;

	if (n_past >= AVERAGE)
		n_past = 0;

	hw_data[0] = 0;
	hw_data[1] = 0;
	hw_data[2] = 0;

	for (i = 0; i < AVERAGE; i++) {
		hw_data[0] += past_hw_datax[i];
		hw_data[1] += past_hw_datay[i];
		hw_data[2] += past_hw_dataz[i];
	}

	hw_data[0] = hw_data[0]/AVERAGE;
	hw_data[1] = hw_data[1]/AVERAGE;
	hw_data[2] = hw_data[2]/AVERAGE;

	/* multiply the orientation matrix based to align with position */
	data[0] = hw_data[0] * lsm303dl_mag_info->orientation[0] +
		hw_data[1] * lsm303dl_mag_info->orientation[3] +
		hw_data[2] * lsm303dl_mag_info->orientation[6];

	data[1] = hw_data[0] * lsm303dl_mag_info->orientation[1] +
		hw_data[1] * lsm303dl_mag_info->orientation[4] +
		hw_data[2] * lsm303dl_mag_info->orientation[7];

	data[2] = hw_data[0] * lsm303dl_mag_info->orientation[2] +
		hw_data[1] * lsm303dl_mag_info->orientation[5] +
		hw_data[2] * lsm303dl_mag_info->orientation[8];

	/* copy data values back to hw_data */
	hw_data[0] = data[0];
	hw_data[1] = data[1];
	hw_data[2] = data[2];

	/*Adjust x-axis value based on sensitivity*/
	if (hw_data[0] != 0xF000)
		data[0] = (hw_data[0] * 1000) / sens_table_mag[idx].x;
	else
		data[0] = 0x8000;


	/*Adjust y-axis value based on sensitivity*/
	if (hw_data[1] != 0xF000)
		data[1] = (hw_data[1] * 1000) / sens_table_mag[idx].y;
	else
		data[1] = 0x8000;

	/*Adjust z-axis value based on sensitivity*/
	if (hw_data[2] != 0xF000)
		data[2] = (hw_data[2] * 1000) / sens_table_mag[idx].z;
	else
		data[2] = 0x8000;
	lsm303dl_log("\n Final compass reg values: %d\t%d\t%d\t\n", data[0],
				data[1], data[2]);

err:
	wake_unlock(&lsm303dl_mag_info->wakelock);
	return ret;
}
EXPORT_SYMBOL(lsm303dl_mag_get_data);

/*************************************************************************
 *     name    =       lsm303dl_mag_power_on_off
 *     func    =       switch ON/OFF the mag_regltr_3v
 *     input   =       Boolean flag (0/1)
 *     output  =       None
 *     retuen  =       0
 **************************************************************************/
int lsm303dl_mag_power_on_off(bool flag)
{

#ifdef RUNTIME_PM
	int ret;

	if (!mag_regltr_3v) {
		lsm303dl_err("Error: mag_regltr_3v is unavailable\n");
		return -1;
	}

	if ((flag == 1)) {
		lsm303dl_log("\n LDO on %s ", __func__);
		ret = regulator_enable(mag_regltr_3v);
		if (ret)
			lsm303dl_err("Error: Mag 3v Regulator enable failed\n");
	} else if ((flag == 0)) {
		lsm303dl_log("\n LDO off %s ", __func__);
		ret = regulator_disable(mag_regltr_3v);
		if (ret)
			lsm303dl_err("Error: Mag 3v Regulator disable failed\n");
	}
#endif

	return 0;
}
EXPORT_SYMBOL(lsm303dl_mag_power_on_off);

/*************************************************************************
 *      name    =       lsm303dl_mag_cs_power_on_off
 *      func    =       switch ON/OFF the mag_regltr_18v
 *      input   =       Boolean flag (0/1)
 *      output  =       None
 *      retuen  =       0
 * **************************************************************************/
int lsm303dl_mag_cs_power_on_off(bool flag)
{

#ifdef RUNTIME_PM
	int ret;

	if (!mag_regltr_18v) {
		lsm303dl_err("Error: mag_regltr_18v is unavailable\n");
		return -1;
	}

	if ((flag == 1)) {
		lsm303dl_log("\n LDO on %s ", __func__);
		ret = regulator_enable(mag_regltr_18v);
		if (ret)
			lsm303dl_err("Error: Mag 1v8 Regulator enable failed\n");
	} else if ((flag == 0)) {
		lsm303dl_log("\n LDO off %s ", __func__);
		ret = regulator_disable(mag_regltr_18v);
		if (ret)
			lsm303dl_err("Error: Mag 1v8 Regulator disable failed\n");
	}
#endif

	return 0;
}


/*************************************************************************
 *	name	=	lsm303dl_mag_suspend
 *	func	=	Suspend magnetometer device
 *	input	=	struct device *dev
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static int lsm303dl_mag_suspend(struct device *dev)
{
	lsm303dl_mag_info->suspend_flag = true;
	lsm303dl_mag_power_on_off(false);
	return 0;
}

/*************************************************************************
 *	name	=	lsm303dl_mag_resume
 *	func	=	Resume Magnetometer device
 *	input	=	struct device *dev
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static int lsm303dl_mag_resume(struct device *dev)
{
	lsm303dl_mag_power_on_off(true);
	lsm303dl_mag_info->suspend_flag = false;
	return 0;
}

/*************************************************************************
 *	name	=	lsm303dl_mag_i2c_probe
 *	func	=	Probe I2C slave device of magnetometer
 *	input	=	struct i2c_client *client, const struct i2c_device_id
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static int lsm303dl_mag_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *devid)
{
	int	ret = 0;
	struct device_node *np;
	signed char orientation[9];
	int i;
	u8 reg_default[3] = {0};

	lsm303dl_log("lsm303dl_mag_i2c_probe is called\n");

#ifdef RUNTIME_PM

	lsm303dl_log("Magnetometer setting up regulator supplies");
	mag_regltr_18v = regulator_get(NULL, "sensor_1v8");
	if (IS_ERR_OR_NULL(mag_regltr_18v)) {
		lsm303dl_err("%s: Failed to get 1.8V regulator\n", __func__);
		return -EBUSY;
	}
	regulator_set_voltage(mag_regltr_18v, 1800000, 1800000);
	ret = regulator_enable(mag_regltr_18v);
	if (ret)
		lsm303dl_err("Error: Mag 1v8 regulator enable failed\n");

	mag_regltr_3v = regulator_get(NULL, "sensor_3v");
	if (IS_ERR_OR_NULL(mag_regltr_3v)) {
		lsm303dl_err("%s: Failed to get 3V regulator\n", __func__);
		ret = -EBUSY;
		goto err_request_regulator;
	}
	regulator_set_voltage(mag_regltr_3v, 3000000, 3000000);
	ret = regulator_enable(mag_regltr_3v);
	if (ret)
		lsm303dl_err("Error: Mag 3v regulator enable failed\n");
#endif
/* Runtime PM regulator setup */


	/*Check functionalities of I2C adapter*/
	ret = i2c_check_functionality(client->adapter, I2C_FUNC_I2C);
	if (ret == 0) {
		lsm303dl_err("Magnetometer I2C is malfunction\n");
		return -EIO;
	}

	if (!client->dev.platform_data) {
		np = client->dev.of_node;
		if (of_property_read_u8_array(np, "orientation",
				orientation, sizeof(orientation)))
					return -EINVAL;
	}

	/*Allocate memory for lsm303dl_mag_data structure*/
	lsm303dl_mag_info = kzalloc(sizeof(struct lsm303dl_mag_data),
					GFP_KERNEL);
	if (NULL == lsm303dl_mag_info) {
		lsm303dl_err("Can't allocate memmory for lsm303dl_mag_data struct\n");
		return -ENOMEM;
	}

	/*Initialize values for lsm303dl_mag_data structure*/
	lsm303dl_mag_info->sensitivity = 1;
	lsm303dl_mag_info->delay = 100;
	lsm303dl_mag_info->suspend_flag = false;
	lsm303dl_mag_info->poll_interval = 100;
	for (i = 0; i < 9; i++)
		lsm303dl_mag_info->orientation[i] = orientation[i];

	mag_client = client;

	/*Initialize mutex and wake lock*/
	mutex_init(&lsm303dl_mag_info->lock);
	wake_lock_init(&lsm303dl_mag_info->wakelock,
			WAKE_LOCK_SUSPEND, "lsm303dl-mag-wakelock");

	mutex_lock(&lsm303dl_mag_info->lock);

	/*Initialize Magnetometer fixed and default setting*/
	reg_default[0] = 0x90;
	reg_default[1] = 0x20;
	reg_default[2] = 0x03;

	ret = lsm303dl_mag_i2c_write(CRA_REG_M, reg_default,
				sizeof(reg_default)/sizeof(reg_default[0]));
	if (ret < 0) {
		lsm303dl_err("Can't Initialize Mag fixed &default setting\n");
		ret = -EIO;
		goto hw_init_err;
	}

	lsm303dl_mag_power_status(MAG_STANDBY);
	mutex_unlock(&lsm303dl_mag_info->lock);
	return 0;

hw_init_err:
	mutex_unlock(&lsm303dl_mag_info->lock);
	wake_lock_destroy(&lsm303dl_mag_info->wakelock);

	kfree(lsm303dl_mag_info);
	lsm303dl_mag_info = NULL;

#ifdef RUNTIME_PM
	if (mag_regltr_3v) {
		lsm303dl_mag_power_on_off(false);
		regulator_put(mag_regltr_3v);
	}
err_request_regulator:
	if (mag_regltr_18v) {
		lsm303dl_mag_cs_power_on_off(false);
		regulator_put(mag_regltr_18v);
	}
#endif

	return ret;
}

/*************************************************************************
 *	name	=	lsm303dl_mag_i2c_remove
 *	func	=	Remove I2C slave device of magnetometer
 *	input	=	struct i2c_client *client
 *	output	=	None
 *	return	=	0
 *************************************************************************/
static int lsm303dl_mag_i2c_remove(struct i2c_client *client)
{
	wake_lock_destroy(&lsm303dl_mag_info->wakelock);

	/*Release all allocated memory*/
	kfree(lsm303dl_mag_info);
	lsm303dl_mag_info = NULL;

#ifdef RUNTIME_PM
	if (mag_regltr_18v) {
		lsm303dl_mag_cs_power_on_off(false);
		regulator_put(mag_regltr_18v);
	}
	if (mag_regltr_3v) {
		lsm303dl_mag_power_on_off(false);
		regulator_put(mag_regltr_3v);
	}
#endif

	return 0;
}

/****************************************************
*	I2C device id structure definition				*
*****************************************************/
static const struct i2c_device_id lsm303dl_mag_id[] = {
	{	LSM303DL_MAG_NAME,		0			},
	{								}
};

/****************************************************
*	Power management structure definition		*
*****************************************************/
static const struct dev_pm_ops lsm303dl_mag_pm_ops = {
	.suspend = lsm303dl_mag_suspend,
	.resume = lsm303dl_mag_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id lsm303dl_mag_of_match[] = {
	{.compatible = "st,lsm303dl-mag",},
	{},
};

MODULE_DEVICE_TABLE(of, lsm303dl_mag_of_match);
#endif

/****************************************************
*	I2C driver structure definition				*
*****************************************************/
static struct i2c_driver mag_driver = {
	.probe			= lsm303dl_mag_i2c_probe,
	.remove			= lsm303dl_mag_i2c_remove,
	.id_table		= lsm303dl_mag_id,
	.driver			= {
		.owner		= THIS_MODULE,
		.name		= LSM303DL_MAG_NAME,
		.of_match_table = of_match_ptr(lsm303dl_mag_of_match),
		.pm		= &lsm303dl_mag_pm_ops,
	},
};

/*************************************************************************
 *	name	=	lsm303dl_mag_init
 *	func	=	Initialize I2C slave device of magnetometer
 *	input	=	None
 *	output	=	None
 *	return	=	0, -ENOTSUPP
 *************************************************************************/
static int __init lsm303dl_mag_init(void)
{
	int ret	= 0;

	lsm303dl_log("lsm303dl_mag_init is called\n");
	/*Register magnetometer driver to I2C core*/
	ret = i2c_add_driver(&mag_driver);
	if (ret < 0) {
		lsm303dl_err("Cannot register magnetometer to I2C core\n");
		ret = -ENOTSUPP;
		goto handle_error;
	}
	return 0;

handle_error:
	/*Remove Magnetometer driver from I2C core*/
	i2c_del_driver(&mag_driver);

	return ret;
}

/*************************************************************************
 *	name	=	lsm303dl_mag_exit
 *	func	=	Finalize I2C slave device of magnetometer
 *	input	=	None
 *	output	=	None
 *	return	=	None
 *************************************************************************/
static void __exit lsm303dl_mag_exit(void)
{
	/*Remove Magnetometer driver from I2C core*/
	i2c_del_driver(&mag_driver);

	/*Delete Magnetometer client from I2C core*/
	if (mag_client != NULL)
		i2c_unregister_device(mag_client);

}

module_init(lsm303dl_mag_init);
module_exit(lsm303dl_mag_exit);

MODULE_DESCRIPTION("LSM303DL Magnetometer Driver");
MODULE_AUTHOR("Renesas");
MODULE_LICENSE("GPL v2");
