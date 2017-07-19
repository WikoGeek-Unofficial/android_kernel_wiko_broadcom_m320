/*
 * Toshiba T8EV5 sensor driver
 *
 * Copyright 2014 Broadcom Corporation.  All rights reserved.
 * http://www.broadcom.com/licenses/GPLv2.php
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <mach/r8a7373.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <media/sh_mobile_csi2.h>
#include <linux/videodev2_brcm.h>
#ifdef CONFIG_VIDEO_LM3642
#include <media/lm3642.h>
#endif
#include "t8ev5.h"

int flash_check;

/* t8ev5 has only one fixed colorspace per pixelcode */
struct t8ev5_datafmt {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};

static const struct t8ev5_datafmt t8ev5_fmts[] = {
	/*
	 * Order important: first natively supported,
	 *second supported with a GPIO extender
	 */
	{V4L2_MBUS_FMT_SBGGR10_1X10,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SGBRG10_1X10,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SGRBG10_1X10,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SRGGB10_1X10,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_UYVY8_2X8,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_VYUY8_2X8,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_YUYV8_2X8,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_YVYU8_2X8,	V4L2_COLORSPACE_SRGB},

};

enum t8ev5_size {
	T8EV5_SIZE_SXGA = 0,/*  1280 x 960 */
	T8EV5_SIZE_5MP,	    /*  2592 x 1944 */
	T8EV5_SIZE_LAST,
	T8EV5_SIZE_MAX
};

enum t8ev5_mode {
	T8EV5_MODE_IDLE = 0,
	T8EV5_MODE_STREAMING,
	T8EV5_MODE_CAPTURE,
	T8EV5_MODE_MAX,
};

static const struct v4l2_frmsize_discrete t8ev5_frmsizes[T8EV5_SIZE_LAST] = {
	{1280, 960},
	{2560, 1920},
};

static const struct t8ev5_reg t8ev5_modes[T8EV5_SIZE_LAST][128] = {
	{
		/* SXGA */
		{0x0102, 0x03},
		{0x0105, 0x01},
		{0x0106, 0x58},
		{0x0107, 0x00},
		{0x0108, 0x7B},
		{0x0109, 0x00},
		{0x010A, 0x20},
		{0x010B, 0x11},
		{0x010D, 0x05},
		{0x010E, 0x00},
		{0x010F, 0x03},
		{0x0110, 0xC0},
		{0x013A, 0x01},
		{0x0315, 0x98},
		{0x0102, 0x02},

		{T8EV5_REG_ESC, 0},
	},
	{
		/* 5MP */
		{0x0102, 0x03},	/* -/-/-/-/-/-/VLAT_ON/GROUP_HOLD */
		{0x0105, 0x01},	/* - / - / - / - / - / H_COUNT[10:8] */
		{0x0106, 0x6F},	/* H_COUNT[7:0]  */
		{0x0107, 0x00},	/* - / - / - / - / - / V_COUNT[10:8]  */
		{0x0108, 0xF7},	/* V_COUNT[7:0]  */
		{0x0109, 0x00},	/* - / - / - / - / - / - / - / SCALE_M[8]  */
		{0x010A, 0x10},	/* SCALE_M[7:0]  */
		{0x010B, 0x00},	/* - / V_ANABIN[2:0] / - / - / - / H_ANABIN  */
		{0x010D, 0x0A},	/* - / - / - / - / HOUTPIX[11:8]  */
		{0x010E, 0x00},	/* HOUTPIX[7:0]  */
		{0x010F, 0x07},	/* - / - / - / - / - / VOUTPIX[10:8]  */
		{0x0110, 0x80},	/* VOUTPIX[7:0]  */
		{0x013A, 0x02},
		{0x0315, 0x98},
		{0x0102, 0x02},	/* -/-/-/-/-/-/VLAT_ON/GROUP_HOLD */
		{T8EV5_REG_ESC, 0},
	},
};

/* Power function for t8ev5 */
/* FIXME: the rst pin and pwdn pin is board related
 * so such definitions should be removed from here
 * and passed from board_xxx
 */
#define T8EV5_RESET_PIN     GPIO_PORT20
#define T8EV5_PWDN_PIN      GPIO_PORT45

