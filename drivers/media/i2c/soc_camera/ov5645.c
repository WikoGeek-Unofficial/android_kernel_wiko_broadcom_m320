/*
 * OmniVision OV5645 sensor driver
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 *modify it under the terms of the GNU General Public License as
 *published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 *kind, whether express or implied; without even the implied warranty
 *of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <mach/r8a7373.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <media/sh_mobile_csi2.h>
#include <linux/videodev2_brcm.h>
#include <mach/setup-u2camera.h>
#include "ov5645.h"

#define TORCH_EN (10)
#define FLASH_EN (11)

#define FLASH_TIMEOUT_MS	500
struct timer_list flash_timer;

#define CAM_LED_ON				(1)
#define CAM_LED_OFF				(0)
#define CAM_LED_MODE_PRE			(1<<1)
#define CAM_LED_MODE_TORCH			(1<<0)
#define CAM_FLASH_ENSET     (GPIO_PORT99)
#define CAM_FLASH_FLEN      (GPIO_PORT100)
typedef struct touch_area v4l2_touch_area;

/* For retaing exposure values */
u8 exp1, exp_2, exp3, gain1, gain2;
u8 g1, g2, g3, g4, g5, g6;
u8 scene_registers[2];

static int initalized;
static int capture_mode;
static int scene_mode;

/* #define OV5645_DEBUG */

#define iprintk(format, arg...)	\
	printk(KERN_INFO"[%s]: "format"\n", __func__, ##arg)


#define OV5645_FLASH_THRESHHOLD		32

int OV5645_power(struct device *dev, int power_on)
{
	struct clk *vclk1_clk;
	int iret;
	struct regulator *regulator;
	dev_dbg(dev, "%s(): power_on=%d\n", __func__, power_on);

	vclk1_clk = clk_get(NULL, "vclk1_clk");
	if (IS_ERR(vclk1_clk)) {
		dev_err(dev, "clk_get(vclk1_clk) failed\n");
		return -1;
	}

	if (power_on) {
		initalized = 0;
		printk(KERN_ALERT "%s PowerON\n", __func__);
		sh_csi2_power(dev, power_on);

		/* CAM_VDDIO_1V8 On */
		regulator = regulator_get(NULL, "cam_sensor_io");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_enable(regulator);
		regulator_put(regulator);
		mdelay(10);
		/* CAM_AVDD_2V8  On */
		regulator = regulator_get(NULL, "cam_sensor_a");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_enable(regulator);
		regulator_put(regulator);

		mdelay(10);
		/* CAM_DVDD_1V5 On */
		regulator = regulator_get(NULL, "vcam_sense_1v5");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_enable(regulator);
		regulator_set_voltage(regulator, 1500000, 1500000);
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
		gpio_set_value(GPIO_PORT45, 1); /* CAM0_STBY */
		mdelay(5);

		gpio_set_value(GPIO_PORT20, 1); /* CAM0_RST_N Hi */
		mdelay(30);
		
		gpio_set_value(GPIO_PORT91, 1); /* CAM1_STBY */
                mdelay(10);
		gpio_set_value(GPIO_PORT16, 0); /* CAM1_RST_N */
		mdelay(10);

		printk(KERN_ALERT "%s PowerON fin\n", __func__);
		} else {
		printk(KERN_ALERT "%s PowerOFF\n", __func__);
		mdelay(1);
		gpio_set_value(GPIO_PORT20, 0); /* CAM0_RST_N */
		mdelay(3);

		gpio_set_value(GPIO_PORT45, 0); /* CAM0_STBY */
		mdelay(5);

                gpio_set_value(GPIO_PORT91, 0); /* CAM1_STBY */
                mdelay(10);

		clk_disable(vclk1_clk);
		mdelay(5);
		/* CAM_DVDD_1V5 On */
		regulator = regulator_get(NULL, "vcam_sense_1v5");
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
		regulator = regulator_get(NULL, "cam_sensor_io");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_disable(regulator);
		regulator_put(regulator);
		mdelay(1);

		/* 5M_AF_2V8 Off */
		regulator = regulator_get(NULL, "cam_af");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_disable(regulator);
		regulator_put(regulator);
		sh_csi2_power(dev, power_on);
		printk(KERN_ALERT "%s PowerOFF fin\n", __func__);
	}

	clk_put(vclk1_clk);

	return 0;
}

static int ov5645_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_subdev_desc *ssdd = soc_camera_i2c_to_desc(client);
	int ret;
	if (on) {
		ret = soc_camera_power_on(&client->dev, ssdd);
		if (ret < 0)
			return ret;
	} else{
		ret = soc_camera_power_off(&client->dev, ssdd);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*extern int hawaii_camera_AF_power(int on);*/

/* OV5645 has only one fixed colorspace per pixelcode */
struct ov5645_datafmt {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};

static const struct ov5645_datafmt ov5645_fmts[] = {
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

enum ov5645_size {
	OV5645_SIZE_QCIF,	/*  176 x 144 */
	OV5645_SIZE_QVGA,	/*  320 x 240 */
	OV5645_SIZE_CIF,	/*  352 x 288 */
	OV5645_SIZE_VGA,	/*  640 x 480 */
	OV5645_SIZE_480p,	/*  720 x 480 */
	OV5645_SIZE_720P,
	OV5645_SIZE_1280x960,	/*  1280 x 960 (1.2M) */
	OV5645_SIZE_UXGA,	/*  1600 x 1200 (2M) */
	OV5645_SIZE_1080P,
	OV5645_SIZE_QXGA,	/*  2048 x 1536 (3M) */
	OV5645_SIZE_5MP,
	OV5645_SIZE_LAST,
	OV5645_SIZE_MAX
};

enum cam_running_mode {
	CAM_RUNNING_MODE_NOTREADY,
	CAM_RUNNING_MODE_PREVIEW,
	CAM_RUNNING_MODE_CAPTURE,
	CAM_RUNNING_MODE_CAPTURE_DONE,
	CAM_RUNNING_MODE_RECORDING,
};
enum cam_running_mode runmode;

static const struct v4l2_frmsize_discrete ov5645_frmsizes[OV5645_SIZE_LAST] = {
	{176, 144},
	{320, 240},
	{352, 288},
	{640, 480},
	{720, 480},
	{1280, 720},
	{1280, 960},
	{1600, 1200},
	{1920, 1080},
	{2048, 1536},
	{2560, 1920},
};

static int ov_capture_width, ov_capture_height;
/* Scalers to map image resolutions into AF 80x60 virtual viewfinder */
static const struct ov5645_af_zone_scale af_zone_scale[OV5645_SIZE_LAST] = {
	{2, 2},
        {4, 4},
	{4, 5},
        {8, 8},
        {9, 8},
        {16, 12},
        {16, 16},
        {20, 20},
        {24, 18},
        {26, 26},
        {32, 32},
};

static int ov5645_init(struct i2c_client *client);
/* Find a data format by a pixel code in an array */
static int ov5645_find_datafmt(enum v4l2_mbus_pixelcode code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ov5645_fmts); i++)
		if (ov5645_fmts[i].code == code)
			break;

	/* If not found, select latest */
	if (i >= ARRAY_SIZE(ov5645_fmts))
		i = ARRAY_SIZE(ov5645_fmts) - 1;

	return i;
}

/* Find a frame size in an array */
static int ov5645_find_framesize(u32 width, u32 height)
{
	int i;

	for (i = 0; i < OV5645_SIZE_LAST; i++) {
		if ((ov5645_frmsizes[i].width >= width) &&
		    (ov5645_frmsizes[i].height >= height))
			break;
	}

	/* If not found, select biggest */
	if (i >= OV5645_SIZE_LAST)
		i = OV5645_SIZE_LAST - 1;

	return i;
}


/**
 *struct ov5645_otp - ov5645 OTP format
 *@module_integrator_id: 32-bit module integrator ID number
 *@lens_id: 32-bit lens ID number
 *
 * Define a structure for OV5645 OTP calibration data
 */
struct ov5645_otp {
	u8  flag;
	u8  module_integrator_id;
	u8  lens_id;
};


struct ov5645 {
	struct v4l2_subdev subdev;
	struct v4l2_subdev_sensor_interface_parms *plat_parms;
	int i_size;
	int i_fmt;
	int brightness;
	int contrast;
	int colorlevel;
	int sharpness;
	int isomode;
	int saturation;
	int antibanding;
	int whitebalance;
	int framerate;
	int focus_mode;
	int scenemode;
	int width;
	int height;
	int metering_exposure;
	/*
	 * focus_status = 1 focusing
	 * focus_status = 0 focus cancelled or not focusing
	 */
	atomic_t focus_status;

	/*
	 * touch_focus holds number of valid touch focus areas. 0 = none
	 */
	int touch_focus;
	v4l2_touch_area touch_area[OV5645_MAX_FOCUS_AREAS];
	short flashmode;
	short fireflash;
	struct ov5645_otp otp;
};

static int ov5645_set_flash_mode(int mode, struct i2c_client *client);

/*static int flash_gpio_strobe(int);*/

static struct ov5645 *to_ov5645(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov5645, subdev);
}

/**
 *ov5645_reg_read - Read a value from a register in an ov5645 sensor device
 *@client: i2c driver client structure
 *@reg: register address / offset
 *@val: stores the value that gets read
 *
 * Read a value from a register in an ov5645 sensor device.
 * The value is returned in 'val'.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ov5645_reg_read(struct i2c_client *client, u16 reg, u8 *val)
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
 * Write a value to a register in ov5645 sensor device.
 *@client: i2c driver client structure.
 *@reg: Address of the register to read value from.
 *@val: Value to be written to a specific register.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ov5645_reg_write(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;
	unsigned char data[3] = { (u8) (reg >> 8), (u8) (reg & 0xff), val };
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 3,
		.buf = data,
	};

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Failed writing register 0x%02x!\n", reg);
		return ret;
	}

	return 0;
}

/**
 * Initialize a list of ov5645 registers.
 * The list of registers is terminated by the pair of values
 *@client: i2c driver client structure.
 *@reglist[]: List of address of the registers to write data.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ov5645_reg_writes(struct i2c_client *client,
			     const struct ov5645_reg reglist[])
{
	int err = 0, index;

	for (index = 0; ((reglist[index].reg != 0xFFFF) &&
		(err == 0)); index++) {
		err |=
		    ov5645_reg_write(client, reglist[index].reg,
				     reglist[index].val);
		/*  Check for Pause condition */
		if ((reglist[index + 1].reg == 0xFFFF)
		    && (reglist[index + 1].val != 0)) {
			msleep(reglist[index + 1].val);
			index += 1;
		}
	}
	return 0;
}

