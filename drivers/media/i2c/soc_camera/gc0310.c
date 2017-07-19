/*
 * GalaxyCore GC0310 sensor driver
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
#include "gc0310.h"
//++ subcamera id custom_t:peter
#define  gpio_subcamera_id  102
//-- subcamera id custom_t:peter

/* gc0310 has only one fixed colorspace per pixelcode */
struct gc0310_datafmt {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};

static const struct gc0310_datafmt gc0310_fmts[] = {
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


enum gc0310_size {
	GC0310_SIZE_VGA,	/*  640 x 480 */
	GC0310_SIZE_LAST,
};

enum gc0310_mode {
	GC0310_MODE_IDLE = 0,
	GC0310_MODE_STREAMING,
	GC0310_MODE_CAPTURE,
	GC0310_MODE_MAX,
};

static const struct v4l2_frmsize_discrete gc0310_frmsizes[GC0310_SIZE_LAST] = {
	{640, 480},
};

static const struct gc0310_reg gc0310_modes[GC0310_SIZE_LAST][32] = {
	{
		/* VGA */
		{0xfe, 0x03},
		{0x10, 0x90},
		{0xfe, 0x00},
		{GC0310_REG_ESC, 200},
		{GC0310_REG_ESC, 0},
	},
};

/* Power function for gc0310 */
/* FIXME: the rst pin and pwdn pin is board related
 * so such definitions should be removed from here
 * and passed from board_xxx
 */
#define GC0310_RESET_PIN        GPIO_PORT16
#define GC0310_PWDN_PIN         GPIO_PORT91

#define BACKCAM_RESET_PIN       GPIO_PORT20
#define BACKCAM_PWDN_PIN        GPIO_PORT45

int gc0310_power(struct device *dev, int power_on)
{
	struct clk *vclk1_clk, *vclk2_clk;
	int iret;
	struct regulator *regulator;

	vclk1_clk = clk_get(NULL, "vclk1_clk");
	if (IS_ERR(vclk1_clk)) {
		dev_err(dev, "clk_get(vclk1_clk) failed\n");
		return -1;
	}

	vclk2_clk = clk_get(NULL, "vclk2_clk");
	if (IS_ERR(vclk2_clk)) {
		dev_err(dev, "clk_get(vclk2_clk) failed\n");
		return -1;
	}

	if (power_on) {
		pr_info("%s PowerON\n", __func__);

		sh_csi2_power(dev, power_on);

		/* make sure main cam is off */
		gpio_set_value(BACKCAM_RESET_PIN, 0); /* CAM1_RST_N */
		mdelay(10);
		gpio_set_value(BACKCAM_PWDN_PIN, 0); /* CAM1_STBY */
		mdelay(10);
		/* CAM_VDDIO_1V8 On */
		regulator = regulator_get(NULL, "cam_sensor_io");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_enable(regulator);
		regulator_put(regulator);
		udelay(100);


		/* CAM_AVDD_2V8  On */
		regulator = regulator_get(NULL, "cam_sensor_a");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_enable(regulator);
		regulator_put(regulator);
		mdelay(1);

		/* MCLK Sub-Camera */
		iret = clk_set_rate(vclk2_clk,
			clk_round_rate(vclk2_clk, 24000000));
		if (0 != iret) {
			dev_err(dev,
				"clk_set_rate(vclk2_clk) failed (ret=%d)\n",
				iret);
		}

		iret = clk_enable(vclk2_clk);
		if (0 != iret) {
			dev_err(dev, "clk_enable(vclk2_clk) failed (ret=%d)\n",
				iret);
		}
		mdelay(10);
		gpio_set_value(GC0310_RESET_PIN, 1); /* CAM1_RST_N */
		mdelay(10);
		gpio_set_value(GC0310_PWDN_PIN, 0); /* CAM1_STBY */
		mdelay(10);
		pr_info("%s PowerON fin\n", __func__);
	} else {
		pr_info("%s PowerOFF\n", __func__);

		gpio_set_value(GC0310_PWDN_PIN, 1); /* CAM1_STBY */
		mdelay(10);
		gpio_set_value(GC0310_RESET_PIN, 0); /* CAM1_RST_N */
		mdelay(10);

		/* disable MCLK */
		clk_disable(vclk2_clk);
		mdelay(1);

		/* CAM_AVDD_2V8  Off */
		regulator = regulator_get(NULL, "cam_sensor_a");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_disable(regulator);
		regulator_put(regulator);
		mdelay(1);

		/* CAM_VDDIO_1V8 Off */
		regulator = regulator_get(NULL, "cam_sensor_io");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_disable(regulator);
		regulator_put(regulator);
		mdelay(1);

		/* CAM_CORE_1V2  Off */
		sh_csi2_power(dev, power_on);
		pr_info("%s PowerOFF fin\n", __func__);
	}

	clk_put(vclk1_clk);
	clk_put(vclk2_clk);

	return 0;
}

/* Find a data format by a pixel code in an array */
static int gc0310_find_datafmt(enum v4l2_mbus_pixelcode code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gc0310_fmts); i++)
		if (gc0310_fmts[i].code == code)
			break;

	/* If not found, select latest */
	if (i >= ARRAY_SIZE(gc0310_fmts))
		i = ARRAY_SIZE(gc0310_fmts) - 1;

	return i;
}

