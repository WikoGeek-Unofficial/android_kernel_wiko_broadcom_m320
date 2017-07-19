/*
 * include/media/lm3642.h
 *
 * Copyright (C) 2013,2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __LM3642_H__
#define __LM3642_H__

#define LM3642_NAME				"lm3642"
#define LM3642_I2C_ADDR			(0x63)

#define LM3642_FLASH_TIMEOUT_MIN		100	/* ms */
#define LM3642_FLASH_TIMEOUT_MAX		800
#define LM3642_FLASH_TIMEOUT_STEP		100

#define LM3642_FLASH_INTENSITY_MIN		94	/* mA */
#define LM3642_FLASH_INTENSITY_MAX		1500
#define LM3642_FLASH_INTENSITY_STEP		94

#define LM3642_TORCH_INTENSITY_MIN		48	/* mA */
#define LM3642_TORCH_INTENSITY_MAX		375
#define LM3642_TORCH_INTENSITY_STEP		47


/*
 * lm3642_platform_data - Flash controller platform data
 * @peak:	Inductor peak current limit (0=1.6A, 1=1.88A)
 * @ext_ctrl:	True if external flash strobe can be used
 * @strobe_pin:	gpio pin to control strobe
 * @torch_pin:	gpio pin to control torch
 * @flash_max_current:	Max flash current (mA, <= LM3642_FLASH_INTENSITY_MAX)
 * @torch_max_current:	Max torch current (mA, >= LM3642_TORCH_INTENSITY_MAX)
 * @timeout_max:	Max flash timeout (us, <= LM3642_FLASH_TIMEOUT_MAX)
 */
struct lm3642_platform_data {
	unsigned int peak;
	bool ext_ctrl;
	/* pins */
	unsigned int strobe_pin;
	unsigned int torch_pin;

	/* Flash and torch currents and timeout limits */
	unsigned int flash_max_current;
	unsigned int torch_max_current;
	unsigned int timeout_max;
};

int lm3642_setup(int mode, int intensity, int duration);
int lm3642_enable(void);
int lm3642_disable(void);

#endif /* __LM3642_H__ */
