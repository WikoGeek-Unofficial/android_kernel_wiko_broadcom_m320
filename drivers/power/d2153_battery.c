/*
 * Battery driver for Dialog D2153
 *
 * Copyright(c) 2013 Dialog Semiconductor Ltd.
 *
 * Author: Dialog Semiconductor Ltd. D. Chen, A Austin, E Jeong
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/power_supply.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/wakelock.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#ifdef CONFIG_SEC_CHARGING_FEATURE
#include <linux/spa_power.h>
#include <linux/spa_agent.h>
#endif
#include "linux/err.h"
#include <linux/alarmtimer.h>


#include <linux/d2153/core.h>
#include <linux/d2153/d2153_battery.h>
#include <linux/d2153/d2153_reg.h>
#include <linux/d2153/hwmon.h>
#include <linux/bcm.h>
#ifdef CONFIG_D2153_DEBUG_FEATURE
#include <linux/debugfs.h>
#endif

#ifdef custom_t_BATTERY
#include <linux/reboot.h>
extern struct lc709203f_chip *g_lc709203f_info;
#endif

#if defined (custom_t_BATTERY_TEMPERATURE_DEBUG)
static int g_battery_thermal_throttling_flag=3; // 0:nothing, 1:enable batTT&chrTimer, 2:disable batTT&chrTimer, 3:enable batTT, disable chrTimer
static int battery_cmd_thermal_test_mode=0;
static int battery_cmd_thermal_test_mode_value=0;
#endif  /* custom_t_BATTERY_TEMPERATURE_DEBUG */


static const char __initdata d2153_battery_banner[] = \
    "D2153 Battery, (c) 2013 Dialog Semiconductor Ltd.\n";

/***************************************************************************
 Pre-definition
***************************************************************************/
#define FALSE								(0)
#define TRUE								(1)

#if USED_BATTERY_CAPACITY == BAT_CAPACITY_1800MA
#define ADC_VAL_100_PERCENT					3386   // About 4153mV
#define MAX_FULL_CHARGED_ADC				3481   // About 4200mV
#define ORIGN_CV_START_ADC					3194   // About 4060mV
#define ORIGN_FULL_CHARGED_ADC				3481   // About 4200mV

#define D2153_BAT_CHG_FRST_FULL_LVL         4168   // 4168mV
#define D2153_BAT_CHG_BACK_FULL_LVL         4180   // 4180mV

#define FIRST_VOLTAGE_DROP_ADC				106    // 121

#define FIRST_VOLTAGE_DROP_LL_ADC			340
#define FIRST_VOLTAGE_DROP_L_ADC			60
#define FIRST_VOLTAGE_DROP_RL_ADC			25

#define CHARGE_OFFSET_KRNL_H				880
#define CHARGE_OFFSET_KRNL_L				5

#define CHARGE_ADC_KRNL_H					1700
#define CHARGE_ADC_KRNL_L					2050
#define CHARGE_ADC_KRNL_F					2060

#define CHARGE_OFFSET_KRNL					77
#define CHARGE_OFFSET_KRNL2					98     /* 50 */

#define LAST_CHARGING_WEIGHT				380   // 900
#define VBAT_3_4_VOLTAGE_ADC				1605
#elif USED_BATTERY_CAPACITY == BAT_CAPACITY_2100MA
#define ADC_VAL_100_PERCENT			3645   // About 4280mV
#define MAX_FULL_CHARGED_ADC				3788   // About 4340mV
#define ORIGN_CV_START_ADC					3686   // About 4300mV
#define ORIGN_FULL_CHARGED_ADC				3780   // About 4345mV

#define D2153_BAT_CHG_FRST_FULL_LVL         4310   // About EOC 160mA
#define D2153_BAT_CHG_BACK_FULL_LVL         4340   // About EOC 50mA

#define FIRST_VOLTAGE_DROP_ADC				145

#define FIRST_VOLTAGE_DROP_LL_ADC			340
#define FIRST_VOLTAGE_DROP_L_ADC			60
#define FIRST_VOLTAGE_DROP_RL_ADC			25

#define CHARGE_OFFSET_KRNL_H				360    // 400 -> 360
#define CHARGE_OFFSET_KRNL_L				160

#define CHARGE_ADC_KRNL_H					1150   // 1155 -> 1150
#define CHARGE_ADC_KRNL_L					2304
#define CHARGE_ADC_KRNL_F					2305

#define CHARGE_OFFSET_KRNL					20
#define CHARGE_OFFSET_KRNL2					104    // 94 -> 104

#define LAST_CHARGING_WEIGHT				124   // 900
#define VBAT_3_4_VOLTAGE_ADC				1605

#elif USED_BATTERY_CAPACITY == BAT_CAPACITY_2200MA

#define ADC_VAL_100_PERCENT			3645   /* About 4280mV */
#define MAX_FULL_CHARGED_ADC			3788   /* About 4340mV */
#define ORIGN_CV_START_ADC			3686   /* About 4300mV */
#define ORIGN_FULL_CHARGED_ADC			3780   /* About 4345mV */

#define D2153_BAT_CHG_FRST_FULL_LVL		4310   /* About EOC 160mA */
#define D2153_BAT_CHG_BACK_FULL_LVL		4340   /* About EOC 50mA */

#define FIRST_VOLTAGE_DROP_ADC			145

#define FIRST_VOLTAGE_DROP_LL_ADC		340
#define FIRST_VOLTAGE_DROP_L_ADC		60
#define FIRST_VOLTAGE_DROP_RL_ADC		25

#define CHARGE_OFFSET_KRNL_H			360    /* 400 -> 360 */
#define CHARGE_OFFSET_KRNL_L			160

#define CHARGE_ADC_KRNL_H			1150   /* 1155 -> 1150 */
#define CHARGE_ADC_KRNL_L			2304
#define CHARGE_ADC_KRNL_F			2305

#define CHARGE_OFFSET_KRNL			20
#define CHARGE_OFFSET_KRNL2			104    /* 94 -> 104 */

#define LAST_CHARGING_WEIGHT			124    /* 900 */
#define VBAT_3_4_VOLTAGE_ADC			1605


#else

#define ADC_VAL_100_PERCENT			3445
#define MAX_FULL_CHARGED_ADC				3470
#define ORIGN_CV_START_ADC					3320
#define ORIGN_FULL_CHARGED_ADC				3480   // About 4345mV

#define D2153_BAT_CHG_FRST_FULL_LVL         4160   // About EOC 160mA
#define D2153_BAT_CHG_BACK_FULL_LVL         4185   // About EOC 60mA

#define FIRST_VOLTAGE_DROP_ADC				165

#define FIRST_VOLTAGE_DROP_LL_ADC			340
#define FIRST_VOLTAGE_DROP_L_ADC			60
#define FIRST_VOLTAGE_DROP_RL_ADC			25

#define CHARGE_OFFSET_KRNL_H				880
#define CHARGE_OFFSET_KRNL_L				5

#define CHARGE_ADC_KRNL_H					1700
#define CHARGE_ADC_KRNL_L					2050
#define CHARGE_ADC_KRNL_F					2060

#define CHARGE_OFFSET_KRNL					77
#define CHARGE_OFFSET_KRNL2					50

#define LAST_CHARGING_WEIGHT				450   // 900
#define VBAT_3_4_VOLTAGE_ADC				1605

#endif /* USED_BATTERY_CAPACITY == BAT_CAPACITY_????MA */

#define FULL_CAPACITY						1000

#define NORM_NUM					10000
#define NORM_CHG_NUM						100000

#define DISCHARGE_SLEEP_OFFSET			55    /* 45*/
#define LAST_VOL_UP_PERCENT			75
#define ADC_ERR_TRIES				10
#define ADC_RETRY_DELAY				100	/*100 ms*/

#ifdef CONFIG_D2153_DEBUG_FEATURE
#define DEBUG_FS_PERMISSIONS	(S_IRUSR | S_IWUSR | S_IROTH | S_IRGRP)
#endif


/* Static Function Prototype */
/* static void d2153_external_event_handler(int category, int event); */
static u32 debug_mask = D2153_PRINT_ERROR | D2153_PRINT_INIT | \
			D2153_PRINT_FLOW;
#define pr_batt(debug_level, args...) \
	do { \
		if (debug_mask & D2153_PRINT_##debug_level) { \
			pr_info("[D2153-BATT]:"args); \
		} \
	} while (0)

#define to_d2153_battery_data(ptr, mem)	container_of((ptr), \
				struct d2153_battery, mem)

#ifndef CONFIG_SEC_CHARGING_FEATURE
static enum power_supply_property d2153_battery_props[] = {
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_HPA,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
};
#endif
static struct d2153_battery *gbat = NULL;

static unsigned int ntc_disable;
module_param(ntc_disable, uint, 0644);

static unsigned int dbg_batt_en;
module_param(dbg_batt_en, uint, 0644);

static char d2153_dbg_print1[FLAG_MAX_SIZE];
static char d2153_dbg_print2[FLAG_MAX_SIZE];

static int temp_adc_min;
static int temp_adc_max;
/* LUT for NCP15XW223 thermistor with 10uA current source selected */
static struct adc2temp_lookuptbl adc2temp_lut = {
	// Case of NCP03XH223 for TEMP1/TEMP2
	.adc  = {  // ADC-12 input value
		2144,      1691,      1341,      1072,     865,      793,      703,
		577,       480,       400,       334,      285,      239,      199,
		179,       168,       143,       124,      106,      98,       93,
		88,
	},
	.temp = {	// temperature (degree K)
		C2K(-200), C2K(-150), C2K(-100), C2K(-50), C2K(0),	 C2K(20),  C2K(50),
		C2K(100),  C2K(150),  C2K(200),  C2K(250), C2K(300), C2K(350), C2K(400),
		C2K(430),  C2K(450),  C2K(500),  C2K(550), C2K(600), C2K(630), C2K(650),
		C2K(670),
	},
};


static struct adc2temp_lookuptbl adc2temp_lut_for_adcin = {
	// Case of NCP03XH223 for ADC-IN
	.adc  = {  // ADC-12 input value
		3967,      3229,      2643,      2176,     1801,     1498,     1344,
		1251,      1166,      1051,      948,      885,      828,      749,
		679,       636,       597,       543,      495,      465,      438,
		401,
	},
	.temp = {	// temperature (degree K)
		C2K(50),  C2K(100), C2K(150), C2K(200), C2K(250), C2K(300), C2K(330),
		C2K(350), C2K(370), C2K(400), C2K(430), C2K(450), C2K(470), C2K(500),
		C2K(530), C2K(550), C2K(570), C2K(600), C2K(630), C2K(650), C2K(670),
		C2K(700),
	},
};



static u16 temp_lut_length = (u16)sizeof(adc2temp_lut.adc)/sizeof(u16);
static u16 adcin_lut_length = (u16)sizeof(adc2temp_lut_for_adcin.adc)/sizeof(u16);

static struct adc2vbat_lookuptbl adc2vbat_lut = {
	.adc	 = {1843, 1946, 2148, 2253, 2458, 2662, 2867, 2683, 3072, 3482,}, // ADC-12 input value
	.offset  = {   0,	 0,    0,	 0,    0,	 0,    0,	 0,    0,    0,}, // charging mode ADC offset
	.vbat	 = {3400, 3450, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200,}, // VBAT (mV)
};

#if USED_BATTERY_CAPACITY == BAT_CAPACITY_1800MA
static struct adc2soc_lookuptbl adc2soc_lut = {
//         {3400, 3471, 3561, 3604, 3640, 3673, 3765, 3781, 3788, 3814, 3876, 3932, 3996, 4082, 4153}
.adc_ht  = {1843, 1988, 2173, 2261, 2335, 2402, 2591, 2623, 2638, 2691, 2818, 2933, 3064, 3240, ADC_VAL_100_PERCENT}, // ADC input @ high temp
.adc_rt  = {1843, 1988, 2173, 2261, 2335, 2402, 2591, 2623, 2638, 2691, 2818, 2933, 3064, 3240, ADC_VAL_100_PERCENT}, // ADC input @ room temp
.adc_rlt = {1843, 1988, 2173, 2261, 2335, 2402, 2591, 2623, 2638, 2691, 2818, 2933, 3064, 3240, ADC_VAL_100_PERCENT}, // ADC input @ low temp(0)
.adc_lt  = {1843, 1988, 2173, 2261, 2335, 2402, 2591, 2623, 2638, 2691, 2818, 2933, 3064, 3240, ADC_VAL_100_PERCENT}, // ADC input @ low temp(0)
.adc_lmt = {1843, 1988, 2173, 2261, 2335, 2402, 2591, 2623, 2638, 2691, 2818, 2933, 3064, 3240, ADC_VAL_100_PERCENT}, // ADC input @ low mid temp(-10)
.adc_llt = {1843, 1988, 2173, 2261, 2335, 2402, 2591, 2623, 2638, 2691, 2818, 2933, 3064, 3240, ADC_VAL_100_PERCENT}, // ADC input @ low low temp(-20)
.soc     = {   0,   10,   30,   50,   70,  100,  200,  300,  400,  500,  600,  700,  800,  900, 1000}, // SoC in %
};


//Discharging Weight(Room/Low/low low)           //     0,    1,     3,    5,    7,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100
static u16 adc_weight_section_discharge[]        = {14100, 4800,  3385,  965,  393,  425,  471,  485,  638,  683,  793,  885, 1002, 1328, 2495};
static u16 adc_weight_section_discharge_rlt[]    = {14100, 9100,  8985, 1585,  525,  352,  132,  119,  165,  375,  355,  514,  769, 1228, 2495};
static u16 adc_weight_section_discharge_lt[]     = {14100, 9100,  8985, 1585,  525,  352,  132,  119,  165,  375,  355,  514,  769, 1228, 2495};
static u16 adc_weight_section_discharge_lmt[]    = {14100, 9100,  8985, 1585,  525,  352,  132,  119,  165,  375,  355,  514,  769, 1228, 2495};
static u16 adc_weight_section_discharge_llt[]    = {19100, 9400, 10985, 1985,  480,  337,  132,  119,  165,  375,  355,  514,  769, 1228, 2495};

//Discharging Weight(Room/Low/low low)           //   0, 1-2, 3-4, 5-6, 7-9,10-19,20-29,30-39,40-49,50-59,60-69,70-79,80-89,90-99, 100
static u16 adc_weight_section_discharge_1c[]     = {215, 215, 103,  81,  47,   42,   8,    4,   12,   26,   25,   32,   45,   52,  52};
static u16 adc_weight_section_discharge_rlt_1c[] = {215, 215, 103,  81,  47,   42,   8,    4,   12,   26,   25,   32,   45,   52,  52};
static u16 adc_weight_section_discharge_lt_1c[]  = {215, 215, 103,  81,  47,   42,   8,    4,   12,   26,   25,   32,   45,   52,  52};
static u16 adc_weight_section_discharge_lmt_1c[] = {215, 215, 103,  81,  47,   42,   8,    4,   12,   26,   25,   32,   45,   52,  52};
static u16 adc_weight_section_discharge_llt_1c[] = {215, 215, 103,  81,  47,   42,   8,    4,   12,   26,   25,   32,   45,   52,  52};

//Discharging Weight(Room/Low/low low)           //   0, 1-2, 3-4, 5-6, 7-9,10-19,20-29,30-39,40-49,50-59,60-69,70-79,80-89,90-99, 100
static u16 adc_weight_section_discharge_2c[]     = {240, 240, 113,  92,  57,   49,    9,    5,   15,   37,   32,   36,   50,   56,  56};
static u16 adc_weight_section_discharge_rlt_2c[] = {240, 240, 113,  92,  57,   49,    9,    5,   15,   37,   32,   36,   50,   56,  56};
static u16 adc_weight_section_discharge_lt_2c[]  = {240, 240, 113,  92,  57,   49,    9,    5,   15,   37,   32,   36,   50,   56,  56};
static u16 adc_weight_section_discharge_lmt_2c[] = {240, 240, 113,  92,  57,   49,    9,    5,   15,   37,   32,   36,   50,   56,  56};
static u16 adc_weight_section_discharge_llt_2c[] = {240, 240, 113,  92,  57,   49,    9,    5,   15,   37,   32,   36,   50,   56,  56};

//Charging Weight(Room/Low/low low)              //   0, 1-2, 3-4, 5-6, 7-9,10-19,20-29,30-39,40-49,50-59,60-69,70-79,80-89,90-99, 100
static u16 adc_weight_section_charge[]          = {1281, 921, 459, 339, 165,  181,   34,   14,   42,  124,  103,  121,  138,  188, 188};
static u16 adc_weight_section_charge_rlt[]      = {1281, 921, 459, 339, 165,  181,   34,   14,   42,  124,  103,  121,  138,  188, 188};
static u16 adc_weight_section_charge_lt[]       = {1281, 921, 459, 339, 165,  181,   34,   14,   42,  124,  103,  121,  138,  188, 188};
static u16 adc_weight_section_charge_lmt[]      = {1281, 921, 459, 339, 165,  181,   34,   14,   42,  124,  103,  121,  138,  188, 188};
static u16 adc_weight_section_charge_llt[]      = {1281, 921, 459, 339, 165,  181,   34,   14,   42,  124,  103,  121,  138,  188, 188};


static struct diff_tbl dischg_diff_tbl = {
   //  0,  1-2,  3-4,  5-6,  7-9,10-19,20-29,30-39,40-49,50-59,60-69,70-79,80-89,90-99, 100
    {118,  118,  131,  135,  135,  126,  130,  127,  128,  138,  128,  114,  109,  103, 103},
    {474,  474,  474,  467,  454,  432,  430,  408,  402,  420,  429,  409,  396,  380, 380},
};

static u16 fg_reset_drop_offset[] = {86, 86};


#elif USED_BATTERY_CAPACITY == BAT_CAPACITY_2100MA

static struct adc2soc_lookuptbl adc2soc_lut = {
	.adc_ht  = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666, 2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,}, // ADC input @ high temp
	.adc_rt  = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666, 2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,}, // ADC input @ room temp
	.adc_rlt = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666, 2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,}, // ADC input @ low temp(0)
	.adc_lt  = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666, 2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,}, // ADC input @ low temp(0)
	.adc_lmt = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666, 2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,}, // ADC input @ low mid temp(-10)
	.adc_llt = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666, 2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,}, // ADC input @ low low temp(-20)
	.soc	 = {   0,	10,   30,	50,   70,  100,  200,  300,  400,  500,  600,  700,  800,  900, 1000,}, // SoC in %
};

//Discharging Weight(Room/Low/low low)           //     0,    1,     3,    5,    7,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100
static u16 adc_weight_section_discharge[]        = {14100, 4800,  3385,  965,  393,  425,  471,  485,  638,  683,  793,  885, 1002, 1328, 2495};
static u16 adc_weight_section_discharge_rlt[]    = {14100, 9100,  8985, 1585,  525,  352,  132,  119,  165,  375,  355,  514,  769, 1228, 2495};
static u16 adc_weight_section_discharge_lt[]     = {14100, 9100,  8985, 1585,  525,  352,  132,  119,  165,  375,  355,  514,  769, 1228, 2495};
static u16 adc_weight_section_discharge_lmt[]    = {14100, 9100,  8985, 1585,  525,  352,  132,  119,  165,  375,  355,  514,  769, 1228, 2495};
static u16 adc_weight_section_discharge_llt[]    = {19100, 9400, 10985, 1985,  480,  337,  132,  119,  165,  375,  355,  514,  769, 1228, 2495};

