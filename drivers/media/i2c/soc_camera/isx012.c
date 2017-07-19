/*
 * Driver for Sony ISX012 Camera
 *
 * Copyright (C) 2012-2013 Renesas Mobile Corp.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* for debug */
/* #undef DEBUG */
#define DEBUG 1
/* #define DEBUG */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/v4l2-mediabus.h>
#include <linux/module.h>
#include <linux/videodev2.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/d2153/core.h>

#include <media/soc_camera.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-ctrls.h>
#include <media/sh_mobile_csi2.h>
#include <media/sh_mobile_rcu.h>

#include <mach/r8a7373.h>
int flash_check;


#define ISX012_SLAVE_ADDR     (0x3D)


static ssize_t maincamtype_ISX012_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	char *sensorname = "ISX012";
	return sprintf(buf, "%s\n", sensorname);
}

static ssize_t maincamfw_ISX012_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	char *sensorfw = "ISX012 N";
	return sprintf(buf, "%s\n", sensorfw);
}

static ssize_t maincamflash_ISX012_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	if ((0 >= count) || !buf)
		return 0;
	if (buf[0] == '0') {
		sh_mobile_rcu_flash(0);
		flash_check = 0;
	} else {
		sh_mobile_rcu_flash(1);
		flash_check = 1;
	}
	return count;
}

static ssize_t mainvendorid_ISX012_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	char *vendorid = "0x0A04";
	return sprintf(buf, "%s\n", vendorid);
}

static DEVICE_ATTR(rear_camtype, 0444, maincamtype_ISX012_show, NULL);
static DEVICE_ATTR(rear_camfw, 0444, maincamfw_ISX012_show, NULL);
static DEVICE_ATTR(rear_flash, 0200, NULL, maincamflash_ISX012_store);
static DEVICE_ATTR(rear_vendorid, 0444, mainvendorid_ISX012_show, NULL);

typedef struct isx012_regset_t {
	u16 usRegs;
	u16 usData;
	u8 ucLen;
} _isx012_regset_t;

struct ISX012_datafmt {
	enum v4l2_mbus_pixelcode	code;
	enum v4l2_colorspace		colorspace;
};

struct ISX012 {
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler hdl;
	const struct ISX012_datafmt	*fmt;
	unsigned int			width;
	unsigned int			height;
};

static const struct ISX012_datafmt ISX012_colour_fmts[] = {
	{V4L2_MBUS_FMT_SBGGR10_1X10,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SGBRG10_1X10,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SGRBG10_1X10,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_SRGGB10_1X10,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_UYVY8_2X8,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_VYUY8_2X8,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_YUYV8_2X8,	V4L2_COLORSPACE_SRGB},
	{V4L2_MBUS_FMT_YVYU8_2X8,	V4L2_COLORSPACE_SRGB},
};

static int _ISX012_i2c_send(struct i2c_client *client, const u8 *data, int len)
{
	int ret = 0;
	if (len <= 0) {
		printk(KERN_ERR "%s(): invalid length %d", __func__, len);
		return -EINVAL;
	}

	ret = i2c_master_send(client, data, len);
	if (ret < 0) {
		printk(KERN_ERR "Failed to send %d bytes [errno=%d]",
			len, ret);
		return ret;
	} else if (ret != len) {
		printk(
		KERN_ERR "Failed to send exactly %d bytes (send %d)",
		len, ret);
		return -EIO;
	} else {
		ret = 0;
	}
	return ret;
}

static int _ISX012_i2c_read(struct i2c_client *client, const u8 *data, int len)
{
	int ret = 0;
	unsigned char buf[1];
	if (len <= 0) {
		printk(KERN_ERR "%s(): invalid length %d", __func__, len);
		return -EINVAL;
	}

	ret = i2c_master_send(client, data, len);
	if (ret < 0) {
		printk(KERN_ERR "Failed to send %d bytes [errno=%d]",
			len, ret);
		return ret;
	} else if (ret != len) {
		printk(
		KERN_ERR "Failed to send exactly %d bytes (send %d)",
		len, ret);
		return -EIO;
	} else {
		ret = 0;
	}

	ret = i2c_master_recv(client, buf, 1);
   if (ret < 0) {
		printk(KERN_ERR "Failed to recv %d bytes [errno=%d]",
			len, ret);
		return ret;
	} else if (ret != 1) {
		printk(
		KERN_ERR "Failed to recv exactly %d bytes (send %d)",
		len, ret);
		return -EIO;
	} else {
		ret = 0;
	}
	return buf[0] & 0xff;
}

int ISX012_waitForModeTransition(struct device *dev, int shift)
{
/*
	shift value is 0 : Wait for Mode Transition (OM)
	shift value is 1 : Wait for Mode Transition (CM)
	shift value is 2 : Wait for JPEG_UPDATE
	shift value is 3 : Wait for VINT
*/

   struct i2c_client *pClient = container_of(dev, struct i2c_client, dev);
	int     iRoofOutCnt = 50;
	u8      ucValue = 0;
	int     readValue = 0;
	u16     readAddr = 0x000E;    /* INTSTS */
	u16     writeAddr = 0x0012;   /*  INTCLR */
	u8      writeRegs[3] = {0, };
	u8      readRegs[2] = {0, };
   int     iRet = 0;
   u8      length = 0;

	printk(KERN_ERR "%s(): START ISX012_waitForModeTransition\n", __func__);

	do {
	   mdelay(10);

	   readRegs[0] = (unsigned char)(readAddr >> 8) & 0x00FF;
	   readRegs[1] = (unsigned char)(readAddr >> 0) & 0x00FF;
		length = 2;
		readValue = _ISX012_i2c_read(pClient, readRegs, length);
	   if (readValue < 0) {
	       printk(KERN_ERR "%s error iRet=%d\n", __func__, readAddr);
	       return readAddr;
	   }

	   printk(KERN_ALERT "%s REG=0x%04x INTSTS=0x%02x\n", __func__, readAddr , readValue);

	   ucValue = ((readValue & (0x01 << shift)) >> shift);
	   printk(KERN_ALERT"%s REG=0x%04x ucValue=0x%02x\n", __func__, readAddr, ucValue);
	   iRoofOutCnt--;
	} while ((ucValue != 1) && iRoofOutCnt);
	printk(KERN_ALERT "%s REG=0x%04x iRoofOutCnt=%d\n", __func__, readAddr, iRoofOutCnt);

	iRoofOutCnt = 50;
	do {
		writeRegs[0] = (unsigned char)(writeAddr >> 8) & 0x00FF;
		writeRegs[1] = (unsigned char)(writeAddr >> 0) & 0x00FF;
		writeRegs[2] = 0x01;
      length = 3;
	  iRet = _ISX012_i2c_send(pClient, writeRegs, length);
	   if (iRet < 0) {
	       printk(KERN_ERR " : error iRet=%d\n", iRet);
	       return iRet;
	   }
	   iRet = 0;
	   readRegs[0] = (unsigned char)(readAddr >> 8) & 0x00FF;
	   readRegs[1] = (unsigned char)(readAddr >> 0) & 0x00FF;;
	   length = 2;;

	   mdelay(1);
	   readValue = _ISX012_i2c_read(pClient, readRegs, length);
	   if (readValue < 0) {
	       printk(KERN_ERR "%s error iRet=%d\n", __func__, readAddr);
	       return readAddr;
	   }

	   ucValue = (u8)(readValue & 0xFF);

	   printk(KERN_ALERT "%s REG=0x%04x INTSTS=0x%02x\n", __func__, readAddr, readValue);

		ucValue = ((readValue & (0x01 << shift)) >> shift);
	   printk(KERN_ALERT "%s REG=0x%04x ucValue=0x%02x\n", __func__, readAddr, ucValue);
	   iRoofOutCnt--;
	} while ((ucValue != 0) && iRoofOutCnt);

	printk(KERN_ALERT "%s REG=0x%04x iRoofOutCnt=%d\n", __func__, readAddr, iRoofOutCnt);


	printk(KERN_ERR "%s(): END ISX012_waitForModeTransition\n", __func__);
	return iRet;
}


