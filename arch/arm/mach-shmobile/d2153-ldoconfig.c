/*
 * arch/arm/mach-shmobile/d2153-ldoconfig.c
 *
 * LDO configurations (init values and consumer names) for EOS.
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

#include <linux/kernel.h>
#include <linux/regulator/machine.h>
#include <linux/d2153/core.h>
#include <linux/d2153/pmic.h>

struct regulator_consumer_supply d2153_buck1_supplies[] = {
	REGULATOR_SUPPLY("vcore", NULL),
};

struct regulator_consumer_supply d2153_buck2_supplies[] = {
	REGULATOR_SUPPLY("vio2", NULL),
};

struct regulator_consumer_supply d2153_buck3_supplies[] = {
	REGULATOR_SUPPLY("vio1", NULL),
};

struct regulator_consumer_supply d2153_buck4_supplies[] = {
	REGULATOR_SUPPLY("vcore_rf", NULL),
};

struct regulator_consumer_supply d2153_buck5_supplies[] = {
	REGULATOR_SUPPLY("vana1_rf", NULL),
};

struct regulator_consumer_supply d2153_buck6_supplies[] = {
	REGULATOR_SUPPLY("vpam", NULL),
};

struct regulator_consumer_supply d2153_ldo1_supplies[] = {
	REGULATOR_SUPPLY("vdig_rf", NULL),	/* VDIG_RF */
};

struct regulator_consumer_supply d2153_ldo2_supplies[] = {
	REGULATOR_SUPPLY("vdd_mhl", NULL),	/* VDDR */
	REGULATOR_SUPPLY("vlcd_1v2", NULL),	/* VLCD_1V2 */
	/* VCAM_SENSE_1V5 for amethyst */
	REGULATOR_SUPPLY("vcam_sense_1v5", NULL),
	/* CAM_SENSOR_CORE_1V2 for vivaltoLTE */
	REGULATOR_SUPPLY("cam_sensor_core_1v2", NULL),
};

struct regulator_consumer_supply d2153_ldo3_supplies[] = {
	REGULATOR_SUPPLY("vmmc", NULL),	/* VMMC */
};

struct regulator_consumer_supply d2153_ldo4_supplies[] = {
	REGULATOR_SUPPLY("vregtcxo", NULL),	/* VVCTCXO */
};

struct regulator_consumer_supply d2153_ldo5_supplies[] = {
	REGULATOR_SUPPLY("vmipi", NULL),	/* VMIPI */
};

struct regulator_consumer_supply d2153_ldo6_supplies[] = {
	REGULATOR_SUPPLY("vusim1", NULL),	/* VUSIM1 */
};

struct regulator_consumer_supply d2153_ldo7_supplies[] = {
	REGULATOR_SUPPLY("sensor_3v", NULL),	/* SENSOR */
};

struct regulator_consumer_supply d2153_ldo8_supplies[] = {
	REGULATOR_SUPPLY("vlcd_3v", NULL),	/* VLCD_3V0 */
};

struct regulator_consumer_supply d2153_ldo9_supplies[] = {
	REGULATOR_SUPPLY("vlcd_1v8", NULL),	/* VLDO_1V8 */
	REGULATOR_SUPPLY("vtsp_1v8", NULL), /* vtsp_1v8 */
};

struct regulator_consumer_supply d2153_ldo10_supplies[] = {
	REGULATOR_SUPPLY("vio_sd", NULL),	/* VIO_SD */
};

struct regulator_consumer_supply d2153_ldo11_supplies[] = {
	REGULATOR_SUPPLY("key_led", NULL),	/* key led */
	REGULATOR_SUPPLY("vled", "leds-regulator.0"),	/* key led */
};

struct regulator_consumer_supply d2153_ldo12_supplies[] = {
	REGULATOR_SUPPLY("cam_sensor_a", NULL),	/* cam_sensor_a */
};

struct regulator_consumer_supply d2153_ldo13_supplies[] = {
	REGULATOR_SUPPLY("cam_af", NULL),	/* cam_af */
};

struct regulator_consumer_supply d2153_ldo14_supplies[] = {
	REGULATOR_SUPPLY("vt_cam", NULL),	 /* vt_cam */
	REGULATOR_SUPPLY("sensor_led_3v", NULL), /* sensor_led_3v */
	REGULATOR_SUPPLY("vusim2", NULL), /* vusim2 for amethyst */
};

