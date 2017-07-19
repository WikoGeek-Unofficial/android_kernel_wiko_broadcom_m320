/*
 * SuperPix SP1628 sensor driver
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
#include "sp1628.h"


/* sp1628 has only one fixed colorspace per pixelcode */
struct sp1628_datafmt {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};

static const struct sp1628_datafmt sp1628_fmts[] = {
	/*
	 * Order important: first natively supported,
	 * second supported with a GPIO extender
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

enum sp1628_size {
	SP1628_SIZE_720P,	/*  1280 x 720 */
	SP1628_SIZE_LAST,
	SP1628_SIZE_MAX
};

enum sp1628_mode {
	SP1628_MODE_IDLE = 0,
	SP1628_MODE_STREAMING,
	SP1628_MODE_CAPTURE,
	SP1628_MODE_MAX,
};

static const struct v4l2_frmsize_discrete sp1628_frmsizes[SP1628_SIZE_LAST] = {
	{1280, 720},
};

static const struct sp1628_reg sp1628_modes[SP1628_SIZE_LAST][64] = {
	{
		/* 720P */
		{SP1628_REG_ESC, 0},
	},
};

/* Power function for sp1628 */
/* FIXME: the rst pin and pwdn pin is board related
 * so such definitions should be removed from here
 * and passed from board_xxx
 */
#define SP1628_RESET_PIN        GPIO_PORT16
#define SP1628_PWDN_PIN         GPIO_PORT91

int sp1628_power(struct device *dev, int power_on)
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

		/* CAM_AVDD_2V8  On */
		regulator = regulator_get(NULL, "cam_sensor_a");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_enable(regulator);
		regulator_put(regulator);
		mdelay(1);

		/* CAM_VDDIO_1V8 On */
		gpio_set_value(GPIO_PORT8, 1); /* CAM0_DVDD_PWR_EN */
		mdelay(5);

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
		gpio_set_value(SP1628_RESET_PIN, 1); /* CAM1_RST_N */
		mdelay(10);
		gpio_set_value(SP1628_PWDN_PIN, 0); /* CAM1_STBY */
		mdelay(5);

		pr_info("%s PowerON fin\n", __func__);
	} else {
		pr_info("%s PowerOFF\n", __func__);

		gpio_set_value(SP1628_PWDN_PIN, 1); /* CAM1_STBY */
		mdelay(2);

		gpio_set_value(SP1628_RESET_PIN, 0); /* CAM1_RST_N */
		mdelay(1);

		clk_disable(vclk2_clk);
		mdelay(1);

		/* CAM_VDDIO_1V8 Off */
		gpio_set_value(GPIO_PORT8, 0); /* CAM0_DVDD_PWR_EN */
		mdelay(5);

		/* CAM_AVDD_2V8  Off */
		regulator = regulator_get(NULL, "cam_sensor_a");
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
static int sp1628_find_datafmt(enum v4l2_mbus_pixelcode code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sp1628_fmts); i++)
		if (sp1628_fmts[i].code == code)
			break;

	/* If not found, select latest */
	if (i >= ARRAY_SIZE(sp1628_fmts))
		i = ARRAY_SIZE(sp1628_fmts) - 1;

	return i;
}

/* Find a frame size in an array */
static int sp1628_find_framesize(u32 width, u32 height)
{
	int i;

	for (i = 0; i < SP1628_SIZE_LAST; i++) {
		if ((sp1628_frmsizes[i].width >= width) &&
		    (sp1628_frmsizes[i].height >= height))
			break;
	}

	/* If not found, select biggest */
	if (i >= SP1628_SIZE_LAST)
		i = SP1628_SIZE_LAST - 1;

	return i;
}

struct sp1628_s {
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
	short inited;
	short mode;
	short status;
	short active_mode;
};

static struct sp1628_s *to_sp1628(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client),
			struct sp1628_s, subdev);
}

/**
 * sp1628_reg_read - Read a value from a register in sp1628
 * @client: i2c driver client structure
 * @reg: register address / offset
 * @val: stores the value that gets read
 *
 * Read a value from a register in an sp1628 sensor device.
 * The value is returned in 'val'.
 * Returns zero if successful, or non-zero otherwise.
 */
static int sp1628_reg_read(struct i2c_client *client, u8 reg, u8 *val)
{
	int ret;
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret >= 0) {
		*val = (unsigned char)ret;
		ret = 0;
	} else
		dev_err(&client->dev, "read [0x%02x] failed!\n", reg);

	return ret;
}