#ifdef OV5645_DEBUG
static int ov5645_reglist_compare(struct i2c_client *client,
				  const struct ov5645_reg reglist[])
{
	int err = 0, index;
	u8 reg;

	for (index = 0; ((reglist[index].reg != 0xFFFF) &&
		(err == 0)); index++) {
		err |= ov5645_reg_read(client, reglist[index].reg, &reg);
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

/**
 * Write an array of data to ov5645 sensor device.
 *@client: i2c driver client structure.
 *@reg: Address of the register to read value from.
 *@data: pointer to data to be written starting at specific register.
 *@size: # of data to be written starting at specific register.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ov5645_array_write(struct i2c_client *client,
			      const u8 *data, u16 size)
{
	int ret;
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = size,
		.buf = (u8 *) data,
	};

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Failed writing array to 0x%02x!\n",
			((data[0] << 8) | (data[1])));
		return ret;
	}

	return 0;
}

static int ov5645_af_ack(struct i2c_client *client, int num_trys)
{
	int ret = 0;
	u8 af_ack = 0;
	int i;


	for (i = 0; i < num_trys; i++) {
		ov5645_reg_read(client, OV5645_CMD_ACK, &af_ack);
		if (af_ack == 0)
			break;
		msleep(50);
	}
	if (af_ack != 0) {
		dev_dbg(&client->dev, "af ack failed\n");
		return OV5645_AF_FAIL;
	}
	return ret;
}

static int ov5645_af_enable(struct i2c_client *client)
{
	int ret = 0;
	u8 af_st;
	int i;
	iprintk("ov5645_af_enable entry");

	/* hawaii_camera_AF_power(1); */
	msleep(20);

	ret = ov5645_reg_writes(client, ov5645_afpreinit_tbl);
	if (ret)
		return ret;

	ret = ov5645_array_write(client, ov5645_afinit_data,
				 sizeof(ov5645_afinit_data)
				 / sizeof(ov5645_afinit_data[0]));
	if (ret)
		return ret;
	msleep(10);

	ret = ov5645_reg_writes(client, ov5645_afpostinit_tbl);
	if (ret)
		return ret;

	msleep(20);

	for (i = 0; i < 30; i++) {
		ov5645_reg_read(client, OV5645_CMD_FW_STATUS, &af_st);
		if (af_st == 0x70)
			break;
		msleep(20);
	}
	iprintk("af_st check time %d", i);

	return ret;
}

static int ov5645_af_release(struct i2c_client *client)
{
	int ret = 0, i;
	u8 af_rel;
	u8 adc_reg;
	u16 high_read, low_read, high, low, adcvalue;


	ret = ov5645_reg_write(client, OV5645_CMD_ACK, 0x01);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, OV5645_CMD_MAIN, 0x08);
	if (ret)
		return ret;
	/*From data sheet it is found that 0x3023
	reg value should be zero for af_release*/
		for (i = 0; i < 100; i++) {
			ov5645_reg_read(client, 0x3023, &af_rel);
		if (af_rel == 0)
			break;
		msleep(20);
		}

	high_read = ov5645_reg_read(client, 0x3603, &adc_reg);
	low_read =  ov5645_reg_read(client, 0x3602, &adc_reg);
	adcvalue = ((high_read&0x3f)<<4) + ((low_read&0xf0)>>4);
	high = adcvalue>>4;
	low = (adcvalue&0x0f)<<4;
	high_read  = (((high_read)&0xC0)|high);
	ov5645_reg_write(client, 0x3603, high_read);
	low_read = (((low_read)&0x0F)|low);
	ov5645_reg_write(client, 0x3602, low_read);

	return ret;
}
static int ov5645_af_pause(struct i2c_client *client)
{
	int ret = 0;
	u8 af_ack;
	int i;

	ret = ov5645_reg_write(client, OV5645_CMD_ACK, 0x01);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, OV5645_CMD_MAIN, 0x06);
	if (ret)
		return ret;

	for (i = 0; i < 100; i++) {
		ov5645_reg_read(client, OV5645_CMD_ACK, &af_ack);
		if (af_ack == 0)
			break;
		msleep(20);
	}
	return ret;
}

static int ov5645_enable_af_continuous(struct i2c_client *client)
{
	int ret = 0;
	u8 af_ack;
	int i;

	ret = ov5645_af_release(client);
	if (ret)
		return ret;

	ret = ov5645_reg_write(client, OV5645_CMD_PARA0, 0x0);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, OV5645_CMD_MAIN, 0x04);
	if (ret)
		return ret;

	for (i = 0; i < 100; i++) {
		ov5645_reg_read(client, OV5645_CMD_ACK, &af_ack);
		if (af_ack == 0)
			break;
		msleep(20);
	}
	return ret;
}

static int ov5645_af_relaunch(struct i2c_client *client, int ratio)
{
	int ret = 0;
	int i;
	u8 af_ack = 0;

	ret = ov5645_reg_write(client, 0x3028, ratio);
	if (ret)
		return ret;

	ret = ov5645_reg_write(client, OV5645_CMD_ACK, 0x01);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, OV5645_CMD_MAIN, 0x80);
	if (ret)
		return ret;

	for (i = 0; i < 100; i++) {
		ov5645_reg_read(client, OV5645_CMD_ACK, &af_ack);
		if (af_ack == 0)
			break;
		msleep(20);
	}

	return ret;
}

static int ov5645_af_macro(struct i2c_client *client)
{
	int ret = 0;


	ret = ov5645_af_release(client);
	if (ret)
		return ret;

	ret = ov5645_reg_write(client, 0x3023, 0x01);
	if (ret)
		return ret;

	ret = ov5645_reg_write(client, 0x3022, 0x03);
	if (ret)
		return ret;

	return ret;
}

static int ov5645_af_infinity(struct i2c_client *client)
{
	int ret = 0;


	ret = ov5645_af_release(client);
	if (ret)
		return ret;

	ret = ov5645_reg_write(client, 0x3023, 0x01);
	if (ret)
		return ret;

	ret = ov5645_reg_write(client, 0x3022, 0x08);
	if (ret)
		return ret;

	return ret;
}

/* Set the touch area x,y in VVF coordinates*/
static int ov5645_af_touch(struct i2c_client *client)
{
	int ret = OV5645_AF_SUCCESS;
	struct ov5645 *ov5645 = to_ov5645(client);


	/* verify # zones correct */
	if (ov5645->touch_focus) {

		/* touch zone config */
		ret = ov5645_reg_write(client, 0x3024,
				       (u8) ov5645->touch_area[0].leftTopX);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, 0x3025,
				       (u8) ov5645->touch_area[0].leftTopY);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, OV5645_CMD_ACK, 0x01);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, OV5645_CMD_MAIN, 0x81);
		if (ret)
			return ret;
		ret = ov5645_af_ack(client, 50);
		if (ret) {
			dev_dbg(&client->dev, "zone config ack failed\n");
			return ret;
		}

	}

	iprintk(" exit");

	return ret;
}

/* Set the touch area, areas can overlap and
are givin in current sensor resolution coords */

#if 0
static int ov5645_af_area(struct i2c_client *client)
{
	int ret = OV5645_AF_SUCCESS;
	struct ov5645 *ov5645 = to_ov5645(client);
	u8 weight[OV5645_MAX_FOCUS_AREAS];
	int i;


	/* verify # zones correct */
	if ((ov5645->touch_focus) &&
	    (ov5645->touch_focus <= OV5645_MAX_FOCUS_AREAS)) {

		iprintk("entry touch_focus %d", ov5645->touch_focus);

		/* enable zone config */
		ret = ov5645_reg_write(client, OV5645_CMD_ACK, 0x01);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, OV5645_CMD_MAIN, 0x8f);
		if (ret)
			return ret;
		ret = ov5645_af_ack(client, 50);
		if (ret) {
			dev_dbg(&client->dev, "zone config ack failed\n");
			return ret;
		}

		/* clear all zones */
		for (i = 0; i < OV5645_MAX_FOCUS_AREAS; i++)
			weight[i] = 0;

		/* write area to sensor */
		for (i = 0; i < ov5645->touch_focus; i++) {

			ret = ov5645_reg_write(client, 0x3024,
					       (u8) ov5645->
					       touch_area[i].leftTopX);
			if (ret)
				return ret;
			ret = ov5645_reg_write(client, 0x3025,
					       (u8) ov5645->
					       touch_area[i].leftTopY);
			if (ret)
				return ret;
			ret = ov5645_reg_write(client, 0x3026,
					       (u8) (ov5645->
						     touch_area[i].leftTopX +
						     ov5645->
						     touch_area
						     [i].rightBottomX));
			if (ret)
				return ret;
			ret = ov5645_reg_write(client, 0x3027,
					       (u8) (ov5645->
						     touch_area[i].leftTopY +
						     ov5645->
						     touch_area
						     [i].rightBottomY));
			if (ret)
				return ret;
			ret = ov5645_reg_write(client, OV5645_CMD_ACK, 0x01);
			if (ret)
				return ret;
			ret = ov5645_reg_write(client, OV5645_CMD_MAIN,
					       (0x90 + i));
			if (ret)
				return ret;
			ret = ov5645_af_ack(client, 50);
			if (ret) {
				dev_dbg(&client->dev, "zone update failed\n");
				return ret;
			}
			weight[i] = (u8) ov5645->touch_area[i].weight;
		}

		/* enable zone with weight */
		ret = ov5645_reg_write(client, 0x3024, weight[0]);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, 0x3025, weight[1]);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, 0x3026, weight[2]);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, 0x3027, weight[3]);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, 0x3028, weight[4]);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, OV5645_CMD_ACK, 0x01);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, OV5645_CMD_MAIN, 0x98);
		if (ret)
			return ret;
		ret = ov5645_af_ack(client, 50);
		if (ret) {
			dev_dbg(&client->dev, "weights failed\n");
			return ret;
		}

		/* launch zone configuration */
		ret = ov5645_reg_write(client, OV5645_CMD_ACK, 0x01);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, OV5645_CMD_MAIN, 0x9f);
		if (ret)
			return ret;
		ret = ov5645_af_ack(client, 50);
		if (ret) {
			dev_dbg(&client->dev, "launch failed\n");
			return ret;
		}
	}

	return ret;
}
#endif