//These table are for 0.1C load currents.
//Discharging Weight(Room/Low/low low)           //   0,   1,   3,   5,   7,  10,  20,  30,  40,  50,  60,  70,  80,  90, 100
static u16 adc_weight_section_discharge_1c[]     = { 41, 123,  84,  36,  18,  25,  12,  10,  19,  32,  41,  60,  82,  94, 303};
static u16 adc_weight_section_discharge_rlt_1c[] = { 41, 123,  84,  36,  18,  25,  12,  10,  19,  32,  41,  60,  82,  94, 303};
static u16 adc_weight_section_discharge_lt_1c[]  = { 41, 123,  84,  36,  18,  25,  12,  10,  19,  32,  41,  60,  82,  94, 303};
static u16 adc_weight_section_discharge_lmt_1c[] = { 41, 123,  84,  36,  18,  25,  12,  10,  19,  32,  41,  60,  82,  94, 303};
static u16 adc_weight_section_discharge_llt_1c[] = { 41, 123,  84,  36,  18,  25,  12,  10,  19,  32,  41,  60,  82,  94, 303};

//These table are for 0.2C load currents.
//Discharging Weight(Room/Low/low low)           //   0,   1,   3,   5,   7,  10,  20,  30,  40,  50,  60,  70,  80,  90, 100
static u16 adc_weight_section_discharge_2c[]     = { 47, 141,  93,  59,  25,  33,  15,  12,  24,  43,  45,  65,  89,  95, 303};
static u16 adc_weight_section_discharge_rlt_2c[] = { 47, 141,  93,  59,  25,  33,  15,  12,  24,  43,  45,  65,  89,  95, 303};
static u16 adc_weight_section_discharge_lt_2c[]  = { 47, 141,  93,  59,  25,  33,  15,  12,  24,  43,  45,  65,  89,  95, 303};
static u16 adc_weight_section_discharge_lmt_2c[] = { 47, 141,  93,  59,  25,  33,  15,  12,  24,  43,  45,  65,  89,  95, 303};
static u16 adc_weight_section_discharge_llt_2c[] = { 47, 141,  93,  59,  25,  33,  15,  12,  24,  43,  45,  65,  89,  95, 303};

//Charging Weight(Room/Low/low low)              //    0,   1,   3,   5,   7,  10,  20,  30,  40,  50,  60,  70,  80,  90, 100
static u16 adc_weight_section_charge[]           = {2950, 954, 468, 325, 102,  94,  33,  30,  52, 101, 112, 132, 141, 115, LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_rlt[]		 = {2950, 954, 468, 325, 102,  94,  33,  30,  52, 101, 112, 132, 141, 115, LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_lt[]		 = {2950, 954, 468, 325, 102,  94,  33,  30,  52, 101, 112, 132, 141, 115, LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_lmt[]		 = {2950, 954, 468, 325, 102,  94,  33,  30,  52, 101, 112, 132, 141, 115, LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_llt[]		 = {2950, 954, 468, 325, 102,  94,  33,  30,  52, 101, 112, 132, 141, 115, LAST_CHARGING_WEIGHT};

static struct diff_tbl dischg_diff_tbl = {
   //  0,  1-2,  3-4,  5-6,  7-9,10-19,20-29,30-39,40-49,50-59,60-69,70-79,80-89,90-99, 100
	{355,  277,  267,  264,  180,  117,  110,  117,  115,  115,   95,   79,   69,   30,  35},  // 0.1C
	{710,  706,  513,  409,  350,  249,  212,  202,  197,  211,  206,  182,  155,  135, 164},  // 0.2C
};

static u16 fg_reset_drop_offset[] = {86, 86};

#elif USED_BATTERY_CAPACITY == BAT_CAPACITY_2200MA

static struct adc2soc_lookuptbl adc2soc_lut = {
		    /* ADC input @ high temp */
	.adc_ht  = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666,
		    2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,},
		    /* ADC input @ room temp */
	.adc_rt  = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666,
		    2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,},
		    /* ADC input @ low temp(0) */
	.adc_rlt = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666,
		    2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,},
		    /* ADC input @ low temp(0) */
	.adc_lt  = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666,
		    2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,},
		    /* ADC input @ low mid temp(-10) */
	.adc_lmt = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666,
		    2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,},
		    /* ADC input @ low low temp(-20) */
	.adc_llt = {1843, 1891, 2170, 2314, 2395, 2438, 2573, 2623, 2666,
		    2746, 2898, 3057, 3246, 3466, ADC_VAL_100_PERCENT,},
		     /* SoC in % */
	.soc	 = {   0,	10,   30,	50,   70,  100,  200, 300,
		     400,  500,  600,  700,  800,  900, 1000,},
};

/* Discharging Weight(Room/Low/low low) */
						    /* 0,    1,     3,    5,
						       7,   10,    20,   30,
						       40,   50,   60,   70,
						       80,   90,  100 */
static u16 adc_weight_section_discharge[]        = {14100, 4800,  3385,  965,
						      393,  425,  471,  485,
						      638,  683,  793,  885,
						      1002, 1328, 2495};
static u16 adc_weight_section_discharge_rlt[]    = {14100, 9100,  8985, 1585,
						      525,  352,  132,  119,
						      165,  375,  355,  514,
						      769, 1228, 2495};
static u16 adc_weight_section_discharge_lt[]     = {14100, 9100,  8985, 1585,
						      525,  352,  132,  119,
						      165,  375,  355,  514,
						      769, 1228, 2495};
static u16 adc_weight_section_discharge_lmt[]    = {14100, 9100,  8985, 1585,
						      525,  352,  132,  119,
						      165,  375,  355,  514,
						      769, 1228, 2495};
static u16 adc_weight_section_discharge_llt[]    = {19100, 9400, 10985, 1985,
						      480,  337,  132,  119,
						      165,  375,  355,  514,
						      769, 1228, 2495};

/* These table are for 0.1C load currents. */
/* Discharging Weight(Room/Low/low low) */
						   /* 0,   1,   3,   5,   7,
						     10,  20,  30,  40,  50,
						     60,  70,  80,  90, 100 */
static u16 adc_weight_section_discharge_1c[]     = { 41, 123,  84,  36,  18,
						     25,  12,  10,  19,  32,
						     41,  60,  82,  94, 303};
static u16 adc_weight_section_discharge_rlt_1c[] = { 41, 123,  84,  36,  18,
						     25,  12,  10,  19,  32,
						     41,  60,  82,  94, 303};
static u16 adc_weight_section_discharge_lt_1c[]  = { 41, 123,  84,  36,  18,
						     25,  12,  10,  19,  32,
						     41,  60,  82,  94, 303};
static u16 adc_weight_section_discharge_lmt_1c[] = { 41, 123,  84,  36,  18,
						     25,  12,  10,  19,  32,
						     41,  60,  82,  94, 303};
static u16 adc_weight_section_discharge_llt_1c[] = { 41, 123,  84,  36,  18,
						     25,  12,  10,  19,  32,
						     41,  60,  82,  94, 303};

/* These table are for 0.2C load currents. */
/* Discharging Weight(Room/Low/low low) */
						   /* 0,   1,   3,   5,   7,
						     10,  20,  30,  40,  50,
						     60,  70,  80,  90, 100 */
static u16 adc_weight_section_discharge_2c[]     = { 47, 141,  93,  59,  25,
						     33,  15,  12,  24,  43,
						     45,  65,  89,  95, 303};
static u16 adc_weight_section_discharge_rlt_2c[] = { 47, 141,  93,  59,  25,
						     33,  15,  12,  24,  43,
						     45,  65,  89,  95, 303};
static u16 adc_weight_section_discharge_lt_2c[]  = { 47, 141,  93,  59,  25,
						     33,  15,  12,  24,  43,
						     45,  65,  89,  95, 303};
static u16 adc_weight_section_discharge_lmt_2c[] = { 47, 141,  93,  59,  25,
						     33,  15,  12,  24,  43,
						     45,  65,  89,  95, 303};
static u16 adc_weight_section_discharge_llt_2c[] = { 47, 141,  93,  59,  25,
						     33,  15,  12,  24,  43,
						     45,  65,  89,  95, 303};

/* Charging Weight(Room/Low/low low) */
						    /* 0,   1,   3,   5,   7,
						      10,  20,  30,  40,  50,
						      60,  70,  80,  90,
						      100 */
