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

#include <linux/clk.h>
#include <linux/cpu_cooling.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif
#include <linux/delay.h>
#include <linux/hwspinlock.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_data/sh_thermal.h>
#include <linux/platform_device.h>
#include <linux/sort.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <mach/r8a7373.h>
#include "hotplug_cooling.h"
#include "sh_thermal_data.h"

/* Flag to control writability of trip threshold limits */
#define THS_ENABLE_DEBUG			1
#if (THS_ENABLE_DEBUG == 1)
#define TRIP_UPDATE_MASK			0x0F
#else
#define TRIP_UPDATE_MASK			0x0
#endif

/*
 * Rounded delay time of 62.5usec (2cycles of RCLK (32KHz)
 * for actual analog reflected
 * */
#define ANALOG_WAIT		63
#define THS_ERR_MARGIN	5
#define HPB_TIMEOUT     1
#define THS_MAX_CNT		2
#define THS0			0
#define THS1			1

#define THS_FILE_PERM		(S_IRUGO | S_IWUSR)
#define TRIP_TEMP(x)		(thermal->pdata->trips[x].temp)
#define TRIP_HOTPLUG(x)		(thermal->pdata->trips[x].hotplug_mask)
#define TRIP_TYPE(x)		(thermal->pdata->trips[x].type)
#define TRIP_MAXFREQ(x)		(thermal->pdata->trips[x].max_freq)

enum {
	E_NORMAL_1 = 1,
	E_NORMAL_2 = 1 << 1,
	E_IDLE	   = 1 << 2,
};

enum {
	THS_LOG_ALERT = 1 << 0,    /*b 00000001*/
	THS_LOG_ERR = 1 << 1,      /*b 00000010*/
	THS_LOG_DBG = 1 << 2,      /*b 00000100*/
	THS_LOG_DBGFS = 1 << 3,    /*b 00001000*/
	THS_LOG_INIT = 1 << 4,     /*b 00010000*/
};
static int ths_dbg_mask = THS_LOG_ALERT | THS_LOG_ERR;

/* ths thermal zone private data */
struct ths_thermal {
	void __iomem *iomem_base;
	struct thermal_zone_device *tz0;
	struct thermal_zone_device *tz1;
	struct thermal_cooling_device *freq_cdev;
	struct thermal_cooling_device *hotplug_cdev;
	struct work_struct tj_work;
	struct thermal_sensor_data *pdata;
	struct clk *ths_clk;
	struct mutex lock;
	enum thermal_device_mode mode;
	int cur_trip; /* Current upper temp being monitored */
	int sensor_mode[THS_MAX_CNT];
	int ths0_temp;
	int irq;
	int ths_disable;
};

/* forward declarations */
static int ths_get_cur_temp(struct ths_thermal *thermal,
			int ths_id, int *cur_temp);
static inline int ths_sensor_enable(struct ths_thermal *thermal,
			bool enable);
static void ths_set_thresholds(struct ths_thermal *thermal,
			int index);

/* thermal framework callbacks */
static int ths0_tz_cdev_bind(struct thermal_zone_device *tz,
		struct thermal_cooling_device *cdev)
{
	struct ths_thermal *thermal = tz->devdata;
	int cidx = -1, hidx = -1, ret = 0, level;

	if (thermal->freq_cdev == cdev) {
		/* Bind cpufreq cooling device to thermal zone */

		for (cidx = 0; cidx < thermal->pdata->trip_cnt; cidx++) {
			if (!TRIP_MAXFREQ(cidx))
				continue;

			level = cpufreq_cooling_get_level(0,
						TRIP_MAXFREQ(cidx));
			if (level == THERMAL_CSTATE_INVALID)
				continue;

			ret = thermal_zone_bind_cooling_device(tz, cidx,
						cdev, level, 0);
			if (ret) {
				ths_dbg(THS_LOG_ERR, "binding cooling device (%s) on trip %d: failed\n",
						cdev->type, cidx);
				goto err_cpu;
			}
		}
	} else if (thermal->hotplug_cdev == cdev) {
		/* Bind hotplug cooling device to thermal zone */

		for (hidx = 0; hidx < thermal->pdata->trip_cnt; hidx++) {
			if (!TRIP_HOTPLUG(hidx))
				continue;

			level = hotplug_cooling_get_level(TRIP_HOTPLUG(hidx));
			if (level == THERMAL_CSTATE_INVALID)
				continue;

			ret = thermal_zone_bind_cooling_device(tz, hidx,
						cdev, level, 0);
			if (ret) {
				ths_dbg(THS_LOG_ERR, "binding cooling device (%s) on trip %d: failed\n",
						cdev->type, hidx);
				goto err_hotplug;
			}
		}
	}

	return ret;

err_hotplug:
	for (; hidx >= 0; --hidx)
		thermal_zone_unbind_cooling_device(tz, hidx, cdev);
	return ret;

err_cpu:
	for (; cidx >= 0; --cidx)
		thermal_zone_unbind_cooling_device(tz, cidx, cdev);

	return ret;
}

static int ths0_tz_cdev_unbind(struct thermal_zone_device *tz,
		struct thermal_cooling_device *cdev)
{
	struct ths_thermal *thermal = tz->devdata;
	int idx, ret = 0;

	if (tz != thermal->tz0)
		return ret;

	for (idx = 0; idx < thermal->pdata->trip_cnt; idx++) {
		ret = thermal_zone_unbind_cooling_device(tz, idx, cdev);
		if (ret)
			ths_dbg(THS_LOG_DBG, "unbinding cooling device (%s) on trip %d: failed\n",
					cdev->type, idx);
	}

