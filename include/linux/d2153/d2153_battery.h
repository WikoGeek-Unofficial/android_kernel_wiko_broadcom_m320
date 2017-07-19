/*
 * d2153 Battery/Power module declarations.
  *
 * Copyright(c) 2013 Dialog Semiconductor Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 */


#ifndef __D2153_BATTERY_H__
#define __D2153_BATTERY_H__

#include <linux/mutex.h>
#include <linux/wakelock.h>
#include <linux/power_supply.h>
#ifdef CONFIG_D2153_HW_TIMER
#include <linux/alarmtimer.h>
#endif

#define BCM_ID_814296

#ifdef custom_t_BATTERY
#define custom_t_BATTERY_CHARGER_ON
#define custom_t_BATTERY_PROTECT
//#define custom_t_BATTERY_CAPACITY_SYNC
//#define custom_t_BATTERY_DISCHARGE
//#define custom_t_BATTERY_TEMPERATURE_DEBUG
#endif  /* custom_t_BATTERY */

#define D2153_REG_EOM_IRQ
#undef  D2153_REG_VDD_MON_IRQ
#undef  D2153_REG_VDD_LOW_IRQ
#define D2153_REG_TBAT2_IRQ
#define CONFIG_D2153_EOC_CTRL
#define CONFIG_D2153_SOC_GO_DOWN_IN_CHG
#ifdef CONFIG_DEBUG_FS
#define CONFIG_D2153_DEBUG_FEATURE
#endif

#define D2153_MANUAL_READ_RETRIES			(5)
#define ADC2TEMP_LUT_SIZE					(22)
#define ADC2VBAT_LUT_SIZE					(10)
#define D2153_ADC_RESOLUTION				(10)

#define AVG_SIZE							(16)
#define AVG_SHIFT							(4)

#define SEC_FOR_MINUTE						(60)
#define SEC_TO_MILLISEC						(1000)

#define DEGREEK_FOR_DEGREEC_0				(273)

#define C2K(c)								(273 + c)	/* convert from Celsius to Kelvin */
#define K2C(k)								(k - 273)	/* convert from Kelvin to Celsius */

#define BAT_HIGH_TEMPERATURE				C2K(700)
#define BAT_ROOM_TEMPERATURE				C2K(200) //C2K(250)
#define BAT_ROOM_LOW_TEMPERATURE			C2K(100)
#define BAT_LOW_TEMPERATURE					C2K(0)
#define BAT_LOW_MID_TEMPERATURE				C2K(-100)
#define BAT_LOW_LOW_TEMPERATURE				C2K(-200)

#define BAT_CAPACITY_1300MA					(1300)
#define BAT_CAPACITY_1500MA					(1500)
#define BAT_CAPACITY_1800MA					(1800)
#define BAT_CAPACITY_2000MA					(2000)
#define BAT_CAPACITY_2100MA					(2100)
#define BAT_CAPACITY_2200MA					(2200)
#define BAT_CAPACITY_4500MA					(4500)
#define BAT_CAPACITY_7000MA					(7000)

#if defined(CONFIG_MACH_AFYONLTE)
#define USED_BATTERY_CAPACITY				BAT_CAPACITY_2100MA
#define MULTI_WEIGHT_SIZE					15
#define CONFIG_D2153_MULTI_WEIGHT
#elif defined(CONFIG_MACH_EOS)
#define USED_BATTERY_CAPACITY				BAT_CAPACITY_1800MA
#define MULTI_WEIGHT_SIZE					15
#define CONFIG_D2153_MULTI_WEIGHT
#else
#define USED_BATTERY_CAPACITY				BAT_CAPACITY_2100MA
#define MULTI_WEIGHT_SIZE					15
#define CONFIG_D2153_MULTI_WEIGHT
#endif

#if USED_BATTERY_CAPACITY == BAT_CAPACITY_1800MA
#define CONFIG_SOC_LUT_15STEPS
#elif USED_BATTERY_CAPACITY == BAT_CAPACITY_2100MA
#define CONFIG_SOC_LUT_15STEPS
#elif USED_BATTERY_CAPACITY == BAT_CAPACITY_2200MA
#define CONFIG_SOC_LUT_15STEPS
#else
#undef  CONFIG_SOC_LUT_15STEPS
#endif

#ifdef CONFIG_SOC_LUT_15STEPS
#define ADC2SOC_LUT_SIZE					(15)
#else
#define ADC2SOC_LUT_SIZE					(14)
#endif

#define D2153_VOLTAGE_MONITOR_START			(HZ*1)
#define D2153_VOLTAGE_MONITOR_NORMAL		(HZ*10)
#if defined (BCM_ID_814296)
#define D2153_VOLTAGE_MONITOR_FAST			(HZ*3)
#else
#define D2153_VOLTAGE_MONITOR_FAST			(HZ*5)
#endif  /* BCM_ID_814296 */

