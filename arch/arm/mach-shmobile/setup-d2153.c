/*
 * arch/arm/mach-shmobile/setup-d2153.c
 *
 * Default configurations for D2153 PMIC
 *
 * Copyright (C) 2013 Renesas Mobile Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/d2153/core.h>
#include <linux/d2153/pmic.h>

#define mV_to_uV(v)                 ((v) * 1000)
#define uV_to_mV(v)                 ((v) / 1000)
#define MAX_MILLI_VOLT              (3300)


/* D2153 DC-DCs */
// BUCK1
__weak struct regulator_consumer_supply d2153_buck1_supplies[] = {
	REGULATOR_SUPPLY("vcore", NULL),
};

static struct regulator_init_data d2153_buck1 = {
	.constraints = {
		.min_uV = D2153_BUCK1_VOLT_LOWER,
		.max_uV = D2153_BUCK1_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_buck1_supplies),
	.consumer_supplies = d2153_buck1_supplies,
};

// BUCK2
__weak struct regulator_consumer_supply d2153_buck2_supplies[] = {
	REGULATOR_SUPPLY("vio2", NULL),
};

static struct regulator_init_data d2153_buck2 = {
	.constraints = {
		.min_uV = D2153_BUCK2_VOLT_LOWER,
		.max_uV = D2153_BUCK2_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_buck2_supplies),
	.consumer_supplies = d2153_buck2_supplies,
};

// BUCK3
__weak struct regulator_consumer_supply d2153_buck3_supplies[] = {
	REGULATOR_SUPPLY("vio1", NULL),
};

static struct regulator_init_data d2153_buck3 = {
	.constraints = {
		.min_uV = D2153_BUCK3_VOLT_LOWER,
		.max_uV = D2153_BUCK3_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_buck3_supplies),
	.consumer_supplies = d2153_buck3_supplies,
};

// BUCK4
__weak struct regulator_consumer_supply d2153_buck4_supplies[] = {
	REGULATOR_SUPPLY("vcore_rf", NULL),
};

static struct regulator_init_data d2153_buck4 = {
	.constraints = {
		.min_uV = D2153_BUCK4_VOLT_LOWER,
		.max_uV = D2153_BUCK4_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_buck4_supplies),
	.consumer_supplies = d2153_buck4_supplies,
};

// BUCK5
__weak struct regulator_consumer_supply d2153_buck5_supplies[] = {
	REGULATOR_SUPPLY("vana1_rf", NULL),
};

static struct regulator_init_data d2153_buck5 = {
	.constraints = {
		.min_uV = D2153_BUCK5_VOLT_LOWER,
		.max_uV = D2153_BUCK5_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_buck5_supplies),
	.consumer_supplies = d2153_buck5_supplies,
};

// BUCK6
__weak struct regulator_consumer_supply d2153_buck6_supplies[] = {
	REGULATOR_SUPPLY("vpam", NULL),
};

static struct regulator_init_data d2153_buck6 = {
	.constraints = {
		.min_uV = D2153_BUCK6_VOLT_LOWER,
		.max_uV = D2153_BUCK6_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_buck6_supplies),
	.consumer_supplies = d2153_buck6_supplies,
};

/* D2153 LDOs */
// LDO1
__weak struct regulator_consumer_supply d2153_ldo1_supplies[] = {
	REGULATOR_SUPPLY("vdig_rf", NULL),	// VDIG_RF
};

static struct regulator_init_data d2153_ldo1 = {
	.constraints = {
		.min_uV = D2153_LDO1_VOLT_LOWER,
		.max_uV = D2153_LDO1_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo1_supplies),
	.consumer_supplies = d2153_ldo1_supplies,
};

// LDO2
__weak struct regulator_consumer_supply d2153_ldo2_supplies[] = {
	REGULATOR_SUPPLY("vdd_mhl", NULL),	// VDDR
	REGULATOR_SUPPLY("vlcd_1v2", NULL),	// VLCD_1V2
	REGULATOR_SUPPLY("vcam_sense_1v5", NULL),/*VCAM_SENSE_1V5 for amethyst*/
	REGULATOR_SUPPLY("cam_sensor_core_1v2", NULL),
	/*CAM_SENSOR_CORE_1V2 for vivaltoLTE*/
};

