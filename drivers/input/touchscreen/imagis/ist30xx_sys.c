/*
 *  Copyright (C) 2010,Imagis Technology Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <asm/unaligned.h>

#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <mach/common.h>

#include "ist30xx.h"

/******************************************************************************
 * Return value of Error
 * EPERM  : 1
 * ENOENT : 2
 * EIO    : 5
 * EINVAL : 22
 *****************************************************************************/


extern struct ist30xx_data *ts_data;

const u32 pos_cmd = cpu_to_be32(CMD_GET_COORD);
struct i2c_msg pos_msg[READ_CMD_MSG_LEN] = {
	{
		.flags = 0,
		.len = IST30XX_ADDR_LEN,
		.buf = (u8 *)&pos_cmd,
	},
	{ .flags = I2C_M_RD, },
};

int ist30xx_get_position(struct i2c_client *client, u32 *buf, u16 len)
{
	int ret, i;
	pos_msg[0].addr = client->addr;
	pos_msg[1].addr = client->addr;
	pos_msg[1].len = len * IST30XX_DATA_LEN,
	pos_msg[1].buf = (u8 *)buf,

	ret = i2c_transfer(client->adapter, pos_msg, READ_CMD_MSG_LEN);
	if (ret != READ_CMD_MSG_LEN) {
		pr_err("[ TSP ] %s: i2c failed (%d)\n", __func__, ret);
		return -EIO;
	}

	for (i = 0; i < len; i++)
		buf[i] = cpu_to_be32(buf[i]);

	return 0;
}

int ist30xx_cmd_run_device(struct i2c_client *client)
{
	int ret = -EIO;

	ist30xx_reset();
	ret = ist30xx_write_cmd(client, CMD_RUN_DEVICE, 0);
	msleep(10);

	return ret;
}

int ist30xx_cmd_start_scan(struct i2c_client *client)
{
	int ret = ist30xx_write_cmd(client, CMD_START_SCAN, 0);

	msleep(100);

	return ret;
}

int ist30xx_cmd_calibrate(struct i2c_client *client)
{
	int ret = ist30xx_write_cmd(client, CMD_CALIBRATE, 0);

	msleep(100);

	return ret;
}

int ist30xx_cmd_update(struct i2c_client *client, int cmd)
{
	int ret = ist30xx_write_cmd(client, cmd, 0);

	msleep(10);

	return ret;
}

int ist30xx_cmd_reg(struct i2c_client *client, int cmd)
{
	int ret = ist30xx_write_cmd(client, cmd, 0);

	if (cmd == CMD_ENTER_REG_ACCESS)
		msleep(40);

	return ret;
}


int ist30xx_read_cmd(struct i2c_client *client, u32 cmd, u32 *buf)
{
	int ret;
	u32 le_reg = cpu_to_be32(cmd);

	struct i2c_msg msg[READ_CMD_MSG_LEN] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = IST30XX_ADDR_LEN,
			.buf = (u8 *)&le_reg,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = IST30XX_DATA_LEN,
			.buf = (u8 *)buf,
		},
	};

	ret = i2c_transfer(client->adapter, msg, READ_CMD_MSG_LEN);
	if (ret != READ_CMD_MSG_LEN) {
		pr_err("[ TSP ] %s: i2c failed (%d)\n", __func__, ret);
		return -EIO;
	}
	*buf = cpu_to_be32(*buf);

	return 0;
}


int ist30xx_write_cmd(struct i2c_client *client, u32 cmd, u32 val)
{
	int ret;
	struct i2c_msg msg;
	u8 msg_buf[IST30XX_ADDR_LEN + IST30XX_DATA_LEN];

	put_unaligned_be32(cmd, msg_buf);
	put_unaligned_be32(val, msg_buf + IST30XX_ADDR_LEN);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = IST30XX_ADDR_LEN + IST30XX_DATA_LEN;
	msg.buf = msg_buf;

	ret = i2c_transfer(client->adapter, &msg, WRITE_CMD_MSG_LEN);
	if (ret != WRITE_CMD_MSG_LEN) {
		pr_err("[ TSP ] %s: i2c failed (%d)\n", __func__, ret);
		return -EIO;
	}

	return 0;
}


static struct regulator *touch_regulator;

static void ts_power_enable(int en)
{
	printk(KERN_INFO "%s %s\n", __func__, (en) ? "on" : "off");

	if (touch_regulator == NULL) {
		touch_regulator = regulator_get(NULL, "vtsp_3v");
		if(IS_ERR(touch_regulator)){
			printk(KERN_ERR "can not get VTOUCH_3.3V\n");
			touch_regulator = NULL;
			return;
		}
	}

	if (en) {
		regulator_set_voltage(touch_regulator, 3000000, 3000000); //3.0V
		regulator_enable(touch_regulator);
	} else {
		regulator_disable(touch_regulator);
	}
}


int ist30xx_power_on(void)
{
	if (ts_data->status.power != 1) {
		ts_power_enable(1);

		msleep(20);
		ts_data->status.power = 1;
		DMSG("[ TSP ] %s\n", __func__);
	}

	return 0;
}


int ist30xx_power_off(void)
{

	if (ts_data->status.power != 0) {
		ts_power_enable(0);

		msleep(10);
		ts_data->status.power = 0;
		DMSG("[ TSP ] %s\n", __func__);
	}

	return 0;
}


int ist30xx_reset(void)
{
	ist30xx_power_off();
	ist30xx_power_on();

	ts_data->status.power = 1;
	return 0;
}


int ist30xx_internal_suspend(struct ist30xx_data *data)
{
	ist30xx_power_off();
	return 0;
}


int ist30xx_internal_resume(struct ist30xx_data *data)
{
	int ret = -EIO;
	ist30xx_power_on();
	ret = ist30xx_write_cmd(data->client, CMD_RUN_DEVICE, 0);
	msleep(10);
	return ret;
}


int ist30xx_init_system(void)
{
	int ret;

	// TODO : place additional code here.

	ret = ist30xx_power_on();
	if (ret) {
		pr_err("[ TSP ] %s: ist30xx_power_on failed (%d)\n", __func__, ret);
		return -EIO;
	}

	ret = ist30xx_reset();
	if (ret) {
		pr_err("[ TSP ] %s: ist30xx_reset failed (%d)\n", __func__, ret);
		return -EIO;
	}

	return 0;
}
