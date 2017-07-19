/*
 * rmu2_rwdt.h
 *
 * Copyright (C) 2013 Renesas Mobile Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#ifndef _LINUX_RWDT_H
#define _LINUX_RWDT_H

#include <linux/ioctl.h>

#ifdef CONFIG_RMU2_RWDT
int rmu2_rwdt_cntclear(void);
int rmu2_rwdt_stop(void);
void rmu2_touch_softlockup_watchdog(void);
#else
static inline int rmu2_rwdt_cntclear(void)
{
	return 0;
}
static inline int rmu2_rwdt_stop(void)
{
	return 0;
}
static inline void rmu2_touch_softlockup_watchdog(void) {}
#endif

#endif  /* _LINUX_RWDT_H */

/* End of File */