static struct regulator_init_data d2153_ldo2 = {
	.constraints = {
		.min_uV = D2153_LDO2_VOLT_LOWER,
		.max_uV = D2153_LDO2_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo2_supplies),
	.consumer_supplies = d2153_ldo2_supplies,
};

// LDO3
__weak struct regulator_consumer_supply d2153_ldo3_supplies[] = {
	REGULATOR_SUPPLY("vmmc", NULL),	// VMMC
};

static struct regulator_init_data d2153_ldo3 = {
	.constraints = {
		.min_uV = D2153_LDO3_VOLT_UPPER,
		.max_uV = D2153_LDO3_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo3_supplies),
	.consumer_supplies = d2153_ldo3_supplies,
};

// LDO4
__weak struct regulator_consumer_supply d2153_ldo4_supplies[] = {
	REGULATOR_SUPPLY("vregtcxo", NULL),	// VVCTCXO
};

static struct regulator_init_data d2153_ldo4 = {
	.constraints = {
		.min_uV = D2153_LDO4_VOLT_LOWER,
		.max_uV = D2153_LDO4_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo4_supplies),
	.consumer_supplies = d2153_ldo4_supplies,
};

// LDO5
__weak struct regulator_consumer_supply d2153_ldo5_supplies[] = {
	REGULATOR_SUPPLY("vmipi", NULL),	// VMIPI
};

static struct regulator_init_data d2153_ldo5 = {
	.constraints = {
		.min_uV = D2153_LDO5_VOLT_LOWER,
		.max_uV = D2153_LDO5_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo5_supplies),
	.consumer_supplies = d2153_ldo5_supplies,
};

// LDO6
__weak struct regulator_consumer_supply d2153_ldo6_supplies[] = {
	REGULATOR_SUPPLY("vusim1", NULL),	// VUSIM1
};

static struct regulator_init_data d2153_ldo6 = {
	.constraints = {
		.min_uV = D2153_LDO6_VOLT_LOWER,
		.max_uV = D2153_LDO6_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo6_supplies),
	.consumer_supplies = d2153_ldo6_supplies,
};

// LDO7
__weak struct regulator_consumer_supply d2153_ldo7_supplies[] = {
	REGULATOR_SUPPLY("sensor_3v", NULL),	// SENSOR
};

static struct regulator_init_data d2153_ldo7 = {
	.constraints = {
		.min_uV = D2153_LDO7_VOLT_LOWER,
		.max_uV = D2153_LDO7_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo7_supplies),
	.consumer_supplies = d2153_ldo7_supplies,
};

// LDO8
__weak struct regulator_consumer_supply d2153_ldo8_supplies[] = {
	REGULATOR_SUPPLY("vlcd_3v", NULL),	// VLCD_3V0
};

static struct regulator_init_data d2153_ldo8 = {
	.constraints = {
		.min_uV = D2153_LDO8_VOLT_LOWER,
		.max_uV = D2153_LDO8_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo8_supplies),
	.consumer_supplies = d2153_ldo8_supplies,
};

// LDO9
__weak struct regulator_consumer_supply d2153_ldo9_supplies[] = {
	REGULATOR_SUPPLY("vlcd_1v8", NULL),	// VLDO_1V8
	REGULATOR_SUPPLY("vtsp_1v8", NULL),     // vtsp_1v8
};

static struct regulator_init_data d2153_ldo9 = {
	.constraints = {
		.min_uV = D2153_LDO9_VOLT_LOWER,
		.max_uV = D2153_LDO9_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo9_supplies),
	.consumer_supplies = d2153_ldo9_supplies,
};

// LDO10
__weak struct regulator_consumer_supply d2153_ldo10_supplies[] = {
	REGULATOR_SUPPLY("vio_sd", NULL),	// VIO_SD
};

static struct regulator_init_data d2153_ldo10 = {
	.constraints = {
		.min_uV = D2153_LDO10_VOLT_LOWER,
		.max_uV = D2153_LDO10_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.boot_on = 0,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo10_supplies),
	.consumer_supplies = d2153_ldo10_supplies,
};

// LDO11
__weak struct regulator_consumer_supply d2153_ldo11_supplies[] = {
	REGULATOR_SUPPLY("key_led", NULL),	// key led
	REGULATOR_SUPPLY("vled", "leds-regulator.0"),	// key led
};

