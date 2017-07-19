/*
 * arch/arm/mach-shmobile/dev-touchpanel.c
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
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/i2c-gpio.h>
#include <linux/irq.h>
#include <mach/r8a7373.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/cyttsp4_bus.h>
#include <linux/cyttsp4_core.h>
#include <linux/cyttsp4_btn.h>
#include <linux/cyttsp4_mt.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/d2153/pmic.h>
#include <linux/regulator/consumer.h>
#include <mach/irqs.h>
#include <linux/input/mt.h>
#include "sh-pfc.h"
#include <mach/common.h>
#ifdef CONFIG_TOUCHSCREEN_SYNAPTIC_RMI4
#include <linux/rmi.h>
#endif
#ifdef CONFIG_TOUCHSCREEN_EKTF2K
#include <linux/ektf2k.h>
#endif
//++ add broadcom patch: tp driver by suwei 2014.4.25
#define CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4_INCLUDE_FW
#ifndef SAMSUNG_SYSINFO_DATA
#define SAMSUNG_SYSINFO_DATA
#endif
//-- add broadcom patch: tp driver by suwei 2014.4.25
#if defined(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4_INCLUDE_FW) \
&& defined(SAMSUNG_SYSINFO_DATA)
#include "mach/Afyon_cyttsp4_img_CY05A200_first.h"
#include "mach/Afyon_cyttsp4_img_CY056600_second.h"
#include <linux/cyttsp4_regs.h>

#include "mach/HEAT_LTE_HW02_FW03.h"
#include "mach/HEAT_LTE_HW01_FW02.h"

static struct cyttsp4_touch_firmware cyttsp4_firmware[] = {
	// CYTTSP4_SILICON_REV_D
	{
	.img = cyttsp4_img,
	.size = ARRAY_SIZE(cyttsp4_img),
		.hw_version = 0x05,
		.fw_version = 0xA200,
		.config_version = 0x65,
	},

	// CYTTSP4_SILICON_REV_B
	{
		.img = cyttsp4_img_revB,
		.size = ARRAY_SIZE(cyttsp4_img_revB),
		.hw_version = 0x05,
		.fw_version = 0x6600,
		.config_version = 0x65,
	},
	/*CYTTSP4_SILICON_REV_D*/
	{
		.img = cyttsp4_img_revD,
		.size = ARRAY_SIZE(cyttsp4_img_revD),
		.hw_version = 0x02,
		.fw_version = 0x0300,
		.config_version = 0x03,
	},

	/*CYTTSP4_SILICON_REV_B for Heat*/
	{
		.img = cyttsp4_img_revB_HEAT,
		.size = ARRAY_SIZE(cyttsp4_img_revB_HEAT),
		.hw_version = 0x01,
		/*.fw_version = 0x0200,
		.config_version = 0x02,*/
		.fw_version = 0x0100,
		.config_version = 0x01,
	},
	{
		.img = NULL,
	}
};

#else /*defined(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4_INCLUDE_FW) \
 && defined(SAMSUNG_SYSINFO_DATA)*/
static struct cyttsp4_touch_firmware cyttsp4_firmware = {
	.img = NULL,
	.size = 0,
	.ver = NULL,
	.vsize = 0,
};
#endif /*defined(CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4_INCLUDE_FW) && \
 defined(SAMSUNG_SYSINFO_DATA)*/

#define CYTTSP4_USE_I2C

#ifdef CYTTSP4_USE_I2C
#define CYTTSP4_I2C_NAME "cyttsp4_i2c_adapter"
#define CYTTSP4_IRQ_GROUP "irqc_irq32"
#endif

#define CY_MAXX 540
#define CY_MAXY 960
#define CY_MAXX_HEAT 480
#define CY_MAXY_HEAT 800

#define CY_MINX 0
#define CY_MINY 0

#define CY_ABS_MIN_X CY_MINX
#define CY_ABS_MIN_Y CY_MINY
#define CY_ABS_MAX_X CY_MAXX
#define CY_ABS_MAX_Y CY_MAXY
#define CY_ABS_MAX_X_HEAT CY_MAXX_HEAT
#define CY_ABS_MAX_Y_HEAT CY_MAXY_HEAT
#define CY_ABS_MIN_P 0
#define CY_ABS_MAX_P 255
#define CY_ABS_MIN_W 0
#define CY_ABS_MAX_W 255

