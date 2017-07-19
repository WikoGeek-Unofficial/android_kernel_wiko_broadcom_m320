/*****************************************************************************
 *  Copyright 2001 - 2014 Broadcom Corporation.  All rights reserved.
 *
 *  Unless you and Broadcom execute a separate written software license
 *  agreement governing use of this software, this software is licensed to you
 *  under the terms of the GNU General Public License version 2, available at
 *  http://www.gnu.org/licenses/old-license/gpl-2.0.html (the "GPL").
 *
 *  Notwithstanding the above, under no circumstances may you combine this
 *  software in any way with any other Broadcom software provided under a
 *  license other than the GPL, without Broadcom's express prior written
 *  consent.
 *
 *****************************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif
#include <linux/stringify.h>
#include <linux/io.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/bcm.h>
#ifdef CONFIG_SEC_CHARGING_FEATURE
#include <linux/spa_power.h>
#endif

static int charging_enable = 1;
module_param_named(charging_enable, charging_enable, int,
		S_IRUGO | S_IWUSR | S_IWGRP);

static int debug_mask = BCM_PRINT_ERROR | BCM_PRINT_INIT | \
			BCM_PRINT_FLOW;

#define pr_accy(debug_level, args...) \
	do { \
		if (debug_mask & BCM_PRINT_##debug_level) { \
			pr_info("[ACCY]:"args); \
		} \
	} while (0)


#ifdef CONFIG_DEBUG_FS
#define DEBUG_FS_PERMISSIONS	(S_IRUSR | S_IWUSR | S_IROTH | S_IRGRP)
#endif

#define MAX_EVENTS              100
#define MIN_CURR		2

struct bcm_accy_spa_event_map {
	u32 accy_event;
	u32 spa_event;
};

struct event_notifier {
	u32 event_id;
	struct blocking_notifier_head notifiers;
};
struct bcm_accy_event {
	struct list_head node;
	int event;
	int *data;
};

struct bcm_accy_data {
	/* event notifier */
	struct event_notifier event[PMU_EVENT_MAX];
	struct bcm_accy_event event_pool[MAX_EVENTS];
	struct workqueue_struct *wq;
	struct delayed_work evt_notify_work;
	struct mutex accy_mutex;
	struct list_head event_pending_list;
	struct list_head event_free_list;
	struct dentry *bcm_accy_dent_dir;
	spinlock_t accy_lock;
	enum power_supply_type chrgr_type;
	int charging_curr;
	int usb_host_en;
	int v_float;
	bool icc_host_ctrl;
};
struct bcm_accy_data *di;
static atomic_t drv_init_done;

static struct bcm_agent_fn bcm_agent_fn[BCM_AGENT_MAX];
static int bcm_accy_queue_event(struct bcm_accy_data *di,
		int event, u32 *data);

int bcm_add_notifier(u32 event_id, struct notifier_block *notifier)
{

	pr_accy(FLOW, "%s: event add:%d\n", __func__, event_id);

	if (!di) {
		pr_err("%s: BCM ACCY core driver is not initialized\n",
				__func__);
		return -EAGAIN;
	}

	if (unlikely(event_id >= PMU_EVENT_MAX)) {
		pr_err("%s: Invalid event id\n", __func__);
		return -EINVAL;
	}
	return blocking_notifier_chain_register(
			&di->event[event_id].notifiers, notifier);
}
EXPORT_SYMBOL_GPL(bcm_add_notifier);

int bcm_remove_notifier(u32 event_id, struct notifier_block *notifier)
{

	if (!di) {
		pr_err("%s: BCM ACCY core driver is not initialized\n",
				__func__);
		return -EAGAIN;
	}

	if (unlikely(event_id >= PMU_EVENT_MAX)) {
		pr_err("%s: Invalid event id\n", __func__);
		return -EINVAL;
	}
	return blocking_notifier_chain_unregister(
			&di->event[event_id].notifiers, notifier);
}
EXPORT_SYMBOL_GPL(bcm_remove_notifier);