int t8ev5_power(struct device *dev, int power_on)
{
	struct clk *vclk1_clk;
	int iret;
	struct regulator *regulator;

	vclk1_clk = clk_get(NULL, "vclk1_clk");
	if (IS_ERR(vclk1_clk)) {
		dev_err(dev, "clk_get(vclk1_clk) failed\n");
		return -1;
	}

	if (power_on) {
		pr_info("%s PowerON\n", __func__);
		sh_csi2_power(dev, power_on);

		/* CAM_DVDD On */
		gpio_set_value(GPIO_PORT8, 1); /* CAM0_DVDD_PWR_EN */
		mdelay(10);
		/* CAM_AVDD_2V8  On */
		regulator = regulator_get(NULL, "cam_sensor_a");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_enable(regulator);
		regulator_put(regulator);

		mdelay(10);
		/* CAM_IOVDD On */
		regulator = regulator_get(NULL, "cam_sensor_io");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_enable(regulator);
		regulator_set_voltage(regulator, 1800000, 1800000);
		regulator_put(regulator);

		mdelay(10);

		iret = clk_set_rate(vclk1_clk,
		clk_round_rate(vclk1_clk, 24000000));
		if (0 != iret) {
			dev_err(dev,
			"clk_set_rate(vclk1_clk) failed (ret=%d)\n",
				iret);
		}

		iret = clk_enable(vclk1_clk);

		if (0 != iret) {
			dev_err(dev, "clk_enable(vclk1_clk) failed (ret=%d)\n",
				iret);
		}
		mdelay(10);
		gpio_set_value(T8EV5_PWDN_PIN, 1); /* CAM0_STBY */
		mdelay(5);

		gpio_set_value(T8EV5_RESET_PIN, 1); /* CAM0_RST_N Hi */
		mdelay(30);

		gpio_set_value(GPIO_PORT91, 1); /* CAM1_STBY */
				mdelay(10);

		/* 5M_AF_2V8 On */
		regulator = regulator_get(NULL, "cam_af");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_enable(regulator);
		regulator_put(regulator);

		pr_info("%s PowerON fin\n", __func__);
	} else {
		pr_info("%s PowerOFF\n", __func__);
		mdelay(1);
		gpio_set_value(T8EV5_RESET_PIN, 0); /* CAM0_RST_N */
		mdelay(3);

		gpio_set_value(T8EV5_PWDN_PIN, 0); /* CAM0_STBY */
		mdelay(5);
		gpio_set_value(GPIO_PORT91, 1); /* CAM1_STBY */
		mdelay(10);

		clk_disable(vclk1_clk);
		mdelay(5);
		/* CAM_DVDD_1V5 On */
		regulator = regulator_get(NULL, "cam_sensor_io");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_disable(regulator);
		regulator_put(regulator);
		mdelay(5);

		/* CAM_AVDD_2V8  Off */
		regulator = regulator_get(NULL, "cam_sensor_a");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_disable(regulator);
		regulator_put(regulator);
		mdelay(5);

		/* CAM_VDDIO_1V8 Off */
		gpio_set_value(GPIO_PORT8, 0); /* CAM0_PWR_EN */
		mdelay(1);

		/* 5M_AF_2V8 Off */
		regulator = regulator_get(NULL, "cam_af");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_disable(regulator);
		regulator_put(regulator);
		sh_csi2_power(dev, power_on);
		pr_info("%s PowerOFF fin\n", __func__);
	}

	clk_put(vclk1_clk);

	return 0;
}



/* Find a data format by a pixel code in an array */
static int t8ev5_find_datafmt(enum v4l2_mbus_pixelcode code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(t8ev5_fmts); i++)
		if (t8ev5_fmts[i].code == code)
			break;

	/* If not found, select latest */
	if (i >= ARRAY_SIZE(t8ev5_fmts))
		i = ARRAY_SIZE(t8ev5_fmts) - 1;

	return i;
}

/* Find a frame size in an array */
static int t8ev5_find_framesize(u32 width, u32 height)
{
	int i;

	for (i = 0; i < T8EV5_SIZE_LAST; i++) {
		if ((t8ev5_frmsizes[i].width >= width) &&
		    (t8ev5_frmsizes[i].height >= height))
			break;
	}

	/* If not found, select biggest */
	if (i >= T8EV5_SIZE_LAST)
		i = T8EV5_SIZE_LAST - 1;

	return i;
}

struct t8ev5_s {
	struct v4l2_subdev subdev;
	struct v4l2_subdev_sensor_interface_parms *plat_parms;
	int i_size;
	int i_fmt;
	short ev;
	short effect;
	short contrast;
	short sharpness;
	short saturation;
	short banding;
	short wb;
	short focus_mode;
	int framerate;
	int width;
	int height;
	enum v4l2_flash_led_mode flash_mode;
	short inited;
	short mode;
	short status;
	short active_mode;
};

static struct t8ev5_s *to_t8ev5(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client),
			struct t8ev5_s, subdev);
}

/**
 * t8ev5_reg_read - Read a value from a register in t8ev5
 * @client: i2c driver client structure
 * @reg: register address / offset
 * @val: stores the value that gets read
 *
 * Read a value from a register in an t8ev5 sensor device.
 * The value is returned in 'val'.
 * Returns zero if successful, or non-zero otherwise.
 */
static int t8ev5_reg_read(struct i2c_client *client, u16 reg, u8 *val)
{
	int ret;
	u8 data[2] = { 0 };
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
		.buf = data,
	};

	data[0] = (u8) (reg >> 8);
	data[1] = (u8) (reg & 0xff);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		goto err;

	msg.flags = I2C_M_RD;
	msg.len = 1;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		goto err;

	*val = data[0];
	return 0;

err:
	dev_err(&client->dev, "Failed reading register 0x%02x!\n", reg);
	return ret;
}

/**
 * Write a value to a register in t8ev5 sensor device.
 *@client: i2c driver client structure.
 *@reg: Address of the register to read value from.
 *@val: Value to be written to a specific register.
 * Returns zero if successful, or non-zero otherwise.
 */