/* Find a frame size in an array */
static int gc0310_find_framesize(u32 width, u32 height)
{
	int i;

	for (i = 0; i < GC0310_SIZE_LAST; i++) {
		if ((gc0310_frmsizes[i].width >= width) &&
		    (gc0310_frmsizes[i].height >= height))
			break;
	}

	/* If not found, select biggest */
	if (i >= GC0310_SIZE_LAST)
		i = GC0310_SIZE_LAST - 1;

	return i;
}

struct gc0310_s {
	struct v4l2_subdev subdev;
	struct v4l2_subdev_sensor_interface_parms *plat_parms;
	int i_size;
	int i_fmt;
	int ev;
	int effect;
	int contrast;
	int sharpness;
	int saturation;
	int banding;
	int wb;
	int framerate;
	int width;
	int height;
	int inited;
	short mode;
	short status;
};

static struct gc0310_s *to_gc0310(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client),
			struct gc0310_s, subdev);
}

/**
 * gc0310_reg_read - Read a value from a register in gc0310
 * @client: i2c driver client structure
 * @reg: register address / offset
 * @val: stores the value that gets read
 *
 * Read a value from a register in an gc0310 sensor device.
 * The value is returned in 'val'.
 * Returns zero if successful, or non-zero otherwise.
 */
static int gc0310_reg_read(struct i2c_client *client, u8 reg, u8 *val)
{
	int ret;
	u8 data[2] = { 0 };
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 1,
		.buf = data,
	};

	data[0] = reg;

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
 * Write a value to a register in gc0310 sensor device.
 *@client: i2c driver client structure.
 *@reg: Address of the register to read value from.
 *@val: Value to be written to a specific register.
 * Returns zero if successful, or non-zero otherwise.
 */