#define CY_ABS_MIN_T 0

#define CY_ABS_MAX_T 15

#define CY_IGNORE_VALUE 0xFFFF

#define  NLSX4373_EN_GPIO    14

#define TSP_DEBUG_OPTION 0
static int __maybe_unused touch_debug_option = TSP_DEBUG_OPTION;

#define	CAPRI_TSP_DEBUG(fmt, args...) do { \
	if (touch_debug_option)	\
		pr_info("[TSP %s:%4d] " fmt, \
		__func__, __LINE__, ## args); \
	} while (0)

static unsigned int heat_gpio;
unsigned int int_gpio;
unsigned int cyttsp4_enable_gpio;
unsigned int cyttsp4_enable_gpio_rev00;

static int cyttsp4_hw_power(int on, int use_irq, int irq_gpio)
{
	int ret = 0;
	static struct regulator *reg_l31;

	pr_info("[TSP] power %s, system_rev=%d\n",
		on ? "on" : "off", system_rev);

	if (!reg_l31) {
		reg_l31 = regulator_get(NULL, "vtsp_3v");
		if (IS_ERR(reg_l31)) {
			pr_err("%s: could not get 8917_l31, rc = %ld\n",
				__func__, PTR_ERR(reg_l31));
			return -1;
		}
		ret = regulator_set_voltage(reg_l31, 2850000, 2850000);
		if (ret) {
			pr_err("%s: unable to set l31 voltage to 3.3V\n",
				__func__);
			return -1;
		}
	}

	if (on) {
		ret = regulator_enable(reg_l31);
		if (ret) {
			pr_err("%s: enable l31 failed, rc=%d\n",
				__func__, ret);
			return -1;
		}
		pr_info("%s: tsp 3.3V on is finished.\n", __func__);

		/* Delay for tsp chip is ready for i2c before enable irq */
		msleep(20);
		if (heat_gpio == 1) {
			gpio_direction_output(cyttsp4_enable_gpio, 1);
			gpio_direction_output(cyttsp4_enable_gpio_rev00, 1);
		}
		msleep(50);
	} else {

		if (heat_gpio == 1) {
			gpio_direction_output(cyttsp4_enable_gpio, 0);
			gpio_direction_output(cyttsp4_enable_gpio_rev00, 0);
		}
		/* Delay for 20 msec */
		msleep(20);

		if (regulator_is_enabled(reg_l31))
			ret = regulator_disable(reg_l31);
		else
			pr_info(
				"%s: rugulator is(L31(3.3V) disabled\n",
					__func__);
		if (ret) {
			pr_err("%s: disable l31 failed, rc=%d\n",
				__func__, ret);
			return -1;
		}
		pr_info("%s: tsp 3.3V off is finished.\n", __func__);


	/* Delay for 100 msec */
	msleep(100);
	}

	return 0;
}



static int cyttsp4_xres(struct cyttsp4_core_platform_data *pdata,
		struct device *dev)
{
	int irq_gpio = pdata->irq_gpio;
	int rc = 0;

	cyttsp4_hw_power(0, true, irq_gpio);

	cyttsp4_hw_power(1, true, irq_gpio);

	return rc;
}

static int cyttsp4_init(struct cyttsp4_core_platform_data *pdata,
		int on, struct device *dev)
{
	int rc = 0;

	if (on)
		cyttsp4_hw_power(1, false, 0);
	else
		cyttsp4_hw_power(0, false, 0);

	return rc;
}

static int cyttsp4_wakeup(struct cyttsp4_core_platform_data *pdata,
		struct device *dev, atomic_t *ignore_irq)
{
	int irq_gpio = pdata->irq_gpio;

	return cyttsp4_hw_power(1, true, irq_gpio);
}

static int cyttsp4_sleep(struct cyttsp4_core_platform_data *pdata,
		struct device *dev, atomic_t *ignore_irq)
{
	int irq_gpio = pdata->irq_gpio;

	return cyttsp4_hw_power(0, true, irq_gpio);
}