int bcm_call_notifier(enum bcm_event_t event_id, void *para)
{
	if (!di) {
		pr_err("%s: BCM ACCY core driver is not initialized\n",
				__func__);
		return -EAGAIN;
	}
	pr_accy(FLOW, "%s: event send %d\n", __func__, event_id);
	return blocking_notifier_call_chain(&di->event[event_id].notifiers,
			event_id, para);
}
EXPORT_SYMBOL_GPL(bcm_call_notifier);

int bcm_agent_register(unsigned int agent_id, void *fn, const char *agent_name)
{
	int ret = 0;

	if (agent_id >= BCM_AGENT_MAX) {
		pr_accy(INIT, "%s: id:%d is wrong, failed to register\n",
				__func__, agent_id);
		return -1;
	}
	if (fn == NULL) {
		pr_accy(INIT, "%s: fn is NULL failed to register id:%d\n",
				__func__, agent_id);
		return -1;
	}

	if (bcm_agent_fn[agent_id].fn.dummy == NULL) {
		bcm_agent_fn[agent_id].fn.dummy = (int (*)(void))fn;
		bcm_agent_fn[agent_id].agent_name = (char *)agent_name;
		pr_accy(INIT, "%s : id:%d has been successfully registered\n",
				__func__, agent_id);
		pr_accy(INIT, "%s : agent_name = %s\n",
				__func__, bcm_agent_fn[agent_id].agent_name);
	} else {
		pr_accy(INIT, "%s : id:%d is already registered\n",
				__func__, agent_id);
		return -1;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(bcm_agent_register);
int bcm_agent_set(int ctrl, unsigned long data)
{
	int ret = 0;
	int enable = 0;

	pr_accy(VERBOSE, "%s\n", __func__);

	switch (ctrl) {
	case BCM_AGENT_SET_CHARGE:
		pr_accy(FLOW, "BCM_AGENT_SET_CHARGE, data: %lu\n", data);
		enable = data;
		if (bcm_agent_fn[BCM_AGENT_SET_CHARGE].fn.set_charge) {
			ret = bcm_agent_fn[BCM_AGENT_SET_CHARGE].fn
				.set_charge(enable);

		} else {
			WARN_ON(1);
			ret = -ENODATA;
		}
		break;

	case BCM_AGENT_SET_CHARGE_CURRENT:
		pr_accy(FLOW, "BCM_AGENT_SET_CHARGE_CURRENT, data: %lu\n",
				data);

		if (bcm_agent_fn[BCM_AGENT_SET_CHARGE_CURRENT].fn.
				set_charge_current) {
			ret = bcm_agent_fn[BCM_AGENT_SET_CHARGE_CURRENT].fn.
				set_charge_current(data);
			di->charging_curr = data;
		} else {
			WARN_ON(1);
			ret = -ENODATA;
		}
		break;

	case BCM_AGENT_SET_CHARGE_VOLTAGE:
		pr_accy(FLOW, "BCM_AGENT_SET_CHARGE_VOLTAGE, data: %lu\n",
				data);

		if (bcm_agent_fn[BCM_AGENT_SET_CHARGE_VOLTAGE].fn.
				set_charge_voltage) {
			ret = bcm_agent_fn[BCM_AGENT_SET_CHARGE_VOLTAGE].fn.
				set_charge_voltage(data);
		} else {
			WARN_ON(1);
			ret = -ENODATA;
		}
		break;
	case BCM_AGENT_CHRGR_INSERT:
		pr_accy(FLOW, "BCM_AGENT_CHRGR_INSERT, data: %lu\n",
				data);

		if (bcm_agent_fn[BCM_AGENT_CHRGR_INSERT].fn.chrgr_insert) {
			ret = bcm_agent_fn[BCM_AGENT_CHRGR_INSERT].fn.
				chrgr_insert(data);
		} else {
			WARN_ON(1);
			ret = -ENODATA;
		}
		break;
	case BCM_AGENT_SET_FG_TEMP:

		if (bcm_agent_fn[BCM_AGENT_SET_FG_TEMP].fn.set_fg_temp) {
			ret = bcm_agent_fn[BCM_AGENT_SET_FG_TEMP].fn.
			set_fg_temp(data);
		} else
			ret = -ENODATA;
		break;
	case BCM_AGENT_SET_FG_RECHG_SOC:
		if (bcm_agent_fn[BCM_AGENT_SET_FG_RECHG_SOC].fn
		.set_fg_rechg_soc) {
			ret = bcm_agent_fn[BCM_AGENT_SET_FG_RECHG_SOC].fn.
			set_fg_rechg_soc(data);
		} else
			ret = -ENODATA;
		break;
	default:
		pr_accy(FLOW, "ctrl: %d, data: %lu\n", ctrl, data);
		WARN_ON(1);
		ret = -EINVAL;
		break;
	}
	return ret;

}
EXPORT_SYMBOL_GPL(bcm_agent_set);

int bcm_agent_get(int ctrl, void *data)
{
	int ret = 0;

	switch (ctrl) {
	case BCM_AGENT_GET_SOC:
		if (bcm_agent_fn[BCM_AGENT_GET_SOC].fn.get_fg_soc) {
			ret = bcm_agent_fn[BCM_AGENT_GET_SOC].fn
			.get_fg_soc();
		} else
			ret = -ENODATA;
		break;
	case BCM_AGENT_GET_RAW_SOC:
		if (bcm_agent_fn[BCM_AGENT_GET_RAW_SOC].fn.get_fg_raw_soc) {
			ret = bcm_agent_fn[BCM_AGENT_GET_RAW_SOC].fn
			.get_fg_raw_soc();
		} else
			ret = -ENODATA;
		break;
	case BCM_AGENT_GET_VCELL:
		if (bcm_agent_fn[BCM_AGENT_GET_VCELL].fn.get_fg_vcell) {
			ret = bcm_agent_fn[BCM_AGENT_GET_VCELL].fn
			.get_fg_vcell();
		} else
			ret = -ENODATA;
		break;
	case BCM_AGENT_GET_FG_THRESHOLD_SOC:
		if (bcm_agent_fn[BCM_AGENT_GET_FG_THRESHOLD_SOC].fn.
			get_fg_threshold_soc) {
			ret = bcm_agent_fn[BCM_AGENT_GET_FG_THRESHOLD_SOC].fn
			.get_fg_threshold_soc();
		} else
			ret = -ENODATA;
		break;
	case BCM_AGENT_GET_FG_MAX_SOC:
		if (bcm_agent_fn[BCM_AGENT_GET_FG_MAX_SOC].fn.get_fg_max_soc) {
			ret = bcm_agent_fn[BCM_AGENT_GET_FG_MAX_SOC].fn
			.get_fg_max_soc();
		} else
			ret = -ENODATA;
		break;
	case BCM_AGENT_GET_FG_RECHG_SOC:
		if (bcm_agent_fn[BCM_AGENT_GET_FG_RECHG_SOC].fn.
			get_fg_rechg_soc) {
			ret = bcm_agent_fn[BCM_AGENT_GET_FG_RECHG_SOC].fn
				.get_fg_rechg_soc();
		} else
			ret = -ENODATA;
		break;
	case BCM_AGENT_GET_FG_BKCHG_SOC:
		if (bcm_agent_fn[BCM_AGENT_GET_FG_BKCHG_SOC].fn
			.get_fg_bkchg_soc) {
			ret = bcm_agent_fn[BCM_AGENT_GET_FG_BKCHG_SOC].fn
			.get_fg_bkchg_soc();
		} else
			ret = -ENODATA;
		break;
	case BCM_AGENT_GET_FG_FTCHG_SOC:
		if (bcm_agent_fn[BCM_AGENT_GET_FG_FTCHG_SOC].fn
			.get_fg_ftchg_soc) {
			ret = bcm_agent_fn[BCM_AGENT_GET_FG_FTCHG_SOC].fn
				.get_fg_ftchg_soc();
		} else
			ret = -ENODATA;
		break;

	case BCM_AGENT_GET_FG_PRIVATE:
		if (bcm_agent_fn[BCM_AGENT_GET_FG_PRIVATE].fn
			.get_fg_private) {
			ret = bcm_agent_fn[BCM_AGENT_GET_FG_PRIVATE].fn
			.get_fg_private();
		} else
			ret = 0;
		break;
#ifdef custom_t_BATTERY
	case BCM_AGENT_GET_CHARGE_STATE:
		if (bcm_agent_fn[BCM_AGENT_GET_CHARGE_STATE].fn
			.get_charge_state) {
			ret = bcm_agent_fn[BCM_AGENT_GET_CHARGE_STATE].fn
			.get_charge_state();
		} else {
			ret = 0;
		}
		break;
#endif		

	default:
		ret = 0;
		break;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(bcm_agent_get);

int bcm_set_icc_fc(int curr)
{
	int ret;
	pr_accy(VERBOSE, "%s curr:%d\n", __func__, curr);
	if (!atomic_read(&drv_init_done)) {
		pr_accy(ERROR, "%s: accy driver not initialized\n", __func__);
		return -EAGAIN;
	}

	mutex_lock(&di->accy_mutex);
	ret = bcm_agent_set(BCM_AGENT_SET_CHARGE_CURRENT, curr);
	if (ret == -1) {
		WARN_ON(1);
		mutex_unlock(&di->accy_mutex);
		return ret;
	}
	bcm_accy_queue_event(di, PMU_ACCY_EVT_OUT_CHRG_CURR,
			&di->charging_curr);
	mutex_unlock(&di->accy_mutex);
	if (curr <= MIN_CURR) {
		bcm_chrgr_usb_en(0);
		di->icc_host_ctrl = false;
	} else if ((!di->icc_host_ctrl) && (curr > MIN_CURR)) {
		bcm_chrgr_usb_en(1);
		di->icc_host_ctrl = true;
	}
	return ret;
}
EXPORT_SYMBOL(bcm_set_icc_fc);

int bcm_chrgr_usb_en(int enable)
{
	int ret = 0;
	pr_accy(VERBOSE, "%s enable:%d\n", __func__, enable);

	if (!charging_enable)
		return 0;

	if (!atomic_read(&drv_init_done)) {
		pr_accy(ERROR, "%s: accy driver not initialized\n", __func__);
		return -EAGAIN;
	}
	mutex_lock(&di->accy_mutex);
	ret = bcm_agent_set(BCM_AGENT_SET_CHARGE, enable);
	if (ret) {
		WARN_ON(1);
		mutex_unlock(&di->accy_mutex);
		return ret;
	}
	di->usb_host_en = enable;
	ret = bcm_accy_queue_event(di, PMU_CHRGR_EVT_CHRG_STATUS,
			&di->usb_host_en);

	mutex_unlock(&di->accy_mutex);
	return ret;
}
EXPORT_SYMBOL(bcm_chrgr_usb_en);

#ifdef CONFIG_SEC_CHARGING_FEATURE
int bcm_chrgr_en_st(int enable)
{
	int ret = 0;
	pr_accy(FLOW, "%s enable:%d\n", __func__, enable);

	if (!charging_enable)
		return 0;

	if (!atomic_read(&drv_init_done)) {
		pr_accy(ERROR, "%s: accy driver not initialized\n", __func__);
		return -EAGAIN;
	}
	mutex_lock(&di->accy_mutex);

	di->usb_host_en = enable;
	ret = bcm_accy_queue_event(di, PMU_CHRGR_EVT_CHRG_STATUS,
			&di->usb_host_en);

	mutex_unlock(&di->accy_mutex);
	return ret;
}
EXPORT_SYMBOL(bcm_chrgr_en_st);
#endif

int bcm_set_charge_volt(unsigned int mVolt)
{
	int ret;
	pr_accy(FLOW, "%s mVolt:%d\n", __func__, mVolt);
	if (!atomic_read(&drv_init_done)) {
		pr_accy(ERROR, "%s: accy driver not initialized\n", __func__);
		return -EAGAIN;
	}
	mutex_lock(&di->accy_mutex);
	ret = bcm_agent_set(BCM_AGENT_SET_CHARGE_VOLTAGE, mVolt);
	if (ret == -1) {
		WARN_ON(1);
		mutex_unlock(&di->accy_mutex);
		return ret;
	}
	di->v_float = mVolt;
	ret = bcm_accy_queue_event(di, PMU_CHRGR_EVT_SET_CHARGE_VOLTAGE,
			&di->v_float);
	mutex_unlock(&di->accy_mutex);
	return ret;

}
EXPORT_SYMBOL(bcm_set_charge_volt);

int bcm_chrgr_insert(unsigned int insert)
{
	int ret;
	pr_accy(VERBOSE, "%s insert:%d\n", __func__, insert);
	if (!atomic_read(&drv_init_done)) {
		pr_accy(ERROR, "%s: accy driver not initialized\n", __func__);
		return -EAGAIN;
	}
	mutex_lock(&di->accy_mutex);
	ret = bcm_agent_set(BCM_AGENT_CHRGR_INSERT, insert);
	if (ret == -1) {
		WARN_ON(1);
		mutex_unlock(&di->accy_mutex);
		return ret;
	}
	mutex_unlock(&di->accy_mutex);
	return ret;
}
EXPORT_SYMBOL(bcm_chrgr_insert);

static void bcm_accy_evt_notify_work(struct work_struct *work)
{

	struct bcm_accy_event *event_node;
	unsigned long flags;
	int event;
	u32 *data;

	pr_accy(VERBOSE, "%s\n", __func__);
	BUG_ON(!di);
#if 0
	if (!atomic_read(&di->usb_allow_bc_detect)) {
		queue_delayed_work(di->wq, &di->evt_notify_work, 100);
		return;
	}
#endif
	for (; ;) {
		spin_lock_irqsave(&di->accy_lock, flags);
		if (list_empty(&di->event_pending_list)) {
			spin_unlock_irqrestore(&di->accy_lock, flags);
			break;
		}
		event_node = list_first_entry(&di->event_pending_list,
				struct bcm_accy_event, node);
		event = event_node->event;
		data = event_node->data;
		list_del(&event_node->node);
		list_add_tail(&event_node->node, &di->event_free_list);
		spin_unlock_irqrestore(&di->accy_lock, flags);
		pr_accy(VERBOSE, "posting event: %d data:%p\n", event,
				data);
		bcm_call_notifier(event, data);
	}
}

static int bcm_accy_queue_event(struct bcm_accy_data *di,
		int event, u32 *data)
{
	struct bcm_accy_event *event_node;
	unsigned long flags;

	if (!atomic_read(&drv_init_done))
		return -EAGAIN;

	BUG_ON(!di);

	spin_lock_irqsave(&di->accy_lock, flags);

	if (list_empty(&di->event_free_list)) {
		pr_accy(ERROR, "Accy event Q full!!\n");
		spin_unlock_irqrestore(&di->accy_lock, flags);
		return -ENOMEM;
	} else {
		event_node = list_first_entry(&di->event_free_list,
				struct bcm_accy_event, node);
		event_node->event = event;
		event_node->data = data;
		list_del(&event_node->node);
		list_add_tail(&event_node->node, &di->event_pending_list);
	}

	spin_unlock_irqrestore(&di->accy_lock, flags);
	queue_delayed_work(di->wq, &di->evt_notify_work, 0);
	return 0;
}

void bcm_accy_event_handler(int evt, void *data)
{
	pr_accy(FLOW, "%s: evt:%d\n", __func__, evt);
	if (!di)
		BUG_ON(1);

	switch (evt) {
	case PMU_ACCY_EVT_OUT_CHRGR_TYPE:
		di->chrgr_type = (enum power_supply_type)data;
		bcm_accy_queue_event(di, PMU_ACCY_EVT_OUT_CHRGR_TYPE,
				&di->chrgr_type);

		break;
	case PMU_ACCY_EVT_OUT_CHRG_CURR:
#ifndef CONFIG_SEC_CHARGING_FEATURE
		bcm_set_icc_fc((int)data);
#endif
		break;

	}
}

static int bcm_accy_eventq_init(struct bcm_accy_data *di)
{
	int idx;
	BUG_ON(!di);
	INIT_LIST_HEAD(&di->event_free_list);
	INIT_LIST_HEAD(&di->event_pending_list);

	for (idx = 0; idx < MAX_EVENTS; idx++)
		list_add_tail(&di->event_pool[idx].node, &di->event_free_list);

	return 0;
}


#ifdef CONFIG_DEBUG_FS
static int debugfs_set_chrg_curr(void *data, u64 charge_curr)
{
	bcm_set_icc_fc(charge_curr);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(chrg_curr_fops,
		NULL, debugfs_set_chrg_curr, "%llu\n");

static int debugfs_set_chrg_en(void *data, u64 enable)
{
	if (enable == true)
		bcm_chrgr_usb_en(1);
	else if (enable == false)
		bcm_chrgr_usb_en(0);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(chrg_fops,
		NULL, debugfs_set_chrg_en, "%llu\n");

static void bcm_accy_debugfs_init(struct bcm_accy_data *di)
{
	struct dentry *dentry_file;

	pr_accy(FLOW, "%s\n", __func__);
	di->bcm_accy_dent_dir = debugfs_create_dir("bcm-accy", NULL);
	if (!di->bcm_accy_dent_dir) {
		pr_accy(FLOW, "Failed to setup bcm-accy debugfs\n");
		return;
	}

	dentry_file = debugfs_create_file("chrgr_en", DEBUG_FS_PERMISSIONS,
			di->bcm_accy_dent_dir, di, &chrg_fops);
	if (!dentry_file) {
		pr_accy(FLOW, "create chrgr_en fops failed\n");
		goto clean_debugfs;
	}
	dentry_file = debugfs_create_file("chrgr_curr", DEBUG_FS_PERMISSIONS,
			di->bcm_accy_dent_dir, di, &chrg_curr_fops);
	if (!dentry_file) {
		pr_accy(FLOW, "create chrg_curr fops failed\n");
		goto clean_debugfs;
	}

	dentry_file = debugfs_create_u32("debug_mask", DEBUG_FS_PERMISSIONS,
			di->bcm_accy_dent_dir, &debug_mask);
	if (!dentry_file) {
		pr_accy(FLOW, "create debug_mask failed\n");
		goto clean_debugfs;
	}
	return;
clean_debugfs:
	pr_accy(FLOW, "clean_debugfs\n");
	debugfs_remove_recursive(di->bcm_accy_dent_dir);
}

#endif

static int bcm_accy_remove(struct platform_device *pdev)
{
	cancel_delayed_work_sync(&di->evt_notify_work);
	destroy_workqueue(di->wq);
	kfree(di);
	return 0;
}
static int bcm_accy_probe(struct platform_device *pdev)
{
	int i;
	int ret = 0;
	pr_accy(INIT, "%s called\n", __func__);
	di = kzalloc(sizeof(struct bcm_accy_data), GFP_KERNEL);

	if (!di)
		return -ENOMEM;
	di->icc_host_ctrl = true;
	for (i = 0; i < PMU_EVENT_MAX; i++) {
		di->event[i].event_id = i;
		BLOCKING_INIT_NOTIFIER_HEAD(&di->event[i].notifiers);
	}

	di->wq = create_singlethread_workqueue("accy_wq");
	if (!di->wq) {
		pr_accy(INIT, "failed to create workq\n");
		ret = -ENOMEM;
		goto free_mem;
	}

	INIT_DELAYED_WORK(&di->evt_notify_work,
			bcm_accy_evt_notify_work);
	spin_lock_init(&di->accy_lock);
	mutex_init(&di->accy_mutex);
	bcm_accy_eventq_init(di);

	di->chrgr_type = POWER_SUPPLY_TYPE_UNKNOWN;
	atomic_set(&drv_init_done, 1);
	pr_accy(INIT, "%s: success\n", __func__);

#ifdef CONFIG_DEBUG_FS
	bcm_accy_debugfs_init(di);
#endif
	return 0;
free_mem:
	kfree(di);
	return ret;
}
static struct platform_driver bcm_accy_driver = {
	.driver = {
		.name = "bcm_accy",
	},
	.probe = bcm_accy_probe,
	.remove = bcm_accy_remove,
};

static int __init bcm_accy_init(void)
{
	return platform_driver_register(&bcm_accy_driver);
}
subsys_initcall(bcm_accy_init);

static void __exit bcm_accy_exit(void)
{
	platform_driver_unregister(&bcm_accy_driver);
}
module_exit(bcm_accy_exit);

MODULE_DESCRIPTION("Broadcom ACCY Driver");
MODULE_LICENSE("GPL");