/* Convert touch area from sensor resolution coords to ov5645 VVF zone */
static int ov5645_af_zone_conv(struct i2c_client *client,
			       v4l2_touch_area *zone_area, int zone)
{
	int ret = 0;
	u32 x0, y0, x1, y1, weight;
	struct ov5645 *ov5645 = to_ov5645(client);

	iprintk("entry ");

	/* Reset zone */
	ov5645->touch_area[zone].leftTopX = 0;
	ov5645->touch_area[zone].leftTopY = 0;
	ov5645->touch_area[zone].rightBottomX = 0;
	ov5645->touch_area[zone].rightBottomY = 0;
	ov5645->touch_area[zone].weight = 0;

	/* x y w h are in current sensor resolution dimensions */
	if (((u32) zone_area->leftTopX + (u32) zone_area->rightBottomX)
	    > ov5645_frmsizes[ov5645->i_size].width) {
		iprintk("zone width error: x=0x%x w=0x%x",
			zone_area->leftTopX, zone_area->rightBottomX);
		ret = -EINVAL;
		goto out;
	} else if (((u32) zone_area->leftTopY + (u32) zone_area->rightBottomY)
		   > ov5645_frmsizes[ov5645->i_size].height) {
		iprintk("zone height error: y=0x%x h=0x%x",
			zone_area->leftTopY, zone_area->rightBottomY);
		ret = -EINVAL;
		goto out;
	} else if ((u32) zone_area->weight > 1000) {

		iprintk("zone weight error: weight=0x%x", zone_area->weight);
		ret = -EINVAL;
		goto out;
	}

	/* conv area to sensor VVF zone */
	x0 = (u32) zone_area->leftTopX / af_zone_scale[ov5645->i_size].x_scale;
	if (x0 > (OV5645_AF_NORMALIZED_W - 8))
		x0 = (OV5645_AF_NORMALIZED_W - 8);
	x1 = ((u32) zone_area->leftTopX + (unsigned int)zone_area->rightBottomX)
	    / af_zone_scale[ov5645->i_size].x_scale;
	if (x1 > OV5645_AF_NORMALIZED_W)
		x1 = OV5645_AF_NORMALIZED_W;
	y0 = (u32) zone_area->leftTopY / af_zone_scale[ov5645->i_size].y_scale;
	if (y0 > (OV5645_AF_NORMALIZED_H - 8))
		y0 = (OV5645_AF_NORMALIZED_H - 8);
	y1 = ((u32) zone_area->leftTopY + (unsigned int)zone_area->rightBottomY)
	    / af_zone_scale[ov5645->i_size].y_scale;
	if (y1 > OV5645_AF_NORMALIZED_H)
		y1 = OV5645_AF_NORMALIZED_H;

	/* weight ranges from 1-1000 */
	/* Convert weight */
	weight = 0;
	if ((zone_area->weight > 0) && (zone_area->weight <= 125))
		weight = 1;
	else if ((zone_area->weight > 125) && (zone_area->weight <= 250))
		weight = 2;
	else if ((zone_area->weight > 250) && (zone_area->weight <= 375))
		weight = 3;
	else if ((zone_area->weight > 375) && (zone_area->weight <= 500))
		weight = 4;
	else if ((zone_area->weight > 500) && (zone_area->weight <= 625))
		weight = 5;
	else if ((zone_area->weight > 625) && (zone_area->weight <= 750))
		weight = 6;
	else if ((zone_area->weight > 750) && (zone_area->weight <= 875))
		weight = 7;
	else if (zone_area->weight > 875)
		weight = 8;

	/* Minimum zone size */
	if (((x1 - x0) >= 8) && ((y1 - y0) >= 8)) {

		ov5645->touch_area[zone].leftTopX = (int)x0;
		ov5645->touch_area[zone].leftTopY = (int)y0;
		ov5645->touch_area[zone].rightBottomX = (int)(x1 - x0);
		ov5645->touch_area[zone].rightBottomY = (int)(y1 - y0);
		ov5645->touch_area[zone].weight = (int)weight;

	} else {
		dev_dbg(&client->dev,
			"zone %d size failed: x0=%d x1=%d y0=%d y1=%d w=%d\n",
			zone, x0, x1, y0, y1, weight);
		ret = -EINVAL;
		goto out;
	}

out:

	return ret;
}

static int ov5645_af_status(struct i2c_client *client, int num_trys)
{
	int ret = OV5645_AF_SUCCESS;
	struct ov5645 *ov5645 = to_ov5645(client);

	u8 af_ack = 0;
	int i = 0;
	u8 af_zone4;

	for (i = 0; i < num_trys; i++) {
		ov5645_reg_read(client, OV5645_CMD_ACK, &af_ack);
		if (af_ack == 0)
			break;
		msleep(50);
	}
	if (ov5645->focus_mode == FOCUS_MODE_AUTO) {
		ov5645_reg_read(client, 0x3028, &af_zone4);
		if (af_zone4 == 0)
			ret = OV5645_AF_FAIL;
		else
			ret = OV5645_AF_SUCCESS;
		}
	return ret;
}

/* module MCLK */
#define MCLK 2400


static int ov5645_get_HTS(struct v4l2_subdev *sd)
{
	/* read HTS from register settings */
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int HTS;
	u8 val;

	ov5645_reg_read(client, 0x380c, &val);
	HTS = val;
	ov5645_reg_read(client, 0x380d, &val);
	HTS = (HTS << 8) + val;

	return HTS;
}

static int ov5645_get_shutter(struct v4l2_subdev *sd)
{
	/* read shutter, in number of line period*/
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int shutter;
	u8 val;

	ov5645_reg_read(client, 0x3500, &val);
	shutter = (val & 0x0f);
	ov5645_reg_read(client, 0x3501, &val);
	shutter = (shutter << 8) + val;
	ov5645_reg_read(client, 0x3502, &val);
	shutter = (shutter << 4) + (val >> 4);

	return shutter;
}

static int ov5645_get_exp_time(struct v4l2_subdev *sd)
{
	u8 line_l, val, etime = 0;
	line_l = ov5645_get_HTS(sd);
	val = ov5645_get_shutter(sd);
	etime = val * line_l;

	return etime;
}

static int ov5645_write_capture_registers(struct v4l2_subdev *sd);
static int ov5645_write_gain(struct v4l2_subdev *sd);
static int ov5645_read_capture_registers(struct v4l2_subdev *sd);
static int ov5645_read_gain(struct v4l2_subdev *sd);
static int ov5645_read_scene_mode(struct v4l2_subdev *sd);
static int ov5645_write_scene_mode(struct v4l2_subdev *sd);

static int ov5645_get_sysclk(struct v4l2_subdev *sd)
{
	/* calculate sysclk */
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 val;
	int multiplier, pre_div, VCO, sys_div, pll_rdiv, Bit_div2x;
	int sclk_rdiv, sysclk;
	int sclk_rdiv_map[] = { 1, 2, 4, 8 };

	ov5645_reg_read(client, 0x3034, &val);
	val &= 0x0F;
	Bit_div2x = 1;
	if (val == 8 || val == 10)
		Bit_div2x = val / 2;

	ov5645_reg_read(client, 0x3035, &val);
	sys_div = val >> 4;
	if (sys_div == 0)
		sys_div = 16;

	ov5645_reg_read(client, 0x3036, &val);
	if (val == 0)
		val = 0x68;
	multiplier = val;

	ov5645_reg_read(client, 0x3037, &val);
	if ((val & 0x0f) == 0)
		val = 0x12;

	pre_div = val & 0x0f;
	pll_rdiv = ((val >> 4) & 0x01) + 1;

	ov5645_reg_read(client, 0x3108, &val);
	val &= 0x03;
	sclk_rdiv = sclk_rdiv_map[val];

	VCO = MCLK * multiplier / pre_div;

	sysclk = VCO / sys_div / pll_rdiv * 2 / Bit_div2x / sclk_rdiv;

	return sysclk;
}


static int ov5645_set_banding(struct v4l2_subdev *sd)
{
	int ret = 0;
	int sysclk, mode_hts, mode_vts;
	int band_step60, max_band60, band_step50, max_band50;
	u8 val;

	struct i2c_client *client = v4l2_get_subdevdata(sd);

	/* get PCLK */
	sysclk = ov5645_get_sysclk(sd);
	/* get HTS */
	ov5645_reg_read(client, 0x380c, &val);
	mode_hts = val; mode_hts <<= 8;
	ov5645_reg_read(client, 0x380d, &val);
	mode_hts |= val;
	/* get VTS */
	ov5645_reg_read(client, 0x380e, &val);
	mode_vts = val; mode_vts <<= 8;
	ov5645_reg_read(client, 0x380f, &val);
	mode_vts |= val;
	/* calc banding fiters */
	/* 60HZ */
	band_step60 = sysclk * 100 / mode_hts * 100 / 120;
	ov5645_reg_write(client, 0x3a0a, (band_step60 >> 8));
	ov5645_reg_write(client, 0x3a0b, (band_step60 & 0xff));
	max_band60 = (int)((mode_vts - 4) / band_step60);
	ov5645_reg_write(client, 0x3a0d, max_band60);
	/* 50HZ */
	band_step50 = sysclk * 100 / mode_hts;
	ov5645_reg_write(client, 0x3a08, (band_step50 >> 8));
	ov5645_reg_write(client, 0x3a09, (band_step50 & 0xff));
	max_band50 = (int)((mode_vts - 4) / band_step50);
	ov5645_reg_write(client, 0x3a0e, max_band50);

	return ret;
}

static int ov5645_config_preview(struct v4l2_subdev *sd)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	struct ov5645 *ov5645 = to_ov5645(client);

	ov5645_reg_write(client, 0x3503, 0x00);
	dev_dbg(&client->dev, "ov5645->i_size=%d\n", ov5645->i_size);

	if (capture_mode) {
		ret = ov5645_read_capture_registers(sd);
		if (ret)
			return ret;
		ret = ov5645_read_gain(sd);
		if (ret)
			return ret;
		ret = ov5645_reg_writes(client, ov5645_5m_capture_init);
		capture_mode = 0;
		ov5645_write_capture_registers(sd);
		ov5645_write_gain(sd);
	} else if (ov5645->i_size == OV5645_SIZE_VGA) {
		if (initalized == 2) {
			ret = ov5645_reg_writes(client,
				ov5645_vga_preview_init);
			msleep(120);

			ov5645_af_relaunch(client, 1);
			if (ov5645->focus_mode ==
				 FOCUS_MODE_CONTINUOUS_PICTURE)
				ret = ov5645_enable_af_continuous(client);
			if (scene_mode) {
				ov5645_write_scene_mode(sd);
				msleep(20);
			}
			if (ret)
				return ret;
		}
	} else if (ov5645->i_size == OV5645_SIZE_QCIF ||
			ov5645->i_size == OV5645_SIZE_CIF) {
		ret = ov5645_reg_writes(client, ov5645_cif_init);
		if (ret)
			return ret;
	} else if (ov5645->i_size == OV5645_SIZE_QVGA) {
		ret = ov5645_reg_writes(client, ov5645_vga_preview_init);
		if (ret)
			return ret;
	} else if (ov5645->i_size == OV5645_SIZE_480p) {
		ret = ov5645_reg_writes(client, ov5645_480p_20fps_init);
		if (ret)
			return ret;
		ov5645_af_relaunch(client, 1);
	} else if (ov5645->i_size == OV5645_SIZE_720P) {
		ret = ov5645_reg_writes(client, ov5645_720p_init);
		if (ret)
			return ret;
	} else {
		ret = ov5645_reg_writes(client, ov5645_1080p_init);
		if (ret)
			return ret;
	}
	initalized = 2;
	return ret;
}

static int ov5645_read_capture_registers(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	ret = ov5645_reg_read(client, 0x3500, &exp1);
	if (ret)
		return ret;
	ret = ov5645_reg_read(client, 0x3501, &exp_2);
	if (ret)
		return ret;
	ret = ov5645_reg_read(client, 0x3502, &exp3);
	if (ret)
		return ret;
	ret = ov5645_reg_read(client, 0x350a, &gain1);
	if (ret)
		return ret;
	ret = ov5645_reg_read(client, 0x350b, &gain2);
	if (ret)
		return ret;

	return 0;
}

static int ov5645_write_capture_registers(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	ret = ov5645_reg_write(client, 0x3500, exp1);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, 0x3501, exp_2);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, 0x3502, exp3);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, 0x350a, gain1);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, 0x350b, gain2);
	if (ret)
		return ret;

	return 0;
}

