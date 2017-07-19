
/*****************************************************************************
 *  Copyright 2001 - 20012 Broadcom Corporation.  All rights reserved.
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
#ifndef __BCM_H
#define __BCM_H
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/wakelock.h>

/* local debug level */
#define BCM_PRINT_ERROR      (1U << 0)
#define BCM_PRINT_INIT       (1U << 1)
#define BCM_PRINT_FLOW       (1U << 2)
#define BCM_PRINT_DATA       (1U << 3)
#define BCM_PRINT_WARNING    (1U << 4)
#define BCM_PRINT_VERBOSE    (1U << 5)


enum bcm_event_t {
	PMU_ACCY_EVT_OUT_CHRGR_TYPE,
	PMU_ACCY_EVT_OUT_USB_IN,
	PMU_ACCY_EVT_OUT_USB_RM,
	PMU_ACCY_EVT_OUT_ADP_CHANGE,
	PMU_ACCY_EVT_OUT_ADP_SENSE_END,
	PMU_ACCY_EVT_OUT_ADP_CALIBRATION_DONE,
	PMU_ACCY_EVT_OUT_ID_CHANGE,
	PMU_ACCY_EVT_OUT_VBUS_VALID,
	PMU_ACCY_EVT_OUT_VBUS_INVALID,
	PMU_ACCY_EVT_OUT_SESSION_VALID,
	PMU_ACCY_EVT_OUT_SESSION_INVALID,
	PMU_ACCY_EVT_OUT_SESSION_END_INVALID,
	PMU_ACCY_EVT_OUT_SESSION_END_VALID,
	PMU_ACCY_EVT_OUT_CHGDET_LATCH,
	PMU_ACCY_EVT_OUT_CHGDET_LATCH_TO,
	PMU_ACCY_EVT_OUT_CHRG_RESUME_VBUS,
	PMU_ACCY_EVT_OUT_CHRG_CURR,
	PMU_ACCY_EVT_OUT_USBOV,
	PMU_ACCY_EVT_OUT_USBOV_DIS,
	PMU_ACCY_EVT_OUT_CHGERRDIS,

	PMU_CHRGR_DET_EVT_OUT_XCVR,
	PMU_CHRGR_EVT_SET_CHARGE_VOLTAGE,
	PMU_CHRGR_EVT_MBTEMP,
	PMU_CHRGR_EVT_MBOV,
	PMU_CHRGR_EVT_EOC,
	PMU_CHRGR_EVT_CHRG_STATUS,
	PMU_ACLD_EVT_ACLD_STATUS,
	PMU_THEMAL_THROTTLE_STATUS,
	PMU_FG_EVT_CAPACITY,
	PMU_FG_EVT_EOC,
	PMU_JIG_EVT_USB,
	PMU_JIG_EVT_UART,
	PMU_EVENT_MAX,
};

#if 0
enum bcm_chrgr_type_t {
	PMU_CHRGR_TYPE_NONE,
	PMU_CHRGR_TYPE_SDP,
	PMU_CHRGR_TYPE_CDP,
	PMU_CHRGR_TYPE_DCP,
	PMU_CHRGR_TYPE_TYPE1,
	PMU_CHRGR_TYPE_TYPE2,
	PMU_CHRGR_TYPE_PS2,
	PMU_CHRGR_TYPE_ACA_DOCK,
	PMU_CHRGR_TYPE_ACA,
	PMU_CHRGR_TYPE_MAX,
};
#endif

