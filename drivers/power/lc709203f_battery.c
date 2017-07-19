/*
* Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
*
* This program is free software; you can redistribute it and/or modify it
* under the terms and conditions of the GNU General Public License,
* version 2, as published by the Free Software Foundation.
*
* This program is distributed in the hope it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <asm/unaligned.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/jiffies.h>

#include <linux/d2153/core.h>
#include <linux/d2153/lc709203f_battery.h>
#include <linux/d2153/d2153_reg.h>

#include <linux/bcm.h>

//#define LC709203_FULL_WORK

#ifdef custom_t_BATTERY
static void lc709203f_shutdown(struct i2c_client *client); 
#endif


#define LC709203F_SUPSEND
#define LC709203F_THERMISTOR_B		0x06
#define LC709203F_INITIAL_RSOC		0x07
#define LC709203F_TEMPERATURE		0x08
#define LC709203F_VOLTAGE		0x09
#define LC709203F_CURRENT_DIRECTION	0X0A
#define LC709203F_ADJUSTMENT_PACK_APPLI	0x0B
#define LC709203F_ADJUSTMENT_PACK_THERM	0x0C
#define LC709203F_RSOC			0x0D
#define LC709203F_INDICATOR_TO_EMPTY	0x0F
#define LC709203F_IC_VERSION		0x11
#define LC709203F_CHANGE_OF_THE_PARAM	0x12
#define LC709203F_ALARM_LOW_CELL_RSOC	0x13
#define LC709203F_ALARM_LOW_CELL_VOLT	0x14
#define LC709203F_IC_POWER_MODE		0x15
#define LC709203F_STATUS_BIT		0x16
#define LC709203F_NUM_OF_THE_PARAM	0x1A

#define LC709203F_DELAY			(50*HZ)
#define LC709203F_MAX_REGS		0x1A
struct lc709203f_chip *g_lc709203f_info;

struct lc709203f_reg {
	const char *name;
	u8 reg;
} lc709203f_regs[] = {
	{"THERMISTOR_B", LC709203F_THERMISTOR_B},
	{"INITIAL_RSOC", LC709203F_INITIAL_RSOC},
	{"TEMPERATURE", LC709203F_TEMPERATURE},
	{"VOLTAGE", LC709203F_VOLTAGE},
	{"CURRENT_DIRECTION", LC709203F_CURRENT_DIRECTION},
	{"ADJUSTMENT_PACK_APPLI", LC709203F_ADJUSTMENT_PACK_APPLI},
	{"ADJUSTMENT_PACK_THERMISTOR", LC709203F_ADJUSTMENT_PACK_THERM},
	{"RSOC", LC709203F_RSOC},
	{"INDICATOR_TO_EMPTY", LC709203F_INDICATOR_TO_EMPTY},
	{"IC_VERSION", LC709203F_IC_VERSION},
	{"CHANGE_OF_THE_PARAM", LC709203F_CHANGE_OF_THE_PARAM},
	{"ALARM_LOW_CELL_RSOC", LC709203F_ALARM_LOW_CELL_RSOC},
	{"ALARM_LOW_CELL_VOLT", LC709203F_ALARM_LOW_CELL_VOLT},
	{"IC_POWER_MODE", LC709203F_IC_POWER_MODE},
	{"STATUS_BIT", LC709203F_STATUS_BIT},
	{"NUM_OF_THE_PARAM", LC709203F_NUM_OF_THE_PARAM},
};


static int lc709203f_read_word(struct i2c_client *client, u8 reg)
{
	int ret;

	struct lc709203f_chip *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->mutex);
	if (chip && chip->shutdown_complete) {
		mutex_unlock(&chip->mutex);
		return -ENODEV;
	}
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev, "err reading reg addr 0x%x, %d\n",
				reg, ret);

	mutex_unlock(&chip->mutex);
	return ret;
}

static int lc709203f_write_word(struct i2c_client *client, u8 reg, u16 value)
{
	struct lc709203f_chip *chip = i2c_get_clientdata(client);
	int ret;
	mutex_lock(&chip->mutex);
	if (chip && chip->shutdown_complete) {
		mutex_unlock(&chip->mutex);
		return -ENODEV;
	}
	ret = i2c_smbus_write_word_data(client, reg, value);
	if (ret < 0)
		dev_err(&client->dev, "err writing 0x%0x, %d\n" , reg, ret);

	mutex_unlock(&chip->mutex);
	return ret;
}

static int battery_gauge_get_adjusted_soc(
	int min_soc, int max_soc, int actual_soc_semi)
{
	int ret;
	int inte;
	int min_soc_semi = min_soc * 1000;
	int max_soc_semi = max_soc * 1000;

	if (actual_soc_semi >= max_soc_semi)
		return 1000;
	if (actual_soc_semi <= min_soc_semi)
		return 0;
	ret = (actual_soc_semi - min_soc_semi) / (max_soc - min_soc);
	inte = ret/10;
	if (ret > (inte*10 + 4))
		ret = ret + 10;
	return ret;
}

static int lc709203f_get_private(void)
{
	if (g_lc709203f_info == NULL)
		return 0;
	else
		return  1;
}


static int lc709203f_get_fg_threshold_soc(void)
{
	if (g_lc709203f_info == NULL)
		return 0;
	else
		return  g_lc709203f_info->pdata->threshold_soc;
}

static int lc709203f_get_fg_max_soc(void)
{
	if (g_lc709203f_info == NULL)
		return 1000;
	else
		return g_lc709203f_info->pdata->maximum_soc;
}

static int lc709203f_get_fg_rechg_soc(void)
{
	if (g_lc709203f_info == NULL)
		return 1000;
	else
		return g_lc709203f_info->pdata->rechg_soc;
}

static int lc709203f_get_fg_ftchg_soc(void)
{
	if (g_lc709203f_info == NULL)
		return 1000;
	else
		return g_lc709203f_info->pdata->ftchg_soc;
}


static int lc709203f_get_fg_bkchg_soc(void)
{
	if (g_lc709203f_info == NULL)
		return 950;
	else
		return g_lc709203f_info->pdata->bkchg_soc;
}

static int lc709203f_set_fg_rechg_soc(int data)
{
	if (g_lc709203f_info == NULL)
		return -1;
	else
		g_lc709203f_info->pdata->maximum_soc = data;
	return 0;
}

static int lc709203f_get_vcell(void)
{
	int val;
	int vcell;
	vcell = 0;
	if (g_lc709203f_info == NULL) {
		pr_err("lc709203 chip is null\n");
		return 0;
	}
	val = lc709203f_read_word(g_lc709203f_info->client, LC709203F_VOLTAGE);
	if (val < 0)
		dev_err(&g_lc709203f_info->client->dev,
				"%s: err %d\n", __func__, val);
	else
		vcell = val;

#ifdef custom_t_BATTERY
	if(val < 0)
		vcell = 3500;// don't set to 0, otherwise the battery driver will shutdown the phone
	WARN_ON(val < 0);
#endif
	return vcell;
}

static int lc709203f_get_ite(void)
{
	int val;
	int i;
#ifdef custom_t_BATTERY
	int ret = 0;
#endif
	
	if (g_lc709203f_info == NULL)
		pr_err("lc709203 chip is null\n");
	for (i = 0; i < 3; i++) {
		val = lc709203f_read_word(g_lc709203f_info->client,
			LC709203F_INDICATOR_TO_EMPTY);
	if (val < 0)
		dev_err(&g_lc709203f_info->client->dev,
			"%s: err %d\n", __func__, val);
		else
			break;
	}

#ifdef custom_t_BATTERY
	ret = lc709203f_read_word(g_lc709203f_info->client,LC709203F_IC_POWER_MODE);
	pr_info("lc709203f] raw soc =%d pm =%d\n", val,ret);
#else
	pr_info("lc709203f] raw soc =%d\n", val);
#endif

	return val;

}

static int lc709203f_set_temp(int temp)
{
	int val;
	val = 0;
	if (g_lc709203f_info == NULL)
		return -1;
	val = i2c_smbus_write_word_data(g_lc709203f_info->client, 0x08,
		temp + 2732);
	return val;
}

static int lc709203f_get_soc(void)
{
	int val;
	int soc;
	int i;
	soc = 0;
	if (g_lc709203f_info == NULL)
		pr_err("lc709203 chip is null\n");
		
	soc = g_lc709203f_info->soc;
		
	for (i = 0; i < 3; i++) {
		val = lc709203f_read_word(g_lc709203f_info->client,
				LC709203F_INDICATOR_TO_EMPTY);
	if (val < 0)
		dev_err(&g_lc709203f_info->client->dev,
				"%s: err %d\n", __func__, val);
		else {
			soc = battery_gauge_get_adjusted_soc(
				g_lc709203f_info->pdata->threshold_soc,
				g_lc709203f_info->pdata->maximum_soc,
				val * 1000);
			g_lc709203f_info->soc = soc;
			break;
		}
	}
	
#ifdef custom_t_BATTERY
	//pr_info("lc709203f] soc =%d\n", soc);
	if(val < 0)
	{
		pr_warn("lc709203f] lc709203f_read_word return %d, we just fix it as 11 for software work around\n", val);	
		soc = 11;
	}
#endif

	return soc;
}

static void of_lc709203f_parse_platform_data(struct i2c_client *client,
				struct lc709203f_platform_data *pdata)
{
	char const *pstr;
	struct device_node *np = client->dev.of_node;
	u32 pval;
	int ret;

	ret = of_property_read_u32(np, "initial-rsoc", &pval);
	if (!ret)
		pdata->initial_rsoc = pval;
	else {
		dev_warn(&client->dev, "initial-rsoc not provided\n");
		pdata->initial_rsoc = 0xAA55;
	}

	ret = of_property_read_u32(np, "appli-adjustment", &pval);
	if (!ret)
		pdata->appli_adjustment = pval;
	else
		dev_warn(&client->dev, "appli-adjustment not found\n");

	ret = of_property_read_u32(np, "change-of-param", &pval);
	if (!ret)
		pdata->change_of_param = pval;
	else {
		dev_warn(&client->dev, "change of param not found\n");
		pdata->change_of_param = 0;
	}

	pdata->tz_name = NULL;
	ret = of_property_read_string(np, "tz-name", &pstr);
	if (!ret)
		pdata->tz_name = pstr;

	ret = of_property_read_u32(np, "thermistor-beta", &pval);
	if (!ret)
		pdata->thermistor_beta = pval;
	else {
		if (!pdata->tz_name)
			dev_warn(&client->dev,
				"Thermistor beta not provided\n");
	}

	ret = of_property_read_u32(np, "thermistor-adjustment", &pval);
	if (!ret)
		pdata->therm_adjustment = pval;

	ret = of_property_read_u32(np, "kernel-threshold-soc", &pval);
	if (!ret)
		pdata->threshold_soc = pval;
	else
		dev_warn(&client->dev, "kernel-threshold-soc not found\n");

	ret = of_property_read_u32(np, "kernel-maximum-soc", &pval);
	if (!ret)
		pdata->maximum_soc = pval;
	else {
		pdata->maximum_soc = 970;
		dev_warn(&client->dev, "kernel-maximum-soc not found\n");
	}
	ret = of_property_read_u32(np, "kernel-1stcharge-soc", &pval);
	if (!ret)
		pdata->ftchg_soc = pval;
	else {
		pdata->ftchg_soc = 999;
		dev_warn(&client->dev, "kernel-1stcharge-soc not found\n");
	}

	ret = of_property_read_u32(np, "kernel-recharge-soc", &pval);
	if (!ret)
		pdata->rechg_soc = pval;
	else {
		pdata->rechg_soc = 999;
		dev_warn(&client->dev, "kernel-recharge-soc not found\n");
	}
	ret = of_property_read_u32(np, "kernel-bakcharge-soc", &pval);
	if (!ret)
		pdata->bkchg_soc = pval;
	else {
		pdata->bkchg_soc = 999;
		dev_warn(&client->dev, "kernel-backcharge-soc not found\n");
	}
	pr_info("[lc709203] board config: apa[0x%x], soc[%d, %d, %d, %d]",
		pdata->appli_adjustment,  pdata->threshold_soc,
		pdata->maximum_soc, pdata->rechg_soc, pdata->bkchg_soc);
}

#ifdef CONFIG_DEBUG_FS

#include <linux/debugfs.h>
#include <linux/seq_file.h>

static struct dentry *debugfs_root;
static u8 valid_command[] = {0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xD, 0xF,
			0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x1A};
static int dbg_lc709203f_show(struct seq_file *s, void *data)
{
	struct i2c_client *client = s->private;
	int ret;
	int i;

	seq_puts(s, "Register-->Value(16bit)\n");
	for (i = 0; i < ARRAY_SIZE(valid_command); ++i) {
		ret = lc709203f_read_word(client, valid_command[i]);
		if (ret < 0)
			seq_printf(s, "0x%02x: ERROR\n", valid_command[i]);
		else
			seq_printf(s, "0x%02x: 0x%04x\n",
						valid_command[i], ret);
	}
	return 0;
}

static int dbg_lc709203f_open(struct inode *inode, struct file *file)
{
	return single_open(file, dbg_lc709203f_show, inode->i_private);
}

static const struct file_operations lc709203f_debug_fops = {
	.open		= dbg_lc709203f_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int lc709203f_debugfs_init(struct i2c_client *client)
{
	debugfs_root = debugfs_create_dir("lc709203f", NULL);
	if (!debugfs_root)
		pr_warn("lc709203f: Failed to create debugfs directory\n");

	(void) debugfs_create_file("registers", S_IRUGO,
			debugfs_root, (void *)client, &lc709203f_debug_fops);
	return 0;
}
#else
static int lc709203f_debugfs_init(struct i2c_client *client)
{
	return 0;
}
#endif

#ifdef custom_t_DEVINFO
static ssize_t device_name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	int val =0;

	if (g_lc709203f_info == NULL)
	{
		pr_err("lc709203 chip is null\n");
		sprintf(buf, "%s\n", "DA9066");
	}
	else
	{
		val = lc709203f_read_word(g_lc709203f_info->client,LC709203F_IC_VERSION);
		sprintf(buf, "%s(%x)\n", "LC709203F",val);
	}	


	
	ret = strlen(buf)+1 ;
	return ret;
}


static struct kobject *android_fg_kobj;
static DEVICE_ATTR(device_name, S_IRUGO, device_name_show, NULL);

#endif

#define PROBE_TRIES 50
static int lc709203f_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct lc709203f_chip *chip;
	int ret;
	int i;
	int jump;
	ret = -ENODEV;
	jump = 0;
	/* Required PEC functionality */
	client->flags = client->flags | I2C_CLIENT_PEC;
	/* Check if device exist or not */

