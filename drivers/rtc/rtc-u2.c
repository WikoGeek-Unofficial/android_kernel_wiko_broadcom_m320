/*
 * Copyright 2012 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * A copy of the GPL is available at
 * http://www.broadcom.com/licenses/GPLv2.php, or by writing to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
/*
 * Notes:
 * This rtc-u2 is a fake RTC device based on sh cmt, as RTC in PMU
 * can turn on the phone if phone is in shutdown state, so we plan to use it as
 * a power-off alarm device, so a new RTC device is needed for android alarm
 * device.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <mach/cmt.h>
#include <linux/io.h>




#define PRINT_ERROR (1U << 0)
#define PRINT_INIT (1U << 1)
#define PRINT_FLOW (1U << 2)
#define PRINT_DATA (1U << 3)
#define PRINT_INFO (1U << 4)
static int dbg_mask = PRINT_ERROR | PRINT_INIT | PRINT_DATA | PRINT_INFO;
module_param_named(dbg_mask, dbg_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define pr_rtc(debug_level, args...) \
		do { \
			if (dbg_mask & PRINT_##debug_level) { \
				pr_info(args); \
			} \
		} while (0)


#define SEC2COUNT(sec)	((sec<<8)-2)
#define COUNT2SEC(cnt)	((cnt+2)>>8)

struct rtc_u2 {
	void __iomem *base_addr;
	void __iomem *cmclke;
	int clk_bit;
	int irq;
	int alarm_irq_enabled;
	struct rtc_device *rtc;
	struct rtc_device *real_rtc;
};

static int u2_timer_get_counter(struct rtc_u2 *rtc_info)
{
	void __iomem *base;
	unsigned int value;
	int count;

	base = rtc_info->base_addr;

	value = __raw_readl(base + CMCOR);
	count = value - __raw_readl(base + CMCNT);

	return (count > 0) ? count : 0;
}

static int u2_timer_disable_and_clear(struct rtc_u2 *rtc_info)
{
	unsigned long flags, wrflg, i = 0;
	void __iomem *base;
	unsigned int value;

	base = rtc_info->base_addr;

	__raw_readl(base + CMCSR);
	__raw_writel(0x00000186, base + CMCSR);	/* Int disable */
	__raw_writel(0, base + CMCNT);
	__raw_writel(0, base + CMSTR);

	do {
		wrflg = ((__raw_readl(base + CMCSR) >> 13) & 0x1);
		i++;
	} while (wrflg != 0x00 && i < 0xffffffff);

	/* wait at least 3 RCLK cycle */
	udelay(150);
	if (rtc_info->cmclke) {
		spin_lock_irqsave(&cmt_lock, flags);
		value = __raw_readl(rtc_info->cmclke);
		__raw_writel(value & ~(1<<rtc_info->clk_bit), rtc_info->cmclke);
		spin_unlock_irqrestore(&cmt_lock, flags);
	}

	return 0;
}

static int u2_timer_cmt_start(struct rtc_u2 *rtc_info, unsigned long second)
{
	unsigned long flags, wrflg, i = 0;
	void __iomem *base;
	unsigned int value;

	base = rtc_info->base_addr;

	if (rtc_info->cmclke) {
		spin_lock_irqsave(&cmt_lock, flags);
		value = __raw_readl(rtc_info->cmclke);
		__raw_writel(value | (1<<rtc_info->clk_bit), rtc_info->cmclke);
		spin_unlock_irqrestore(&cmt_lock, flags);
	}

	/* wait at least 2 RCLK cycle */
	udelay(100);
	__raw_writel(0, base + CMSTR);
	__raw_writel(0, base + CMCNT);
	__raw_writel(0x000000a6, base + CMCSR);	/* Int enable */

	__raw_writel(SEC2COUNT(second), base + CMCOR);

	do {
		wrflg = ((__raw_readl(base + CMCSR) >> 13) & 0x1);
		i++;
	} while (wrflg != 0x00 && i < 0xffffffff);

	__raw_writel(1, base + CMSTR);

	return 0;
}