static int cyttsp4_power(struct cyttsp4_core_platform_data *pdata,
		int on, struct device *dev, atomic_t *ignore_irq)
{
	if (on)
		return cyttsp4_wakeup(pdata, dev, ignore_irq);

	return cyttsp4_sleep(pdata, dev, ignore_irq);
}

static int cyttsp4_irq_stat(struct cyttsp4_core_platform_data *pdata,
		struct device *dev)
{

	return gpio_get_value(pdata->irq_gpio);
}


static struct regulator *keyled_regulator;
static int touchkey_regulator_cnt;

static int cyttsp4_led_power_onoff(int on)
{
	int ret;

	if (keyled_regulator == NULL) {
		pr_info(" %s, %d\n", __func__, __LINE__);
		keyled_regulator = regulator_get(NULL, "key_led");
		if (IS_ERR(keyled_regulator)) {
			pr_err("can not get KEY_LED_3.3V\n");
			return -1;
		}
	ret = regulator_set_voltage(keyled_regulator, 3300000, 3300000);
	pr_info("regulator_set_voltage ret = %d\n", ret);

	}

	if (on) {
		pr_info("Touchkey On\n");
		ret = regulator_enable(keyled_regulator);
		if (ret) {
			pr_err("can not enable KEY_LED_3.3V, ret=%d\n", ret);
		} else {
			touchkey_regulator_cnt++;
			pr_info("regulator_enable ret = %d, cnt=%d\n",
				ret, touchkey_regulator_cnt);
		}

	} else {
		pr_info("Touchkey Off\n");
		ret = regulator_disable(keyled_regulator);
		if (ret) {
			pr_err("can not disabled KEY_LED_3.3V ret=%d\n", ret);
		} else {
			touchkey_regulator_cnt--;
			pr_info("regulator_disable ret= %d, cnt=%d\n",
				ret, touchkey_regulator_cnt);
		}

	}
	return 0;
}

#ifdef BUTTON_PALM_REJECTION
static u16 cyttsp4_btn_keys[] = {
	/* use this table to map buttons to keycodes (see input.h) */
	KEY_MENU,		/* 139 */
	KEY_MENU,		/* 139 */
	KEY_BACK,		/* 158 */
	KEY_BACK,		/* 158 */
};
#else
/* Button to keycode conversion */
static u16 cyttsp4_btn_keys[] = {
	/* use this table to map buttons to keycodes (see input.h) */
	KEY_MENU,		/* 139 */
	KEY_BACK,		/* 158 */
};
#endif	/* BUTTON_PALM_REJECTION */

static struct touch_settings cyttsp4_sett_btn_keys = {
	.data = (uint8_t *)&cyttsp4_btn_keys[0],
	.size = ARRAY_SIZE(cyttsp4_btn_keys),
	.tag = 0,
};

static struct cyttsp4_core_platform_data _cyttsp4_core_platform_data = {
/*	.irq_gpio = CYTTSP4_IRQ_GPIO,*/ /* GPIO 32. */
	/* .rst_gpio = CYTTSP4_RST_GPIO, */
	.xres = cyttsp4_xres,
	.init = cyttsp4_init,
	.power = cyttsp4_power,
	.irq_stat = cyttsp4_irq_stat,
	.led_power = cyttsp4_led_power_onoff,
	.sett = {
		NULL,	/* Reserved */
		NULL,	/* Command Registers */
		NULL,	/* Touch Report */
		NULL,	/* Cypress Data Record */
		NULL,	/* Test Record */
		NULL,	/* Panel Configuration Record */
		NULL, /* &cyttsp4_sett_param_regs, */
		NULL, /* &cyttsp4_sett_param_size, */
		NULL,	/* Reserved */
		NULL,	/* Reserved */
		NULL,	/* Operational Configuration Record */
		NULL, /* &cyttsp4_sett_ddata, *//* Design Data Record */
		NULL, /* &cyttsp4_sett_mdata, *//* Manufacturing Data Record */
		NULL,	/* Config and Test Registers */
		&cyttsp4_sett_btn_keys,	/* button-to-keycode table */
	},
	.fw = cyttsp4_firmware,
};

/*
static struct cyttsp4_core cyttsp4_core_device = {
	.name = CYTTSP4_CORE_NAME,
	.id = "main_ttsp_core",
	.adap_id = CYTTSP4_I2C_NAME,
	.dev = {
		.platform_data = &_cyttsp4_core_platform_data,
	},
};
*/