static u16 adc_weight_section_charge[]           = {2950, 954, 468, 325, 102,
						      94,  33,  30,  52, 101,
						      112, 132, 141, 115,
						      LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_rlt[]	 = {2950, 954, 468, 325, 102,
						      94,  33,  30,  52, 101,
						      112, 132, 141, 115,
						      LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_lt[]	 = {2950, 954, 468, 325, 102,
						      94,  33,  30,  52, 101,
						      112, 132, 141, 115,
						      LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_lmt[]	 = {2950, 954, 468, 325, 102,
						      94,  33,  30,  52, 101,
						      112, 132, 141, 115,
						      LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_llt[]	 = {2950, 954, 468, 325, 102,
						      94,  33,  30,  52, 101,
						      112, 132, 141, 115,
						      LAST_CHARGING_WEIGHT};

static struct diff_tbl dischg_diff_tbl = {
	/*  0,  1-2,  3-4,  5-6,  7-9,10-19,20-29,30-39,
	40-49,50-59,60-69,70-79,80-89,90-99, 100 */
	{ 355,  277,  267,  264,  180,  117,  110,  117,
	  115,  115,   95,   79,   69,   30,  35},  /* 0.1C */
	{ 710,  706,  513,  409,  350,  249,  212,  202,
	  197,  211,  206,  182,  155,  135, 164},  /* 0.2C */
};

static u16 fg_reset_drop_offset[] = {86, 86};


#else
// For 2100mAh battery
static struct adc2soc_lookuptbl adc2soc_lut = {
	.adc_ht  = {1843, 1911, 2206, 2375, 2395, 2415, 2567, 2623, 2670, 2755, 2921, 3069, 3263, 3490, ADC_VAL_100_PERCENT,}, // ADC input @ high temp
	.adc_rt  = {1843, 1911, 2206, 2375, 2395, 2415, 2567, 2623, 2670, 2755, 2921, 3069, 3263, 3490, ADC_VAL_100_PERCENT,}, // ADC input @ room temp
	.adc_rlt = {1843, 1911, 2206, 2375, 2395, 2415, 2567, 2623, 2670, 2755, 2921, 3069, 3263, 3490, ADC_VAL_100_PERCENT,}, // ADC input @ low temp(0)
	.adc_lt  = {1843, 1911, 2206, 2375, 2395, 2415, 2567, 2623, 2670, 2755, 2921, 3069, 3263, 3490, ADC_VAL_100_PERCENT,}, // ADC input @ low temp(0)
	.adc_lmt = {1843, 1911, 2206, 2375, 2395, 2415, 2567, 2623, 2670, 2755, 2921, 3069, 3263, 3490, ADC_VAL_100_PERCENT,}, // ADC input @ low mid temp(-10)
	.adc_llt = {1843, 1911, 2206, 2375, 2395, 2415, 2567, 2623, 2670, 2755, 2921, 3069, 3263, 3490, ADC_VAL_100_PERCENT,}, // ADC input @ low low temp(-20)
	.soc	 = {   0,	10,   30,	50,   70,  100,  200,  300,  400,  500,  600,  700,  800,  900, 1000,}, // SoC in %
};

//Discharging Weight(Room/Low/low low) for 0.1C
//Discharging Weight(Room/Low/low low)           //   0,   1,   3,   5,   7,  10,  20,  30,  40,  50,  60,  70,  80,  90, 100
//static u16 adc_weight_section_discharge_1c[]   = { 50,  95,  75,  15,  15,  25,  10,  10,  17,  28,  32,  54,  75, 100, 295};
static u16 adc_weight_section_discharge_1c[]     = { 49,  40,  15,  10,  14,  23,  10,  10,  17,  28,  32,  54,  75, 100, 295};
static u16 adc_weight_section_discharge_rlt_1c[] = { 49,  40,  15,  10,  14,  23,  10,  10,  17,  28,  32,  54,  75, 100, 295};
static u16 adc_weight_section_discharge_lt_1c[]  = { 49,  40,  15,  10,  14,  23,  10,  10,  17,  28,  32,  54,  75, 100, 295};
static u16 adc_weight_section_discharge_lmt_1c[] = { 49,  40,  15,  10,  14,  23,  10,  10,  17,  28,  32,  54,  75, 100, 295};
static u16 adc_weight_section_discharge_llt_1c[] = { 49,  40,  15,  10,  14,  23,  10,  10,  17,  28,  32,  54,  75, 100, 295};

//Discharging Weight(Room/Low/low low) for 0.2C
//Discharging Weight(Room/Low/low low)           //   0,   1,   3,   5,   7,  10,  20,  30,  40,  50,  60,  70,  80,  90, 100
//static u16 adc_weight_section_discharge_2c[]   = { 50, 245, 210,  24,  18,  40,  14,  12,  23,  38,  37,  54,  75,  75, 265};
static u16 adc_weight_section_discharge_2c[]     = { 50, 215, 201,  24,  19,  42,  15,  13,  24,  38,  37,  54,  75,  75, 265};
static u16 adc_weight_section_discharge_rlt_2c[] = { 50, 215, 201,  24,  19,  42,  15,  13,  24,  38,  37,  54,  75,  75, 265};
static u16 adc_weight_section_discharge_lt_2c[]  = { 50, 215, 201,  24,  19,  42,  15,  13,  24,  38,  37,  54,  75,  75, 265};
static u16 adc_weight_section_discharge_lmt_2c[] = { 50, 215, 201,  24,  19,  42,  15,  13,  24,  38,  37,  54,  75,  75, 265};
static u16 adc_weight_section_discharge_llt_2c[] = { 50, 215, 201,  24,  19,  42,  15,  13,  24,  38,  37,  54,  75,  75, 265};

//Charging Weight(Room/Low/low low)              //    0,   1,   3,   5,   7,  10,  20,  30,  40,  50,  60,  70,  80,  90, 100
//static u16 adc_weight_section_charge[]         = {2800, 642, 620,  63,  53, 108,  40,  33,  58, 113, 107, 133, 142,  63, LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge[]           = {2800, 642, 620,  63,  53, 106,  37,  31,  54, 108, 102, 130, 147,  70, LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_rlt[]		 = {2800, 642, 620,  63,  53, 106,  37,  31,  54, 108, 102, 130, 147,  70, LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_lt[]		 = {2800, 642, 620,  63,  53, 106,  37,  31,  54, 108, 102, 130, 147,  70, LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_lmt[]		 = {2800, 642, 620,  63,  53, 106,  37,  31,  54, 108, 102, 130, 147,  70, LAST_CHARGING_WEIGHT};
static u16 adc_weight_section_charge_llt[]		 = {2800, 642, 620,  63,  53, 106,  37,  31,  54, 108, 102, 130, 147,  70, LAST_CHARGING_WEIGHT};

static struct diff_tbl dischg_diff_tbl = {
   //  0,  1-2,  3-4,  5-6,  7-9,10-19,20-29,30-39,40-49,50-59,60-69,70-79,80-89,90-99, 100
	{400,  360,  230,  120,  120,  130,  110,  110,  110,  115,  100,   75,   60,   30,  20},  // 0.1C
	{635,  465,  270,  245,  220,  245,  230,  225,  217,  245,  245,  213,  178,  153, 250},  // 0.2C
};

static u16 fg_reset_drop_offset[] = {172, 98};

#endif /* USED_BATTERY_CAPACITY == BAT_CAPACITY_????MA */


static u16 adc2soc_lut_length = (u16)sizeof(adc2soc_lut.soc)/sizeof(u16);
static u16 adc2vbat_lut_length = (u16)sizeof(adc2vbat_lut.offset)/sizeof(u16);


#ifdef CONFIG_D2153_DEBUG_FEATURE
unsigned int d2153_attr_idx=0;

static ssize_t d2153_battery_attrs_show(struct device *pdev, struct device_attribute *attr, char *buf);

enum {
	D2153_PROP_SOC_LUT = 0,
	D2153_PROP_DISCHG_WEIGHT,
	D2153_PROP_CHG_WEIGHT,
	D2153_PROP_BAT_CAPACITY,
	/* If you want to add a property,
	   then please add before "D2153_PROP_ALL" attributes */
	D2153_PROP_ALL,
};

#define D2153_PROP_MAX			(D2153_PROP_ALL + 1)

static struct device_attribute d2153_battery_attrs[]=
{
	__ATTR(display_soc_lut, S_IRUGO, d2153_battery_attrs_show, NULL),
	__ATTR(display_dischg_weight, S_IRUGO, d2153_battery_attrs_show, NULL),
	__ATTR(display_chg_weight, S_IRUGO, d2153_battery_attrs_show, NULL),
	__ATTR(display_capacity, S_IRUGO, d2153_battery_attrs_show, NULL),
	/* If you want to add a property,
	   then please add before "D2153_PROP_ALL" attributes */
	__ATTR(display_all_information, S_IRUGO, d2153_battery_attrs_show, NULL),
};

static ssize_t d2153_battery_attrs_show(struct device *pdev,
										struct device_attribute *attr,
										char *buf)
{
	u8 i = 0, length = 0;
	ssize_t count = 0;
	unsigned int view_all = 0;
	const ptrdiff_t off = attr - d2153_battery_attrs;

	switch(off) {
		case D2153_PROP_ALL:
			view_all=1;

		case D2153_PROP_SOC_LUT:
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"\n## SOC Look up table ...\n");
			// High temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc2soc_lut.adc_ht  = {");
			for(i = 0; i < adc2soc_lut_length; i++) {
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%4d,", adc2soc_lut.adc_ht[i]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Room temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc2soc_lut.adc_rt  = {");
			for(i = 0; i < adc2soc_lut_length; i++) {
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%4d,", adc2soc_lut.adc_rt[i]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Room-low temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc2soc_lut.adc_rlt = {");
			for(i = 0; i < adc2soc_lut_length; i++) {
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%4d,", adc2soc_lut.adc_rlt[i]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Low temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc2soc_lut.adc_lt  = {");
			for(i = 0; i < adc2soc_lut_length; i++) {
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%4d,", adc2soc_lut.adc_lt[i]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Low-mid temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc2soc_lut.adc_lmt = {");
			for(i = 0; i < adc2soc_lut_length; i++) {
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%4d,", adc2soc_lut.adc_lmt[i]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Low-low temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc2soc_lut.adc_llt = {");
			for(i = 0; i < adc2soc_lut_length; i++) {
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%4d,", adc2soc_lut.adc_llt[i]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			if (!view_all) break;

		case D2153_PROP_DISCHG_WEIGHT:
			length = (u8)sizeof(adc_weight_section_discharge)/sizeof(u16);
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"\n## Discharging weight table ...\n");

			// Discharge weight at high and room temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge     = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at room-low temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_rlt = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_rlt[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_lt  = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_lt[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low-mid temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_lmt = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_lmt[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low-low temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_llt = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_llt[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

#ifdef CONFIG_D2153_MULTI_WEIGHT
			// Discharge weight at high and room temperature. for 0.1c
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_1c     = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_1c[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at room-low temperature. for 0.1c
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_rlt_1c = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_rlt_1c[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low temperature. for 0.1c
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_lt_1c  = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_lt_1c[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low-mid temperature. for 0.1c
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_lmt_1c = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_lmt_1c[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low-low temperature. for 0.1c
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_llt_1c = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_llt_1c[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");


			// Discharge weight at high and room temperature. for 0.2c
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_2c     = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_2c[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at room-low temperature. for 0.2
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_rlt_2c = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_rlt_2c[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low temperature. for 0.2c
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_lt_2c  = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_lt_2c[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low-mid temperature. for 0.2c
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_lmt_2c = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_lmt_2c[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low-low temperature. for 0.2c
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_discharge_llt_2c = {");
			for(i = 0; i < length; i++)
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_discharge_llt_2c[i]);
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");
#endif /* CONFIG_D2153_MULTI_WEIGHT */

			if (!view_all) break;

		case D2153_PROP_CHG_WEIGHT:
			length = (u8)sizeof(adc_weight_section_charge)/sizeof(u16);
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"\n## Charging weight table ...\n");

			// Discharge weight at high and room temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_charge     = {");
			for(i = 0; i < length; i++) {
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_charge[i]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at room-low temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_charge_rlt = {");
			for(i = 0; i < length; i++) {
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_charge_rlt[i]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_charge_lt  = {");
			for(i = 0; i < length; i++) {
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_charge_lt[i]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low-mid temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_charge_lmt = {");
			for(i = 0; i < length; i++) {
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_charge_lmt[i]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			// Discharge weight at low-low temperature
			count += scnprintf(buf + count, PAGE_SIZE - count,
									"adc_weight_section_charge_llt = {");
			for(i = 0; i < length; i++) {
				count += scnprintf(buf + count, PAGE_SIZE - count,
									"%5d,", adc_weight_section_charge_llt[i]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "}\n");

			if (!view_all) break;

		case D2153_PROP_BAT_CAPACITY:
			count += scnprintf(buf + count, PAGE_SIZE - count,
								"\n## Battery Capacity is  %d mAh\n",
								USED_BATTERY_CAPACITY);
			if (!view_all)
				break;

		default:
			break;
	}

	return count;
}

#endif /* CONFIG_D2153_DEBUG_FEATURE */

/*
 * Name : chk_lut
 *
 */
static int chk_lut (u16* x, u16* y, u16 v, u16 l) {
	int i;
	//u32 ret;
	int ret;

	if (v < x[0])
		ret = y[0];
	else if (v >= x[l-1])
		ret = y[l-1];
	else {
		for (i = 1; i < l; i++) {
			if (v < x[i])
				break;
		}
		ret = y[i-1];
		ret = ret + ((v-x[i-1])*(y[i]-y[i-1]))/(x[i]-x[i-1]);
	}
	//return (u16) ret;
	return ret;
}

/*
 * Name : chk_lut_temp
 * return : The return value is Kelvin degree
 */
static int chk_lut_temp (u16* x, u16* y, u16 v, u16 l) {
	int i, ret;

	if (v >= x[0])
		ret = y[0];
	else if (v < x[l-1])
		ret = y[l-1];
	else {
		for (i=1; i < l; i++) {
			if (v > x[i])
				break;
		}
		ret = y[i-1];
		ret = ret + ((v-x[i-1])*(y[i]-y[i-1]))/(x[i]-x[i-1]);
	}

	return ret;
}


/*
 * Name : adc_to_soc_with_temp_compensat
 *
 */
u32 adc_to_soc_with_temp_compensat(u16 adc, u16 temp) {
	int sh, sl;

	if(temp < BAT_LOW_LOW_TEMPERATURE)
		temp = BAT_LOW_LOW_TEMPERATURE;
	else if(temp > BAT_HIGH_TEMPERATURE)
		temp = BAT_HIGH_TEMPERATURE;

	if((temp <= BAT_HIGH_TEMPERATURE) && (temp > BAT_ROOM_TEMPERATURE)) {
		sh = chk_lut(adc2soc_lut.adc_ht, adc2soc_lut.soc, adc, adc2soc_lut_length);
		sl = chk_lut(adc2soc_lut.adc_rt, adc2soc_lut.soc, adc, adc2soc_lut_length);
		sh = sl + (temp - BAT_ROOM_TEMPERATURE)*(sh - sl)
								/ (BAT_HIGH_TEMPERATURE - BAT_ROOM_TEMPERATURE);
	} else if((temp <= BAT_ROOM_TEMPERATURE) && (temp > BAT_ROOM_LOW_TEMPERATURE)) {
		sh = chk_lut(adc2soc_lut.adc_rt, adc2soc_lut.soc, adc, adc2soc_lut_length);
		sl = chk_lut(adc2soc_lut.adc_rlt, adc2soc_lut.soc, adc, adc2soc_lut_length);
		sh = sl + (temp - BAT_ROOM_LOW_TEMPERATURE)*(sh - sl)
								/ (BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);
	} else if((temp <= BAT_ROOM_LOW_TEMPERATURE) && (temp > BAT_LOW_TEMPERATURE)) {
		sh = chk_lut(adc2soc_lut.adc_rlt, adc2soc_lut.soc, adc, adc2soc_lut_length);
		sl = chk_lut(adc2soc_lut.adc_lt, adc2soc_lut.soc, adc, adc2soc_lut_length);
		sh = sl + (temp - BAT_LOW_TEMPERATURE)*(sh - sl)
								/ (BAT_ROOM_LOW_TEMPERATURE-BAT_LOW_TEMPERATURE);
	} else if((temp <= BAT_LOW_TEMPERATURE) && (temp > BAT_LOW_MID_TEMPERATURE)) {
		sh = chk_lut(adc2soc_lut.adc_lt, adc2soc_lut.soc, adc, adc2soc_lut_length);
		sl = chk_lut(adc2soc_lut.adc_lmt, adc2soc_lut.soc, adc, adc2soc_lut_length);
		sh = sl + (temp - BAT_LOW_MID_TEMPERATURE)*(sh - sl)
								/ (BAT_LOW_TEMPERATURE-BAT_LOW_MID_TEMPERATURE);
	} else {
		sh = chk_lut(adc2soc_lut.adc_lmt, adc2soc_lut.soc,	adc, adc2soc_lut_length);
		sl = chk_lut(adc2soc_lut.adc_llt, adc2soc_lut.soc, adc, adc2soc_lut_length);
		sh = sl + (temp - BAT_LOW_LOW_TEMPERATURE)*(sh - sl)
								/ (BAT_LOW_MID_TEMPERATURE-BAT_LOW_LOW_TEMPERATURE);
	}

	return sh;
}


/*
 * Name : soc_to_adc_with_temp_compensat
 *
 */
u32 soc_to_adc_with_temp_compensat(u16 soc, u16 temp) {
	int sh, sl;

	if(temp < BAT_LOW_LOW_TEMPERATURE)
		temp = BAT_LOW_LOW_TEMPERATURE;
	else if(temp > BAT_HIGH_TEMPERATURE)
		temp = BAT_HIGH_TEMPERATURE;

	pr_batt(VERBOSE,
		"%s. Parameter. SOC = %d. temp = %d\n",
		__func__, soc, temp);

	if((temp <= BAT_HIGH_TEMPERATURE) && (temp > BAT_ROOM_TEMPERATURE)) {
		sh = chk_lut(adc2soc_lut.soc, adc2soc_lut.adc_ht, soc, adc2soc_lut_length);
		sl = chk_lut(adc2soc_lut.soc, adc2soc_lut.adc_rt, soc, adc2soc_lut_length);
		sh = sl + (temp - BAT_ROOM_TEMPERATURE)*(sh - sl)
								/ (BAT_HIGH_TEMPERATURE - BAT_ROOM_TEMPERATURE);
	} else if((temp <= BAT_ROOM_TEMPERATURE) && (temp > BAT_ROOM_LOW_TEMPERATURE)) {
		sh = chk_lut(adc2soc_lut.soc, adc2soc_lut.adc_rt, soc, adc2soc_lut_length);
		sl = chk_lut(adc2soc_lut.soc, adc2soc_lut.adc_rlt, soc, adc2soc_lut_length);
		sh = sl + (temp - BAT_ROOM_LOW_TEMPERATURE)*(sh - sl)
								/ (BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);
	} else if((temp <= BAT_ROOM_LOW_TEMPERATURE) && (temp > BAT_LOW_TEMPERATURE)) {
		sh = chk_lut(adc2soc_lut.soc, adc2soc_lut.adc_rlt, soc, adc2soc_lut_length);
		sl = chk_lut(adc2soc_lut.soc, adc2soc_lut.adc_lt, soc, adc2soc_lut_length);
		sh = sl + (temp - BAT_LOW_TEMPERATURE)*(sh - sl)
								/ (BAT_ROOM_LOW_TEMPERATURE-BAT_LOW_TEMPERATURE);
	} else if((temp <= BAT_LOW_TEMPERATURE) && (temp > BAT_LOW_MID_TEMPERATURE)) {
		sh = chk_lut(adc2soc_lut.soc, adc2soc_lut.adc_lt, soc, adc2soc_lut_length);
		sl = chk_lut(adc2soc_lut.soc, adc2soc_lut.adc_lmt, soc, adc2soc_lut_length);
		sh = sl + (temp - BAT_LOW_MID_TEMPERATURE)*(sh - sl)
								/ (BAT_LOW_TEMPERATURE-BAT_LOW_MID_TEMPERATURE);
	} else {
		sh = chk_lut(adc2soc_lut.soc, adc2soc_lut.adc_lmt,	soc, adc2soc_lut_length);
		sl = chk_lut(adc2soc_lut.soc, adc2soc_lut.adc_llt, soc, adc2soc_lut_length);
		sh = sl + (temp - BAT_LOW_LOW_TEMPERATURE)*(sh - sl)
								/ (BAT_LOW_MID_TEMPERATURE-BAT_LOW_LOW_TEMPERATURE);
	}

	return sh;
}


/*
 * Name : adc_to_degree
 *
 */
int adc_to_degree_k(u16 adc, u8 ch) {
	if(ch == D2153_ADC_AIN) {
		return (chk_lut_temp(adc2temp_lut_for_adcin.adc,
								adc2temp_lut_for_adcin.temp,
								adc, adcin_lut_length));
	} else {
	return (chk_lut_temp(adc2temp_lut.adc,
								adc2temp_lut.temp, adc, temp_lut_length));
	}
}

int degree_k2c(u16 k) {
	return (K2C(k));
}

/*
 * Name : get_adc_offset
 *
 */
int get_adc_offset(u16 adc) {
	return (chk_lut(adc2vbat_lut.adc, adc2vbat_lut.offset, adc, adc2vbat_lut_length));
}

/*
 * Name : adc_to_vbat
 *
 */
u16 adc_to_vbat(u16 adc, u8 chrgr_connected)
{
	u16 a = adc;

	if (chrgr_connected)
		a = adc - get_adc_offset(adc); // deduct charging offset
	// return (chk_lut(adc2vbat_lut.adc, adc2vbat_lut.vbat, a, adc2vbat_lut_length));
	return (2500 + ((a * 2000) >> 12));
}

/*
 * Name : adc_to_soc
 * get SOC (@ room temperature) according ADC input
 */
int adc_to_soc(u16 adc, u8 chrgr_connected)
{

	u16 a = adc;

	if (chrgr_connected)
		a = adc - get_adc_offset(adc); // deduct charging offset
	return (chk_lut(adc2soc_lut.adc_rt, adc2soc_lut.soc, a, adc2soc_lut_length));
}


/*
 * Name : do_interpolation
 */
int do_interpolation(int x0, int x1, int y0, int y1, int x)
{
	int y = 0;

	if(!(x1 - x0 )) {
		pr_batt(ERROR,
			"%s. Div by Zero, check x1(%d), x0(%d) value\n",
			__func__, x1, x0);
		return 0;
	}

	y = y0 + (x - x0)*(y1 - y0)/(x1 - x0);
	pr_batt(ERROR, "%s. Interpolated y_value is = %d\n", __func__, y);

	return y;
}


#define D2153_MAX_SOC_CHANGE_OFFSET		20

static void d2153_update_psy(struct d2153_battery *bdata, bool force_update)
{
#ifndef CONFIG_SEC_CHARGING_FEATURE
	if (bdata->prev_capacity != bdata->capacity) {
		pr_batt(VERBOSE, "Capacity Changed, so update Power Supply\n");
		power_supply_changed(&bdata->psy);
		bdata->prev_capacity = bdata->capacity;
		bdata->force_dbg_print = TRUE;
	} else if (force_update) {
		power_supply_changed(&bdata->psy);
		pr_batt(FLOW, "Force update\n");
	}
#ifdef custom_t_BATTERY	
	else
    {
        //after +- 1 dgree we report the message
        bool needReport = false;
        
        if (bdata->prev_current_temperature >= bdata->current_temperature)
        {      
            if(bdata->prev_current_temperature - bdata->current_temperature >= 10)
            {
                needReport = true;
            }
        }
        else
        {
            if(bdata->current_temperature - bdata->prev_current_temperature >= 10)
            {
                needReport = true;
            }
        }

        if (needReport)
        {
            pr_batt(FLOW,"current_temperature have a big change, so update Power Supply");			
            power_supply_changed(&bdata->psy);
            bdata->prev_current_temperature = bdata->current_temperature;
        }
    }
#endif
	
#endif
}
/*
 * Name : d2153_get_soc
 */
#ifdef CONFIG_SEC_CHARGING_FEATURE
static int d2153_get_soc(struct d2153_battery *bdata)
{
	int soc;

	if ((!bdata->volt_adc_init_done)) {
		pr_batt(ERROR, "%s. Invalid parameter\n", __func__);
		return -EINVAL;
	}

	pr_batt(VERBOSE, "%s. Getting SOC\n", __func__);

	if (bdata->soc)
		bdata->prev_soc = bdata->soc;

	soc = adc_to_soc_with_temp_compensat(bdata->average_volt_adc,
				C2K(bdata->current_temperature)); // BCM_ID_814048

	if(soc > FULL_CAPACITY) {
		soc = FULL_CAPACITY;
		if (bdata->virtual_battery_full == 1) {
			bdata->virtual_battery_full = 0;
			soc = FULL_CAPACITY;
		}
	}

	pr_batt(VERBOSE, "%s. 0. SOC = %d\n", __func__, soc);

	/* Don't allow soc goes up when battery is dicharged. */
	if (!bdata->charger_connected
		&& ((soc > bdata->prev_soc) && bdata->prev_soc)) {
		pr_batt(VERBOSE,
			"%s. Don't allow soc goes up SOC:%d\n", __func__, soc);
		soc = bdata->prev_soc;
	}

	// Write SOC and average VBAT ADC to GP Register
	d2153_reg_write(bdata->pd2153, D2153_GP_ID_2_REG, (0xFF & soc));
	d2153_reg_write(bdata->pd2153, D2153_GP_ID_3_REG, (0x0F & (soc>>8)));
	d2153_reg_write(bdata->pd2153, D2153_GP_ID_4_REG,
			(0xFF & bdata->average_volt_adc));
	d2153_reg_write(bdata->pd2153, D2153_GP_ID_5_REG,
			(0xF & (bdata->average_volt_adc>>8)));

	bdata->soc = soc;

	return soc;
}
#else
static int d2153_get_soc(struct d2153_battery *bdata)
{
	int soc;
	int ext_fg;
	ext_fg = 0;

	ext_fg = bcm_agent_get(BCM_AGENT_GET_FG_PRIVATE, NULL);
	if (ext_fg == 0) {
		if ((!bdata->volt_adc_init_done)) {
			pr_batt(ERROR, "%s. Invalid parameter\n", __func__);
			return -EINVAL;
		}
		if (bdata->soc)
			bdata->prev_soc = bdata->soc;
		soc = adc_to_soc_with_temp_compensat(bdata->average_volt_adc,
				C2K(bdata->current_temperature)); // BCM_ID_814048
	} else {
		if (bdata->soc)
			bdata->prev_soc = bdata->soc;
		soc = bcm_agent_get(BCM_AGENT_GET_SOC, NULL);
		if (soc < 0)
			soc = bdata->prev_soc;
		bdata->raw_soc = bcm_agent_get(BCM_AGENT_GET_RAW_SOC, NULL);
		bcm_agent_set(BCM_AGENT_SET_FG_TEMP,
			bdata->current_temperature);
	}
	if (soc > FULL_CAPACITY) {
		soc = FULL_CAPACITY;
		if (bdata->virtual_battery_full == 1) {
			bdata->virtual_battery_full = 0;
			soc = FULL_CAPACITY;
		}
	}
	pr_batt(FLOW, "%s. 0. SOC = %d\n", __func__, soc);
	/* Don't allow soc goes up when battery is dicharged. */
	if (!bdata->charger_connected
		&& ((soc > bdata->prev_soc) && bdata->prev_soc)) {
		pr_batt(VERBOSE,
			"%s. Don't allow soc goes up SOC:%d\n", __func__, soc);
		soc = bdata->prev_soc;
	}
	if (ext_fg == 0) {
		/* Write SOC and average VBAT ADC to GP Register*/
		d2153_reg_write(bdata->pd2153, D2153_GP_ID_2_REG, (0xFF & soc));
		d2153_reg_write(bdata->pd2153, D2153_GP_ID_3_REG,
					(0x0F & (soc>>8)));
		d2153_reg_write(bdata->pd2153, D2153_GP_ID_4_REG,
			(0xFF & bdata->average_volt_adc));
		d2153_reg_write(bdata->pd2153, D2153_GP_ID_5_REG,
			(0xF & (bdata->average_volt_adc>>8)));
	}

	bdata->soc = soc;

	return soc;
}
#endif
/*
 * Name : d2153_get_weight_from_lookup
 */
static u16 d2153_get_weight_from_lookup(u16 tempk,
		u16 average_adc, u8 chrgr_connected, int diff)
{
	u8 i = 0;
	u16 *plut = NULL;
	int weight = 0;
#ifdef CONFIG_D2153_MULTI_WEIGHT
	int diff_offset, weight_1c, weight_2c = 0;
#endif

	// Sanity check.
	if (tempk < BAT_LOW_LOW_TEMPERATURE)
		tempk = BAT_LOW_LOW_TEMPERATURE;
	else if (tempk > BAT_HIGH_TEMPERATURE)
		tempk = BAT_HIGH_TEMPERATURE;

	// Get the SOC look-up table
	if(tempk >= BAT_HIGH_TEMPERATURE) {
		plut = &adc2soc_lut.adc_ht[0];
	} else if(tempk < BAT_HIGH_TEMPERATURE && tempk >= BAT_ROOM_TEMPERATURE) {
		plut = &adc2soc_lut.adc_rt[0];
	} else if(tempk < BAT_ROOM_TEMPERATURE && tempk >= BAT_ROOM_LOW_TEMPERATURE) {
		plut = &adc2soc_lut.adc_rlt[0];
	} else if (tempk < BAT_ROOM_LOW_TEMPERATURE && tempk >= BAT_LOW_TEMPERATURE) {
		plut = &adc2soc_lut.adc_lt[0];
	} else if(tempk < BAT_LOW_TEMPERATURE && tempk >= BAT_LOW_MID_TEMPERATURE) {
		plut = &adc2soc_lut.adc_lmt[0];
	} else
		plut = &adc2soc_lut.adc_llt[0];

	for(i = adc2soc_lut_length - 1; i; i--) {
		if(plut[i] <= average_adc)
			break;
	}

#ifdef CONFIG_D2153_MULTI_WEIGHT
	// 0.1C, 0.2C
	if (i <= (MULTI_WEIGHT_SIZE-1)
		&& (!chrgr_connected)) {
		if(dischg_diff_tbl.c1_diff[i] > diff)
			diff_offset = dischg_diff_tbl.c1_diff[i];
		else if(dischg_diff_tbl.c2_diff[i] < diff)
			diff_offset = dischg_diff_tbl.c2_diff[i];
		else
			diff_offset = diff;
		pr_batt(VERBOSE,
			"%s. diff = %d, diff_offset = %d\n",
			__func__, diff, diff_offset);
	} else {
		diff_offset = 0;
		pr_batt(VERBOSE, "%s. diff = %d\n", __func__, diff);
	}
#endif /* CONFIG_D2153_MULTI_WEIGHT */


	if ((tempk <= BAT_HIGH_TEMPERATURE) && (tempk > BAT_ROOM_TEMPERATURE)) {
		if (chrgr_connected) {
			if(average_adc < plut[0]) {
				// under 1% -> fast charging
				weight = adc_weight_section_charge[0];
			} else
				weight = adc_weight_section_charge[i];
		} else {
#ifdef CONFIG_D2153_MULTI_WEIGHT
			if(i <= (MULTI_WEIGHT_SIZE-1)) {
				weight = adc_weight_section_discharge_1c[i] -
							((diff_offset - dischg_diff_tbl.c1_diff[i])
							 * (adc_weight_section_discharge_1c[i] -
							 adc_weight_section_discharge_2c[i]) /
							 (dischg_diff_tbl.c2_diff[i] -
							 dischg_diff_tbl.c1_diff[i]));
			} else
#endif /* CONFIG_D2153_MULTI_WEIGHT */
			{
				weight = adc_weight_section_discharge[i];
			}
		}
	} else if((tempk <= BAT_ROOM_TEMPERATURE) && (tempk > BAT_ROOM_LOW_TEMPERATURE)) {
		if (chrgr_connected) {
			if(average_adc < plut[0]) i = 0;

			weight=adc_weight_section_charge_rlt[i];
			weight = weight + ((tempk-BAT_ROOM_LOW_TEMPERATURE)
				*(adc_weight_section_charge[i]-adc_weight_section_charge_rlt[i]))
							/(BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);
		} else {
#ifdef CONFIG_D2153_MULTI_WEIGHT
			if(i <= (MULTI_WEIGHT_SIZE-1)) {
				weight_1c = adc_weight_section_discharge_rlt_1c[i];
				weight_1c = weight_1c + ((tempk - BAT_ROOM_LOW_TEMPERATURE)
							* (adc_weight_section_discharge_1c[i]
							- adc_weight_section_discharge_rlt_1c[i]))
							/ (BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);

				weight_2c = adc_weight_section_discharge_rlt_2c[i];
				weight_2c = weight_2c + ((tempk - BAT_ROOM_LOW_TEMPERATURE)
							* (adc_weight_section_discharge_2c[i]
							- adc_weight_section_discharge_rlt_2c[i]))
							/ (BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);

				weight = weight_1c - ((diff_offset - dischg_diff_tbl.c1_diff[i])
										* (weight_1c - weight_2c)
										/ (dischg_diff_tbl.c2_diff[i] -
											dischg_diff_tbl.c1_diff[i]));

				pr_batt(DATA,
					"%s(line.%d). weight = %d, weight_1c = %d, weight_2c = %d\n",
					__func__, __LINE__,
					weight, weight_1c, weight_2c);

			} else
#endif /* CONFIG_D2153_MULTI_WEIGHT */
			{
				weight=adc_weight_section_discharge_rlt[i];
				weight = weight + ((tempk-BAT_ROOM_LOW_TEMPERATURE)
								*(adc_weight_section_discharge[i]
									- adc_weight_section_discharge_rlt[i]))
								/(BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);
			}
		}
	} else if((tempk <= BAT_ROOM_LOW_TEMPERATURE) && (tempk > BAT_LOW_TEMPERATURE)) {
		if (chrgr_connected) {
			if(average_adc < plut[0]) i = 0;

			weight = adc_weight_section_charge_lt[i];
			weight = weight + ((tempk-BAT_LOW_TEMPERATURE)
				*(adc_weight_section_charge_rlt[i]-adc_weight_section_charge_lt[i]))
								/(BAT_ROOM_LOW_TEMPERATURE-BAT_LOW_TEMPERATURE);
		} else {
#ifdef CONFIG_D2153_MULTI_WEIGHT
			if(i <= (MULTI_WEIGHT_SIZE-1)) {
				weight_1c = adc_weight_section_discharge_rlt_1c[i];
				weight_1c = weight_1c + ((tempk - BAT_ROOM_LOW_TEMPERATURE)
							* (adc_weight_section_discharge_1c[i]
							- adc_weight_section_discharge_rlt_1c[i]))
							/ (BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);

				weight_2c = adc_weight_section_discharge_rlt_2c[i];
				weight_2c = weight_2c + ((tempk - BAT_ROOM_LOW_TEMPERATURE)
							* (adc_weight_section_discharge_2c[i]
							- adc_weight_section_discharge_rlt_2c[i]))
							/ (BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);

				weight = weight_1c - ((diff_offset - dischg_diff_tbl.c1_diff[i])
										* (weight_1c - weight_2c)
										/ (dischg_diff_tbl.c2_diff[i] -
											dischg_diff_tbl.c1_diff[i]));

				pr_batt(DATA,
					"%s(line.%d). weight = %d, weight_1c = %d, weight_2c = %d\n",
					__func__, __LINE__,
					weight, weight_1c, weight_2c);

			} else
#endif /* CONFIG_D2153_MULTI_WEIGHT */
			{
				weight = adc_weight_section_discharge_lt[i];
				weight = weight + ((tempk-BAT_LOW_TEMPERATURE)
								*(adc_weight_section_discharge_rlt[i]
									- adc_weight_section_discharge_lt[i]))
								/(BAT_ROOM_LOW_TEMPERATURE-BAT_LOW_TEMPERATURE);
			}
		}
	} else if((tempk <= BAT_LOW_TEMPERATURE) && (tempk > BAT_LOW_MID_TEMPERATURE)) {
		if (chrgr_connected) {
			if(average_adc < plut[0]) i = 0;

			weight = adc_weight_section_charge_lmt[i];
			weight = weight + ((tempk-BAT_LOW_MID_TEMPERATURE)
				*(adc_weight_section_charge_lt[i]-adc_weight_section_charge_lmt[i]))
								/(BAT_LOW_TEMPERATURE-BAT_LOW_MID_TEMPERATURE);
		} else {
#ifdef CONFIG_D2153_MULTI_WEIGHT
			if(i <= (MULTI_WEIGHT_SIZE-1)) {
				weight_1c = adc_weight_section_discharge_rlt_1c[i];
				weight_1c = weight_1c + ((tempk - BAT_ROOM_LOW_TEMPERATURE)
							* (adc_weight_section_discharge_1c[i]
							- adc_weight_section_discharge_rlt_1c[i]))
							/ (BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);

				weight_2c = adc_weight_section_discharge_rlt_2c[i];
				weight_2c = weight_2c + ((tempk - BAT_ROOM_LOW_TEMPERATURE)
							* (adc_weight_section_discharge_2c[i]
							- adc_weight_section_discharge_rlt_2c[i]))
							/ (BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);

				weight = weight_1c - ((diff_offset - dischg_diff_tbl.c1_diff[i])
										* (weight_1c - weight_2c)
										/ (dischg_diff_tbl.c2_diff[i] -
											dischg_diff_tbl.c1_diff[i]));

				pr_batt(DATA,
					"%s(line.%d). weight = %d, weight_1c = %d, weight_2c = %d\n",
					__func__, __LINE__,
					weight, weight_1c, weight_2c);
			} else
#endif /* CONFIG_D2153_MULTI_WEIGHT */
			{
				weight = adc_weight_section_discharge_lmt[i];
				weight = weight + ((tempk-BAT_LOW_MID_TEMPERATURE)
								* (adc_weight_section_discharge_lt[i]
									- adc_weight_section_discharge_lmt[i]))
								/ (BAT_LOW_TEMPERATURE-BAT_LOW_MID_TEMPERATURE);
			}
		}
	} else {
		if (chrgr_connected) {
			if(average_adc < plut[0]) i = 0;

			weight = adc_weight_section_charge_llt[i];
			weight = weight + ((tempk-BAT_LOW_LOW_TEMPERATURE)
				*(adc_weight_section_charge_lmt[i]-adc_weight_section_charge_llt[i]))
								/(BAT_LOW_MID_TEMPERATURE-BAT_LOW_LOW_TEMPERATURE);
		} else {
#ifdef CONFIG_D2153_MULTI_WEIGHT
			if(i <= (MULTI_WEIGHT_SIZE-1)) {
				weight_1c = adc_weight_section_discharge_rlt_1c[i];
				weight_1c = weight_1c + ((tempk - BAT_ROOM_LOW_TEMPERATURE)
							* (adc_weight_section_discharge_1c[i]
							- adc_weight_section_discharge_rlt_1c[i]))
							/ (BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);

				weight_2c = adc_weight_section_discharge_rlt_2c[i];
				weight_2c = weight_2c + ((tempk - BAT_ROOM_LOW_TEMPERATURE)
							* (adc_weight_section_discharge_2c[i]
							- adc_weight_section_discharge_rlt_2c[i]))
							/ (BAT_ROOM_TEMPERATURE-BAT_ROOM_LOW_TEMPERATURE);

				weight = weight_1c - ((diff_offset - dischg_diff_tbl.c1_diff[i])
										* (weight_1c - weight_2c)
										/ (dischg_diff_tbl.c2_diff[i] -
											dischg_diff_tbl.c1_diff[i]));

				pr_batt(DATA,
					"%s(line.%d). weight = %d, weight_1c = %d, weight_2c = %d\n",
					__func__, __LINE__,
					weight, weight_1c, weight_2c);
			} else
#endif /* CONFIG_D2153_MULTI_WEIGHT */
			{
				weight = adc_weight_section_discharge_llt[i];
				weight = weight + ((tempk-BAT_LOW_LOW_TEMPERATURE)
								* (adc_weight_section_discharge_lmt[i]
									- adc_weight_section_discharge_llt[i]))
								/ (BAT_LOW_MID_TEMPERATURE
									-BAT_LOW_LOW_TEMPERATURE);
			}
		}
	}

	return weight;

}

#define LCD_DIM_ADC			2168

extern int get_cable_type(void);

/*
 * Name : d2153_reset_sw_fuelgauge
 */
static int d2153_reset_sw_fuelgauge(struct d2153_battery *bdata)
{
	u8 i, j = 0;
	int read_adc = 0;
	u32 average_adc, sum_read_adc = 0;

	if (bdata == NULL) {
		pr_batt(ERROR, "%s. Invalid argument\n", __func__);
		return -EINVAL;
	}

	pr_batt(FLOW, "++++++ Reset Software Fuelgauge +++++++++++\n");
	bdata->volt_adc_init_done = FALSE;

	/* Initialize ADC buffer */
	memset(bdata->voltage_adc, 0x0, ARRAY_SIZE(bdata->voltage_adc));
	bdata->sum_voltage_adc = 0;
	bdata->soc = 0;
	bdata->prev_soc = 0;

	/* Read VBAT_S ADC */
	for(i = 8, j = 0; i; i--) {
		read_adc = bdata->d2153_read_adc(bdata, D2153_ADC_VOLTAGE,
							D2153_ADC_ISRC_10UA);
		if (bdata->adc_res[D2153_ADC_VOLTAGE].is_adc_eoc) {
			read_adc = bdata->adc_res[D2153_ADC_VOLTAGE].read_adc;
			//pr_info("%s. Read ADC %d : %d\n", __func__, i, read_adc);
			if(read_adc > 0) {
				sum_read_adc += read_adc;
				j++;
			}
		}
		msleep(10);
	}
	average_adc = read_adc = sum_read_adc / j;
	pr_batt(DATA, "%s. average = %d, j = %d\n", __func__, average_adc, j);

	/* To be compensated a read ADC */
	average_adc = fg_reset_drop_offset[0];
	if(average_adc > MAX_FULL_CHARGED_ADC) {
		average_adc = MAX_FULL_CHARGED_ADC;
	}
	pr_batt(DATA, "%s. average ADC = %d. voltage = %d mV\n",
				__func__, average_adc,
				adc_to_vbat(average_adc,
				bdata->charger_connected));

	bdata->current_volt_adc = average_adc;

	bdata->origin_volt_adc = read_adc;
	bdata->average_volt_adc = average_adc;
	bdata->current_voltage = adc_to_vbat(bdata->current_volt_adc,
		bdata->charger_connected);
	bdata->average_voltage = adc_to_vbat(bdata->average_volt_adc,
		bdata->charger_connected);
	bdata->volt_adc_init_done = TRUE;

	pr_batt(FLOW, "%s. Average. ADC = %d, Voltage =  %d\n",
			__func__,
			bdata->average_volt_adc,
			bdata->average_voltage);

	return 0;
}


#ifdef CONFIG_SEC_CHARGING_FEATURE
extern struct spa_power_data spa_power_pdata;
extern int spa_event_handler(int evt, void *data);
#endif

int d2153_volt_adc_with_temp(struct d2153_battery *bdata, u32 vbat_adc)
{
	//int X = C2K(bdata->avg_temp);
	int X = C2K(bdata->current_temperature);
	int convert_vbat_adc, X1, X0;
	int Y1, Y0 = FIRST_VOLTAGE_DROP_ADC;

	if (X <= BAT_LOW_LOW_TEMPERATURE) {
		convert_vbat_adc = vbat_adc + (Y0 + FIRST_VOLTAGE_DROP_LL_ADC);
	} else if (X >= BAT_ROOM_TEMPERATURE) {
		convert_vbat_adc = vbat_adc + Y0;
	} else {
		if (X <= BAT_LOW_MID_TEMPERATURE) {
			Y1 = Y0 + FIRST_VOLTAGE_DROP_ADC;
			Y0 = Y0 + FIRST_VOLTAGE_DROP_LL_ADC;
			X0 = BAT_LOW_LOW_TEMPERATURE;
			X1 = BAT_LOW_MID_TEMPERATURE;
		} else if (X <= BAT_LOW_TEMPERATURE) {
			Y1 = Y0 + FIRST_VOLTAGE_DROP_L_ADC;
			Y0 = Y0 + FIRST_VOLTAGE_DROP_ADC;
			X0 = BAT_LOW_MID_TEMPERATURE;
			X1 = BAT_LOW_TEMPERATURE;
		} else {
			Y1 = Y0 + FIRST_VOLTAGE_DROP_RL_ADC;
			Y0 = Y0 + FIRST_VOLTAGE_DROP_L_ADC;
			X0 = BAT_LOW_TEMPERATURE;
			X1 = BAT_ROOM_LOW_TEMPERATURE;
		}
		convert_vbat_adc = vbat_adc + Y0
			+ ((X - X0) * (Y1 - Y0)) / (X1 - X0);
	}
	return convert_vbat_adc;

}

/*
 * Name : d2153_read_voltage
 */
static int d2153_read_voltage(struct d2153_battery *bdata)
{
	int new_vol_adc = 0, base_weight, new_vol_orign;
	int offset_with_new = 0;
	int ret = 0;
	int num_multi=0;
	int orign_offset;

	//custom_t:CJ
	#ifdef custom_t_BATTERY_CHARGER_ON
	int is_charging_disabled_for_init_adc = 0;
	#endif

    pr_batt(FLOW,
        "%s. chrgr_connected = %d\n",
        __func__, bdata->charger_connected);

	// Read voltage ADC
	ret = bdata->d2153_read_adc(bdata, D2153_ADC_VOLTAGE,
							D2153_ADC_ISRC_10UA);
	if(ret < 0)
		return ret;

	if (bdata->adc_res[D2153_ADC_VOLTAGE].is_adc_eoc) {

		new_vol_orign =
			new_vol_adc =
				bdata->adc_res[D2153_ADC_VOLTAGE].read_adc;


		if (bdata->volt_adc_init_done) {

			if (bdata->charger_connected) {

				orign_offset =
					new_vol_adc - bdata->average_volt_adc;
				base_weight = d2153_get_weight_from_lookup(
						C2K(bdata->current_temperature), // BCM_ID_814048
						bdata->average_volt_adc,
						bdata->charger_connected,
						orign_offset);
				offset_with_new = orign_offset * base_weight;

				bdata->sum_total_adc += offset_with_new;
				num_multi = bdata->sum_total_adc / NORM_CHG_NUM;
				if(num_multi) {
					bdata->average_volt_adc += num_multi;
					bdata->sum_total_adc =
						bdata->sum_total_adc
						% NORM_CHG_NUM;
				} else {
					new_vol_adc = bdata->average_volt_adc;
				}

				bdata->current_volt_adc = new_vol_adc;
			} else {

				orign_offset =
					bdata->average_volt_adc - new_vol_adc;
				base_weight = d2153_get_weight_from_lookup(
						C2K(bdata->current_temperature), // BCM_ID_814048
						bdata->average_volt_adc,
						bdata->charger_connected,
						orign_offset);

				offset_with_new = orign_offset * base_weight;

				if (bdata->reset_total_adc) {
					bdata->sum_total_adc =
						bdata->sum_total_adc / 10;
					bdata->reset_total_adc = 0;
					pr_batt(FLOW,
						"%s. reset_total_adc. true to false\n",
						__func__);
				}
				bdata->sum_total_adc += offset_with_new;

				num_multi = bdata->sum_total_adc / NORM_NUM;
				if(num_multi) {
					bdata->average_volt_adc -= num_multi;
					bdata->sum_total_adc =
						bdata->sum_total_adc % NORM_NUM;
				} else {
					new_vol_adc = bdata->average_volt_adc;
				}

			}
		}else {
			// Before initialization
			u8 i = 0;
			u8 res_msb, res_lsb, res_msb_adc, res_lsb_adc = 0;
			u32 capacity = 0, vbat_adc = 0;
			int convert_vbat_adc;


			// If there is SOC data in the register
			// the SOC(capacity of battery) will be used as initial SOC
			ret = d2153_reg_read(bdata->pd2153,
				D2153_GP_ID_2_REG, &res_lsb);
			ret = d2153_reg_read(bdata->pd2153,
				D2153_GP_ID_3_REG, &res_msb);
			capacity = (((res_msb & 0x0F) << 8) | (res_lsb & 0xFF));

			ret = d2153_reg_read(bdata->pd2153,
				D2153_GP_ID_4_REG, &res_lsb_adc);
			ret = d2153_reg_read(bdata->pd2153,
				D2153_GP_ID_5_REG, &res_msb_adc);
			vbat_adc = (((res_msb_adc & 0x0F) << 8) | (res_lsb_adc & 0xFF));

			pr_batt(VERBOSE,
				"%s. capacity = %d, vbat_adc = %d\n",
				__func__, capacity, vbat_adc);

			if(capacity) {
				convert_vbat_adc = vbat_adc;
				pr_batt(FLOW,
					"!#!#!# Boot by NORMAL Power off !#!#!#\n");
			} else {
				u8 ta_status, charging_status = 0;

				pr_batt(FLOW,
					"!#!#!# Boot by Battery insert !#!#!#\n");

				bdata->pd2153->average_vbat_init_adc =
					(bdata->pd2153->vbat_init_adc[0] +
					 bdata->pd2153->vbat_init_adc[1] +
					 bdata->pd2153->vbat_init_adc[2]) / 3;

				vbat_adc = bdata->pd2153->average_vbat_init_adc;

				ret = d2153_reg_read(bdata->pd2153, D2153_STATUS_C_REG, &ta_status);
				charging_status = (int)(ta_status & D2153_GPI_3_TA_MASK);

				pr_batt(FLOW, "%s, average_init_adc = %d, charging_status = %d\n",
							__func__, vbat_adc, charging_status);

				if (charging_status) {
                    #if defined (custom_t_BATTERY_CHARGER_ON)
                    if(charging_status)
                    {
                        int is_charging = bcm_agent_get(BCM_AGENT_GET_CHARGE_STATE,NULL);
                        if (is_charging)
                        {
							//custom_t:CJ
							convert_vbat_adc = d2153_volt_adc_with_temp(bdata,bdata->pd2153->average_vbat_init_adc);
							bdata->current_voltage = adc_to_vbat(convert_vbat_adc,is_charging);

							pr_batt(FLOW, "%s, check if need disable charger, convert_vbat_adc = %d, bdata->current_voltage = %d\n",
										__func__, convert_vbat_adc, bdata->current_voltage);
							if(bdata->current_voltage > 3450)
							{
	                            bcm_chrgr_usb_en(0);
								is_charging_disabled_for_init_adc = 1;
    	                        msleep(5);
							}
                        }

                        // Read voltage ADC
                        ret = bdata->d2153_read_adc(bdata, D2153_ADC_VOLTAGE,
                                                D2153_ADC_ISRC_10UA);
                        if(ret >= 0)
                            bdata->pd2153->vbat_init_adc[0] = bdata->adc_res[D2153_ADC_VOLTAGE].read_adc;
    
                        msleep(1);
                        ret = bdata->d2153_read_adc(bdata, D2153_ADC_VOLTAGE,
                                                D2153_ADC_ISRC_10UA);
                        if(ret >= 0)
                            bdata->pd2153->vbat_init_adc[1] = bdata->adc_res[D2153_ADC_VOLTAGE].read_adc;
                            
                        msleep(1);
                        ret = bdata->d2153_read_adc(bdata, D2153_ADC_VOLTAGE,
                                                D2153_ADC_ISRC_10UA);
                        if(ret >= 0)
                            bdata->pd2153->vbat_init_adc[2] = bdata->adc_res[D2153_ADC_VOLTAGE].read_adc;
                        
                        pr_batt(FLOW, "%s, vbat_init_adc[0] = %d, vbat_init_adc[1] = %d vbat_init_adc[2] = %d\n",
                                    __func__, bdata->pd2153->vbat_init_adc[0],bdata->pd2153->vbat_init_adc[1],bdata->pd2153->vbat_init_adc[2]);
                        
                        //msleep(1);
                        if (is_charging)
                            bcm_chrgr_usb_en(1);
                        //msleep(1);
    
                        bdata->pd2153->average_vbat_init_adc =
                            (bdata->pd2153->vbat_init_adc[0] +
                             bdata->pd2153->vbat_init_adc[1] +
                             bdata->pd2153->vbat_init_adc[2]) / 3;
    
                        vbat_adc = bdata->pd2153->average_vbat_init_adc;
    
                        pr_batt(FLOW, "%s, fan5405_is_charge=%d\n", __func__, is_charging);
                        
                        convert_vbat_adc =
    						d2153_volt_adc_with_temp(bdata,
    							vbat_adc);
                    }
                    #else
					if(vbat_adc < CHARGE_ADC_KRNL_F) {
			            int X1, X0;
            			int Y1, Y0 = FIRST_VOLTAGE_DROP_ADC;
                        
						// In this case, vbat_adc is bigger than OCV
						// So, subtract a interpolated value
						// from initial average value(vbat_adc)
						u16 temp_adc = 0;

						if(vbat_adc < CHARGE_ADC_KRNL_H)
							vbat_adc = CHARGE_ADC_KRNL_H;

						X0 = CHARGE_ADC_KRNL_H;    X1 = CHARGE_ADC_KRNL_L;
						Y0 = CHARGE_OFFSET_KRNL_H; Y1 = CHARGE_OFFSET_KRNL_L;
						temp_adc = do_interpolation(X0, X1, Y0, Y1, vbat_adc);
						convert_vbat_adc = vbat_adc - temp_adc;
					} else {
						convert_vbat_adc = new_vol_orign + CHARGE_OFFSET_KRNL2;
					}
                    #endif  /* custom_t_BATTERY_CHARGER_ON */
				} else {
					vbat_adc = new_vol_orign;
					pr_batt(VERBOSE,
						"[L%d] %s discharging new_vol_adc = %d\n",
						__LINE__, __func__, vbat_adc);

					convert_vbat_adc =
						d2153_volt_adc_with_temp(bdata,
							vbat_adc);
				}
			}
			new_vol_adc = convert_vbat_adc;

			if(new_vol_adc > MAX_FULL_CHARGED_ADC) {
				new_vol_adc = MAX_FULL_CHARGED_ADC;
				pr_batt(FLOW,
					"%s. Set new_vol_adc to max. ADC value\n",
					__func__);
			}

			for(i = AVG_SIZE; i ; i--) {
				bdata->voltage_adc[i-1] = new_vol_adc;
				bdata->sum_voltage_adc += new_vol_adc;
			}

			bdata->current_volt_adc = new_vol_adc;
			bdata->volt_adc_init_done = TRUE;
			bdata->average_volt_adc = new_vol_adc;
		}

		bdata->origin_volt_adc = new_vol_orign;

//custom_t:CJ
#ifdef custom_t_BATTERY_CHARGER_ON
		if(is_charging_disabled_for_init_adc)
		{
			pr_batt(FLOW,"is_charging_disabled_for_init_adc\n");
			bdata->current_voltage =
				adc_to_vbat(bdata->current_volt_adc,
					0);
			bdata->average_voltage =
				adc_to_vbat(bdata->average_volt_adc,
				    0);
		}
		else
#endif			
		{
			bdata->current_voltage =
				adc_to_vbat(bdata->current_volt_adc,
					bdata->charger_connected);
			bdata->average_voltage =
				adc_to_vbat(bdata->average_volt_adc,
				    bdata->charger_connected);
		}

	}
	else {
		pr_batt(ERROR,
			"%s. Voltage ADC read failure\n", __func__);
		ret = -EIO;
	}

	return ret;
}


/*
 * Name : d2153_read_temperature
 */
static int d2153_read_temperature(struct d2153_battery *bdata)
{
	u8 ch = 0;
	u16 new_temp_adc = 0;
	int ret = -EIO;
	u16 temp_adc_u10 = 0, temp_adc_u50 = 0;
	int retries = ADC_ERR_TRIES;


	/* Read temperature ADC
	 * Channel : D2153_ADC_TEMPERATURE_1 -> TEMP_BOARD
	 * Channel : D2153_ADC_TEMPERATURE_2 -> TEMP_RF
	 */

	/* To read a temperature ADC of BOARD */
	ch = D2153_ADC_TEMPERATURE_1;

	/* To balance out the ADC Error caused by battery contact resistors
	 * and ground current, use the following sequence:
	 * Use 10uA current source to read ADC.
	 * Get ADC1 (ADC1=10uA*Rntc+(Delta)V)
	 * Use 50uA current source to read ADC.
	 * Get ADC2 (ADC2=50uA*Rntc+(Delta)V)
	 * Then ADC3=ADC2-ADC1=40uA*Rntc + (Delta)V - (Delta)V
	 * new_temp_adc = ADC3/4 and use look up 10uA temperature table.
	 */
	/* By default 10uA current source is enabled */
	while (retries) {
		ret = bdata->d2153_read_adc(bdata, ch, D2153_ADC_ISRC_10UA);

		if (bdata->adc_res[ch].is_adc_eoc) {
			temp_adc_u10 = bdata->adc_res[ch].read_adc;

			if (temp_adc_u10 >= D2153_BATTERY_ABSENT) {
				bdata->battery_present = BATTERY_NOT_DETECTED;
				break;
			} else
				bdata->battery_present = BATTERY_DETECTED;

			/* To Enable 50uA current source */
			ret = bdata->d2153_read_adc(bdata, ch,
							D2153_ADC_ISRC_50UA);

			if (bdata->adc_res[ch].is_adc_eoc) {
				temp_adc_u50 = bdata->adc_res[ch].read_adc;

				if (temp_adc_u50 < ADC_TEMP_OVF)
					new_temp_adc =
						(temp_adc_u50 - temp_adc_u10)/4;
				else
					new_temp_adc = temp_adc_u10;

				pr_batt(VERBOSE,
					"%s:adc@50uA: %d @10uA: %d,@Diff: %d\n",
						__func__, temp_adc_u50,
						temp_adc_u10, new_temp_adc);

				if ((new_temp_adc < temp_adc_min) ||
					(new_temp_adc > temp_adc_max)) {
					pr_batt(FLOW,
					"%s:ADC@50uA:%d @10uA:%d,@Diff:%d\n",
						__func__, temp_adc_u50,
						temp_adc_u10, new_temp_adc);
					if (bdata->adc_max_lmt) {
						break;
					} else if (--retries) {
						pr_batt(ERROR,
						"%s:Invalid ADC read\n",
								__func__);
						msleep(ADC_RETRY_DELAY);
					} else {
						bdata->adc_max_lmt = TRUE;
					}
				} else {
					bdata->adc_max_lmt = FALSE;
					break;
				}
			} else {
				pr_batt(ERROR,
					"%s. Temp ADC read failed with 50uA\n",
								__func__);
				goto err;
			}
		} else {
			pr_batt(ERROR, "%s. Temp ADC read failed with 10uA\n",
								__func__);
			goto err;
		}
	}

	if (bdata->battery_present == BATTERY_NOT_DETECTED) {
		pr_batt(FLOW,
		   "DUMMY or No Battery so Report Fake Temp(25C), Actual ADC:%d\n",
			temp_adc_u10);
		bdata->average_temp_adc =
			bdata->pd2153->pdata->pbat_platform->fake_room_temp_adc;
	} else	if (ntc_disable) {
		pr_batt(FLOW,
			"NTC Disabled so Report Fake Temp(25C), Actual ADC:%d\n",
			bdata->average_temp_adc);
		bdata->average_temp_adc =
			bdata->pd2153->pdata->pbat_platform->fake_room_temp_adc;
	} else
		bdata->average_temp_adc = new_temp_adc;

	return bdata->average_temp_adc;
err:
	return ret;
}

/*
 * Name : d2153_get_rf_temperature
 */
int d2153_get_rf_temperature(void)
{
	u8 i, j, channel;
	int sum_temp_adc, ret = 0;
	struct d2153_battery *bdata = gbat;

	if (bdata == NULL) {
		pr_batt(ERROR, "%s. battery_data is NULL\n", __func__);
		return -EINVAL;
	}

	/* To read a temperature2 ADC */
	sum_temp_adc = 0;
	channel = D2153_ADC_TEMPERATURE_2;
	for(i = 10, j = 0; i; i--) {
		ret = bdata->d2153_read_adc(bdata, channel,
							D2153_ADC_ISRC_10UA);
		if(ret == 0) {
			sum_temp_adc += bdata->adc_res[channel].read_adc;
			if(++j == 3)
				break;
		} else
			msleep(20);
	}
	if (j) {
		bdata->current_rf_temp_adc = (sum_temp_adc / j);
		bdata->current_rf_temperature =
		degree_k2c(adc_to_degree_k(bdata->current_rf_temp_adc,
			channel));
#ifdef CONFIG_D2153_BATTERY_DEBUG
		pr_batt(VERBOSE,
			"%s. RF_TEMP_ADC = %d, RF_TEMPERATURE = %3d.%d\n",
			__func__, bdata->current_rf_temp_adc,
			(bdata->current_rf_temperature/10),
			(bdata->current_rf_temperature%10));
#endif
		return bdata->current_rf_temperature;
	} else {
		pr_batt(ERROR,
			"%s:ERROR in reading RF temperature.\n", __func__);
		return -EIO;
	}
 }
EXPORT_SYMBOL(d2153_get_rf_temperature);

/*
 * Name : d2153_get_ap_temperature
 */
int d2153_get_ap_temperature(int opt)
{
	u8 i, j, channel;
	int sum_temp_adc, ret = 0;
	struct d2153_battery *bdata = NULL;

	if(gbat == NULL) {
		pr_batt(ERROR, "%s. battery_data is NULL\n", __func__);
		return -EINVAL;
	}
	bdata = gbat;

	/* To read a AP_TEMP ADC */
	sum_temp_adc = 0;
	channel = D2153_ADC_AIN;
	for(i = 10, j = 0; i; i--) {
		ret = bdata->d2153_read_adc(bdata, channel,
							D2153_ADC_ISRC_10UA);
		if(ret == 0) {
			sum_temp_adc += bdata->adc_res[channel].read_adc;
			if(++j == 3)
				break;
		} else
			msleep(20);
	}
	if (j) {
		bdata->current_ap_temp_adc = (sum_temp_adc / j);
		bdata->current_ap_temperature =
		degree_k2c(adc_to_degree_k(bdata->current_ap_temp_adc,
			channel));

		pr_batt(FLOW, "%s. AP_TEMP_ADC = %d, AP_TEMPERATURE = %3d.%d\n",
					__func__,
					bdata->current_ap_temp_adc,
					(bdata->current_ap_temperature/10),
					(bdata->current_ap_temperature%10));
		if (opt == D2153_OPT_ADC)
			return bdata->current_ap_temp_adc;
		else
			return bdata->current_ap_temperature;
	} else {
		pr_batt(ERROR,
			"%s:ERROR in reading AP temperature.\n", __func__);
		return -EIO;
	}
}
EXPORT_SYMBOL(d2153_get_ap_temperature);

/*
 * Name : d2153_battery_read_status
 */
#ifdef CONFIG_SEC_CHARGING_FEATURE
int d2153_battery_read_status(int type)
{
	int val = 0;
	struct d2153_battery *bdata = NULL;

	if (gbat == NULL) {
		pr_batt(ERROR, "%s. driver data is NULL\n", __func__);
		return -EINVAL;
	}

	bdata = gbat;

	switch(type){
		case D2153_BATTERY_SOC:
			val = bdata->capacity;
			break;

		case D2153_BATTERY_CUR_VOLTAGE:
			val = bdata->current_voltage;
			break;

		case D2153_BATTERY_AVG_VOLTAGE:
			val = bdata->average_voltage;
			break;

		case D2153_BATTERY_VOLTAGE_NOW :
		{
			u8 ch = D2153_ADC_VOLTAGE;

			val = bdata->d2153_read_adc(bdata, ch,
							D2153_ADC_ISRC_10UA);
			if(val < 0)
				return val;
			if (bdata->adc_res[ch].is_adc_eoc) {
				val =
					adc_to_vbat(bdata->adc_res[ch].read_adc,
						0);
#ifdef CONFIG_D2153_BATTERY_DEBUG
				pr_batt(FLOW,
				"%s: read adc to bat value = %d\n",
				__func__, val);
#endif
			} else {
				val = -EINVAL;
			}
			break;
		}

		case D2153_BATTERY_TEMP_HPA:
			val = d2153_get_rf_temperature();
			break;

		case D2153_BATTERY_TEMP_ADC:
			val = bdata->average_temp_adc;
			break;
		case D2153_BATTERY_STATUS:
			val = bdata->battery_present;
			break;
	}

	return val;
}
#else
int d2153_battery_read_status(int type)
{
	u8 ch;
	int val = 0;
	struct d2153_battery *bdata = NULL;
	int ext_fg;
	ext_fg = 0;
	ext_fg = bcm_agent_get(BCM_AGENT_GET_FG_PRIVATE, NULL);

	if (gbat == NULL) {
		pr_batt(ERROR, "%s. driver data is NULL\n", __func__);
		return -EINVAL;
	}
	bdata = gbat;

	switch (type) {
	case D2153_BATTERY_SOC:
		val = bdata->capacity;
		break;

	case D2153_BATTERY_CUR_VOLTAGE:
		if (ext_fg)
			val = bcm_agent_get(BCM_AGENT_GET_VCELL, NULL);
		else
			val = bdata->current_voltage;
		break;

	case D2153_BATTERY_AVG_VOLTAGE:
		if (ext_fg)
			val = bcm_agent_get(BCM_AGENT_GET_VCELL, NULL);
		else
			val = bdata->average_voltage;
		break;

	case D2153_BATTERY_VOLTAGE_NOW:
		ch = D2153_ADC_VOLTAGE;

		if (ext_fg)
			val = bcm_agent_get(BCM_AGENT_GET_VCELL, NULL);
		else {
			val = bdata->d2153_read_adc(bdata, ch,
						D2153_ADC_ISRC_10UA);
			if (val < 0)
				return val;
			if (bdata->adc_res[ch].is_adc_eoc) {
				val =
				adc_to_vbat(bdata->adc_res[ch].read_adc, 0);
#ifdef CONFIG_D2153_BATTERY_DEBUG
					pr_batt(FLOW,
					"%s: read adc to bat value = %d\n",
					__func__, val);
#endif
			} else
				val = -EINVAL;
		}
		break;
	case D2153_BATTERY_TEMP_HPA:
		val = d2153_get_rf_temperature();
		break;
	case D2153_BATTERY_TEMP_ADC:
		val = bdata->average_temp_adc;
		break;
	case D2153_BATTERY_STATUS:
		val = bdata->battery_present;
		break;
	}

	return val;
}
#endif
EXPORT_SYMBOL(d2153_battery_read_status);


/*
 * Name : d2153_battery_set_status
 */
int d2153_battery_set_status(int type, int status)
{
	int val = 0;
	struct d2153_battery *bdata = NULL;

	if(gbat == NULL) {
		pr_batt(ERROR, "%s. driver data is NULL\n", __func__);
		return -EINVAL;
	}

	bdata = gbat;

	switch(type){
		case D2153_STATUS_CHARGING :
			/* Discharging = 0, Charging = 1 */
			val = bdata->charger_connected;
			break;
		case D2153_RESET_SW_FG :
			/* Reset SW fuel gauge */
			cancel_delayed_work_sync(&bdata->monitor_volt_work);
			val = d2153_reset_sw_fuelgauge(bdata);
			schedule_delayed_work(&gbat->monitor_volt_work, 0);
			break;
		case D2153_STATUS_EOC_CTRL:
			bdata->charger_ctrl_status = status;
			break;
		default :
			return -EINVAL;
	}

	return val;
}
EXPORT_SYMBOL(d2153_battery_set_status);

#ifdef CONFIG_SEC_CHARGING_FEATURE
/*
 * Name: d2153_check_bat_presence - Get battery Presence status
 */
static int d2153_check_bat_presence(void)
{
	int batt_stat;
	batt_stat = d2153_battery_read_status(D2153_BATTERY_STATUS);
	return batt_stat;
}

/*
 * Name: d2153_get_full_charge - Get the full charge value
 */
static int d2153_get_full_charge(void)
{
	int full_cap;
	full_cap = USED_BATTERY_CAPACITY;
	return full_cap;
}

/*
 * Name: d2153_ctrl_fg - Reset fuel gauge
 */
static int d2153_ctrl_fg(void *data)
{
	int ret;
	ret = d2153_battery_set_status(D2153_RESET_SW_FG, 0);
	return ret;
}
/*
 * Name: d2153_get_voltage - Get battery voltage
 */
static int d2153_get_voltage(unsigned char opt)
{
	int volt;
	volt = d2153_battery_read_status(D2153_BATTERY_AVG_VOLTAGE);
	return volt;
}
/*
 * Name: d2153_get_capacity - Get battery SOC
 */
static int d2153_get_capacity(void)
{
	unsigned int bat_per;
	bat_per = d2153_battery_read_status(D2153_BATTERY_SOC);
	return bat_per;
}

/*
 * Name: d2153_get_temp_adc - Read battery temperature adc value
 */
static int d2153_get_temp_adc(unsigned int opt)
{
	int temp;
	temp = d2153_battery_read_status(D2153_BATTERY_TEMP_ADC);
	return temp;
}
#endif

/*
 * Name: d2153_get_temp - Read battery temperature
 */
static int d2153_get_temp(struct d2153_battery *bdata)
{

	int temp_adc, i, temp = 0;
	struct d2153_ntc_temp_tb *batt_temp_tb =
		bdata->pd2153->pdata->pbat_platform->batt_ntc_temp_tb;
	unsigned int batt_temp_tb_len =
		bdata->pd2153->pdata->pbat_platform->batt_ntc_temp_tb_len;

	pr_batt(VERBOSE, "%s: Enter\n", __func__);

	temp_adc = d2153_battery_read_status(D2153_BATTERY_TEMP_ADC);
	pr_batt(VERBOSE, "%s: tem_adc: %d\n", __func__, temp_adc);
	for (i = 0 ; i < batt_temp_tb_len - 1 ; i++) {
		if (batt_temp_tb[i].adc >= temp_adc
			&& batt_temp_tb[i+1].adc <= temp_adc) {
			int temp_diff, adc_diff, inc_comp;
			temp_diff = (batt_temp_tb[i+1].temp
				- batt_temp_tb[i].temp);
			adc_diff =
				(batt_temp_tb[i+1].adc - batt_temp_tb[i].adc);
			inc_comp =	batt_temp_tb[i].temp
				- ((temp_diff * batt_temp_tb[i].adc)
				/ adc_diff);
			temp =
				((temp_adc * temp_diff) / adc_diff) + inc_comp;

			return temp;
		}
	}

	pr_batt(VERBOSE, "%s : Leave\n", __func__);

	if (batt_temp_tb[0].adc < temp_adc)
		return batt_temp_tb[0].temp;
	else if (batt_temp_tb[batt_temp_tb_len - 1].adc > temp_adc)
		return batt_temp_tb[batt_temp_tb_len - 1].temp;
	else
		return temp;
}

#ifdef CONFIG_SEC_CHARGING_FEATURE
static void d2153_spa_sw_maintainance_charging(struct d2153_battery *bdata)
{
#ifdef CONFIG_D2153_EOC_CTRL
	struct power_supply *ps;
	union power_supply_propval value;

	if (!bdata->volt_adc_init_done)
		return;

	if (bdata->capacity != FULL_CAPACITY/10)
		return;

	pr_batt(VERBOSE, "%s cap: %d,ctrl: %d, avg volt: %d, eoc: %d\n",
		__func__, bdata->capacity,
		bdata->charger_ctrl_status,
		bdata->average_voltage, bdata->eoc_status);

	pr_batt(FLOW, "$$$$$$In SW Maintainance Charging, state: %d $$$$$$\n",
		bdata->charger_ctrl_status);


	ps = power_supply_get_by_name("battery");
	if (ps == NULL) {
		pr_batt(ERROR,
				"%s. ps \"battery\" yet to register\n",
				__func__);
		return;
	}

	ps->get_property(ps, POWER_SUPPLY_PROP_STATUS, &value);
	pr_batt(VERBOSE, "%s. Battery PROP_STATUS = %d\n",
			__func__, value.intval);

	if (bdata->is_charging  == POWER_SUPPLY_STATUS_CHARGING) {
		if (value.intval == POWER_SUPPLY_STATUS_FULL) {
			if (((bdata->charger_ctrl_status
				== D2153_BAT_RECHG_FULL)
				|| (bdata->charger_ctrl_status
					== D2153_BAT_CHG_BACKCHG_FULL))
				&& (bdata->average_voltage
					>= D2153_BAT_CHG_BACK_FULL_LVL)) {

				spa_event_handler(SPA_EVT_EOC, 0);

				bdata->charger_ctrl_status
					= D2153_BAT_RECHG_FULL;
				pr_batt(FLOW,
					"%s. Recharging Done.(3) 2nd full > discharge > Recharge\n",
					__func__);
			} else if ((bdata->charger_ctrl_status
				== D2153_BAT_CHG_FRST_FULL)
				&& (bdata->average_voltage
				>= D2153_BAT_CHG_BACK_FULL_LVL)) {

				bdata->charger_ctrl_status =
					D2153_BAT_CHG_BACKCHG_FULL;

				spa_event_handler(SPA_EVT_EOC, 0);

				pr_batt(FLOW,
					"%s. Recharging Done.(4) 1st full > 2nd full\n",
					__func__);
			}
		} else {
			/* Will stop charging when a voltage approach to
			*  first full charge level
			*/
			if ((bdata->charger_ctrl_status
				< D2153_BAT_CHG_FRST_FULL)
				&& (bdata->average_voltage
					>= D2153_BAT_CHG_FRST_FULL_LVL)) {

				spa_event_handler(SPA_EVT_EOC, 0);

				bdata->charger_ctrl_status
					= D2153_BAT_CHG_FRST_FULL;

				pr_batt(FLOW,
					"%s. First charge done.(1)\n",
					__func__);
			} else if ((bdata->charger_ctrl_status
				< D2153_BAT_CHG_FRST_FULL)
				&& (bdata->average_voltage
					>= D2153_BAT_CHG_BACK_FULL_LVL)) {

				spa_event_handler(SPA_EVT_EOC, 0);
				spa_event_handler(SPA_EVT_EOC, 0);

				bdata->charger_ctrl_status =
					D2153_BAT_CHG_BACKCHG_FULL;
				pr_batt(FLOW,
					"%s. Fully charged.(2)(Back-charging done)\n",
					__func__);
			}
		}
	}
#endif
}
#else
static void d2153_sw_maintainance_charging(struct d2153_battery *bdata)
{
	int ext_fg;
	u32 bkchg_soc;
	u32 rechg_soc;
	u32 ftchg_soc;

	u32 recharge_volt =
			bdata->pd2153->pdata->pbat_platform->recharge_voltage;
	ext_fg = 0;
	ext_fg = bcm_agent_get(BCM_AGENT_GET_FG_PRIVATE, NULL);
#ifdef CONFIG_D2153_EOC_CTRL
	if (!bdata->volt_adc_init_done && (ext_fg == 0))
		return;

	if (bdata->capacity != FULL_CAPACITY/10)
		return;

	if (bdata->health != POWER_SUPPLY_HEALTH_GOOD)
		return;

	if (bdata->charging_timer_expired) {
		if (bdata->is_charging == POWER_SUPPLY_STATUS_CHARGING) {
			bcm_chrgr_usb_en(0);
			pr_batt(FLOW,
				"Charging Timer Expiry, In EOC, so Disabled charging\n");
		}
		return;
	}
	if (ext_fg != 0) {
		bkchg_soc = bcm_agent_get(BCM_AGENT_GET_FG_BKCHG_SOC, NULL);
		rechg_soc = bcm_agent_get(BCM_AGENT_GET_FG_RECHG_SOC, NULL);
		ftchg_soc = bcm_agent_get(BCM_AGENT_GET_FG_FTCHG_SOC, NULL);
	}
	pr_batt(VERBOSE, "%s cap: %d,ctrl: %d, avg volt: %d, eoc: %d\n",
		__func__, bdata->capacity,
		bdata->charger_ctrl_status,
		bdata->average_voltage, bdata->eoc_status);


	pr_batt(VERBOSE, "$$$In SW Maintainance Charging, state: %d $$$\n",
		bdata->charger_ctrl_status);
	if (ext_fg) {
		if (bdata->is_charging == POWER_SUPPLY_STATUS_CHARGING) {
			if (((bdata->charger_ctrl_status
				== D2153_BAT_RECHG_FULL) ||
					(bdata->charger_ctrl_status
				== D2153_BAT_CHG_BACKCHG_FULL))
				&& (bdata->raw_soc >= bkchg_soc)) {
				bdata->charger_ctrl_status
					= D2153_BAT_RECHG_FULL;
				bcm_chrgr_usb_en(0);
				bdata->eoc_status = true;
			pr_batt(FLOW,
		"%s. Recharging Done.(3) 2nd full > discharge > Recharge\n",
			__func__);
			} else if ((bdata->charger_ctrl_status
				== D2153_BAT_CHG_FRST_FULL)
			&& (bdata->raw_soc >= bkchg_soc)) {
				bdata->charger_ctrl_status
					= D2153_BAT_CHG_BACKCHG_FULL;
				bcm_chrgr_usb_en(0);
				bdata->eoc_status = true;
				pr_batt(FLOW,
			"%s. Recharging Started.(4) 1st full > 2nd full\n",
			__func__);
			} else if ((bdata->charger_ctrl_status
				< D2153_BAT_CHG_FRST_FULL)
				&& (bdata->raw_soc >= ftchg_soc)) {
				bdata->charger_ctrl_status
					= D2153_BAT_CHG_FRST_FULL;
				bcm_chrgr_usb_en(0);
				bdata->eoc_status = true;
				pr_batt(FLOW, "%s. First charge done.(1)\n",
					__func__);
			} else if ((bdata->charger_ctrl_status
				== D2153_BAT_RECHG_FULL)
				&& (bdata->raw_soc >= bkchg_soc)) {
					bdata->charger_ctrl_status
					= D2153_BAT_CHG_FRST_FULL;
				bcm_chrgr_usb_en(0);
				bdata->eoc_status = true;
			}
		} else {
			if ((bdata->charger_ctrl_status
				>= D2153_BAT_CHG_FRST_FULL)
				&& (bdata->raw_soc <= rechg_soc)) {
				pr_batt(FLOW, "%s. Recharging kick off\n",
					__func__);
				bcm_chrgr_usb_en(1);
				bdata->eoc_status = true;
			}
		}

	} else {
		if (bdata->is_charging	== POWER_SUPPLY_STATUS_CHARGING) {
			if (((bdata->charger_ctrl_status
				== D2153_BAT_RECHG_FULL)
				|| (bdata->charger_ctrl_status
				== D2153_BAT_CHG_BACKCHG_FULL))
				&& (bdata->average_voltage
				>= D2153_BAT_CHG_BACK_FULL_LVL)) {

				bdata->charger_ctrl_status
					= D2153_BAT_RECHG_FULL;
				bcm_chrgr_usb_en(0);
				bdata->eoc_status = true;
			pr_batt(FLOW,
		"%s. Recharging Done.(3) 2nd full > discharge > Recharge\n",
					__func__);
			} else if ((bdata->charger_ctrl_status
				== D2153_BAT_CHG_FRST_FULL)
				&& (bdata->average_voltage
				>= D2153_BAT_CHG_BACK_FULL_LVL)) {

				bdata->charger_ctrl_status
					= D2153_BAT_CHG_BACKCHG_FULL;
				bcm_chrgr_usb_en(0);
				bdata->eoc_status = true;
				pr_batt(FLOW,
			"%s. Recharging Started.(4) 1st full > 2nd full\n",
					__func__);
			/* Will stop charging when a voltage approach
			 * to first full charge level
			 */
			} else if ((bdata->charger_ctrl_status
				< D2153_BAT_CHG_FRST_FULL)
				&& (bdata->average_voltage
				>= D2153_BAT_CHG_FRST_FULL_LVL)) {


				bdata->charger_ctrl_status
					= D2153_BAT_CHG_FRST_FULL;
				bcm_chrgr_usb_en(0);
				bdata->eoc_status = true;
				pr_batt(FLOW,
					"%s. First charge done.(1)\n",
					__func__);
			} else if ((bdata->charger_ctrl_status
				< D2153_BAT_CHG_FRST_FULL)
				&& (bdata->average_voltage
				>= D2153_BAT_CHG_BACK_FULL_LVL)) {

				bdata->charger_ctrl_status
						= D2153_BAT_CHG_BACKCHG_FULL;
				bcm_chrgr_usb_en(0);
				bdata->eoc_status = true;
				pr_batt(FLOW,
				"%s. Fully charged.(2)(Back-charging done)\n",
					__func__);
			}
		} else {
			if ((bdata->charger_ctrl_status
				>= D2153_BAT_CHG_FRST_FULL)
				&& (bdata->average_voltage <= recharge_volt)) {

				bcm_chrgr_usb_en(1);
				bdata->eoc_status = true;
				pr_batt(FLOW,
			"%s. Recharging started. Vbat < rechrg_volt)\n",
					__func__);
			}
		}
	}

#endif
}
#endif


static void d2153_dbg_print(struct d2153_battery *bdata)
{
	struct rtc_time tm;
	struct timeval time;
	unsigned long local_time;

	int len = 0;

	pr_batt(VERBOSE, "Enter: %s\n", __func__);

#ifdef CONFIG_SEC_CHARGING_FEATURE
	len += snprintf(d2153_dbg_print1+len, sizeof(d2153_dbg_print1) - len,
			"F:%d%d%d%d%d%d\n",
			bdata->is_charging,
			bdata->charger_connected,
			bdata->charger_type,
			bdata->charger_ctrl_status,
			bdata->battery_present,
			bdata->eoc_status);
#else
	len += snprintf(d2153_dbg_print1+len, sizeof(d2153_dbg_print1) - len,
			"F:%d%d%d%d%d%d%d\n",
			bdata->is_charging,
			bdata->charger_connected,
			bdata->charger_type,
			bdata->charger_ctrl_status,
			bdata->battery_present,
			bdata->eoc_status,
			bdata->charging_timer_expired);
#endif

	#ifdef custom_t_BATTERY
	#if defined (custom_t_BATTERY_CAPACITY_SYNC)
	pr_batt(FLOW, "Battery Info, %d, %d, %d, %d, %d, %d, %d\n", 
        bdata->soc,
        bdata->ui_capacity,
        bdata->average_voltage,
        bdata->current_temperature,
        bdata->charger_connected,
        bdata->average_volt_adc,
        bdata->origin_volt_adc);
    #else
	pr_batt(FLOW, "Battery Info, %d, %d, %d, %d, %d, %d\n", 
        bdata->soc,
        bdata->average_voltage,
        bdata->current_temperature,
        bdata->charger_connected,
        bdata->average_volt_adc,
        bdata->origin_volt_adc);
	#endif  /* custom_t_BATTERY_CAPACITY_SYNC */
	#endif  /* custom_t_BATTERY */

	if (bdata->force_dbg_print || dbg_batt_en ||
		(strncmp(d2153_dbg_print1, d2153_dbg_print2, len) != 0)) {
		do_gettimeofday(&time);
		local_time = (u32)(time.tv_sec - (sys_tz.tz_minuteswest * 60));
		rtc_time_to_tm(local_time, &tm);

#ifdef CONFIG_SEC_CHARGING_FEATURE
		pr_batt(FLOW,
		"C: %4d, V: %4d, T: %4d @ (%04d-%02d-%02d %02d:%02d:%02d)\n",
			bdata->capacity, bdata->average_voltage,
			bdata->current_temperature,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec);

		pr_batt(FLOW, "F: %d %d %d %d %d %d\n",
				bdata->is_charging,
				bdata->charger_connected,
				bdata->charger_type,
				bdata->charger_ctrl_status,
				bdata->battery_present,
				bdata->eoc_status);
#else
		pr_batt(FLOW,
		"C: %4d, V: %4d, T: %4d @ (%04d-%02d-%02d %02d:%02d:%02d)\n",
			bdata->capacity, bdata->average_voltage,
			bdata->current_temperature,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec);

		pr_batt(FLOW, "F: %d %d %d %d %d %d %d\n",
				bdata->is_charging,
				bdata->charger_connected,
				bdata->charger_type,
				bdata->charger_ctrl_status,
				bdata->battery_present,
				bdata->eoc_status,
				bdata->charging_timer_expired);
#endif

		pr_batt(VERBOSE, "Temp-adc: %4d, Vbat-adc: %4d\n",
				bdata->average_temp_adc,
				bdata->average_volt_adc);


		pr_batt(FLOW,
				"# SOC = %3d.%d %%, ADC(read) = %4d,"
				"ADC(avg) = %4d, Voltage(avg) = %4d mV,"
				"ADC(VF) = %4d\n",
				(bdata->soc/10),
				(bdata->soc%10),
				bdata->origin_volt_adc,
				bdata->average_volt_adc,
				bdata->average_voltage,
				bdata->vf_adc);


		bdata->force_dbg_print = FALSE;
		strncpy(d2153_dbg_print2, d2153_dbg_print1, len);
	}
}
#ifdef custom_t_BATTERY
extern void vibrator_ctrl_regulator(int on_off);
#endif

#if defined (custom_t_BATTERY_CAPACITY_SYNC)
static void d2153_capacity_sync(struct d2153_battery *bdata)
{
    static u32 count_time = 0;
    
    pr_batt(FLOW,
        "d2153_capacity_sync %d, %d, %d\n",
        count_time, bdata->charger_connected, bdata->charger_change);

    if (bdata->charger_change)
    {
        count_time = 0; // reset timer.
        bdata->charger_change = false;
    }

    if (INVALID_CAPACITY == bdata->ui_capacity)
    {
        bdata->ui_capacity = bdata->capacity;   // Init ui_capacity.
        pr_batt(FLOW,
		    "Init ui_capacity: %d\n",
			bdata->ui_capacity);
    }
        
    if (bdata->charger_connected)   /* Charging */
    {
        if (bdata->capacity > bdata->ui_capacity)
        {
            count_time += (D2153_VOLTAGE_MONITOR_FAST/HZ);
            if (CAPACITY_SYNC_TIME_CHARGING <= count_time)   // 30s
            {
                if (100 > bdata->ui_capacity)
                    bdata->ui_capacity ++;
                
                count_time = 0;
            }
        }
        else
        {
            count_time = 0; // reset timer.
        }
    }
    else    /* Discharging */
    {
        if (bdata->capacity < bdata->ui_capacity)
        {
            count_time += (D2153_VOLTAGE_MONITOR_NORMAL/HZ);
            if (CAPACITY_SYNC_TIME_DISCHARGING <= count_time)   // 60s
            {
                if (0 < bdata->ui_capacity)
                    bdata->ui_capacity --;
                
                count_time = 0;
            }
        }
        else
        {
            count_time = 0; // reset timer.
        }
    }
}
#endif  /* custom_t_BATTERY_CAPACITY_SYNC */

#ifdef custom_t_BATTERY_PROTECT
static int voltage_too_low_times = 0;
#endif
static void d2153_monitor_voltage_work(struct work_struct *work)
{
	int ret;
	int ext_fg;
	u32 rechg_soc;
	struct d2153_battery *bdata =
		container_of(work, struct d2153_battery,
			monitor_volt_work.work);
	ret = 0;
	ext_fg = 0;
	if (bdata == NULL) {
		pr_batt(ERROR, "%s. Invalid driver data\n", __func__);
		goto err_adc_read;
	}
	ext_fg = bcm_agent_get(BCM_AGENT_GET_FG_PRIVATE, NULL);
	if (ext_fg != 0) {
		rechg_soc = bcm_agent_get(BCM_AGENT_GET_FG_RECHG_SOC, NULL);
		/* Calculate Capacity */
		bdata->soc = d2153_get_soc(bdata);
		if (bdata->soc < 0)
			goto err_adc_read;
		bdata->capacity = bdata->soc / 10;
        #if defined (custom_t_BATTERY_DISCHARGE)
        if (bdata->capacity <= 3)
        {
            bdata->capacity = 3;
        }
        #endif  /* custom_t_BATTERY_DISCHARGE */
		if (bdata->capacity > FULL_CAPACITY/10)
			bdata->capacity = FULL_CAPACITY/10;

        #if defined (custom_t_BATTERY_CAPACITY_SYNC)
        d2153_capacity_sync(bdata);
        #endif  /* custom_t_BATTERY_CAPACITY_SYNC */
        
		d2153_update_psy(bdata, false);
		if (bdata->capacity < FULL_CAPACITY/10) {
			if ((bdata->charger_ctrl_status
				>= D2153_BAT_CHG_FRST_FULL)
				&& (bdata->charger_connected))
				bcm_chrgr_usb_en(1);
			bdata->eoc_status = false;
			bdata->charger_ctrl_status = D2153_BAT_CHG_START;
		}

	} else {
		ret = d2153_read_voltage(bdata);
		if (ret < 0) {
			pr_batt(ERROR, "%s. Read voltage ADC failure\n",
					__func__);
			goto err_adc_read;
		}

		/* Calculate Capacity */
		bdata->soc = d2153_get_soc(bdata);
		bdata->capacity =	bdata->soc/10;

        #if defined (custom_t_BATTERY_DISCHARGE)
        if (bdata->capacity <= 3)
        {
            bdata->capacity = 3;
        }
        #endif  /* custom_t_BATTERY_DISCHARGE */
        
        #if defined (custom_t_BATTERY_CAPACITY_SYNC)
        d2153_capacity_sync(bdata);
        #endif  /* custom_t_BATTERY_CAPACITY_SYNC */
        
		d2153_update_psy(bdata, false);

		if (bdata->capacity < 100) {
			bdata->charger_ctrl_status = D2153_BAT_CHG_START;
			bdata->eoc_status = false;
		}
	}
	if (bdata->charger_connected) {
#ifdef CONFIG_SEC_CHARGING_FEATURE
		d2153_spa_sw_maintainance_charging(bdata);
#else
		d2153_sw_maintainance_charging(bdata);
#endif
	}

	d2153_dbg_print(bdata);

	if (!bdata->charger_connected)
		schedule_delayed_work(&bdata->monitor_volt_work,
			D2153_VOLTAGE_MONITOR_NORMAL);
	else
		schedule_delayed_work(&bdata->monitor_volt_work,
			D2153_VOLTAGE_MONITOR_FAST);

#ifdef CONFIG_D2153_HW_TIMER
	if (wake_lock_active(&bdata->d2153_alarm_wake_lock))
		wake_unlock(&bdata->d2153_alarm_wake_lock);
#endif	

#ifdef custom_t_BATTERY_PROTECT
	ret = d2153_battery_read_status(D2153_BATTERY_VOLTAGE_NOW);
	pr_batt(FLOW,"D2153_BATTERY_VOLTAGE_NOW %d, bdata->charger_type %d\n",ret,bdata->charger_type);
	
	#if defined (custom_t_BATTERY_DISCHARGE)
	if( (ret < 2800) && (ret > 0) && (bdata->charger_type == POWER_SUPPLY_TYPE_BATTERY) && (ext_fg == 0))
    #else
	if( (ret < 3300) && (ret > 0) && (bdata->charger_type == POWER_SUPPLY_TYPE_BATTERY) && (ext_fg == 0))
	#endif  /* custom_t_BATTERY_DISCHARGE */
	{
	/*
	< 4>[ 1.429488307 C0	  4 kworker/0:0 	]	Workqueue: events d2153_monitor_voltage_work 
	< 4>[ 1.429497768 C0	  4 kworker/0:0 	]	task: ee00ec00 ti: ee088000 task.ti: ee088000 
	< 4>[ 1.429513845 C0	  4 kworker/0:0 	]	PC is at regulator_set_voltage+0x8/0xfc 
	< 4>[ 1.429527538 C0	  4 kworker/0:0 	]	LR is at vibrator_ctrl_regulator+0x4c/0x12c 
	some time the vibrator_ctrl_regulator is not ready and the charge is disable first in driver, so we need check some time before power off
	*/
		voltage_too_low_times ++;
			
		if(voltage_too_low_times > 3)
		{
			printk("voltage is too low (%4d)!!! we will shutdown the phone.",ret);
			vibrator_ctrl_regulator(1);
#ifdef custom_t_BATTERY_CHARGER_ON
			//custom_t:CJ
			pr_batt(FLOW,"before call machine_power_off\n");
			bcm_chrgr_usb_en(1);
#endif				
			mdelay(300);
			machine_power_off();
		}
		else
		{
			printk("voltage is too low (%4d)!!! voltage_too_low_times %d.",ret,voltage_too_low_times);
		}
	}
	else
	{
		//printk("average_voltage is %4d\n",bdata->average_voltage);
	}
#endif  /* custom_t_BATTERY_PROTECT */

	return;

err_adc_read:
	schedule_delayed_work(&bdata->monitor_volt_work,
		D2153_VOLTAGE_MONITOR_START);
	return;
}

#ifndef CONFIG_SEC_CHARGING_FEATURE
static void d2153_batt_temp_check(struct d2153_battery *bdata)
{
	struct d2153_battery_platform_data *pdata =
			bdata->pd2153->pdata->pbat_platform;
	if ((bdata->is_charging == POWER_SUPPLY_STATUS_CHARGING) &&
		(bdata->current_temperature >= pdata->suspend_temp_hot)) {
		bdata->ntc_cnt++;
		if (bdata->ntc_cnt >= NTC_CNT_MAX) {
			/*Disable charging due to battery overheat*/
			pr_batt(FLOW,
				"%s:ntc temp %d exceed hot suspend temp\n",
					__func__, bdata->current_temperature);
			bcm_chrgr_usb_en(0);
			bdata->health = POWER_SUPPLY_HEALTH_OVERHEAT;
			bdata->ntc_cnt = 0;
		}
	} else if ((bdata->is_charging == POWER_SUPPLY_STATUS_CHARGING) &&
		(bdata->current_temperature <= pdata->suspend_temp_cold)) {
		bdata->ntc_cnt++;
		if (bdata->ntc_cnt >= NTC_CNT_MAX) {
			/*Disable charging due to battery low temp*/
			pr_batt(FLOW,
				"%s:ntc temp %d cross cold suspend temp\n",
					__func__, bdata->current_temperature);
			bcm_chrgr_usb_en(0);
			bdata->health = POWER_SUPPLY_HEALTH_COLD;
			bdata->ntc_cnt = 0;
		}
	} else if ((bdata->is_charging == POWER_SUPPLY_STATUS_DISCHARGING) &&
			(bdata->health == POWER_SUPPLY_HEALTH_OVERHEAT) &&
				(bdata->current_temperature <
						pdata->recovery_temp_hot)) {
		bdata->ntc_cnt++;
		if (bdata->ntc_cnt >= NTC_CNT_MAX) {
			/*Enable charging if disabled by this work*/
			pr_batt(FLOW,
				"%s:ntc temp %d less than hot recovery temp\n",
					__func__, bdata->current_temperature);
			bcm_chrgr_usb_en(1);
			bdata->health = POWER_SUPPLY_HEALTH_GOOD;
			bdata->ntc_cnt = 0;
		}
	} else if ((bdata->is_charging == POWER_SUPPLY_STATUS_DISCHARGING) &&
			(bdata->health == POWER_SUPPLY_HEALTH_COLD) &&
				(bdata->current_temperature >
						pdata->recovery_temp_cold)) {
		bdata->ntc_cnt++;
		if (bdata->ntc_cnt >= NTC_CNT_MAX) {
			/*Enable charging if disabled by this work*/
			pr_batt(FLOW,
				"%s:ntc temp %d exceeded cold recovery temp\n",
					__func__, bdata->current_temperature);
			bcm_chrgr_usb_en(1);
			bdata->health = POWER_SUPPLY_HEALTH_GOOD;
			bdata->ntc_cnt = 0;
		}
	} else if (bdata->ntc_cnt) {
		bdata->ntc_cnt = 0;
	}
	return;
}
#endif

static void d2153_monitor_temperature_work(struct work_struct *work)
{
	struct d2153_battery *bdata =
		container_of(work, struct d2153_battery,
			monitor_temp_work.work);
	int ret;

	ret = d2153_read_temperature(bdata);
	if(ret < 0) {
		pr_batt(ERROR, "%s. Failed to read_temperature\n", __func__);
		schedule_delayed_work(&bdata->monitor_temp_work,
			D2153_TEMPERATURE_MONITOR_FAST);
		return;
	}

	/*Calculate NTC Temperature */
    #if defined (custom_t_BATTERY_TEMPERATURE_DEBUG)
    if(battery_cmd_thermal_test_mode == 1)
    {
        bdata->current_temperature = battery_cmd_thermal_test_mode_value*10;
        pr_batt(FLOW, "[Battery] In thermal_test_mode , Tbat=%d\n", bdata->current_temperature);
    }
    else
    {
        bdata->current_temperature = d2153_get_temp(bdata);
    }
    #else
    bdata->current_temperature = d2153_get_temp(bdata);
    #endif  /* custom_t_BATTERY_TEMPERATURE_DEBUG */

	/* Whenever there is change in temp for more than 1C, set the flag */
	if (IS_N_DIFF(bdata->prev_temperature,
				bdata->current_temperature, BATT_TEMP_DIFF)) {
		bdata->prev_temperature = bdata->current_temperature;
		bdata->force_dbg_print = TRUE;
	}

#ifndef CONFIG_SEC_CHARGING_FEATURE
	if (bdata->charger_connected)
		d2153_batt_temp_check(bdata);
#endif
	if (bdata->temp_adc_init_done) {
		schedule_delayed_work(&bdata->monitor_temp_work,
			D2153_TEMPERATURE_MONITOR_NORMAL);
	}
	else {
		bdata->temp_adc_init_done = TRUE;
		schedule_delayed_work(&bdata->monitor_temp_work,
			D2153_TEMPERATURE_MONITOR_FAST);
	}

#ifdef CONFIG_D2153_BATTERY_DEBUG
	pr_batt(FLOW,
		"TEMP_BOARD(ADC) = %4d, Board Temperauter(Celsius) = %3d.%d\n",
		bdata->average_temp_adc,
		(bdata->current_temperature/10),
		(bdata->current_temperature%10)); // BCM_ID_814048
#endif

#ifdef custom_t_BATTERY_CHARGER_ON0
	//custom_t:CJ
	if( /*bdata->current_voltage != 0 && */
		bdata->current_voltage < 3700 && 
		bdata->health == POWER_SUPPLY_HEALTH_GOOD && 
		!bdata->charger_connected)
	{
		pr_batt(FLOW,"current_voltage %d, charger_connected %d, enable chager\n",
			bdata->current_voltage,bdata->charger_connected);
		
		bcm_chrgr_usb_en(1);
//		bcm_set_icc_fc(500);
	}
#endif

	return ;
}

#ifndef CONFIG_SEC_CHARGING_FEATURE
static void d2153_charging_expiry_work(struct work_struct *work)
{
	struct d2153_battery *bdata =
		container_of(work, struct d2153_battery,
			charging_expiry_work.work);
	pr_batt(FLOW, "%s:Charging will be disabled Now\n", __func__);
	bdata->charging_timer_expired = true;
	bcm_chrgr_usb_en(0);
}
#endif
/*
 * Name : d2153_battery_start
 */
void d2153_battery_start(void)
{
	schedule_delayed_work(&gbat->monitor_volt_work, 0);
}
EXPORT_SYMBOL_GPL(d2153_battery_start);


/*
 * Name : d2153_battery_data_init
 */
static void d2153_battery_data_init(struct d2153_battery *bdata)
{
	unsigned int tb_len;

	if (unlikely(!bdata)) {
		pr_batt(ERROR, "%s. Invalid platform data\n", __func__);
		return;
	}

	bdata->adc_mode = D2153_ADC_MODE_MAX;

	bdata->sum_total_adc = 0;
	bdata->vdd_hwmon_level = 0;
	bdata->adc_max_lmt = FALSE;
	bdata->adc_prev_val = 0;
	bdata->volt_adc_init_done = FALSE;
	bdata->temp_adc_init_done = FALSE;
	bdata->battery_present = BATTERY_DETECTED;
	bdata->is_charging = POWER_SUPPLY_STATUS_DISCHARGING;
	bdata->charger_type = POWER_SUPPLY_TYPE_BATTERY;
#ifndef CONFIG_SEC_CHARGING_FEATURE
	bdata->health = POWER_SUPPLY_HEALTH_GOOD;
	bdata->ntc_cnt = 0;
#endif
	bdata->force_dbg_print = FALSE;
	memset(d2153_dbg_print1, 0, sizeof(d2153_dbg_print1));
	memset(d2153_dbg_print2, 0, sizeof(d2153_dbg_print2));

	tb_len = bdata->pd2153->pdata->pbat_platform->batt_ntc_temp_tb_len;
	temp_adc_min =
	bdata->pd2153->pdata->pbat_platform->batt_ntc_temp_tb[tb_len-1].adc;
	temp_adc_max =
	bdata->pd2153->pdata->pbat_platform->batt_ntc_temp_tb[0].adc;

#ifdef CONFIG_D2153_EOC_CTRL
	bdata->charger_ctrl_status = D2153_BAT_CHG_START;
#endif

#ifdef CONFIG_D2153_HW_TIMER
	wake_lock_init(&bdata->d2153_alarm_wake_lock,
			WAKE_LOCK_SUSPEND, "d2153_alarm_wakelock");
#endif

    #if defined (custom_t_BATTERY_CAPACITY_SYNC)
    bdata->ui_capacity = INVALID_CAPACITY;
    #endif  /* custom_t_BATTERY_CAPACITY_SYNC */

	return;
}

#ifdef CONFIG_D2153_HW_TIMER
enum alarmtimer_restart d2153_battery_alarm_callback(
		struct alarm *alarm, ktime_t now)
{
	struct d2153_battery *bdata = to_d2153_battery_data(alarm, alarm);
	wake_lock(&bdata->d2153_alarm_wake_lock);

	return ALARMTIMER_NORESTART;
}
#endif /* CONFIG_D2153_HW_TIMER */

#ifndef CONFIG_SEC_CHARGING_FEATURE
static int d2153_battery_get_properties(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct d2153_battery *bdata = to_d2153_battery_data(psy, psy);
	int ret = 0;
	int ext_fg;
	ext_fg = 0;
	ext_fg = bcm_agent_get(BCM_AGENT_GET_FG_PRIVATE, NULL);
	pr_batt(VERBOSE, "%s: Prop: %d\n", __func__, psp);
	switch (psp) {
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (!bdata->charger_connected)
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (bdata->capacity == FULL_CAPACITY/10 ||
				bdata->eoc_status)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = bdata->is_charging;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = bdata->battery_present;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		#if defined (custom_t_BATTERY_CAPACITY_SYNC)
        if (INVALID_CAPACITY != bdata->ui_capacity)
		    val->intval = bdata->ui_capacity;
        else
		    val->intval = bdata->capacity;
        #else
		val->intval = bdata->capacity;
		#endif  /* custom_t_BATTERY_CAPACITY_SYNC */
		pr_batt(VERBOSE, "POWER_SUPPLY_PROP_CAPACITY = %d\n",
				val->intval);
		BUG_ON(val->intval < 0);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (ext_fg != 0)
			val->intval = bcm_agent_get(BCM_AGENT_GET_VCELL, NULL);
		else
			val->intval = bdata->average_voltage;
		val->intval = val->intval * 1000;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = bdata->current_temperature;
		break;
	case POWER_SUPPLY_PROP_TEMP_HPA:
		val->intval = d2153_get_rf_temperature();
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = bdata->health;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval =
			bdata->pd2153->pdata->pbat_platform->battery_capacity;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = "Li-Ion Battery";
		break;
	default:
		BUG_ON(1);
		break;
	}

	return ret;
}
#endif

static int d2153_battery_event_handler(struct notifier_block *nb,
		unsigned long event, void *para)
{
	struct d2153_battery *bdata;
	int enable;

	switch (event) {
	case PMU_ACCY_EVT_OUT_CHRGR_TYPE:
		bdata = to_d2153_battery_data(nb, chgr_detect);
		bdata->charger_type = *(enum power_supply_type *)para;
		if ((bdata->charger_type == POWER_SUPPLY_TYPE_UNKNOWN) ||
			(bdata->charger_type == POWER_SUPPLY_TYPE_BATTERY)) {
			pr_batt(FLOW, "Charger Disconnected!!!!\n");
            #if defined (custom_t_BATTERY_CAPACITY_SYNC)
            if (bdata->charger_connected)
                bdata->charger_change = true;
            #endif  /* custom_t_BATTERY_CAPACITY_SYNC */
			bdata->charger_connected = false;
			bdata->eoc_status = false;
			bdata->charger_ctrl_status = D2153_BAT_CHG_START;
			bdata->is_charging = POWER_SUPPLY_STATUS_DISCHARGING;
#ifndef CONFIG_SEC_CHARGING_FEATURE
			bdata->health = POWER_SUPPLY_HEALTH_GOOD;
#endif

		} else if ((bdata->charger_type == POWER_SUPPLY_TYPE_UPS) ||
			(bdata->charger_type == POWER_SUPPLY_TYPE_MAINS) ||
			(bdata->charger_type == POWER_SUPPLY_TYPE_USB) ||
			(bdata->charger_type == POWER_SUPPLY_TYPE_USB_DCP) ||
			(bdata->charger_type == POWER_SUPPLY_TYPE_USB_CDP) ||
			(bdata->charger_type == POWER_SUPPLY_TYPE_USB_ACA)) {

			pr_batt(FLOW, "Charger Connected!!!!, Type : %d\n",
				bdata->charger_type);
			bdata->is_charging = POWER_SUPPLY_STATUS_CHARGING;
            #if defined (custom_t_BATTERY_CAPACITY_SYNC)
            if (!bdata->charger_connected)
                bdata->charger_change = true;
            #endif  /* custom_t_BATTERY_CAPACITY_SYNC */
			bdata->charger_connected = true;
			bdata->eoc_status = false;
		} else {
			pr_batt(FLOW, "Unkown Charger Detected!!!!\n");
			bdata->charger_connected = false;
			bdata->eoc_status = false;
		}
	break;
	case PMU_CHRGR_EVT_CHRG_STATUS:
		bdata = to_d2153_battery_data(nb, chgr_detect);
		enable = *(int *)para;
		if (enable && (bdata->charger_connected == true)) {
			pr_batt(FLOW, "Charging Enabled!!!\n");
			bdata->is_charging = POWER_SUPPLY_STATUS_CHARGING;
#ifndef CONFIG_SEC_CHARGING_FEATURE
			bdata->charging_timer_expired = false;
			/* Start charging expiry work */
			schedule_delayed_work(&bdata->charging_expiry_work,
				msecs_to_jiffies(
				bdata->pd2153->pdata->pbat_platform->chrgr_exp
				*SEC_FOR_MINUTE
				*SEC_TO_MILLISEC));
#endif
		} else {
			pr_batt(FLOW, "Charging Disabled!!!\n");
			bdata->is_charging = POWER_SUPPLY_STATUS_DISCHARGING;
#ifndef CONFIG_SEC_CHARGING_FEATURE
			cancel_delayed_work(&bdata->charging_expiry_work);
			bdata->charging_timer_expired = false;
#endif
		}
	d2153_update_psy(bdata, true);
	break;
	}
	return 0;
}

static int d2153_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t d2153_adc_set_mode(struct file *file, char const __user *buf,
					size_t count, loff_t *offset)
{
	struct d2153_battery *bdata = file->private_data;

	int len, mode;
	char input_str[100];
	adc_mode adc_mode = 1;

	if (count > 100)
		len = 100;
	else
		len = count;
	memset(input_str, 0, 100);
	if (copy_from_user(input_str, buf, len))
		return -EFAULT;
	sscanf(input_str, "%d\n", &mode);

	if (mode == 1)
		adc_mode = D2153_ADC_IN_AUTO;
	else if (mode == 2)
		adc_mode = D2153_ADC_IN_MANUAL;
	if (bdata->adc_mode != adc_mode) {
		d2153_set_adc_mode(bdata, adc_mode,
			bdata->pd2153->pdata->pbat_platform->adc_config_mode);
		bdata->adc_mode =  adc_mode;
	}
	return count;
}

static ssize_t d2153_adc_get_mode(struct file *file, char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct d2153_battery *bdata = file->private_data;

	char out_str[100];
	int len = 0;

	memset(out_str, 0, sizeof(out_str));
	len += snprintf(out_str+len, sizeof(out_str) - len, "\nADC_MODE %d\n\n",
							bdata->adc_mode);
	len += snprintf(out_str+len, sizeof(out_str) - len,
	"To update:echo <option> > adc_mode\nOptions:\n1.AUTO\n2.MANUAL\n");
	return simple_read_from_buffer(user_buf, count, ppos,
							out_str, len);

}

static const struct file_operations mode_fops = {
	.open = d2153_debugfs_open,
	.write = d2153_adc_set_mode,
	.read = d2153_adc_get_mode,
};


#ifdef CONFIG_D2153_DEBUG_FEATURE
int debugfs_d2153_flags_init(struct d2153_battery *bdata,
	struct dentry *flags_dir)
{
	struct dentry *file;

	file = debugfs_create_u8("charging", DEBUG_FS_PERMISSIONS,
			flags_dir, &bdata->is_charging);
	if (IS_ERR_OR_NULL(file))
		return -1;

	file = debugfs_create_u8("charger_connected", DEBUG_FS_PERMISSIONS,
			flags_dir, (u8 *)&bdata->charger_connected);
	if (IS_ERR_OR_NULL(file))
		return -1;

	file = debugfs_create_u8("charger_type", DEBUG_FS_PERMISSIONS,
			flags_dir, (u8 *)&bdata->charger_type);
	if (IS_ERR_OR_NULL(file))
		return -1;

	file = debugfs_create_u8("charger_ctrl_status", DEBUG_FS_PERMISSIONS,
			flags_dir, (u8 *)&bdata->charger_ctrl_status);
	if (IS_ERR_OR_NULL(file))
		return -1;

	file = debugfs_create_u8("battery_present", DEBUG_FS_PERMISSIONS,
			flags_dir, (u8 *)&bdata->battery_present);
	if (IS_ERR_OR_NULL(file))
		return -1;

	file = debugfs_create_u8("volt_adc_init_done", DEBUG_FS_PERMISSIONS,
			flags_dir, (u8 *)&bdata->volt_adc_init_done);
	if (IS_ERR_OR_NULL(file))
		return -1;

	file = debugfs_create_u8("temp_adc_init_done", DEBUG_FS_PERMISSIONS,
			flags_dir, (u8 *)&bdata->temp_adc_init_done);
	if (IS_ERR_OR_NULL(file))
		return -1;

	file = debugfs_create_u8("eoc_status", DEBUG_FS_PERMISSIONS,
			flags_dir, (u8 *)&bdata->eoc_status);
	if (IS_ERR_OR_NULL(file))
		return -1;

	return 0;
}


static void d2153_battery_debugfs_init(struct d2153_battery *bdata)
{
	struct dentry *dentry_d2153_batt_dir;
	struct dentry *dentry_d2153_flags_dir;
	struct dentry *dentry_d2153_batt_file;

	dentry_d2153_batt_dir = debugfs_create_dir("d2153-batt",
		bdata->pd2153->dent_d2153);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_dir))
		goto debugfs_clean;

	dentry_d2153_flags_dir = debugfs_create_dir("flags",
		dentry_d2153_batt_dir);
	if (IS_ERR_OR_NULL(dentry_d2153_flags_dir))
		goto debugfs_clean;

	if (debugfs_d2153_flags_init(bdata, dentry_d2153_flags_dir))
		goto debugfs_clean;

	dentry_d2153_batt_file = debugfs_create_u32("capacity",
		DEBUG_FS_PERMISSIONS,
		dentry_d2153_batt_dir, &bdata->capacity);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_file))
		goto debugfs_clean;

	dentry_d2153_batt_file = debugfs_create_u32("debug_mask",
		DEBUG_FS_PERMISSIONS,
		dentry_d2153_batt_dir, &debug_mask);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_file))
		goto debugfs_clean;

	dentry_d2153_batt_file = debugfs_create_u16("wakeup_poll_rate",
		DEBUG_FS_PERMISSIONS,
		dentry_d2153_batt_dir,
			&bdata->pd2153->pdata->pbat_platform->poll_rate);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_file))
		goto debugfs_clean;

	dentry_d2153_batt_file = debugfs_create_u32("suspend_temp_hot",
		DEBUG_FS_PERMISSIONS, dentry_d2153_batt_dir,
			&bdata->pd2153->pdata->pbat_platform->suspend_temp_hot);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_file))
		goto debugfs_clean;


	dentry_d2153_batt_file = debugfs_create_u32("recovery_temp_hot",
		DEBUG_FS_PERMISSIONS, dentry_d2153_batt_dir,
		&bdata->pd2153->pdata->pbat_platform->recovery_temp_hot);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_file))
		goto debugfs_clean;


	dentry_d2153_batt_file = debugfs_create_u32("suspend_temp_cold",
		DEBUG_FS_PERMISSIONS, dentry_d2153_batt_dir,
		&bdata->pd2153->pdata->pbat_platform->suspend_temp_cold);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_file))
		goto debugfs_clean;


	dentry_d2153_batt_file = debugfs_create_u32("recovery_temp_cold",
		DEBUG_FS_PERMISSIONS, dentry_d2153_batt_dir,
		&bdata->pd2153->pdata->pbat_platform->recovery_temp_cold);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_file))
		goto debugfs_clean;


	dentry_d2153_batt_file = debugfs_create_u32("ntc_disable",
		DEBUG_FS_PERMISSIONS,
		dentry_d2153_batt_dir, &ntc_disable);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_file))
		goto debugfs_clean;

	dentry_d2153_batt_file = debugfs_create_file("adc_mode",
		DEBUG_FS_PERMISSIONS,
		dentry_d2153_batt_dir, bdata, &mode_fops);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_file))
		goto debugfs_clean;

	dentry_d2153_batt_file = debugfs_create_u32("dbg_batt_en",
		DEBUG_FS_PERMISSIONS,
		dentry_d2153_batt_dir, &dbg_batt_en);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_file))
		goto debugfs_clean;