enum bcm_agent_feature {
	BCM_AGENT_SET_CHARGE,
	BCM_AGENT_SET_CHARGE_CURRENT,
	BCM_AGENT_SET_CHARGE_VOLTAGE,
	BCM_AGENT_SET_FULL_CHARGE,
	BCM_AGENT_GET_VOLTAGE,
	BCM_AGENT_GET_TEMP,
	BCM_AGENT_GET_CAPACITY,
	BCM_AGENT_GET_BATT_PRESENCE_PMIC,
	BCM_AGENT_GET_BATT_PRESENCE_CHARGER,
	BCM_AGENT_GET_CHARGER_TYPE,
	BCM_AGENT_GET_CURRENT,
	BCM_AGENT_GET_CHARGE_STATE,
	BCM_AGENT_CHRGR_INSERT,
	BCM_AGENT_CTRL_FG,
	BCM_AGENT_GET_RAW_SOC,
	BCM_AGENT_GET_SOC,
	BCM_AGENT_GET_VCELL,
	BCM_AGENT_SET_FG_TEMP,
	BCM_AGENT_GET_FG_THRESHOLD_SOC,
	BCM_AGENT_GET_FG_MAX_SOC,
	BCM_AGENT_GET_FG_FTCHG_SOC,
	BCM_AGENT_GET_FG_BKCHG_SOC,
	BCM_AGENT_GET_FG_RECHG_SOC,
	BCM_AGENT_SET_FG_RECHG_SOC,
	BCM_AGENT_GET_FG_PRIVATE,
	BCM_AGENT_MAX,
};

struct bcm_chrgr_pdata {
	int *chrgr_curr_lmt_tbl;
	unsigned int flags;
};

union bcm_agents {
	int (*set_charge)(unsigned int en);
	int (*set_charge_current)(unsigned int curr);
	int (*set_charge_voltage)(unsigned int mvolt);
	int (*set_full_charge)(unsigned int eoc);
	int (*get_voltage)(unsigned int opt);
	int (*get_temp)(unsigned int opt);
	int (*get_capacity)(void);
	int (*get_batt_presence_pmic)(void);
	int (*get_batt_presence_charger)(void);
	int (*get_charger_type)(void);
	int (*get_charger_current)(unsigned int opt);
	int (*get_charge_state)(void);
	int (*chrgr_insert)(unsigned int en);
	int (*ctrl_fg)(void *data);
	int (*get_fg_soc)(void);
	int (*get_fg_raw_soc)(void);
	int (*get_fg_vcell)(void);
	int (*set_fg_temp)(unsigned int temp);
	int (*get_fg_private)(void);
	int (*get_fg_threshold_soc)(void);
	int (*get_fg_max_soc)(void);
	int (*get_fg_rechg_soc)(void);
	int (*get_fg_ftchg_soc)(void);
	int (*get_fg_bkchg_soc)(void);
	int (*set_fg_rechg_soc)(unsigned int data);
	int (*dummy)(void);
};

struct bcm_agent_fn {
	union bcm_agents fn;
	char *agent_name;
};

struct d2153_ntc_temp_tb {
	int adc;
	int temp;
};

extern struct d2153_battery_platform_data pbat_pdata;
extern struct d2153_battery_platform_data amethyst_pbat_pdata;
extern struct d2153_battery_platform_data p35b_pbat_pdata;
extern struct d2153_battery_platform_data l4020ap_c_pbat_pdata;
extern struct d2153_hwmon_platform_data d2153_adc_pdata;
extern struct d2153_rtc_platform_data prtc_data;
extern struct d2153_rtc_platform_data amethyst_prtc_data;
extern struct d2153_rtc_platform_data p35b_prtc_data;
extern struct d2153_rtc_platform_data l4020ap_c_prtc_data;

int bcm_init(void);
int bcm_add_notifier(u32 event_id, struct notifier_block *notifier);
int bcm_remove_notifier(u32 event_id, struct notifier_block *notifier);
int bcm_call_notifier(u32 event_id, void *para);
void bcm_accy_event_handler(int evt, void *data);
int bcm_agent_register(unsigned int agent_id, void *fn, const char *agent_name);
int bcm_set_icc_fc(int curr);
int bcm_chrgr_usb_en(int enable);
int bcm_agent_get(int ctrl, void *data);
int bcm_agent_set(int ctrl, unsigned long data);
#ifdef CONFIG_SEC_CHARGING_FEATURE
int bcm_chrgr_en_st(int enable);
#endif
int bcm_set_charge_volt(unsigned int mVolt);
int bcm_chrgr_insert(unsigned int insert);
#endif