static int t8ev5_reg_write(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;
	unsigned char data[4] = { (u8) (reg >> 8), (u8) (reg & 0xff), val, 0 };
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 3,
		.buf = data,
	};

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev,
		"Failed writing register 0x%04xvat 0x%x !\n", reg, val);
		return ret;
	}

	return 0;
}

static const struct v4l2_queryctrl t8ev5_controls[] = {
	{
	 .id = V4L2_CID_CAMERA_BRIGHTNESS,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "EV",
	 .minimum = EV_MINUS_4,
	 .maximum = EV_PLUS_4,
	 .step = 2,
	 .default_value = EV_DEFAULT,
	 },

	{
	 .id = V4L2_CID_CAMERA_EFFECT,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "Color Effects",
	 .minimum = IMAGE_EFFECT_NONE,
	 .maximum = (1 << IMAGE_EFFECT_NONE | 1 << IMAGE_EFFECT_SEPIA |
		     1 << IMAGE_EFFECT_BNW | 1 << IMAGE_EFFECT_NEGATIVE),
	 .step = 1,
	 .default_value = IMAGE_EFFECT_NONE,
	 },
	{
	 .id = V4L2_CID_CAMERA_ANTI_BANDING,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "Anti Banding",
	 .minimum = ANTI_BANDING_AUTO,
	 .maximum = ANTI_BANDING_60HZ,
	 .step = 1,
	 .default_value = ANTI_BANDING_AUTO,
	 },
	{
	 .id = V4L2_CID_CAMERA_WHITE_BALANCE,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "White Balance",
	 .minimum = WHITE_BALANCE_AUTO,
	 .maximum = WHITE_BALANCE_FLUORESCENT,
	 .step = 1,
	 .default_value = WHITE_BALANCE_AUTO,
	 },
	{
	 .id = V4L2_CID_CAMERA_FRAME_RATE,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "Framerate control",
	 .minimum = FRAME_RATE_AUTO,
	 .maximum = (1 << FRAME_RATE_AUTO | 1 << FRAME_RATE_15 |
				1 << FRAME_RATE_30),
	 .step = 1,
	 .default_value = FRAME_RATE_AUTO,
	},
	{
	 .id = V4L2_CID_CAMERA_FLASH_MODE,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "Flash control",
	 .minimum = V4L2_FLASH_LED_MODE_NONE,
	 .maximum = V4L2_FLASH_LED_MODE_TORCH,
	 .step = 1,
	 .default_value = V4L2_FLASH_LED_MODE_NONE,
	},
};

/**
 * Initialize a list of t8ev5 registers.
 * The list of registers is terminated by the pair of values
 * @client: i2c driver client structure.
 * @reglist[]: List of address of the registers to write data.
 * Returns zero if successful, or non-zero otherwise.
 */
static int t8ev5_reg_writes(struct i2c_client *client,
			     const struct t8ev5_reg reglist[])
{
	int err = 0, index;

	for (index = 0; 0 == err; index++) {
		if (T8EV5_REG_ESC != reglist[index].reg)
			err |= t8ev5_reg_write(client,
				reglist[index].reg, reglist[index].val);
		else {
			if (reglist[index].val)
				msleep(reglist[index].val);
			else
				break;
		}
	}
	return 0;
}

#ifdef T8EV5_DEBUG
#define iprintk(format, arg...)	\
	pr_info("[%s]: "format"\n", __func__, ##arg)

static int t8ev5_reglist_compare(struct i2c_client *client,
				  const struct t8ev5_reg reglist[])
{
	int err = 0, index;
	u8 reg;

	for (index = 0; err == 0; index++) {
		if (T8EV5_REG_ESC != reglist[index].reg) {
			err |= t8ev5_reg_read(client, reglist[index].reg, &reg);
			if (reglist[index].val != reg) {
				iprintk("reg err:reg=0x%x val=0x%x rd=0x%x",
				reglist[index].reg, reglist[index].val, reg);
			}
		} else {
			if (reglist[index].val)
				msleep(reglist[index].val);
			else
				break;
		}
	}
	return 0;
}
#endif

static int t8ev5_set_flash_mode(struct i2c_client *client, int mode)
{
	int ret = 0;
	struct t8ev5_s *t8ev5 = to_t8ev5(client);

	if (t8ev5->flash_mode == mode)
		return 0;

	pr_info("%s: mode:%d\n", __func__, mode);
	#ifdef CONFIG_VIDEO_LM3642
	lm3642_disable();
	switch (mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		lm3642_setup(mode, 0, 0);
		break;
	case V4L2_FLASH_LED_MODE_FLASH:
		lm3642_setup(mode, 0x8, 600);
		break;
	case V4L2_FLASH_LED_MODE_TORCH:
		lm3642_setup(mode, 0x4, 0);
		lm3642_enable();
		break;
	default:
		break;
	}
	#endif

	t8ev5->flash_mode = mode;

	return ret;
}

static int t8ev5_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	struct t8ev5_s *t8ev5 = to_t8ev5(client);
	int ret = 0;

	if (on) {
		ret = soc_camera_power_on(&client->dev, ssdd);
		pr_info("%s: download init/af settings\n", __func__);
		ret = t8ev5_reg_writes(client, t8ev5_common_init);
		ret = t8ev5_reg_writes(client, t8ev5_af_fw);
		t8ev5->focus_mode = -1;
		t8ev5->flash_mode = V4L2_FLASH_LED_MODE_NONE;
		t8ev5->inited = 1;
	} else {
		ret = soc_camera_power_off(&client->dev, ssdd);
		t8ev5_set_flash_mode(client, V4L2_FLASH_LED_MODE_NONE);
		t8ev5->inited = 0;
		t8ev5->active_mode = T8EV5_SIZE_LAST;
	}

	pr_info("%s: X :%d\n", __func__, ret);
	return ret;
}

