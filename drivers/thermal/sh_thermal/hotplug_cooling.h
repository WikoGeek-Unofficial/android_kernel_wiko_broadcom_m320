/*****************************************************************************
* Copyright 2013 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

#ifndef __HOTPLUG_COOLING_H__
#define __HOTPLUG_COOLING_H__

#include <linux/thermal.h>
#include <linux/cpumask.h>

#ifdef CONFIG_HOTPLUG_THERMAL

struct thermal_cooling_device *hotplug_cooling_register(
			const struct cpumask *allowed_cpus);

void hotplug_cooling_unregister(struct thermal_cooling_device *cdev);

unsigned long hotplug_cooling_get_level(unsigned int cpu_mask);

#else

static inline struct thermal_cooling_device *hotplug_cooling_register(
			const struct cpumask *allowed_cpus)
{
	return NULL;
}

static inline void hotplug_cooling_unregister(
			struct thermal_cooling_device *cdev)
{
	return;
}

static inline unsigned long hotplug_cooling_get_level(unsigned int cpu_mask)
{
	return THERMAL_CSTATE_INVALID;
}
#endif	/* CONFIG_HOTPLUG_THERMAL */

#endif /* __HOTPLUG_COOLING_H__ */