static int ov5645_read_gain(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	ret = ov5645_reg_read(client, 0x3400, &g1);
	if (ret)
		return ret;
	ret = ov5645_reg_read(client, 0x3401, &g2);
	if (ret)
		return ret;
	ret = ov5645_reg_read(client, 0x3402, &g3);
	if (ret)
		return ret;
	ret = ov5645_reg_read(client, 0x3403, &g4);
	if (ret)
		return ret;
	ret = ov5645_reg_read(client, 0x3404, &g5);
	if (ret)
		return ret;
	ret = ov5645_reg_read(client, 0x3405, &g6);
	if (ret)
		return ret;

	return 0;
}

static int ov5645_write_gain(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	ret = ov5645_reg_write(client, 0x3400, g1);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, 0x3401, g2);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, 0x3402, g3);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, 0x3403, g4);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, 0x3404, g5);
	if (ret)
		return ret;
	ret = ov5645_reg_write(client, 0x3405, g6);
	if (ret)
		return ret;

	return 0;
}

static int ov5645_read_scene_mode(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	ret = ov5645_reg_read(client, 0x3035, &scene_registers[0]);
	ret |= ov5645_reg_read(client, 0x5001, &scene_registers[1]);

	return ret;
}

static int ov5645_write_scene_mode(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	ret = ov5645_reg_write(client, 0x3035, scene_registers[0]);
	ret |= ov5645_reg_write(client, 0x5001, scene_registers[1]);

	return ret;
}

static int ov5645_flash_control(struct i2c_client *client, int control)
{
	int ret = 0;

	mic2871_led((control >> 1 && CAM_LED_ON),
		(control & CAM_LED_MODE_PRE));

	return ret;
}

static void flash_timer_callback(unsigned long data)
{
	gpio_set_value(CAM_FLASH_FLEN, 0);
	gpio_set_value(CAM_FLASH_ENSET, 0);
}

static int ov5645_pre_flash(struct i2c_client *client)
{
	int ret = 0;
	struct ov5645 *ov5645 = to_ov5645(client);

	ov5645->fireflash = 0;
	if (FLASH_MODE_ON == ov5645->flashmode) {
		ret = ov5645_flash_control(client, ov5645->flashmode);
		ov5645->fireflash = 1;
	} else if (FLASH_MODE_AUTO == ov5645->flashmode) {
		u8 average = 0;
		ov5645_reg_read(client, 0x56a1, &average);
		if ((average & 0xFF) < OV5645_FLASH_THRESHHOLD) {
			ret = ov5645_flash_control(client, FLASH_MODE_ON);
			ov5645->fireflash = 1;
		}
	}
	if (1 == ov5645->fireflash)
		msleep(50);

	if (1 == ov5645->fireflash)
		mod_timer(&flash_timer,
			jiffies + msecs_to_jiffies(FLASH_TIMEOUT_MS));

	return ret;
}

static int ov5645_af_start(struct i2c_client *client)
{
	int ret = 0;
	int i = 0;
	u8 af_ack;
	struct ov5645 *ov5645 = to_ov5645(client);

	iprintk("entry focus_mode %d", ov5645->focus_mode);

		if (ov5645->touch_focus) {
			/*if (ov5645->touch_focus == 1)
				ret = ov5645_af_touch(client);
			else
				ret = ov5645_af_area(client);*/
			ret = ov5645_af_touch(client);
		}
		if (ret)
			return ret;

		ret = ov5645_reg_write(client, OV5645_CMD_ACK, 0x1);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, OV5645_CMD_MAIN, 0x08);
		if (ret)
			return ret;

		for (i = 0; i < 100; i++) {
			ov5645_reg_read(client, OV5645_CMD_ACK, &af_ack);
		if (af_ack == 0)
			break;
		msleep(20);
		}

		ret = ov5645_reg_write(client, OV5645_CMD_ACK, 0x01);
		if (ret)
			return ret;
		ret = ov5645_reg_write(client, OV5645_CMD_MAIN, 0x03);
		if (ret)
			return ret;


	return ret;
}

static int stream_mode = -1;
static int ov5645_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;

	printk(KERN_INFO "%s: enable:%d runmode:%d  stream_mode:%d\n",
	       __func__, enable, runmode, stream_mode);

	if (enable == stream_mode)
		return ret;
	ov5645_set_banding(sd);
	stream_mode = enable;
	return ret;
}

static int ov5645_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5645 *ov5645 = to_ov5645(client);

	mf->width = ov5645_frmsizes[ov5645->i_size].width;
	mf->height = ov5645_frmsizes[ov5645->i_size].height;
	mf->code = ov5645_fmts[ov5645->i_fmt].code;
	mf->colorspace = ov5645_fmts[ov5645->i_fmt].colorspace;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static int ov5645_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	int i_fmt;
	int i_size;

	i_fmt = ov5645_find_datafmt(mf->code);

	mf->code = ov5645_fmts[i_fmt].code;
	mf->colorspace = ov5645_fmts[i_fmt].colorspace;
	mf->field = V4L2_FIELD_NONE;

	i_size = ov5645_find_framesize(mf->width, mf->height);

	mf->width = ov5645_frmsizes[i_size].width;
	mf->height = ov5645_frmsizes[i_size].height;

	return 0;
}

static int ov5645_read_module_id(struct i2c_client *client, int index_group)
{
	u8 tmp, flag, module_id;
	int ret = 0;
	struct ov5645 *ov5645 = to_ov5645(client);
	struct ov5645_otp *otp = &(ov5645->otp);

	if (index_group == 0)
			ret = ov5645_reg_read(client, 0x3d05, &tmp);
	else if (index_group == 1)
			ret = ov5645_reg_read(client, 0x3d0e, &tmp);
	else if (index_group == 2)
			ret = ov5645_reg_read(client, 0x3d17, &tmp);

	if (ret < 0)
		return 0;

	flag = tmp & 0x80;
	module_id = tmp & 0x7f;
	otp->module_integrator_id = 0;

	if ((flag == 0) && (module_id != 0)) {
		otp->module_integrator_id = module_id;
		return 2;
		}
	else
		return 1;
}


static int ov5645_otp_read(struct i2c_client *client)
{
	int ret = 0, i;
	int index, result;
	u8 tmp1, tmp2;

	ret = ov5645_reg_read(client, 0x3000, &tmp1);
	if (ret < 0)
		return ret;

	ret = ov5645_reg_write(client, 0x3000, 0x00);

	ret = ov5645_reg_read(client, 0x3004, &tmp2);

	tmp2 |= 0x10;
	ret = ov5645_reg_write(client, 0x3004, tmp2);

	ret = ov5645_reg_write(client, 0x3d21, 0x01);

	mdelay(10);

	for (index = 0; index < 3; index++) {
		result = ov5645_read_module_id(client, index);
		if (result == 2)
			break;
	}

	if (index > 2)
		pr_info("ov5645 OTP empty\n");

	if (result == 0)
		pr_info("ov5645 OTP read fail\n");


	for (i = 0; i < 16; i++)
		ov5645_reg_write(client, (0x3d00+i), 0x00);

	ret = ov5645_reg_write(client, 0x3000, tmp1);

	tmp2 &= ~0x10;
	ret = ov5645_reg_write(client, 0x3004, tmp2);

	return ret;
}


static int ov5645_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5645 *ov5645 = to_ov5645(client);
	u8 ldo_value, ldo_disable;
	int ret = 0;
	int iret;
	struct regulator *regulator;

	ret = ov5645_try_fmt(sd, mf);
	if (ret < 0)
		return ret;
	ov5645->i_size = ov5645_find_framesize(mf->width, mf->height);
	ov5645->i_fmt = ov5645_find_datafmt(mf->code);
	if (initalized == 0)	{
		ret = ov5645_reg_write(client, 0x3008, 0x82);
		msleep(20);
		/* Sensor settings to disable internal DVDD */
		ov5645_reg_read(client, 0x3031, &ldo_value);
		ldo_disable = ldo_value | (1<<3);
		ret = ov5645_reg_write(client, 0x3031, ldo_disable);
		if (ret)
			return ret;
		dev_dbg(&client->dev, "Loading AF during initialization\n");
		ret = ov5645_af_enable(client);
		if (ret)
			return ret;
		/* 5M_AF_2V8 On */
		regulator = regulator_get(NULL, "cam_af");
		if (IS_ERR(regulator))
			return -1;
		iret = regulator_enable(regulator);
		regulator_put(regulator);
		ret = ov5645_reg_writes(client, vga_common_init);
		if (ret)
			return ret;

		initalized = 1;
	}

	printk(KERN_INFO "%s: code:0x%x fmt[%d]\n", __func__,
	       ov5645_fmts[ov5645->i_fmt].code, ov5645->i_size);

	if (CAM_RUNNING_MODE_PREVIEW == runmode)
		ov5645_config_preview(sd);
	return ret;
}

static int ov5645_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	id->ident = V4L2_IDENT_OV5645;
	id->revision = 0;

	return 0;
}

/*
 * return value of this function should be
 * 0 == CAMERA_AF_STATUS_FOCUSED
 * 1 == CAMERA_AF_STATUS_FAILED
 * 2 == CAMERA_AF_STATUS_SEARCHING
 * 3 == CAMERA_AF_STATUS_CANCELLED
 * to keep consistent with auto_focus_result
 * in videodev2_brcm.h
 */
static int ov5645_get_af_status(struct i2c_client *client, int num_trys)
{
	int ret = OV5645_AF_PENDING;
	struct ov5645 *ov5645 = to_ov5645(client);

	if (atomic_read(&ov5645->focus_status)
	    == OV5645_FOCUSING) {
		ret = ov5645_af_status(client, num_trys);
		/*
		 * convert OV5645_AF_* to auto_focus_result
		 * in videodev2_brcm
		 */
		switch (ret) {
		case OV5645_AF_SUCCESS:
			ret = CAMERA_AF_STATUS_FOCUSED;
			break;
		case OV5645_AF_FAIL:
			ret = CAMERA_AF_STATUS_FAILED;
			break;
		default:
			ret = CAMERA_AF_STATUS_SEARCHING;
			break;
		}
	}
	if (atomic_read(&ov5645->focus_status)
	    == OV5645_NOT_FOCUSING) {
		ret = CAMERA_AF_STATUS_CANCELLED;	/* cancelled? */
	}
	if ((CAMERA_AF_STATUS_FOCUSED == ret) ||
	    (CAMERA_AF_STATUS_FAILED == ret))
		atomic_set(&ov5645->focus_status, OV5645_NOT_FOCUSING);

	return ret;
}