static irqreturn_t rtc_u2_irq(int irq, void *dev_id)
{
	struct rtc_u2 *rtc_info = (struct rtc_u2 *)dev_id;
	unsigned int reg_val;
	void __iomem *base;

	base = rtc_info->base_addr;

	reg_val = __raw_readl(base + CMCSR);
	reg_val &= ~0x0000c000U;
	__raw_writel(reg_val, base + CMCSR);

	rtc_update_irq(rtc_info->rtc, 1, RTC_IRQF | RTC_AF);

	pr_rtc(FLOW, "rtc_u2_irq\n");

	return IRQ_HANDLED;
}

static int get_timekeeping_rtc_time(struct rtc_u2 *rtc_info,
	struct rtc_time *tm)
{
	if (!rtc_info->real_rtc)
		rtc_info->real_rtc = rtc_class_open(
			CONFIG_RTC_HCTOSYS_DEVICE);

	if (rtc_info->real_rtc == NULL) {
		struct timespec now;
		/*no rtc avaliable, temply use SW time*/
		pr_rtc(INFO, "use SW time!\n");
		getnstimeofday(&now);
		rtc_time_to_tm(now.tv_sec, tm);
	} else {
		rtc_read_time(rtc_info->real_rtc, tm);
	}

	return 0;
}

static int set_timekeeping_rtc_time(struct rtc_u2 *rtc_info,
	struct rtc_time *tm)
{
	if (!rtc_info->real_rtc)
		rtc_info->real_rtc = rtc_class_open(
			CONFIG_RTC_HCTOSYS_DEVICE);

	if (rtc_info->real_rtc)
		rtc_set_time(rtc_info->real_rtc, tm);
	else
		pr_rtc(INFO, "rtc time will lost after reboot!\n");

	return 0;
}

static int rtc_u2_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc_u2 *rtc_info = dev_get_drvdata(dev);
	int ret = 0;

	get_timekeeping_rtc_time(rtc_info, tm);

	ret = rtc_valid_tm(tm);

	pr_rtc(DATA, "err=%d now time=%d.%d.%d.%d.%d.%d\n", ret,
		tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);
	return ret;
}

static int rtc_u2_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc_u2 *rtc_info = dev_get_drvdata(dev);
	int ret = 0;

	pr_rtc(DATA, "%s: time=%d.%d.%d.%d.%d.%d\n",
		__func__,
		tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	set_timekeeping_rtc_time(rtc_info, tm);

	return ret;
}

static int rtc_u2_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct rtc_u2 *rtc_info = dev_get_drvdata(dev);
	unsigned int count;
	struct rtc_time tm;
	unsigned long now_time;

	count = u2_timer_get_counter(rtc_info);

	get_timekeeping_rtc_time(rtc_info, &tm);

	rtc_tm_to_time(&tm, &now_time);

	rtc_time_to_tm(now_time + COUNT2SEC(count), &alarm->time);

	alarm->enabled = rtc_info->alarm_irq_enabled;

	pr_rtc(DATA, "alarm time=%d.%d.%d.%d.%d.%d\n",
		alarm->time.tm_year, alarm->time.tm_mon, alarm->time.tm_mday,
		alarm->time.tm_hour, alarm->time.tm_min, alarm->time.tm_sec);

	return 0;
}

static int rtc_u2_alarm_irq_enable(struct device *dev,
		unsigned int enabled)
{
	struct rtc_u2 *rtc_info = dev_get_drvdata(dev);
	int ret = 0;

	pr_rtc(FLOW, "alarm %s\n", enabled ? "enabled" : "disabled");
	if (!enabled)
		ret = u2_timer_disable_and_clear(rtc_info);
	rtc_info->alarm_irq_enabled = enabled;
	return ret;
}