/**
 * Write a value to a register in sp1628 sensor device.
 *@client: i2c driver client structure.
 *@reg: Address of the register to read value from.
 *@val: Value to be written to a specific register.
 * Returns zero if successful, or non-zero otherwise.
 */
static int sp1628_reg_write(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;
	unsigned char data[2] = { (u8) (reg & 0xff), val };
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
		.buf = data,
	};

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		pr_info("%s: write fail [0x%02x]:0x%x!\n", __func__, reg, val);
		return ret;
	}

	return 0;
}



static const struct v4l2_queryctrl sp1628_controls[] = {
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
 * Initialize a list of sp1628 registers.
 * The list of registers is terminated by the pair of values
 * @client: i2c driver client structure.
 * @reglist[]: List of address of the registers to write data.
 * Returns zero if successful, or non-zero otherwise.
 */
static int sp1628_reg_writes(struct i2c_client *client,
			     const struct sp1628_reg reglist[])
{
	int err = 0, index;

	for (index = 0; 0 == err; index++) {
		if (SP1628_REG_ESC != reglist[index].reg)
			err |= sp1628_reg_write(client,
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

#ifdef SP1628_DEBUG
#define iprintk(format, arg...)	\
	pr_info("[%s]: "format"\n", __func__, ##arg)

static int sp1628_reglist_compare(struct i2c_client *client,
				  const struct sp1628_reg reglist[])
{
	int err = 0, index;
	u8 reg;

	for (index = 0; ((reglist[index].reg != 0xFFFF) &&
			(err == 0)); index++) {
		err |= sp1628_reg_read(client, reglist[index].reg, &reg);
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
static int sp1628_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	struct sp1628_s *sp1628 = to_sp1628(client);
	int ret = 0;

	if (on)
		ret = soc_camera_power_on(&client->dev, ssdd);
	else {
		ret = soc_camera_power_off(&client->dev, ssdd);
		sp1628->inited = 0;
		sp1628->active_mode = SP1628_SIZE_LAST;
	}

	return ret;
}

static int sp1628_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sp1628_s *sp1628 = to_sp1628(client);
	pr_info("%s: enable:%d mode:%d  status:%d\n",
	       __func__, enable, sp1628->mode, sp1628->status);

	if (sp1628->mode == sp1628->status)
		return ret;

	if (sp1628->mode) {
		/* download common init */
		if (0 == sp1628->inited) {
			pr_info("%s: download init settings\n", __func__);
			sp1628_reg_writes(client, sp1628_common_init);
			sp1628->inited = 1;
		}
		/* download mode */
		pr_info("%s: download mode[%d] settings\n",
			__func__, sp1628->i_size);
		sp1628_reg_writes(client, sp1628_modes[sp1628->i_size]);
		/* start stream*/
		pr_info("%s: start streaming\n", __func__);
		ret = sp1628_reg_writes(client, sp1628_stream_on);
	} else {
		pr_info("%s: stop streaming\n", __func__);
		ret = sp1628_reg_writes(client, sp1628_stream_off);
	}

	sp1628->status = sp1628->mode;

	return ret;
}

static int sp1628_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sp1628_s *sp1628 = to_sp1628(client);

	mf->width = sp1628_frmsizes[sp1628->i_size].width;
	mf->height = sp1628_frmsizes[sp1628->i_size].height;
	mf->code = sp1628_fmts[sp1628->i_fmt].code;
	mf->colorspace = sp1628_fmts[sp1628->i_fmt].colorspace;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static int sp1628_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	int i_fmt;
	int i_size;

	i_fmt = sp1628_find_datafmt(mf->code);

	mf->code = sp1628_fmts[i_fmt].code;
	mf->colorspace = sp1628_fmts[i_fmt].colorspace;
	mf->field = V4L2_FIELD_NONE;

	i_size = sp1628_find_framesize(mf->width, mf->height);

	mf->width = sp1628_frmsizes[i_size].width;
	mf->height = sp1628_frmsizes[i_size].height;

	return 0;
}

static int sp1628_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sp1628_s *sp1628 = to_sp1628(client);
	int ret = 0;

	ret = sp1628_try_fmt(sd, mf);
	if (ret < 0)
		return ret;

	sp1628->i_size = sp1628_find_framesize(mf->width, mf->height);
	sp1628->i_fmt = sp1628_find_datafmt(mf->code);
	pr_info("%s: select mode:%d\n", __func__, sp1628->i_size);

	return ret;
}

static int sp1628_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	id->ident = V4L2_IDENT_SP1628;
	id->revision = 0;

	return 0;
}

static int sp1628_get_tline(struct v4l2_subdev *sd)
{
	return 0;
}

static int sp1628_get_integ(struct v4l2_subdev *sd)
{
	return 0;
}


static int sp1628_get_exp_time(struct v4l2_subdev *sd)
{
	unsigned short val1, val2, etime;
	val1 = sp1628_get_integ(sd);
	val2 = sp1628_get_tline(sd);
	etime = val1 * val2;
	return etime;
}


static int sp1628_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sp1628_s *sp1628 = to_sp1628(client);

	dev_dbg(&client->dev, "sp1628_g_ctrl\n");

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_BRIGHTNESS:
		ctrl->value = sp1628->ev;
		break;
	case V4L2_CID_CAMERA_CONTRAST:
		ctrl->value = sp1628->contrast;
		break;
	case V4L2_CID_CAMERA_EFFECT:
		ctrl->value = sp1628->effect;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = sp1628->saturation;
		break;
	case V4L2_CID_SHARPNESS:
		ctrl->value = sp1628->sharpness;
		break;
	case V4L2_CID_CAMERA_ANTI_BANDING:
		ctrl->value = sp1628->banding;
		break;
	case V4L2_CID_CAMERA_WHITE_BALANCE:
		ctrl->value = sp1628->wb;
		break;
	case V4L2_CID_CAMERA_FRAME_RATE:
		ctrl->value = sp1628->framerate;
		break;
	case V4L2_CID_CAMERA_EXP_TIME:
		ctrl->value = sp1628_get_exp_time(sd);
		break;
	}

	return 0;
}