int ISX012_table_write(struct i2c_client *pClient,
				const void *pvArg, int iResType)
{
	int iRet = 0;
	u32 uiCnt = 0;
	u8 rgucWriteRegs[4] = { 0, };
	_isx012_regset_t *pstRegLists = 0;

	if (iResType == 0) {
		pstRegLists = (_isx012_regset_t *) pvArg;
		{
			if (pstRegLists[uiCnt].ucLen == 0x02) {
				rgucWriteRegs[0] =
					(u8) ((pstRegLists[uiCnt].usRegs >> 8)
						& 0xFF);
				rgucWriteRegs[1] =
					(u8) (pstRegLists[uiCnt].usRegs & 0xFF);
				rgucWriteRegs[2] =
					(u8) (pstRegLists[uiCnt].usData & 0xFF);
				rgucWriteRegs[3] =
					(u8) (pstRegLists[uiCnt].usData >> 8
						& 0xFF);
			} else if (pstRegLists[uiCnt].ucLen == 0x01
				|| pstRegLists[uiCnt].ucLen == 0x03) {
				rgucWriteRegs[0] =
					(u8) ((pstRegLists[uiCnt].usRegs >> 8)
						& 0xFF);
				rgucWriteRegs[1] =
					(u8) (pstRegLists[uiCnt].usRegs & 0xFF);
				rgucWriteRegs[2] =
					(u8) (pstRegLists[uiCnt].usData & 0xFF);
				rgucWriteRegs[3] = 0x00;
			} else if (pstRegLists[uiCnt].ucLen == 0xFF) {
				rgucWriteRegs[0] =
					(u8) ((pstRegLists[uiCnt].usRegs >> 8)
						& 0xFF);
				rgucWriteRegs[1] =
					(u8) (pstRegLists[uiCnt].usRegs & 0xFF);
				rgucWriteRegs[2] =
					(u8) (pstRegLists[uiCnt].usData & 0xFF);
				rgucWriteRegs[3] =
					(u8) (pstRegLists[uiCnt].usData >> 8
						& 0xFF);
			} else {
				printk(KERN_ALERT "%s :Unexpected value!!",
					__func__);
				return iRet;
			}

			while (rgucWriteRegs[0] != 0xFF
				|| rgucWriteRegs[1] != 0xFF
				|| rgucWriteRegs[2] != 0xFF) {
				if (pstRegLists[uiCnt].ucLen != 0x03) {
					iRet = _ISX012_i2c_send(pClient,
						rgucWriteRegs,
						2 + pstRegLists[uiCnt].ucLen);

#if 0
					if (pstRegLists[uiCnt].ucLen == 0x02)
						printk(KERN_ERR "_ISX012_i2c_send SADDR=0x%02X LENGTH=%d ADDR=%04X DATA=%04X\n", ISX012_SLAVE_ADDR, (2 + pstRegLists[uiCnt].ucLen), pstRegLists[uiCnt].usRegs, pstRegLists[uiCnt].usData);
					else if (pstRegLists[uiCnt].ucLen == 0x01)
						printk(KERN_ERR "_ISX012_i2c_send SADDR=0x%02X LENGTH=%d ADDR=%04X DATA=%02X\n", ISX012_SLAVE_ADDR, (2 + pstRegLists[uiCnt].ucLen), pstRegLists[uiCnt].usRegs, pstRegLists[uiCnt].usData);
#endif
					if (iRet < 0) {
						printk(
						KERN_ALERT \
						"%s :write failed=break!",
						__func__);
						iRet = -1;
						return iRet;
					}

				} else { /* 0x03 is delay */
					mdelay(rgucWriteRegs[2]);
					printk(
					KERN_ALERT "%s :setfile delay :  %d",
					__func__,
					rgucWriteRegs[2]);
				}

				uiCnt++;

				if (pstRegLists[uiCnt].ucLen == 0x02) {
					rgucWriteRegs[0] =
						(u8) ((pstRegLists[uiCnt].usRegs
							>> 8) & 0xFF);
					rgucWriteRegs[1] =
						(u8) (pstRegLists[uiCnt].usRegs
							& 0xFF);

					rgucWriteRegs[2] =
						(u8) (pstRegLists[uiCnt].usData
							& 0xFF);
					rgucWriteRegs[3] =
						(u8) (pstRegLists[uiCnt].usData
							>> 8 & 0xFF);
				} else if (pstRegLists[uiCnt].ucLen == 0x01
					|| pstRegLists[uiCnt].ucLen == 0x03) {
					rgucWriteRegs[0] =
						(u8) ((pstRegLists[uiCnt].usRegs
							>> 8) & 0xFF);
					rgucWriteRegs[1] =
						(u8) (pstRegLists[uiCnt].usRegs
							& 0xFF);
					rgucWriteRegs[2] =
						(u8) (pstRegLists[uiCnt].usData
							& 0xFF);
					rgucWriteRegs[3] = 0x00;
				} else if (pstRegLists[uiCnt].ucLen == 0xFF) {
					rgucWriteRegs[0] =
						(u8) ((pstRegLists[uiCnt].usRegs
							>> 8) & 0xFF);
					rgucWriteRegs[1] =
						(u8) (pstRegLists[uiCnt].usRegs
							& 0xFF);
					rgucWriteRegs[2] =
						(u8) (pstRegLists[uiCnt].usData
							& 0xFF);
					rgucWriteRegs[3] =
						(u8) (pstRegLists[uiCnt].usData
							>> 8 & 0xFF);
				} else {
					printk(
					KERN_ALERT "%s :Unexpected value!!",
					__func__);
					return iRet;
				}
			}

		}
	}

	return iRet;
}
/* ISX012-0
     MIPI 2LANE      648
     PLL   648MHz
     DCK       81
     inifile
     size address data   */