static int gc0310_reg_write(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;
	unsigned char data[2] = { reg, val };
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
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

static const struct v4l2_queryctrl gc0310_controls[] = {
	{
	 .id = V4L2_CID_CAMERA_BRIGHTNESS,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "EV",
	 .minimum = EV_MINUS_1,
	 .maximum = EV_PLUS_1,
	 .step = 1,
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

};

/**
 * Initialize a list of gc0310 registers.
 * The list of registers is terminated by the pair of values
 * @client: i2c driver client structure.
 * @reglist[]: List of address of the registers to write data.
 * Returns zero if successful, or non-zero otherwise.
 */
static int gc0310_reg_writes(struct i2c_client *client,
			     const struct gc0310_reg reglist[])
{
	int err = 0, index;

	for (index = 0; 0 == err; index++) {
		if (GC0310_REG_ESC != reglist[index].reg)
			err |= gc0310_reg_write(client,
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

#ifdef GC0310_DEBUG
#define iprintk(format, arg...)	\
	pr_info("[%s]: "format"\n", __func__, ##arg)

static int gc0310_reglist_compare(struct i2c_client *client,
				  const struct gc0310_reg reglist[])
{
	int err = 0, index;
	u8 reg;

	for (index = 0; ((reglist[index].reg != 0xFFFF) &&
			(err == 0)); index++) {
		err |= gc0310_reg_read(client, reglist[index].reg, &reg);
		if (reglist[index].val != reg) {
			iprintk("reg err:reg=0x%x val=0x%x rd=0x%x",
			reglist[index].reg, reglist[index].val, reg);
		}
		/*  Check for Pause condition */
		if ((reglist[index + 1].reg == 0xFFFF)
				&& (reglist[index + 1].val != 0)) {
			msleep(reglist[index + 1].val);
			index += 1;
		}
	}
	return 0;
}
#endif

static int gc0310_init(struct i2c_client *client)
{
	struct gc0310_s *gc0310 = to_gc0310(client);
	int ret = 0;

	gc0310->ev = EV_DEFAULT;
	gc0310->contrast = CONTRAST_DEFAULT;
	gc0310->effect = IMAGE_EFFECT_NONE;
	gc0310->banding = ANTI_BANDING_AUTO;
	gc0310->wb = WHITE_BALANCE_AUTO;
	gc0310->framerate = FRAME_RATE_AUTO;
	gc0310->inited = 0;
	gc0310->i_size = GC0310_SIZE_VGA;
	gc0310->i_fmt = 0;	/* First format in the list */
	gc0310->width = 640;
	gc0310->height = 480;

	pr_info("Sensor initialized\n");

	return ret;
}

static int gc0310_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	int ret;

	if (on) {
		ret = soc_camera_power_on(&client->dev, ssdd);
		gc0310_init(client);
	} else {
		ret = soc_camera_power_off(&client->dev, ssdd);
	}

	return ret;
}

static int gc0310_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0310_s *gc0310 = to_gc0310(client);
	pr_info("%s: enable:%d runmode:%d  stream_mode:%d\n",
	       __func__, enable, gc0310->mode, gc0310->status);

	gc0310->status = gc0310->mode;

	return ret;
}

static int gc0310_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0310_s *gc0310 = to_gc0310(client);

	mf->width = gc0310_frmsizes[gc0310->i_size].width;
	mf->height = gc0310_frmsizes[gc0310->i_size].height;
	mf->code = gc0310_fmts[gc0310->i_fmt].code;
	mf->colorspace = gc0310_fmts[gc0310->i_fmt].colorspace;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static int gc0310_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	int i_fmt;
	int i_size;

	i_fmt = gc0310_find_datafmt(mf->code);

	mf->code = gc0310_fmts[i_fmt].code;
	mf->colorspace = gc0310_fmts[i_fmt].colorspace;
	mf->field = V4L2_FIELD_NONE;

	i_size = gc0310_find_framesize(mf->width, mf->height);

	mf->width = gc0310_frmsizes[i_size].width;
	mf->height = gc0310_frmsizes[i_size].height;

	return 0;
}

static int gc0310_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0310_s *gc0310 = to_gc0310(client);
	int ret = 0;

	ret = gc0310_try_fmt(sd, mf);
	if (ret < 0)
		return ret;

	gc0310->i_size = gc0310_find_framesize(mf->width, mf->height);
	gc0310->i_fmt = gc0310_find_datafmt(mf->code);

	if (0 == gc0310->inited) {
		pr_info("%s: download init settings\n", __func__);
		ret = gc0310_reg_writes(client, gc0310_common_init);
		gc0310->inited = 1;
	}
	ret = gc0310_reg_writes(client, gc0310_modes[gc0310->i_size]);

	pr_info("%s: X:%d size:%d\n", __func__, ret, gc0310->i_size);

	return ret;
}

static int gc0310_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	id->ident = V4L2_IDENT_GC0310;
	id->revision = 0;

	return 0;
}

static int gc0310_get_tline(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 val;
	int exp;

	/* set to page 0 */
	gc0310_reg_write(client, 0xfe, 0x00);
	/* read exposure */
	gc0310_reg_read(client, 0x03, &val);
	exp = val & 0x0f; exp <<= 8;
	gc0310_reg_read(client, 0x04, &val);
	exp |= val;
	return exp;
}

static int gc0310_get_integ(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 val;
	int hts;

	/* set to page 0 */
	gc0310_reg_write(client, 0xfe, 0x00);
	/* read window width */
	gc0310_reg_read(client, 0x0f, &val);
	hts = val; hts <<= 8;
	gc0310_reg_read(client, 0x10, &val);
	hts |= val;
	return hts;
}


static int gc0310_get_exp_time(struct v4l2_subdev *sd)
{
	unsigned short val1, val2;
	int etime;
	val1 = gc0310_get_integ(sd);
	val2 = gc0310_get_tline(sd);
	etime = val1 * val2;
	return etime;
}


static int gc0310_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0310_s *gc0310 = to_gc0310(client);

	dev_dbg(&client->dev, "gc0310_g_ctrl\n");

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_BRIGHTNESS:
		ctrl->value = gc0310->ev;
		break;
	case V4L2_CID_CAMERA_CONTRAST:
		ctrl->value = gc0310->contrast;
		break;
	case V4L2_CID_CAMERA_EFFECT:
		ctrl->value = gc0310->effect;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = gc0310->saturation;
		break;
	case V4L2_CID_SHARPNESS:
		ctrl->value = gc0310->sharpness;
		break;
	case V4L2_CID_CAMERA_ANTI_BANDING:
		ctrl->value = gc0310->banding;
		break;
	case V4L2_CID_CAMERA_WHITE_BALANCE:
		ctrl->value = gc0310->wb;
		break;
	case V4L2_CID_CAMERA_FRAME_RATE:
		ctrl->value = gc0310->framerate;
		break;
	case V4L2_CID_CAMERA_EXP_TIME:
		ctrl->value = gc0310_get_exp_time(sd);
		break;
	}

	return 0;
}