	return ret;
}

static int ths0_tz_get_temp(struct thermal_zone_device *tz,
		unsigned long *temp)
{
	struct ths_thermal *thermal = tz->devdata;

	*temp = thermal->ths0_temp;

	return 0;
}

static int ths0_tz_get_trip_temp(struct thermal_zone_device *tz,
		int trip, unsigned long *temp)
{
	struct ths_thermal *thermal = tz->devdata;

	if (trip < 0 || trip >= thermal->pdata->trip_cnt)
		return -EINVAL;

	*temp = TRIP_TEMP(trip);

	return 0;
}

static int ths0_tz_get_trend(struct thermal_zone_device *tz,
		int trip, enum thermal_trend *trend)
{
	struct ths_thermal *thermal = tz->devdata;
	unsigned long trip_temp;
	int ret;

	if (trip < 0 || trip >= thermal->pdata->trip_cnt)
		return -EINVAL;

	ret = ths0_tz_get_trip_temp(tz, trip, &trip_temp);
	if (ret < 0)
		return ret;

	if (thermal->ths0_temp >= trip_temp)
		*trend = THERMAL_TREND_RAISE_FULL;
	else
		*trend = THERMAL_TREND_DROP_FULL;

	return 0;
}

static int ths0_tz_get_mode(struct thermal_zone_device *tz,
		enum thermal_device_mode *mode)
{
	struct ths_thermal *thermal = tz->devdata;

	*mode = thermal->mode;

	return 0;
}

static int ths0_tz_set_mode(struct thermal_zone_device *tz,
		enum thermal_device_mode mode)
{
	struct ths_thermal *thermal = tz->devdata;

	if (thermal->mode == mode)
		return 0;

	if (mode == THERMAL_DEVICE_ENABLED)
		ths_sensor_enable(thermal, true);
	else
		ths_sensor_enable(thermal, false);

	return 0;
}

static int ths0_tz_get_trip_type(struct thermal_zone_device *tz,
		int trip, enum thermal_trip_type *type)
{
	struct ths_thermal *thermal = tz->devdata;

	if (trip < 0 || trip >= thermal->pdata->trip_cnt)
		return -EINVAL;

	*type = TRIP_TYPE(trip);

	return 0;
}

static int ths0_tz_set_trip_temp(struct thermal_zone_device *tz,
		int trip, unsigned long temp)
{
	struct ths_thermal *thermal = tz->devdata;

	if (trip < 0 || trip >= thermal->pdata->trip_cnt)
		return -EINVAL;

	mutex_lock(&thermal->lock);
	TRIP_TEMP(trip) = temp;
	/* check if requested trip temp is the current theshold set */
	if (trip == thermal->cur_trip || trip == thermal->cur_trip-1)
		ths_set_thresholds(thermal, thermal->cur_trip);
	mutex_unlock(&thermal->lock);

	return 0;
}

static int ths0_tz_get_crit_temp(struct thermal_zone_device *tz,
			unsigned long *temp)
{
	struct ths_thermal *thermal = tz->devdata;
	int idx = thermal->pdata->trip_cnt-1;
	long crit_temp = 0;

	for (; idx < 0; idx--) {
		if (TRIP_TYPE(idx) == THERMAL_TRIP_CRITICAL) {
			crit_temp = TRIP_TEMP(idx);
			break;
		}
	}
	*temp = crit_temp;

	return 0;
}

static struct thermal_zone_device_ops ths0_tz_ops = {
	.bind = ths0_tz_cdev_bind,
	.unbind = ths0_tz_cdev_unbind,
	.get_temp = ths0_tz_get_temp,
	.get_trend = ths0_tz_get_trend,
	.get_mode = ths0_tz_get_mode,
	.set_mode = ths0_tz_set_mode,
	.get_trip_type = ths0_tz_get_trip_type,
	.get_trip_temp = ths0_tz_get_trip_temp,
	.set_trip_temp = ths0_tz_set_trip_temp,
	.get_crit_temp = ths0_tz_get_crit_temp,
};

static int ths1_tz_get_temp(struct thermal_zone_device *tz,
		unsigned long *temp)
{
	struct ths_thermal *thermal = tz->devdata;

	return ths_get_cur_temp(thermal, THS1, (int *)temp);
}

static struct thermal_zone_device_ops ths1_tz_ops = {
	.get_temp = ths1_tz_get_temp,
};

/* THS thermal helper functions */
static inline void ths_modify_register_32(void __iomem *addr,
		u32 set_value, u32 clear_mask)
{
	u32 value;
	value = ioread32(addr);
	value &= ~clear_mask;
	value |= set_value;
	iowrite32(value, addr);
}

static inline void ths_set_register_32(void __iomem *addr, u32 value)
{
	iowrite32(value, addr);
}

static inline u32 ths_get_register_32(void __iomem *addr)
{
	return  ioread32(addr);
}

static int ths_get_cur_temp(struct ths_thermal *thermal,
		int ths_id, int *cur_temp)
{
	int ret = 0;
	u32 ctemp = 0;

	if (thermal->sensor_mode[ths_id] == E_IDLE) {
		ths_dbg(THS_LOG_ERR, "THS(%d) device is idle\n", ths_id);
		ret = -ENXIO;
		goto ths_error;
	}

	ctemp = ths_get_register_32(thermal->iomem_base + THSSR(ths_id)) &
					CTEMP_MASK;
	*cur_temp = REG2TEMP(ctemp);

ths_error:

	return ret;
}