static int ov5645_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5645 *ov5645 = to_ov5645(client);

	dev_dbg(&client->dev, "ov5645_g_ctrl\n");

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_BRIGHTNESS:
		ctrl->value = ov5645->brightness;
		break;
	case V4L2_CID_CAMERA_CONTRAST:
		ctrl->value = ov5645->contrast;
		break;
	case V4L2_CID_CAMERA_EFFECT:
		ctrl->value = ov5645->colorlevel;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = ov5645->saturation;
		break;
	case V4L2_CID_SHARPNESS:
		ctrl->value = ov5645->sharpness;
		break;
	case V4L2_CID_CAMERA_ANTI_BANDING:
		ctrl->value = ov5645->antibanding;
		break;
	case V4L2_CID_CAMERA_WHITE_BALANCE:
		ctrl->value = ov5645->whitebalance;
		break;
	case V4L2_CID_CAMERA_METERING_EXPOSURE:
		ctrl->value = ov5645->metering_exposure;
		break;
	case V4L2_CID_CAMERA_FRAME_RATE:
		ctrl->value = ov5645->framerate;
		break;
	case V4L2_CID_CAMERA_FOCUS_MODE:
		ctrl->value = ov5645->focus_mode;
		break;
	case V4L2_CID_CAMERA_TOUCH_AF_AREA:
		ctrl->value = ov5645->touch_focus;
		break;
	case V4L2_CID_CAMERA_AUTO_FOCUS_RESULT:
		/*
		 * this is called from another thread to read AF status
		 */
		ctrl->value = ov5645_get_af_status(client, 100);
		ov5645->touch_focus = 0;
		break;
	case V4L2_CID_CAMERA_FLASH_MODE:
		ctrl->value = ov5645->flashmode;
		break;
	case V4L2_CID_CAMERA_EXP_TIME:
		/* This is called to get the exposure values */
		ctrl->value = ov5645_get_exp_time(sd);
		break;
	}

	return 0;
}