static int t8ev5_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct t8ev5_s *t8ev5 = to_t8ev5(client);
	pr_info("%s: enable:%d runmode:%d  stream_mode:%d\n",
	       __func__, enable, t8ev5->mode, t8ev5->status);

	if (t8ev5->mode == t8ev5->status)
		return ret;

	if (t8ev5->mode) {
		/* download mode */
		pr_info("%s: download mode[%d]\n", __func__, t8ev5->i_size);
		t8ev5_reg_writes(client, t8ev5_modes[t8ev5->i_size]);

		/* start stream*/
		pr_info("%s: start streaming\n", __func__);
		ret = t8ev5_reg_writes(client, t8ev5_stream_on);
	} else
		ret = t8ev5_reg_writes(client, t8ev5_stream_off);

	t8ev5->status = t8ev5->mode;

	return ret;
}

static int t8ev5_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct t8ev5_s *t8ev5 = to_t8ev5(client);

	mf->width = t8ev5_frmsizes[t8ev5->i_size].width;
	mf->height = t8ev5_frmsizes[t8ev5->i_size].height;
	mf->code = t8ev5_fmts[t8ev5->i_fmt].code;
	mf->colorspace = t8ev5_fmts[t8ev5->i_fmt].colorspace;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static int t8ev5_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	int i_fmt;
	int i_size;

	i_fmt = t8ev5_find_datafmt(mf->code);

	mf->code = t8ev5_fmts[i_fmt].code;
	mf->colorspace = t8ev5_fmts[i_fmt].colorspace;
	mf->field = V4L2_FIELD_NONE;

	i_size = t8ev5_find_framesize(mf->width, mf->height);

	mf->width = t8ev5_frmsizes[i_size].width;
	mf->height = t8ev5_frmsizes[i_size].height;

	return 0;
}

static int t8ev5_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct t8ev5_s *t8ev5 = to_t8ev5(client);
	int ret = 0;

	pr_info("%s: E\n", __func__);
	ret = t8ev5_try_fmt(sd, mf);
	if (ret < 0)
		return ret;

	t8ev5->i_size = t8ev5_find_framesize(mf->width, mf->height);
	t8ev5->i_fmt = t8ev5_find_datafmt(mf->code);

	pr_info("%s: X:%d size:%d\n", __func__, ret, t8ev5->i_size);
	return ret;
}

static int t8ev5_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	pr_info("%s\n", __func__);
	id->ident = V4L2_IDENT_T8EV5;
	id->revision = 0;

	return 0;
}

static int t8ev5_get_tline(struct v4l2_subdev *sd)
{
	return 0;
}

static int t8ev5_get_integ(struct v4l2_subdev *sd)
{
	return 0;
}


static int t8ev5_get_exp_time(struct v4l2_subdev *sd)
{
	unsigned short val1, val2, etime;
	val1 = t8ev5_get_integ(sd);
	val2 = t8ev5_get_tline(sd);
	etime = val1 * val2;
	return etime;
}


static int t8ev5_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct t8ev5_s *t8ev5 = to_t8ev5(client);
	u8 val;

	dev_dbg(&client->dev, "t8ev5_g_ctrl\n");

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_BRIGHTNESS:
		ctrl->value = t8ev5->ev;
		break;
	case V4L2_CID_CAMERA_CONTRAST:
		ctrl->value = t8ev5->contrast;
		break;
	case V4L2_CID_CAMERA_EFFECT:
		ctrl->value = t8ev5->effect;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = t8ev5->saturation;
		break;
	case V4L2_CID_SHARPNESS:
		ctrl->value = t8ev5->sharpness;
		break;
	case V4L2_CID_CAMERA_ANTI_BANDING:
		ctrl->value = t8ev5->banding;
		break;
	case V4L2_CID_CAMERA_WHITE_BALANCE:
		ctrl->value = t8ev5->wb;
		break;
	case V4L2_CID_CAMERA_AUTO_FOCUS_RESULT:
		/* read AF status */
		if (0 != t8ev5->focus_mode) {
			ctrl->value = 0;
			break;
		}
		t8ev5_reg_read(client, 0x2800, &val);
		pr_info("%s: AF Status: 0x%x\n", __func__, val);
		if (val & 0x90)
			ctrl->value = -1;
		else
			ctrl->value = 0;
		break;
	case V4L2_CID_CAMERA_FRAME_RATE:
		ctrl->value = t8ev5->framerate;
		break;
	case V4L2_CID_CAMERA_EXP_TIME:
		ctrl->value = t8ev5_get_exp_time(sd);
		break;
	case V4L2_CID_CAMERA_FLASH_MODE:
		ctrl->value = t8ev5->flash_mode;
		break;
	}

	return 0;
}

