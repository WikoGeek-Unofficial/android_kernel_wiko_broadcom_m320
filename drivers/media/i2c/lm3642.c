/*
 * drivers/media/i2c/lm3642.c - LM3642 flash lamp controllers driver
 *
 * Copyright (c) 2013,2014 Broadcom Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#include <media/lm3642.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

/* Register definitions */
/* Enable register */
#define LM3642_ENABLE_REG           0x0a
/* mode slection */
#define LM3642_ENABLE_EN_MASK           (0x03 << 0)
#define LM3642_ENABLE_EN_SHUTDOWN       (0x00 << 0)
#define LM3642_ENABLE_EN_INDICAT_MODE   (0x01 << 0)
#define LM3642_ENABLE_EN_TORCH_MODE     (0x02 << 0)
#define LM3642_ENABLE_EN_FLASH_MODE     (0x03 << 0)
/* torch pin selection */
#define LM3642_ENABLE_TORCH_PIN_MASK    (0x01 << 4)
#define LM3642_ENABLE_TORCH_PIN_DISABLE (0x00 << 4)
#define LM3642_ENABLE_TORCH_PIN_ENABLE  (0x01 << 4)
/* storbe pin selection */
#define LM3642_ENABLE_STROBE_PIN_MASK    (0x01 << 5)
#define LM3642_ENABLE_STROBE_PIN_DISABLE (0x00 << 5)
#define LM3642_ENABLE_STROBE_PIN_ENABLE  (0x01 << 5)
/* TX pin selection */
#define LM3642_ENABLE_TX_PIN_MASK       (0x01 << 6)
#define LM3642_ENABLE_TX_PIN_DISABLE    (0x00 << 6)
#define LM3642_ENABLE_TX_PIN_ENABLE     (0x01 << 6)
/* IVFM mode selection */
#define LM3642_ENABLE_IVFM_MASK         (0x01 << 7)
#define LM3642_ENABLE_IVFM_DISABLE      (0x00 << 7)
#define LM3642_ENABLE_IVFM_ENABLE       (0x01 << 7)

/* Flags register */
#define LM3642_FLAGS_REG            0x0b
#define LM3642_FLAGS_TIMEOUT_MASK       (0x01 << 0)
#define LM3642_FLAGS_THERMAL_MASK       (0x01 << 1)
#define LM3642_FLAGS_SHORT_MASK         (0x01 << 2)
#define LM3642_FLAGS_OVP_MASK           (0x01 << 3)
#define LM3642_FLAGS_UVLO_MASK          (0x01 << 4)
#define LM3642_FLAGS_IVFM_MASK          (0x01 << 5)

/* Flash feature register*/
#define LM3642_FLASH_FEATUER_REG    0x08
#define LM3642_FLASH_TIMEOUT_MASK       (0x7 << 0)
#define LM3642_FLASH_RAMPTIME_MSAK      (0x7 << 3)
#define LM3642_FLASH_INDUC_LIMIT_MASK   (0x1 << 6)

/* Current control register */
#define LM3642_CURRENT_CONTROL_REG  0x09
#define LM3642_CURRENT_FLASH_MASK       (0xf << 0)
#define LM3642_CURRENT_TORCH_MASK       (0x7 << 4)

/* IVFM register */
#define LM3642_IVFM_REG             0x01
#define LM3642_IVFM_DOWN_THRESH_MASK    (0x7 << 2)
#define LM3642_IVFM_UVLDO_MASK          (0x1 << 7)
#define LM3642_IVFM_UVLDO_DISABLE       (0x0 << 7)
#define LM3642_IVFM_UVLDO_ENABLE        (0x1 << 7)

/* Torch ramp time register */
#define LM3642_TORCH_RAMPTIME_REG   0x06
#define LM3642_TORCH_RAMPTIME_DOWN_MASK (0x7 << 0)
#define LM3642_TORCH_RAMPTIME_UP_MASK   (0x7 << 3)

/* Revision register */
#define LM3642_REVISION_REG         0x00
#define LM3642_REVISION_MASK            (0x7 << 0)


