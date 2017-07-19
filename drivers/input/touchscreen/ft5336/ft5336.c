/*
 *
 * FocalTech ft5x06 TouchScreen driver.
 *
 * Copyright (c) 2010  Focal tech Ltd.
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 * Copyright (c) 2013, Broadcom Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/platform_device.h> /*BRCM uses device tree.*/
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/i2c/ft5336.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
/* Early-suspend level */
#define FT5X06_SUSPEND_LEVEL 1
#endif
#define CFG_MAX_TOUCH_POINTS	5
#define FT_STARTUP_DLY	  200
#define FT_RESET_DLY		20
#define FT_T_RTP		 2
#define FT_T_VDR		 2
#define FT_PRESS		0x7F
#define FT_MAX_ID	   0x0F
#define FT_TOUCH_STEP	   6
#define FT_TOUCH_X_H_POS	3
#define FT_TOUCH_X_L_POS	4
#define FT_TOUCH_Y_H_POS	5
#define FT_TOUCH_Y_L_POS	6
#define FT_TOUCH_EVENT_POS  3
#define FT_TOUCH_ID_POS	 5
#define POINT_READ_BUF  (3 + FT_TOUCH_STEP * CFG_MAX_TOUCH_POINTS)
/*register address*/
#define FT5X06_REG_PMODE	0xA5
#define FT5X06_REG_FW_VER   0xA6
#define FT5X06_REG_POINT_RATE   0x88
#define FT5X06_REG_THGROUP  0x80
/* power register bits*/
#define FT5X06_PMODE_ACTIVE	 0x00
#define FT5X06_PMODE_MONITOR		0x01
#define FT5X06_PMODE_STANDBY		0x02
#define FT5X06_PMODE_HIBERNATE	  0x03
#define FT5X06_VTG_MIN_UV   3000000
#define FT5X06_VTG_MAX_UV   3300000
#define FT5X06_I2C_VTG_MIN_UV   1800000
#define FT5X06_I2C_VTG_MAX_UV   1800000

/*support sysfs*/
#define SYSFS_DEBUG
/*#define FTS_APK_DEBUG*/

#ifdef SYSFS_DEBUG
#include "ft5336_ex_fun.h"
#endif

/*Broadcom start*/
#define DBG_PRINTF   1

#if DBG_PRINTF

#ifdef dev_info
#undef dev_info
#endif
#define dev_info(dev, fmt, arg...)	printk(fmt, ##arg)

#ifdef dev_err
#undef dev_err
#endif
#define dev_err(dev, fmt, arg...)	printk(fmt, ##arg)