#define D2153_TEMPERATURE_MONITOR_START		(HZ*1)
#define D2153_TEMPERATURE_MONITOR_NORMAL	(HZ*5)
#define D2153_TEMPERATURE_MONITOR_FAST		(HZ*1)

#define DCP_TIMER_RESET_PERIOD_SEC              15

#define D2153_NOTIFY_INTERVAL				(HZ*10)

#define D2153_SLEEP_MONITOR_WAKELOCK_TIME	(0.35*HZ)

#define D2153_VOLT_WORK_DELAY				(1000)

#define BAT_VOLTAGE_ADC_DIVISION			(1700)

#define BAT_POWER_OFF_VOLTAGE				(3400)

#define D2153_CHARGE_CV_ADC_LEVEL		    (3380)
#define FULLY_CHARGED_ADC_LEVEL			    (3465)

#define D2153_CAL_HIGH_VOLT					(4200)
#define D2153_CAL_LOW_VOLT					(3400)

#define D2153_BASE_4P2V_ADC                 (3269)
#define D2153_BASE_3P4V_ADC                 (1581)
#define D2153_CAL_MAX_OFFSET				(10)

/* for Hardware semaphore */
#define CONST_HPB_WAIT						(100) //25
#define RT_CPU_SIDE							(0x01)
#define SYS_CPU_SIDE						(0x40)
#define BB_CPU_SIDE							(0x93)
#define D2153_OPT_ADC						(0)
#define D2153_OPT_TEMP						(1)

#define D2153_BATTERY_ABSENT		(0xFFA)	/* 4090 */

#define BCMPMU_SPA_EN						(1 << 0)

/* ADC current source */
#define D2153_ADC_ISRC_10UA		10
#define D2153_ADC_ISRC_50UA		50

#ifndef CONFIG_SEC_CHARGING_FEATURE
#define NTC_CNT_MAX				3
#define ADC_TEMP_OVF				0x0F00
#else
#define ADC_TEMP_OVF				0x0FFF
#endif

#define IS_N_DIFF(a, b, deg) (abs(a - b) >= deg)
#define FLAG_MAX_SIZE	20

/* HIGH BATT TEMP */
#define BATT_TEMP_DIFF			10

#if defined (custom_t_BATTERY_CAPACITY_SYNC)
#define INVALID_CAPACITY 0xff
#define CAPACITY_SYNC_TIME_CHARGING 30
#define CAPACITY_SYNC_TIME_DISCHARGING 60
#endif  /* custom_t_BATTERY_CAPACITY_SYNC */

enum {
	CHARGER_TYPE_NONE = 0,
	CHARGER_TYPE_TA,
	CHARGER_TYPE_USB,
	CHARGER_TYPE_MAX
};

enum {
	D2153_BATTERY_SOC = 0,
	D2153_BATTERY_TEMP_HPA,
	D2153_BATTERY_TEMP_ADC,
	D2153_BATTERY_CUR_VOLTAGE,
	D2153_BATTERY_AVG_VOLTAGE,
	D2153_BATTERY_VOLTAGE_NOW,
	D2153_BATTERY_STATUS,
	D2153_BATTERY_MAX
};

enum {
	D2153_BATTERY_STATUS_DISCHARGING = 0,
	D2153_BATTERY_STATUS_CHARGING,
	D2153_BATTERY_STATUS_MAX
};

enum {
	D2153_STATUS_CHARGING = 0,
	D2153_RESET_SW_FG,
	D2153_STATUS_EOC_CTRL,
	D2153_STATUS_MAX
};

enum {
	BATTERY_NOT_DETECTED,
	BATTERY_DETECTED
};

#ifdef CONFIG_D2153_EOC_CTRL
enum {
	D2153_BAT_CHG_START = 0,
	D2153_BAT_CHG_FRST_FULL,
	D2153_BAT_CHG_BACKCHG_FULL,
	D2153_BAT_RECHG_FULL,
	D2153_BAT_CHG_MAX,
};
#endif /* CONFIG_D2153_EOC_CTRL */

typedef enum d2153_adc_channel {
	D2153_ADC_VOLTAGE = 0,
	D2153_ADC_TEMPERATURE_1,
	D2153_ADC_TEMPERATURE_2,
	D2153_ADC_VF,
	D2153_ADC_AIN,
	D2153_ADC_TJUNC,
	D2153_ADC_CHANNEL_MAX    
} adc_channel;

typedef enum d2153_adc_mode {
	D2153_ADC_IN_AUTO = 1,
	D2153_ADC_IN_MANUAL,
	D2153_ADC_MODE_MAX
} adc_mode;

enum d2153_adc_config_mode {
	D2153_ADC_ECO,
	D2153_ADC_FAST
};

/* Print Levels */
#define D2153_PRINT_ERROR	(1U << 0)
#define D2153_PRINT_INIT	(1U << 1)
#define D2153_PRINT_FLOW	(1U << 2)
#define D2153_PRINT_DATA	(1U << 3)
#define D2153_PRINT_WARNING	(1U << 4)
#define D2153_PRINT_VERBOSE	(1U << 5)