static int ths_sensor_set_mode(struct ths_thermal *thermal,
		int ths_id, int mode)
{
	int ret = 0;

	switch (mode) {
	case E_NORMAL_1:	/* Set THS#ths_id to Normal 1 operation */
		ths_modify_register_32(thermal->iomem_base +
				(THSCR(ths_id)), 0, THIDLE1 | THIDLE0);
		break;
	case E_NORMAL_2:	/* Set THS#ths_id to Normal 2 operation */
		ths_modify_register_32(thermal->iomem_base +
				(THSCR(ths_id)), THIDLE1, THIDLE0);
		break;
	case E_IDLE:		/* Set THS#ths_id to Idle operation */
		ths_modify_register_32(thermal->iomem_base +
				(THSCR(ths_id)), THIDLE1 | THIDLE0, 0);
		udelay(ANALOG_WAIT);	/* Wait for actual analog reflected */
		break;
	default:
		ths_dbg(THS_LOG_ERR, "THS(%d) invalid mode\n", ths_id);
		ret = -EINVAL;
	}

	return ret;
}

static int ths_sensor_set_mode_update(struct ths_thermal *thermal,
		int ths_id, int mode)
{
	int ret;

	ret = ths_sensor_set_mode(thermal, ths_id, mode);
	if (!ret)
		thermal->sensor_mode[ths_id] = mode;

	return ret;
}

static void ths_enable_reset_signal(struct ths_thermal *thermal,
			bool enable)
{
	unsigned int value;
	int ret;

	/* Loop until getting the lock */
	for (;;) {
		/* Take the lock, spin for 1 msec if it's already taken */
		ret = hwspin_lock_timeout_irq(r8a7373_hwlock_sysc, HPB_TIMEOUT);
		if (0 == ret) {
			ths_dbg(THS_LOG_DBG, "HW semaphore get success\n");
			break;
		}
	}

	value  = __raw_readl(RESCNT);
	if (enable) {
		value |= TRESV_MSK;
		thermal->pdata->flags &= THS_ENABLE_RESET;
	} else {
		value &= ~TRESV_MSK;
		thermal->pdata->flags &= ~THS_ENABLE_RESET;
	}
	__raw_writel(value, RESCNT);

	/* Release the lock */
	hwspin_unlock_irq(r8a7373_hwlock_sysc);
}

static void ths_set_thresholds(struct ths_thermal *thermal, int trip)
{
	int low_temp, high_temp;

	if (trip >= thermal->pdata->trip_cnt)
		trip = thermal->pdata->trip_cnt-1;

	low_temp = trip ? TRIP_TEMP(trip-1) - thermal->pdata->hysteresis : 0;
	high_temp = TRIP_TEMP(trip);

	ths_modify_register_32(thermal->iomem_base + INTCTLR0_RW_32B,
			CTEMP1_HEX(high_temp) | CTEMP0_HEX(low_temp),
			CTEMP1_MASK | CTEMP0_MASK);
	thermal->cur_trip = trip;
}

static void ths_set_init_thresholds(struct ths_thermal *thermal)
{
	int idx;

	/* Get current temp to judge which trip to be monitored */
	ths_get_cur_temp(thermal, THS0, &thermal->ths0_temp);

	/* find the current trip index */
	for (idx = 0; idx < thermal->pdata->trip_cnt; idx++)
		if (thermal->ths0_temp < TRIP_TEMP(idx))
			break;
	ths_set_thresholds(thermal, idx);
}

static inline void ths_set_interrupts(struct ths_thermal *thermal,
			bool enable)
{
	if (enable) {
		/* Unmask TJ00, TJ01 & TJ03 interrupts */
		ths_modify_register_32(thermal->iomem_base + INT_MASK_RW_32B,
			0, TJ00INT_MSK | TJ01INT_MSK | TJ03INT_MSK);
	} else {
		/* Mask TJ00, TJ01 & TJ03 interrupts */
		ths_modify_register_32(thermal->iomem_base + INT_MASK_RW_32B,
			TJ00INT_MSK | TJ01INT_MSK | TJ03INT_MSK, 0);
	}
}

