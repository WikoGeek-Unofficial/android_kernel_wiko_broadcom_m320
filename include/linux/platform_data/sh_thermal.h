/******************************************************************************/
/*                                                                            */
/*  Copyright 2013  Broadcom Corporation                                      */
/*                                                                            */
/*     Unless you and Broadcom execute a separate written software license    */
/*     agreement governing use of this software, this software is licensed    */
/*     to you under the terms of the GNU General Public License version 2     */
/*     (the GPL), available at                                                */
/*                                                                            */
/*          http://www.broadcom.com/licenses/GPLv2.php                        */
/*                                                                            */
/*     with the following added to such license:                              */
/*                                                                            */
/*     As a special exception, the copyright holders of this software give    */
/*     you permission to link this software with independent modules, and     */
/*     to copy and distribute the resulting executable under terms of your    */
/*     choice, provided that you also meet, for each linked independent       */
/*     module, the terms and conditions of the license of that module. An     */
/*     independent module is a module which is not derived from this          */
/*     software.  The special exception does not apply to any modifications   */
/*     of the software.                                                       */
/*                                                                            */
/*     Notwithstanding the above, under no circumstances may you combine      */
/*     this software in any way with any other Broadcom software provided     */
/*     under a license other than the GPL, without Broadcom's express prior   */
/*     written consent.                                                       */
/*                                                                            */
/******************************************************************************/
#ifndef __SH_THERMAL_H__
#define __SH_THERMAL_H__

#include <linux/thermal.h>

#if defined(DEBUG)
#define ths_dbg printk
#else
#define ths_dbg(log_typ, format...)			\
	do {								\
		if ((log_typ) == THS_LOG_ERR ||		\
				(log_typ) == THS_LOG_ALERT)	\
			pr_err("[THS]:"format);			\
		else if (ths_dbg_mask & (log_typ))		\
			pr_info("[THS]:"format);			\
	} while (0)
#endif

#define SMSTPCR5_THS_STOP_BIT		22

enum {
	THS_ENABLE_RESET = (1 << 0),
};

struct ths_trip {
	int temp;
	unsigned int max_freq;
	unsigned long hotplug_mask;
	enum thermal_trip_type type;
};

struct thermal_sensor_data {
	int hysteresis;
	int flags;
	int shutdown_temp;
	int trip_cnt;
	struct ths_trip *trips;
};

#endif /*__SH_THERMAL_H__*/