#ifdef dev_dbg
#undef dev_dbg
#endif
#define dev_dbg(dev, fmt, flag, arg...)	 \
	{if (bcmtch_debug_flag & flag) printk(fmt, flag, ##arg); }

#endif

/*- debug flag -*/

#define BCMTCH_DF_MV	0x00000001	  /* touch MOVE event */
#define BCMTCH_DF_UP	0x00000002	  /* touch UP event */
#define BCMTCH_DF_TE	0x00000004	  /* touch extension events */
#define BCMTCH_DF_BT	0x00000008	  /* BUTTON event */
#define BCMTCH_DF_CH	0x00000010	  /* channel protocol */
#define BCMTCH_DF_ST	0x00000020	  /* state protocol */
#define BCMTCH_DF_RC	0x00000040	  /* rm */
#define BCMTCH_DF_RF	0x00000080	  /* rf */
#define BCMTCH_DF_FR	0x00000100	  /* frame events */
#define BCMTCH_DF_FE	0x00000200	  /* frame extension events */
#define BCMTCH_DF_PB	0x00000400	  /* post-boot */
#define BCMTCH_DF_DT	0x00000800	  /* device tree */
#define BCMTCH_DF_HO	0x00001000	  /* host override */
#define BCMTCH_DF_PM	0x00002000	  /* power management */
#define BCMTCH_DF_WD	0x00004000	  /* watch dog */
#define BCMTCH_DF_IH	0x00008000	  /* interrupt */
#define BCMTCH_DF_I2C   0x00010000	  /* i2c */
#define BCMTCH_DF_INFO  0x00020000	  /* info */

static int bcmtch_debug_flag = 0x3FFFF; /* = 0; */

/*#define CONFIG_VIRTUAL_KEYS_FT5336*/
#ifdef CONFIG_VIRTUAL_KEYS_FT5336
static const char *vkey_scope;
#endif

#define M250_IRQPIN_BASE		2000
#define pin_irq(irq_number)	 (irq_number - M250_IRQPIN_BASE)

/*Broadcom end*/

struct ts_event {
	u16 x[CFG_MAX_TOUCH_POINTS];	/*x coordinate */
	u16 y[CFG_MAX_TOUCH_POINTS];	/*y coordinate */
	/* touch event: 0 -- down; 1-- contact; 2 -- contact */
	u8 touch_event[CFG_MAX_TOUCH_POINTS];
	u8 finger_id[CFG_MAX_TOUCH_POINTS]; /*touch ID */
	u16 pressure;
	u8 touch_point;
};
struct ft5x06_ts_data {
	/*Work queue structure for defining work queue handler*/
	struct work_struct work;
	/*Work queue structure for transaction handling*/
	struct workqueue_struct *p_workqueue;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct ts_event event;
	const struct ft5x06_ts_platform_data *pdata;
	struct regulator *vdd;
	struct regulator *vcc_i2c;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};
int ft5x06_i2c_read(struct i2c_client *client, char *writebuf,
			   int writelen, char *readbuf, int readlen)
{
	int ret;
	if (writelen > 0) {
		struct i2c_msg msgs[] = {
			{
				 .addr = client->addr,
				 .flags = 0,
				 .len = writelen,
				 .buf = writebuf,
			 },
			{
				 .addr = client->addr,
				 .flags = I2C_M_RD,
				 .len = readlen,
				 .buf = readbuf,
			 },
		};
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret < 0)
			dev_err(&client->dev, "%s: i2c read error.\n",
				__func__);
	} else {
		struct i2c_msg msgs[] = {
			{
				 .addr = client->addr,
				 .flags = I2C_M_RD,
				 .len = readlen,
				 .buf = readbuf,
			 },
		};
		ret = i2c_transfer(client->adapter, msgs, 1);
		if (ret < 0)
			dev_err(&client->dev, "%s:i2c read error.\n", __func__);
	}
	return ret;
}
int ft5x06_i2c_write(struct i2c_client *client, char *writebuf,
				int writelen)
{
	int ret;
	struct i2c_msg msgs[] = {
		{
			 .addr = client->addr,
			 .flags = 0,
			 .len = writelen,
			 .buf = writebuf,
		 },
	};
	ret = i2c_transfer(client->adapter, msgs, 1);
	if (ret < 0)
		dev_err(&client->dev, "%s: i2c write error.\n", __func__);
	return ret;
}
static void ft5x06_report_value(struct ft5x06_ts_data *data)
{
	struct ts_event *event = &data->event;
	int i;
	int fingerdown = 0;
	for (i = 0; i < event->touch_point; i++) {
		if (event->touch_event[i] == 0 || event->touch_event[i] == 2) {
			event->pressure = FT_PRESS;
			fingerdown++;
		} else {
			event->pressure = 0;
		}
		input_report_abs(data->input_dev, ABS_MT_POSITION_X,
				 event->x[i]);
		input_report_abs(data->input_dev, ABS_MT_POSITION_Y,
				 event->y[i]);
		input_report_abs(data->input_dev, ABS_MT_PRESSURE,
				 event->pressure);
		input_report_abs(data->input_dev, ABS_MT_TRACKING_ID,
				 event->finger_id[i]);
		input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR,
				 event->pressure);
		input_mt_sync(data->input_dev);
	}
	input_report_key(data->input_dev, BTN_TOUCH, !!fingerdown);
	input_sync(data->input_dev);
}
static int ft5x06_handle_touchdata(struct ft5x06_ts_data *data)
{
	struct ts_event *event = &data->event;
	int ret, i;
	u8 buf[POINT_READ_BUF] = { 0 };
	u8 pointid = FT_MAX_ID;
	ret = ft5x06_i2c_read(data->client, buf, 1, buf, POINT_READ_BUF);
	if (ret < 0) {
		dev_err(&data->client->dev, "%s read touchdata failed.\n",
			__func__);
		return ret;
	}
	memset(event, 0, sizeof(struct ts_event));
	event->touch_point = 0;
	for (i = 0; i < CFG_MAX_TOUCH_POINTS; i++) {
		pointid = (buf[FT_TOUCH_ID_POS + FT_TOUCH_STEP * i]) >> 4;
		if (pointid >= FT_MAX_ID)
			break;
		else
			event->touch_point++;
		event->x[i] =
		(s16) (buf[FT_TOUCH_X_H_POS + FT_TOUCH_STEP * i] & 0x0F) <<
		8 | (s16) buf[FT_TOUCH_X_L_POS + FT_TOUCH_STEP * i];
		event->y[i] =
		(s16) (buf[FT_TOUCH_Y_H_POS + FT_TOUCH_STEP * i] & 0x0F) <<
		8 | (s16) buf[FT_TOUCH_Y_L_POS + FT_TOUCH_STEP * i];
		event->touch_event[i] =
			buf[FT_TOUCH_EVENT_POS + FT_TOUCH_STEP * i] >> 6;
		event->finger_id[i] =
			(buf[FT_TOUCH_ID_POS + FT_TOUCH_STEP * i]) >> 4;
		dev_dbg(&data->client->dev,
			"DT:[%x]:touch_event=%d, x=%d, y=%d,  finger_id=%d\n",
			BCMTCH_DF_UP,
			event->touch_event[i], event->x[i], event->y[i],
			event->finger_id[i]);
	}
	ft5x06_report_value(data);
	return 0;
}
#if 0
static irqreturn_t ft5x06_ts_interrupt(int irq, void *dev_id)
{
	struct ft5x06_ts_data *data = dev_id;
	int rc;
	disable_irq(data->client->irq);
	rc = ft5x06_handle_touchdata(data);
	if (rc)
		pr_err("%s: handling touchdata failed\n", __func__);
	enable_irq(data->client->irq);
	return IRQ_HANDLED;
}
#endif
static void focaltech_ft5336_work_func(struct work_struct *work)
{
	struct ft5x06_ts_data *data = container_of(work,
					struct ft5x06_ts_data, work);
	int rc;
	/*disable_irq_nosync(ft5x0x_ts->irq);*/
	rc = ft5x06_handle_touchdata(data);
	if (rc)
		pr_err("%s: handling touchdata failed\n", __func__);
	/*enable_irq(ft5x0x_ts->irq);*/
}

