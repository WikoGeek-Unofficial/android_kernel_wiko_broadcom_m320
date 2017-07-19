/* drivers/misc/l3gd20_local.h
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
#include <linux/earlysuspend.h>

#define AUTO_INCREMENT				0x80
#define L3GD20_ENABLE_ALL_AXES		0x07
#define HW_DELAY_11MS				11
#define HW_DELAY_6MS				6
#define HW_DELAY_3MS				3
#define HW_DELAY_2MS				2
#define POWER_POLLING_THRES			500
#define MAX_ODR						11
#define L3GD20_I2C_BUS_NUMBER		2
#define L3GD20_I2C_SLAVE_ADDR		0x6b
#define MSEC_TO_NSEC				1000000
#define L3GD20_MAX_RANGE			32767
#define L3GD20_MIN_RANGE			-32768

/* Define print function */
/*#define L3GD20_LOG*/
#ifdef L3GD20_LOG
	#define l3gd20_log(fmt, ...) \
	pr_debug("\n%s(%d): " fmt, __func__, \
					__LINE__, ## __VA_ARGS__)
#else
	#define l3gd20_log(x...) do { } while (0)
#endif

/* runtime pm flag */
#define RUNTIME_PM

/* I2C info */
#define L3GD20_I2C_RETRIES			5
#define L3GD20_RETRIES_DELAY		5	/* ms */

/* l3gd20 registers map */
#define L3GD20_CTRL_REG1			0x20
#define L3GD20_CTRL_REG2			0x21
#define L3GD20_CTRL_REG3			0x22
#define L3GD20_CTRL_REG4			0x23
#define L3GD20_CTRL_REG5			0x24
#define L3GD20_OUT_X_L				0x28
#define L3GD20_OUT_X_H				0x29
#define L3GD20_OUT_Y_L				0x2A
#define L3GD20_OUT_Y_H				0x2B
#define L3GD20_OUT_Z_L				0x2C
#define L3GD20_OUT_Z_H				0x2D
#define L3GD20_FIFO_CTRL			0x2E
#define L3GD20_INT1_THRESH_X_H		0x32
#define L3GD20_INT1_THRESH_X_L		0x33
#define L3GD20_INT1_THRESH_Y_H		0x34
#define L3GD20_INT1_THRESH_Y_L		0x35
#define L3GD20_INT1_THRESH_Z_H		0x36
#define L3GD20_INT1_THRESH_Z_L		0x37

/* Output data rate mode */
#define ODR_MODE1					0
#define ODR_MODE2					1
#define ODR_MODE3					2
#define ODR_MODE4					3

/* Bandwidth mode */
#define BW_MODE1					0
#define BW_MODE2					1
#define BW_MODE3					2
#define BW_MODE4					3

/* High-pass filter mode */
#define HPF_MODE1					0
#define HPF_MODE2					1
#define HPF_MODE3					2
#define HPF_MODE4					3

/* High-pass filter cut off frequency selection value */
#define HPF_FRE_SEL1				0
#define HPF_FRE_SEL2				1
#define HPF_FRE_SEL3				2
#define HPF_FRE_SEL4				3
#define HPF_FRE_SEL5				4
#define HPF_FRE_SEL6				5
#define HPF_FRE_SEL7				6
#define HPF_FRE_SEL8				7
#define HPF_FRE_SEL9				8
#define HPF_FRE_SEL10				9

/* Sensitivity */
#define SENSITIVE_1					0
#define SENSITIVE_2					1
#define SENSITIVE_3					2
#define SENSITIVE_4					3

/* Conversion unit */
#define CONVERT_SENSITY_250_NUMERATOR		875
#define CONVERT_SENSITY_250_DENOMINATOR		100000
#define CONVERT_SENSITY_500_NUMERATOR		1750
#define CONVERT_SENSITY_500_DENOMINATOR		100000
#define CONVERT_SENSITY_2000_NUMERATOR		70
#define CONVERT_SENSITY_2000_DENOMINATOR	1000

/* Parameter location */
#define BANDWIDTH		0
#define SENSITIVITY		1
#define ENABLE_HPF		2
#define HPF_MODE		3
#define HPF_CUTOFF_FREQUENCY	4
#define L3GD20_SETTING_MAX	5

/* MASK of register */
#define BW00		0x00
#define BW01		0x10
#define BW10		0x20
#define BW11		0x30
#define ODR095		0x00  /* ODR =  95Hz */
#define ODR190		0x40  /* ODR = 190Hz */
#define ODR380		0x80  /* ODR = 380Hz */
#define ODR760		0xC0  /* ODR = 760Hz */

/* data structure */
struct l3gd20_data {
	struct mutex lock;
	struct wake_lock wakelock;
	struct input_dev *gyro_input_dev;
	struct work_struct timer_work_func;
	struct hrtimer timer;
	struct early_suspend early_suspend;
	/*Orientation Matrix Values */
	signed char orientation[9];
	unsigned long poll_cycle;
	unsigned long real_dly;
	unsigned char sensitivity;
	atomic_t available;
	atomic_t enable;
	bool suspend_flag;
};

struct l3gd20_triple {
	short    x;
	short    y;
	short    z;
};

struct output_rate {
	int poll_rate_ms;
	unsigned char mask;
};
