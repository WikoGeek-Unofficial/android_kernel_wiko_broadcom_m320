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
#ifndef __LINUX_LTR501_H
#define __LINUX_LTR501_H

#define LTR501_ALS_LEVEL_NUM	16
#define LTR501_DRV_NAME		"ltr501"

#define LTR501_ALS_CONTR	0x80
#define LTR501_PS_CONTR		0x81
#define LTR501_PS_LED		0x82
#define LTR501_PS_N_PULSES	0x83
#define LTR501_PS_MEAS_RATE	0x84
#define LTR501_ALS_MEAS_RATE	0x85

#define LTR501_PART_ID		0x86
#define LTR501_MANUFAC_ID	0x87

#define LTR501_ALS_DATA1	0x88
#define LTR501_ALS_DATA0	0x8A

#define LTR501_ALS_PS_STATUS	0x8C
#define LTR501_PS_DATA		0x8D

#define LTR501_INTERRUPT	0x8F
#define LTR501_PS_THRES_UP	0x90
#define LTR501_PS_THRES_LOW	0x92

#define LTR501_ALS_THRES_UP	0x97
#define LTR501_ALS_THRES_LOW	0x99
#define LTR501_INTERRUPT_PERSIST	0x9E

#define LTR501_ALS_CONTR_SW_RESET	BIT(2)
#define LTR501_CONTR_PS_GAIN_MASK	(BIT(3) | BIT(2))
#define LTR501_CONTR_PS_GAIN_SHIFT	2
#define LTR501_CONTR_ALS_GAIN_MASK	BIT(3)
#define LTR501_CONTR_ACTIVE		BIT(1)

#define LTR501_INTR_MODE		0x01
#define LTR501_STATUS_ALS_RDY		BIT(2)
#define LTR501_STATUS_PS_RDY		BIT(0)

#define LTR501_PS_DATA_MASK		0x7ff

#define LIGHT_SENSOR_START_TIME_DELAY	5000000

struct ltr501_reg {
	const char *name;
	u8 reg;
} ltr501_regs[] = {
	{"ALS_CONTR", LTR501_ALS_CONTR},
	{"PS_CONTR", LTR501_PS_CONTR},
	{"PS_LED", LTR501_PS_LED},
	{"PS_N_PULSE", LTR501_PS_N_PULSES},
	{"PS_MEAS_RATE", LTR501_PS_MEAS_RATE},
	{"ALS_MEAS_RATE", LTR501_ALS_MEAS_RATE},
	{"PART_ID", LTR501_PART_ID},
	{"MANUFAC_ID", LTR501_MANUFAC_ID},
	{"ALS_DATA1_LSB", LTR501_ALS_DATA1},
	{"ALS_DATA1_MSB", LTR501_ALS_DATA1 + 1},
	{"ALS_DATA0_LSB", LTR501_ALS_DATA0},
	{"ALS_DATA0_MSB", LTR501_ALS_DATA0 + 1},
	{"ALS_PS_STATUS", LTR501_ALS_PS_STATUS},
	{"PS_DATA_LSB", LTR501_PS_DATA},
	{"PS_DATA_MSB", LTR501_PS_DATA + 1},
	{"INTERRUPT", LTR501_INTERRUPT},
	{"PS_THRES_UP_LSB", LTR501_PS_THRES_UP},
	{"PS_THRES_UP_MSB", LTR501_PS_THRES_UP + 1},
	{"PS_THRES_LOW_LSB", LTR501_PS_THRES_LOW},
	{"PS_THRES_LOW_MSB", LTR501_PS_THRES_LOW + 1},
	{"ALS_THRES_UP_LSB", LTR501_ALS_THRES_UP},
	{"ALS_THRES_UP_MSB", LTR501_ALS_THRES_UP + 1},
	{"ALS_THRES_LOW_LSB", LTR501_ALS_THRES_LOW},
	{"ALS_THRES_LOW_MSB", LTR501_ALS_THRES_LOW + 1},
	{"INTERRUPT_PERSIST", LTR501_INTERRUPT_PERSIST},
};

enum {
	LIGHT_ENABLED = BIT(0),
	PROXIMITY_ENABLED = BIT(1),
};

struct ltr501_pdata {
	u8 als_contr;
	u8 ps_contr;
	u8 ps_led;
	u8 ps_n_pulses;
	u8 ps_meas_rate;
	u8 als_meas_rate;
	u8 interrupt;
	u8 interrupt_persist;
	u16 prox_threshold_high;
	u16 prox_threshold_low;
	u16 prox_offset;
	u8 scale_factor;
};
#endif