static struct cyttsp4_core_info cyttsp4_core_info __maybe_unused = {
	.name = CYTTSP4_CORE_NAME,
	.id = "main_ttsp_core",
	.adap_id = CYTTSP4_I2C_NAME,
	.platform_data = &_cyttsp4_core_platform_data,
};




static uint16_t cyttsp4_abs[] = {
	ABS_MT_POSITION_X, CY_ABS_MIN_X, CY_ABS_MAX_X, 0, 0,
	ABS_MT_POSITION_Y, CY_ABS_MIN_Y, CY_ABS_MAX_Y, 0, 0,
	ABS_MT_PRESSURE, CY_ABS_MIN_P, CY_ABS_MAX_P, 0, 0,
	CY_IGNORE_VALUE, CY_ABS_MIN_W, CY_ABS_MAX_W, 0, 0,
	ABS_MT_TRACKING_ID, CY_ABS_MIN_T, CY_ABS_MAX_T, 0, 0,
	ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0,
	ABS_MT_TOUCH_MINOR, 0, 255, 0, 0,
	ABS_MT_ORIENTATION, -128, 127, 0, 0,
};

static struct touch_framework cyttsp4_framework = {
	.abs = (uint16_t *)&cyttsp4_abs[0],
	.size = ARRAY_SIZE(cyttsp4_abs),
	.enable_vkeys = 0,
};

static struct cyttsp4_mt_platform_data _cyttsp4_mt_platform_data = {
	.frmwrk = &cyttsp4_framework,
	.flags = 0x00,
	.inp_dev_name = CYTTSP4_MT_NAME,
};
/*
struct cyttsp4_device cyttsp4_mt_device = {
	.name = CYTTSP4_MT_NAME,
	.core_id = "main_ttsp_core",
	.dev = {
		.platform_data = &_cyttsp4_mt_platform_data,
	}
};
*/

static struct cyttsp4_device_info cyttsp4_mt_info __maybe_unused = {
	.name = CYTTSP4_MT_NAME,
	.core_id = "main_ttsp_core",
	.platform_data = &_cyttsp4_mt_platform_data,
};

static struct cyttsp4_btn_platform_data _cyttsp4_btn_platform_data = {
	.inp_dev_name = CYTTSP4_BTN_NAME,
};

/*
struct cyttsp4_device cyttsp4_btn_device = {
	.name = CYTTSP4_BTN_NAME,
	.core_id = "main_ttsp_core",
	.dev = {
		.platform_data = &_cyttsp4_btn_platform_data,
	}
};
*/

static struct cyttsp4_device_info cyttsp4_btn_info __maybe_unused = {
	.name = CYTTSP4_BTN_NAME,
	.core_id = "main_ttsp_core",
	.platform_data = &_cyttsp4_btn_platform_data,
};

#ifdef CYTTSP4_VIRTUAL_KEYS
static ssize_t cyttps4_virtualkeys_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,
		__stringify(EV_KEY) ":"
		__stringify(KEY_BACK) ":1360:90:160:180" ":"
		__stringify(EV_KEY) ":"
		__stringify(KEY_MENU) ":1360:270:160:180" ":"
		__stringify(EV_KEY) ":"
		__stringify(KEY_HOME) ":1360:450:160:180" ":"
		__stringify(EV_KEY) ":"
		__stringify(KEY_SEARCH) ":1360:630:160:180" "\n"
		);
}

static struct kobj_attribute cyttsp4_virtualkeys_attr = {
	.attr = {
		.name = "virtualkeys.cyttsp4_mt",
		.mode = S_IRUGO,
	},
	.show = &cyttps4_virtualkeys_show,
};

static struct attribute *cyttsp4_properties_attrs[] = {
	&cyttsp4_virtualkeys_attr.attr,
	NULL
};

static struct attribute_group cyttsp4_properties_attr_group = {
	.attrs = cyttsp4_properties_attrs,
};
#endif

static unsigned long pin_pulloff_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_DISABLE, 0),
};

static unsigned long pin_pullup_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_UP, 1),
};

/*
static unsigned long pin_pulldown_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_DOWN, 1),
};
*/

