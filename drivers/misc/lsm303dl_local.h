/* drivers/misc/lsm303dl_local.h
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

#ifndef __LSM303DL_LOCAL_H__
#define __LSM303DL_LOCAL_H__

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

/************************************************
*	Debug definition section		*
************************************************/
/*#define LSM303DL_LOG*/
#ifdef LSM303DL_LOG
	#define lsm303dl_log(fmt, ...)	pr_debug("%s(%d): " \
			fmt, __func__, __LINE__, ## __VA_ARGS__)
#else
	#define lsm303dl_log(x...) do { } while (0)
#endif

#define lsm303dl_err(fmt, ...)  pr_err("%s(%d): " \
			fmt, __func__, __LINE__, ## __VA_ARGS__)


/************************************************
*	Accelerometer definition section	    *
************************************************/

/*Conversion unit setting*/
#define ACCELERATION_LOW			(12)
#define ACCELERATION_MED			(4)
#define ACCELERATION_HIGH			(2)
#define ACCELERATION_EXTREME			(1)

/*Register name*/
#define CTRL_REG1_A				(0x20)
#define CTRL_REG2_A				(0x21)
#define CTRL_REG4_A				(0x23)
#define OUT_X_L_A				(0x28)
#define FIFO_CTRL_REG_A				(0x2E)
#define INT1_SRC_A				(0X31)
#define INT1_CFG_A				(0x30)
#define INT1_THS_A				(0x32)
#define CLICK_CFG_A				(0x38)
#define CLICK_THS_A				(0x3A)


/*Output data setting*/
#define ACC_ENABLE_ALL_AXES			(0x07)
#define ACC_DISABLE_ALL_AXES			(0x00)
#define ACC_I2C_AUTO_INCREMENT			(0x80)

#define ACC_IRQ_NUMBER				(0x31)

/************************************************
*	Magnetometer definition section	        *
************************************************/

/*Conversion unit setting*/
#define MAG_XY_SENS_1_3				(1100)
#define MAG_Z_SENS_1_3				(980)
#define MAG_XY_SENS_1_9				(855)
#define MAG_Z_SENS_1_9				(760)
#define MAG_XY_SENS_2_5				(670)
#define MAG_Z_SENS_2_5				(600)
#define MAG_XY_SENS_4_0				(450)
#define MAG_Z_SENS_4_0				(400)
#define MAG_XY_SENS_4_7				(400)
#define MAG_Z_SENS_4_7				(355)
#define MAG_XY_SENS_5_6				(330)
#define MAG_Z_SENS_5_6				(295)
#define MAG_XY_SENS_8_1				(230)
#define MAG_Z_SENS_8_1				(205)

/*Register name*/
#define CRA_REG_M				(0x00)
#define CRB_REG_M				(0x01)
#define MR_REG_M				(0x02)
#define OUT_X_H_M				(0x03)
#define MAG_IRQ_NUMBER				(0x30)

/************************************************
*	LSM303DL definition section	            *
************************************************/
/*Polling mechanism setting*/
#define LSM303DL_POLL_THR			(1400)
#define LSM303DL_POLL_MIN			(5)
#define LSM303DL_ACTIVATE_TIME			(10)
#define LSM303DL_INTERVAL_DEFAUT		(50)
#define MAG_POLLING_INTERVAL_MIN		(4)
#define ACC_POLLING_INTERVAL_MIN		(2)

#define LSM303DL_BUSY				(1)
#define LSM303DL_AVAILABLE			(0)

#define LSM303DL_ACC_REPORT_ID			(1)
#define LSM303DL_MAG_REPORT_ID			(2)

/*I2C setting*/
#define LSM303DL_I2C_RETRIES			(5)
#define LSM303DL_I2C_BUS_NUMBER			(2)
#define LSM303DL_I2C_ADDR_ACC			(0x19)
#define LSM303DL_I2C_ADDR_MAG			(0x1E)
#define LSM303DL_I2C_RETRY_DELAY		(5)

/*Event setting*/
#define EVENT_TYPE_ACCEL_X			(ABS_X)
#define EVENT_TYPE_ACCEL_Y			(ABS_Y)
#define EVENT_TYPE_ACCEL_Z			(ABS_Z)
#define EVENT_TYPE_MAGV_X			(ABS_HAT0X)
#define EVENT_TYPE_MAGV_Y			(ABS_HAT0Y)
#define EVENT_TYPE_MAGV_Z			(ABS_BRAKE)

/*Input parameter setting*/
#define LSM303DL_INPUT_FLAT			(0)
#define LSM303DL_INPUT_FUZZ			(0)
#define LSM303DL_MAX_RANGE_ACC			(32000)
#define LSM303DL_MAX_RANGE_MAG			(32000)

/*sensor ID*/
#define ID_ACC					(0)
#define ID_MAG					(1)