static int sp1628_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sp1628_s *sp1628 = to_sp1628(client);
	int ret = 0;

	dev_dbg(&client->dev, "sp1628_s_ctrl\n");

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_BRIGHTNESS:
		if (ctrl->value > EV_PLUS_4)
			return -EINVAL;

		sp1628->ev = ctrl->value;
		switch (sp1628->ev) {
		case EV_MINUS_4:
			ret = sp1628_reg_writes(client, sp1628_ev_lv1);
			break;
		case EV_MINUS_2:
			ret = sp1628_reg_writes(client, sp1628_ev_lv2);
			break;
		case EV_PLUS_2:
			ret = sp1628_reg_writes(client, sp1628_ev_lv4);
			break;
		case EV_PLUS_4:
			ret = sp1628_reg_writes(client, sp1628_ev_lv5);
			break;
		default:
			ret = sp1628_reg_writes(client, sp1628_ev_lv3_default);
			break;
		}
		break;

	case V4L2_CID_CAMERA_CONTRAST:
		if (ctrl->value > CONTRAST_PLUS_2)
			return -EINVAL;

		sp1628->contrast = ctrl->value;
		switch (sp1628->contrast) {
		case CONTRAST_MINUS_2:
			ret = sp1628_reg_writes(client, sp1628_contrast_lv1);
			break;
		case CONTRAST_MINUS_1:
			ret = sp1628_reg_writes(client, sp1628_contrast_lv2);
			break;
		case CONTRAST_PLUS_1:
			ret = sp1628_reg_writes(client, sp1628_contrast_lv4);
			break;
		case CONTRAST_PLUS_2:
			ret = sp1628_reg_writes(client, sp1628_contrast_lv5);
			break;
		default:
			ret = sp1628_reg_writes(client,
						sp1628_contrast_lv3_default);
			break;
		}
		break;

	case V4L2_CID_CAMERA_EFFECT:
		if (ctrl->value > IMAGE_EFFECT_BNW)
			return -EINVAL;

		sp1628->effect = ctrl->value;

		switch (sp1628->effect) {
		case IMAGE_EFFECT_MONO:
			ret = sp1628_reg_writes(client, sp1628_effect_bw);
			break;
		case IMAGE_EFFECT_SEPIA:
			ret = sp1628_reg_writes(client,
						sp1628_effect_sepia);
			break;
		case IMAGE_EFFECT_NEGATIVE:
			ret = sp1628_reg_writes(client,
						sp1628_effect_negative);
			break;
		default:
			ret = sp1628_reg_writes(client,
						sp1628_effect_no);
			break;
		}
		break;

	case V4L2_CID_SATURATION:
		if (ctrl->value > SATURATION_PLUS_2)
			return -EINVAL;

		sp1628->saturation = ctrl->value;
		switch (sp1628->saturation) {
		case SATURATION_MINUS_2:
			ret = sp1628_reg_writes(client, sp1628_saturation_lv1);
			break;
		case SATURATION_MINUS_1:
			ret = sp1628_reg_writes(client, sp1628_saturation_lv2);
			break;
		case SATURATION_PLUS_1:
			ret = sp1628_reg_writes(client, sp1628_saturation_lv4);
			break;
		case SATURATION_PLUS_2:
			ret = sp1628_reg_writes(client, sp1628_saturation_lv5);
			break;
		default:
			ret = sp1628_reg_writes(client,
						sp1628_saturation_lv3_default);
			break;
		}
		break;

	case V4L2_CID_SHARPNESS:
		if (ctrl->value > SHARPNESS_PLUS_2)
			return -EINVAL;

		sp1628->sharpness = ctrl->value;
		switch (sp1628->sharpness) {
		case SHARPNESS_MINUS_2:
			ret = sp1628_reg_writes(client, sp1628_sharpness_lv1);
			break;
		case SHARPNESS_MINUS_1:
			ret = sp1628_reg_writes(client, sp1628_sharpness_lv2);
			break;
		case SHARPNESS_PLUS_1:
			ret = sp1628_reg_writes(client, sp1628_sharpness_lv4);
			break;
		case SHARPNESS_PLUS_2:
			ret = sp1628_reg_writes(client, sp1628_sharpness_lv5);
			break;
		default:
			ret = sp1628_reg_writes(client,
						sp1628_sharpness_lv3_default);
			break;
		}
		break;

	case V4L2_CID_CAMERA_ANTI_BANDING:
		if (ctrl->value > ANTI_BANDING_OFF)
			return -EINVAL;

		sp1628->banding = ctrl->value;
		switch (sp1628->banding) {
		case ANTI_BANDING_50HZ:
			ret = sp1628_reg_writes(client, sp1628_banding_50);
			break;
		case ANTI_BANDING_60HZ:
			ret = sp1628_reg_writes(client, sp1628_banding_60);
			break;
		case ANTI_BANDING_OFF:
			ret = sp1628_reg_writes(client, sp1628_banding_auto);
			break;
		case ANTI_BANDING_AUTO:
		default:
			ret = sp1628_reg_writes(client, sp1628_banding_auto);
			break;
		}
		break;

	case V4L2_CID_CAMERA_WHITE_BALANCE:
		if (ctrl->value > WHITE_BALANCE_MAX)
			return -EINVAL;

		sp1628->wb = ctrl->value;

		switch (sp1628->wb) {
		case WHITE_BALANCE_FLUORESCENT:
			ret = sp1628_reg_writes(client, sp1628_wb_fluorescent);
			break;
		case WHITE_BALANCE_SUNNY:
			ret = sp1628_reg_writes(client, sp1628_wb_daylight);
			break;
		case WHITE_BALANCE_CLOUDY:
			ret = sp1628_reg_writes(client, sp1628_wb_cloudy);
			break;
		case WHITE_BALANCE_TUNGSTEN:
			ret = sp1628_reg_writes(client, sp1628_wb_tungsten);
			break;
		default:
			ret = sp1628_reg_writes(client, sp1628_wb_auto);
			break;
		}
		break;

	case V4L2_CID_CAMERA_FRAME_RATE:

		break;


	case V4L2_CID_CAM_PREVIEW_ONOFF:
		{
			pr_info(
			       "%s PREVIEW_ONOFF:%d runmode = %d\n",
			       __func__, ctrl->value, sp1628->mode);
			if (ctrl->value)
				sp1628->mode = SP1628_MODE_STREAMING;
			else
				sp1628->mode = SP1628_MODE_IDLE;

			break;
		}

	case V4L2_CID_CAM_CAPTURE:
		sp1628->mode = SP1628_MODE_CAPTURE;
		break;

	case V4L2_CID_CAM_CAPTURE_DONE:
		sp1628->mode = SP1628_MODE_IDLE;
		break;

	case V4L2_CID_PARAMETERS:
		dev_info(&client->dev, "sp1628 capture parameters\n");
		break;
	}

	return ret;
}