/*
 * Single mapping table covering all devices that can be set up by this file.
 * Per-display variation in pull behaviour retained unmodified from pre-pinctrl.
 *
 * Driver GPIO handling is varied; ideally they would all call
 * gpio_request themselves iff they use the GPIO API.
 *
 * Cypress driver is passed a GPIO number and converts it to an IRQ with
 * gpio_to_irq. It otherwise does not use the GPIO API, except that our
 * cyttsp4_irq_stat callback uses gpio_get_value(). We can get away with an
 * irqc mux rather than requesting the GPIO. It switches between default
 * and sleep pin states in early suspend.
 *
 * Broadcom driver requests GPIO itself.
 *
 * Note that we include an irqc mux entry only for drivers that _don't_ request
 * the IRQ as a GPIO line, as mux+GPIO claims would conflict.
 */
static struct pinctrl_map pinctrl_map[] __initdata = {
	/* Cypress TrueTouch */
	SH_PFC_MUX_GROUP_DEFAULT(CYTTSP4_CORE_NAME".0",
				 CYTTSP4_IRQ_GROUP, "irqc"),
	SH_PFC_CONFIGS_GROUP_DEFAULT(CYTTSP4_CORE_NAME".0",
				     CYTTSP4_IRQ_GROUP, pin_pullup_conf),

	/* Broadcom - no entries at present; pull left as bootloader */
};

/*
 * For reasons unknown (likely an error), only the "Heat" Cypress driver
 * pulled its IRQ line down in pre-pinctrl early suspend. The "Afyon" driver
 * didn't. Both drivers have been aligned to always use
 * pinctrl_pm_select_sleep_state, and we retain differing behaviour by only
 * conditionally registering the sleep state - this is registered in
 * tsp_detector_init (used by board-heatlte.c) but not by
 * tsp_cyttsp4_init (used by board_afyonlte.c).
 */
static struct pinctrl_map cypress_sleep_pulldown[] __initdata = {
	SH_PFC_MUX_GROUP(CYTTSP4_CORE_NAME".0", PINCTRL_STATE_SLEEP,
			 CYTTSP4_IRQ_GROUP, "irqc"),
	SH_PFC_CONFIGS_GROUP(CYTTSP4_CORE_NAME".0",  PINCTRL_STATE_SLEEP,
			     CYTTSP4_IRQ_GROUP, pin_pulloff_conf),
};

static void register_cyttsp4_devices(void)
{
	pr_err("[TSP] %s\n", __func__);
/* How is this supposed to work if the driver is a module? */
#if IS_BUILTIN(CONFIG_TOUCHSCREEN_CYTTSP4)
	_cyttsp4_core_platform_data.irq_gpio = int_gpio;
	if (heat_gpio == 1) {
		cyttsp4_abs[2] = CY_ABS_MAX_X_HEAT;
		cyttsp4_abs[7] = CY_ABS_MAX_Y_HEAT;
		cyttsp4_register_core_device_heat(&cyttsp4_core_info);
		cyttsp4_register_device_heat(&cyttsp4_mt_info);
		cyttsp4_register_device_heat(&cyttsp4_btn_info);
	} else {
		cyttsp4_register_core_device(&cyttsp4_core_info);
		cyttsp4_register_device(&cyttsp4_mt_info);
		cyttsp4_register_device(&cyttsp4_btn_info);
	}
#endif
}

void __init tsp_cyttsp4_init(void)
{
	struct device_node *np;
	pr_err("[TSP] %s\n", __func__);

	np = of_find_compatible_node(NULL, NULL, "cypress,cyttsp4");
	if (np) {
		sh_pfc_register_mappings(pinctrl_map, ARRAY_SIZE(pinctrl_map));
		if (of_device_is_available(np)) {
			if (!of_property_match_string(np,
					"tname", "Cypress-Heat")) {
				heat_gpio = 1;
			/* See cypress_sleep_pulldown
			 * comments about Afyon/Heat pulls
			 */
				sh_pfc_register_mappings(cypress_sleep_pulldown,
					ARRAY_SIZE(cypress_sleep_pulldown));
			} else {
				heat_gpio = 0;
			}
		}
		of_node_put(np);
		register_cyttsp4_devices();
	} else {
		pr_err("[TSP] %s touchscreen@24 node not found\n", __func__);
	}
}