static int t8ev5_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct t8ev5_s *t8ev5 = to_t8ev5(client);
	int ret = 0;

	dev_dbg(&client->dev, "t8ev5_s_ctrl\n");

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_BRIGHTNESS:
		if (ctrl->value > EV_PLUS_4)
			return -EINVAL;

		t8ev5->ev = ctrl->value;
		switch (t8ev5->ev) {
		case EV_MINUS_4:
			ret = t8ev5_reg_writes(client, t8ev5_ev_lv1);
			break;
		case EV_MINUS_2:
			ret = t8ev5_reg_writes(client, t8ev5_ev_lv2);
			break;
		case EV_PLUS_2:
			ret = t8ev5_reg_writes(client, t8ev5_ev_lv4);
			break;
		case EV_PLUS_4:
			ret = t8ev5_reg_writes(client, t8ev5_ev_lv5);
			break;
		default:
			ret = t8ev5_reg_writes(client, t8ev5_ev_lv3_default);
			break;
		}
		break;

	case V4L2_CID_CAMERA_CONTRAST:
		if (ctrl->value > CONTRAST_PLUS_2)
			return -EINVAL;

		t8ev5->contrast = ctrl->value;
		switch (t8ev5->contrast) {
		case CONTRAST_MINUS_2:
			ret = t8ev5_reg_writes(client, t8ev5_contrast_lv1);
			break;
		case CONTRAST_MINUS_1:
			ret = t8ev5_reg_writes(client, t8ev5_contrast_lv2);
			break;
		case CONTRAST_PLUS_1:
			ret = t8ev5_reg_writes(client, t8ev5_contrast_lv4);
			break;
		case CONTRAST_PLUS_2:
			ret = t8ev5_reg_writes(client, t8ev5_contrast_lv5);
			break;
		default:
			ret = t8ev5_reg_writes(client,
						t8ev5_contrast_lv3_default);
			break;
		}
		break;

	case V4L2_CID_CAMERA_EFFECT:
		if (ctrl->value > IMAGE_EFFECT_BNW)
			return -EINVAL;

		t8ev5->effect = ctrl->value;

		switch (t8ev5->effect) {
		case IMAGE_EFFECT_MONO:
			ret = t8ev5_reg_writes(client, t8ev5_effect_bw);
			break;
		case IMAGE_EFFECT_SEPIA:
			ret = t8ev5_reg_writes(client,
						t8ev5_effect_sepia);
			break;
		case IMAGE_EFFECT_NEGATIVE:
			ret = t8ev5_reg_writes(client,
						t8ev5_effect_negative);
			break;
		default:
			ret = t8ev5_reg_writes(client,
						t8ev5_effect_no);
			break;
		}
		break;

	case V4L2_CID_SATURATION:
		if (ctrl->value > SATURATION_PLUS_2)
			return -EINVAL;

		t8ev5->saturation = ctrl->value;
		switch (t8ev5->saturation) {
		case SATURATION_MINUS_2:
			ret = t8ev5_reg_writes(client, t8ev5_saturation_lv1);
			break;
		case SATURATION_MINUS_1:
			ret = t8ev5_reg_writes(client, t8ev5_saturation_lv2);
			break;
		case SATURATION_PLUS_1:
			ret = t8ev5_reg_writes(client, t8ev5_saturation_lv4);
			break;
		case SATURATION_PLUS_2:
			ret = t8ev5_reg_writes(client, t8ev5_saturation_lv5);
			break;
		default:
			ret = t8ev5_reg_writes(client,
						t8ev5_saturation_lv3_default);
			break;
		}
		break;

	case V4L2_CID_SHARPNESS:
		if (ctrl->value > SHARPNESS_PLUS_2)
			return -EINVAL;

		t8ev5->sharpness = ctrl->value;
		switch (t8ev5->sharpness) {
		case SHARPNESS_MINUS_2:
			ret = t8ev5_reg_writes(client, t8ev5_sharpness_lv1);
			break;
		case SHARPNESS_MINUS_1:
			ret = t8ev5_reg_writes(client, t8ev5_sharpness_lv2);
			break;
		case SHARPNESS_PLUS_1:
			ret = t8ev5_reg_writes(client, t8ev5_sharpness_lv4);
			break;
		case SHARPNESS_PLUS_2:
			ret = t8ev5_reg_writes(client, t8ev5_sharpness_lv5);
			break;
		default:
			ret = t8ev5_reg_writes(client,
						t8ev5_sharpness_lv3_default);
			break;
		}
		break;

	case V4L2_CID_CAMERA_ANTI_BANDING:
		if (ctrl->value > ANTI_BANDING_OFF)
			return -EINVAL;

		t8ev5->banding = ctrl->value;
		switch (t8ev5->banding) {
		case ANTI_BANDING_50HZ:
			ret = t8ev5_reg_writes(client, t8ev5_banding_50);
			break;
		case ANTI_BANDING_60HZ:
			ret = t8ev5_reg_writes(client, t8ev5_banding_60);
			break;
		case ANTI_BANDING_OFF:
			ret = t8ev5_reg_writes(client, t8ev5_banding_auto);
			break;
		case ANTI_BANDING_AUTO:
		default:
			ret = t8ev5_reg_writes(client, t8ev5_banding_auto);
			break;
		}
		break;

	case V4L2_CID_CAMERA_WHITE_BALANCE:
		if (ctrl->value > WHITE_BALANCE_MAX)
			return -EINVAL;

		t8ev5->wb = ctrl->value;

		switch (t8ev5->wb) {
		case WHITE_BALANCE_FLUORESCENT:
			ret = t8ev5_reg_writes(client, t8ev5_wb_fluorescent);
			break;
		case WHITE_BALANCE_SUNNY:
			ret = t8ev5_reg_writes(client, t8ev5_wb_daylight);
			break;
		case WHITE_BALANCE_CLOUDY:
			ret = t8ev5_reg_writes(client, t8ev5_wb_cloudy);
			break;
		case WHITE_BALANCE_TUNGSTEN:
			ret = t8ev5_reg_writes(client, t8ev5_wb_tungsten);
			break;
		default:
			ret = t8ev5_reg_writes(client, t8ev5_wb_auto);
			break;
		}
		break;

	case V4L2_CID_CAMERA_FOCUS_MODE:
		pr_info("%s: AF mode: %d\n", __func__, ctrl->value);
		/* defined in EosCamera.h
		#define EOS_CAM_FOCUS_MODE_AUTO                 0
		#define EOS_CAM_FOCUS_MODE_CONTINUOUS_PICTURE   1
		#define EOS_CAM_FOCUS_MODE_CONTINUOUS_VIDEO     2
		#define EOS_CAM_FOCUS_MODE_EDOF                 3
		#define EOS_CAM_FOCUS_MODE_FIXED                4
		#define EOS_CAM_FOCUS_MODE_INFINITY             5
		#define EOS_CAM_FOCUS_MODE_MACRO                6
		*/
		if (ctrl->value == t8ev5->focus_mode)
			return 0;
		/* focus mode is changed */
		/* stop CAF if needed */
		if (1 == t8ev5->focus_mode ||
			2 == t8ev5->focus_mode)
			ret = t8ev5_reg_write(client, 0x27FF, 0x37);
		t8ev5->focus_mode = ctrl->value;
		switch (ctrl->value) {
		case 0:
			/* download AF settings */
			ret = t8ev5_reg_writes(client, t8ev5_af_fw);
			break;
		case 1:
		case 2:
			/* download CAF settings */
			ret = t8ev5_reg_writes(client, t8ev5_caf_fw);
			/* start CAF */
			ret = t8ev5_reg_write(client, 0x2401, 0x15);
			ret = t8ev5_reg_write(client, 0x2402, 0x33);
			ret = t8ev5_reg_write(client, 0x27FF, 0xE1);
			ret = t8ev5_reg_write(client, 0x27FF, 0x30);
			break;
		case 5:
			/* infinity */
			ret = t8ev5_reg_write(client, 0x27FF, 0x00);
			break;
		case 6:
			/* macro */
			ret = t8ev5_reg_write(client, 0x27FF, 0x1F);
			break;
		default:
			t8ev5->focus_mode = -1;
			ret = -EINVAL;
		}
		break;

	case V4L2_CID_CAMERA_SET_AUTO_FOCUS:
		if (ctrl->value > AUTO_FOCUS_ON)
			return -EINVAL;
		/* start and stop af cycle here */
		pr_info("%s: AF:%s\n", __func__, ctrl->value ? "ON" : "OFF");
		if (0 == t8ev5->focus_mode) {
			u8 val;
			int retry = 10;
			do {
				t8ev5_reg_read(client, 0x2800, &val);
				if (val & 0x7F)
					break;
				else {
					retry--;
					msleep(20);
				}
			} while (retry > 0);
			switch (ctrl->value) {
			case AUTO_FOCUS_OFF:
				t8ev5_reg_write(client, 0x27ff, 0xf7);
				break;
			case AUTO_FOCUS_ON:
				t8ev5_reg_write(client, 0x27ff, 0x28);
				break;
			}
		}
		break;

	case V4L2_CID_CAMERA_FRAME_RATE:
		break;

	case V4L2_CID_CAM_PREVIEW_ONOFF:
		if (ctrl->value)
			t8ev5->mode = T8EV5_MODE_STREAMING;
		else
			t8ev5->mode = T8EV5_MODE_IDLE;
		break;

	case V4L2_CID_CAM_CAPTURE:
		t8ev5->mode = T8EV5_MODE_CAPTURE;
		break;

	case V4L2_CID_CAM_CAPTURE_DONE:
		t8ev5->mode = T8EV5_MODE_IDLE;
		break;

	case V4L2_CID_PARAMETERS:
		dev_info(&client->dev, "t8ev5 capture parameters\n");
		break;

	case V4L2_CID_CAMERA_FLASH_MODE:
		ret = t8ev5_set_flash_mode(client, ctrl->value);
		break;
	}

	return ret;
}


