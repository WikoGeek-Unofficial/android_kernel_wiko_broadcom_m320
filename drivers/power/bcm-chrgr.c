/*****************************************************************************
 *  Copyright 2001 - 2014 Broadcom Corporation.  All rights reserved.
 *
 *  Unless you and Broadcom execute a separate written software license
 *  agreement governing use of this software, this software is licensed to you
 *  under the terms of the GNU General Public License version 2, available at
 *  http://www.gnu.org/licenses/old-license/gpl-2.0.html (the "GPL").
 *
 *  Notwithstang the above, under no circumstances may you combine this
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
#include <linux/bcm.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>


static u32 debug_mask = BCM_PRINT_ERROR | BCM_PRINT_INIT | BCM_PRINT_FLOW;

#define pr_chrgr(debug_level, args...) \
	do { \
		if (debug_mask & BCM_PRINT_##debug_level) { \
			pr_info("[CHRGR]:"args); \
		} \
	} while (0)

struct bcm_chrgr_info {
	int online;
	int volt_max;
	int curr;
	char *model_name;
};

struct bcm_chrgr_data {
	struct notifier_block chgr_detect;
	struct bcm_chrgr_info ac_chrgr_info;
	struct bcm_chrgr_info usb_chrgr_info;
	struct power_supply ac_psy;
	struct power_supply usb_psy;
	int *chrgr_curr_tbl;
};
struct bcm_chrgr_data *g_chrgr_data;

static enum power_supply_property bcm_chrgr_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

static enum power_supply_property bcm_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

static char *chrgr_names[] = {
	[POWER_SUPPLY_TYPE_UNKNOWN]	= "none",
	[POWER_SUPPLY_TYPE_USB]	= "sdp",
	[POWER_SUPPLY_TYPE_USB_CDP]	= "cdp",
	[POWER_SUPPLY_TYPE_USB_DCP]	= "dcp",
	[POWER_SUPPLY_TYPE_USB_ACA]	= "aca",
	[POWER_SUPPLY_TYPE_BATTERY] = "bat",
};

static int chrgr_curr_lmt_default[] = {
	[POWER_SUPPLY_TYPE_UNKNOWN] = 0,
	[POWER_SUPPLY_TYPE_BATTERY] = 0,
	[POWER_SUPPLY_TYPE_UPS] = 0,
	[POWER_SUPPLY_TYPE_MAINS] = 1300,
	[POWER_SUPPLY_TYPE_USB] = 500,
	[POWER_SUPPLY_TYPE_USB_DCP] = 1300,
	[POWER_SUPPLY_TYPE_USB_CDP] = 500,
	[POWER_SUPPLY_TYPE_USB_ACA] = 500,
};

char *supplies_to[] = {
	"battery",
};

char *get_supply_type_str(int chrgr_type)
{
	switch (chrgr_type) {
	case POWER_SUPPLY_TYPE_USB:
	case POWER_SUPPLY_TYPE_USB_CDP:
	case POWER_SUPPLY_TYPE_USB_ACA:
		return"bcm_usb";

	case POWER_SUPPLY_TYPE_USB_DCP:
	case POWER_SUPPLY_TYPE_MAINS:
		return "bcm_ac";

	default:
			break;
		}

	return NULL;
}
EXPORT_SYMBOL_GPL(get_supply_type_str);

static int charger_event_handler(struct notifier_block *nb,
		unsigned long event, void *para)
{

	struct bcm_chrgr_data *di = g_chrgr_data;
	enum power_supply_type chrgr_type;

	switch (event) {
	case PMU_ACCY_EVT_OUT_CHRGR_TYPE:
		chrgr_type =  *(enum power_supply_type *)para;
#ifdef custom_t_BATTERY
		pr_chrgr(FLOW, "%s, chrgr type=%d chrgr_curr_limit=%d\n",
				__func__, chrgr_type,di->chrgr_curr_tbl[chrgr_type]);
#else
		pr_chrgr(FLOW, "%s, chrgr type=%d\n",
				__func__, chrgr_type);
#endif
		
		if ((chrgr_type == POWER_SUPPLY_TYPE_UPS) ||
				(chrgr_type == POWER_SUPPLY_TYPE_MAINS) ||
				(chrgr_type == POWER_SUPPLY_TYPE_USB) ||
				(chrgr_type == POWER_SUPPLY_TYPE_USB_DCP) ||
				(chrgr_type == POWER_SUPPLY_TYPE_USB_CDP) ||
				(chrgr_type == POWER_SUPPLY_TYPE_USB_ACA)) {
			bcm_chrgr_insert(1);
			bcm_set_icc_fc(di->chrgr_curr_tbl[chrgr_type]);
			bcm_chrgr_usb_en(1);
			if ((get_supply_type_str(chrgr_type) != NULL) &&
					(strcmp(get_supply_type_str(chrgr_type),
						"bcm_usb") == 0)) {
				di->usb_chrgr_info.online = 1 ;
				di->usb_chrgr_info.model_name =
					chrgr_names[chrgr_type];
				pr_chrgr(VERBOSE, "%s, ONLINE SET\n",
						__func__);
				power_supply_changed(&di->usb_psy);
			} else {
				di->ac_chrgr_info.online = 1;
				di->ac_chrgr_info.model_name =
					chrgr_names[chrgr_type];
				power_supply_changed(&di->ac_psy);
			}
		} else if ((chrgr_type == POWER_SUPPLY_TYPE_UNKNOWN) ||
			(chrgr_type == POWER_SUPPLY_TYPE_BATTERY)) {
			bcm_chrgr_insert(0);
			bcm_chrgr_usb_en(0);
			if (di->ac_chrgr_info.online) {
				di->ac_chrgr_info.online = 0;
				power_supply_changed(&di->ac_psy);
			} else {
				di->usb_chrgr_info.online = 0 ;
				power_supply_changed(&di->usb_psy);
			}
		} else {
			pr_chrgr(FLOW, "Unknown charger type:%d\n",
					chrgr_type);
			WARN_ON(1);
		}
		break;
	}
	return 0;
}

static int bcm_chrgr_ac_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	int ret = 0;

	pr_chrgr(VERBOSE, "%s: property %d\n", __func__, psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = g_chrgr_data->ac_chrgr_info.online;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = g_chrgr_data->ac_chrgr_info.curr;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = g_chrgr_data->ac_chrgr_info.model_name;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int bcm_chrgr_ac_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	int ret = 0;

	pr_chrgr(VERBOSE, "%s: property %d\n", __func__, psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		g_chrgr_data->ac_chrgr_info.online = val->intval;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		g_chrgr_data->ac_chrgr_info.curr = val->intval;
		bcm_set_icc_fc(val->intval);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int bcm_chrgr_usb_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	int ret = 0;

	pr_chrgr(VERBOSE, "%s: property %d\n", __func__, psp);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = g_chrgr_data->usb_chrgr_info.online;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = g_chrgr_data->usb_chrgr_info.curr;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = g_chrgr_data->usb_chrgr_info.model_name;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int bcm_chrgr_usb_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	int ret = 0;

	pr_chrgr(VERBOSE, "%s: property %d\n", __func__, psp);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		g_chrgr_data->usb_chrgr_info.online = val->intval;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		g_chrgr_data->usb_chrgr_info.curr = val->intval;
		bcm_set_icc_fc(val->intval);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int bcm_chrgr_remove(struct platform_device *pdev)
{
#ifdef CONFIG_SEC_CHARGING_FEATURE
	power_supply_unregister(&g_chrgr_data->ac_psy);
	power_supply_unregister(&g_chrgr_data->usb_psy);
	kfree(g_chrgr_data);
#endif
	return 0;
}
static int bcm_chrgr_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct bcm_chrgr_pdata *pdata;

#ifdef CONFIG_SEC_CHARGING_FEATURE
	return 0;
#endif

	pr_chrgr(INIT, "%s called\n", __func__);
	g_chrgr_data = kzalloc(sizeof(struct bcm_chrgr_data), GFP_KERNEL);

	if (!g_chrgr_data)
		return -ENOMEM;

	pdata = pdev->dev.platform_data;
	if (pdata && pdata->chrgr_curr_lmt_tbl)
		g_chrgr_data->chrgr_curr_tbl = pdata->chrgr_curr_lmt_tbl;
	else
		g_chrgr_data->chrgr_curr_tbl = chrgr_curr_lmt_default;

	g_chrgr_data->ac_psy.name = "bcm_ac";
	g_chrgr_data->ac_psy.type = POWER_SUPPLY_TYPE_MAINS;
	g_chrgr_data->ac_psy.properties = bcm_chrgr_props;
	g_chrgr_data->ac_psy.num_properties = ARRAY_SIZE(bcm_chrgr_props);
	g_chrgr_data->ac_psy.get_property = bcm_chrgr_ac_get_property;
	g_chrgr_data->ac_psy.set_property = bcm_chrgr_ac_set_property;
	g_chrgr_data->ac_psy.supplied_to = supplies_to;
	g_chrgr_data->ac_psy.num_supplicants = ARRAY_SIZE(supplies_to);

	g_chrgr_data->usb_psy.name = "bcm_usb";
	g_chrgr_data->usb_psy.type = POWER_SUPPLY_TYPE_USB;
	g_chrgr_data->usb_psy.properties = bcm_usb_props;
	g_chrgr_data->usb_psy.num_properties = ARRAY_SIZE(bcm_usb_props);
	g_chrgr_data->usb_psy.get_property = bcm_chrgr_usb_get_property;
	g_chrgr_data->usb_psy.set_property = bcm_chrgr_usb_set_property;
	g_chrgr_data->usb_psy.supplied_to = supplies_to;
	g_chrgr_data->usb_psy.num_supplicants = ARRAY_SIZE(supplies_to);

	ret = power_supply_register(&pdev->dev, &g_chrgr_data->ac_psy);
	if (ret)
		goto free_dev_info;

	ret = power_supply_register(&pdev->dev, &g_chrgr_data->usb_psy);
	if (ret)
		goto unregister_ac_supply;

	g_chrgr_data->chgr_detect.notifier_call = charger_event_handler;
	ret = bcm_add_notifier(PMU_ACCY_EVT_OUT_CHRGR_TYPE,
			&g_chrgr_data->chgr_detect);
	if (ret)
		return ret;

	pr_chrgr(INIT, "%s: success\n", __func__);
	return 0;
unregister_ac_supply:
	power_supply_unregister(&g_chrgr_data->ac_psy);
free_dev_info:
	kfree(g_chrgr_data);
	return ret;
}
static struct platform_driver bcm_chrgr_driver = {
	.driver = {
		.name = "bcm_chrgr",
	},
	.probe = bcm_chrgr_probe,
	.remove = bcm_chrgr_remove,
};

static int __init bcm_chrgr_init(void)
{
	return platform_driver_register(&bcm_chrgr_driver);
}
subsys_initcall(bcm_chrgr_init);

static void __exit bcm_chrgr_exit(void)
{
	platform_driver_unregister(&bcm_chrgr_driver);
}
module_exit(bcm_chrgr_exit);

MODULE_DESCRIPTION("Broadcom Charger Driver");
MODULE_LICENSE("GPL");