static long sp1628_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
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
static int sp1628_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->size > 1)
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	reg->size = 1;
	if (sp1628_reg_read(client, reg->reg, &reg->val))
		return -EIO return 0;

}

static int sp1628_s_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR || reg->size > 1)
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	if (sp1628_reg_write(client, reg->reg, reg->val))
		return -EIO;


	return 0;
}
#endif

static int sp1628_init(struct i2c_client *client)
{
	struct sp1628_s *sp1628 = to_sp1628(client);
	int ret = 0;

	sp1628->i_size = SP1628_SIZE_720P;
	sp1628->i_fmt = 0;	/* First format in the list */
	sp1628->width = 1280;
	sp1628->height = 720;
	sp1628->ev = EV_DEFAULT;
	sp1628->contrast = CONTRAST_DEFAULT;
	sp1628->effect = IMAGE_EFFECT_NONE;
	sp1628->banding = ANTI_BANDING_AUTO;
	sp1628->wb = WHITE_BALANCE_AUTO;
	sp1628->framerate = FRAME_RATE_AUTO;
	sp1628->inited = 0;
	sp1628->mode = SP1628_MODE_IDLE;
	sp1628->status = 0;
	sp1628->active_mode = SP1628_SIZE_720P;

	pr_info("Sensor initialized\n");

	return ret;
}