irqreturn_t focaltech_ft5336_irq_handler(int irq, void *dev_id)
{
	struct ft5x06_ts_data *data = dev_id;

	dev_dbg(&data->client->dev,
		"IH:[%x]:touch_irq=%d\n",
		BCMTCH_DF_IH,
		data->pdata->touch_irq);

	queue_work(data->p_workqueue, (struct work_struct *)&data->work);

	return IRQ_HANDLED;
}



#ifdef CONFIG_VIRTUAL_KEYS_FT5336

static ssize_t ft5336_virtual_keys_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, vkey_scope);
}

static struct kobj_attribute ft5336_virtual_keys_attr = {
	.attr = {
		.name = "virtualkeys.FocalTech-Ft5336",
		.mode = S_IRUGO,
	},
	.show = &ft5336_virtual_keys_show,
};

static struct attribute *ft5336_properties_attrs[] = {
	&ft5336_virtual_keys_attr.attr,
	NULL
};

static struct attribute_group ft5336_properties_attr_group = {
	.attrs = ft5336_properties_attrs,
};

static void ft5336_virtual_keys_init(void)
{
	int ret = 0;
	struct kobject *properties_kobj;
	properties_kobj = kobject_create_and_add("board_properties", NULL);
	if (properties_kobj)
		ret = sysfs_create_group(properties_kobj,
					&ft5336_properties_attr_group);
	if (!properties_kobj || ret)
		pr_err("failed to create board_properties\n");
}

#endif

static int ft5x06_power_on(struct ft5x06_ts_data *data, bool on)
{
	int rc;
	if (!on)
		goto power_off;
	rc = regulator_enable(data->vdd);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vdd enable failed rc=%d\n", rc);
		return rc;
	} else  {
		dev_info(&data->client->dev, "[TSP] 3.0v regulator power on\n");
	}
#if 0 /*BRCM do not support vcc_i2C*/
	rc = regulator_enable(data->vcc_i2c);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vcc_i2c enable failed rc=%d\n", rc);
		regulator_disable(data->vdd);
	}
#endif
	return rc;
power_off:
	rc = regulator_disable(data->vdd);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vdd disable failed rc=%d\n", rc);
		return rc;
	} else  {
		dev_info(&data->client->dev, "[TSP] 3.0v regulator power off\n");
	}
#if 0 /*BRCM do not support vcc_i2C*/
	rc = regulator_disable(data->vcc_i2c);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vcc_i2c disable failed rc=%d\n", rc);
		regulator_enable(data->vdd);
	}