static int gc0310_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0310_s *gc0310 = to_gc0310(client);
	int ret = 0;

	dev_dbg(&client->dev, "gc0310_s_ctrl\n");

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_BRIGHTNESS:
		if (ctrl->value > EV_PLUS_4)
			return -EINVAL;

		gc0310->ev = ctrl->value;
		switch (gc0310->ev) {
		case EV_MINUS_4:
			ret = gc0310_reg_writes(client, gc0310_ev_lv1);
			break;
		case EV_MINUS_2:
			ret = gc0310_reg_writes(client, gc0310_ev_lv2);
			break;
		case EV_PLUS_2:
			ret = gc0310_reg_writes(client, gc0310_ev_lv4);
			break;
		case EV_PLUS_4:
			ret = gc0310_reg_writes(client, gc0310_ev_lv5);
			break;
		default:
			ret = gc0310_reg_writes(client, gc0310_ev_lv3_default);
			break;
		}
		break;

	case V4L2_CID_CAMERA_CONTRAST:
		if (ctrl->value > CONTRAST_PLUS_2)
			return -EINVAL;

		gc0310->contrast = ctrl->value;
		switch (gc0310->contrast) {
		case CONTRAST_MINUS_2:
			ret = gc0310_reg_writes(client, gc0310_contrast_lv1);
			break;
		case CONTRAST_MINUS_1:
			ret = gc0310_reg_writes(client, gc0310_contrast_lv2);
			break;
		case CONTRAST_PLUS_1:
			ret = gc0310_reg_writes(client, gc0310_contrast_lv4);
			break;
		case CONTRAST_PLUS_2:
			ret = gc0310_reg_writes(client, gc0310_contrast_lv5);
			break;
		default:
			ret = gc0310_reg_writes(client,
						gc0310_contrast_lv3_default);
			break;
		}
		break;

	case V4L2_CID_CAMERA_EFFECT:
		if (ctrl->value > IMAGE_EFFECT_BNW)
			return -EINVAL;

		gc0310->effect = ctrl->value;

		switch (gc0310->effect) {
		case IMAGE_EFFECT_MONO:
			ret = gc0310_reg_writes(client, gc0310_effect_bw);
			break;
		case IMAGE_EFFECT_SEPIA:
			ret = gc0310_reg_writes(client,
						gc0310_effect_sepia);
			break;
		case IMAGE_EFFECT_NEGATIVE:
			ret = gc0310_reg_writes(client,
						gc0310_effect_negative);
			break;
		default:
			ret = gc0310_reg_writes(client,
						gc0310_effect_no);
			break;
		}
		break;

	case V4L2_CID_SATURATION:
		if (ctrl->value > SATURATION_PLUS_2)
			return -EINVAL;

		gc0310->saturation = ctrl->value;
		switch (gc0310->saturation) {
		case SATURATION_MINUS_2:
			ret = gc0310_reg_writes(client, gc0310_saturation_lv1);
			break;
		case SATURATION_MINUS_1:
			ret = gc0310_reg_writes(client, gc0310_saturation_lv2);
			break;
		case SATURATION_PLUS_1:
			ret = gc0310_reg_writes(client, gc0310_saturation_lv4);
			break;
		case SATURATION_PLUS_2:
			ret = gc0310_reg_writes(client, gc0310_saturation_lv5);
			break;
		default:
			ret = gc0310_reg_writes(client,
						gc0310_saturation_lv3_default);
			break;
		}
		break;

	case V4L2_CID_SHARPNESS:
		if (ctrl->value > SHARPNESS_PLUS_2)
			return -EINVAL;

		gc0310->sharpness = ctrl->value;
		switch (gc0310->sharpness) {
		case SHARPNESS_MINUS_2:
			ret = gc0310_reg_writes(client, gc0310_sharpness_lv1);
			break;
		case SHARPNESS_MINUS_1:
			ret = gc0310_reg_writes(client, gc0310_sharpness_lv2);
			break;
		case SHARPNESS_PLUS_1:
			ret = gc0310_reg_writes(client, gc0310_sharpness_lv4);
			break;
		case SHARPNESS_PLUS_2:
			ret = gc0310_reg_writes(client, gc0310_sharpness_lv5);
			break;
		default:
			ret = gc0310_reg_writes(client,
						gc0310_sharpness_lv3_default);
			break;
		}
		break;

	case V4L2_CID_CAMERA_ANTI_BANDING:
		if (ctrl->value > ANTI_BANDING_OFF)
			return -EINVAL;

		gc0310->banding = ctrl->value;
		switch (gc0310->banding) {
		case ANTI_BANDING_50HZ:
			ret = gc0310_reg_writes(client, gc0310_banding_50);
			break;
		case ANTI_BANDING_60HZ:
			ret = gc0310_reg_writes(client, gc0310_banding_60);
			break;
		case ANTI_BANDING_OFF:
			ret = gc0310_reg_writes(client, gc0310_banding_auto);
			break;
		case ANTI_BANDING_AUTO:
		default:
			ret = gc0310_reg_writes(client, gc0310_banding_auto);
			break;
		}
		break;

	case V4L2_CID_CAMERA_WHITE_BALANCE:
		if (ctrl->value > WHITE_BALANCE_MAX)
			return -EINVAL;

		gc0310->wb = ctrl->value;

		switch (gc0310->wb) {
		case WHITE_BALANCE_FLUORESCENT:
			ret = gc0310_reg_writes(client, gc0310_wb_fluorescent);
			break;
		case WHITE_BALANCE_SUNNY:
			ret = gc0310_reg_writes(client, gc0310_wb_daylight);
			break;
		case WHITE_BALANCE_CLOUDY:
			ret = gc0310_reg_writes(client, gc0310_wb_cloudy);
			break;
		case WHITE_BALANCE_TUNGSTEN:
			ret = gc0310_reg_writes(client, gc0310_wb_tungsten);
			break;
		default:
			ret = gc0310_reg_writes(client, gc0310_wb_auto);
			break;
		}
		break;

	case V4L2_CID_CAMERA_FRAME_RATE:

		break;


	case V4L2_CID_CAM_PREVIEW_ONOFF:
		{
			pr_info(
			       "gc0310 PREVIEW_ONOFF:%d runmode = %d\n",
			       ctrl->value, gc0310->mode);
			if (ctrl->value)
				gc0310->mode = GC0310_MODE_STREAMING;
			else
				gc0310->mode = GC0310_MODE_IDLE;

			break;
		}

	case V4L2_CID_CAM_CAPTURE:
		gc0310->mode = GC0310_MODE_CAPTURE;
		break;

	case V4L2_CID_CAM_CAPTURE_DONE:

		gc0310->mode = GC0310_MODE_IDLE;
		break;

	case V4L2_CID_PARAMETERS:
		dev_info(&client->dev, "gc0310 capture parameters\n");
		break;
	}

	return ret;
}