#ifndef CONFIG_SEC_CHARGING_FEATURE
	dentry_d2153_batt_file = debugfs_create_u16("chrgr_expiry_in_min",
		DEBUG_FS_PERMISSIONS,
		dentry_d2153_batt_dir,
			&bdata->pd2153->pdata->pbat_platform->chrgr_exp);
	if (IS_ERR_OR_NULL(dentry_d2153_batt_file))
		goto debugfs_clean;
#endif

	return;

debugfs_clean:
	if (!IS_ERR_OR_NULL(dentry_d2153_batt_dir))
		debugfs_remove_recursive(dentry_d2153_batt_dir);

}
#endif

#if defined (custom_t_BATTERY_TEMPERATURE_DEBUG)
static int proc_utilization_show(struct seq_file *m, void *v)
{
    seq_printf(m, "=> g_battery_thermal_throttling_flag=%d,\nbattery_cmd_thermal_test_mode=%d,\nbattery_cmd_thermal_test_mode_value=%d\n", 
        g_battery_thermal_throttling_flag, battery_cmd_thermal_test_mode, battery_cmd_thermal_test_mode_value);
    return 0;
}

static int proc_utilization_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_utilization_show, NULL);
}

static ssize_t battery_cmd_write(struct file *file, const char *buffer, size_t count, loff_t *data)
{
    int len = 0, bat_tt_enable=0, bat_thr_test_mode=0, bat_thr_test_value=0;
    char desc[32];
    
    len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
    if (copy_from_user(desc, buffer, len))
    {
        return 0;
    }
    desc[len] = '\0';
    
    if (sscanf(desc, "%d %d %d", &bat_tt_enable, &bat_thr_test_mode, &bat_thr_test_value) == 3)
    {
        g_battery_thermal_throttling_flag = bat_tt_enable;
        battery_cmd_thermal_test_mode = bat_thr_test_mode;
        battery_cmd_thermal_test_mode_value = bat_thr_test_value;
        
        pr_batt(FLOW, "bat_tt_enable=%d, bat_thr_test_mode=%d, bat_thr_test_value=%d\n", 
            g_battery_thermal_throttling_flag, battery_cmd_thermal_test_mode, battery_cmd_thermal_test_mode_value);
        
        return count;
    }
    else
    {
        pr_batt(FLOW, "  bad argument, echo [bat_tt_enable] [bat_thr_test_mode] [bat_thr_test_value] > battery_cmd\n");
    }
    
    return -EINVAL;
}