struct diff_tbl {
	u16 c1_diff[MULTI_WEIGHT_SIZE];
	u16 c2_diff[MULTI_WEIGHT_SIZE];
};

struct adc_man_res {
	u16 read_adc;
	u8  is_adc_eoc;
};

struct init_drop_table {
	u16	init_avg_adc;
	u16 compensate_offset;
};

struct adc2temp_lookuptbl {
	u16 adc[ADC2TEMP_LUT_SIZE];
	u16 temp[ADC2TEMP_LUT_SIZE];
};

struct adc2vbat_lookuptbl {
	u16 adc[ADC2VBAT_LUT_SIZE];
	u16 vbat[ADC2VBAT_LUT_SIZE];
	u16 offset[ADC2VBAT_LUT_SIZE];
};

struct adc2soc_lookuptbl {
	u16 adc_ht[ADC2SOC_LUT_SIZE];
	u16 adc_rt[ADC2SOC_LUT_SIZE];
	u16 adc_rlt[ADC2SOC_LUT_SIZE];
	u16 adc_lt[ADC2SOC_LUT_SIZE];
	u16 adc_lmt[ADC2SOC_LUT_SIZE];
	u16 adc_llt[ADC2SOC_LUT_SIZE];
	u16 soc[ADC2SOC_LUT_SIZE];
};

struct adc_cont_in_auto {
	u8 adc_preset_val;
	u8 adc_cont_val;
	u8 adc_msb_res;
	u8 adc_lsb_res;
	u8 adc_lsb_mask;
};

struct d2153_battery {
	struct d2153	*pd2153;
	struct device	*dev;
	struct platform_device *pdev;
	struct bcm_accessories_pdata *bcmaccy;
	struct power_supply psy;

	u8 adc_mode;

	struct delayed_work	monitor_volt_work;
	struct delayed_work	monitor_temp_work;
	struct delayed_work	sleep_monitor_work;
#ifndef CONFIG_SEC_CHARGING_FEATURE
	struct delayed_work charging_expiry_work;
	bool charging_timer_expired;
#endif
#ifdef CONFIG_D2153_HW_TIMER
	struct alarm		alarm;
#endif
	struct mutex		meoc_lock;
	struct mutex		lock;
	struct mutex		api_lock;

	struct notifier_block chgr_detect;

	u8  is_charging;
	bool eoc_status;
	u8  vdd_hwmon_level;

	u32	current_level;

	// for voltage
	u32 origin_volt_adc;
	u32 current_volt_adc;
	u32 average_volt_adc;
	u32	current_voltage;
	u32	average_voltage;
	u32 sum_voltage_adc;
	u8  reset_total_adc;
	int sum_total_adc;

	// for temperature
	u32	current_temp_adc;
	u32 current_rf_temp_adc;
	u32 current_ap_temp_adc;
	u32	average_temp_adc;
	int	current_temperature;
#ifdef custom_t_BATTERY	
	int prev_current_temperature;
#endif	
	int	prev_temperature;
	int current_rf_temperature;
	int current_ap_temperature;
 // BCM_ID_814048 int	avg_temp;
	u32 sum_temperature_adc;

	u32	soc;
	u32 prev_soc;
	u32 raw_soc;

	int battery_technology;
	int battery_present;
	u32	capacity;
	u32	prev_capacity;
	#if defined (custom_t_BATTERY_CAPACITY_SYNC)
	u32	ui_capacity;
    bool charger_change;
	#endif  /* custom_t_BATTERY_CAPACITY_SYNC */

	u16 vf_adc;
	u32 vf_ohm;
	u32	vf_lower;
	u32	vf_upper; 

	u32 voltage_adc[AVG_SIZE];
	u32 temperature_adc[AVG_SIZE];
	u8	voltage_idx;
	u8 	temperature_idx;
	u32 adc_prev_val;
	bool adc_max_lmt;

	u8  volt_adc_init_done;
	u8  temp_adc_init_done;
    struct adc_man_res adc_res[D2153_ADC_CHANNEL_MAX];

	u8 virtual_battery_full;
#ifdef CONFIG_D2153_EOC_CTRL
	u8 charger_ctrl_status;
#endif
	bool charger_connected;
	enum power_supply_type charger_type;
#ifdef CONFIG_D2153_HW_TIMER
	struct wake_lock d2153_alarm_wake_lock;
#endif
#ifndef CONFIG_SEC_CHARGING_FEATURE
	u8 health;
	int ntc_cnt;
#endif

	bool force_dbg_print;

	int (*d2153_read_adc)(struct d2153_battery *pbat,
				adc_channel channel, int curr_src);
};


void d2153_handle_modem_reset(void);
int  d2153_battery_read_status(int type);
int  d2153_battery_set_status(int type, int status);
int  d2153_get_rf_temperature(void);
void d2153_battery_start(void);

#endif /* __D2153_BATTERY_H__ */
