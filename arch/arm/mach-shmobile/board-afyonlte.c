/*
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
#include <linux/dma-mapping.h>
#include <mach/irqs.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/hwspinlock.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <mach/common.h>
#include <mach/r8a7373.h>
#include <mach/gpio.h>
#include <mach/setup-u2usb.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <video/sh_mobile_lcdc.h>
#include <linux/irqchip/arm-gic.h>
#include <mach/poweroff.h>
#ifdef CONFIG_MFD_D2153
#include <linux/d2153/core.h>
#include <linux/d2153/d2153_aad.h>
#include <linux/d2153/pmic.h>
#endif
#include <mach/setup-u2spa.h>
#include <mach/setup-u2vibrator.h>
#include <mach/setup-u2ion.h>
#include <mach/setup-u2rcu.h>
#include <mach/setup-u2camera.h>
#include <mach/dev-gps.h>
#if defined(CONFIG_RENESAS_NFC)
#ifdef CONFIG_PN544_NFC
#include <mach/dev-renesas-nfc.h>
#endif
#endif
#if defined(CONFIG_SAMSUNG_MHL)
#include <mach/dev-edid.h>
#endif
#ifdef CONFIG_USB_OTG
#include <linux/usb/tusb1211.h>
#endif
#if defined(CONFIG_SND_SOC_SH4_FSI)
#include <mach/setup-u2audio.h>
#endif /* CONFIG_SND_SOC_SH4_FSI */
#if defined(CONFIG_PN547_NFC) || defined(CONFIG_NFC_PN547)
#include <linux/nfc/pn547.h>
#endif
#if defined(CONFIG_SAMSUNG_SENSOR)
#include <mach/dev-sensor.h>
#endif
#if defined(CONFIG_BCMI2CNFC) || defined(CONFIG_PN547_NFC)  || defined(CONFIG_NFC_PN547)
#include <mach/dev-nfc.h>
#endif

#include <mach/dev-bt.h>

#include <mach/setup-u2stm.h>
#include "sh-pfc.h"

#ifdef CONFIG_SEC_THERMISTOR
#include <mach/sec_thermistor.h>
#endif
#include <linux/bcm.h>

void (*shmobile_arch_reset)(char mode, const char *cmd);


#ifdef CONFIG_SEC_THERMISTOR
static struct sec_therm_platform_data sec_therm_pdata = {
        .polling_interval = 30 * 1000, /* msecs */
        .no_polling = 0,
};

struct platform_device sec_device_thermistor = {
        .name = "sec-thermistor",
        .id = -1,
        .dev.platform_data = &sec_therm_pdata,
};
#endif

/* D2153 setup */
#ifdef CONFIG_MFD_D2153
/*TODO: move these externs to header*/
extern struct d2153_regl_init_data
	      d2153_regulators_init_data[D2153_NUMBER_OF_REGULATORS];
extern struct d2153_regl_map regl_map[D2153_NUMBER_OF_REGULATORS];

struct d2153_platform_data d2153_pdata = {
	.hwmon_pdata = &d2153_adc_pdata,
	.pbat_platform  = &pbat_pdata,
	.prtc_platform	= &prtc_data,
	.regulator_data = d2153_regulators_init_data,
	.regl_map = regl_map,
};
#define LDO_CONSTRAINTS(__id)					\
	(d2153_pdata.regulator_data[__id].initdata->constraints)

#define SET_LDO_ALWAYS_ON(__id) (LDO_CONSTRAINTS(__id).always_on = 1)

#define SET_LDO_BOOT_ON(__id) (LDO_CONSTRAINTS(__id).boot_on = 1)

#define SET_LDO_APPLY_UV(__id) (LDO_CONSTRAINTS(__id).apply_uV = true)

static void __init d2153_init_board_defaults(void)
{
	SET_LDO_ALWAYS_ON(D2153_BUCK_1); /* VCORE */
	SET_LDO_ALWAYS_ON(D2153_BUCK_2); /* VIO2 */
	SET_LDO_ALWAYS_ON(D2153_BUCK_3); /* VIO1 */
	SET_LDO_ALWAYS_ON(D2153_BUCK_4); /* VCORE_RF */
	SET_LDO_ALWAYS_ON(D2153_BUCK_5); /* VANA1_RF */
	SET_LDO_ALWAYS_ON(D2153_BUCK_6); /* VPAM */

	/* VDIG_RF */
	SET_LDO_ALWAYS_ON(D2153_LDO_1);

	/* VMMC */
	SET_LDO_ALWAYS_ON(D2153_LDO_3);
	SET_LDO_APPLY_UV(D2153_LDO_3);

	/* VVCTCXO */
	SET_LDO_ALWAYS_ON(D2153_LDO_4);

	/* VMIPI */
	SET_LDO_ALWAYS_ON(D2153_LDO_5);

	/* VDD_MOTOR */
	SET_LDO_APPLY_UV(D2153_LDO_16);

	/* This is assumed with device tree, so always set it for consistency */
	regulator_has_full_constraints();
}

#endif

/* I2C */
static struct i2c_board_info __initdata i2c0_devices_d2153[] = {
#if defined(CONFIG_MFD_D2153)
	{
		/* for D2153 PMIC driver */
		I2C_BOARD_INFO("d2153", D2153_PMIC_I2C_ADDR),
		.platform_data = &d2153_pdata,
		.irq = irq_pin(28),
	},
#endif /* CONFIG_MFD_D2153 */
};

static void __init ldi_mdnie_init(void)
{
	platform_device_register_simple("mdnie", -1, NULL, 0);
}