static const struct file_operations battery_cmd_proc_fops = { 
    .open  = proc_utilization_open, 
    .read  = seq_read,
    .write = battery_cmd_write,
};
#endif  /* custom_t_BATTERY_TEMPERATURE_DEBUG */

/*
 * Name : d2153_battery_probe
 */
static int d2153_battery_probe(struct platform_device *pdev)
{
	struct d2153 *d2153 = platform_get_drvdata(pdev);
	struct d2153_battery *bdata = &d2153->batt;

	int i;
	int ret = 0;

	pr_batt(INIT, "Start %s\n", __func__);

	if (unlikely(!bdata)) {
		pr_batt(ERROR, "%s. Invalid platform data\n", __func__);
		return -EINVAL;
	}

	gbat = bdata;
	bdata->pd2153 = d2153;

	// Initialize a resource locking
	mutex_init(&bdata->lock);
	mutex_init(&bdata->api_lock);
	mutex_init(&bdata->meoc_lock);

	// Store a driver data structure to platform.
	platform_set_drvdata(pdev, bdata);

#ifndef CONFIG_SEC_CHARGING_FEATURE
		bdata->psy.name = "battery";
		bdata->psy.type = POWER_SUPPLY_TYPE_BATTERY;
		bdata->psy.properties = d2153_battery_props;
		bdata->psy.num_properties = ARRAY_SIZE(d2153_battery_props);
		bdata->psy.get_property = d2153_battery_get_properties;
		bdata->psy.external_power_changed = NULL;
		/*
		fg->psy.external_power_changed =
			bcmpmu_fg_external_power_changed;
		*/
#endif


	d2153_battery_data_init(bdata);
	d2153_set_adc_mode(bdata,
		bdata->pd2153->pdata->pbat_platform->adc_conv_mode,
			bdata->pd2153->pdata->pbat_platform->adc_config_mode);
	// Disable 50uA current source in Manual ctrl register
	d2153_reg_write(bdata->pd2153, D2153_ADC_MAN_REG, 0x00);

	INIT_DELAYED_WORK(&bdata->monitor_volt_work,
		d2153_monitor_voltage_work);
	INIT_DELAYED_WORK(&bdata->monitor_temp_work,
		d2153_monitor_temperature_work);
#ifndef CONFIG_SEC_CHARGING_FEATURE
	INIT_DELAYED_WORK(&bdata->charging_expiry_work,
		d2153_charging_expiry_work);
#endif

	device_init_wakeup(&pdev->dev, 1);


#ifdef CONFIG_D2153_DEBUG_FEATURE
	for (i = 0; i < D2153_PROP_MAX ; i++) {
		ret = device_create_file(&pdev->dev, &d2153_battery_attrs[i]);
		if (ret) {
			pr_batt(ERROR,
				"Failed to create battery sysfs entries\n");
			goto exit;
		}
	}
	d2153_battery_debugfs_init(bdata);
#endif

    #if defined (custom_t_BATTERY_TEMPERATURE_DEBUG)
    proc_create("battery_cmd", 0666/*S_IRUGO | S_IWUSR*/, NULL, &battery_cmd_proc_fops);
    #endif  /* custom_t_BATTERY_TEMPERATURE_DEBUG */

#ifdef CONFIG_D2153_HW_TIMER
	alarm_init(&bdata->alarm, ALARM_REALTIME, d2153_battery_alarm_callback);
#endif

#ifdef CONFIG_SEC_CHARGING_FEATURE
	spa_agent_register(SPA_AGENT_GET_CAPACITY,
			(void *)d2153_get_capacity, "d2153_battery");
	spa_agent_register(SPA_AGENT_GET_TEMP,
			(void *)d2153_get_temp_adc, "d2153_battery");
	spa_agent_register(SPA_AGENT_GET_VOLTAGE,
			(void *)d2153_get_voltage, "d2153-battery");
	spa_agent_register(SPA_AGENT_GET_BATT_PRESENCE_PMIC,
			(void *)d2153_check_bat_presence, "d2153-battery");
	spa_agent_register(SPA_AGENT_CTRL_FG,
			(void *)d2153_ctrl_fg, "d2153-battery");
	spa_agent_register(SPA_AGENT_GET_FULL_CHARGE,
			(void *)d2153_get_full_charge, "d2153-battery");
#endif

	/* Intialize and register Events */
	pr_batt(FLOW, "%s.Before ADD ...\n", __func__);
	bdata->chgr_detect.notifier_call = d2153_battery_event_handler;
	ret = bcm_add_notifier(PMU_ACCY_EVT_OUT_CHRGR_TYPE,
			&bdata->chgr_detect);
	if (ret) {
		pr_batt(ERROR, "%s:Event handler registration failed\n",
			__func__);
		goto exit;
	}

	bdata->chgr_detect.notifier_call = d2153_battery_event_handler;
	ret = bcm_add_notifier(PMU_CHRGR_EVT_CHRG_STATUS,
			&bdata->chgr_detect);
	if (ret) {
		pr_batt(ERROR, "%s:Event handler registration failed\n",
			__func__);
		goto exit;
	}

#ifndef CONFIG_SEC_CHARGING_FEATURE
	ret = power_supply_register(&pdev->dev, &bdata->psy);
	if (ret) {
		pr_batt(ERROR, "%s: Failed to register power supply\n",
				__func__);
		goto exit;
	}
	pr_batt(FLOW, "%s regulated_vol:%d\n",
			__func__,
			bdata->pd2153->pdata->pbat_platform->regulated_vol);
	bcm_set_charge_volt(bdata->pd2153->pdata->pbat_platform->regulated_vol);
#endif

	/* Start schedule of dealyed work for Temperature and Voltage */
	schedule_delayed_work(&bdata->monitor_temp_work, 0);
	/* Start one second late so that temp is updated by that time */
	schedule_delayed_work(&bdata->monitor_volt_work,
		msecs_to_jiffies(D2153_VOLT_WORK_DELAY));

	pr_batt(FLOW, "%s. End...\n", __func__);
	return ret;
exit:
	pr_batt(FLOW, "%s. Probe Failed...\n", __func__);
	return ret;
}

