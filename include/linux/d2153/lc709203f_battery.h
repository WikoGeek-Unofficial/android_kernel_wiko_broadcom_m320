/*
 * lc709203f external fuel gauge  declarations.
 *
 * 2001 - 2014 Broadcom Corporation.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 */


#ifndef __LC709203_BATTERY_EXT_H__
#define __LC709203_BATTERY_EXT_H__


struct lc709203f_platform_data {
	const char *tz_name;
	u32 initial_rsoc;
	u32 appli_adjustment;
	u32 change_of_param;
	u32 thermistor_beta;
	u32 therm_adjustment;
	u32 threshold_soc;
	u32 maximum_soc;
	u32 rechg_soc;
	u32 bkchg_soc;
	u32 ftchg_soc;
};

struct lc709203f_chip {
	struct i2c_client               *client;
	struct lc709203f_platform_data  *pdata;
	struct d2153_battery *d2153;
	int shutdown_complete;
	int charge_complete;
	struct mutex mutex;
	int soc;
#if defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif

};
#endif /* __LC709203_BATTERY_EXT_H__ */
