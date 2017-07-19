/*
 *
 * FocalTech ft5x06 TouchScreen driver header file.
 *
 * Copyright (c) 2010  Focal tech Ltd.
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
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
#ifndef __LINUX_FT5X06_TS_H__
#define __LINUX_FT5X06_TS_H__

#define FT5336_NAME "ft5336_ts"

#define BCMTCH_MAX_BUTTONS       16
#define BCMTCH_POWER_ON_DELAY_US_MIN    5000

#define FT5336_X_MAX_DEFAULT 768
#define FT5336_Y_MAX_DEFAULT 1280
#define FT5336_IRQ_GPIO_DEFAULT 21

int ft5x06_i2c_read(struct i2c_client *client, char *writebuf,
	int writelen, char *readbuf, int readlen);
int ft5x06_i2c_write(struct i2c_client *client, char *writebuf,
	int writelen);

struct ft5x06_ts_platform_data {
	u32 x_max;
	u32 y_max;
	u32 irq_gpio;
	int irq_gpio_trigger;
	int touch_irq;
	u32 reset_gpio;
	int reset_gpio_polarity;
	int reset_gpio_time_ms;
	int (*power_init) (bool);
	int (*power_on) (bool);

	int power_on_delay_us;
	int ext_button_count;
	int ext_button_map[BCMTCH_MAX_BUTTONS];
	char *vkey_scope;
};
#endif