static void sp1628_video_remove(struct soc_camera_device *icd)
{
	/*dev_dbg(&icd->dev, "Video removed: %p, %p\n",
	icd->dev.parent, icd->vdev);commented because dev is not not
	part soc_camera_device on 3.4 verison*/
}

static int sp1628_queryctrl(struct v4l2_subdev *sd,
		struct v4l2_queryctrl *qc)
{
	int index = 0;
	for (index = 0; (index) < (ARRAY_SIZE(sp1628_controls)); index++) {
		if ((qc->id) == (sp1628_controls[index].id)) {
			qc->type = sp1628_controls[index].type;
			qc->minimum = sp1628_controls[index].minimum;
			qc->maximum = sp1628_controls[index].maximum;
			qc->step = sp1628_controls[index].step;
			qc->default_value =
				sp1628_controls[index].default_value;
			qc->flags = sp1628_controls[index].flags;
			strlcpy(qc->name, sp1628_controls[index].name,
			sizeof(qc->name));
			return 0;
		}
	}
	return -EINVAL;
}

static struct v4l2_subdev_core_ops sp1628_subdev_core_ops = {
	.s_power = sp1628_s_power,
	.g_chip_ident = sp1628_g_chip_ident,
	.g_ctrl = sp1628_g_ctrl,
	.s_ctrl = sp1628_s_ctrl,
	.ioctl = sp1628_ioctl,
	.queryctrl = sp1628_queryctrl,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = sp1628_g_register,
	.s_register = sp1628_s_register,
#endif
};

static int sp1628_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(sp1628_fmts))
		return -EINVAL;

	*code = sp1628_fmts[index].code;
	return 0;
}

static int sp1628_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sp1628_s *priv = to_sp1628(client);
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
static int sp1628_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sp1628_s *priv = to_sp1628(client);

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

static int sp1628_enum_framesizes(struct v4l2_subdev *sd,
				  struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index >= SP1628_SIZE_LAST)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->pixel_format = V4L2_PIX_FMT_UYVY;

	fsize->discrete = sp1628_frmsizes[fsize->index];

	return 0;
}

/* we only support fixed frame rate */
static int sp1628_enum_frameintervals(struct v4l2_subdev *sd,
				      struct v4l2_frmivalenum *interval)
{
	int size;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (interval->index >= 1)
		return -EINVAL;

	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;

	size = sp1628_find_framesize(interval->width, interval->height);
	switch (size) {
	case SP1628_SIZE_720P:
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

static int sp1628_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sp1628_s *sp1628 = to_sp1628(client);
	struct v4l2_captureparm *cparm;

	if (param->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	cparm = &param->parm.capture;

	memset(param, 0, sizeof(*param));
	param->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	cparm->capability = V4L2_CAP_TIMEPERFRAME;

	switch (sp1628->i_size) {
	case SP1628_SIZE_720P:
	default:
		cparm->timeperframe.numerator = 1;
		cparm->timeperframe.denominator = 24;
		break;
	}

	return 0;
}

static int sp1628_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	/*
	 * FIXME: This just enforces the hardcoded framerates until this is
	 * flexible enough.
	 */
	return sp1628_g_parm(sd, param);
}

static struct v4l2_subdev_video_ops sp1628_subdev_video_ops = {
	.s_stream = sp1628_s_stream,
	.s_mbus_fmt = sp1628_s_fmt,
	.g_mbus_fmt = sp1628_g_fmt,
	.g_crop		= sp1628_g_crop,
	.cropcap	= sp1628_cropcap,
	.try_mbus_fmt = sp1628_try_fmt,
	.enum_mbus_fmt = sp1628_enum_fmt,
	.enum_mbus_fsizes = sp1628_enum_framesizes,
	.enum_framesizes = sp1628_enum_framesizes,
	.enum_frameintervals = sp1628_enum_frameintervals,
	.g_parm = sp1628_g_parm,
	.s_parm = sp1628_s_parm,
};

static int sp1628_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	/* Quantity of initial bad frames to skip. Revisit. */
	/* Waitting for AWB stability, avoid green color issue */
	*frames = 2;