static long t8ev5_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	int ret = 0;

	switch (cmd) {
	case VIDIOC_THUMB_SUPPORTED:
		{
			int *p = arg;
			*p = 0;	/* no we don't support thumbnail */
			break;
		}
	case VIDIOC_JPEG_G_PACKET_INFO:
		{
			struct v4l2_jpeg_packet_info *p =
			    (struct v4l2_jpeg_packet_info *)arg;
			p->padded = 0;
			p->packet_size = 0x400;
			break;
		}

	case VIDIOC_SENSOR_G_OPTICAL_INFO:
		{
			struct v4l2_sensor_optical_info *p =
			    (struct v4l2_sensor_optical_info *)arg;
			/* assuming 67.5 degree diagonal viewing angle */
			p->hor_angle.numerator = 5401;
			p->hor_angle.denominator = 100;
			p->ver_angle.numerator = 3608;
			p->ver_angle.denominator = 100;
			p->focus_distance[0] = 10;	/*near focus in cm */
			p->focus_distance[1] = 100;	/*optimal focus in cm*/
			p->focus_distance[2] = -1;	/*infinity*/
			p->focal_length.numerator = 342;
			p->focal_length.denominator = 100;
			break;
		}
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static int t8ev5_init(struct i2c_client *client)
{
	struct t8ev5_s *t8ev5 = to_t8ev5(client);
	int ret = 0;

	t8ev5->i_size = T8EV5_SIZE_SXGA;
	t8ev5->i_fmt = 0;	/* First format in the list */
	t8ev5->width = 1280;
	t8ev5->height = 960;
	t8ev5->ev = EV_DEFAULT;
	t8ev5->contrast = CONTRAST_DEFAULT;
	t8ev5->effect = IMAGE_EFFECT_NONE;
	t8ev5->banding = ANTI_BANDING_AUTO;
	t8ev5->wb = WHITE_BALANCE_AUTO;
	t8ev5->focus_mode = -1;
	t8ev5->flash_mode = V4L2_FLASH_LED_MODE_NONE;
	t8ev5->framerate = FRAME_RATE_AUTO;
	t8ev5->inited = 0;
	t8ev5->mode = T8EV5_MODE_IDLE;
	t8ev5->status = 0;
	t8ev5->active_mode = T8EV5_SIZE_SXGA;

	pr_info("Sensor initialized\n");

	return ret;
}

static void t8ev5_video_remove(struct soc_camera_device *icd)
{
	/*dev_dbg(&icd->dev, "Video removed: %p, %p\n",
	icd->dev.parent, icd->vdev);commented because dev is not not
	part soc_camera_device on 3.4 verison*/
}

static struct v4l2_subdev_core_ops t8ev5_subdev_core_ops = {
	.s_power = t8ev5_s_power,
	.g_chip_ident = t8ev5_g_chip_ident,
	.g_ctrl = t8ev5_g_ctrl,
	.s_ctrl = t8ev5_s_ctrl,
	.ioctl = t8ev5_ioctl,
};

static int t8ev5_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct t8ev5_s *priv = to_t8ev5(client);
	struct v4l2_rect *rect = &a->c;

	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rect->top	= 0;
	rect->left	= 0;
	rect->width	= priv->width;
	rect->height	= priv->height;
	dev_info(&client->dev, "%s: width = %d height = %d\n", __func__ ,
						rect->width, rect->height);

	return 0;
}
static int t8ev5_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct t8ev5_s *priv = to_t8ev5(client);

	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= priv->width;
	a->bounds.height		= priv->height;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;
	dev_info(&client->dev, "%s: width = %d height = %d\n", __func__ ,
					a->bounds.width, a->bounds.height);

	return 0;
}