static const _isx012_regset_t ISX012_Pll_Setting_2[] = {
{0x0007, 0x01, 0x01},                   /*  PLL_CKSEL                 : PLL 648MHz */
{0x0008, 0x03, 0x01},                   /*  SRCCK_DIV                 : 1/8 frequency */

{0x0004, 0x03, 0x01},                  /* I2C_ADR_SEL 2: 0x3C MIPI selected,  3: 0x3D MIPI selected */
{0x5008, 0x00, 0x01},                  /* ENDIAN_SEL : 0:Little Endian */
{0x6DA8, 0x01, 0x01},                  /* SHD_CoEF (OTP shading ON flag) */
{0x6DA9, 0x09, 0x01},                  /*  WHITE_CTRL */
{0x6DCB, 0x22, 0x01},                  /*  YGAM_CONFIG2 : */

{0x00C4, 0x11, 0x01},                   /*  VIF_CLKCONFIG1            : VIFSEL and VIFDIV setting value with full frame pixel setting for other then JPG */
{0x00C5, 0x11, 0x01},                   /*  VIF_CLKCONFIG2            : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for other then JPG */
{0x00C6, 0x11, 0x01},                   /*  VIF_CLKCONFIG3            : VIFSEL and VIFDIV setting value with 1/4 sub-sampling setting for other then JPG */
{0x00C7, 0x11, 0x01},                   /*  VIF_CLKCONFIG4            : VIFSEL and VIFDIV setting value with 1/8 sub-sampling setting for other then JPG */
{0x00C8, 0x11, 0x01},                   /*  VIF_CLKCONFIG5            : VIFSEL and VIFDIV setting value with full frame pixel setting for JPG mode */
{0x00C9, 0x11, 0x01},                   /*  VIF_CLKCONFIG6            : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for JPG mode */
{0x00CA, 0x11, 0x01},                   /*  VIF_CLKCONFIG7            : VIFSEL and VIFDIV setting value with 1/4 sub-sampling setting for JPG mode */
{0x018C, 0x0000, 0x02},                /*  VADJ_SENS_1_1             : VMAX adjustment value for full frame pixel */
{0x018E, 0x0000, 0x02},                /*  VADJ_SENS_1_2             : VMAX adjustment value for 1/2 sub-sampling */
{0x0190, 0x0000, 0x02},                /*  VADJ_SENS_1_4             : VMAX adjustment value for 1/4 sub-sampling */
{0x0192, 0x0000, 0x02},                /*  VADJ_SENS_1_8             : VMAX adjustment value for 1/8 sub-sampling */
{0x0194, 0x0000, 0x02},                /*  VADJ_SENS_HD_1_1          : VMAX adjustment value for HD full frame pixel */
{0x0196, 0x0000, 0x02},                /*  VADJ_SENS_HD_1_2          : VMAX adjustment value for HD 1/2 sub-sampling */
{0x6A16, 0x0400, 0x02},                /*  FLC_OPD_HEIGHT_NORMAL_1_1 : Detection window vertical size with all 32 windows for FLC full frame pixel */
{0x6A18, 0x03C0, 0x02},                /*  FLC_OPD_HEIGHT_NORMAL_1_2 : Detection window vertical size with all 32 windows for FLC 1/2 sub-sampling */
{0x6A1A, 0x01E0, 0x02},                /*  FLC_OPD_HEIGHT_NORMAL_1_4 : Detection window vertical size with all 32 windows for FLC 1/4 sub-sampling */
{0x6A1C, 0x00E0, 0x02},                /*  FLC_OPD_HEIGHT_NORMAL_1_8 : Detection window vertical size with all 32 windows for FLC 1/8 sub-sampling */
{0x6A1E, 0x0400, 0x02},                /*  FLC_OPD_HEIGHT_HD_1_1     : Detection window vertical size with all 32 windows for FLC HD full frame pixel */
{0x6A20, 0x02C0, 0x02},                /*  FLC_OPD_HEIGHT_HD_1_2     : Detection window vertical size with all 32 windows for FLC HD 1/2 sub-sampling */
{0x0016, 0x0010, 0x02},                /*  GPIO_FUNCSEL              : GPIO setting */
{0x5C01, 0x00, 0x01},                   /*  RGLANESEL                 : Select 1Lane or 2Lane */

{0x5C04, 0x06, 0x01},                   /*  RGTLPX                    :  0x5C04   0x4  ->  0x6 */
{0x5C05, 0x05, 0x01},                   /*  RGTCLKPREPARE             :  0x5C05   0x3  ->  0x5 */
{0x5C06, 0x14, 0x01},                   /*  RGTCLKZERO                : */
{0x5C07, 0x02, 0x01},                   /*  RGTCLKPRE                 : */
{0x5C08, 0x0D, 0x01},                   /*  RGTCLKPOST                :  0x5C08   0x11 ->  0xD */
{0x5C09, 0x07, 0x01},                   /*  RGTCLKTRAIL               :  0x5C09   0x5  ->  0x7 */
{0x5C0A, 0x0A, 0x01},                   /*  RGTHSEXIT                 :  0x5C0A   0x7  ->  0xA */
{0x5C0B, 0x05, 0x01},                   /*  RGTHSPREPARE              :  0x5C0B   0x3  ->  0x5 */
{0x5C0C, 0x08, 0x01},                   /*  RGTHSZERO                 :  0x5C0C   0x7  ->  0x8 */
{0x5C0D, 0x07, 0x01},                   /*  RGTHSTRAIL                :  0x5C0D   0x5  ->  0x7 */

{0x0009, 0x01, 0x01},                 /*  EXT_PLL_CKSEL       : PLL 648MHz */
{0x00D0, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT1  : VIFSEL and VIFDIV setting value with full frame pixel setting for JPG and interleave mode */
{0x00D1, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT2  : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for JPG and interleave mode */
{0x00D4, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT5  : VIFSEL and VIFDIV setting value with full frame pixel setting for JPG mode */
{0x00D5, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT6  : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for JPG mode */
{0x00D8, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT9  : VIFSEL and VIFDIV setting value with full frame pixel setting for other than JPG */
{0x00D9, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT10 : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for other than JPG */


/* init Preview setting */
{0x0089, 0x00, 0x01},/* OUTFMT_MONI */
{0x0090, 0x0280, 0x02},/* HSIZE_MONI : 640 */
{0x0096, 0x01E0, 0x02},/* VSIZE_MONI : 480 */
{0x0083, 0x01, 0x01},/* SENSMODE_MONI */
{0x0086, 0x02, 0x01},/* FPSTYPE_MONI */
{0x0081, 0x00, 0x01},/* MODESEL */
{0x0082, 0x01, 0x01},/* MONI_REFRESH */

/* jpeg setting
    Apex40 is not Jpeg Capture */

/* Fast mode setting */
{0x500A, 0x00, 0x01},    /*  FAST_MODECHG_EN */
{0x500B, 0x01, 0x01},    /*  FAST_SHT_MODE_SEL */
{0x500C, 0x00FA, 0x02},    /*  FAST_SHT_LIMIT_COUNT */

/* Select sensor inversion link control */
{0x501A, 0x00, 0x01},    /* SENS_REVERSE_CTRL */

/* shading */
{0x6DBC, 0x03, 0x01},    /*  WHITE_EDGE_MAX : */
{0x6DF6, 0xFF, 0x01},    /*  WHITE_SHD_JUDGE_BODY_COLOR_RATIO : */
{0x6DF7, 0xF0, 0x01},    /*  WHITE_SHD_JUDGE_RED_RATIO : */
{0x6DAD, 0x0C, 0x01},    /*  WHITE_OFSET1_UP : */
{0x6DAE, 0x0C, 0x01},    /*  WHITE_OFSET1_DOWN : */
{0x6DAF, 0x11, 0x01},    /*  WHITE_OFSET1_RIGHT : */
{0x6DB0, 0x1B, 0x01},    /*  WHITE_OFSET1_LEFT : */
{0x6DB1, 0x0D, 0x01},    /*  WHITE_OFSET2_UP : */
{0x6DB2, 0x13, 0x01},    /*  WHITE_OFSET2_DOWN : */
{0x6DB3, 0x11, 0x01},    /*  WHITE_OFSET2_RIGHT : */
{0x6DB4, 0x17, 0x01},    /*  WHITE_OFSET2_LEFT : */

/* addtional code */
{0xF200, 0xB9B9, 0x02},
{0xF202, 0x4E12, 0x02},
{0xF204, 0x6055, 0x02},
{0xF206, 0x008B, 0x02},
{0xF208, 0xF177, 0x02},
{0xF20A, 0xFA70, 0x02},
{0xF20C, 0x0000, 0x02},
{0xF20E, 0x0000, 0x02},
{0xF210, 0x0000, 0x02},
{0xF212, 0x0000, 0x02},
{0xF214, 0x0000, 0x02},
{0xF216, 0x0000, 0x02},
{0xF218, 0x0000, 0x02},
{0xF21A, 0x0000, 0x02},
{0xF21C, 0x0000, 0x02},
{0xF21E, 0x0000, 0x02},
{0xF220, 0x0000, 0x02},
{0xF222, 0x0000, 0x02},
{0xF224, 0x0000, 0x02},
{0xF226, 0x0000, 0x02},
{0xF228, 0x0000, 0x02},
{0xF22A, 0x0000, 0x02},
{0xF22C, 0x0000, 0x02},
{0xF22E, 0x0000, 0x02},
{0xF230, 0x0000, 0x02},
{0xF232, 0x0000, 0x02},
{0xF234, 0x0000, 0x02},
{0xF236, 0x0000, 0x02},
{0xF238, 0x0000, 0x02},
{0xF23A, 0x0000, 0x02},
{0xF23C, 0x0000, 0x02},
{0xF23E, 0x0000, 0x02},
{0xF240, 0x0000, 0x02},
{0xF242, 0x0000, 0x02},
{0xF244, 0xB47E, 0x02},
{0xF246, 0x4808, 0x02},
{0xF248, 0x7800, 0x02},
{0xF24A, 0x07C0, 0x02},
{0xF24C, 0x0FC0, 0x02},
{0xF24E, 0xF687, 0x02},
{0xF250, 0xF8ED, 0x02},
{0xF252, 0xF68E, 0x02},
{0xF254, 0xFE2B, 0x02},
{0xF256, 0xF688, 0x02},
{0xF258, 0xFF6B, 0x02},
{0xF25A, 0xF693, 0x02},
{0xF25C, 0xFB6B, 0x02},
{0xF25E, 0xF687, 0x02},
{0xF260, 0xF947, 0x02},
{0xF262, 0xBC7E, 0x02},
{0xF264, 0xF688, 0x02},
{0xF266, 0xFD8F, 0x02},
{0xF268, 0x239C, 0x02},
{0xF26A, 0x0018, 0x02},
{0x0006, 0x16, 0x01},                  /* INCK_SET : 24MHz */

{0xFFFF, 0xFF, 0x01}
};

/*  ISX012-0
      MIPI 2LANE 432/LANE
      PLL   432MHz
      DCK       54
      inifile
      size address data */

static const _isx012_regset_t ISX012_Pll_Setting_3[] = {
{0x0007, 0x00, 0x01},                  /*  PLL_CKSEL                 : PLL 432MHz */
{0x0008, 0x00, 0x01},                  /*  SRCCK_DIV                 : 1/5 frequency */

{0x0004, 0x03, 0x01},                  /* I2C_ADR_SEL 2: 0x3C MIPI selected,  3: 0x3D MIPI selected */
{0x5008, 0x00, 0x01},                  /* ENDIAN_SEL : 0:Little Endian */
{0x6DA8, 0x01, 0x01},                  /* SHD_CoEF (OTP shading ON flag) */
{0x6DA9, 0x09, 0x01},                  /*  WHITE_CTRL */
{0x6DCB, 0x22, 0x01},                   /*  YGAM_CONFIG2 : */

{0x00C4, 0x11, 0x01},                  /*  VIF_CLKCONFIG1            : VIFSEL and VIFDIV setting value with full frame pixel setting for other then JPG */
{0x00C5, 0x11, 0x01},                  /*  VIF_CLKCONFIG2            : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for other then JPG */
{0x00C6, 0x11, 0x01},                  /*  VIF_CLKCONFIG3            : VIFSEL and VIFDIV setting value with 1/4 sub-sampling setting for other then JPG */
{0x00C7, 0x11, 0x01},                  /*  VIF_CLKCONFIG4            : VIFSEL and VIFDIV setting value with 1/8 sub-sampling setting for other then JPG */
{0x00C8, 0x11, 0x01},                  /*  VIF_CLKCONFIG5            : VIFSEL and VIFDIV setting value with full frame pixel setting for JPG mode */
{0x00C9, 0x11, 0x01},                  /*  VIF_CLKCONFIG6            : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for JPG mode */
{0x00CA, 0x11, 0x01},                  /*  VIF_CLKCONFIG7            : VIFSEL and VIFDIV setting value with 1/4 sub-sampling setting for JPG mode */
{0x00CC, 0x11, 0x01},                  /*  VIF_CLKCONFIG9            : VIFSEL and VIFDIV setting value with full frame pixel setting for JPG and interleave mode */
{0x00CD, 0x11, 0x01},                  /*  VIF_CLKCONFIG10           : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for JPG and interleave mode */
{0x6A12, 0x11, 0x01},                  /*  VIF_CLKCONFIG13 for RAW8  : VIFSEL and VIFDIV setting value with full frame pixel setting for RAW mode */
{0x6A13, 0x11, 0x01},                  /*  VIF_CLKCONFIG14 for RAW8  : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for RAW mode */
{0x6A14, 0x11, 0x01},                  /*  VIF_CLKCONFIG15 for RAW8  : VIFSEL and VIFDIV setting value with 1/4 sub-sampling setting for RAW mode */
{0x6A15, 0x11, 0x01},                  /*  VIF_CLKCONFIG16 for RAW8  : VIFSEL and VIFDIV setting value with 1/8 sub-sampling setting for RAW mode */
{0x018C, 0x0000, 0x02},                /*  VADJ_SENS_1_1             : VMAX adjustment value for full frame pixel */
{0x018E, 0x0012, 0x02},                /*  VADJ_SENS_1_2             : VMAX adjustment value for 1/2 sub-sampling */
{0x0190, 0x0000, 0x02},               /*  VADJ_SENS_1_4             : VMAX adjustment value for 1/4 sub-sampling */
{0x0192, 0x0000, 0x02},               /*  VADJ_SENS_1_8             : VMAX adjustment value for 1/8 sub-sampling */
{0x0194, 0x0027, 0x02},               /*  VADJ_SENS_HD_1_1          : VMAX adjustment value for HD full frame pixel */
{0x0196, 0x0015, 0x02},               /*  VADJ_SENS_HD_1_2          : VMAX adjustment value for HD 1/2 sub-sampling */
{0x6A16, 0x0440, 0x02},               /*  FLC_OPD_HEIGHT_NORMAL_1_1 : Detection window vertical size with all 32 windows for FLC full frame pixel */
{0x6A18, 0x03C0, 0x02},               /*  FLC_OPD_HEIGHT_NORMAL_1_2 : Detection window vertical size with all 32 windows for FLC 1/2 sub-sampling */
{0x6A1A, 0x01E0, 0x02},               /*  FLC_OPD_HEIGHT_NORMAL_1_4 : Detection window vertical size with all 32 windows for FLC 1/4 sub-sampling */
{0x6A1C, 0x00E0, 0x02},               /*  FLC_OPD_HEIGHT_NORMAL_1_8 : Detection window vertical size with all 32 windows for FLC 1/8 sub-sampling */
{0x6A1E, 0x0420, 0x02},               /*  FLC_OPD_HEIGHT_HD_1_1     : Detection window vertical size with all 32 windows for FLC HD full frame pixel */
{0x6A20, 0x02C0, 0x02},               /*  FLC_OPD_HEIGHT_HD_1_2     : Detection window vertical size with all 32 windows for FLC HD 1/2 sub-sampling */
{0x0016, 0x0010, 0x02},               /*  GPIO_FUNCSEL              : GPIO setting */
{0x5C01, 0x00, 0x01},                  /*  RGLANESEL                 : */
{0x5C04, 0x04, 0x01},                  /*  RGTLPX                    : */
{0x5C05, 0x03, 0x01},                  /*  RGTCLKPREPARE             : */
{0x5C06, 0x0E, 0x01},                  /*  RGTCLKZERO                : */
{0x5C07, 0x02, 0x01},                  /*  RGTCLKPRE                 : */
{0x5C08, 0x0B, 0x01},                  /*  RGTCLKPOST                : */
{0x5C09, 0x05, 0x01},                  /*  RGTCLKTRAIL               : */
{0x5C0A, 0x07, 0x01},                  /*  RGTHSEXIT                 : */
{0x5C0B, 0x03, 0x01},                  /*  RGTHSPREPARE              : */
{0x5C0C, 0x07, 0x01},                  /*  RGTHSZERO                 : */
{0x5C0D, 0x05, 0x01},                  /*  RGTHSTRAIL                : */

{0x0009, 0x01, 0x01},		/*  */
{0x000A, 0x03, 0x01},		/*  EXT_SRCCK_DIV	: 1/8 frequency */
{0x00D8, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT9  : VIFSEL and VIFDIV setting value with full frame pixel setting for other than JPG */
{0x00D9, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT10 : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for other than JPG */
{0x00DA, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT11 : VIFSEL and VIFDIV setting value with 1/4 sub-sampling setting for other than JPG */
{0x00DB, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT12 : VIFSEL and VIFDIV setting value with 1/8 sub-sampling setting for other than JPG */
{0x00AC, 0x02, 0x01},                 /*  */

/* init Preview setting */
{0x0089, 0x00, 0x01},/* OUTFMT_MONI */
{0x0090, 0x0280, 0x02},/* HSIZE_MONI : 640 */
{0x0096, 0x01E0, 0x02},/* VSIZE_MONI : 480 */
{0x0083, 0x01, 0x01},/* SENSMODE_MONI */
{0x0086, 0x02, 0x01},/* FPSTYPE_MONI */
{0x0081, 0x00, 0x01},/* MODESEL */
{0x0082, 0x01, 0x01},/* MONI_REFRESH */

/* jpeg setting */
/* Apex40 is not Jpeg Capture */

/* Fast mode setting */
{0x500A, 0x00, 0x01},    /*  FAST_MODECHG_EN */
{0x500B, 0x01, 0x01},    /*  FAST_SHT_MODE_SEL */
{0x500C, 0x00FA, 0x02},    /*  FAST_SHT_LIMIT_COUNT */

/* Select sensor inversion link control */
{0x501A, 0x00, 0x01},    /* SENS_REVERSE_CTRL */

/* shading */
{0x6DBC, 0x03, 0x01},    /*  WHITE_EDGE_MAX : */
{0x6DF6, 0xFF, 0x01},    /*  WHITE_SHD_JUDGE_BODY_COLOR_RATIO : */
{0x6DF7, 0xF0, 0x01},    /*  WHITE_SHD_JUDGE_RED_RATIO : */
{0x6DAD, 0x0C, 0x01},    /*  WHITE_OFSET1_UP : */
{0x6DAE, 0x0C, 0x01},    /*  WHITE_OFSET1_DOWN : */
{0x6DAF, 0x11, 0x01},    /*  WHITE_OFSET1_RIGHT : */
{0x6DB0, 0x1B, 0x01},    /*  WHITE_OFSET1_LEFT : */
{0x6DB1, 0x0D, 0x01},    /*  WHITE_OFSET2_UP : */
{0x6DB2, 0x13, 0x01},    /*  WHITE_OFSET2_DOWN : */
{0x6DB3, 0x11, 0x01},    /*  WHITE_OFSET2_RIGHT : */
{0x6DB4, 0x17, 0x01},    /*  WHITE_OFSET2_LEFT : */

/* additional code */
{0xF200, 0xB9B9, 0x02},
{0xF202, 0x4E12, 0x02},
{0xF204, 0x6055, 0x02},
{0xF206, 0x008B, 0x02},
{0xF208, 0xF177, 0x02},
{0xF20A, 0xFA70, 0x02},
{0xF20C, 0x0000, 0x02},
{0xF20E, 0x0000, 0x02},
{0xF210, 0x0000, 0x02},
{0xF212, 0x0000, 0x02},
{0xF214, 0x0000, 0x02},
{0xF216, 0x0000, 0x02},
{0xF218, 0x0000, 0x02},
{0xF21A, 0x0000, 0x02},
{0xF21C, 0x0000, 0x02},
{0xF21E, 0x0000, 0x02},
{0xF220, 0x0000, 0x02},
{0xF222, 0x0000, 0x02},
{0xF224, 0x0000, 0x02},
{0xF226, 0x0000, 0x02},
{0xF228, 0x0000, 0x02},
{0xF22A, 0x0000, 0x02},
{0xF22C, 0x0000, 0x02},
{0xF22E, 0x0000, 0x02},
{0xF230, 0x0000, 0x02},
{0xF232, 0x0000, 0x02},
{0xF234, 0x0000, 0x02},
{0xF236, 0x0000, 0x02},
{0xF238, 0x0000, 0x02},
{0xF23A, 0x0000, 0x02},
{0xF23C, 0x0000, 0x02},
{0xF23E, 0x0000, 0x02},
{0xF240, 0x0000, 0x02},
{0xF242, 0x0000, 0x02},
{0xF244, 0xB47E, 0x02},
{0xF246, 0x4808, 0x02},
{0xF248, 0x7800, 0x02},
{0xF24A, 0x07C0, 0x02},
{0xF24C, 0x0FC0, 0x02},
{0xF24E, 0xF687, 0x02},
{0xF250, 0xF8ED, 0x02},
{0xF252, 0xF68E, 0x02},
{0xF254, 0xFE2B, 0x02},
{0xF256, 0xF688, 0x02},
{0xF258, 0xFF6B, 0x02},
{0xF25A, 0xF693, 0x02},
{0xF25C, 0xFB6B, 0x02},
{0xF25E, 0xF687, 0x02},
{0xF260, 0xF947, 0x02},
{0xF262, 0xBC7E, 0x02},
{0xF264, 0xF688, 0x02},
{0xF266, 0xFD8F, 0x02},
{0xF268, 0x239C, 0x02},
{0xF26A, 0x0018, 0x02},
{0x0006, 0x16, 0x01},                  /* INCK_SET : 24MHz */

{0xFFFF, 0xFF, 0xFF}
};

/*  ISX012-0
     MIPI 2LANE 432/LANE
     PLL   432MHz
     DCK       54
     inifile
     size address data */

static const _isx012_regset_t ISX012_Pll_Setting_4[] = {
{0x0007, 0x00, 0x01},                  /*  PLL_CKSEL                 : PLL 432MHz */
{0x0008, 0x00, 0x01},                  /*  SRCCK_DIV                 : 1/5 frequency */

{0x0004, 0x03, 0x01},                  /* I2C_ADR_SEL 2: 0x3C MIPI selected, 3: 0x3D MIPI selected */
{0x5008, 0x00, 0x01},                  /* ENDIAN_SEL : 0:Little Endian */
{0x6DA8, 0x01, 0x01},                  /* SHD_CoEF (OTP shading ON flag) */
{0x6DA9, 0x09, 0x01},                  /*  WHITE_CTRL */
{0x6DCB, 0x22, 0x01},    /*  YGAM_CONFIG2 : */

{0x00C4, 0x11, 0x01},                  /*  VIF_CLKCONFIG1            : VIFSEL and VIFDIV setting value with full frame pixel setting for other then JPG */
{0x00C5, 0x11, 0x01},                  /*  VIF_CLKCONFIG2            : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for other then JPG */
{0x00C6, 0x11, 0x01},                  /*  VIF_CLKCONFIG3            : VIFSEL and VIFDIV setting value with 1/4 sub-sampling setting for other then JPG */
{0x00C7, 0x11, 0x01},                  /*  VIF_CLKCONFIG4            : VIFSEL and VIFDIV setting value with 1/8 sub-sampling setting for other then JPG */
{0x00C8, 0x11, 0x01},                  /*  VIF_CLKCONFIG5            : VIFSEL and VIFDIV setting value with full frame pixel setting for JPG mode */
{0x00C9, 0x11, 0x01},                  /*  VIF_CLKCONFIG6            : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for JPG mode */
{0x00CA, 0x11, 0x01},                  /*  VIF_CLKCONFIG7            : VIFSEL and VIFDIV setting value with 1/4 sub-sampling setting for JPG mode */
{0x00CC, 0x11, 0x01},                  /*  VIF_CLKCONFIG9            : VIFSEL and VIFDIV setting value with full frame pixel setting for JPG and interleave mode */
{0x00CD, 0x11, 0x01},                  /*  VIF_CLKCONFIG10           : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for JPG and interleave mode */
{0x6A12, 0x11, 0x01},                  /*  VIF_CLKCONFIG13 for RAW8  : VIFSEL and VIFDIV setting value with full frame pixel setting for RAW mode */
{0x6A13, 0x11, 0x01},                  /*  VIF_CLKCONFIG14 for RAW8  : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for RAW mode */
{0x6A14, 0x11, 0x01},                  /*  VIF_CLKCONFIG15 for RAW8  : VIFSEL and VIFDIV setting value with 1/4 sub-sampling setting for RAW mode */
{0x6A15, 0x11, 0x01},                  /*  VIF_CLKCONFIG16 for RAW8  : VIFSEL and VIFDIV setting value with 1/8 sub-sampling setting for RAW mode */
{0x018C, 0x0026, 0x02},                /*  VADJ_SENS_1_1             : VMAX adjustment value for full frame pixel */
{0x018E, 0x0012, 0x02},                /*  VADJ_SENS_1_2             : VMAX adjustment value for 1/2 sub-sampling */
{0x0190, 0x0000, 0x02},               /*  VADJ_SENS_1_4             : VMAX adjustment value for 1/4 sub-sampling */
{0x0192, 0x0000, 0x02},               /*  VADJ_SENS_1_8             : VMAX adjustment value for 1/8 sub-sampling */
{0x0194, 0x0027, 0x02},               /*  VADJ_SENS_HD_1_1          : VMAX adjustment value for HD full frame pixel */
{0x0196, 0x0015, 0x02},               /*  VADJ_SENS_HD_1_2          : VMAX adjustment value for HD 1/2 sub-sampling */
{0x6A16, 0x0440, 0x02},               /*  FLC_OPD_HEIGHT_NORMAL_1_1 : Detection window vertical size with all 32 windows for FLC full frame pixel */
{0x6A18, 0x03C0, 0x02},               /*  FLC_OPD_HEIGHT_NORMAL_1_2 : Detection window vertical size with all 32 windows for FLC 1/2 sub-sampling */
{0x6A1A, 0x01E0, 0x02},               /*  FLC_OPD_HEIGHT_NORMAL_1_4 : Detection window vertical size with all 32 windows for FLC 1/4 sub-sampling */
{0x6A1C, 0x00E0, 0x02},               /*  FLC_OPD_HEIGHT_NORMAL_1_8 : Detection window vertical size with all 32 windows for FLC 1/8 sub-sampling */
{0x6A1E, 0x0420, 0x02},               /*  FLC_OPD_HEIGHT_HD_1_1     : Detection window vertical size with all 32 windows for FLC HD full frame pixel */
{0x6A20, 0x02C0, 0x02},               /*  FLC_OPD_HEIGHT_HD_1_2     : Detection window vertical size with all 32 windows for FLC HD 1/2 sub-sampling */
{0x0016, 0x0010, 0x02},               /*  GPIO_FUNCSEL              : GPIO setting */
{0x5C01, 0x00, 0x01},                  /*  RGLANESEL                 : */
{0x5C04, 0x04, 0x01},                  /*  RGTLPX                    : */
{0x5C05, 0x03, 0x01},                  /*  RGTCLKPREPARE             : */
{0x5C06, 0x0E, 0x01},                  /*  RGTCLKZERO                : */
{0x5C07, 0x02, 0x01},                  /*  RGTCLKPRE                 : */
{0x5C08, 0x0B, 0x01},                  /*  RGTCLKPOST                : */
{0x5C09, 0x05, 0x01},                  /*  RGTCLKTRAIL               : */
{0x5C0A, 0x07, 0x01},                  /*  RGTHSEXIT                 : */
{0x5C0B, 0x03, 0x01},                  /*  RGTHSPREPARE              : */
{0x5C0C, 0x07, 0x01},                  /*  RGTHSZERO                 : */
{0x5C0D, 0x05, 0x01},                  /*  RGTHSTRAIL                : */

{0x6A9E, 0x15C0, 0x02},                /* HMAX_1_1(0x6A9E)=0x15C0 */

{0x0009, 0x01, 0x01},		/*  */
{0x000A, 0x03, 0x01},		/*  EXT_SRCCK_DIV	: 1/8 frequency */
{0x00D8, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT9  : VIFSEL and VIFDIV setting value with full frame pixel setting for other than JPG */
{0x00D9, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT10 : VIFSEL and VIFDIV setting value with 1/2 sub-sampling setting for other than JPG */
{0x00DA, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT11 : VIFSEL and VIFDIV setting value with 1/4 sub-sampling setting for other than JPG */
{0x00DB, 0x11, 0x01},                 /*  VIF_CLKCONFIG_EXT12 : VIFSEL and VIFDIV setting value with 1/8 sub-sampling setting for other than JPG */
{0x00AC, 0x00, 0x01},                 /*  */

/* init Preview setting */
{0x0089, 0x00, 0x01},                  /* OUTFMT_MONI */
{0x0090, 0x0280, 0x02},                  /* HSIZE_MONI : 640 */
{0x0096, 0x01E0, 0x02},                  /* VSIZE_MONI : 480 */
{0x0083, 0x01, 0x01},                  /* SENSMODE_MONI */
{0x0086, 0x02, 0x01},                  /* FPSTYPE_MONI */
{0x0081, 0x00, 0x01},                  /* MODESEL */
{0x0082, 0x01, 0x01},                  /* MONI_REFRESH */

/* jpeg setting */
/* Apex40 is not Jpeg Capture */

/* Fast mode setting */
{0x500A, 0x00, 0x01},    /*  FAST_MODECHG_EN */
{0x500B, 0x01, 0x01},    /*  FAST_SHT_MODE_SEL */
{0x500C, 0x00FA, 0x02},    /*  FAST_SHT_LIMIT_COUNT */

/* Select sensor inversion link control */
{0x501A, 0x00, 0x01},    /* SENS_REVERSE_CTRL */

/* shading */
{0x6DBC, 0x03, 0x01},    /*  WHITE_EDGE_MAX : */
{0x6DF6, 0xFF, 0x01},    /*  WHITE_SHD_JUDGE_BODY_COLOR_RATIO : */
{0x6DF7, 0xF0, 0x01},    /*  WHITE_SHD_JUDGE_RED_RATIO : */
{0x6DAD, 0x0C, 0x01},    /*  WHITE_OFSET1_UP : */
{0x6DAE, 0x0C, 0x01},    /*  WHITE_OFSET1_DOWN : */
{0x6DAF, 0x11, 0x01},    /*  WHITE_OFSET1_RIGHT : */
{0x6DB0, 0x1B, 0x01},    /*  WHITE_OFSET1_LEFT : */
{0x6DB1, 0x0D, 0x01},    /*  WHITE_OFSET2_UP : */
{0x6DB2, 0x13, 0x01},    /*  WHITE_OFSET2_DOWN : */
{0x6DB3, 0x11, 0x01},    /*  WHITE_OFSET2_RIGHT : */
{0x6DB4, 0x17, 0x01},    /*  WHITE_OFSET2_LEFT : */

/* additional code */
{0xF200,  0xB9B9,  0x02},
{0xF202,  0x4E12,  0x02},
{0xF204,  0x6055,  0x02},
{0xF206,  0x008B,  0x02},
{0xF208,  0xF177,  0x02},
{0xF20A,  0xFA70,  0x02},
{0xF20C,  0x0000,  0x02},
{0xF20E,  0x0000,  0x02},
{0xF210,  0x0000,  0x02},
{0xF212,  0x0000,  0x02},
{0xF214,  0x0000,  0x02},
{0xF216,  0x0000,  0x02},
{0xF218,  0x0000,  0x02},
{0xF21A,  0x0000,  0x02},
{0xF21C,  0x0000,  0x02},
{0xF21E,  0x0000,  0x02},
{0xF220,  0x0000,  0x02},
{0xF222,  0x0000,  0x02},
{0xF224,  0x0000,  0x02},
{0xF226,  0x0000,  0x02},
{0xF228,  0x0000,  0x02},
{0xF22A,  0x0000,  0x02},
{0xF22C,  0x0000,  0x02},
{0xF22E,  0x0000,  0x02},
{0xF230,  0x0000,  0x02},
{0xF232,  0x0000,  0x02},
{0xF234,  0x0000,  0x02},
{0xF236,  0x0000,  0x02},
{0xF238,  0x0000,  0x02},
{0xF23A,  0x0000,  0x02},
{0xF23C,  0x0000,  0x02},
{0xF23E,  0x0000,  0x02},
{0xF240,  0x0000,  0x02},
{0xF242,  0x0000,  0x02},
{0xF244,  0xB47E,  0x02},
{0xF246,  0x4808,  0x02},
{0xF248,  0x7800,  0x02},
{0xF24A,  0x07C0,  0x02},
{0xF24C,  0x0FC0,  0x02},
{0xF24E,  0xF687,  0x02},
{0xF250,  0xF8ED,  0x02},
{0xF252,  0xF68E,  0x02},
{0xF254,  0xFE2B,  0x02},
{0xF256,  0xF688,  0x02},
{0xF258,  0xFF6B,  0x02},
{0xF25A,  0xF693,  0x02},
{0xF25C,  0xFB6B,  0x02},
{0xF25E,  0xF687,  0x02},
{0xF260,  0xF947,  0x02},
{0xF262,  0xBC7E,  0x02},
{0xF264,  0xF688,  0x02},
{0xF266,  0xFD8F,  0x02},
{0xF268,  0x239C,  0x02},
{0xF26A,  0x0018,  0x02},
{0x0006, 0x16, 0x01},                  /* INCK_SET : 24MHz */

{0xFFFF,  0xFF,  0xFF}
};

static const _isx012_regset_t ISX012_Sensor_Off_VCM[] = {
{0x6674, 0x01, 0x01},                      /*  AF_MONICHG_MOVE_F */
{0x00B2, 0x02, 0x01},                      /* AFMODE_MONI : Manual AF mode */
{0x0081, 0x00, 0x01},                      /* MODESEL : Monitoring mode */
{0x0082, 0x01, 0x01},                      /* MONI_REFRESH */
{0xFFFE, 0xC8, 0x01},                      /*  $wait,  200 */
{0x6600, 0x0000, 0x02},                      /*  AF_SEARCH_AREA_LOW */
{0x6666, 0x0000, 0x02},                      /*  AF_AREA_LOW_TYPE1 */
{0x6648, 0x00C8, 0x02},                      /*  AF_MANUAL_POS : */
{0x00B1, 0x01, 0x01},                      /* AF_RESTART_F */
{0x0082, 0x01, 0x01},                      /* MONI_REFRESH */
{0xFFFE, 0x01, 0x01},                      /*  $wait, 1 */
{0x6648, 0x0019, 0x02},                      /*  AF_MANUAL_POS : */
{0x00B1, 0x01, 0x01},                      /*  AF_RESTART_F */
{0x0082, 0x01, 0x01},                      /* MONI_REFRESH */
{0xFFFE, 0x01, 0x01},                      /*  $wait, 1 */

{0xFFFF, 0xFF, 0xFF}
};

void ISX012_pll_init(struct device *dev)
{
	int ret = 0;
	struct i2c_client *pClient = container_of(dev, struct i2c_client, dev);

	printk(KERN_ERR "%s(): START ISX012_Pll_Setting_4\n", __func__);
	ret = ISX012_table_write(pClient, ISX012_Pll_Setting_4, 0);

	if (ret < 0) {
		printk(KERN_ERR "%s(): ISX012_Pll_Setting_4 failed ISX012 SADDR is not 0x3D\n", __func__);
	}
	printk(KERN_ERR "%s(): END ISX012_Pll_Setting_4\n", __func__);
}

void ISX012_sensor_off_VCM(struct device *dev)
{
	int ret = 0;
	struct i2c_client *pClient = container_of(dev, struct i2c_client, dev);

	printk(KERN_ERR "%s(): START ISX012_Sensor_Off_VCM\n", __func__);
	ret = ISX012_table_write(pClient, ISX012_Sensor_Off_VCM, 0);

	if (ret < 0) {
		printk(KERN_ERR "%s(): ISX012_Sensor_Off_VCM failed ISX012 SADDR is not 0x3D\n", __func__);
	}
	printk(KERN_ERR "%s(): END ISX012_Sensor_Off_VCM\n", __func__);
}

static struct ISX012 *to_ISX012(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client),
					struct ISX012, subdev);
}

/* Find a data format by a pixel code in an array */
static const struct ISX012_datafmt *ISX012_find_datafmt(
					enum v4l2_mbus_pixelcode code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ISX012_colour_fmts); i++)
		if (ISX012_colour_fmts[i].code == code)
			return ISX012_colour_fmts + i;

	return NULL;
}

/* select nearest higher resolution for capture */
static void ISX012_res_roundup(u32 *width, u32 *height)
{
	int i;
	int res_x[] = { 640, 1280, 1280, 2560 };
	int res_y[] = { 480, 720, 960, 1920 };

	for (i = 0; i < ARRAY_SIZE(res_x); i++) {
		if (res_x[i] >= *width && res_y[i] >= *height) {
			*width = res_x[i];
			*height = res_y[i];
			return;
		}
	}

	*width = res_x[3];
	*height = res_y[3];
}

static int ISX012_try_fmt(struct v4l2_subdev *sd,
	       struct v4l2_mbus_framefmt *mf)
{
	const struct ISX012_datafmt *fmt = ISX012_find_datafmt(mf->code);

	dev_dbg(sd->v4l2_dev->dev, "%s(%u)\n", __func__, mf->code);

	if (!fmt) {
		mf->code	= ISX012_colour_fmts[0].code;
		mf->colorspace	= ISX012_colour_fmts[0].colorspace;
	}

	dev_dbg(sd->v4l2_dev->dev, "in: mf->width = %d, height = %d\n",
		mf->width, mf->height);
	ISX012_res_roundup(&mf->width, &mf->height);
	dev_dbg(sd->v4l2_dev->dev, "out: mf->width = %d, height = %d\n",
		mf->width, mf->height);
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ISX012_s_fmt(struct v4l2_subdev *sd,
	     struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ISX012 *priv = to_ISX012(client);

	dev_dbg(sd->v4l2_dev->dev, "%s(%u)\n", __func__, mf->code);

	/* MIPI CSI could have changed the format, double-check */
	if (!ISX012_find_datafmt(mf->code)) {
		dev_err(sd->v4l2_dev->dev, "%s -EINVAL\n", __func__);
		return -EINVAL;
	}

	ISX012_try_fmt(sd, mf);

	priv->fmt	= ISX012_find_datafmt(mf->code);
	priv->width	= mf->width;
	priv->height	= mf->height;

	return 0;
}

static int isx012_s_power(struct v4l2_subdev *sd, int on)
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

static int ISX012_g_fmt(struct v4l2_subdev *sd,
	     struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ISX012 *priv = to_ISX012(client);

	const struct ISX012_datafmt *fmt = priv->fmt;

	mf->code	= fmt->code;
	mf->colorspace	= fmt->colorspace;
	mf->width	= priv->width;
	mf->height	= priv->height;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ISX012_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ISX012 *priv = to_ISX012(client);
	struct v4l2_rect *rect = &a->c;

	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rect->top	= 0;
	rect->left	= 0;
	rect->width	= priv->width;
	rect->height	= priv->height;

	return 0;
}

static int ISX012_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ISX012 *priv = to_ISX012(client);

	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= priv->width;
	a->bounds.height		= priv->height;
	dev_dbg(&client->dev, "crop: width = %d, height = %d\n",
		a->bounds.width, a->bounds.height);
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int ISX012_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
		enum v4l2_mbus_pixelcode *code)
{
	if ((unsigned int)index >= ARRAY_SIZE(ISX012_colour_fmts))
		return -EINVAL;

	*code = ISX012_colour_fmts[index].code;
	return 0;
}

static int ISX012_g_chip_ident(struct v4l2_subdev *sd,
		    struct v4l2_dbg_chip_ident *id)
{
#if 0
	/* check i2c device */
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg[2];
	unsigned char send_buf[2];
	unsigned char rcv_buf[2];
	int loop = 0;
	int ret = 0;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags & I2C_M_TEN;
	msg[0].len = 2;
	msg[0].buf = (char *) send_buf;
	/* FW Sensor ID Support */
	send_buf[0] = 0x01;
	send_buf[1] = 0x5A;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags & I2C_M_TEN;
	msg[1].flags |= I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = rcv_buf;

	for (loop = 0; loop < 5; loop++) {
		ret = i2c_transfer(client->adapter, msg, 2);
		if (0 <= ret)
			break;
	}
	if (0 > ret) {
		dev_err(&client->dev, "%s :Read Error(%d)\n", __func__, ret);
		id->ident = V4L2_IDENT_NONE;
	} else {
		dev_dbg(&client->dev, "%s :Read OK\n", __func__);
		id->ident = V4L2_IDENT_ISX012;
	}
#endif
	id->ident = V4L2_IDENT_ISX012;
	id->revision = 0;

	return 0;
}

/* Request bus settings on camera side */
static int ISX012_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2;
	cfg->flags = V4L2_MBUS_CSI2_2_LANE |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	return 0;
}

/* Alter bus settings on camera side */
static int ISX012_s_mbus_config(struct v4l2_subdev *sd,
				const struct v4l2_mbus_config *cfg)
{
	return 0;
}

static int ISX012_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_GET_TUNING:
#ifdef CONFIG_SOC_CAMERA_ISX012_TUNING
		ctrl->value = 1;
#else
		ctrl->value = 0;
#endif
		/* no break */
	default:
		return 0;
	}
	return -ENOIOCTLCMD;
}

static struct v4l2_subdev_video_ops ISX012_subdev_video_ops = {
	.s_mbus_fmt	= ISX012_s_fmt,
	.g_mbus_fmt	= ISX012_g_fmt,
	.try_mbus_fmt	= ISX012_try_fmt,
	.enum_mbus_fmt	= ISX012_enum_fmt,
	.g_crop		= ISX012_g_crop,
	.cropcap	= ISX012_cropcap,
	.g_mbus_config	= ISX012_g_mbus_config,
	.s_mbus_config	= ISX012_s_mbus_config,
};

static struct v4l2_subdev_core_ops ISX012_subdev_core_ops = {
	.s_power	= isx012_s_power,
	.g_chip_ident	= ISX012_g_chip_ident,
	.g_ctrl		= ISX012_g_ctrl,
};

static struct v4l2_subdev_ops ISX012_subdev_ops = {
	.core	= &ISX012_subdev_core_ops,
	.video	= &ISX012_subdev_video_ops,
};

static int ISX012_s_ctrl(struct v4l2_ctrl *ctrl)
{
	return 0;
}

static struct v4l2_ctrl_ops ISX012_ctrl_ops = {
	.s_ctrl = ISX012_s_ctrl,
};

static int ISX012_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct ISX012 *priv;
	struct soc_camera_subdev_desc *sdesc = soc_camera_i2c_to_desc(client);
	int ret = 0;

	dev_dbg(&client->dev, "%s():\n", __func__);

	if (!sdesc) {
		dev_err(&client->dev, "ISX012: missing platform data!\n");
		return -EINVAL;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev,
			"ISX012: Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &ISX012_subdev_ops);
	v4l2_ctrl_handler_init(&priv->hdl, 4);
	v4l2_ctrl_new_std(&priv->hdl, &ISX012_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ISX012_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ISX012_ctrl_ops,
			V4L2_CID_GAIN, 0, 127, 1, 66);
	v4l2_ctrl_new_std(&priv->hdl, &ISX012_ctrl_ops,
			V4L2_CID_AUTO_WHITE_BALANCE, 0, 1, 1, 1);
	priv->subdev.ctrl_handler = &priv->hdl;
	if (priv->hdl.error) {
		int err = priv->hdl.error;

		kfree(priv);
		return err;
	}

	priv->width	= 640;
	priv->height	= 480;
	priv->fmt	= &ISX012_colour_fmts[0];
	ret = v4l2_ctrl_handler_setup(&priv->hdl);
	if (0 > ret) {
		dev_err(&client->dev,
			"ISX012: v4l2_ctrl_handler_setup Error(%d)\n", ret);
		kfree(priv);
		return ret;
	}

	if (cam_class_init == false) {
		dev_dbg(&client->dev,
			"Start create class for factory test mode !\n");
		camera_class = class_create(THIS_MODULE, "camera");
		cam_class_init = true;
	}

	if (camera_class) {
		dev_dbg(&client->dev, "Create main camera device !\n");

		sec_main_cam_dev = device_create(camera_class,
						NULL, 0, NULL, "rear");
		if (IS_ERR(sec_main_cam_dev)) {
			dev_err(&client->dev,
				"Failed to create device"
				"(sec_main_cam_dev)!\n");
		}

		if (device_create_file(sec_main_cam_dev,
					&dev_attr_rear_camtype) < 0) {
			dev_err(&client->dev,
				"failed to create main camera "
				"device file, %s\n",
				dev_attr_rear_camtype.attr.name);
		}
		if (device_create_file(sec_main_cam_dev,
					&dev_attr_rear_camfw) < 0) {
			dev_err(&client->dev,
				"failed to create main camera "
				"device file, %s\n",
				dev_attr_rear_camfw.attr.name);
		}
		if (device_create_file(sec_main_cam_dev,
					&dev_attr_rear_flash) < 0) {
			dev_err(&client->dev,
				"failed to create main camera "
				"device file, %s\n",
				dev_attr_rear_flash.attr.name);
		}
		if (device_create_file(sec_main_cam_dev,
					&dev_attr_rear_vendorid) < 0) {
			dev_err(&client->dev,
				"failed to create main camera "
				"device file, %s\n",
				dev_attr_rear_vendorid.attr.name);
		}
	}

	return ret;
}

static int ISX012_remove(struct i2c_client *client)
{
	struct ISX012 *priv = to_ISX012(client);
	struct soc_camera_subdev_desc *sdesc = soc_camera_i2c_to_desc(client);

	v4l2_device_unregister_subdev(&priv->subdev);
	if (sdesc->free_bus)
		sdesc->free_bus(sdesc);
	v4l2_ctrl_handler_free(&priv->hdl);
	kfree(priv);

	return 0;
}

/* CAM0 Power function */
int ISX012_power(struct device *dev, int power_on)
{
	struct clk *vclk1_clk, *vclk2_clk;
	int iRet;
	struct regulator *regulator;
	dev_dbg(dev, "%s(): power_on=%d\n", __func__, power_on);

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
		printk(KERN_ALERT "%s PowerON\n", __func__);
		sh_csi2_power(dev, power_on);
#ifndef CONFIG_MACH_VIVALTOLTE
		gpio_set_value(GPIO_PORT3, 0); /* CAM_PWR_EN Low */
#endif
		gpio_set_value(GPIO_PORT16, 0); /* CAM1_RST_N */
		gpio_set_value(GPIO_PORT91, 0); /* CAM1_STBY */
		gpio_set_value(GPIO_PORT20, 0); /* CAM0_RST_N */
		gpio_set_value(GPIO_PORT45, 0); /* CAM0_STBY */

		/* CAM_CORE_1V2  On */
#ifndef CONFIG_MACH_VIVALTOLTE
		gpio_set_value(GPIO_PORT3, 1);
#else
		regulator = regulator_get(NULL, "cam_sensor_core_1v2");
		if (IS_ERR(regulator))
			return -1;
		if (regulator_enable(regulator))
			dev_warn(dev, "Could not enable regulator\n");
		regulator_put(regulator);
#endif
		/* mdelay(1); */

		/* CAM_VDDIO_1V8  On */
		regulator = regulator_get(NULL, "cam_sensor_io");
		if (IS_ERR(regulator))
			return -1;
		if (regulator_enable(regulator))
			dev_warn(dev, "Could not enable regulator\n");
		regulator_put(regulator);

		mdelay(1);

		/* CAM_AVDD_2V8 On */
		regulator = regulator_get(NULL, "cam_sensor_a");
		if (IS_ERR(regulator))
			return -1;
		if (regulator_enable(regulator))
			dev_warn(dev, "Could not enable regulator\n");
		regulator_put(regulator);
		mdelay(1);

		gpio_set_value(GPIO_PORT91, 1); /* CAM1_STBY */
		mdelay(2);

		iRet = clk_set_rate(vclk1_clk,
			clk_round_rate(vclk1_clk, 24000000));
		if (0 != iRet) {
			dev_err(dev,
				"clk_set_rate(vclk1_clk) failed (ret=%d)\n",
				iRet);
		}

		iRet = clk_enable(vclk1_clk);
		if (0 != iRet) {
			dev_err(dev, "clk_enable(vclk1_clk) failed (ret=%d)\n",
				iRet);
		}
		mdelay(3);

		gpio_set_value(GPIO_PORT16, 1); /* CAM1_RST_N */
		mdelay(4);

		gpio_set_value(GPIO_PORT91, 0); /* CAM1_STBY */
		mdelay(2);

		gpio_set_value(GPIO_PORT20, 1); /* CAM0_RST_N Hi */
		mdelay(20);

		printk(KERN_ALERT "%s First OM check\n", __func__);
		ISX012_waitForModeTransition(dev, 0);

		printk(KERN_ALERT "%s ISX012_pll_init\n", __func__);
		ISX012_pll_init(dev);

		printk(KERN_ALERT "%s ISX012_waitForModeTransition\n", __func__);
		ISX012_waitForModeTransition(dev, 0);
		mdelay(20);
		gpio_set_value(GPIO_PORT45, 1); /* CAM0_STBY */
		mdelay(1);

		printk(KERN_ALERT "%s Third OM check\n", __func__);
		ISX012_waitForModeTransition(dev, 0);

		/* 5M_AF_2V8 On */
		regulator = regulator_get(NULL, "cam_af");
		if (IS_ERR(regulator))
			return -1;
		if (regulator_enable(regulator))
			dev_warn(dev, "Could not enable regulator\n");
		regulator_put(regulator);

		printk(KERN_ALERT "%s PowerON fin\n", __func__);
	} else {
		printk(KERN_ALERT "%s ISX012_sensor_off_VCM \n", __func__);
		ISX012_sensor_off_VCM(dev);

		printk(KERN_ALERT "%s ISX012_waitForModeTransition\n", __func__);
		ISX012_waitForModeTransition(dev, 0);

		printk(KERN_ALERT "%s PowerOFF\n", __func__);

		gpio_set_value(GPIO_PORT20, 0); /* CAM0_RST_N */
		mdelay(1);

		clk_disable(vclk1_clk);

		gpio_set_value(GPIO_PORT45, 0); /* CAM0_STBY */
		mdelay(1);

		gpio_set_value(GPIO_PORT16, 0); /* CAM1_RST_N */
		mdelay(1);

		/* CAM_CORE_1V2  Off */
#ifndef CONFIG_MACH_VIVALTOLTE
		gpio_set_value(GPIO_PORT3, 0);
#else
		regulator = regulator_get(NULL, "cam_sensor_core_1v2");
		if (IS_ERR(regulator))
			return -1;
		regulator_disable(regulator);
		regulator_put(regulator);
#endif
		mdelay(1);

		/* CAM_VDDIO_1V8 Off */
		regulator = regulator_get(NULL, "cam_sensor_io");
		if (IS_ERR(regulator))
			return -1;
		regulator_disable(regulator);
		regulator_put(regulator);
		mdelay(1);

		/* CAM_AVDD_2V8  Off */
		regulator = regulator_get(NULL, "cam_sensor_a");
		if (IS_ERR(regulator))
			return -1;
		regulator_disable(regulator);
		regulator_put(regulator);
		mdelay(1);

		/* 5M_AF_2V8 Off */
		regulator = regulator_get(NULL, "cam_af");
		if (IS_ERR(regulator))
			return -1;
		regulator_disable(regulator);
		regulator_put(regulator);
		sh_csi2_power(dev, power_on);
		printk(KERN_ALERT "%s PowerOFF fin\n", __func__);
	}

	clk_put(vclk1_clk);
	clk_put(vclk2_clk);

	return 0;
}

static const struct i2c_device_id ISX012_id[] = {
	{ "ISX012", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ISX012_id);

static struct i2c_driver ISX012_i2c_driver = {
	.driver = {
		.name = "ISX012",
	},
	.probe		= ISX012_probe,
	.remove		= ISX012_remove,
	.id_table	= ISX012_id,
};

module_i2c_driver(ISX012_i2c_driver);

MODULE_DESCRIPTION("Samsung ISX012 Camera driver");
MODULE_AUTHOR("Renesas Mobile Corp.");
MODULE_LICENSE("GPL v2");