#endif
	return rc;
}
static int ft5x06_power_init(struct ft5x06_ts_data *data, bool on)
{
	int rc;
	if (!on)
		goto pwr_deinit;
	/*BRCM vtsp_3v regulator is for touch panel*/
	data->vdd = regulator_get(&data->client->dev, "vtsp_3v");
	if (IS_ERR(data->vdd)) {
		rc = PTR_ERR(data->vdd);
		dev_err(&data->client->dev,
			"Regulator get failed vdd rc=%d\n", rc);
		return rc;
	}
	if (regulator_count_voltages(data->vdd) > 0) {
		rc = regulator_set_voltage(data->vdd, FT5X06_VTG_MIN_UV,
					   FT5X06_VTG_MAX_UV);
		dev_info(&data->client->dev,
			"[TSP] 3.0v regulator_set_voltage ret_val = %d\n", rc);
		if (rc) {
			dev_err(&data->client->dev,
				"Regulator set_vtg failed vdd rc=%d\n", rc);
			goto reg_vdd_put;
		}
	}
#if 0 /*BRCM do not support vcc_i2C*/
	data->vcc_i2c = regulator_get(&data->client->dev, "vcc_i2c");
	if (IS_ERR(data->vcc_i2c)) {
		rc = PTR_ERR(data->vcc_i2c);
		dev_err(&data->client->dev,
			"Regulator get failed vcc_i2c rc=%d\n", rc);
		goto reg_vdd_set_vtg;
	}
	if (regulator_count_voltages(data->vcc_i2c) > 0) {
		rc = regulator_set_voltage(data->vcc_i2c, FT5X06_I2C_VTG_MIN_UV,
					   FT5X06_I2C_VTG_MAX_UV);
		if (rc) {
			dev_err(&data->client->dev,
			"Regulator set_vtg failed vcc_i2c rc=%d\n", rc);
			goto reg_vcc_i2c_put;
		}
	}
#endif
	return 0;
#if 0 /*BRCM do not support vcc_i2C*/
reg_vcc_i2c_put:
	regulator_put(data->vcc_i2c);
reg_vdd_set_vtg:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, FT5X06_VTG_MAX_UV);
#endif
reg_vdd_put:
	regulator_put(data->vdd);
	return rc;
pwr_deinit:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, FT5X06_VTG_MAX_UV);
	regulator_put(data->vdd);
#if 0 /*BRCM do not support vcc_i2C*/
	if (regulator_count_voltages(data->vcc_i2c) > 0)
		regulator_set_voltage(data->vcc_i2c, 0, FT5X06_I2C_VTG_MAX_UV);
	regulator_put(data->vcc_i2c);