#ifdef custom_t_DEVINFO
		android_fg_kobj = kobject_create_and_add("fuelgauge", NULL) ;
		if (android_fg_kobj == NULL) {
			printk(KERN_ERR "%s: subsystem_register failed\n", __func__);
			ret = -ENOMEM;
			return ret;
		}
		ret = sysfs_create_file(android_fg_kobj, &dev_attr_device_name.attr);
		if (ret) {
			printk(KERN_ERR "%s: sysfs_create_file failed\n", __func__);
			return ret;
		}
#endif

	for (i = 0; i < PROBE_TRIES; i++) {
		ret = i2c_smbus_read_word_data(client,
					LC709203F_NUM_OF_THE_PARAM);
		if (ret < 0 || i == PROBE_TRIES-1) {
			dev_err(&client->dev,
				"device is not responding, %d\n", ret);
			usleep_range(5000, 25000);
		} else
			break;
	}

	if (ret < 0) {
		g_lc709203f_info = NULL;
		dev_err(&client->dev,
			"device doesnot exist, %d\n", ret);

		return -ENODEV;
	}
	dev_info(&client->dev, "Device Params 0x%04x\n", ret);

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	g_lc709203f_info = chip;
	chip->client = client;
	if (client->dev.of_node) {
		chip->pdata = kzalloc(sizeof(*chip->pdata), GFP_KERNEL);
		if (!chip->pdata) {
			kfree(chip);
			return -ENOMEM;
		}
		dev_info(&client->dev, "parse dts\n");
		of_lc709203f_parse_platform_data(client, chip->pdata);
	} else
		chip->pdata = client->dev.platform_data;

	if (!chip->pdata) {
		pr_info("[lc709203f]pdata is null\n");
		chip->pdata = kzalloc(sizeof(*chip->pdata), GFP_KERNEL);
		chip->pdata->initial_rsoc = 0xAA55;
		chip->pdata->appli_adjustment = 0x0024;
		chip->pdata->change_of_param = 1;
		chip->pdata->thermistor_beta = 0x0D34;
		chip->pdata->therm_adjustment = 0x001e;
		chip->pdata->threshold_soc = 10;
		chip->pdata->maximum_soc = 970;
		chip->pdata->rechg_soc = 945;
		chip->pdata->bkchg_soc = 980;
	}

	mutex_init(&chip->mutex);
	chip->shutdown_complete = 0;
	i2c_set_clientdata(client, chip);
	dev_info(&client->dev, "initial-rsoc: 0x%04x\n",
			chip->pdata->initial_rsoc);

	#if defined (LC709203_FULL_WORK)
	ret = i2c_smbus_read_word_data(client, LC709203F_ADJUSTMENT_PACK_APPLI);
	if (ret < 0)
		dev_err(&client->dev,
			"1st cannot read apa, ret:%d\n", ret);
	else if (ret == chip->pdata->appli_adjustment) {
		dev_info(&client->dev, "jump chip init");
		jump = 1;
		goto jump_chip_init;
	}
	#endif  /* LC709203_FULL_WORK */

	ret =  i2c_smbus_read_word_data(client, LC709203F_IC_VERSION);
	if (ret < 0)
		dev_err(&client->dev,
			"1st cannot read chip version, ret:%d\n", ret);
	pr_info("[lc709203] chip version val=[0x%x]\n",  ret);
	ret = lc709203f_write_word(chip->client,
				LC709203F_IC_POWER_MODE, 0x01);
	if (ret < 0)
		dev_err(&client->dev,
			"1st cannot write addr:0x15, ret:%d\n", ret);
	ret = lc709203f_write_word(client,
			LC709203F_IC_POWER_MODE, 0x01);
	if (ret < 0)
		dev_err(&client->dev,
			"2nd cannot write addr:0x15, ret:%d\n", ret);
	for (i = 0; i < PROBE_TRIES; i++) {
		ret = lc709203f_write_word(chip->client,
			LC709203F_ADJUSTMENT_PACK_APPLI,
			chip->pdata->appli_adjustment);
		if (ret < 0 && i == PROBE_TRIES-1) {
			dev_err(&client->dev,
				"ADJUSTMENT_APPLI write failed: %d\n", ret);
			return ret;
		} else
			break;
	}
	ret = lc709203f_write_word(chip->client,
			LC709203F_CHANGE_OF_THE_PARAM,
			chip->pdata->change_of_param);
	if (ret < 0)
		dev_err(&client->dev,
			"cannot write addr:0x12, ret:%d\n", ret);
	ret = lc709203f_write_word(chip->client, LC709203F_STATUS_BIT, 0);
	if (ret < 0)
		dev_err(&client->dev,
			"cannot write addr:0x16, ret:%d\n", ret);