static void ths_init_hardware(struct ths_thermal *thermal)
{
	struct thermal_sensor_data *pdata = thermal->pdata;

	/* Disable chattering restraint function */
	ths_set_register_32(thermal->iomem_base + FILONOFF0_RW_32B,
				FILONOFF_CHATTERING_DI);
	ths_set_register_32(thermal->iomem_base + FILONOFF1_RW_32B,
				FILONOFF_CHATTERING_DI);

	/*
	 * Set detection mode for both THSs
	 *	+ Tj1, Tj3 are rising
	 *	+ Tj0 is falling
	 */
	ths_set_register_32(thermal->iomem_base + POSNEG0_RW_32B,
				POSNEG_DETECTION);
	ths_set_register_32(thermal->iomem_base + POSNEG1_RW_32B,
				POSNEG_DETECTION);

	/* Clear Interrupt Status register */
	ths_set_register_32(thermal->iomem_base + STR_RW_32B,
				TJST_ALL_CLEAR);

	/*
	 * Set operation mode for THS0/THS1: Normal 1 mode
	 * and TSC decides a value of CPTAP automatically
	 */
	ths_modify_register_32(thermal->iomem_base + THSCR0_RW_32B,
				CPCTL, THIDLE1 | THIDLE0);
	ths_modify_register_32(thermal->iomem_base + THSCR1_RW_32B,
				CPCTL, THIDLE1 | THIDLE0);

	/* Enable INTDT3, INTDT1, INTDT0 in THS0 and only INTDT3 in THS1 */
	ths_set_register_32(thermal->iomem_base + ENR_RW_32B,
				TJ13_EN | TJ03_EN | TJ01_EN | TJ00_EN);

	/* Set hardware shutdown temperature for TS0 and TS1 */
	ths_set_register_32(thermal->iomem_base + INTCTLR0_RW_32B,
				CTEMP3_HEX(pdata->shutdown_temp));
	ths_set_register_32(thermal->iomem_base + INTCTLR1_RW_32B,
				CTEMP3_HEX(pdata->shutdown_temp));
	ths_enable_reset_signal(thermal, true);

	/*
	 * Mask to output THOUT signal for both THS0/1
	 * Mask to output reset signal when Tj > Tj3 for both THS0/1
	 * If reset config is enabled, reset will be enabled in ISR for Tj3
	 */
	ths_modify_register_32(thermal->iomem_base + PORTRST_MASK_RW_32B,
		TJ13PORT_MSK | TJ03PORT_MSK | TJ03RST_MSK | TJ13RST_MSK, 0);

	/* Wait for THS operating */
	udelay(300);	/* 300us */

	ths_set_init_thresholds(thermal);

	/* Mask Tj0/1/2/3 in THS1 to not output them to INTC */
	ths_modify_register_32(thermal->iomem_base + INT_MASK_RW_32B,
			TJ13INT_MSK | TJ12INT_MSK | TJ11INT_MSK | TJ10INT_MSK,
			TJ00INT_MSK | TJ01INT_MSK | TJ03INT_MSK);

	ths_set_register_32(thermal->iomem_base + STR_RW_32B, TJST_ALL_CLEAR);
}

static int ths_sensor_enable(struct ths_thermal *thermal,
			bool enable)
{
	int ret = 0;

	if (enable) {
		ret = clk_enable(thermal->ths_clk);
		if (ret < 0)
			ths_dbg(THS_LOG_ERR, "Failed to enable ths clock\n");
	} else {
		clk_disable(thermal->ths_clk);
	}

	return ret;
}

static void ths_work_tj(struct work_struct *work)
{
	struct ths_thermal *thermal = container_of(work, struct ths_thermal,
								tj_work);
	int intr_status;
	int update_tz = false;

	mutex_lock(&thermal->lock);
	intr_status = ths_get_register_32(thermal->iomem_base + STR_RW_32B);

	if ((TJ03ST == (intr_status & TJ03ST)) ||
		(TJ13ST == (intr_status & TJ13ST))) {
		/* INTDT3 interrupt occured, reset device */
		/* Un-mask INTDT3 interrupt to output to INTC(THS0) */
		ths_dbg(THS_LOG_ERR, "Soc shutdown temp is reached. Cur temp is %dC\n",
					thermal->ths0_temp);
		if (thermal->pdata->flags & THS_ENABLE_RESET) {
			ths_dbg(THS_LOG_ERR, "!!!!OVERHEAT-SHUTTING DOWN!!!!\n");
			ths_modify_register_32(
				thermal->iomem_base + PORTRST_MASK_RW_32B,
				TJ13PORT_MSK | TJ03PORT_MSK,
				TJ13RST_MSK | TJ03RST_MSK);
		}
	} else if (TJ01ST == (intr_status & TJ01ST)) {
		/* INTDT1 interrupt, increasing trend*/
		ths_dbg(THS_LOG_ERR, "Soc temp is raising. Cur temp is %dC\n",
					thermal->ths0_temp);
		ths_set_thresholds(thermal, ++thermal->cur_trip);
		update_tz = true;

	} else if (TJ00ST == (intr_status & TJ00ST)) {
		/* INTDT0 interrupt, decreasing trend*/

		/*
		 * THS triggers interrupt with +/-5C margin. In order
		 * to make thermal framework function as expected,
		 * do error correction.
		 **/
		if (thermal->ths0_temp == TRIP_TEMP(thermal->cur_trip - 1) -
				thermal->pdata->hysteresis + THS_ERR_MARGIN)
			thermal->ths0_temp -= THS_ERR_MARGIN;

		ths_dbg(THS_LOG_ERR, "Soc temp is falling. Cur temp is %dC\n",
					thermal->ths0_temp);
		ths_set_thresholds(thermal, --thermal->cur_trip);
		update_tz = true;
	}

	/* For RSTFLG bit - HRM mentions as "It's cleared in 0 writing
	 * after Tj*ST bits in STR register were cleared". It was observed
	 * that this bit was not reset when the entire register was reset
	 * once. So calling the same function to clear RSTFLG bit
	 * */
	ths_set_register_32(thermal->iomem_base + STR_RW_32B, TJST_ALL_CLEAR);
	ths_set_register_32(thermal->iomem_base + STR_RW_32B, TJST_ALL_CLEAR);

	/* hint thermal framework to do throttling */
	if (update_tz)
		thermal_zone_device_update(thermal->tz0);

	mutex_unlock(&thermal->lock);
	enable_irq(thermal->irq);
}

static irqreturn_t ths_isr(int irq, void *data)
{
	struct ths_thermal *thermal = data;

	if (!ths_get_register_32(thermal->iomem_base + STR_RW_32B))
		return IRQ_HANDLED;

	disable_irq_nosync(irq);
	ths_get_cur_temp(thermal, THS0, &thermal->ths0_temp);
	schedule_work(&thermal->tj_work);

	return IRQ_HANDLED;
}

/* debug fs functions */
#ifdef CONFIG_DEBUG_FS