#ifdef CONFIG_TOUCHSCREEN_SYNAPTIC_RMI4
#define SYNAPTICS_TP_NAME "rmi_i2c"
#define SYNAPTICS_SLAVE_ADDR 0x22
#define SYNAPTICS_INT_PIN 30
#define SYNAPTICS_I2C_ID  4

struct syna_gpio_data {
	u16 gpio_number;
	char *gpio_name;
};
static struct syna_gpio_data synaptics2202_gpiodata = {
	.gpio_number = SYNAPTICS_INT_PIN,
	.gpio_name = "touch_interrupt",
};

static struct virtualbutton_map synaptics_button_codes[3] = {
{.x = 200, .y = 2100, .width = 200, .height = 100, .code = KEY_BACK},
{.x = 500, .y = 2100, .width = 200, .height = 100, .code = KEY_HOME},
{.x = 900, .y = 2100, .width = 200, .height = 100, .code = KEY_MENU},

};

static struct rmi_f11_virtualbutton_map synaptics_button_map = {
	.buttons = ARRAY_SIZE(synaptics_button_codes),
	.map = synaptics_button_codes,
};
static int synaptics_touchpad_gpio_setup(void *gpio_data, bool configure)
{
	int retval = 0;
	struct syna_gpio_data *data = gpio_data;

	if (configure) {
		retval = gpio_request(data->gpio_number, "rmi4_attn");
		if (retval) {
			pr_err("%s: Failed to get attn gpio %d. Code: %d.",
			       __func__, data->gpio_number, retval);
			return retval;
		}

		retval = gpio_direction_input(data->gpio_number);
		if (retval) {
			pr_err("%s: Failed to setup attn gpio %d. Code: %d.",
			       __func__, data->gpio_number, retval);
			gpio_free(data->gpio_number);
		}
	} else {
		pr_warn("%s: No way to deconfigure gpio %d.",
		       __func__, data->gpio_number);
	}

	return retval;
}

static struct rmi_device_platform_data synaptics2202_platformdata = {
	.driver_name = SYNAPTICS_TP_NAME,
	.attn_gpio = SYNAPTICS_INT_PIN,
	.attn_polarity = RMI_ATTN_ACTIVE_LOW,
	.gpio_data = &synaptics2202_gpiodata,
	.gpio_config = synaptics_touchpad_gpio_setup,
	.virtualbutton_map = &synaptics_button_map,
	.axis_align = {
		.swap_axes = false,
	},
};
static struct i2c_board_info __initdata synaptics_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO(SYNAPTICS_TP_NAME, SYNAPTICS_SLAVE_ADDR),
		.irq = irq_pin(SYNAPTICS_INT_PIN),
		.platform_data = &synaptics2202_platformdata,
	},
};

void  __init tsp_synaptics2202_init(void)
{
	i2c_register_board_info(SYNAPTICS_I2C_ID,
		synaptics_i2c_boardinfo,
		ARRAY_SIZE(synaptics_i2c_boardinfo));
}
#endif

//++ add broadcom patch: tp driver by suwei 2014.4.25
#ifdef CONFIG_TOUCHSCREEN_EKTF2K
#define EKTF_I2C_ID 4
#define ELAN_IRQ_PIN  30


struct elan_ktf2k_i2c_platform_data ts_elan_ktf2k_data[] = {
	{
			.version = 0x0001,
			.abs_x_min = 0,
			.abs_x_max = ELAN_X_MAX,
			.abs_y_min = 0,
			.abs_y_max = ELAN_Y_MAX,
			.intr_gpio = ELAN_IRQ_PIN,
	},
};
static struct i2c_board_info ektf_i2c_devices[] = {
	{
			I2C_BOARD_INFO(ELAN_KTF2K_NAME, 0x15),
			.platform_data = &ts_elan_ktf2k_data,
			.irq = irq_pin(ELAN_IRQ_PIN),
	},

};
void  __init tsp_ektf2227_init(void)
{
	i2c_register_board_info(EKTF_I2C_ID, ektf_i2c_devices,
		ARRAY_SIZE(ektf_i2c_devices));
}
#endif
//-- add broadcom patch: tp driver by suwei 2014.4.25
