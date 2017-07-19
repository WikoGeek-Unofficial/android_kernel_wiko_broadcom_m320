/*****************************************************************************
 *  Copyright 2001 - 2014 Broadcom Corporation.  All rights reserved.
 *
 *  HWMON Driver for Dialog D2135
 *
 *  Unless you and Broadcom execute a separate written software license
 *  agreement governing use of this software, this software is licensed to you
 *  under the terms of the GNU General Public License version 2, available at
 *  http://www.gnu.org/licenses/old-license/gpl-2.0.html (the "GPL").
 *
 *  Notwithstanding the above, under no circumstances may you combine this
 *  software in any way with any other Broadcom software provided under a
 *  license other than the GPL, without Broadcom's express prior written
 *  consent.
 *
 *****************************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/d2153/core.h>
#include <linux/d2153/hwmon.h>
#include <linux/hwmon-sysfs.h>
#ifdef CONFIG_D2153_DEBUG_FEATURE
#include <linux/debugfs.h>
#endif
#include <linux/d2153/d2153_reg.h>
#include <linux/d2153/d2153_battery.h>
#include <linux/d2153/core.h>

static int debug_mask = D2153_PRINT_ERROR | D2153_PRINT_INIT |
			D2153_PRINT_WARNING | D2153_PRINT_FLOW;
#define pr_hwmon(debug_level, args...) \
	do { \
		if (debug_mask & D2153_PRINT_##debug_level) { \
			pr_info("[D2153-hwmon]:"args); \
		} \
	} while (0)

#define FALSE							(0)
#define TRUE							(1)

/*
 * Name : d2153_read_adc_in_auto
 * Desc : Read ADC raw data for each channel.
 * Param :
 * - d2153 :
 * - channel : voltage, temperature 1, temperature 2, VF and TJUNC*
 */