#endif
	return 0;
}
#ifdef CONFIG_PM
static int ft5x06_ts_suspend(struct device *dev)
{
	struct ft5x06_ts_data *data = dev_get_drvdata(dev);
	char txbuf[2];
	disable_irq(data->client->irq);
	if (gpio_is_valid(data->pdata->reset_gpio)) {
		txbuf[0] = FT5X06_REG_PMODE;
		txbuf[1] = FT5X06_PMODE_HIBERNATE;
		ft5x06_i2c_write(data->client, txbuf, sizeof(txbuf));
	}
	return 0;
}
static int ft5x06_ts_resume(struct device *dev)
{
	struct ft5x06_ts_data *data = dev_get_drvdata(dev);
	if (gpio_is_valid(data->pdata->reset_gpio)) {
		gpio_set_value_cansleep(data->pdata->reset_gpio, 0);
		msleep(FT_RESET_DLY);
		gpio_set_value_cansleep(data->pdata->reset_gpio, 1);
	}
	enable_irq(data->client->irq);
	return 0;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
static void ft5x06_ts_early_suspend(struct early_suspend *handler)
{
	struct ft5x06_ts_data *data = container_of(handler,
						   struct ft5x06_ts_data,
						   early_suspend);
	ft5x06_ts_suspend(&data->client->dev);
}
static void ft5x06_ts_late_resume(struct early_suspend *handler)
{
	struct ft5x06_ts_data *data = container_of(handler,
						   struct ft5x06_ts_data,
						   early_suspend);
	ft5x06_ts_resume(&data->client->dev);
}
#endif
static const struct dev_pm_ops ft5x06_ts_pm_ops = {
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend = ft5x06_ts_suspend,
	.resume = ft5x06_ts_resume,
#endif
};
#endif

static int init_platform_data(struct device *p_device,
			struct ft5x06_ts_platform_data *pdata)
{
	int ret_val = 0;
	/*int32_t   idx;*/
	/*int32_t btn_count;*/
	int32_t of_ret_val;
	struct device_node *np;
	enum of_gpio_flags gpio_flags;

	if (!p_device) {
		dev_err(p_device, "%s()error: client->dev== NULL\n", __func__);
		ret_val = -EINVAL;
	} else {
		np = p_device->of_node;
		if (!np) {
			dev_err(p_device,
				" Device tree (DT) error! of_node is NULL.\n");
			return -ENODEV;
		}

#if 0 /*FT5336 do not need i2c_addr_sys*/
		/*
		 * Obtain the address of the SYS/AHB on I2C bus.
		 */
		of_ret_val = of_property_read_u32(np, "addr-sys",
					&pdata->i2c_addr_sys);
		if (of_ret_val) {
			dev_err(p_device,
				"DT property addr-sys not found!\n");
			ret_val = -ENODEV;
			goto of_read_error;
		}

		dev_dbg(p_device,
			"DT:[%x]:addr-sys = 0x%x\n",
			BCMTCH_DF_DT,
			pdata->i2c_addr_sys);
#endif

		/*
		 * Obtain the GPIO reset pin.
		 */
		if (!of_find_property(np, "reset-gpios", NULL)) {
			dev_err(p_device,
				"DT property reset-gpios not found!\n");
			ret_val = of_ret_val;
			pdata->reset_gpio = -1;
			pdata->reset_gpio_polarity = -1;
		} else {
			pdata->reset_gpio =
					of_get_named_gpio_flags(
						np,
						"reset-gpios",
						0,
						&gpio_flags);
			dev_dbg(p_device,
				"DT:[%x]:gpio-reset-pin = 0x%x\n",
				BCMTCH_DF_DT,
				pdata->reset_gpio);

			pdata->reset_gpio_polarity =
					gpio_flags & OF_GPIO_ACTIVE_LOW;
			dev_dbg(p_device,
				"DT:[%x]:gpio-reset-polarity = 0x%x\n",
				BCMTCH_DF_DT,
				pdata->reset_gpio_polarity);

#if 0 /*FT5336 do not need reset-time-ms*/
			/*
			 * Obtain the GPIO reset time in ms.
			 */
			of_ret_val = of_property_read_u32(np, "reset-time-ms",
					&pdata->reset_gpio_time_ms);
			if (of_ret_val) {
				/* set default value */
				pdata->reset_gpio_time_ms = 100;
			}
			dev_dbg(p_device,
				"DT:[%x]:gpio-reset-time = %u\n",
				BCMTCH_DF_DT,
				pdata->reset_gpio_time_ms);
#endif
		}

		/*
		 * Obtain irq
		 *client->irq == pdata->touch_irq
		 */
		 pdata->touch_irq =
			irq_of_parse_and_map(np, 0);
		if (pdata->touch_irq) {
			pdata->irq_gpio = pin_irq(pdata->touch_irq);
			pdata->irq_gpio_trigger = IRQF_TRIGGER_FALLING;
			dev_dbg(p_device,
				"DT:[%x]:irq = %d, irq_gpio=0x%d, irq_gpio_trigger=0x%x",
				BCMTCH_DF_DT,
				pdata->touch_irq,
				pdata->irq_gpio,
				pdata->irq_gpio_trigger);
		} else {
			dev_err(p_device,
				"DT: interrupts (irq) request failed!\n");
			ret_val = -ENODEV;
			goto of_read_error;
		}

		/*
		 * Obtain x-max-value
		 */
		of_ret_val = of_property_read_u32(np, "x-max-value",
						&pdata->x_max);
		if (of_ret_val) {
			dev_dbg(p_device,
				"DT:[%x]: x-max-value not found!\n",
				BCMTCH_DF_DT);
			pdata->x_max = FT5336_X_MAX_DEFAULT;
		}
		dev_dbg(p_device,
			"DT:[%x]:x-max-value=%d",
			BCMTCH_DF_DT,
			pdata->x_max);

		/*
		 * Obtain y-max-value
		 */
		of_ret_val = of_property_read_u32(np, "y-max-value",
						&pdata->y_max);
		if (of_ret_val) {
			dev_dbg(p_device,
				"DT:[%x]: y-max-value not found!\n",
				BCMTCH_DF_DT);
			pdata->y_max = FT5336_Y_MAX_DEFAULT;
		}
		dev_dbg(p_device,
			"DT:[%x]:y-max-value=%d",
			BCMTCH_DF_DT,
			pdata->y_max);

#if 0 /*FT5336 do not need ext-button-count & ext-button-map*/
		/*
		 * Obtain the key map.
		 */
		of_ret_val =
				of_property_read_u32(np, "ext-button-count",
						&btn_count);
		if (of_ret_val) {
			dev_dbg(p_device,
				"DT:[%x]:ext-button-count not found!\n",
				BCMTCH_DF_DT);
			btn_count = 0;
		}

		if (btn_count) {
			dev_dbg(p_device,
				"DT:[%x]:ext-button-count = %d\n",
				BCMTCH_DF_DT,
				btn_count);

			/* Read array data from device tree */
			of_ret_val =
					of_property_read_u32_array(
						np, "ext-button-map",
						(u32 *) pdata->ext_button_map,
						btn_count);
			if (of_ret_val) {
				dev_err(p_device,
					"DT property ext-button-map read failed!\n");
				pdata->ext_button_count = 0;
			} else {
				pdata->ext_button_count = btn_count;

				dev_dbg(p_device,
					"DT:[%x]:ext-button-map =",
					BCMTCH_DF_DT);
				for (idx = 0; idx < btn_count; idx++)
					dev_dbg(p_device,
						"DT:[%x]:%d",
						BCMTCH_DF_DT,
						pdata->ext_button_map[idx]);
			}
		}
#endif

#ifdef CONFIG_VIRTUAL_KEYS_FT5336 /*FT5336 do not need vkey_scope*/
		/*
		 * Obtain vkey_scope
		 */
		of_ret_val =
				of_property_read_string(np, "vkey_scope",
						&vkey_scope);
		if (of_ret_val) {
			dev_dbg(p_device,
				"DT:[%x]:vkey_scope not found!\n",
				BCMTCH_DF_DT);
			vkey_scope = "\n";
		}

		dev_dbg(p_device,
			"\nDT:[%x]:vkey_scope:%s",
			BCMTCH_DF_DT,
			vkey_scope);
#endif

#if 0 /*FT5336 do not need power-on-delay-us*/
		/*
		 * Wait for applying power to Napa.
		 * NAPA POR is 4ms == 4000us
		 * - Value should NOT be less than 5000us
		 */
		of_ret_val =
				of_property_read_u32(np,
					"power-on-delay-us",
					&pdata->power_on_delay_us);
		if (of_ret_val) {
			dev_info(p_device,
				"DT property power-on-delay-us not found!\n");
			pdata->power_on_delay_us =
					BCMTCH_POWER_ON_DELAY_US_MIN;
		}
		/*
		 * NAPA POR is 4ms == 4000us
		 * - Value should NOT be less than 5000us
		 */
		pdata->power_on_delay_us =
			max(BCMTCH_POWER_ON_DELAY_US_MIN,
				pdata->power_on_delay_us);

		dev_dbg(p_device,
			"DT:[%x]:power-on-delay-us = %u\n",
			BCMTCH_DF_DT,
			pdata->power_on_delay_us);
#endif
	}
	return ret_val;
of_read_error:
	if (!ret_val)
		ret_val = -ENODEV;

	return ret_val;
}


static int ft5x06_ts_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	/*BRCM platform use device tree not platform_data.*/
	/*const struct ft5x06_ts_platform_data *pdata =
			client->dev.platform_data;*/
		struct ft5x06_ts_platform_data *pdata;
	struct ft5x06_ts_data *data;
	struct input_dev *input_dev;
	u8 reg_value;
	u8 reg_addr;
	int err;

	dev_dbg(&client->dev,
		"INFO:[%x]:Driver Build at  %s : %s\n",
		BCMTCH_DF_INFO,
		__DATE__,
		__TIME__);

	/* print driver probe header */
	if (client)
		dev_dbg(&client->dev,
			"INFO:[%x]:dev=%s addr=0x%x irq=%d\n",
			BCMTCH_DF_INFO,
			client->name,
			client->addr,
			client->irq);

	if (id)
		dev_dbg(&client->dev,
			"INFO:[%x]:match id=%s\n",
			BCMTCH_DF_INFO,
			id->name);

	pdata = kzalloc(sizeof(struct ft5x06_ts_platform_data), GFP_KERNEL);
	if (!pdata) {
		dev_err(&client->dev,
				"Not enough memory for ft5x06_ts_platform_data\n");
		return -ENOMEM;
	}
	/* setup local platform data from client device structure */
	init_platform_data(&client->dev, pdata);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C not supported\n");
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}
	data = kzalloc(sizeof(struct ft5x06_ts_data), GFP_KERNEL);
	if (!data) {
		dev_err(&client->dev, "Not enough memory for ft5x06_ts_data\n");
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}
	data->p_workqueue = create_singlethread_workqueue("ft5x06_wq");
	if (data->p_workqueue) {
		INIT_WORK(&data->work, focaltech_ft5336_work_func);
	} else {
		dev_err(&client->dev, "Could not create work queue ft5x06_wq: no memory");
		err = -ENOMEM;
		goto exit_create_workqueue_failed;
	}