/*
 * Name : d2153_battery_suspend
 */

static int d2153_battery_suspend_pre(struct device *dev)
{
	struct d2153_battery *bdata = dev_get_drvdata(dev);
#ifdef CONFIG_D2153_HW_TIMER
	struct timeval suspend_time = {0, 0};
#endif
	pr_batt(VERBOSE, "%s. Enter\n", __func__);

	cancel_delayed_work_sync(&bdata->monitor_temp_work);
	cancel_delayed_work_sync(&bdata->monitor_volt_work);

#ifdef CONFIG_D2153_HW_TIMER
	if(suspend_time.tv_sec == 0) {
		do_gettimeofday(&suspend_time);
		pr_batt(FLOW, "%s: suspend_time = %ld\n", __func__,
			suspend_time.tv_sec);
	}

	/* If DCP charger is connected, wakeup early */
	if (bdata->charger_type == POWER_SUPPLY_TYPE_USB_DCP)
		alarm_start_relative(&bdata->alarm, ktime_set(
					DCP_TIMER_RESET_PERIOD_SEC, 0));
	else
		alarm_start_relative(&bdata->alarm, ktime_set(
			bdata->pd2153->pdata->pbat_platform->poll_rate, 0));
#endif /* CONFIG_D2153_HW_TIMER */
	pr_batt(VERBOSE, "%s. Leave\n", __func__);

	return 0;
}