int d2153_read_adc_in_auto(struct d2153_battery *bdata,
				adc_channel channel, int curr_src)
{
	struct adc_cont_in_auto *adc_cont_inven =
			bdata->pd2153->pdata->hwmon_pdata->adc_cont_inven;
	u8 msb_res, lsb_res;
	u8 reg = 0;
	int ret = 0;
#ifdef custom_t_BATTERY_CJ
	int times = 1;
	int avg_adc = 0;
	int read_adc = 0;
#endif
	if (bdata == NULL) {
		pr_hwmon(ERROR, "%s. Invalid argument\n", __func__);
		return -EINVAL;
	}

	/* The valid channel is from ADC_VOLTAGE to ADC_AIN in auto mode.*/
	if (channel >= D2153_ADC_CHANNEL_MAX - 1) {
		pr_hwmon(ERROR,
			"%s. Invalid channel(%d) in auto mode\n",
			__func__, channel);
		return -EINVAL;
	}

	mutex_lock(&bdata->meoc_lock);

	ret = d2153_get_adc_hwsem();
	if (ret < 0) {
		mutex_unlock(&bdata->meoc_lock);
		pr_hwmon(ERROR, "%s:lock is already taken.\n", __func__);
		return -EBUSY;
	}

	bdata->adc_res[channel].is_adc_eoc = FALSE;
	bdata->adc_res[channel].read_adc = 0;

	if (curr_src == D2153_ADC_ISRC_50UA) {
		/* Enable 50uA current source */
		ret = d2153_reg_read(bdata->pd2153, D2153_ADC_MAN_REG, &reg);
		reg |= D2153_ISRC_50U_MASK;
		d2153_reg_write(bdata->pd2153, D2153_ADC_MAN_REG, reg);
	} else {
		/* Enable 10uA current source */
		ret = d2153_reg_read(bdata->pd2153, D2153_ADC_MAN_REG, &reg);
		reg &= ~D2153_ISRC_50U_MASK;
		d2153_reg_write(bdata->pd2153, D2153_ADC_MAN_REG, reg);
	}
	/* Set ADC_CONT register to select a channel*/
	if (adc_cont_inven[channel].adc_preset_val) {
		if (channel == D2153_ADC_AIN) {
			ret = d2153_reg_write(bdata->pd2153,
				D2153_ADC_CONT2_REG,
				adc_cont_inven[channel].adc_preset_val);
		} else {
			ret = d2153_reg_write(bdata->pd2153,
				D2153_ADC_CONT_REG,
				adc_cont_inven[channel].adc_preset_val);
		}
		usleep_range(1000, 1100);
		ret |= d2153_set_bits(bdata->pd2153,
			D2153_ADC_CONT_REG,
			adc_cont_inven[channel].adc_cont_val);
		if (ret < 0)
			goto out;
	} else {
		ret = d2153_reg_write(bdata->pd2153,
			D2153_ADC_CONT_REG,
			adc_cont_inven[channel].adc_cont_val);
		if (ret < 0)
			goto out;
	}

#ifdef custom_t_BATTERY_CJ
	if(channel == D2153_ADC_VOLTAGE)
		times = 10;
	
	for(;times > 0;times--)
	{
#endif
	
	if (bdata->pd2153->pdata->pbat_platform->adc_config_mode ==
								D2153_ADC_ECO)
		msleep(30);
	else if (bdata->pd2153->pdata->pbat_platform->adc_config_mode ==
								D2153_ADC_FAST)
		usleep_range(5000, 5100);

	/* Read result register for requested adc channel*/
	ret = d2153_reg_read(bdata->pd2153,
			adc_cont_inven[channel].adc_msb_res,
			&msb_res);
	ret |= d2153_reg_read(bdata->pd2153,
			adc_cont_inven[channel].adc_lsb_res,
			&lsb_res);
	lsb_res &= adc_cont_inven[channel].adc_lsb_mask;

		
#ifdef custom_t_BATTERY_CJ
	read_adc = ((msb_res << 4) |
		(lsb_res >> (adc_cont_inven[channel].adc_lsb_mask ==
						ADC_RES_MASK_MSB ? 4 : 0)));

	if(channel == D2153_ADC_VOLTAGE)
		pr_hwmon(DATA,"%s. channel %d times %d read_adc %d\n",__func__,channel,times,read_adc);
	
	if(avg_adc != 0)
		avg_adc = (avg_adc + read_adc) / 2;
	else
		avg_adc = read_adc;
	
	}

	if(channel == D2153_ADC_VOLTAGE)
		pr_hwmon(DATA,"%s. channel %d avg_adc %d\n",__func__,channel,avg_adc);

#endif
	
	ret = d2153_reg_write(bdata->pd2153, D2153_ADC_CONT_REG, 0x00);
	if (ret < 0)
		goto out;

	/* Make ADC result*/
	bdata->adc_res[channel].is_adc_eoc = TRUE;
#ifdef custom_t_BATTERY_CJ
	bdata->adc_res[channel].read_adc =	avg_adc;
#else
	bdata->adc_res[channel].read_adc = ((msb_res << 4) |
		(lsb_res >> (adc_cont_inven[channel].adc_lsb_mask ==
						ADC_RES_MASK_MSB ? 4 : 0)));
#endif

out:
	/* Default Setting - Enable 10uA current source */
	ret = d2153_reg_read(bdata->pd2153, D2153_ADC_MAN_REG, &reg);
	reg &= ~D2153_ISRC_50U_MASK;
	d2153_reg_write(bdata->pd2153, D2153_ADC_MAN_REG, reg);

	d2153_put_adc_hwsem();
	mutex_unlock(&bdata->meoc_lock);

	return ret;
}

/*
 * Name : d2153_read_adc_in_manual
 */