static int debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t debugfs_show_table(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	struct ths_thermal *thermal = file->private_data;
	char out_str[500];
	u32 len = 0;
	int idx;

	memset(out_str, 0, sizeof(out_str));

	len += snprintf(out_str+len, sizeof(out_str)-len,
			"THS trip table:\n");
	len += snprintf(out_str+len, sizeof(out_str)-len,
		"Trip no.\tTemperature\tMax freq\tHotplug bit mask\n");
	for (idx = 0; idx < thermal->pdata->trip_cnt; idx++)
		len += snprintf(out_str+len, sizeof(out_str)-len,
			"  %d\t\t%d\t\t%d\t\t%lu\n", idx, TRIP_TEMP(idx),
				TRIP_MAXFREQ(idx), TRIP_HOTPLUG(idx));
	len += snprintf(out_str+len, sizeof(out_str)-len,
		"To Update table, use the below format\n");
	len += snprintf(out_str+len, sizeof(out_str)-len,
		"echo <trip no.> <temp> > set_trip_temp\n");
	len += snprintf(out_str+len, sizeof(out_str)-len,
		"echo <trip no.> <freq> > set_trip_freq\n");
	len += snprintf(out_str+len, sizeof(out_str)-len,
		"echo <trip no.> <hotplug mask> > set_trip_hotplug\n");
	len += snprintf(out_str+len, sizeof(out_str)-len,
	"Note:Temp should be in ascending order,");
	len += snprintf(out_str+len, sizeof(out_str)-len,
		"freq and hotplug bitmask should be in descending order\n");
	return simple_read_from_buffer(user_buf, count, ppos, out_str, len);
}

static const struct file_operations debugfs_table_fops = {
	.open = debugfs_open,
	.read = debugfs_show_table,
};

static ssize_t debugfs_set_temp(struct file *file,
			char const __user *buf, size_t count, loff_t *offset)
{
	struct ths_thermal *thermal = file->private_data;
	unsigned int cnt = thermal->pdata->trip_cnt;
	u32 idx, temp = 0;
	char input_str[100];

	memset(input_str, 0, 100);

	if (copy_from_user(input_str, buf, count))
		return -EFAULT;

	sscanf(input_str, "%d%d", &idx, &temp);

	if (idx < 0 || idx >= cnt) {
		ths_dbg(THS_LOG_ERR, "Invalid Trip count\n");
		return count;
	}

	if (((idx > 0) && (temp < TRIP_TEMP(idx-1))) ||
		((idx < cnt - 1) && (temp > TRIP_TEMP(idx+1)))) {
		ths_dbg(THS_LOG_ERR, "Temperatue not in ascending order\n");
		return count;
	}
	ths0_tz_set_trip_temp(thermal->tz0, idx, temp);

	return count;
}

static const struct file_operations debugfs_set_trip_temp_fops = {
	.open = debugfs_open,
	.write = debugfs_set_temp,
};

static ssize_t debugfs_set_freq(struct file *file,
			char const __user *buf, size_t count, loff_t *offset)
{
	struct ths_thermal *thermal = file->private_data;
	u32 idx, freq = 0;
	int level = 0;
	char input_str[100];

	memset(input_str, 0, 100);

	if (copy_from_user(input_str, buf, count))
		return -EFAULT;

	sscanf(input_str, "%d%d", &idx, &freq);

	if (idx < 0 || idx >= thermal->pdata->trip_cnt) {
		ths_dbg(THS_LOG_ERR, "Invalid Trip count\n");
		return count;
	}

	level = cpufreq_cooling_get_level(0, freq);
	if (level == THERMAL_CSTATE_INVALID) {
		ths_dbg(THS_LOG_ERR, "No match for entered frequency\n");
		return -EINVAL;
	}

	mutex_lock(&thermal->lock);
	thermal_zone_device_unregister(thermal->tz0);
	TRIP_MAXFREQ(idx) = freq;

	thermal->tz0 = thermal_zone_device_register("ths0",
			thermal->pdata->trip_cnt, TRIP_UPDATE_MASK, thermal,
			&ths0_tz_ops, NULL, 0, 0);
	if (IS_ERR(thermal->tz0)) {
		ths_dbg(THS_LOG_ERR, "thermal zone THS0 registration failed\n");
		return PTR_ERR(thermal->tz0);
	}

	mutex_unlock(&thermal->lock);

	return count;
}

static const struct file_operations debugfs_set_trip_freq_fops = {
	.open = debugfs_open,
	.write = debugfs_set_freq,
};

static ssize_t debugfs_set_hotplug(struct file *file,
			char const __user *buf, size_t count, loff_t *offset)
{
	struct ths_thermal *thermal = file->private_data;
	u32 idx, hotplug_bitmask = 0;
	int level;
	char input_str[100];

	memset(input_str, 0, 100);

	if (copy_from_user(input_str, buf, count))
		return -EFAULT;

	sscanf(input_str, "%d%d", &idx, &hotplug_bitmask);

	if (idx < 0 || idx >= thermal->pdata->trip_cnt) {
		ths_dbg(THS_LOG_ERR, "Invalid Trip count\n");
		return count;
	}

	level = hotplug_cooling_get_level(hotplug_bitmask);
	if (level == THERMAL_CSTATE_INVALID) {
		ths_dbg(THS_LOG_ERR, "Invalid hotplug bitmask\n");
		return -EINVAL;
	}

	mutex_lock(&thermal->lock);
	thermal_zone_device_unregister(thermal->tz0);
	TRIP_HOTPLUG(idx) = hotplug_bitmask;

	thermal->tz0 = thermal_zone_device_register("ths0",
			thermal->pdata->trip_cnt, TRIP_UPDATE_MASK, thermal,
			&ths0_tz_ops, NULL, 0, 0);
	if (IS_ERR(thermal->tz0)) {
		ths_dbg(THS_LOG_ERR, "thermal zone THS0 registration failed\n");
		return PTR_ERR(thermal->tz0);
	}

	mutex_unlock(&thermal->lock);

	return count;
}

