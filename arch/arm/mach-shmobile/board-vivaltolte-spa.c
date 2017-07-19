/******************************************************************************/
/*                                                                            */
/* Copyright 2014 Broadcom Corporation                                        */
/*                                                                            */
/* Unless you and Broadcom execute a separate written software license        */
/* agreement governing use of this software, this software is licensed        */
/* to you under the terms of the GNU General Public License version 2         */
/* (the GPL), available at                                                    */
/*                                                                            */
/* http://www.broadcom.com/licenses/GPLv2.php                                 */
/*                                                                            */
/* with the following added to such license:                                  */
/*                                                                            */
/* As a special exception, the copyright holders of this software give        */
/* you permission to link this software with independent modules, and         */
/* to copy and distribute the resulting executable under terms of your        */
/* choice, provided that you also meet, for each linked independent           */
/* module, the terms and conditions of the license of that module.            */
/* An independent module is a module which is not derived from this           */
/* software. The special exception does not apply to any modifications        */
/* of the software.                                                           */
/*                                                                            */
/* Notwithstanding the above, under no circumstances may you combine          */
/* this software in any way with any other Broadcom software provided         */
/* under a license other than the GPL, without Broadcom's express             */
/* prior written consent.                                                     */
/*                                                                            */
/******************************************************************************/

#include <mach/setup-u2spa.h>
#include <linux/wakelock.h>
#include <mach/r8a7373.h>
#include <mach/gpio.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>

#if defined(CONFIG_SEC_CHARGING_FEATURE)
#include <linux/spa_power.h>
#include <linux/spa_agent.h>

/* Samsung charging feature
 +++ for board files, it may contain changeable values */
static struct spa_temp_tb batt_temp_tb[] = {
	{3000, -250},		/* -25 */
	{2350, -200},		/* -20 */
	{1850, -150},		/* -15 */
	{1480, -100},		/* -10 */
	{1180, -50},		/* -5  */
	{945,  0},			/* 0    */
	{765,  50},			/* 5    */
	{620,  100},		/* 10  */
	{510,  150},		/* 15  */
	{420,  200},		/* 20  */
	{345,  250},		/* 25  */
	{285,  300},		/* 30  */
	{240,  350},		/* 35  */
	{200,  400},		/* 40  */
	{170,  450},		/* 45  */
	{143,  500},		/* 50  */
	{122,  550},		/* 55  */
	{104,  600},		/* 60  */
	{89,  650},			/* 65  */
	{77,  700},			/* 70  */
};

struct spa_power_data spa_power_pdata = {
	.charger_name = "spa_agent_chrg",
	.eoc_current = 180,
	.recharge_voltage = 4300,
	.charging_cur_usb = 500,
	.charging_cur_wall = 1200,
	.suspend_temp_hot = 600,
	.recovery_temp_hot = 400,
	.suspend_temp_cold = -50,
	.recovery_temp_cold = 0,
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
					SPA_AGENT_GET_BATT_PRESENCE_CHARGER;

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