static long gc0310_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
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

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int gc0310_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->size > 1)
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	reg->size = 1;
	if (gc0310_reg_read(client, reg->reg, &reg->val))
		return -EIO return 0;

}

static int gc0310_s_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->size > 1)
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	if (gc0310_reg_write(client, reg->reg, reg->val))
		return -EIO;


	return 0;
}
#endif

static void gc0310_video_remove(struct soc_camera_device *icd)
{
	/*dev_dbg(&icd->dev, "Video removed: %p, %p\n",
	icd->dev.parent, icd->vdev);commented because dev is not not
	part soc_camera_device on 3.4 verison*/
}

static int gc0310_queryctrl(struct v4l2_subdev *sd,
		struct v4l2_queryctrl *qc)
{
	int index = 0;
	for (index = 0; (index) < (ARRAY_SIZE(gc0310_controls)); index++) {
		if ((qc->id) == (gc0310_controls[index].id)) {
			qc->type = gc0310_controls[index].type;
			qc->minimum = gc0310_controls[index].minimum;
			qc->maximum = gc0310_controls[index].maximum;
			qc->step = gc0310_controls[index].step;
			qc->default_value =
				gc0310_controls[index].default_value;
			qc->flags = gc0310_controls[index].flags;
			strlcpy(qc->name, gc0310_controls[index].name,
			sizeof(qc->name));
			return 0;
		}
	}
	return -EINVAL;
}