static const struct file_operations debugfs_set_trip_hotplug_fops = {
	.open = debugfs_open,
	.write = debugfs_set_hotplug,
};

static int debugfs_get_ths_disable(void *data, u64 *disable)
{
	struct ths_thermal *thermal = data;

	*disable = !!thermal->ths_disable;

	return 0;
}

static int debugfs_set_ths_disable(void *data, u64 disable)
{
	struct ths_thermal *thermal = data;

	if (thermal->ths_disable == !!disable)
		return 0;

	if (disable) {
		ths_sensor_set_mode(thermal, THS0, E_IDLE);
		ths_sensor_set_mode(thermal, THS1, E_IDLE);

		ths_set_interrupts(thermal, false);
		cancel_work_sync(&thermal->tj_work);
		ths_sensor_enable(thermal, false);
	} else {
		ths_sensor_enable(thermal, true);
		ths_sensor_set_mode_update(thermal, THS0, E_NORMAL_1);
		ths_sensor_set_mode_update(thermal, THS1, E_NORMAL_1);
		/* Wait for THS operating */
		udelay(300);
		ths_set_init_thresholds(thermal);
		ths_set_interrupts(thermal, true);
		thermal_zone_device_update(thermal->tz0);
	}

	mutex_lock(&thermal->lock);
	thermal->ths_disable = !!disable;
	mutex_unlock(&thermal->lock);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debugfs_ths_disable_fops,
		debugfs_get_ths_disable, debugfs_set_ths_disable, "%llu\n");

static int debugfs_get_reset(void *data, u64 *reset)
{
	struct ths_thermal *thermal = data;

	*reset = thermal->pdata->flags & THS_ENABLE_RESET;

	return 0;
}

static int debugfs_set_reset(void *data, u64 reset)
{
	struct ths_thermal *thermal = data;

	if (reset)
		ths_enable_reset_signal(thermal, true);
	else
		ths_enable_reset_signal(thermal, false);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debugfs_reset_fops,
		debugfs_get_reset, debugfs_set_reset, "%llu\n");