#ifdef CONFIG_VIRTUAL_KEYS_FT5336
	ft5336_virtual_keys_init();
#endif
	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto free_mem;
	}
	data->input_dev = input_dev;
	data->client = client;
	data->pdata = pdata;
	input_dev->name = FT5336_NAME;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_set_drvdata(input_dev, data);
	i2c_set_clientdata(client, data);
	/*__set_bit(EV_SYN, input_dev->evbit);*/
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
#ifdef CONFIG_VIRTUAL_KEYS_FT5336
	set_bit(KEY_MENU, input_dev->keybit);
	set_bit(KEY_HOME, input_dev->keybit);
	set_bit(KEY_BACK, input_dev->keybit);
	/*set_bit(KEY_SEARCH, input_dev->keybit);*/
#endif
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0,
				 pdata->x_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0,
				 pdata->y_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0,
				 CFG_MAX_TOUCH_POINTS, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, FT_PRESS, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, FT_PRESS, 0, 0);
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev, "Input device registration failed\n");
		goto free_inputdev;
	}
	if (pdata->power_init) {
		err = pdata->power_init(true);
		if (err) {
			dev_err(&client->dev, "power init failed");
			goto unreg_inputdev;
		}
	} else {
		err = ft5x06_power_init(data, true);
		if (err) {
			dev_err(&client->dev, "power init failed");
			goto unreg_inputdev;
		}
	}

	if (gpio_is_valid(pdata->irq_gpio)) {
		err = gpio_request(pdata->irq_gpio, "ft5x06_irq_gpio");
		if (err) {
			dev_err(&client->dev, "irq gpio request failed");
			goto pwr_off;
		}
		err = gpio_direction_input(pdata->irq_gpio);
		if (err) {
			dev_err(&client->dev,
				"set_direction for irq gpio failed\n");
			goto free_irq_gpio;
		}
	}
	if (gpio_is_valid(pdata->reset_gpio)) {
		err = gpio_request(pdata->reset_gpio, "ft5x06_reset_gpio");
		if (err) {
			dev_err(&client->dev, "reset gpio request failed");
			goto free_irq_gpio;
		}
		/*
		  pdata->reset_gpio_polarity=0, reset this touch panel.
		  Note the timing that FT5336 needs reset to low
		  before power on.
		*/
		err = gpio_direction_output(pdata->reset_gpio,
				pdata->reset_gpio_polarity);
		if (err) {
			dev_err(&client->dev,
				"set_direction for reset gpio failed\n");
			goto free_reset_gpio;
		}
		/*msleep(FT_RESET_DLY);*/
	}
	/*Power on FT5336*/
	msleep(FT_T_RTP);
	if (pdata->power_on) {
		err = pdata->power_on(true);
		if (err) {
			dev_err(&client->dev, "power on failed");
			goto pwr_deinit;
		}
	} else {
		err = ft5x06_power_on(data, true);
		if (err) {
			dev_err(&client->dev, "power on failed");
			goto pwr_deinit;
		}
	}
	msleep(FT_T_VDR);
	/*reset to high after power on, i.e., !pdata->reset_gpio_polarity=1 */
	gpio_set_value_cansleep(data->pdata->reset_gpio,
		!pdata->reset_gpio_polarity);

	/* make sure CTP already finish startup process */
	msleep(FT_STARTUP_DLY);