static struct v4l2_subdev_core_ops gc0310_subdev_core_ops = {
	.s_power = gc0310_s_power,
	.g_chip_ident = gc0310_g_chip_ident,
	.g_ctrl = gc0310_g_ctrl,
	.s_ctrl = gc0310_s_ctrl,
	.ioctl = gc0310_ioctl,
	.queryctrl = gc0310_queryctrl,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = gc0310_g_register,
	.s_register = gc0310_s_register,
#endif
};

static int gc0310_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(gc0310_fmts))
		return -EINVAL;

	*code = gc0310_fmts[index].code;
	return 0;
}

static int gc0310_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0310_s *priv = to_gc0310(client);
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
static int gc0310_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0310_s *priv = to_gc0310(client);

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

static int gc0310_enum_framesizes(struct v4l2_subdev *sd,
				  struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index >= GC0310_SIZE_LAST)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->pixel_format = V4L2_PIX_FMT_UYVY;

	fsize->discrete = gc0310_frmsizes[fsize->index];

	return 0;
}

/* we only support fixed frame rate */
static int gc0310_enum_frameintervals(struct v4l2_subdev *sd,
				      struct v4l2_frmivalenum *interval)
{
	int size;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (interval->index >= 1)
		return -EINVAL;

	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;

	size = gc0310_find_framesize(interval->width, interval->height);
	switch (size) {
	case GC0310_SIZE_VGA:
	default:
		interval->discrete.numerator = 1;
		interval->discrete.denominator = 24;
		break;
	}
	dev_info(&client->dev, "%s: width=%d height=%d fi=%d/%d\n", __func__,
			interval->width,
			interval->height, interval->discrete.numerator,
			interval->discrete.denominator);

	return 0;
}