static int t8ev5_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(t8ev5_fmts))
		return -EINVAL;

	*code = t8ev5_fmts[index].code;
	return 0;
}

static int t8ev5_enum_framesizes(struct v4l2_subdev *sd,
				  struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index >= T8EV5_SIZE_LAST)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->pixel_format = V4L2_PIX_FMT_UYVY;

	fsize->discrete = t8ev5_frmsizes[fsize->index];

	return 0;
}

/* we only support fixed frame rate */
static int t8ev5_enum_frameintervals(struct v4l2_subdev *sd,
				      struct v4l2_frmivalenum *interval)
{
	int size;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (interval->index >= 1)
		return -EINVAL;

	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;

	size = t8ev5_find_framesize(interval->width, interval->height);
	switch (size) {
	case T8EV5_SIZE_5MP:
		interval->discrete.numerator = 1;
		interval->discrete.denominator = 5;
		break;
	case T8EV5_SIZE_SXGA:
	default:
		interval->discrete.numerator = 1;
		interval->discrete.denominator = 30;
		break;
	}
	dev_info(&client->dev, "%s: width=%d height=%d fi=%d/%d\n", __func__,
			interval->width,
			interval->height, interval->discrete.numerator,
			interval->discrete.denominator);

	return 0;
}

static int t8ev5_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct t8ev5_s *t8ev5 = to_t8ev5(client);
	struct v4l2_captureparm *cparm;

	if (param->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	cparm = &param->parm.capture;

	memset(param, 0, sizeof(*param));
	param->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	cparm->capability = V4L2_CAP_TIMEPERFRAME;

	switch (t8ev5->i_size) {
	case T8EV5_SIZE_5MP:
		cparm->timeperframe.numerator = 1;
		cparm->timeperframe.denominator = 5;
		break;
	case T8EV5_SIZE_SXGA:
	default:
		cparm->timeperframe.numerator = 1;
		cparm->timeperframe.denominator = 24;
		break;
	}

	return 0;
}