static int debugfs_get_ths0_temp(void *data, u64 *temp)
{
	struct ths_thermal *thermal = data;
	int ret, cur_temp;

	ret = ths_get_cur_temp(thermal, THS0, &cur_temp);
	*temp = cur_temp;

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(debugfs_ths0_temp_fops,
		debugfs_get_ths0_temp, NULL, "%llu\n");

static int debugfs_get_ths1_temp(void *data, u64 *temp)
{
	struct ths_thermal *thermal = data;
	int ret, cur_temp;

	ret = ths_get_cur_temp(thermal, THS1, &cur_temp);
	*temp = cur_temp;

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(debugfs_ths1_temp_fops,
		debugfs_get_ths1_temp, NULL, "%llu\n");

static struct dentry *dentry_ths_dir;

static int __init ths_debugfs_init(struct ths_thermal *thermal)
{
	struct dentry *dentry_ths_file;
	int ret = 0;

	dentry_ths_dir = debugfs_create_dir("ths_thermal", NULL);
	if (IS_ERR_OR_NULL(dentry_ths_dir)) {
		ret = PTR_ERR(dentry_ths_dir);
		goto err;
	}

	dentry_ths_file = debugfs_create_file("reset", THS_FILE_PERM,
			dentry_ths_dir, thermal, &debugfs_reset_fops);
	if (IS_ERR_OR_NULL(dentry_ths_file)) {
		ret = PTR_ERR(dentry_ths_file);
		goto err;
	}

	dentry_ths_file = debugfs_create_file("ths0_temp", THS_FILE_PERM,
			dentry_ths_dir, thermal, &debugfs_ths0_temp_fops);
	if (IS_ERR_OR_NULL(dentry_ths_file)) {
		ret = PTR_ERR(dentry_ths_file);
		goto err;
	}

	dentry_ths_file = debugfs_create_file("ths1_temp", THS_FILE_PERM,
			dentry_ths_dir, thermal, &debugfs_ths1_temp_fops);
	if (IS_ERR_OR_NULL(dentry_ths_file)) {
		ret = PTR_ERR(dentry_ths_file);
		goto err;
	}

	dentry_ths_file = debugfs_create_file("trip_table", THS_FILE_PERM,
			dentry_ths_dir, thermal, &debugfs_table_fops);
	if (IS_ERR_OR_NULL(dentry_ths_file)) {
		ret = PTR_ERR(dentry_ths_file);
		goto err;
	}

	dentry_ths_file = debugfs_create_file("set_trip_temp", THS_FILE_PERM,
			dentry_ths_dir, thermal, &debugfs_set_trip_temp_fops);
	if (IS_ERR_OR_NULL(dentry_ths_file)) {
		ret = PTR_ERR(dentry_ths_file);
		goto err;
	}

	dentry_ths_file = debugfs_create_file("set_trip_freq", THS_FILE_PERM,
			dentry_ths_dir, thermal, &debugfs_set_trip_freq_fops);
	if (IS_ERR_OR_NULL(dentry_ths_file)) {
		ret = PTR_ERR(dentry_ths_file);
		goto err;
	}

	dentry_ths_file = debugfs_create_file("set_trip_hotplug", THS_FILE_PERM,
		dentry_ths_dir, thermal, &debugfs_set_trip_hotplug_fops);
	if (IS_ERR_OR_NULL(dentry_ths_file)) {
		ret = PTR_ERR(dentry_ths_file);
		goto err;
	}

	dentry_ths_file = debugfs_create_file("ths_disable", THS_FILE_PERM,
		dentry_ths_dir, thermal, &debugfs_ths_disable_fops);
	if (IS_ERR_OR_NULL(dentry_ths_file)) {
		ret = PTR_ERR(dentry_ths_file);
		goto err;
	}

	dentry_ths_file  = debugfs_create_u32("debug_mask", THS_FILE_PERM,
		dentry_ths_dir, &ths_dbg_mask);
	if (IS_ERR_OR_NULL(dentry_ths_file)) {
		ret = PTR_ERR(dentry_ths_file);
		goto err;
	}

	return ret;

err:
	if (!IS_ERR_OR_NULL(dentry_ths_dir))
		debugfs_remove_recursive(dentry_ths_dir);

	return ret;
}

static void __exit ths_debugfs_exit(struct ths_thermal *thermal)
{
	if (!IS_ERR_OR_NULL(dentry_ths_dir))
		debugfs_remove_recursive(dentry_ths_dir);
}
#else /* CONFIG_DEBUG_FS */

static int __init ths_debugfs_init(struct ths_thermal *thermal)
{
	return 0;
}

static void __exit ths_debugfs_exit(struct ths_thermal *thermal)
{
}
#endif

static struct thermal_sensor_data * __init
ths_thermal_parse_dt(struct device *dev)
{
	struct thermal_sensor_data *data;
	struct device_node *np = dev->of_node;
	char prop_name[32];
	const char *type;
	int i;
	u32 val;

	if (!np)
		return NULL;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	if (of_property_read_u32(np, "shutdown-temperature", &val))
		goto parse_error;
	data->shutdown_temp = val / 1000;

	if (of_property_read_bool(np, "enable-reset"))
		data->flags |= THS_ENABLE_RESET;

	if (of_property_read_u32(np, "hysteresis", &val))
		goto parse_error;
	data->hysteresis = val / 1000;

	if (of_property_read_u32(np, "num-trips", &val))
		goto parse_error;
	data->trip_cnt = val;

	data->trips = devm_kzalloc(dev, data->trip_cnt * sizeof(*data->trips),
			GFP_KERNEL);
	if (!data->trips)
		return NULL;

	for (i = 0; i < data->trip_cnt; i++) {
		sprintf(prop_name, "trip%d-temp", i);
		if (of_property_read_u32(np, prop_name, &val))
			goto parse_error;
		data->trips[i].temp = val / 1000;

		sprintf(prop_name, "trip%d-max-freq", i);
		if (of_property_read_u32(np, prop_name, &val))
			goto parse_error;
		data->trips[i].max_freq = val / 1000;

		sprintf(prop_name, "trip%d-hotplug-mask", i);
		if (of_property_read_u32(np, prop_name, &val))
			goto parse_error;
		data->trips[i].hotplug_mask = val;

		sprintf(prop_name, "trip%d-type", i);
		if (of_property_read_string(np, prop_name, &type))
			goto parse_error;
		if (!strcmp(type, "active"))
			data->trips[i].type = THERMAL_TRIP_ACTIVE;
		else if (!strcmp(type, "passive"))
			data->trips[i].type = THERMAL_TRIP_PASSIVE;
		else if (!strcmp(type, "hot"))
			data->trips[i].type = THERMAL_TRIP_HOT;
		else if (!strcmp(type, "critical"))
			data->trips[i].type = THERMAL_TRIP_CRITICAL;
		else
			goto parse_error;
	}

	return data;

parse_error:
	ths_dbg(THS_LOG_ERR, "Error parsing device node\n");
	return NULL;
}

static int __init ths_thermal_probe(struct platform_device *pdev)
{
	struct ths_thermal *thermal;
	struct thermal_sensor_data *pdata;
	struct resource *res;
	int ret = 0;

	thermal = devm_kzalloc(&pdev->dev, sizeof(*thermal), GFP_KERNEL);
	if (!thermal)
		return -ENOMEM;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		pdata = ths_thermal_parse_dt(&pdev->dev);
	if (!pdata) {
		ths_dbg(THS_LOG_INIT, "No platform data found\n");
		return -EINVAL;
	}
	thermal->pdata = pdata;

	/*
	 * Standard renesas,rcar-thermal DT binding has split resources, so
	 * first resource doesn't technically cover the entire device, but
	 * we get away with it because it must map at least a 4K page.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	thermal->iomem_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(thermal->iomem_base)) {
		ths_dbg(THS_LOG_INIT, "Failed to get I/O memory for ths\n");
		return PTR_ERR(thermal->iomem_base);
	}

	thermal->ths_clk = clk_get(&pdev->dev, "thermal_sensor");
	if (IS_ERR(thermal->ths_clk)) {
		ths_dbg(THS_LOG_INIT, "Failed to get ths clock\n");
		return PTR_ERR(thermal->ths_clk);
	}

	mutex_init(&thermal->lock);
	INIT_WORK(&thermal->tj_work, ths_work_tj);
	platform_set_drvdata(pdev, thermal);

	thermal->sensor_mode[THS0] = E_NORMAL_1;
	thermal->sensor_mode[THS1] = E_NORMAL_1;
	ths_sensor_enable(thermal, true);
	ths_init_hardware(thermal);

	thermal->freq_cdev = cpufreq_cooling_register(cpu_present_mask);
	if (IS_ERR(thermal->freq_cdev)) {
		ths_dbg(THS_LOG_INIT, "cpufreq cooling dev registration failed\n");
		ret = PTR_ERR(thermal->freq_cdev);
		goto err_init;
	}

	thermal->hotplug_cdev = hotplug_cooling_register(cpu_present_mask);
	if (IS_ERR(thermal->hotplug_cdev)) {
		ths_dbg(THS_LOG_INIT, "hotplug cooling dev registration failed\n");
		ret = PTR_ERR(thermal->hotplug_cdev);
		goto err_hotplug_reg;
	}

	/* Register THS0 with thermal framework */
	thermal->tz0  = thermal_zone_device_register("ths0",
			thermal->pdata->trip_cnt, TRIP_UPDATE_MASK, thermal,
			&ths0_tz_ops, NULL, 0, 0);
	if (IS_ERR(thermal->tz0)) {
		ths_dbg(THS_LOG_INIT, "thermal zone THS0 registration failed\n");
		ret = PTR_ERR(thermal->tz0);
		goto err_tz0_reg;
	}

	/* Register THS1 with thermal framework */
	thermal->tz1  = thermal_zone_device_register("ths1",
			0, 0, thermal, &ths1_tz_ops, NULL, 0, 0);
	if (IS_ERR(thermal->tz1)) {
		ths_dbg(THS_LOG_INIT, "thermal zone THS1 registration failed\n");
		ret = PTR_ERR(thermal->tz1);
		goto err_tz1_reg;
	}

	thermal->irq = platform_get_irq(pdev, 0);
	if (thermal->irq < 0) {
		ths_dbg(THS_LOG_INIT, "Failed to get irq\n");
		ret = thermal->irq;
		goto err_tz1_reg;
	}

	ret = devm_request_irq(&pdev->dev, thermal->irq, ths_isr,
			IRQ_LEVEL | IRQF_NO_SUSPEND | IRQF_DISABLED,
			pdev->name, thermal);
	if (ret < 0) {
		ths_dbg(THS_LOG_INIT, "irq registration failed\n");
		goto err_tz1_reg;
	}

	if (ths_debugfs_init(thermal))
		ths_dbg(THS_LOG_DBG, "debugfs init failed\n");

	return ret;

err_tz1_reg:
	thermal_zone_device_unregister(thermal->tz0);
err_tz0_reg:
	cpufreq_cooling_unregister(thermal->freq_cdev);
err_hotplug_reg:
	hotplug_cooling_unregister(thermal->hotplug_cdev);
err_init:
	clk_disable(thermal->ths_clk);
	return ret;
}

static int __exit ths_thermal_remove(struct platform_device *pdev)
{
	struct ths_thermal *thermal = platform_get_drvdata(pdev);

	cpufreq_cooling_unregister(thermal->freq_cdev);
	thermal_zone_device_unregister(thermal->tz0);
	thermal_zone_device_unregister(thermal->tz1);
	ths_sensor_enable(thermal, false);
	ths_debugfs_exit(thermal);
	return 0;
}

static int ths_thermal_suspend(struct platform_device *pdev,
			pm_message_t state)
{
	struct ths_thermal *thermal = platform_get_drvdata(pdev);

	if (thermal->ths_disable)
		return 0;

	/* update the last mode only if Modem CPG clk is OFF */
	if (ioread32(MMSTPCR5) & THS_CLK_SUPPLY_BIT) {
		/* THS is put in idle state, but the internal state is not
		 * updated. So that THS brought back to the state before
		 * entering suspend.
		 * */
		ths_sensor_set_mode(thermal, THS0, E_IDLE);
		ths_sensor_set_mode(thermal, THS1, E_IDLE);
	}

	/* THS seems to trigger spurious interrupts when put in
	 * idle state and clocks are disbaled. So, disable the
	 * interrupts before disabling the clocks. No harm in
	 * doing this, as thermal sensor is anyway not being
	 * monitored in device suspend. Restore them in resume
	 * sequence after THS is reconfigured.
	 * */
	ths_set_interrupts(thermal, false);
	/* cancel pending interrupt work if any */
	cancel_work_sync(&thermal->tj_work);
	ths_sensor_enable(thermal, false);

	return 0;
}

static int ths_thermal_resume(struct platform_device *pdev)
{
	struct ths_thermal *thermal = platform_get_drvdata(pdev);

	if (thermal->ths_disable)
		return 0;

	ths_sensor_enable(thermal, true);
	ths_sensor_set_mode_update(thermal, THS0, thermal->sensor_mode[THS0]);
	ths_sensor_set_mode_update(thermal, THS1, thermal->sensor_mode[THS1]);
	/* Wait for THS operating */
	udelay(300);    /* 300us */
	ths_set_init_thresholds(thermal);
	ths_set_interrupts(thermal, true);
	thermal_zone_device_update(thermal->tz0);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ths_match[] = {
	{ .compatible = "rmobile,ths-thermal", },
	{},
};
#endif

static struct platform_driver ths_thermal_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "thermal_sensor",
		.of_match_table = of_match_ptr(ths_match),
	},
	.remove = __exit_p(ths_thermal_remove),
	.suspend = ths_thermal_suspend,
	.resume = ths_thermal_resume,
};

module_platform_driver_probe(ths_thermal_driver, ths_thermal_probe);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("SH Thermal driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