static int gc0310_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gc0310_s *gc0310 = to_gc0310(client);
	struct v4l2_captureparm *cparm;

	if (param->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	cparm = &param->parm.capture;

	memset(param, 0, sizeof(*param));
	param->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	cparm->capability = V4L2_CAP_TIMEPERFRAME;

	switch (gc0310->i_size) {
	case GC0310_SIZE_VGA:
	default:
		cparm->timeperframe.numerator = 1;
		cparm->timeperframe.denominator = 24;
		break;
	}

	return 0;
}

static int gc0310_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	/*
	 * FIXME: This just enforces the hardcoded framerates until this is
	 * flexible enough.
	 */
	return gc0310_g_parm(sd, param);
}

static struct v4l2_subdev_video_ops gc0310_subdev_video_ops = {
	.s_stream = gc0310_s_stream,
	.s_mbus_fmt = gc0310_s_fmt,
	.g_mbus_fmt = gc0310_g_fmt,
	.g_crop		= gc0310_g_crop,
	.cropcap	= gc0310_cropcap,
	.try_mbus_fmt = gc0310_try_fmt,
	.enum_mbus_fmt = gc0310_enum_fmt,
	.enum_mbus_fsizes = gc0310_enum_framesizes,
	.enum_framesizes = gc0310_enum_framesizes,
	.enum_frameintervals = gc0310_enum_frameintervals,
	.g_parm = gc0310_g_parm,
	.s_parm = gc0310_s_parm,
};

static int gc0310_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	/* Quantity of initial bad frames to skip. Revisit. */
	/* Waitting for AWB stability, avoid green color issue */
	*frames = 5;

	return 0;
}

static struct v4l2_subdev_sensor_ops gc0310_subdev_sensor_ops = {
	.g_skip_frames = gc0310_g_skip_frames,
};

static struct v4l2_subdev_ops gc0310_subdev_ops = {
	.core = &gc0310_subdev_core_ops,
	.video = &gc0310_subdev_video_ops,
	.sensor = &gc0310_subdev_sensor_ops,
};

//++  camera sys by custom_t sw
static struct kobject *android_camera_kobj;
static char * camera_vendor =NULL;

static ssize_t android_front_camera_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;	
	sprintf(buf, "%s\n", camera_vendor);
	ret = strlen(buf)+1 ;
	return ret;
}