static int ov5645_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5645 *ov5645 = to_ov5645(client);
	int ret = 0;
	dev_dbg(&client->dev, "ov5645_s_ctrl\n");
	switch (ctrl->id) {

	case V4L2_CID_CAMERA_SCENE_MODES:


	 if (ctrl->value >=  SCENE_MODE_MAX)
			return -EINVAL;

	ov5645->scenemode = ctrl->value;
	switch (ov5645->scenemode) {
	case SCENE_MODE_SUNSET:
		ov5645_reg_write(client, 0x3035, 0x31);
		ov5645_reg_write(client, 0x3a0f, 0x38);
		ov5645_reg_write(client, 0x3a10, 0x30);
		ov5645_reg_write(client, 0x3a1b, 0x3a);
		ov5645_reg_write(client, 0x3a1e, 0x2e);
		ov5645_reg_write(client, 0x3a11, 0x60);
		ov5645_reg_write(client, 0x3a1f, 0x10);
		ov5645_reg_write(client, 0x5001, 0xa3);
		ov5645_reg_write(client, 0x5588, 0x41);
		ov5645_reg_write(client, 0x5580, 0x02);
		ov5645_reg_write(client, 0x5583, 0x48);
		ov5645_reg_write(client, 0x5584, 0x4c);
		ov5645_reg_write(client, 0x5585, 0x20);
		ov5645_reg_write(client, 0x5586, 0x20);
		ov5645_reg_write(client, 0x5303, 0x00);
		ov5645_reg_write(client, 0x5302, 0x08);
		scene_mode = 1;
		break;
	case SCENE_MODE_SPORTS:
		ov5645_reg_write(client, 0x3035, 0x22);
		ov5645_reg_write(client, 0x3a0f, 0x38);
		ov5645_reg_write(client, 0x3a10, 0x30);
		ov5645_reg_write(client, 0x3a1b, 0x30);
		ov5645_reg_write(client, 0x3a1e, 0x2e);
		ov5645_reg_write(client, 0x3a11, 0x60);
		ov5645_reg_write(client, 0x3a1f, 0x14);
		ov5645_reg_write(client, 0x5001, 0xa3);
		ov5645_reg_write(client, 0x5588, 0x01);
		ov5645_reg_write(client, 0x5580, 0x02);
		ov5645_reg_write(client, 0x5583, 0x40);
		ov5645_reg_write(client, 0x5584, 0x10);
		ov5645_reg_write(client, 0x5585, 0x00);
		ov5645_reg_write(client, 0x5586, 0x20);
		ov5645_reg_write(client, 0x5303, 0x00);
		ov5645_reg_write(client, 0x5302, 0x10);
		scene_mode = 1;
		break;
	case SCENE_MODE_NIGHTSHOT:
		ov5645_reg_write(client, 0x3035, 0x51);
		ov5645_reg_write(client, 0x3a0f, 0x38);
		ov5645_reg_write(client, 0x3a10, 0x30);
		ov5645_reg_write(client, 0x3a1b, 0x3a);
		ov5645_reg_write(client, 0x3a1e, 0x2e);
		ov5645_reg_write(client, 0x3a11, 0x60);
		ov5645_reg_write(client, 0x3a1f, 0x10);
		ov5645_reg_write(client, 0x5001, 0xa3);
		ov5645_reg_write(client, 0x5588, 0x01);
		ov5645_reg_write(client, 0x5580, 0x02);
		ov5645_reg_write(client, 0x5583, 0x40);
		ov5645_reg_write(client, 0x5584, 0x40);
		ov5645_reg_write(client, 0x5585, 0x20);
		ov5645_reg_write(client, 0x5586, 0x20);
		ov5645_reg_write(client, 0x5303, 0x00);
		ov5645_reg_write(client, 0x5302, 0x06);
		scene_mode = 1;
		break;
	case SCENE_MODE_NONE:
		ov5645_reg_write(client, 0x3035, 0x22);
		ov5645_reg_write(client, 0x3a0f, 0x38);
		ov5645_reg_write(client, 0x3a10, 0x30);
		ov5645_reg_write(client, 0x3a1b, 0x3a);
		ov5645_reg_write(client, 0x3a1e, 0x2e);
		ov5645_reg_write(client, 0x3a11, 0x60);
		ov5645_reg_write(client, 0x3a1f, 0x10);
		ov5645_reg_write(client, 0x5001, 0xa3);
		ov5645_reg_write(client, 0x5588, 0x01);
		ov5645_reg_write(client, 0x5580, 0x02);
		ov5645_reg_write(client, 0x5583, 0x40);
		ov5645_reg_write(client, 0x5584, 0x40);
		ov5645_reg_write(client, 0x5585, 0x20);
		ov5645_reg_write(client, 0x5586, 0x20);
		ov5645_reg_write(client, 0x5303, 0x00);
		ov5645_reg_write(client, 0x5302, 0x08);
		scene_mode = 0;
		break;
	}
	ret = ov5645_read_scene_mode(sd);
	break;

	case V4L2_CID_CAMERA_BRIGHTNESS:

		if (ctrl->value > EV_PLUS_4)
			return -EINVAL;

		ov5645->brightness = ctrl->value;
		switch (ov5645->brightness) {
		case EV_MINUS_4:
			ov5645_reg_write(client, 0x3a0f, 0x18);
			ov5645_reg_write(client, 0x3a10, 0x10);
			ov5645_reg_write(client, 0x3a11, 0x30);
			ov5645_reg_write(client, 0x3a1b, 0x18);
			ov5645_reg_write(client, 0x3a1e, 0x10);
			ov5645_reg_write(client, 0x3a1f, 0x10);
			break;
		case EV_MINUS_2:
			ov5645_reg_write(client, 0x3a0f, 0x20);
			ov5645_reg_write(client, 0x3a10, 0x18);
			ov5645_reg_write(client, 0x3a11, 0x41);
			ov5645_reg_write(client, 0x3a1b, 0x20);
			ov5645_reg_write(client, 0x3a1e, 0x18);
			ov5645_reg_write(client, 0x3a1f, 0x10);
			break;
		case EV_PLUS_2:
			ov5645_reg_write(client, 0x3a0f, 0x48);
			ov5645_reg_write(client, 0x3a10, 0x40);
			ov5645_reg_write(client, 0x3a11, 0x80);
			ov5645_reg_write(client, 0x3a1b, 0x48);
			ov5645_reg_write(client, 0x3a1e, 0x40);
			ov5645_reg_write(client, 0x3a1f, 0x20);
			break;
		case EV_PLUS_4:
			ov5645_reg_write(client, 0x3a0f, 0x58);
			ov5645_reg_write(client, 0x3a10, 0x50);
			ov5645_reg_write(client, 0x3a11, 0x91);
			ov5645_reg_write(client, 0x3a1b, 0x58);
			ov5645_reg_write(client, 0x3a1e, 0x50);
			ov5645_reg_write(client, 0x3a1f, 0x20);
			break;
		default:
			ov5645_reg_write(client, 0x3a0f, 0x38);
			ov5645_reg_write(client, 0x3a10, 0x30);
			ov5645_reg_write(client, 0x3a11, 0x61);
			ov5645_reg_write(client, 0x3a1b, 0x38);
			ov5645_reg_write(client, 0x3a1e, 0x30);
			ov5645_reg_write(client, 0x3a1f, 0x10);
			break;
		}
		if (ret)
			return ret;
		break;
	case V4L2_CID_CAMERA_CONTRAST:

		ctrl->value = ctrl->value/20;
		if (ctrl->value >= CONTRAST_MAX)
			return -EINVAL;

		ov5645->contrast = ctrl->value;
		switch (ov5645->contrast) {
		case CONTRAST_MINUS_4:
			ov5645_reg_write(client, 0x5001, 0xff);
			ov5645_reg_write(client, 0x5580, 0x04);
			ov5645_reg_write(client, 0x5586, 0x10);
			ov5645_reg_write(client, 0x5585, 0x10);
			ov5645_reg_write(client, 0x5588, 0x41);
			break;
		case CONTRAST_MINUS_3:
			ov5645_reg_write(client, 0x5001, 0xff);
			ov5645_reg_write(client, 0x5580, 0x04);
			ov5645_reg_write(client, 0x5586, 0x14);
			ov5645_reg_write(client, 0x5585, 0x14);
			ov5645_reg_write(client, 0x5588, 0x41);
			break;
		case CONTRAST_MINUS_2:
			ov5645_reg_write(client, 0x5001, 0xff);
			ov5645_reg_write(client, 0x5580, 0x04);
			ov5645_reg_write(client, 0x5586, 0x18);
			ov5645_reg_write(client, 0x5585, 0x18);
			ov5645_reg_write(client, 0x5588, 0x41);
			break;
		case CONTRAST_MINUS_1:
			ov5645_reg_write(client, 0x5001, 0xff);
			ov5645_reg_write(client, 0x5580, 0x04);
			ov5645_reg_write(client, 0x5586, 0x1c);
			ov5645_reg_write(client, 0x5585, 0x1c);
			ov5645_reg_write(client, 0x5588, 0x41);
			break;
		case CONTRAST_PLUS_4:
			ov5645_reg_write(client, 0x5001, 0xff);
			ov5645_reg_write(client, 0x5580, 0x04);
			ov5645_reg_write(client, 0x5586, 0x30);
			ov5645_reg_write(client, 0x5585, 0x30);
			ov5645_reg_write(client, 0x5588, 0x41);
			break;
		case CONTRAST_PLUS_3:
			ov5645_reg_write(client, 0x5001, 0xff);
			ov5645_reg_write(client, 0x5580, 0x04);
			ov5645_reg_write(client, 0x5586, 0x2c);
			ov5645_reg_write(client, 0x5585, 0x2c);
			ov5645_reg_write(client, 0x5588, 0x41);
			break;
		case CONTRAST_PLUS_2:
			ov5645_reg_write(client, 0x5001, 0xff);
			ov5645_reg_write(client, 0x5580, 0x04);
			ov5645_reg_write(client, 0x5586, 0x28);
			ov5645_reg_write(client, 0x5585, 0x28);
			ov5645_reg_write(client, 0x5588, 0x41);
			break;
		case CONTRAST_PLUS_1:
			ov5645_reg_write(client, 0x5001, 0xff);
			ov5645_reg_write(client, 0x5580, 0x04);
			ov5645_reg_write(client, 0x5586, 0x24);
			ov5645_reg_write(client, 0x5585, 0x24);
			ov5645_reg_write(client, 0x5588, 0x41);
			break;
		default:
			ov5645_reg_write(client, 0x5001, 0xff);
			ov5645_reg_write(client, 0x5580, 0x04);
			ov5645_reg_write(client, 0x5586, 0x20);
			ov5645_reg_write(client, 0x5585, 0x20);
			ov5645_reg_write(client, 0x5588, 0x41);
			break;
			}
	break;
	case V4L2_CID_CAMERA_EFFECT:

		if (ctrl->value > IMAGE_EFFECT_BNW)
			return -EINVAL;

		ov5645->colorlevel = ctrl->value;

		switch (ov5645->colorlevel) {
		case IMAGE_EFFECT_BNW:
			ov5645_reg_write(client, 0x3212, 0x03);
			ov5645_reg_write(client, 0x5580, 0x1e);
			ov5645_reg_write(client, 0x5583, 0x80);
			ov5645_reg_write(client, 0x5584, 0x80);
			ov5645_reg_write(client, 0x5003, 0x08);
			ov5645_reg_write(client, 0x3212, 0x13);
			ov5645_reg_write(client, 0x3212, 0xa3);
			break;
		case IMAGE_EFFECT_SEPIA:
			ov5645_reg_write(client, 0x3212, 0x03);
			ov5645_reg_write(client, 0x5580, 0x1e);
			ov5645_reg_write(client, 0x5583, 0x40);
			ov5645_reg_write(client, 0x5584, 0xa0);
			ov5645_reg_write(client, 0x5003, 0x08);
			ov5645_reg_write(client, 0x3212, 0x13);
			ov5645_reg_write(client, 0x3212, 0xa3);
			break;
		case IMAGE_EFFECT_NEGATIVE:
			ov5645_reg_write(client, 0x3212, 0x03);
			ov5645_reg_write(client, 0x5580, 0x46);
			ov5645_reg_write(client, 0x5583, 0x40);
			ov5645_reg_write(client, 0x5584, 0x30);
			ov5645_reg_write(client, 0x5003, 0x08);
			ov5645_reg_write(client, 0x3212, 0x13);
			ov5645_reg_write(client, 0x3212, 0xa3);
			break;
		default:
			ov5645_reg_write(client, 0x3212, 0x03);
			ov5645_reg_write(client, 0x5580, 0x06);
			ov5645_reg_write(client, 0x5583, 0x40);
			ov5645_reg_write(client, 0x5584, 0x30);
			ov5645_reg_write(client, 0x5003, 0x08);
			ov5645_reg_write(client, 0x3212, 0x13);
			ov5645_reg_write(client, 0x3212, 0xa3);
			break;
		}
		msleep(50);

		break;
	case V4L2_CID_SATURATION:

		if (ctrl->value > OV5645_SATURATION_MAX)
			return -EINVAL;

		ov5645->saturation = ctrl->value;
		switch (ov5645->saturation) {
		case OV5645_SATURATION_MIN:
			break;
		case OV5645_SATURATION_MAX:
			break;
		default:
			break;
		}
		if (ret)
			return ret;
		break;
	case V4L2_CID_SHARPNESS:

		if (ctrl->value > OV5645_SHARPNESS_MAX)
			return -EINVAL;

		ov5645->sharpness = ctrl->value;
		switch (ov5645->sharpness) {
		case OV5645_SHARPNESS_MIN:
			break;
		case OV5645_SHARPNESS_MAX:
			break;
		default:
			break;
		}
		if (ret)
			return ret;
		break;
	case V4L2_CID_CAMERA_METERING_EXPOSURE:

	if ((ctrl->value >= METERING_MAX) &&
		ctrl->value <= METERING_BASE)
		return -EINVAL;

	ov5645->metering_exposure = ctrl->value;
	switch (ov5645->metering_exposure) {
	case METERING_CENTER:
		ov5645_reg_write(client, 0x5688, 0x62);
		ov5645_reg_write(client, 0x5689, 0x26);
		ov5645_reg_write(client, 0x568a, 0xe6);
		ov5645_reg_write(client, 0x568b, 0x6e);
		ov5645_reg_write(client, 0x568c, 0xea);
		ov5645_reg_write(client, 0x568d, 0xae);
		ov5645_reg_write(client, 0x568e, 0xa6);
		ov5645_reg_write(client, 0x568f, 0x6a);
		break;
	case METERING_SPOT:
		ov5645_reg_write(client, 0x5688, 0xa6);
		ov5645_reg_write(client, 0x5689, 0x6a);
		ov5645_reg_write(client, 0x568a, 0xea);
		ov5645_reg_write(client, 0x568b, 0xae);
		ov5645_reg_write(client, 0x568c, 0xa6);
		ov5645_reg_write(client, 0x568d, 0x6a);
		ov5645_reg_write(client, 0x568e, 0x62);
		ov5645_reg_write(client, 0x568f, 0x26);
		break;
	default:
		break;
	}
	if (ret)
		return ret;
	break;

	case V4L2_CID_CAMERA_ANTI_BANDING:

		if (ctrl->value > ANTI_BANDING_60HZ)
			return -EINVAL;

		ov5645->antibanding = ctrl->value;

		switch (ov5645->antibanding) {
		case ANTI_BANDING_50HZ:
			ov5645_reg_write(client, 0x3c00, 0x04);
			break;
		case ANTI_BANDING_60HZ:
			ov5645_reg_write(client, 0x3c00, 0x00);
			break;
		default:
			ov5645_reg_write(client, 0x3622, 0x01);
			ov5645_reg_write(client, 0x3635, 0x1c);
			ov5645_reg_write(client, 0x3634, 0x40);
			ov5645_reg_write(client, 0x3c01, 0x34);

			ov5645_reg_write(client, 0x3c00, 0x00);
			ov5645_reg_write(client, 0x3c04, 0x28);
			ov5645_reg_write(client, 0x3c05, 0x98);
			ov5645_reg_write(client, 0x3c06, 0x00);
			ov5645_reg_write(client, 0x3c07, 0x08);
			ov5645_reg_write(client, 0x3c08, 0x00);
			ov5645_reg_write(client, 0x3c09, 0x1c);
			ov5645_reg_write(client, 0x300c, 0x22);
			ov5645_reg_write(client, 0x3c0a, 0x9c);
			ov5645_reg_write(client, 0x3c0b, 0x40);

			break;
		}
		if (ret)
			return ret;
		break;

	case V4L2_CID_CAMERA_ISO:

		if (ctrl->value >= ISO_MAX)
			return -EINVAL;
		ov5645->isomode = ctrl->value;
		switch (ov5645->isomode) {
		case ISO_AUTO:
			ov5645_reg_write(client, 0x3a18, 0x00);
			ov5645_reg_write(client, 0x3a19, 0xf8);
			break;
		case ISO_100:
			ov5645_reg_write(client, 0x3a18, 0x00);
			ov5645_reg_write(client, 0x3a19, 0x60);
			break;
		case ISO_200:
			ov5645_reg_write(client, 0x3a18, 0x00);
			ov5645_reg_write(client, 0x3a19, 0x90);
			break;
		case ISO_400:
			ov5645_reg_write(client, 0x3a18, 0x00);
			ov5645_reg_write(client, 0x3a19, 0xc0);
			break;
		case ISO_800:
			ov5645_reg_write(client, 0x3a18, 0x00);
			ov5645_reg_write(client, 0x3a19, 0xf0);
		default:
			break;
		}
		break;

	case V4L2_CID_CAMERA_SHARPNESS:
		ctrl->value = ctrl->value/20;
		if (ctrl->value >= SHARPNESS_MAX)
			return -EINVAL;

		ov5645->sharpness = ctrl->value;

		switch (ov5645->sharpness) {
		case SHARPNESS_MINUS_5:
			ov5645_reg_write(client, 0x5308, 0x65);
			ov5645_reg_write(client, 0x5302, 0x00);
			break;
		case SHARPNESS_MINUS_4:
			ov5645_reg_write(client, 0x5308, 0x65);
			ov5645_reg_write(client, 0x5302, 0x02);
			break;
		case SHARPNESS_MINUS_3:
			ov5645_reg_write(client, 0x5308, 0x65);
			ov5645_reg_write(client, 0x5302, 0x04);
			break;
		case SHARPNESS_MINUS_2:
			ov5645_reg_write(client, 0x5308, 0x65);
			ov5645_reg_write(client, 0x5302, 0x08);
			break;
		case SHARPNESS_MINUS_1:
			ov5645_reg_write(client, 0x5308, 0x65);
			ov5645_reg_write(client, 0x5302, 0x0C);
			break;
		case SHARPNESS_PLUS_5:
			ov5645_reg_write(client, 0x5308, 0x65);
			ov5645_reg_write(client, 0x5302, 0x24);
			break;
		case SHARPNESS_PLUS_4:
			ov5645_reg_write(client, 0x5308, 0x65);
			ov5645_reg_write(client, 0x5302, 0x22);
			break;
		case SHARPNESS_PLUS_3:
			ov5645_reg_write(client, 0x5308, 0x65);
			ov5645_reg_write(client, 0x5302, 0x20);
			break;
		case SHARPNESS_PLUS_2:
			ov5645_reg_write(client, 0x5308, 0x65);
			ov5645_reg_write(client, 0x5302, 0x18);
			break;
		case SHARPNESS_PLUS_1:
			ov5645_reg_write(client, 0x5308, 0x65);
			ov5645_reg_write(client, 0x5302, 0x14);
			break;
		default:
			ov5645_reg_write(client, 0x5308, 0x65);
			ov5645_reg_write(client, 0x5302, 0x10);
			break;
		}
	break;

	case V4L2_CID_CAMERA_WHITE_BALANCE:

		if (ctrl->value > WHITE_BALANCE_MAX)
			return -EINVAL;

		ov5645->whitebalance = ctrl->value;

		switch (ov5645->whitebalance) {
		case WHITE_BALANCE_FLUORESCENT:
			ov5645_reg_write(client, 0x3212, 0x03);
			ov5645_reg_write(client, 0x3406, 0x01);
			ov5645_reg_write(client, 0x3400, 0x06);
			ov5645_reg_write(client, 0x3401, 0x48);
			ov5645_reg_write(client, 0x3402, 0x06);
			ov5645_reg_write(client, 0x3403, 0x00);
			ov5645_reg_write(client, 0x3404, 0x0a);
			ov5645_reg_write(client, 0x3405, 0xd3);
			ov5645_reg_write(client, 0x3212, 0x13);
			ov5645_reg_write(client, 0x3212, 0xa3);
			break;
		case WHITE_BALANCE_SUNNY:
			ov5645_reg_write(client, 0x3212, 0x03);
			ov5645_reg_write(client, 0x3406, 0x01);
			ov5645_reg_write(client, 0x3400, 0x06);
			ov5645_reg_write(client, 0x3401, 0x1c);
			ov5645_reg_write(client, 0x3402, 0x04);
			ov5645_reg_write(client, 0x3403, 0x00);
			ov5645_reg_write(client, 0x3404, 0x04);
			ov5645_reg_write(client, 0x3405, 0xf3);
			ov5645_reg_write(client, 0x3212, 0x13);
			ov5645_reg_write(client, 0x3212, 0xa3);
			break;
		case WHITE_BALANCE_CLOUDY:
			ov5645_reg_write(client, 0x3212, 0x03);
			ov5645_reg_write(client, 0x3406, 0x01);
			ov5645_reg_write(client, 0x3400, 0x08);
			ov5645_reg_write(client, 0x3401, 0x1e);
			ov5645_reg_write(client, 0x3402, 0x06);
			ov5645_reg_write(client, 0x3403, 0x02);
			ov5645_reg_write(client, 0x3404, 0x04);
			ov5645_reg_write(client, 0x3405, 0xf3);
			ov5645_reg_write(client, 0x3212, 0x13);
			ov5645_reg_write(client, 0x3212, 0xa3);
			break;
		case WHITE_BALANCE_TUNGSTEN:
			ov5645_reg_write(client, 0x3212, 0x03);
			ov5645_reg_write(client, 0x3406, 0x01);
			ov5645_reg_write(client, 0x3400, 0x04);
			ov5645_reg_write(client, 0x3401, 0x10);
			ov5645_reg_write(client, 0x3402, 0x04);
			ov5645_reg_write(client, 0x3403, 0x00);
			ov5645_reg_write(client, 0x3404, 0x08);
			ov5645_reg_write(client, 0x3405, 0x40);
			ov5645_reg_write(client, 0x3212, 0x13);
			ov5645_reg_write(client, 0x3212, 0xa3);
			break;
		case WHITE_BALANCE_DAYLIGHT:
			ov5645_reg_write(client, 0x3212, 0x03);
			ov5645_reg_write(client, 0x3406, 0x01);
			ov5645_reg_write(client, 0x3400, 0x06);
			ov5645_reg_write(client, 0x3401, 0x1c);
			ov5645_reg_write(client, 0x3402, 0x04);
			ov5645_reg_write(client, 0x3403, 0x00);
			ov5645_reg_write(client, 0x3404, 0x04);
			ov5645_reg_write(client, 0x3405, 0xf3);
			ov5645_reg_write(client, 0x3212, 0x13);
			ov5645_reg_write(client, 0x3212, 0xa3);
			break;
		case WHITE_BALANCE_INCANDESCENT:
			ov5645_reg_write(client, 0x3212, 0x03);
			ov5645_reg_write(client, 0x3406, 0x01);
			ov5645_reg_write(client, 0x3400, 0x04);
			ov5645_reg_write(client, 0x3401, 0x10);
			ov5645_reg_write(client, 0x3402, 0x04);
			ov5645_reg_write(client, 0x3403, 0x00);
			ov5645_reg_write(client, 0x3404, 0x08);
			ov5645_reg_write(client, 0x3405, 0x40);
			ov5645_reg_write(client, 0x3212, 0x13);
			ov5645_reg_write(client, 0x3212, 0xa3);
			break;
		default:  /*auto*/
			ov5645_reg_write(client, 0x3212, 0x03);
			ov5645_reg_write(client, 0x3406, 0x00);
			ov5645_reg_write(client, 0x3400, 0x04);
			ov5645_reg_write(client, 0x3401, 0x00);
			ov5645_reg_write(client, 0x3402, 0x04);
			ov5645_reg_write(client, 0x3403, 0x00);
			ov5645_reg_write(client, 0x3404, 0x04);
			ov5645_reg_write(client, 0x3405, 0x00);
			ov5645_reg_write(client, 0x3212, 0x13);
			ov5645_reg_write(client, 0x3212, 0xa3);
			break;
		}
		msleep(50);

		break;

	case V4L2_CID_CAMERA_FRAME_RATE:

		if (ctrl->value > FRAME_RATE_30)
			return -EINVAL;

		if ((ov5645->i_size < OV5645_SIZE_QVGA) ||
		    (ov5645->i_size > OV5645_SIZE_1280x960)) {
			if (ctrl->value == FRAME_RATE_30 ||
			    ctrl->value == FRAME_RATE_AUTO)
				return 0;
			else
				return -EINVAL;
		}

		ov5645->framerate = ctrl->value;
printk(KERN_INFO "ov5645 framerate = %d  ", ov5645->framerate);
		ov5645_reg_write(client, 0x4202, 0x0f);
		ov5645_reg_write(client, 0x3503, 0x03);
		msleep(50);
		switch (ov5645->framerate) {
		case FRAME_RATE_5:
			ret = ov5645_reg_writes(client, ov5645_fps_5);
			break;
		case FRAME_RATE_7:
			ret = ov5645_reg_writes(client, ov5645_fps_7);
			break;
		case FRAME_RATE_10:
			ret = ov5645_reg_writes(client, ov5645_fps_10);
			break;
		case FRAME_RATE_15:
			ret = ov5645_reg_writes(client, ov5645_fps_15);
			break;
		case FRAME_RATE_20:
			ret = ov5645_reg_writes(client, ov5645_fps_20);
			break;
		case FRAME_RATE_25:
			ret = ov5645_reg_writes(client, ov5645_fps_25);
			break;
		case FRAME_RATE_30:
			ret = ov5645_reg_writes(client, ov5645_fps_30);
		case FRAME_RATE_AUTO:
		default:
			break;
		}
		msleep(100);
		ov5645_reg_write(client, 0x4202, 0x00);
		if (ret)
			return ret;
		break;

	case V4L2_CID_CAMERA_FOCUS_MODE:

		iprintk("set focus_mode %d", ov5645->focus_mode);

		/*
		 * Donot start the AF cycle here
		 * AF Start will be called later in
		 * V4L2_CID_CAMERA_SET_AUTO_FOCUS only for auto, macro mode
		 * it wont be called for infinity.
		 * Donot worry about resolution change for now.
		 * From userspace we set the resolution first
		 * and then set the focus mode.
		 */
		switch (ctrl->value) {
		case FOCUS_MODE_MACRO:
			ov5645->focus_mode = ctrl->value;
			ret = ov5645_af_release(client);
			break;

		case FOCUS_MODE_INFINITY:
			ov5645->focus_mode = ctrl->value;
			ret = ov5645_af_infinity(client);
			break;

		case FOCUS_MODE_AUTO:
			ov5645->focus_mode = ctrl->value;
			ret = ov5645_af_release(client);
			break;

		case FOCUS_MODE_CONTINUOUS_PICTURE:
			ov5645->focus_mode = ctrl->value;
			ret = ov5645_enable_af_continuous(client);
			break;

		default:
			iprintk("Invalid Auto Focus Mode :%s:\n",
					__func__);
			return -EINVAL;
		}

		if (ret)
			return ret;
		break;

	case V4L2_CID_CAMERA_TOUCH_AF_AREA:


		if (ov5645->touch_focus < OV5645_MAX_FOCUS_AREAS) {
			v4l2_touch_area touch_area;
			if (copy_from_user(&touch_area,
					   (v4l2_touch_area *) ctrl->value,
					   sizeof(v4l2_touch_area)))
				return -EINVAL;

			iprintk("z=%d x=0x%x y=0x%x w=0x%x h=0x%x weight=0x%x",
				ov5645->touch_focus, touch_area.leftTopX,
				touch_area.leftTopY, touch_area.rightBottomX,
				touch_area.rightBottomY, touch_area.weight);

			ret = ov5645_af_zone_conv(client, &touch_area,
						  ov5645->touch_focus);
			if (ret == 0)
				ov5645->touch_focus++;
			ret = 0;

		} else
			dev_dbg(&client->dev,
				"Maximum touch focus areas already set\n");

		break;

	case V4L2_CID_CAMERA_SET_AUTO_FOCUS:

		if (ctrl->value > AUTO_FOCUS_ON)
			return -EINVAL;

		/* start and stop af cycle here */
		switch (ctrl->value) {

		case AUTO_FOCUS_OFF:

			if (atomic_read(&ov5645->focus_status)
			    == OV5645_FOCUSING) {
				ret = ov5645_af_release(client);
				atomic_set(&ov5645->focus_status,
					   OV5645_NOT_FOCUSING);
			}
			ov5645->touch_focus = 0;
			break;

		case AUTO_FOCUS_ON:
			/* check if preflash is needed */
			ret = ov5645_pre_flash(client);
			if (ov5645->focus_mode == FOCUS_MODE_CONTINUOUS_PICTURE)
				ret = ov5645_af_pause(client);
			else if (ov5645->focus_mode == FOCUS_MODE_AUTO)
				ret = ov5645_af_start(client);
			else if (ov5645->focus_mode == FOCUS_MODE_MACRO)
				ret = ov5645_af_macro(client);
			else if (ov5645->focus_mode == FOCUS_MODE_INFINITY)
				ret = ov5645_af_infinity(client);

			atomic_set(&ov5645->focus_status, OV5645_FOCUSING);
			break;
		}

		if (ret)
			return ret;
		break;
	case V4L2_CID_CAMERA_FLASH_MODE:
		ov5645_set_flash_mode(ctrl->value, client);
		break;

	case V4L2_CID_CAM_PREVIEW_ONOFF:
		{
			printk(KERN_INFO
			       "ov5645 PREVIEW_ONOFF:%d runmode = %d\n",
			       ctrl->value, runmode);
			if (ctrl->value)
				runmode = CAM_RUNNING_MODE_PREVIEW;
			else
				runmode = CAM_RUNNING_MODE_NOTREADY;
			break;
		}

	case V4L2_CID_CAM_RECORD:
		if (ctrl->value == START_RECORD) {
			ov5645_af_relaunch(client, 1);
			ret = ov5645_enable_af_continuous(client);
		}
		else if (ctrl->value == STOP_RECORD)
			ret = ov5645_af_release(client);
		break;

	case V4L2_CID_CAM_CAPTURE:
		printk(KERN_INFO "ov5645 runmode = capture\n");
		runmode = CAM_RUNNING_MODE_CAPTURE;
		if (ov5645->fireflash) {
			ov5645->fireflash = 0;
			ov5645_flash_control(client, FLASH_MODE_ON);
			msleep(50);
			mod_timer(&flash_timer,
			jiffies + msecs_to_jiffies(FLASH_TIMEOUT_MS));
		}
		capture_mode = 1;
		break;

	case V4L2_CID_CAM_CAPTURE_DONE:
		printk(KERN_INFO "ov5645 runmode = capture_done\n");
		runmode = CAM_RUNNING_MODE_CAPTURE_DONE;
		break;

	case V4L2_CID_PARAMETERS:
		dev_info(&client->dev, "ov5645 capture parameters\n");
		ov_capture_width  = ov5645_frmsizes[ctrl->value].width;
		ov_capture_height = ov5645_frmsizes[ctrl->value].height;
		break;
	}
	return ret;
}