#if defined (LC709203_FULL_WORK)
jump_chip_init:
#endif  /* LC709203_FULL_WORK */

#ifdef custom_t_BATTERY
	dev_info(&client->dev,"base vcell is %d",lc709203f_get_vcell());	

    #if defined (LC709203_FULL_WORK)
    if (0)
    #else
    if (1)  /* Remove LC709203F */
    #endif  /* LC709203_FULL_WORK */
    {
        dev_err(&client->dev,"Remove LC709203F support.\n");
		g_lc709203f_info = NULL;
		lc709203f_shutdown(client);
		return -ENODEV;
    }
    
	if(0x2afe == i2c_smbus_read_word_data(client, LC709203F_IC_VERSION))
	{		
		
		dev_err(&client->dev,"0x2afe is old chip , we just skip it.\n");
		g_lc709203f_info = NULL;
		lc709203f_shutdown(client);
		return -ENODEV;
	}
#endif
	
	bcm_agent_register(BCM_AGENT_GET_RAW_SOC,
		(void *)lc709203f_get_ite, "lc709203f-fg");
	bcm_agent_register(BCM_AGENT_GET_SOC,
		(void *)lc709203f_get_soc, "lc709203f-fg");
	bcm_agent_register(BCM_AGENT_GET_FG_PRIVATE,
		(void *)lc709203f_get_private, "lc709203f-fg");
	bcm_agent_register(BCM_AGENT_GET_VCELL,
		(void *)lc709203f_get_vcell, "lc709203f-fg");
	bcm_agent_register(BCM_AGENT_SET_FG_TEMP,
		(void *)lc709203f_set_temp, "lc709203f-fg");
	bcm_agent_register(BCM_AGENT_GET_FG_THRESHOLD_SOC,
		(void *)lc709203f_get_fg_threshold_soc, "lc709203f-fg");
	bcm_agent_register(BCM_AGENT_GET_FG_MAX_SOC,
		(void *)lc709203f_get_fg_max_soc, "lc709203f-fg");
	bcm_agent_register(BCM_AGENT_GET_FG_RECHG_SOC,
		(void *)lc709203f_get_fg_rechg_soc, "lc709203f-fg");
	bcm_agent_register(BCM_AGENT_GET_FG_FTCHG_SOC,
		(void *)lc709203f_get_fg_ftchg_soc, "lc709203f-fg");
	bcm_agent_register(BCM_AGENT_GET_FG_BKCHG_SOC,
		(void *)lc709203f_get_fg_bkchg_soc, "lc709203f-fg");
	bcm_agent_register(BCM_AGENT_SET_FG_RECHG_SOC,
		(void *)lc709203f_set_fg_rechg_soc, "lc709203f-fg");


	lc709203f_debugfs_init(client);
	return 0;
}