/*Driver name setting*/
#define LSM303DL_DRIVER_NAME			"lsm303dl"
#define LSM303DL_ACC_NAME			"lsm303dl_acc"
#define LSM303DL_MAG_NAME			"lsm303dl_mag"

/*interrupt enable_flag*/
/*#define ACCEL_MAG_INTERRUPT_ENABLED*/

/*for magnetometer hard iron Caliration */
#define MAG_HARD_IRON_CALIBRATION

/* to enable runtime PM */
#define RUNTIME_PM

/************************************************
*	Exported function definition section	*
************************************************/
/*LSM303DL input driver*/
void lsm303dl_acc_report_values(void);
void lsm303dl_mag_report_values(void);
int lsm303dl_get_sns_status(u8 sns_id);
void lsm303dl_acc_int_status(u8 val);
#ifdef CONFIG_HAS_EARLYSUSPEND
/*static void lsm303dl_platform_early_suspend(struct early_suspend \
			*early_suspend);
static void lsm303dl_platform_late_resume(struct early_suspend \
			*early_suspend);*/
#endif

/*LSM303DL accelerometer driver*/
int lsm303dl_acc_set_odr(u32 val);
int lsm303dl_acc_get_data(s16 *data);
int lsm303dl_acc_hw_init(u8 *val);
int lsm303dl_acc_activate(u8 val);

/*LSM303DL magnetometer driver*/
int lsm303dl_mag_set_odr(u32 val);
int lsm303dl_mag_get_data(s16 *data);
int lsm303dl_mag_hw_init(u8 *val);
int lsm303dl_mag_activate(u8 val);

/* PM functions */
int lsm303dl_acc_power_on_off(bool flag);
int lsm303dl_acc_cs_power_on_off(bool flag);
int lsm303dl_mag_power_on_off(bool flag);
int lsm303dl_mag_cs_power_on_off(bool flag);

/************************************************
*	Structure definition section	        *
************************************************/
 /* Structure for lsm303dl_input driver */
struct  lsm303dl_input {
	struct mutex lock;
	struct wake_lock wakelock;
	struct input_dev *input_dev;
	struct work_struct work_func;
	struct hrtimer timer;
	bool suspend_flag;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	/*Activation flag for accelerometer*/
	atomic_t acc_enable;

	/*Activation flag for Magnetometer*/
	atomic_t mag_enable;

	/*Driver status flag*/
	atomic_t available;

	/*User-side delay time*/
	u16  delay;

	/*Actual polling interval on lsm303dl input driver*/
	u16  poll_interval;
};

/* Structure for lsm303dl accelerometer driver */
struct  lsm303dl_acc_data {
	struct mutex lock;
	struct wake_lock wakelock;
	bool suspend_flag;

	/*Sensitivity value*/
	u8 sensitivity;

	/*Output data rate*/
	u8 odr;

	/*User-side delay time*/
	u16  delay;

	/*Actual polling interval on lsm303dl accelerometer driver*/
	u16  poll_interval;

	/*The number of ignored reporting time*/
	u16  report_ignore_cnt;

	/*Orientation Matrix Values */
	signed char orientation[9];

	/*irq number for INT1 interrupt*/
	int irq1;

	/*delayed_work structure for INT1 interrupt*/
	struct delayed_work irq1_work;

	/*workqueue_struct structure for INT1 interrupt*/
	struct workqueue_struct *irq1_workqueue;
	struct hrtimer timer;

	/*work_struct structure for timer interrupt*/
	struct work_struct timer_work;
};

/* Structure for lsm303dl magnetometer driver */
struct  lsm303dl_mag_data {
	struct mutex lock;
	struct wake_lock wakelock;
	bool suspend_flag;

	/*Sensitivity value*/
	u8 sensitivity;

	/*User-side delay time*/
	u16  delay;

	/*Actual polling interval on lsm303dl magnetometer driver*/
	u16  poll_interval;

	/*Orientation Matrix Values */
	signed char orientation[9];

	/*irq number for DRDY interrupt*/
	int drdy;

	/*work_struct structure for DRDY interrupt*/
	struct work_struct drdy_work;

	/*workqueue_struct structure for DRDY interrupt*/
	struct workqueue_struct *drdy_workqueue;
	struct hrtimer timer;

	/*work_struct structure for timer interrupt*/
	struct work_struct timer_work;
};

/*Output data rate structure*/
struct lsm303dl_output_rate {
	/*Output data rate in millisecond*/
	int poll_rate_ms;

	/*The corresponding output data rate in lsm303dl register*/
	u8 mask;
};

/*Sensitivity looked-up table structure*/
struct lsm303dl_sens_table {
	/*Sensitivity unit for x-axis*/
	u16 x;

	/*Sensitivity unit for y-axis*/
	u16 y;

	/*Sensitivity unit for z-axis*/
	u16 z;
};

#endif