	return 0;
}

static struct v4l2_subdev_sensor_ops sp1628_subdev_sensor_ops = {
	.g_skip_frames = sp1628_g_skip_frames,
};

static struct v4l2_subdev_ops sp1628_subdev_ops = {
	.core = &sp1628_subdev_core_ops,
	.video = &sp1628_subdev_video_ops,
	.sensor = &sp1628_subdev_sensor_ops,
};

/*
 * Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one
 */
static int sp1628_video_probe(struct i2c_client *client)
{
	int ret = 0;
	u8 id_high, id_low;
	u16 id;
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);

	pr_info("%s E\n", __func__);
	ret = sp1628_s_power(subdev, 1);
	if (ret < 0)
		return ret;

	/* Read sensor Model ID */
	ret = sp1628_reg_read(client, SP1628_CHIP_ID_HIGH, &id_high);
	if (ret < 0)
		goto out;

	id = id_high << 8;

	ret = sp1628_reg_read(client, SP1628_CHIP_ID_LOW, &id_low);
	if (ret < 0)
		goto out;

	id |= id_low;
	pr_info("%s: Read ID: 0x%x\n", __func__, id);

	if (id != SP1628_CHIP_ID) {
		ret = -ENODEV;
		goto out;
	}

out:
	sp1628_s_power(subdev, 0);
	pr_info("%s X: %d\n", __func__, ret);
	return ret;
}


static int sp1628_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct sp1628_s *sp1628;
	struct soc_camera_link *icl = client->dev.platform_data;
	int ret = 0;

	pr_info("%s E\n", __func__);

	/* input validation */
	if (!icl) {
		dev_err(&client->dev, "sp1628 driver needs platform data\n");
		return -EINVAL;
	}

	if (!icl->priv) {
		dev_err(&client->dev,
			"sp1628 driver needs i/f platform data\n");
		return -EINVAL;
	}

	client->addr = SP1628_CHIP_I2C_ADDR;
	sp1628 = kzalloc(sizeof(struct sp1628_s), GFP_KERNEL);
	if (!sp1628)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&sp1628->subdev, client, &sp1628_subdev_ops);
	ret = sp1628_video_probe(client);
	if (ret)
		goto finish;

	sp1628->plat_parms = icl->priv;

	/* init the sensor here */
	ret = sp1628_init(client);
	if (ret) {
		dev_err(&client->dev, "Failed to initialize sensor\n");
		ret = -EINVAL;
	}

finish:
	if (ret != 0)
		kfree(sp1628);

	pr_info("%s X:%d\n", __func__, ret);
	return ret;
}

static int sp1628_remove(struct i2c_client *client)
{
	struct sp1628_s *sp1628 = to_sp1628(client);
	struct soc_camera_device *icd = client->dev.platform_data;
	sp1628_video_remove(icd);
	client->driver = NULL;
	kfree(sp1628);

	return 0;
}


static const struct i2c_device_id sp1628_id[] = {
	{"sp1628", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sp1628_id);

static struct i2c_driver sp1628_i2c_driver = {
	.driver = {
		   .name = "sp1628",
		   },
	.probe = sp1628_probe,
	.remove = sp1628_remove,
	.id_table = sp1628_id,
};

static int __init sp1628_mod_init(void)
{
	return i2c_add_driver(&sp1628_i2c_driver);
}

static void __exit sp1628_mod_exit(void)
{
	i2c_del_driver(&sp1628_i2c_driver);
}

module_init(sp1628_mod_init);
module_exit(sp1628_mod_exit);

MODULE_DESCRIPTION(" SP1628 Camera driver");
MODULE_AUTHOR("Jun He <junhe@broadcom.com>");
MODULE_LICENSE("GPL v2");

