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

#include <linux/module.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/cpu_cooling.h>
#include <mach/pm.h>

#ifndef CONFIG_HOTPLUG_CPU_MGR
#define cpu_up_manager(cpu, id)		cpu_up(cpu)
#define cpu_down_manager(cpu, id)	cpu_down(cpu)
#endif

struct hotplug_cooling_device {
	int id;
	struct cpumask allowed_cpus;
	unsigned long cur_state;
};
static DEFINE_IDR(hotplug_idr);

static int get_idr(struct idr *idr, int *id)
{
	int ret;

	ret = idr_alloc(idr, NULL, 0, 0, GFP_KERNEL);
	if (unlikely(ret < 0))
		return ret;
	*id = ret;

	return 0;
}

static void release_idr(struct idr *idr, int id)
{
	idr_remove(idr, id);
}

static inline void u32_to_cpu_mask(const unsigned long u32_mask,
		struct cpumask *cpu_mask)
{
	int bit = 0;

	for_each_set_bit(bit, &u32_mask, sizeof(u32_mask))
		cpumask_set_cpu(bit, cpu_mask);
}

static inline void cpu_mask_to_u32(const struct cpumask *cpu_mask,
		unsigned long *u32_mask)
{
	int cpu;
	unsigned long idx = 0;

	*u32_mask = 0;
	for_each_cpu(cpu, cpu_mask)
		*u32_mask |= (1 << idx++);
}

static inline void u32_to_coolant_level(const unsigned long u32_mask,
		unsigned long *level)
{
	unsigned long u32_pmask;

	cpu_mask_to_u32(cpu_possible_mask, &u32_pmask);
	*level = u32_pmask - u32_mask;
}

static inline void coolant_level_to_u32(const unsigned long level,
		unsigned long *u32_mask)
{
	cpu_mask_to_u32(cpu_possible_mask, u32_mask);
	*u32_mask -= level;
}

unsigned long hotplug_cooling_get_level(unsigned long cpu_mask)
{
	unsigned long level = 0;

	u32_to_coolant_level(cpu_mask, &level);

	return level;
}
EXPORT_SYMBOL_GPL(hotplug_cooling_get_level);

static int hotplug_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	cpu_mask_to_u32(cpu_possible_mask, state);

	return 0;
}

static int hotplug_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct hotplug_cooling_device *hotplug_dev = cdev->devdata;

	return hotplug_dev->cur_state;
}

static int hotplug_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct hotplug_cooling_device *hotplug_dev = cdev->devdata;
	struct cpumask cpu_mask;
	unsigned long u32_mask;
	int cpu_id;

	cpumask_clear(&cpu_mask);
	coolant_level_to_u32(state, &u32_mask);
	u32_to_cpu_mask(u32_mask, &cpu_mask);

	hotplug_dev->cur_state = state;

	/* Is requested cpu's are in allowed cpu mask */
	if (!cpumask_subset(&cpu_mask, &hotplug_dev->allowed_cpus))
		return 0;

	/* online cpu's to offline */
	for_each_clear_bit(cpu_id, &u32_mask, num_possible_cpus())
			cpu_down_manager(cpu_id, THS_HOTPLUG_ID);

	/* offline cpu's to online */
	for_each_set_bit(cpu_id, &u32_mask, num_possible_cpus())
			cpu_up_manager(cpu_id, THS_HOTPLUG_ID);

	return 0;
}

/* Bind hotplug callbacks to thermal cooling device ops */
static struct thermal_cooling_device_ops const hotplug_cooling_ops = {
	.get_max_state = hotplug_get_max_state,
	.get_cur_state = hotplug_get_cur_state,
	.set_cur_state = hotplug_set_cur_state,
};

struct thermal_cooling_device *hotplug_cooling_register(
		const struct cpumask *allowed_cpus)
{
	struct thermal_cooling_device *cdev;
	struct hotplug_cooling_device *hotplug_dev;
	char dev_name[THERMAL_NAME_LENGTH];
	int ret = 0;

	/* Is allowed cpus passed valid */
	ret = cpumask_subset(allowed_cpus, cpu_possible_mask);
	if (!ret)
		return ERR_PTR(-EINVAL);

	hotplug_dev = kzalloc(sizeof(struct hotplug_cooling_device),
			      GFP_KERNEL);
	if (!hotplug_dev)
		return ERR_PTR(-ENOMEM);

	cpumask_copy(&hotplug_dev->allowed_cpus, allowed_cpus);

	ret = get_idr(&hotplug_idr, &hotplug_dev->id);
	if (ret)
		goto hotplug_err;

	snprintf(dev_name, sizeof(dev_name), "thermal-hotplug-%d",
		 hotplug_dev->id);

	cdev = thermal_cooling_device_register(dev_name, hotplug_dev,
						   &hotplug_cooling_ops);
	if (!cdev)
		goto idr_err;

	return cdev;

idr_err:
	release_idr(&hotplug_idr, hotplug_dev->id);
hotplug_err:
	kfree(hotplug_dev);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(hotplug_cooling_register);

void hotplug_cooling_unregister(struct thermal_cooling_device *cdev)
{
	struct hotplug_cooling_device *hotplug_dev = cdev->devdata;

	thermal_cooling_device_unregister(cdev);
	release_idr(&hotplug_idr, hotplug_dev->id);
	kfree(hotplug_dev);
}
EXPORT_SYMBOL_GPL(hotplug_cooling_unregister);