static struct i2c_board_info __initdata i2c3_devices[] = {
	{
		I2C_BOARD_INFO("rt8973", 0x28>>1),
		.irq = irq_pin(41),
	},
	{
		I2C_BOARD_INFO("smb328a", (0x69 >> 1)),
		.irq            = irq_pin(19),
	},
};

#if 0
static struct led_regulator_platform_data key_backlight_data = {
	.name   = "button-backlight",
};

static struct platform_device key_backlight_device = {
	.name = "leds-regulator",
	.id   = 0,
	.dev  = {
		.platform_data = &key_backlight_data,
	},
};
#endif

void board_restart(char mode, const char *cmd)
{
	printk(KERN_INFO "%s\n", __func__);
	shmobile_do_restart(mode, cmd, APE_RESETLOG_U2EVM_RESTART);
}

static unsigned long pin_pullup_conf[] __maybe_unused = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_UP, 1),
};

static struct pinctrl_map afyonlte_pinctrl_map[] __initdata = {
	/* D2153 */
	SH_PFC_MUX_GROUP_DEFAULT("0-0049", "irqc_irq28", "irqc"),
	SH_PFC_CONFIGS_GROUP_DEFAULT("0-0049", "irqc_irq28", pin_pullup_conf),
	/* RT8973 */
	SH_PFC_MUX_GROUP_DEFAULT("3-0014", "irqc_irq41", "irqc"),
	SH_PFC_CONFIGS_GROUP_DEFAULT("3-0014", "irqc_irq41", pin_pullup_conf),
	/* SMB328A */
	SH_PFC_MUX_GROUP_DEFAULT("3-0034", "irqc_irq19", "irqc"),
	SH_PFC_CONFIGS_GROUP_DEFAULT("3-0034", "irqc_irq19", pin_pullup_conf),
};

static void __init board_init(void)
{
	r8a7373_zq_calibration();

	r8a7373_avoid_a2slpowerdown_afterL2sync();
	sh_pfc_register_mappings(afyonlte_pinctrl_map,
				 ARRAY_SIZE(afyonlte_pinctrl_map));
	gps_pinconf_init();
	r8a7373_pinmux_init();

	u2evm_init_stm_select();

	r8a7373_add_standard_devices();

	/* r8a7373_hwlock_gpio request has moved to pfc-r8a7373.c */
	r8a7373_hwlock_cpg = hwspin_lock_request_specific(SMCPG);
	r8a7373_hwlock_sysc = hwspin_lock_request_specific(SMSYSC);

	__raw_writew(0x0022, GPIO_DRVCR_SD0);
	__raw_writew(0x0022, GPIO_DRVCR_SIM1);
	__raw_writew(0x0022, GPIO_DRVCR_SIM2);

	shmobile_arch_reset = board_restart;

	/* ===== CWS GPIO ===== */

#if defined(CONFIG_RENESAS_NFC)
	nfc_gpio_init();
#endif

	/* ES2.0: SIM powers */
	__raw_writel(__raw_readl(MSEL3CR) | (1<<27), MSEL3CR);

	usb_init(true);

	d2153_pdata.audio.fm34_device = DEVICE_NONE;
	d2153_pdata.audio.aad_codec_detect_enable = false;
	d2153_pdata.audio.debounce_ms = D2153_AAD_JACK_DEBOUNCE_MS;
	u2audio_init(GPIO_PORT7);

#ifndef CONFIG_ARM_TZ
	r8a7373_l2cache_init();
#else
	/**
	 * In TZ-Mode of R-Mobile U2, it must notify the L2 cache
	 * related info to Secure World. However, SEC_HAL driver is not
	 * registered at the time of L2$ init because "r8a7373l2_cache_init()"
	 * function called more early.
	 */
	l2x0_init_later();
#endif

	add_primary_cam_flash_rt8547(GPIO_PORT99, GPIO_PORT100);
	add_s5k4ecgx_primary_camera();
	add_sr030pc50_secondary_camera();
	camera_init(GPIO_PORT3, GPIO_PORT20, GPIO_PORT45,
		CSI_1_Lane, CSI_2_Lane);

	u2_add_ion_device();
	u2_add_rcu_devices();
	add_bcmbt_lpm_device(GPIO_PORT262, GPIO_PORT272);

#ifdef CONFIG_SEC_THERMISTOR
        platform_device_register(&sec_device_thermistor);
#endif


	d2153_init_board_defaults();
	i2c_register_board_info(0, i2c0_devices_d2153,
					ARRAY_SIZE(i2c0_devices_d2153));

#if defined(CONFIG_SAMSUNG_SENSOR)
	board_sensor_init();
#endif

	i2c_register_board_info(3, i2c3_devices, ARRAY_SIZE(i2c3_devices));

	gps_gpio_init();
	/* PA devices init */
	spa_init();
	bcm_init();
#ifdef CONFIG_VIBRATOR_SS
	ss_vibrator_data.voltage = 2800000;
#endif
	u2_vibrator_init();

	printk(KERN_DEBUG "%s\n", __func__);

#if defined(CONFIG_PN547_NFC) || defined(CONFIG_NFC_PN547)
	pn547_device_i2c_register();
#endif

ldi_mdnie_init();

}

static const char *board_compat_dt[] __initdata = {
	"renesas,afyonlte",
	NULL,
};

DT_MACHINE_START(AFYONLTE, "afyonlte")
	.smp		= smp_ops(r8a7373_smp_ops),
	.map_io		= r8a7373_map_io,
	.init_irq       = r8a7373_init_irq,
	.init_early     = r8a7373_init_early,
	.init_machine   = board_init,
	.init_time	= r8a7373_timers_init,
	.init_late      = r8a7373_init_late,
	.restart        = board_restart,
	.reserve        = r8a7373_reserve,
	.dt_compat	= board_compat_dt,
	MACHINE_END
