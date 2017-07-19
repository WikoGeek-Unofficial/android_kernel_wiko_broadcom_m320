/*
 * Copyright (C) 2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef BCM_WLAN_H
#define BCM_WLAN_H
struct wlan_plat_data
{
	int wl_reset_gpio;
	int host_wake_gpio;
	char *wlan_nv_path;
	char *wlan_mac_addr;
};
#endif