static int lc709203f_remove(struct i2c_client *client)
{
	struct lc709203f_chip *chip = i2c_get_clientdata(client);
	kfree(chip->pdata);
	kfree(chip);
	mutex_destroy(&chip->mutex);

	return 0;
}

static void lc709203f_shutdown(struct i2c_client *client)
{
	struct lc709203f_chip *chip = i2c_get_clientdata(client);
	#if !defined (LC709203_FULL_WORK)
	int ret;
	int i;
#ifndef custom_t_BATTERY
	chip->shutdown_complete = 1;
#endif
	for (i = 0; i < 5; i++) {
		ret = lc709203f_write_word(chip->client,
			LC709203F_IC_POWER_MODE, 0x02);
		if (ret < 0)
			pr_err("lc709203 cannot wirte power mode %d\n", ret);
		else
			break;
	}
	#endif  /* LC709203_FULL_WORK */
#ifdef custom_t_BATTERY
//custom_t:CJ lc709203f_write_word will fail if shutdown_complete == 1
	chip->shutdown_complete = 1;
#endif
}

static const struct i2c_device_id lc709203f_id[] = {
	{ "lc709203f", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lc709203f_id);

#ifdef LC709203F_SUPSEND
static int lc709203_battery_suspend_pre(struct device *dev)
{
	int ret = 0;
	#if !defined (LC709203_FULL_WORK)
	int i;
	pr_info("lc709203_battery_suspend_pre+++\n");
	for (i = 0; i < 5; i++) {
		ret = lc709203f_write_word(g_lc709203f_info->client,
			LC709203F_IC_POWER_MODE, 0x02);
		if (ret < 0)
			pr_err("lc709203 cannot wirte power mode %d\n", ret);
		else
			break;
	}
	#endif  /* LC709203_FULL_WORK */
	return ret;
}

static int lc709203_battery_complete(struct device *dev)
{
	int ret = 0;
	#if !defined (LC709203_FULL_WORK)
	int i;
	pr_info("lc709203_battery_complete+++\n");
	for (i = 0; i < 6; i++) {
		ret = lc709203f_write_word(g_lc709203f_info->client,
			LC709203F_IC_POWER_MODE, 0x01);
		if (ret < 0)
			pr_err("lc709203f cannot write power mode ret=%d\n",
					ret);
		else
			break;
	}
	#endif  /* LC709203_FULL_WORK */

	return ret;
}

static SIMPLE_DEV_PM_OPS(lc709203_pm, lc709203_battery_suspend_pre, lc709203_battery_complete);
#endif

static struct i2c_driver lc709203f_i2c_driver = {
	.driver	= {
		.name	= "lc709203f",
#ifdef LC709203F_SUPSEND
		.pm = &lc709203_pm,
#endif
	},
	.probe		= lc709203f_probe,
	.remove		= lc709203f_remove,
	.id_table	= lc709203f_id,
	.shutdown	= lc709203f_shutdown,
};
/*module_i2c_driver(lc709203f_i2c_driver);*/
static int __init lc709203f_init(void)
{
        return i2c_add_driver(&lc709203f_i2c_driver);
}

static void __exit lc709203f_exit(void)
{
        i2c_del_driver(&lc709203f_i2c_driver);
}

device_initcall(lc709203f_init);
module_exit(lc709203f_exit);

MODULE_AUTHOR("Chaitanya Bandi <bandik@nvidia.com>");
MODULE_DESCRIPTION("OnSemi LC709203F Fuel Gauge");
MODULE_LICENSE("GPL v2");