static int rtc_u2_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct rtc_u2 *rtc_info = dev_get_drvdata(dev);
	int ret = 0;
	unsigned long alarm_time, now_time;
	unsigned long count;
	struct rtc_time tm;

	pr_rtc(DATA, "%s: time=%d.%d.%d.%d.%d.%d\n", __func__,
		alarm->time.tm_year, alarm->time.tm_mon, alarm->time.tm_mday,
		alarm->time.tm_hour, alarm->time.tm_min, alarm->time.tm_sec);

	rtc_tm_to_time(&alarm->time, &alarm_time);
	get_timekeeping_rtc_time(rtc_info, &tm);
	rtc_tm_to_time(&tm, &now_time);

	if (alarm_time <= now_time) {
		pr_rtc(ERROR, "invalid alarm, now %ld, alarm %ld passed\n",
			now_time, alarm_time);
		return -EINVAL;
	}

	count = alarm_time - now_time;

	pr_rtc(INFO, "%s: set alarm count %ld\n", __func__, count);
	if (alarm->enabled) {
		ret = u2_timer_cmt_start(rtc_info, count);
		if (ret) {
			pr_rtc(ERROR, "set alarm failed!\n");
			return -EINVAL;
		}
	}

	rtc_u2_alarm_irq_enable(dev, alarm->enabled);

	return ret;
}

static struct rtc_class_ops rtc_u2_ops = {
	.read_time		= rtc_u2_read_time,
	.set_time		= rtc_u2_set_time,
	.read_alarm		= rtc_u2_read_alarm,
	.set_alarm		= rtc_u2_set_alarm,
	.alarm_irq_enable	= rtc_u2_alarm_irq_enable,
};

#ifdef CONFIG_LOCKDEP
static struct lock_class_key rtc_ops_mutex_key;
#endif

static int rtc_u2_probe(struct platform_device *pdev)
{
	struct rtc_u2 *rtc_info;
	struct resource *res0, *res1, *ires;
	int ret = 0;

	res0 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res0) {
		dev_err(&pdev->dev, "get cmt-base resource error.\n");
		return -ENODEV;
	}

	res1 = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cmclke");
	if (res1) {
		if (pdev->id < 0 || pdev->id > 7) {
			pr_rtc(ERROR, "wrong clk-enable-bit%d\n", pdev->id);
			return -ENODEV;
		}
	} else
		pr_rtc(INFO, "no cmclke resource.\n");

	ires = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!ires) {
		dev_err(&pdev->dev, "get irq error.\n");
		return -ENODEV;
	}

	rtc_info = devm_kzalloc(&pdev->dev, sizeof(struct rtc_u2), GFP_KERNEL);
	if (!rtc_info)
		return -ENOMEM;

	rtc_info->base_addr = devm_ioremap(&pdev->dev, res0->start,
			resource_size(res0));

	if (res1) {
		rtc_info->cmclke = devm_ioremap(&pdev->dev, res1->start,
				resource_size(res1));
		rtc_info->clk_bit = pdev->id;
	}

	rtc_info->irq = ires->start;

	ret = devm_request_threaded_irq(&pdev->dev, rtc_info->irq, NULL,
			rtc_u2_irq, IRQF_ONESHOT, "RTC-U2", rtc_info);
	if (ret < 0) {
		pr_rtc(ERROR, "request_irq failed err=%d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, rtc_info);
	device_init_wakeup(&pdev->dev, 1);

	rtc_info->rtc = rtc_device_register(pdev->name, &pdev->dev,
		&rtc_u2_ops, THIS_MODULE);
	if (IS_ERR(rtc_info->rtc)) {
		ret = PTR_ERR(rtc_info->rtc);
		pr_rtc(ERROR, "register rtc device failed, %d\n", ret);
		return ret;
	}

#ifdef CONFIG_LOCKDEP
	lockdep_set_class(&rtc_info->rtc->ops_lock, &rtc_ops_mutex_key);
#endif

	pr_rtc(INIT, "rtc-u2 initialized.\n");

	return ret;
}

static int rtc_u2_remove(struct platform_device *pdev)
{
	struct rtc_u2 *rtc_info = platform_get_drvdata(pdev);

	if (!IS_ERR(rtc_info->rtc))
		rtc_device_unregister(rtc_info->rtc);

	if (rtc_info->real_rtc)
		rtc_class_close(rtc_info->real_rtc);

	return 0;
}

static struct platform_driver rtc_u2_driver = {
	.driver = {
		.name = "rtc_u2",
		.owner = THIS_MODULE,
	},
	.probe = rtc_u2_probe,
	.remove = rtc_u2_remove,
};

module_platform_driver(rtc_u2_driver);

MODULE_DESCRIPTION("BCM RTC-U2 driver");
MODULE_LICENSE("GPL");