struct regulator_consumer_supply d2153_ldo15_supplies[] = {
	REGULATOR_SUPPLY("vsd", NULL),	/* VSD */
};

struct regulator_consumer_supply d2153_ldo16_supplies[] = {
	REGULATOR_SUPPLY("vdd_motor_pmic", NULL),	/* Motor */
	REGULATOR_SUPPLY("vdd_auxi_pmic", NULL),	/* GPS */
};

struct regulator_consumer_supply d2153_ldo17_supplies[] = {
	REGULATOR_SUPPLY("cam_sensor_io", NULL),	/* cam_sensor_io */
};

struct regulator_consumer_supply d2153_ldo18_supplies[] = {
	REGULATOR_SUPPLY("vrf_ana_high", NULL),	/* vrf_ana_high */
};

struct regulator_consumer_supply d2153_ldo19_supplies[] = {
	REGULATOR_SUPPLY("vtsp_3v", NULL),	/* vtsp_3v */
};

struct regulator_consumer_supply d2153_ldo20_supplies[] = {
	REGULATOR_SUPPLY("sensor_1v8", NULL),	/* sensor_1v8 */
};

struct regulator_consumer_supply d2153_ldoaud1_supplies[] = {
	REGULATOR_SUPPLY("aud1", NULL),	/* aud1 */
};

struct regulator_consumer_supply d2153_ldoaud2_supplies[] = {
	REGULATOR_SUPPLY("aud2", NULL),	/* aud2 */
};

/*
 * Define initial MCTL value of EOS2 with D2153
 *
 * [ LDO ]	0x0 : Off [ BUCK 2,3,4]	0x0: Off
 *		0x1 : On		0x1: On
 *		0x2 : Sleep - LPM	0x2: Sleep(Force PFM mode) - LPM
 *		0x3 : n/a		0x3: n/a
 *
 * [ BUCK 1 ]	0x0 : Off
 *		0x1 : On	(reference VBUCK1     reg[0x002E])
 *		0x2 : Sleep	(reference VBUCK1_RET reg[0x0061])
 *		0x3 : On	(reference VBUCK1_TUR reg[0x0062])
 *
 * |-----------+----+----+----+----+----------------------|
 * | [PC1|PC2] | 11 | 10 | 01 | 00 | Comments             |
 * |-----------+----+----+----+----+----------------------|
 * |    [MCTL] | M3 | M2 | M1 | M0 |                      |
 * |-----------+----+----+----+----+----------------------|
 * |      0xDE | 11 | 01 | 11 | 10 | (TUR, ON, TUR, LPM)  |
 * |      0xCD | 11 | 00 | 11 | 01 | (TUR, OFF, TUR, ON)  |
 * |      0x00 | 00 | 00 | 00 | 00 | (OFF, OFF, OFF, OFF) |
 * |      0x66 | 01 | 10 | 01 | 10 | (ON , LPM, ON , LPM) |
 * |      0x44 | 01 | 00 | 01 | 00 | (ON , OFF, ON , OFF) |
 * |-----------+----+----+----+----+----------------------|
 *
 * NEVIS use M3 and M0
 */