static int ov5645_set_flash_mode(int mode, struct i2c_client *client)
{
	int ret = 0;
	struct ov5645 *ov5645 = to_ov5645(client);

	switch (mode) {
	case FLASH_MODE_ON:
		ov5645->flashmode = mode;
		break;
	case FLASH_MODE_AUTO:
		ov5645->flashmode = mode;
		break;
	case FLASH_MODE_TORCH_ON:
		ov5645->flashmode = mode;
		mic2871_led((ov5645->flashmode >> 1
		&& CAM_LED_ON), (ov5645->flashmode >> 1 &
		CAM_LED_MODE_TORCH));
		break;
	case FLASH_MODE_TORCH_OFF:
	case FLASH_MODE_OFF:
	default:
		ov5645_flash_control(client, mode);
		ov5645->flashmode = mode;
		break;
	}

	return ret;
}

static long ov5645_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
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
			p->focus_distance[0] = 10;	/* near focus in cm */
			p->focus_distance[1] = 100;	/* optimal focus
							in cm */
			p->focus_distance[2] = -1;	/* infinity */
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

static int ov5645_init(struct i2c_client *client)
{
	struct ov5645 *ov5645 = to_ov5645(client);
	int ret = 0;
	/* Power Up, Start Streaming for AF Init */
	/* default brightness and contrast */
	ov5645->brightness = EV_DEFAULT;
	ov5645->contrast = CONTRAST_DEFAULT;
	ov5645->colorlevel = IMAGE_EFFECT_NONE;
	ov5645->antibanding = ANTI_BANDING_AUTO;
	ov5645->whitebalance = WHITE_BALANCE_AUTO;
	ov5645->framerate = FRAME_RATE_AUTO;
	ov5645->focus_mode = FOCUS_MODE_AUTO;
	ov5645->touch_focus = 0;
	atomic_set(&ov5645->focus_status, OV5645_NOT_FOCUSING);
	ov5645->flashmode = FLASH_MODE_OFF;
	ov5645->fireflash = 0;

	dev_dbg(&client->dev, "Sensor initialized\n");

	return ret;
}

