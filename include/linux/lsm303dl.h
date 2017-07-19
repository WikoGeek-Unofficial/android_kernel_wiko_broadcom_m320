/* include/linux/lsm303dl.h
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

#ifndef __LSM303DL_H__
#define __LSM303DL_H__

/* Structure for removing GPIO direct access from code */
struct lsm303dl_acc_port_info {
	unsigned int lsm303dl_acc_port;
};

struct lsm303dl_mag_port_info {
	unsigned int lsm303dl_mag_port;
};

struct lsm303dl_platform_data {
	int (*power)(int on);
};

#define ACC_SLAVE_ADDRESS		0x19
#define MAG_SLAVE_ADDRESS		0x1E
#define ACC_INT				0x31

/************************************************
*	IO control definition section		    *
************************************************/
#define IOCTL_ACC_ENABLE		(_IOW(0xA2, 0x01, unsigned int))
#define IOCTL_MAG_ENABLE		(_IOW(0xA2, 0x02, unsigned int))
#define IOCTL_SET_DELAY			(_IOW(0xA2, 0x03, unsigned int))
#define IOCTL_GET_NV_ADDRESS		(_IOW(0xA2, 0x04, unsigned int))

/************************************************
*	NV setting definition section		    *
************************************************/
#define MAX_SETTING_ACC			(3)
#define MAX_SETTING_MAG			(1)
#define MAX_SETTING_LSM303DL		(4)
#define ACC_HPF				(0)
#define ACC_SENS			(1)
#define ACC_H_RES			(2)
#define MAG_SENS			(3)

/************************************************
*	Accelerometer definition section	    *
************************************************/
/*Power setting*/
#define ACC_NORMAL			(0)
#define ACC_STANDBY			(1)

/*Sensitivity setting*/
#define ACC_SENSITIVITY_LOW		(3)
#define ACC_SENSITIVITY_MED		(2)
#define ACC_SENSITIVITY_HIGH		(1)
#define ACC_SENSITIVITY_EXTREME		(0)

/*High pass filter setting*/
#define HPF_RESET_READ_FILTER		(0)
#define HPF_REFERENCE_SIGNAL		(1)
#define HPF_NORMAL_MODE			(2)
#define HPF_AUTORESET_ON_INTERRUPT	(3)

/*Activation setting*/
#define ACC_ENABLE			(1)
#define ACC_DISABLE			(0)

/*Output data rate setting*/
#define ACC_ODR_NONE			(0)
#define ACC_ODR_1			(1)
#define ACC_ODR_10			(2)
#define ACC_ODR_25			(3)
#define ACC_ODR_50			(4)
#define ACC_ODR_100			(5)
#define ACC_ODR_200			(6)
#define ACC_ODR_400			(7)
#define ACC_ODR_5376			(9)

/*Event setting*/
#define EVENT_TYPE_ACCEL_X		(ABS_X)
#define EVENT_TYPE_ACCEL_Y		(ABS_Y)
#define EVENT_TYPE_ACCEL_Z		(ABS_Z)

/************************************************
*	Magnetometer definition section	        *
************************************************/
/*Power setting*/
#define MAG_NORMAL			(0)
#define MAG_STANDBY			(3)

/*Sensitivity setting*/
#define MAG_SENS_1_3			(1)
#define MAG_SENS_1_9			(2)
#define MAG_SENS_2_5			(3)
#define MAG_SENS_4_0			(4)
#define MAG_SENS_4_7			(5)
#define MAG_SENS_5_6			(6)
#define MAG_SENS_8_1			(7)

/*Output data rate setting*/
#define MAG_ODR_0_75			(0)
#define MAG_ODR_1_5			(1)
#define MAG_ODR_3_0			(2)
#define MAG_ODR_7_5			(3)
#define MAG_ODR_15			(4)
#define MAG_ODR_30			(5)
#define MAG_ODR_75			(6)
#define MAG_ODR_220			(7)
/*Activation setting*/
#define MAG_ENABLE			(1)
#define MAG_DISABLE			(0)

/*Event setting*/
#define EVENT_TYPE_MAGV_X		(ABS_HAT0X)
#define EVENT_TYPE_MAGV_Y		(ABS_HAT0Y)
#define EVENT_TYPE_MAGV_Z		(ABS_BRAKE)

#endif