struct d2153_regl_map regl_map[D2153_NUMBER_OF_REGULATORS] = {
	/* VCORE_1.125 */
	D2153_MCTL_MODE_INIT(D2153_BUCK_1, 0x56, D2153_REGULATOR_LPM_IN_DSM),
	/* VIO2_1.225V */
	D2153_MCTL_MODE_INIT(D2153_BUCK_2, 0x56, D2153_REGULATOR_LPM_IN_DSM),
	/* VIO1_1.825V */
	D2153_MCTL_MODE_INIT(D2153_BUCK_3, 0x56, D2153_REGULATOR_LPM_IN_DSM),
	/* VCORE_RF_1.25V. */
	D2153_MCTL_MODE_INIT(D2153_BUCK_4, 0x56, D2153_REGULATOR_LPM_IN_DSM),
	/* VANA1_RF_1.525V - TSR */
	D2153_MCTL_MODE_INIT(D2153_BUCK_5, 0x00, D2153_REGULATOR_MAX),
	/* VPAM_3.3V - used. */
	D2153_MCTL_MODE_INIT(D2153_BUCK_6, 0x54, D2153_REGULATOR_OFF_IN_DSM),
	/* VDIG_RF_1.1 */
	D2153_MCTL_MODE_INIT(D2153_LDO_1,  0x56, D2153_REGULATOR_LPM_IN_DSM),
	/* VDD_MHL_1.2V */
	D2153_MCTL_MODE_INIT(D2153_LDO_2,  0x00, D2153_REGULATOR_OFF_IN_DSM),
	/* VMMC_2.85V */
	D2153_MCTL_MODE_INIT(D2153_LDO_3,  0x55, D2153_REGULATOR_ON_IN_DSM),
	/* VREG_TCXO_1.8V */
	D2153_MCTL_MODE_INIT(D2153_LDO_4,  0x54, D2153_REGULATOR_OFF_IN_DSM),
	/* VMIPI_1.8V */
	D2153_MCTL_MODE_INIT(D2153_LDO_5,  0x56, D2153_REGULATOR_LPM_IN_DSM),
	/* VUSIM1_1.8V */
	D2153_MCTL_MODE_INIT(D2153_LDO_6,  0x00, D2153_REGULATOR_LPM_IN_DSM),
	/* SENSOR_3V */
	D2153_MCTL_MODE_INIT(D2153_LDO_7,  0x55, D2153_REGULATOR_ON_IN_DSM),
	/* VLCD_3.0V */
	D2153_MCTL_MODE_INIT(D2153_LDO_8,  0x54, D2153_REGULATOR_OFF_IN_DSM),
	/* VLCD_1.8V */
	D2153_MCTL_MODE_INIT(D2153_LDO_9,  0x54, D2153_REGULATOR_OFF_IN_DSM),
	/* VIO_SD_2.85V */
	D2153_MCTL_MODE_INIT(D2153_LDO_10, 0x00, D2153_REGULATOR_OFF_IN_DSM),
	/* KEY_LED_3.3V */
	D2153_MCTL_MODE_INIT(D2153_LDO_11, 0x00, D2153_REGULATOR_OFF_IN_DSM),
	/* CAM_SENSOR_A2.8V */
	D2153_MCTL_MODE_INIT(D2153_LDO_12, 0x00, D2153_REGULATOR_OFF_IN_DSM),
	/* CAM_AF_2V8*/
	D2153_MCTL_MODE_INIT(D2153_LDO_13, 0x00, D2153_REGULATOR_OFF_IN_DSM),
	/* VT_CAM_1.2V/SENSOR_LED_3V */
	D2153_MCTL_MODE_INIT(D2153_LDO_14, 0x55, D2153_REGULATOR_ON_IN_DSM),
	/* VSD_2.85V */
	D2153_MCTL_MODE_INIT(D2153_LDO_15, 0x00, D2153_REGULATOR_OFF_IN_DSM),
	/* VDD_MOTOR_3V */
	D2153_MCTL_MODE_INIT(D2153_LDO_16, 0x00, D2153_REGULATOR_OFF_IN_DSM),
	/* CAM_SENSOR_IO_1.8V */
	D2153_MCTL_MODE_INIT(D2153_LDO_17, 0x00, D2153_REGULATOR_OFF_IN_DSM),
	/* VRF_ANA_HIGH_2.8V */
	D2153_MCTL_MODE_INIT(D2153_LDO_18, 0x00, D2153_REGULATOR_MAX),
	/* VTSP_A3.0V */
	D2153_MCTL_MODE_INIT(D2153_LDO_19, 0x00, D2153_REGULATOR_OFF_IN_DSM),
	/* SENSOR_1.8V */
	D2153_MCTL_MODE_INIT(D2153_LDO_20, 0x55, D2153_REGULATOR_ON_IN_DSM),
	/* LDO_AUD1 1.8 */
	D2153_MCTL_MODE_INIT(D2153_LDO_AUD1, 0x56, D2153_REGULATOR_LPM_IN_DSM),
	/* LDO_AUD2 3.3 */
	D2153_MCTL_MODE_INIT(D2153_LDO_AUD2, 0x56, D2153_REGULATOR_LPM_IN_DSM),
};