int d2153_read_adc_in_manual(struct d2153_battery *bdata,
				adc_channel channel, int curr_src)
{
	struct adc_cont_in_auto *adc_cont_inven =
			bdata->pd2153->pdata->hwmon_pdata->adc_cont_inven;
	struct d2153 *d2153 = bdata->pd2153;
	struct d2153_hwmon *adc = &d2153->hwmon;

	u8 reg = 0, read_msb, read_lsb;
	int ret;

	if (channel == D2153_ADC_AIN) {
		pr_hwmon(WARNING,
			"%s:ADC_AIN channel is not supoorted in manual mode\n",
								__func__);
		return 0;
	}

	mutex_lock(&bdata->meoc_lock);
	ret = d2153_get_adc_hwsem();
	if (ret < 0) {
		mutex_unlock(&bdata->meoc_lock);
		pr_hwmon(ERROR, "%s:lock is already taken\n", __func__);
		return -EBUSY;
	}

	if (curr_src == D2153_ADC_ISRC_50UA) {
		/* Enable 50uA current source */
		ret = d2153_reg_read(bdata->pd2153, D2153_ADC_MAN_REG, &reg);
		reg |= D2153_ISRC_50U_MASK;
		d2153_reg_write(bdata->pd2153, D2153_ADC_MAN_REG, reg);
	} else {
		/* Enable 10uA current source */
		ret = d2153_reg_read(bdata->pd2153, D2153_ADC_MAN_REG, &reg);
		reg &= ~D2153_ISRC_50U_MASK;
		d2153_reg_write(bdata->pd2153, D2153_ADC_MAN_REG, 0x00);
	}


	if (adc_cont_inven[channel].adc_preset_val) {
		if (channel == D2153_ADC_AIN) {
			ret = d2153_reg_write(bdata->pd2153,
				D2153_ADC_CONT2_REG,
				adc_cont_inven[channel].adc_preset_val);
		} else {
			ret = d2153_reg_write(bdata->pd2153,
				D2153_ADC_CONT_REG,
				adc_cont_inven[channel].adc_preset_val);
		}
		if (ret < 0)
			goto out;
		usleep_range(5000, 5100);
	}
	reg = adc_cont_inven[channel].adc_cont_val;
	reg &= D2153_ADC_MODE_MASK;
	ret |= d2153_set_bits(bdata->pd2153, D2153_ADC_CONT_REG, reg);

	init_completion(&adc->man_adc_complete);

	bdata->adc_res[channel].is_adc_eoc = FALSE;
	bdata->adc_res[channel].read_adc = 0;

	ret = d2153_reg_read(bdata->pd2153, D2153_ADC_MAN_REG, &reg);
	reg &= D2153_ISRC_50U_MASK;
	if (ret < 0)
		goto out;
	switch (channel) {
	case D2153_ADC_VOLTAGE:
		reg |= D2153_MUXSEL_VBAT;
		break;
	case D2153_ADC_TEMPERATURE_1:
		reg |= D2153_MUXSEL_TEMP1;
		break;
	case D2153_ADC_TEMPERATURE_2:
		reg |= D2153_MUXSEL_TEMP2;
		break;
	case D2153_ADC_VF:
		reg |= D2153_MUXSEL_VF;
		break;
	case D2153_ADC_TJUNC:
		reg |= D2153_MUXSEL_TJUNC;
		break;
	default:
		pr_hwmon(ERROR,	"%s. Invalid channel(%d)\n", __func__, channel);
		ret = -EINVAL;
		goto out;
	}
	reg |= D2153_MAN_CONV_MASK;
	ret = d2153_reg_write(bdata->pd2153, D2153_ADC_MAN_REG, reg);
	if (ret < 0)
		goto out;

	wait_for_completion(&adc->man_adc_complete);

	ret = d2153_reg_read(d2153, D2153_ADC_RES_H_REG, &read_msb);
	ret |= d2153_reg_read(d2153, D2153_ADC_RES_L_REG, &read_lsb);
	if (ret < 0)
		goto out;
	bdata->adc_res[channel].is_adc_eoc = TRUE;
	bdata->adc_res[channel].read_adc =
		((read_msb << 4) | (read_lsb & D2153_ADC_RES_LSB_MASK));

out:
	/* Default Setting - Enable 10uA current source */
	ret = d2153_reg_read(bdata->pd2153, D2153_ADC_MAN_REG, &reg);
	reg &= ~D2153_ISRC_50U_MASK;
	d2153_reg_write(bdata->pd2153, D2153_ADC_MAN_REG, reg);

	d2153_put_adc_hwsem();
	mutex_unlock(&bdata->meoc_lock);

	if (bdata->adc_res[channel].is_adc_eoc == FALSE) {
		pr_hwmon(WARNING,
			"%s. Failed manual ADC conversion. channel(%d)\n",
							__func__, channel);
		ret = -EIO;
	}

	return ret;
}

/*
 * Name : d2153_set_adc_mode
 */
int d2153_set_adc_mode(struct d2153_battery * const bdata, adc_mode const type,
					enum d2153_adc_config_mode const mode)
{
	struct d2153_hwmon_platform_data *pdata =
			bdata->pd2153->pdata->hwmon_pdata;
	int ret, chnl;