#ifdef SYSFS_DEBUG
	dev_info(&client->dev, "[FTS] ft5x0x_create_sysfs()\n");
	ft5x0x_create_sysfs(client);
#endif

#ifdef FTS_APK_DEBUG
	dev_info(&client->dev, "[FTS] ft5x0x_create_apk_debug_channel()\n");
	ft5x0x_create_apk_debug_channel(client);
#endif
	/*get some register information */
	reg_addr = FT5X06_REG_FW_VER;
	err = ft5x06_i2c_read(client, &reg_addr, 1, &reg_value, 1);
	/*if (err) dev_err(&client->dev, "version read failed");*/
	dev_info(&client->dev, "[FTS] Firmware version = 0x%x\n", reg_value);
	reg_addr = FT5X06_REG_POINT_RATE;
	ft5x06_i2c_read(client, &reg_addr, 1, &reg_value, 1);
	/*if (err) dev_err(&client->dev, "report rate read failed");*/
	dev_info(&client->dev, "[FTS] report rate is %dHz.\n", reg_value * 10);
	reg_addr = FT5X06_REG_THGROUP;
	err = ft5x06_i2c_read(client, &reg_addr, 1, &reg_value, 1);
	/*if (err) dev_err(&client->dev, "threshold read failed");*/
	dev_dbg(&client->dev, "[FTS] touch threshold is %d.\n", reg_value * 4);