static DEVICE_ATTR(front_camera_info, S_IRUGO, android_front_camera_show, NULL);
static int android_camera_sysfs_init(void)
{
	int ret ;

	android_camera_kobj = kobject_create_and_add("android_front_camera", NULL) ;
	if (android_camera_kobj == NULL) {
		printk(KERN_ERR "[camera]%s: subsystem_register failed\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
	ret = sysfs_create_file(android_camera_kobj, &dev_attr_front_camera_info.attr);
	if (ret) {
		printk(KERN_ERR "[camera]%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	return 0;

}

static void  android_camera_sysfs_deinit(void)
{
	sysfs_remove_file(android_camera_kobj, &dev_attr_front_camera_info.attr);
	kobject_del(android_camera_kobj);
}	

//-- camera sys by custom_t sw
//++  module recognition  custom_t:peter
void cmk_module_init(void)
{
	printk("%s\n", __func__);
	gc0310_common_init =  cmk_gc0310_common_init;
}

void blx_module_init(void)
{	
	printk("%s\n", __func__);
	gc0310_common_init =  blx_gc0310_common_init;
}
//--  module recognition  custom_t:peter
/*
 * Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one
 */
static int gc0310_video_probe(struct i2c_client *client)
{
	int ret = 0;
	u8 id_high, id_low;
	u16 id;
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
//++ subcamera id custom_t:peter
	int status;
//-- subcamera id custom_t:peter

	pr_info("%s E\n", __func__);
	ret = gc0310_s_power(subdev, 1);
	if (ret < 0)
		return ret;

	/* Read sensor Model ID */
	ret = gc0310_reg_read(client, GC0310_CHIP_ID_HIGH, &id_high);
	if (ret < 0)
		goto out;

	id = id_high << 8;

	ret = gc0310_reg_read(client, GC0310_CHIP_ID_LOW, &id_low);
	if (ret < 0)
		goto out;

	id |= id_low;
	pr_info("%s: Read ID: 0x%x\n", __func__, id);

	if (id != GC0310_CHIP_ID) {
		ret = -ENODEV;
		goto out;
	}

	pr_info("Detected gc0310 chip 0x%04x\n", id);
//++ subcamera id custom_t:peter
	ret = gpio_request(gpio_subcamera_id, "sub_camera_id");
	if (ret) {
		pr_err("%s: Failed to get intr gpio. Code: %d.",  __func__, ret);
		goto out;
	}
	ret = gpio_direction_input(gpio_subcamera_id);
	if (ret) {
		pr_err("%s: Failed to setup intr gpio. Code: %d.",  __func__, ret);
		gpio_free(gpio_subcamera_id);
		goto out;
	}
	
	status = gpio_get_value(gpio_subcamera_id);
	printk("%s: status = %d,gpio_subcamera_id = %d \n", __func__, status,gpio_subcamera_id);
	if(status == 0)
	{
		camera_vendor = "GC0310-CMK";
	//++  module recognition  custom_t:peter
		cmk_module_init();
	//--  module recognition  custom_t:peter
	}
	if(status == 1)
	{
		camera_vendor = "GC0310-BLX";
		//++  module recognition  custom_t:peter
		blx_module_init();
		//--  module recognition  custom_t:peter
	}
	printk("%s:  %s\n", __func__,camera_vendor);
//-- subcamera id custom_t:peter			
out:
	gc0310_s_power(subdev, 0);
	pr_info("%s X: %d\n", __func__, ret);
	return ret;
}

static int gc0310_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct gc0310_s *gc0310;
	struct soc_camera_link *icl = client->dev.platform_data;
	int ret = 0;

	pr_info("%s E\n", __func__);

	client->addr = GC0310_CHIP_I2C_ADDR;
	if (!icl) {
		dev_err(&client->dev, "gc0310 driver needs platform data\n");
		return -EINVAL;
	}

	if (!icl->priv) {
		dev_err(&client->dev,
			"gc0310 driver needs i/f platform data\n");
		return -EINVAL;
	}

	gc0310 = kzalloc(sizeof(struct gc0310_s), GFP_KERNEL);
	if (!gc0310)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&gc0310->subdev, client, &gc0310_subdev_ops);
	/* check if sensor is presented */
	ret = gc0310_video_probe(client);
	if (ret)
		goto finish;
//++ camera sys by custom_t sw	
	android_camera_sysfs_init();
//-- camera sys by custom_t sw

	gc0310->plat_parms = icl->priv;

	/* init the sensor here */
	ret = gc0310_init(client);
	if (ret) {
		dev_err(&client->dev, "Failed to initialize sensor\n");
		ret = -EINVAL;
	}

finish:
	if (ret != 0)
		kfree(gc0310);
	pr_info("%s X:%d\n", __func__, ret);
	return ret;
}

static int gc0310_remove(struct i2c_client *client)
{
	struct gc0310_s *gc0310 = to_gc0310(client);
	struct soc_camera_device *icd = client->dev.platform_data;
//++ camera sys by custom_t sw
	android_camera_sysfs_deinit();
//-- camera sys by custom_t sw
	gc0310_video_remove(icd);
	client->driver = NULL;
	kfree(gc0310);

	return 0;
}


static const struct i2c_device_id gc0310_id[] = {
	{"gc0310", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, gc0310_id);

static struct i2c_driver gc0310_i2c_driver = {
	.driver = {
		   .name = "gc0310",
		   },
	.probe = gc0310_probe,
	.remove = gc0310_remove,
	.id_table = gc0310_id,
};

static int __init gc0310_mod_init(void)
{
	return i2c_add_driver(&gc0310_i2c_driver);
}

static void __exit gc0310_mod_exit(void)
{
	i2c_del_driver(&gc0310_i2c_driver);
}

module_init(gc0310_mod_init);
module_exit(gc0310_mod_exit);

MODULE_DESCRIPTION(" GC0310 Camera driver");
MODULE_AUTHOR("Jun He <junhe@broadcom.com>");
MODULE_LICENSE("GPL v2");
