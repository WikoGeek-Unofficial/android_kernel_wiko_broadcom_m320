/* include/linux/l3gd20.h
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

#ifndef ANDROID_GYROSCOPE_SENSOR_H
#define ANDROID_GYROSCOPE_SENSOR_H

#define L3GD20_NAME			"l3gd20"
#define ENABLE				1
#define DISABLE				0
#define GYRO_SLAVE_ADDRESS		0x6B


/* IOCLT command for gyroscope sensor */
#define RMC_IO   0xA2
#define L3GD20_IOCTL_SET_DELAY	_IOW(RMC_IO, 0x20, unsigned long)
#define L3GD20_IOCTL_SET_ENABLE	_IOW(RMC_IO, 0x21, int)
#define L3GD20_IOCTL_NV_DATA_ADDRESS	_IOW(RMC_IO, 0x22, unsigned long)
#define EVENT_TYPE_GYRO_X				ABS_HAT1X
#define EVENT_TYPE_GYRO_Y				ABS_HAT1Y
#define EVENT_TYPE_GYRO_Z				ABS_GAS
#define PI						(314/100)
#define CONVERT_GYRO_RAD				((2*PI)/360)

#endif /*ANDROID_GYROSCOPE_SENSOR_H*/