#define LM3642_TIMER_MS_TO_CODE(t)      (((t) - 100) / 100)
#define LM3642_TIMER_CODE_TO_MS(c)      (100 * (c) + 100)

/*
 * struct lm3642_s
 *
 * @subdev:		V4L2 subdev
 * @pdata:		Flash platform data
 * @client:		I2C client
 * @timeout:		Flash timeout in microseconds
 * @flash_current:	Flash current (0=94mA ... 15=1500mA). Maximum
 *			values are 400mA for two LEDs and 500mA for one LED.
 * @torch_current:	Torch light current (0=24mA, 1=47mA ... 7=187mA)
 * @led_mode:		V4L2 flash LED mode
 */
struct lm3642_s {
	const struct lm3642_platform_data *pdata;
	struct i2c_client *client;
	unsigned int timeout;
	u8 flash_current;
	u8 torch_current;
	u8 led_mode;
};

static struct lm3642_s *lm3642;

/* Return negative errno else zero on success */
static int lm3642_write(struct lm3642_s *flash, u8 addr, u8 val)
{
	int rval;

	rval = i2c_smbus_write_byte_data(flash->client, addr, val);

	pr_debug("%s: Write [%02X]=%02X %s\n", __func__, addr, val,
		rval < 0 ? "fail" : "ok");

	return rval;
}

/* Return negative errno else a data byte received from the device. */
static int lm3642_read(struct lm3642_s *flash, u8 addr, u8 *val)
{
	int rval;

	rval = i2c_smbus_read_byte_data(flash->client, addr);
	if (rval >= 0)
		*val = (u8)(rval & 0xFF);

	pr_debug("%s: Read [%02X]=%02X %s\n", __func__, addr, rval,
		rval < 0 ? "fail" : "ok");

	return rval;
}

static int lm3642_read_fault(struct lm3642_s *flash)
{
	int rval;
	u8 val;

	/* NOTE: reading register clear fault status */
	rval = lm3642_read(flash, LM3642_FLAGS_REG, &val);
	if (rval < 0)
		return rval;

	if (val & LM3642_FLAGS_IVFM_MASK)
		pr_info("%s: IVFM fault\n", __func__);

	if (val & LM3642_FLAGS_UVLO_MASK)
		pr_info("%s: UVLO fault\n", __func__);

	if (val & LM3642_FLAGS_TIMEOUT_MASK)
		pr_info("%s: Timeout fault\n", __func__);

	if (val & LM3642_FLAGS_THERMAL_MASK)
		pr_info("%s: Over temperature fault\n", __func__);

	if (val & LM3642_FLAGS_SHORT_MASK)
		pr_info("%s: Short circuit fault\n", __func__);

	if (val & LM3642_FLAGS_OVP_MASK)
		pr_info("%s: Over voltage fault\n", __func__);

	return rval;
}

int lm3642_enable(void)
{
	int ret = 0;

	if (NULL == lm3642)
		return -EINVAL;

	if (lm3642->pdata->ext_ctrl) {
		/* setup pins */
		if (LM3642_ENABLE_EN_FLASH_MODE == lm3642->led_mode) {
			gpio_set_value(lm3642->pdata->torch_pin, 0);
			gpio_set_value(lm3642->pdata->strobe_pin, 1);
		} else if (LM3642_ENABLE_EN_TORCH_MODE == lm3642->led_mode) {
			gpio_set_value(lm3642->pdata->torch_pin, 1);
			gpio_set_value(lm3642->pdata->strobe_pin, 0);
		} else {
			gpio_set_value(lm3642->pdata->torch_pin, 0);
			gpio_set_value(lm3642->pdata->strobe_pin, 0);
		}
	} else {
		u8 val;
		ret = lm3642_read(lm3642, LM3642_ENABLE_REG, &val);
		val &= ~LM3642_ENABLE_EN_MASK;
		val |= lm3642->led_mode;
		lm3642_write(lm3642, LM3642_ENABLE_REG, val);
	}

	return ret;
}

