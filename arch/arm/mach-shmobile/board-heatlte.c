/******************************************************************************/
/*                                                                            */
/* Copyright 2013 Broadcom Corporation                                        */
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
#if defined(CONFIG_NFC_BCM2079X)
#include <linux/nfc/bcm2079x.h>
#endif
#if defined(CONFIG_PN547_NFC) || defined(CONFIG_NFC_PN547)
#include <linux/nfc/pn547.h>
#endif
#if defined(CONFIG_SAMSUNG_SENSOR)
#include <mach/dev-sensor.h>
#endif
#if  defined(CONFIG_PN547_NFC)  || \
	defined(CONFIG_NFC_PN547)
#include <mach/dev-nfc.h>
#endif

#include <mach/dev-bt.h>

#include <mach/setup-u2stm.h>

#include "sh-pfc.h"
#include <linux/bcm.h>

void (*shmobile_arch_reset)(char mode, const char *cmd);

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
#if defined(CONFIG_NFC_BCM2079X)

static int bcm2079x_gpio_setup(void *);
static int bcm2079x_gpio_clear(void *);
static struct bcm2079x_i2c_platform_data bcm2079x_pdata = {
	.irq_gpio = 13,
	.en_gpio = 12,
	.wake_gpio = 14,
	.init = bcm2079x_gpio_setup,
	.reset = bcm2079x_gpio_clear,
};

static int bcm2079x_gpio_setup(void *this)
{

	struct bcm2079x_i2c_platform_data *p;
	p = (struct bcm2079x_i2c_platform_data *) this;
	if (!p)
		return -1;
	pr_info("bcm2079x_gpio_setup nfc en %d, wake %d, irq %d\n",
		p->en_gpio, p->wake_gpio, p->irq_gpio);

	gpio_request(p->irq_gpio, "nfc_irq");
	gpio_direction_input(p->irq_gpio);

	gpio_request(p->en_gpio, "nfc_en");
	gpio_direction_output(p->en_gpio, 1);

	gpio_request(p->wake_gpio, "nfc_wake");
	gpio_direction_output(p->wake_gpio, 0);
	gpio_pull_up_port(p->wake_gpio);

	return 0;
}
static int bcm2079x_gpio_clear(void *this)
{

	struct bcm2079x_i2c_platform_data *p;
	p = (struct bcm2079x_i2c_platform_data *) this;
	if (!p)
		return -1;

	pr_info("bcm2079x_gpio_clear nfc en %d, wake %d, irq %d\n",
		p->en_gpio, p->wake_gpio, p->irq_gpio);

	gpio_direction_output(p->en_gpio, 0);
	gpio_direction_output(p->wake_gpio, 1);
	gpio_free(p->en_gpio);
	gpio_free(p->wake_gpio);
	gpio_free(p->irq_gpio);

	return 0;
}

static struct i2c_board_info __initdata bcm2079x[] = {
	{
	 I2C_BOARD_INFO("bcm2079x", 0x77),
	 .platform_data = (void *)&bcm2079x_pdata,
	 //.irq = gpio_to_irq(13),
	 },

};
#endif


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

void board_restart(char mode, const char *cmd)
{
	printk(KERN_INFO "%s\n", __func__);
	shmobile_do_restart(mode, cmd, APE_RESETLOG_U2EVM_RESTART);
}

static unsigned long pin_pullup_conf[] __maybe_unused = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_UP, 1),
};

static struct pinctrl_map heatlte_pinctrl_map[] __initdata = {
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
	struct device_node *np;
	bool vmode = true;

	np = of_find_node_by_path("/lcdc@fe940000");
	if (np) {
		if (of_device_is_available(np)) {
			if (of_property_match_string(np,
					"pname", "S6E88A0"))
				vmode = false;
		}
		of_node_put(np);
	}

	r8a7373_zq_calibration();

	r8a7373_avoid_a2slpowerdown_afterL2sync();
	sh_pfc_register_mappings(heatlte_pinctrl_map,
				 ARRAY_SIZE(heatlte_pinctrl_map));
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

	/* Oled Display, Touch */
	gpio_request(GPIO_PORT24, NULL);
	gpio_direction_input(GPIO_PORT24);
	gpio_pull_up_port(GPIO_PORT24);

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

if (system_rev <= BOARD_REV_010)
	add_primary_cam_flash_mic2871(GPIO_PORT99, GPIO_PORT100);
else
	add_primary_cam_flash_rt8547(GPIO_PORT99, GPIO_PORT100);


	add_s5k4ecgx_primary_camera();

if (system_rev <= BOARD_REV_000)
	add_sr030pc50_secondary_camera();
else if (system_rev < BOARD_REV_050)
	add_db8v61m_secondary_camera();
else
	add_sr130pc20_secondary_camera();


	camera_init(GPIO_PORT3, GPIO_PORT20, GPIO_PORT45,
		CSI_1_Lane, CSI_2_Lane);

	u2_add_ion_device();
	u2_add_rcu_devices();
	add_bcmbt_lpm_device(GPIO_PORT262, GPIO_PORT272);

	d2153_init_board_defaults();
	i2c_register_board_info(0, i2c0_devices_d2153,
					ARRAY_SIZE(i2c0_devices_d2153));

#if defined(CONFIG_SAMSUNG_SENSOR)
	board_sensor_init();
#endif

	i2c_register_board_info(3, i2c3_devices, ARRAY_SIZE(i2c3_devices));

	gps_gpio_init();

#if defined(CONFIG_NFC_BCM2079X)
	i2c_register_board_info(7, bcm2079x, ARRAY_SIZE(bcm2079x));
#endif
#ifdef CONFIG_SEC_CHARGING_FEATURE
	/* PA devices init */
	spa_init();
#endif
	bcm_init();
#ifdef CONFIG_VIBRATOR_SS
	ss_vibrator_data.voltage = 2800000;
#endif
	u2_vibrator_init();

	printk(KERN_DEBUG "%s\n", __func__);

#if defined(CONFIG_PN547_NFC) || defined(CONFIG_NFC_PN547)
	pn547_device_i2c_register();
#endif
}

static const char *heat_compat_dt[] __initdata = {
	"renesas,heatlte",
	NULL,
};

DT_MACHINE_START(HEATLTE, "heatlte")
	.smp		= smp_ops(r8a7373_smp_ops),
	.map_io		= r8a7373_map_io,
	.init_irq       = r8a7373_init_irq,
	.init_early     = r8a7373_init_early,
	.init_machine   = board_init,
	.init_time	= r8a7373_timers_init,
	.init_late      = r8a7373_init_late,
	.restart        = board_restart,
	.reserve        = r8a7373_reserve,
	.dt_compat	= heat_compat_dt,
MACHINE_END