/*
 * Name : d2153_battery_complete
 */
static void d2153_battery_complete(struct device *dev)
{
	struct d2153_battery *bdata = dev_get_drvdata(dev);
#ifdef CONFIG_D2153_HW_TIMER
	struct timeval resume_time = {0, 0};
#endif
	pr_batt(VERBOSE, "%s. Enter\n", __func__);

#ifdef CONFIG_D2153_HW_TIMER
	do_gettimeofday(&resume_time);
	pr_batt(FLOW, "%s:resume_time = %ld\n", __func__,
		resume_time.tv_sec);

	alarm_cancel(&bdata->alarm);
#endif
	bdata->force_dbg_print = TRUE;

	// Start schedule of dealyed work for monitoring voltage and temperature.
	schedule_delayed_work(&bdata->monitor_temp_work, 0);
	schedule_delayed_work(&bdata->monitor_volt_work, 0);

	pr_batt(VERBOSE, "%s. Leave\n", __func__);
}

/*
 * Name : d2153_battery_remove
 */
static int d2153_battery_remove(struct platform_device *pdev)
{
	struct d2153_battery *bdata = platform_get_drvdata(pdev);

	u8 i;

	pr_batt(FLOW, "%s. Start\n", __func__);

	cancel_delayed_work_sync(&bdata->monitor_volt_work);
	cancel_delayed_work_sync(&bdata->monitor_temp_work);

	if(!d2153_get_adc_hwsem())
		d2153_put_adc_hwsem();

#ifdef CONFIG_D2153_DEBUG_FEATURE
	for (i = 0; i < D2153_PROP_MAX ; i++) {
		device_remove_file(&pdev->dev, &d2153_battery_attrs[i]);
	}
#endif

	pr_batt(FLOW, "%s. End\n", __func__);
	return 0;
}