int lm3642_disable(void)
{
	int ret = 0;

	if (NULL == lm3642)
		return -EINVAL;

	if (lm3642->pdata->ext_ctrl) {
		gpio_set_value(lm3642->pdata->torch_pin, 0);
		gpio_set_value(lm3642->pdata->strobe_pin, 0);
	} else {
		u8 val;
		ret = lm3642_read(lm3642, LM3642_ENABLE_REG, &val);
		val &= ~LM3642_ENABLE_EN_MASK;
		val |= LM3642_ENABLE_EN_SHUTDOWN;
		lm3642_write(lm3642, LM3642_ENABLE_REG, val);
	}

	return ret;
}

/* Put device into know state. */
int lm3642_setup(int mode, int intensity, int duration)
{
	int ret;
	u8 val = 0;

	if (NULL == lm3642)
		return -EINVAL;

	pr_debug("%s: mode:%d intens: %d duration:%d\n", __func__,
		mode, intensity, duration);
	/* read status to clear errors */
	ret = lm3642_read_fault(lm3642);
	if (ret < 0)
		return ret;

	/* setup params */
	if (V4L2_FLASH_LED_MODE_FLASH == mode) {
		lm3642->led_mode = LM3642_ENABLE_EN_FLASH_MODE;
		lm3642->flash_current = (u8)(intensity & 0xF);
		lm3642->timeout = LM3642_TIMER_MS_TO_CODE(duration);
	} else if (V4L2_FLASH_LED_MODE_TORCH == mode) {
		lm3642->led_mode = LM3642_ENABLE_EN_TORCH_MODE;
		lm3642->torch_current = (u8)(intensity & 0x7);
	}

	/* sync to chip */
	val = lm3642->flash_current | (lm3642->torch_current << 4);
	lm3642_write(lm3642, LM3642_CURRENT_CONTROL_REG, val);

	ret = lm3642_read(lm3642, LM3642_FLASH_FEATUER_REG, &val);
	val &= ~LM3642_FLASH_TIMEOUT_MASK;
	val |= lm3642->timeout;
	lm3642_write(lm3642, LM3642_FLASH_FEATUER_REG, val);

	if (lm3642->pdata->ext_ctrl ||
		(V4L2_FLASH_LED_MODE_NONE == mode)) {
		/* sync mode */
		ret = lm3642_read(lm3642, LM3642_ENABLE_REG, &val);
		val &= ~LM3642_ENABLE_EN_MASK;
		val |= lm3642->led_mode;
		if (lm3642->pdata->ext_ctrl)
			val |= 0x30;
		lm3642_write(lm3642, LM3642_ENABLE_REG, val);
	}

	return 0;
}


/* -----------------------------------------------------------------------------
 *  I2C driver
 */
static int lm3642_probe(struct i2c_client *client,
			 const struct i2c_device_id *devid)
{
	struct lm3642_s *flash;
	int ret = 0;

	if (client->dev.platform_data == NULL)
		return -ENODEV;

	flash = kzalloc(sizeof(struct lm3642_s), GFP_KERNEL);
	if (flash == NULL)
		return -ENOMEM;

	flash->pdata = client->dev.platform_data;
	flash->client = client;
	flash->timeout = 0;
	flash->flash_current = 0;
	flash->torch_current = 0;
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
	lm3642_disable();

	lm3642 = flash;

	return ret;
}

static int lm3642_remove(struct i2c_client *client)
{
	kfree(lm3642);
	lm3642 = NULL;

	return 0;
}

static const struct i2c_device_id lm3642_id_table[] = {
	{LM3642_NAME, 0},
	{ },
};
MODULE_DEVICE_TABLE(i2c, lm3642_id_table);

static struct i2c_driver lm3642_i2c_driver = {
	.driver	= {
		.name = LM3642_NAME,
	},
	.probe	= lm3642_probe,
	.remove	= lm3642_remove,
	.id_table = lm3642_id_table,
};

module_i2c_driver(lm3642_i2c_driver);

MODULE_AUTHOR("Jun He <junhe@broadcom.com>");
MODULE_DESCRIPTION("Flashlamp driver for LM3642");
MODULE_LICENSE("GPL");