	pr_hwmon(FLOW, "%s called\n", __func__);
	if (unlikely(!bdata)) {
		pr_hwmon(ERROR, "%s. Invalid parameter.\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&bdata->meoc_lock);
	ret = d2153_get_adc_hwsem();
	if (ret < 0) {
		mutex_unlock(&bdata->meoc_lock);
		pr_hwmon(ERROR, "%s:lock is already taken\n", __func__);
		return -EBUSY;
	}

	for (chnl = 0; chnl < pdata->adc_cont_inven_len; chnl++) {
		if (mode == D2153_ADC_FAST)
			pdata->adc_cont_inven[chnl].adc_cont_val |=
						D2153_ADC_MODE_MASK;
		else if (mode == D2153_ADC_ECO)
			pdata->adc_cont_inven[chnl].adc_cont_val &=
						~D2153_ADC_MODE_MASK;
	}

	if (bdata->adc_mode != type) {
		if (type == D2153_ADC_IN_AUTO) {
			bdata->d2153_read_adc = d2153_read_adc_in_auto;
			bdata->adc_mode = D2153_ADC_IN_AUTO;
		} else if (type == D2153_ADC_IN_MANUAL) {
			bdata->d2153_read_adc = d2153_read_adc_in_manual;
			bdata->adc_mode = D2153_ADC_IN_MANUAL;
		}
	} else {
		pr_hwmon(FLOW,
			"%s: ADC mode is same before was set\n", __func__);
	}

	d2153_put_adc_hwsem();
	mutex_unlock(&bdata->meoc_lock);
	return 0;
}
EXPORT_SYMBOL(d2153_set_adc_mode);

/*
 * Name : d2153_battery_adceom_handler
 */
static irqreturn_t d2153_battery_adceom_handler(int irq, void *data)
{
	struct d2153 *d2153 = (struct d2153 *)data;
	struct d2153_hwmon *adc = &d2153->hwmon;

	complete(&adc->man_adc_complete);
	return IRQ_HANDLED;
}



static int d2153_adc_remove(struct platform_device *pdev)
{
	struct d2153 *d2153 = platform_get_drvdata(pdev);
	/* Free IRQ*/
#ifdef D2153_REG_EOM_IRQ
	d2153_free_irq(d2153, D2153_IRQ_EADCEOM);
#endif /* D2153_REG_EOM_IRQ */
	hwmon_device_unregister(&pdev->dev);
	return 0;
}

#ifdef CONFIG_D2153_DEBUG_FEATURE
static void d2153_hwmon_debugfs_init(struct d2153 *d2153)
{
	struct dentry *dentry_d2153_hwmon_dir;

	dentry_d2153_hwmon_dir = debugfs_create_dir("d2153-hwmon",
							d2153->dent_d2153);
	if (!dentry_d2153_hwmon_dir)
		goto debugfs_clean;

	if (!debugfs_create_u32("debug_mask", S_IWUSR | S_IRUSR,
					dentry_d2153_hwmon_dir, &debug_mask))
		goto debugfs_clean;

	return;

debugfs_clean:
	debugfs_remove_recursive(dentry_d2153_hwmon_dir);

}
#endif

static int d2153_adc_probe(struct platform_device *pdev)
{
	struct d2153 *d2153 = platform_get_drvdata(pdev);
	struct d2153_hwmon *adc = &d2153->hwmon;
	int ret = 0;

	pr_hwmon(INIT, "%s: called\n", __func__);
	adc->classdev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(adc->classdev)) {
		ret = PTR_ERR(adc->classdev);
		pr_hwmon(ERROR, "Class registration failed (%d)\n", ret);
	}
	init_completion(&adc->man_adc_complete);
#ifdef D2153_REG_EOM_IRQ
	pr_hwmon(FLOW, "%s. ADCEOM IRQ register\n", __func__);
	ret = d2153_register_irq(d2153, D2153_IRQ_EADCEOM,
		d2153_battery_adceom_handler, 0, "d2153-eom", d2153);
	if (ret < 0) {
		pr_hwmon(ERROR, "%s. ADCEOM IRQ register failed\n", __func__);
		goto error;
	}
#endif /* D2153_REG_EOM_IRQ */
	d2153_hwmon_debugfs_init(d2153);
	return 0;
error:
	hwmon_device_unregister(&pdev->dev);
	return 0;
}

static struct platform_driver d2153_adc_driver = {
	.driver = {
		.name = "d2153-hwmon",
		.owner = THIS_MODULE,
	},
	.probe = d2153_adc_probe,
	.remove = d2153_adc_remove,
};

static int __init d2153_adc_init(void)
{
	pr_hwmon(INIT, "%s: called\n", __func__);
	return platform_driver_register(&d2153_adc_driver);
}
subsys_initcall(d2153_adc_init);

static void __exit d2153_adc_exit(void)
{
	platform_driver_unregister(&d2153_adc_driver);
}
module_exit(d2153_adc_exit);

MODULE_DESCRIPTION("D2153 ADC Driver");
MODULE_LICENSE("GPL");