static void d2153_battery_shutdown(struct platform_device *pdev)
{
#ifdef custom_t_BATTERY_CHARGER_ON
//custom_t:CJ
	pr_batt(FLOW,"d2153_battery_shutdown\n");
	bcm_chrgr_usb_en(1);
//	bcm_set_icc_fc(500);
#endif

	d2153_battery_remove(pdev);
}

static const struct dev_pm_ops pm = {
	/* make sure this is called before alarmtimer suspend */
	.prepare = d2153_battery_suspend_pre,
	.complete = d2153_battery_complete,
};

static struct platform_driver d2153_battery_driver = {
	.probe    = d2153_battery_probe,
	.remove   = d2153_battery_remove,
	.shutdown = d2153_battery_shutdown,
	.driver   = {
		.name  = "d2153-battery",
		.owner = THIS_MODULE,
		.pm = &pm ,
    },
};

static int __init d2153_battery_init(void)
{
	printk(d2153_battery_banner);
	return platform_driver_register(&d2153_battery_driver);
}
subsys_initcall(d2153_battery_init);

static void __exit d2153_battery_exit(void)
{
	flush_scheduled_work();
	platform_driver_unregister(&d2153_battery_driver);
}
module_exit(d2153_battery_exit);

MODULE_AUTHOR("Dialog Semiconductor Ltd. < eric.jeong@diasemi.com >");
MODULE_DESCRIPTION("Battery driver for the Dialog D2153 PMIC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("Power supply : d2153-battery");
