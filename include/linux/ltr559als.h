/*******************************************************************************
* Copyright 2013 Broadcom Corporation.  All rights reserved.
*
*       @file   drivers/misc/ltr559als.c
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
#ifndef __LINUX_LTR559ALS_H
#define __LINUX_LTR559ALS_H

#define LTR559ALS_ALS_LEVEL_NUM	16
#define LTR559ALS_DRV_NAME		"ltr559als"

#define LTR559ALS_ALS_CONTR	0x80
#define LTR559ALS_PS_CONTR	0x81
#define LTR559ALS_PS_LED	0x82
#define LTR559ALS_PS_N_PULSES	0x83
#define LTR559ALS_PS_MEAS_RATE	0x84
#define LTR559ALS_ALS_MEAS_RATE	0x85

#define LTR559ALS_PART_ID	0x86
#define LTR559ALS_MANUFAC_ID	0x87

#define LTR559ALS_ALS_DATA1	0x88
#define LTR559ALS_ALS_DATA0	0x8A

#define LTR559ALS_ALS_PS_STATUS	0x8C
#define LTR559ALS_PS_DATA	0x8D

#define LTR559ALS_INTERRUPT	0x8F
#define LTR559ALS_PS_THRES_UP	0x90
#define LTR559ALS_PS_THRES_LOW	0x92
#define LTR559ALS_PS_OFFSET	0x94
#define LTR559ALS_ALS_THRES_UP	0x97
#define LTR559ALS_ALS_THRES_LOW	0x99
#define LTR559ALS_INTERRUPT_PERSIST	0x9E

#define LTR559ALS_ALS_CONTR_SW_RESET	BIT(1)
#define LTR559ALS_CONTR_PS_GAIN_MASK	(BIT(3) | BIT(2))
#define LTR559ALS_CONTR_PS_GAIN_SHIFT	2
#define LTR559ALS_CONTR_ALS_GAIN_MASK	(BIT(2) | BIT(3) | BIT(4))
#define LTR559ALS_CONTR_ACTIVE		BIT(1)

#define LTR559ALS_INTR_MODE		0x01
#define LTR559ALS_STATUS_ALS_RDY		BIT(2)
#define LTR559ALS_STATUS_PS_RDY		BIT(0)

#define LTR559ALS_PS_DATA_MASK		0x7ff

#define LIGHT_SENSOR_START_TIME_DELAY	5000000

struct ltr559als_reg {
	const char *name;
	u8 reg;
} ltr559als_regs[] = {
	{"ALS_CONTR", LTR559ALS_ALS_CONTR},
	{"PS_CONTR", LTR559ALS_PS_CONTR},
	{"PS_LED", LTR559ALS_PS_LED},
	{"PS_N_PULSE", LTR559ALS_PS_N_PULSES},
	{"PS_MEAS_RATE", LTR559ALS_PS_MEAS_RATE},
	{"ALS_MEAS_RATE", LTR559ALS_ALS_MEAS_RATE},
	{"PART_ID", LTR559ALS_PART_ID},
	{"MANUFAC_ID", LTR559ALS_MANUFAC_ID},
	{"ALS_DATA1_LSB", LTR559ALS_ALS_DATA1},
	{"ALS_DATA1_MSB", LTR559ALS_ALS_DATA1 + 1},
	{"ALS_DATA0_LSB", LTR559ALS_ALS_DATA0},
	{"ALS_DATA0_MSB", LTR559ALS_ALS_DATA0 + 1},
	{"ALS_PS_STATUS", LTR559ALS_ALS_PS_STATUS},
	{"PS_DATA_LSB", LTR559ALS_PS_DATA},
	{"PS_DATA_MSB", LTR559ALS_PS_DATA + 1},
	{"INTERRUPT", LTR559ALS_INTERRUPT},
	{"PS_THRES_UP_LSB", LTR559ALS_PS_THRES_UP},
	{"PS_THRES_UP_MSB", LTR559ALS_PS_THRES_UP + 1},
	{"PS_THRES_LOW_LSB", LTR559ALS_PS_THRES_LOW},
	{"PS_THRES_LOW_MSB", LTR559ALS_PS_THRES_LOW + 1},
	{"PS_OFFSET_MSB", LTR559ALS_PS_OFFSET},
	{"PS_OFFSET_LSB", LTR559ALS_PS_OFFSET+1},
	{"ALS_THRES_UP_LSB", LTR559ALS_ALS_THRES_UP},
	{"ALS_THRES_UP_MSB", LTR559ALS_ALS_THRES_UP + 1},
	{"ALS_THRES_LOW_LSB", LTR559ALS_ALS_THRES_LOW},
	{"ALS_THRES_LOW_MSB", LTR559ALS_ALS_THRES_LOW + 1},
	{"INTERRUPT_PERSIST", LTR559ALS_INTERRUPT_PERSIST},
};

enum {
	LIGHT_ENABLED = BIT(0),
	PROXIMITY_ENABLED = BIT(1),
};

struct ltr559als_pdata {
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