static void ov5645_video_remove(struct soc_camera_device *icd)
{
	/*dev_dbg(&icd->dev, "Video removed: %p, %p\n",
		icd->dev.parent, icd->vdev);*/
}

static struct v4l2_subdev_core_ops ov5645_subdev_core_ops = {
	.s_power = ov5645_s_power,
	.g_chip_ident = ov5645_g_chip_ident,
	.g_ctrl = ov5645_g_ctrl,
	.s_ctrl = ov5645_s_ctrl,
	.ioctl = ov5645_ioctl,
	/*.queryctrl = ov5645_queryctrl,*/
};

static int ov5645_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5645 *priv = to_ov5645(client);
	struct v4l2_rect *rect = &a->c;

	/*
	*Eventhough the capture gain registers get
	*read( in config capture) and  used  in config preview,
	*flash is not required during that time.
	*But flash is really needed before s_stream and before the rcu capture
	*/

	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rect->top	= 0;
	rect->left	= 0;
	rect->width	= priv->width;
	rect->height	= priv->height;
	dev_info(&client->dev, "%s: width = %d height = %d\n", __func__
						, rect->width, rect->height);

	return 0;
}
static int ov5645_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5645 *priv = to_ov5645(client);

	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= priv->width;
	a->bounds.height		= priv->height;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	dev_info(&client->dev, "%s: width =  %d height =  %d\n", __func__
					, a->bounds.width, a->bounds.height);
	return 0;
}

static int ov5645_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(ov5645_fmts))
		return -EINVAL;

	*code = ov5645_fmts[index].code;
	return 0;
}

static int ov5645_enum_framesizes(struct v4l2_subdev *sd,
				  struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index >= OV5645_SIZE_LAST)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->pixel_format = V4L2_PIX_FMT_UYVY;

	fsize->discrete = ov5645_frmsizes[fsize->index];

	return 0;
}

/* we only support fixed frame rate */
static int ov5645_enum_frameintervals(struct v4l2_subdev *sd,
				      struct v4l2_frmivalenum *interval)
{
	int size;

	if (interval->index >= 1)
		return -EINVAL;

	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;

	size = ov5645_find_framesize(interval->width, interval->height);

	switch (size) {
	case OV5645_SIZE_5MP:
		interval->discrete.numerator = 2;
		interval->discrete.denominator = 15;
		break;
	case OV5645_SIZE_QXGA:
	case OV5645_SIZE_UXGA:
		interval->discrete.numerator = 1;
		interval->discrete.denominator = 15;
		break;
	case OV5645_SIZE_720P:
		interval->discrete.numerator = 1;
		interval->discrete.denominator = 0;
		break;
	case OV5645_SIZE_VGA:
	case OV5645_SIZE_QVGA:
	case OV5645_SIZE_1280x960:
	default:
		interval->discrete.numerator = 1;
		interval->discrete.denominator = 24;
		break;
	}
/*	printk(KERN_ERR"%s: width=%d height=%d fi=%d/%d\n", __func__,
			interval->width,
			interval->height, interval->discrete.numerator,
			interval->discrete.denominator);
			*/
	return 0;
}

static int ov5645_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5645 *ov5645 = to_ov5645(client);
	struct v4l2_captureparm *cparm;

	if (param->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	cparm = &param->parm.capture;

	memset(param, 0, sizeof(*param));
	param->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	cparm->capability = V4L2_CAP_TIMEPERFRAME;

	switch (ov5645->i_size) {
	case OV5645_SIZE_5MP:
		cparm->timeperframe.numerator = 2;
		cparm->timeperframe.denominator = 15;
		break;
	case OV5645_SIZE_QXGA:
	case OV5645_SIZE_UXGA:
		cparm->timeperframe.numerator = 1;
		cparm->timeperframe.denominator = 15;
		break;
	case OV5645_SIZE_1280x960:
	case OV5645_SIZE_720P:
	case OV5645_SIZE_VGA:
	case OV5645_SIZE_QVGA:
	default:
		cparm->timeperframe.numerator = 1;
		cparm->timeperframe.denominator = 24;
		break;
	}

	return 0;
}

static int ov5645_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	/*
	 * FIXME: This just enforces the hardcoded framerates until this is
	 *flexible enough.
	 */
	return ov5645_g_parm(sd, param);
}

static struct v4l2_subdev_video_ops ov5645_subdev_video_ops = {
	.s_stream = ov5645_s_stream,
	.s_mbus_fmt = ov5645_s_fmt,
	.g_mbus_fmt = ov5645_g_fmt,
	.g_crop	    = ov5645_g_crop,
	.cropcap     = ov5645_cropcap,
	.try_mbus_fmt = ov5645_try_fmt,
	.enum_mbus_fmt = ov5645_enum_fmt,
	.enum_mbus_fsizes = ov5645_enum_framesizes,
	.enum_framesizes = ov5645_enum_framesizes,
	.enum_frameintervals = ov5645_enum_frameintervals,
	.g_parm = ov5645_g_parm,
	.s_parm = ov5645_s_parm,
};

static struct v4l2_subdev_ops ov5645_subdev_ops = {
	.core = &ov5645_subdev_core_ops,
	.video = &ov5645_subdev_video_ops,
};

static int ov5645_video_probe(struct i2c_client *client)
{
	int ret = 0;
	u8 id_high, id_low;
	u16 id;
	struct ov5645 *ov5645 = to_ov5645(client);
	struct ov5645_otp *otp = &(ov5645->otp);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);

	pr_info("%s E\n", __func__);
	ret = ov5645_s_power(subdev, 1);
	if (ret < 0)
		return ret;

	/* Read sensor Model ID */
	ret = ov5645_reg_read(client, OV5645_CHIP_ID_HIGH, &id_high);
	if (ret < 0)
		goto out;

	id = id_high << 8;

	ret = ov5645_reg_read(client, OV5645_CHIP_ID_LOW, &id_low);
	if (ret < 0)
		goto out;

	id |= id_low;
	pr_info("%s: Read ID: 0x%x\n", __func__, id);

	if (id != OV5645_CHIP_ID) {
		ret = -ENODEV;
		goto out;
	}

	pr_info("Detected ov5645 chip 0x%04x\n", id);

	ret = ov5645_otp_read(client);
	if (ret < 0)
		goto out;

	if (otp->module_integrator_id == 5)
		pr_info("Detected Darling module\n");
	else if (otp->module_integrator_id == 2)
		pr_info("Detected Truly module\n");
	else
		pr_info("Detected module id %d\n", otp->module_integrator_id);

out:
	ov5645_s_power(subdev, 0);
	pr_info("%s X: %d\n", __func__, ret);
	return ret;
}

static int ov5645_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{

	struct ov5645 *ov5645;
	struct soc_camera_link *icl = client->dev.platform_data;
	int ret = 0;

	if (!icl) {
		dev_err(&client->dev, "OV5645 driver needs platform data\n");
		return -EINVAL;
	}


	if (!icl->priv) {
		dev_err(&client->dev,
			"OV5645 driver needs i/f platform data\n");
		return -EINVAL;
	}

	ov5645 = kzalloc(sizeof(struct ov5645), GFP_KERNEL);
	if (!ov5645)
		return -ENOMEM;

	client->addr = OV5645_CHIP_I2C_ADDR;
	v4l2_i2c_subdev_init(&ov5645->subdev, client, &ov5645_subdev_ops);

	/* Second stage probe - when a capture adapter is there */
	/* icd->ops = &ov5645_ops; */

	ov5645_video_probe(client);

	ov5645->i_size = OV5645_SIZE_VGA;
	ov5645->i_fmt = 0;	/* First format in the list */
	ov5645->plat_parms = icl->priv;
	ov5645->width = 640;
	ov5645->height = 480;
	ret = ov5645_init(client);
	if (ret) {
		dev_err(&client->dev, "Failed to initialize sensor\n");
		ret = -EINVAL;
	}
	init_timer(&flash_timer);
	setup_timer(&flash_timer, flash_timer_callback, 0);
	return ret;
}

static int ov5645_remove(struct i2c_client *client)
{
	struct ov5645 *ov5645 = to_ov5645(client);
	struct soc_camera_device *icd = client->dev.platform_data;

	/* icd->ops = NULL; */
	ov5645_video_remove(icd);
	client->driver = NULL;
	kfree(ov5645);

	return 0;
}

static const struct i2c_device_id ov5645_id[] = {
	{"OV5645", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ov5645_id);

static struct i2c_driver ov5645_i2c_driver = {
	.driver = {
		   .name = "OV5645",
		   },
	.probe = ov5645_probe,
	.remove = ov5645_remove,
	.id_table = ov5645_id,
};

static int __init ov5645_mod_init(void)
{
	return i2c_add_driver(&ov5645_i2c_driver);
}

static void __exit ov5645_mod_exit(void)
{
	i2c_del_driver(&ov5645_i2c_driver);
}

module_init(ov5645_mod_init);
module_exit(ov5645_mod_exit);

MODULE_DESCRIPTION("OmniVision OV5645 Camera driver");
MODULE_AUTHOR("Sergio Aguirre <saaguirre@ti.com>");
MODULE_LICENSE("GPL v2");