static struct regulator_init_data d2153_ldo11 = {
	.constraints = {
		.min_uV = D2153_LDO11_VOLT_LOWER,
		.max_uV = D2153_LDO11_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo11_supplies),
	.consumer_supplies = d2153_ldo11_supplies,
};

// LDO12
__weak struct regulator_consumer_supply d2153_ldo12_supplies[] = {
	REGULATOR_SUPPLY("cam_sensor_a", NULL),	// cam_sensor_a
};

static struct regulator_init_data d2153_ldo12 = {
	.constraints = {
		.min_uV = D2153_LDO12_VOLT_LOWER,
		.max_uV = D2153_LDO12_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo12_supplies),
	.consumer_supplies = d2153_ldo12_supplies,
};

// LDO13
__weak struct regulator_consumer_supply d2153_ldo13_supplies[] = {
	REGULATOR_SUPPLY("cam_af", NULL),	// cam_af
};

static struct regulator_init_data d2153_ldo13 = {
	.constraints = {
		.min_uV = D2153_LDO13_VOLT_LOWER,
		.max_uV = D2153_LDO13_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo13_supplies),
	.consumer_supplies = d2153_ldo13_supplies,
};
// LDO14
__weak struct regulator_consumer_supply d2153_ldo14_supplies[] = {
	REGULATOR_SUPPLY("vt_cam", NULL),	 /* vt_cam */
	REGULATOR_SUPPLY("sensor_led_3v", NULL), /* sensor_led_3v */
	REGULATOR_SUPPLY("vusim2", NULL), /* vusim2 for amethyst */
};

static struct regulator_init_data d2153_ldo14 = {
	.constraints = {
		.min_uV = D2153_LDO14_VOLT_LOWER,
		.max_uV = D2153_LDO14_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo14_supplies),
	.consumer_supplies = d2153_ldo14_supplies,
};

// LDO15
__weak struct regulator_consumer_supply d2153_ldo15_supplies[] = {
	REGULATOR_SUPPLY("vsd", NULL),	// VSD
};

static struct regulator_init_data d2153_ldo15 = {
	.constraints = {
		.min_uV = D2153_LDO15_VOLT_LOWER,
		.max_uV = D2153_LDO15_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.boot_on = 0,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo15_supplies),
	.consumer_supplies = d2153_ldo15_supplies,
};

// LDO16
__weak struct regulator_consumer_supply d2153_ldo16_supplies[] = {
	REGULATOR_SUPPLY("vdd_motor_pmic", NULL),	// Motor
	REGULATOR_SUPPLY("vdd_auxi_pmic", NULL),	// GPS
};

static struct regulator_init_data d2153_ldo16 = {
	.constraints = {
		.min_uV = D2153_LDO16_VOLT_LOWER,
		.max_uV = D2153_LDO16_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo16_supplies),
	.consumer_supplies = d2153_ldo16_supplies,
};

// LDO17
__weak struct regulator_consumer_supply d2153_ldo17_supplies[] = {
	REGULATOR_SUPPLY("cam_sensor_io", NULL),	// cam_sensor_io
};

static struct regulator_init_data d2153_ldo17 = {
	.constraints = {
		.min_uV = D2153_LDO17_VOLT_LOWER,
		.max_uV = D2153_LDO17_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo17_supplies),
	.consumer_supplies = d2153_ldo17_supplies,
};

// LDO18
__weak struct regulator_consumer_supply d2153_ldo18_supplies[] = {
	REGULATOR_SUPPLY("vrf_ana_high", NULL),	// vrf_ana_high
};

static struct regulator_init_data d2153_ldo18 = {
	.constraints = {
		.min_uV = D2153_LDO18_VOLT_LOWER,
		.max_uV = D2153_LDO18_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo18_supplies),
	.consumer_supplies = d2153_ldo18_supplies,
};

// LDO19
__weak struct regulator_consumer_supply d2153_ldo19_supplies[] = {
	REGULATOR_SUPPLY("vtsp_3v", NULL),	// vtsp_3v
};

static struct regulator_init_data d2153_ldo19 = {
	.constraints = {
		.min_uV = D2153_LDO19_VOLT_LOWER,
		.max_uV = D2153_LDO19_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo19_supplies),
	.consumer_supplies = d2153_ldo19_supplies,
};