static int t8ev5_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	/*
	 * FIXME: This just enforces the hardcoded framerates until this is
	 * flexible enough.
	 */
	return t8ev5_g_parm(sd, param);
}

static struct v4l2_subdev_video_ops t8ev5_subdev_video_ops = {
	.s_stream = t8ev5_s_stream,
	.s_mbus_fmt = t8ev5_s_fmt,
	.g_mbus_fmt = t8ev5_g_fmt,
	.g_crop		= t8ev5_g_crop,
	.cropcap	= t8ev5_cropcap,
	.try_mbus_fmt = t8ev5_try_fmt,
	.enum_mbus_fmt = t8ev5_enum_fmt,
	.enum_mbus_fsizes = t8ev5_enum_framesizes,
	.enum_framesizes = t8ev5_enum_framesizes,
	.enum_frameintervals = t8ev5_enum_frameintervals,
	.g_parm = t8ev5_g_parm,
	.s_parm = t8ev5_s_parm,
};


static struct v4l2_subdev_ops t8ev5_subdev_ops = {
	.core = &t8ev5_subdev_core_ops,
	.video = &t8ev5_subdev_video_ops,
};

/*
 * Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one
 */
static int t8ev5_video_probe(struct i2c_client *client)
{
	int ret = 0;
	u8 id_high, id_low;
	u16 id;
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);

	pr_info("%s E\n", __func__);
	ret = t8ev5_s_power(subdev, 1);
	if (ret < 0)
		return ret;

	/* Read sensor Model ID */
	ret = t8ev5_reg_read(client, T8EV5_CHIP_ID_HIGH, &id_high);
	if (ret < 0)
		goto out;

	id = id_high << 8;

	ret = t8ev5_reg_read(client, T8EV5_CHIP_ID_LOW, &id_low);
	if (ret < 0)
		goto out;

	id |= id_low;
	pr_info("%s: Read ID: 0x%x\n", __func__, id);

	if (id != T8EV5_CHIP_ID) {
		ret = -ENODEV;
		goto out;
	}

	pr_info("Detected t8ev5 chip 0x%04x\n", id);
out:
	t8ev5_s_power(subdev, 0);
	pr_info("%s X: %d\n", __func__, ret);
	return ret;
}


static int t8ev5_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct t8ev5_s *t8ev5;
	struct soc_camera_link *icl = client->dev.platform_data;
	int ret = 0;

	pr_info("%s E\n", __func__);

	/* input validation */
	if (!icl) {
		dev_err(&client->dev, "t8ev5 driver needs platform data\n");
		return -EINVAL;
	}

	if (!icl->priv) {
		dev_err(&client->dev,
			"t8ev5 driver needs i/f platform data\n");
		return -EINVAL;
	}

	client->addr = T8EV5_CHIP_I2C_ADDR;
	t8ev5 = kzalloc(sizeof(struct t8ev5_s), GFP_KERNEL);
	if (!t8ev5)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&t8ev5->subdev, client, &t8ev5_subdev_ops);
	/* check if sensor is presented */
	ret = t8ev5_video_probe(client);
	if (ret)
		goto finish;

	t8ev5->plat_parms = icl->priv;

	/* init the sensor here */
	ret = t8ev5_init(client);
	if (ret)
		dev_err(&client->dev, "Failed to initialize sensor\n");

finish:
	if (ret != 0)
		kfree(t8ev5);
	pr_info("%s X:%d\n", __func__, ret);
	return ret;
}

static int t8ev5_remove(struct i2c_client *client)
{
	struct t8ev5_s *t8ev5 = to_t8ev5(client);
	struct soc_camera_device *icd = client->dev.platform_data;
	t8ev5_video_remove(icd);
	client->driver = NULL;
	kfree(t8ev5);

	return 0;
}

static const struct i2c_device_id t8ev5_id[] = {
	{"t8ev5", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, t8ev5_id);

static struct i2c_driver t8ev5_i2c_driver = {
	.driver = {
		   .name = "t8ev5",
		   },
	.probe = t8ev5_probe,
	.remove = t8ev5_remove,
	.id_table = t8ev5_id,
};

static int __init t8ev5_mod_init(void)
{
	return i2c_add_driver(&t8ev5_i2c_driver);
}

static void __exit t8ev5_mod_exit(void)
{
	i2c_del_driver(&t8ev5_i2c_driver);
}

module_init(t8ev5_mod_init);
module_exit(t8ev5_mod_exit);

MODULE_DESCRIPTION(" T8EV5 Camera driver");
MODULE_AUTHOR("Jun He <junhe@broadcom.com>");
MODULE_LICENSE("GPL v2");

