/*
 * arch/arm/mach-shmobile/board-amethyst-spa.c
 *
 * Device initialization for spa_power and spa_agent.c
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

#include <mach/setup-u2spa.h>
#include <linux/wakelock.h>
#include <mach/r8a7373.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>

#if defined(CONFIG_SEC_CHARGING_FEATURE)
#include <linux/spa_power.h>
#include <linux/spa_agent.h>

/* Samsung charging feature
 +++ for board files, it may contain changeable values */

/* Based on ERTJ0EG103FA NTC's R-T curve data*/
/* TODO: These values are to be used in d2153_battery.c also */
static struct spa_temp_tb batt_temp_tb[] = {
	{1486, -250},		/* -25 */
	{1153, -200},		/* -20 */
	{902, -150},		/* -15 */
	{712, -100},		/* -10 */
	{566, -50},		/* -5  */
	{453,  0},		/* 0   */
	{365,  50},		/* 5   */
	{296,  100},		/* 10  */
	{241,  150},		/* 15  */
	{198,  200},		/* 20  */
	{164,  250},		/* 25  */
	{136,  300},		/* 30  */
	{114,  350},		/* 35  */
	{95,  400},		/* 40  */
	{81,  450},		/* 45  */
	{68,  500},		/* 50  */
	{58,  550},		/* 55  */
	{50,  600},		/* 60  */
	{43,  650},		/* 65  */
	{37,  700},		/* 70  */
};

/* Operating temp range taken from spec of
 * 1YSYS06F01 Li Polymer rechargeable battery pack*/
struct spa_power_data spa_power_pdata = {
	.charger_name = "spa_agent_chrg",
	.eoc_current = 180,
	.recharge_voltage = 4300,
	.charging_cur_usb = 500,
	.charging_cur_wall = 1200,
	.suspend_temp_hot = 450,
	.recovery_temp_hot = 400,
	.suspend_temp_cold = 0,
	.recovery_temp_cold = 50,
	.charge_timer_limit = CHARGE_TIMER_6HOUR,
#if defined(CONFIG_SPA_SUPPLEMENTARY_CHARGING)
	.backcharging_time = 30,
#endif
	.regulated_vol = 4350,
	.batt_temp_tb = &batt_temp_tb[0],
	.batt_temp_tb_len = ARRAY_SIZE(batt_temp_tb),
};


static struct platform_device spa_power_device = {
	.name = "spa_power",
	.id = -1,
	.dev.platform_data = &spa_power_pdata,
};

static enum spa_agent_feature bat_check_method =
					SPA_AGENT_GET_BATT_PRESENCE_PMIC;

static struct platform_device spa_agent_device = {
	.name = "spa_agent",
	.id = -1,
	.dev.platform_data = &bat_check_method,
};

static int spa_power_init(void)
{
	int ret;
	ret = platform_device_register(&spa_agent_device);
	if (ret < 0)
		return ret;
	ret = platform_device_register(&spa_power_device);
	if (ret < 0)
		return ret;

	return 0;
}
#endif

void spa_init(void)
{
#if defined(CONFIG_SEC_CHARGING_FEATURE)
	spa_power_init();
#endif
}