// LDO20
__weak struct regulator_consumer_supply d2153_ldo20_supplies[] = {
	REGULATOR_SUPPLY("sensor_1v8", NULL),	// sensor_1v8
};

static struct regulator_init_data d2153_ldo20 = {
	.constraints = {
		.min_uV = D2153_LDO20_VOLT_LOWER,
		.max_uV = D2153_LDO20_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldo20_supplies),
	.consumer_supplies = d2153_ldo20_supplies,
};

// LDO_AUD_1
__weak struct regulator_consumer_supply d2153_ldoaud1_supplies[] = {
	REGULATOR_SUPPLY("aud1", NULL),	// aud1
};

static struct regulator_init_data d2153_ldoaud1 = {
	.constraints = {
		.min_uV = D2153_LDOAUD1_VOLT_LOWER,
		.max_uV = D2153_LDOAUD1_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldoaud1_supplies),
	.consumer_supplies = d2153_ldoaud1_supplies,
};

// LDO_AUD_2
__weak struct regulator_consumer_supply d2153_ldoaud2_supplies[] = {
	REGULATOR_SUPPLY("aud2", NULL),	// aud2
};

static struct regulator_init_data d2153_ldoaud2 = {
	.constraints = {
		.min_uV = D2153_LDOAUD2_VOLT_LOWER,
		.max_uV = D2153_LDOAUD2_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2153_ldoaud2_supplies),
	.consumer_supplies = d2153_ldoaud2_supplies,
};


struct d2153_regl_init_data
	d2153_regulators_init_data[D2153_NUMBER_OF_REGULATORS] = {
	[D2153_BUCK_1] = { D2153_BUCK_1,  &d2153_buck1 },
	[D2153_BUCK_2] = { D2153_BUCK_2,  &d2153_buck2 },
	[D2153_BUCK_3] = { D2153_BUCK_3,  &d2153_buck3 },
	[D2153_BUCK_4] = { D2153_BUCK_4,  &d2153_buck4 },
	[D2153_BUCK_5] = { D2153_BUCK_5,  &d2153_buck5 },
	[D2153_BUCK_6] = { D2153_BUCK_6,  &d2153_buck6 },

	[D2153_LDO_1]  = { D2153_LDO_1, &d2153_ldo1 },
	[D2153_LDO_2]  = { D2153_LDO_2, &d2153_ldo2 },
	[D2153_LDO_3]  = { D2153_LDO_3, &d2153_ldo3 },
	[D2153_LDO_4]  = { D2153_LDO_4, &d2153_ldo4 },
	[D2153_LDO_5]  = { D2153_LDO_5, &d2153_ldo5 },
	[D2153_LDO_6]  = { D2153_LDO_6, &d2153_ldo6 },
	[D2153_LDO_7]  = { D2153_LDO_7, &d2153_ldo7 },
	[D2153_LDO_8]  = { D2153_LDO_8, &d2153_ldo8 },
	[D2153_LDO_9]  = { D2153_LDO_9, &d2153_ldo9 },
	[D2153_LDO_10] = { D2153_LDO_10, &d2153_ldo10 },
	[D2153_LDO_11] = { D2153_LDO_11, &d2153_ldo11 },
	[D2153_LDO_12] = { D2153_LDO_12, &d2153_ldo12 },
	[D2153_LDO_13] = { D2153_LDO_13, &d2153_ldo13 },
	[D2153_LDO_14] = { D2153_LDO_14, &d2153_ldo14 },
	[D2153_LDO_15] = { D2153_LDO_15, &d2153_ldo15 },
	[D2153_LDO_16] = { D2153_LDO_16, &d2153_ldo16 },
	[D2153_LDO_17] = { D2153_LDO_17, &d2153_ldo17 },
	[D2153_LDO_18] = { D2153_LDO_18, &d2153_ldo18 },
	[D2153_LDO_19] = { D2153_LDO_19, &d2153_ldo19 },
	[D2153_LDO_20] = { D2153_LDO_20, &d2153_ldo20 },

	[D2153_LDO_AUD1] = { D2153_LDO_AUD1, &d2153_ldoaud1 },
	[D2153_LDO_AUD2] = { D2153_LDO_AUD2, &d2153_ldoaud2 },
};