#if 0	/*BRCM platform do not support request_threaded_irq().*/
	err = request_threaded_irq(client->irq, NULL,
				   ft5x06_ts_interrupt, pdata->irq_gpio_trigger,
				   client->dev.driver->name, data);
	if (err) {
		dev_err(&client->dev, "request irq failed\n");
		goto free_reset_gpio;
	}
#else
	if (request_irq(client->irq, focaltech_ft5336_irq_handler,
			pdata->irq_gpio_trigger, client->name, data) >= 0) {
		pr_warn("Requested IRQ.\n");
	} else {
		dev_err(&client->dev, "Failed to request IRQ!\n");
	}
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN +
		FT5X06_SUSPEND_LEVEL;
	data->early_suspend.suspend = ft5x06_ts_early_suspend;
	data->early_suspend.resume = ft5x06_ts_late_resume;
	register_early_suspend(&data->early_suspend);
#endif
	return 0;
free_reset_gpio:
	if (gpio_is_valid(pdata->reset_gpio))
		gpio_free(pdata->reset_gpio);
free_irq_gpio:
	if (gpio_is_valid(pdata->irq_gpio))
		gpio_free(pdata->reset_gpio);
pwr_off:
	if (pdata->power_on)
		pdata->power_on(false);
	else
		ft5x06_power_on(data, false);
pwr_deinit:
	if (pdata->power_init)
		pdata->power_init(false);
	else
		ft5x06_power_init(data, false);
unreg_inputdev:
	input_unregister_device(input_dev);
	input_dev = NULL;
free_inputdev:
	input_free_device(input_dev);
free_mem:
	if (data->p_workqueue)
		destroy_workqueue(data->p_workqueue);
exit_create_workqueue_failed:
	kfree(data);
exit_alloc_data_failed:
exit_check_functionality_failed:
	kfree(pdata);
		dev_err(&client->dev, "ft5x06_ts_probe() failed\n");
	return err;
}
static int ft5x06_ts_remove(struct i2c_client *client)
{
	struct ft5x06_ts_data *data = i2c_get_clientdata(client);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&data->early_suspend);
#endif
	free_irq(client->irq, data);
#ifdef SYSFS_DEBUG
	ft5x0x_remove_sysfs(client);
#endif

#ifdef FTS_APK_DEBUG
	ft5x0x_release_apk_debug_channel();
#endif

	if (gpio_is_valid(data->pdata->reset_gpio)) {
		/*reset to 0, before power off*/
		gpio_set_value_cansleep(data->pdata->reset_gpio,
			data->pdata->reset_gpio_polarity);
		msleep(FT_T_VDR);
		gpio_free(data->pdata->reset_gpio);
	}
	if (gpio_is_valid(data->pdata->irq_gpio))
		gpio_free(data->pdata->irq_gpio);
	if (data->pdata->power_on)
		data->pdata->power_on(false);
	else
		ft5x06_power_on(data, false);
	if (data->pdata->power_init)
		data->pdata->power_init(false);
	else
		ft5x06_power_init(data, false);
	input_unregister_device(data->input_dev);
	kfree(data->pdata);
	kfree(data);
	return 0;
}

static const struct of_device_id ft5x06_of_match[] = {
	{.compatible = "focal,ft5336_ts",},
	{},
};

MODULE_DEVICE_TABLE(of, ft5x06_of_match);

static const struct i2c_device_id ft5x06_ts_id[] = {
	{FT5336_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ft5x06_ts_id);
static struct i2c_driver ft5x06_ts_driver = {
	.probe = ft5x06_ts_probe,
	.remove = ft5x06_ts_remove,
	.driver = {
		   .name = FT5336_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table =  ft5x06_of_match,
#ifdef CONFIG_PM
		   .pm = &ft5x06_ts_pm_ops,
#endif
		   },
	.id_table = ft5x06_ts_id,
};
static int __init ft5x06_ts_init(void)
{
	return i2c_add_driver(&ft5x06_ts_driver);
}
module_init(ft5x06_ts_init);
static void __exit ft5x06_ts_exit(void)
{
	i2c_del_driver(&ft5x06_ts_driver);
}
module_exit(ft5x06_ts_exit);
MODULE_AUTHOR("<tsai@broadcom.com>");
MODULE_DESCRIPTION("FocalTech ft5x06 TouchScreen driver");
MODULE_LICENSE("GPL v2");
