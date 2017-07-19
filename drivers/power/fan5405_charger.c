/*
*  FAN5405-charger.c
*  FAN5405 charger interface driver
*
*  Copyright (C) 2013 Broadcom Mobile
*
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/regulator/machine.h>
#include <linux/spa_power.h>
#include <linux/spa_agent.h>
#include <linux/wakelock.h>
#include <asm/uaccess.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif
#include <linux/fan5405.h>

#include <linux/bcm.h>
#if defined(CONFIG_BATTERY_D2153) || defined(CONFIG_BATTERY_D2153_EXT)
#include <linux/d2153/d2153_battery.h>

#ifdef CONFIG_D2153_EOC_CTRL
#define NO_USE_TERMINATION_CURRENT
#endif
#endif

/* t32 timer overflows at 32 seconds, reset it before that time */
#define FAN5405_TIMER_RESET_PERIOD	(5000)

/* STAT bits = 01 refers to charging status */
#define STAT_CHARGING			0x1

#define INPUT_STR_LEN                   100
#define OUTPUT_STR_LEN                  300
#define SENSE_R_DEFAULT                 68
static int debug_mask = BCM_PRINT_ERROR | BCM_PRINT_INIT | BCM_PRINT_FLOW;

#define pr_fan(debug_level, args...) \
	do { \
		if (debug_mask & BCM_PRINT_##debug_level) { \
			pr_info("[FAN]:"args); \
		} \
	} while (0)

enum {
	BAT_NOT_DETECTED,
	BAT_DETECTED
};
		
#ifdef custom_t_BATTERY
static unsigned int charge_current = 500;
#endif

static u8 charge_mode;

static const struct of_device_id fan5405_of_match[] = {
	{.compatible = "bcm,fan5405",},
	{},
};

struct fan5405_chip {
	struct i2c_client		*client;
	struct wake_lock    i2c_lock;
	struct mutex    i2c_mutex_lock;
	struct work_struct      intr_work;
	struct delayed_work	timer_work;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dent_fan5405;
#endif
	struct fan5405_platform_data	*pdata;
	int fault_count;
	int float_volt;
	bool chrgr_connect;
	bool reg_saved;
};

static struct fan5405_chip *fan_charger;

/* A name value pair */
struct dict_atom {
	char * const name;
	unsigned long value;
};

static struct fan5405_reg fan5405_reg_backup[] = {
		{fan5405_CONTROL1,	0x40},
		{fan5405_OREG,		0x8E},
		{fan5405_IBAT,		0x09},
		{fan5405_SP_CHARGER,	0x04},
};

static int reg_backup_tb_len = ARRAY_SIZE(fan5405_reg_backup);

static int fan5405_write_reg(struct i2c_client *client, int reg, u8 value)
{
	int ret;
	wake_lock(&fan_charger->i2c_lock);
	mutex_lock(&fan_charger->i2c_mutex_lock);
	ret = i2c_smbus_write_byte_data(client, reg, value);
	mutex_unlock(&fan_charger->i2c_mutex_lock);
	wake_unlock(&fan_charger->i2c_lock);
	pr_fan(VERBOSE, "%s : REG(0x%x) = 0x%x\n", __func__, reg, value);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	return ret;
}

static int fan5405_read_reg(struct i2c_client *client, int reg)
{
	int ret;
	wake_lock(&fan_charger->i2c_lock);
	mutex_lock(&fan_charger->i2c_mutex_lock);
	ret = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&fan_charger->i2c_mutex_lock);
	wake_unlock(&fan_charger->i2c_lock);
	pr_fan(VERBOSE, "%s : REG(0x%x) = 0x%x\n", __func__, reg, ret);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	return ret;
}

int fan5405_enable_charging(struct i2c_client *client)
{
	int val;

	pr_fan(VERBOSE, "%s\n", __func__);
	val = fan5405_read_reg(client, fan5405_CONTROL1);
	if (val >= 0) {
		val &= ~(CON1_CE_MASK << CON1_CE_SHIFT);
		if (fan5405_write_reg(client, fan5405_CONTROL1, val) < 0) {
			pr_fan(ERROR, "%s : error!\n", __func__);
			return -1;
		}
	}
	return 0;
}

int fan5405_disable_charging(struct i2c_client *client)
{
	int val;

	pr_fan(VERBOSE, "%s\n", __func__);
	val = fan5405_read_reg(client, fan5405_CONTROL1);
	if (val >= 0) {
		val |= (CON1_CE_MASK << CON1_CE_SHIFT);
		if (fan5405_write_reg(client, fan5405_CONTROL1, val) < 0) {
			pr_fan(ERROR, "%s : error!\n", __func__);
			return -1;
		}
	}
	return 0;
}

static int fan5405_get_charger_type(void)
{
	pr_fan(VERBOSE, "%s, %d\n", __func__, charge_mode);
	return charge_mode;

}
static int fan5405_reg_save(void)
{
	int i = 0;
	int val;
	struct i2c_client *client = fan_charger->client;

	/* Read and save the register values */
	pr_fan(VERBOSE, "%s\n", __func__);
	for (i = 0; i < reg_backup_tb_len; i++) {
		if (fan5405_reg_backup[i].addr == fan5405_OREG) {
			val = fan5405_read_reg(client,
					fan5405_reg_backup[i].addr);
			if (val < 0) {
				pr_fan(ERROR, "%s : Read err: %d\n",
						__func__, val);
				return -EAGAIN;
			}
			val &= ~(OREG_VFLOAT_MASK << OREG_VFLOAT_SHIFT);
			val |= (fan_charger->float_volt << OREG_VFLOAT_SHIFT);
			fan5405_reg_backup[i].val = val;
			pr_fan(FLOW, "Saving REG(0x%x) = 0x%x\n",
					fan5405_reg_backup[i].addr,
					fan5405_reg_backup[i].val);
		} else {
			val = fan5405_read_reg(client,
						fan5405_reg_backup[i].addr);
			if (val < 0) {
				pr_fan(ERROR, "%s : Read error: %d\n",
						__func__, val);
				return -EAGAIN;
			}
			fan5405_reg_backup[i].val = val;
			pr_fan(FLOW, "Saving REG(0x%x) = 0x%x\n",
					fan5405_reg_backup[i].addr,
					fan5405_reg_backup[i].val);
		}
	}
	if (!fan_charger->reg_saved)
		fan_charger->reg_saved = true;
	fan_charger->fault_count = 0;

	return 0;
}
#ifdef custom_t_BATTERY
static void show_all_reg(struct i2c_client *client)
{
	int val = 0;
	val = fan5405_read_reg(client, fan5405_CONTROL0);
	if (val >= 0) {
		pr_fan(FLOW,"fan5405_CONTROL0 %x\n",val);
	}
	val = fan5405_read_reg(client, fan5405_CONTROL1);
	if (val >= 0) {
		pr_fan(FLOW,"fan5405_CONTROL1 %x\n",val);
	}
	val = fan5405_read_reg(client, fan5405_OREG);
	if (val >= 0) {
		pr_fan(FLOW,"fan5405_OREG %x\n",val);
	}
	val = fan5405_read_reg(client, fan5405_IC_INFO);
	if (val >= 0) {
		pr_fan(FLOW,"fan5405_IC_INFO %x\n",val);
	}
	val = fan5405_read_reg(client, fan5405_IBAT);
	if (val >= 0) {
		pr_fan(FLOW,"fan5405_IBAT %x\n",val);
	}
	val = fan5405_read_reg(client, fan5405_SP_CHARGER);
	if (val >= 0) {
		pr_fan(FLOW,"fan5405_SP_CHARGER %x\n",val);
	}
	val = fan5405_read_reg(client, fan5405_SAFETY);
	if (val >= 0) {
		pr_fan(FLOW,"fan5405_SAFETY %x\n",val);
	}
}
extern void vibrator_ctrl_regulator(int on_off);
static void fan5405_fault_restore(void);
static int fan5405_set_charge_current(unsigned int curr);
int ifHiZ(struct i2c_client *client)
{
	int val;
	int val2;
	val = fan5405_read_reg(client, fan5405_CONTROL1);
	val2 = fan5405_read_reg(client, fan5405_CONTROL0);	
	pr_fan(FLOW,"fan5405_CONTROL0 %x fan5405_CONTROL1 %x HiZ %x \n",val2,val,(val & (CON1_HZ_MODE_MASK << CON1_HZ_MODE_SHIFT)));
	if ((val & (CON1_HZ_MODE_MASK << CON1_HZ_MODE_SHIFT))) 
	{
		return 1;
	}
	else
	{
		return 0;
	}
}
int exitHiZ(struct i2c_client *client)
{
	int val;
	pr_fan(FLOW,"before fan5405 reset !!!!\n");

	show_all_reg(client);
	
	fan5405_fault_restore();

//exit HI-Z
	val = fan5405_read_reg(client, fan5405_CONTROL1);
	if (val < 0) {
		pr_fan(ERROR, "%s : error!\n", __func__);
		return -1;
	}
	//disable HIZ
	val &= ~(CON1_HZ_MODE_MASK << CON1_HZ_MODE_SHIFT);
	//disbale boost
	val &= ~(CON1_OPA_MODE_MASK << CON1_OPA_MODE_SHIFT);
	//Vlow set to 3.4V
	val &= ~(CON1_LOWV_MASK << CON1_LOWV_SHIFT);


	val = fan5405_write_reg(client, fan5405_CONTROL1, val);
	if (val < 0) {
		pr_fan(ERROR, "%s : error!\n", __func__);
		return -1;
	}

//set STAT 1
	val = fan5405_read_reg(client, fan5405_CONTROL0);
	if (val < 0) {
		pr_fan(ERROR, "%s : error!\n", __func__);
		return -1;
	}
	val |= CON0_EN_STAT_MASK << CON0_EN_STAT_SHIFT;
	
	val = fan5405_write_reg(client, fan5405_CONTROL0, val);
	if (val < 0) {
		pr_fan(ERROR, "%s : error!\n", __func__);
		return -1;
	}

	fan5405_set_charge_current(charge_current);


	pr_fan(FLOW,"after fan5405 reset !!!!\n");

	show_all_reg(client);

	//otg_error = 1;

	return 0;
	
}
#endif

#ifdef custom_t_BATTERY
int fan5405_set_charge(unsigned int en)
#else
static int fan5405_set_charge(unsigned int en)
#endif
{
	int ret = 0;
	int retires = FAN_RETRIES;
#ifdef custom_t_BATTERY
	pr_fan(FLOW, "%s %d\n", __func__,en);
#endif

#ifdef custom_t_BATTERY
	if(ifHiZ(fan_charger->client))
	{
		exitHiZ(fan_charger->client);
	}
#endif
	
	if (en) {
		ret = fan5405_enable_charging(fan_charger->client);
	} else {
		charge_mode = POWER_SUPPLY_TYPE_BATTERY;
		ret = fan5405_disable_charging(fan_charger->client);
	}

	while (retires--) {
		ret = fan5405_reg_save();
		if (!ret)
			break;
		msleep(FAN_RETRIES_DELAY);
	}
#ifdef custom_t_BATTERY
	WARN_ON(retires <= 0);
#else
	BUG_ON(retires <= 0);
#endif
#ifdef CONFIG_SEC_CHARGING_FEATURE
	ret = bcm_chrgr_en_st(en);
#endif
	return ret;
}

static int fan5405_get_float_volt(unsigned int mvolt)
{
	int validval = mvolt;

	if (validval < 3500)
		validval = 3500;
	else if (validval > 4400)
		validval = 4400;
	/* Voltage ranges are from 3.5V to 4.4V with a step of 20mV */
	/*(Charging volt - Min voltage) / Step size */
	fan_charger->float_volt = ((validval - FAN5405_DEF_VFLOAT) /
			FAN5405_DEF_VFLOAT_STEP);
	return fan_charger->float_volt;

}
static int fan5405_set_charge_volt(unsigned int mvolt)
{
	int val = 0;
	struct i2c_client *client = fan_charger->client;

	val = fan5405_read_reg(client, fan5405_OREG);
	if (val >= 0) {
		/* Set Float voltage to 4.34 volts*/
		val &= ~(OREG_VFLOAT_MASK << OREG_VFLOAT_SHIFT);

		val |= (fan5405_get_float_volt(mvolt) << OREG_VFLOAT_SHIFT);
		if (fan5405_write_reg(client, fan5405_OREG, val) < 0) {
			pr_fan(ERROR, "%s : error!\n", __func__);
			return -1;
		}
	}

	return val;
}

static int fan5405_set_charge_current(unsigned int curr)
{
	int val = 0;
	u8 Iinlimit;
	struct i2c_client *client = fan_charger->client;
	int validval = curr;

	if (validval > 1250)
		validval = 1250;

	if (validval > 800)
		Iinlimit = 0x3;
	else if (validval > 500)
		Iinlimit = 0x2;
	else if (validval > 100)
		Iinlimit = 0x1;
	else
		Iinlimit = 0;

	pr_fan(VERBOSE, "%s :current = %d\n", __func__, curr);
	if (curr <= 500)
		charge_mode = POWER_SUPPLY_TYPE_USB;
	else
		charge_mode = POWER_SUPPLY_TYPE_USB_DCP;

	/* Set Input current limit */
	val = fan5405_read_reg(client, fan5405_CONTROL1);
	if (val >= 0) {
		val &= ~(CON1_IN_LIMIT_MASK << CON1_IN_LIMIT_SHIFT);
		val |= Iinlimit << CON1_IN_LIMIT_SHIFT;
		if (fan5405_write_reg(client, fan5405_CONTROL1, val) < 0) {
			pr_fan(ERROR, "%s : error!\n", __func__);
			return -1;
		}
	}

	/* Enable output charging current setting by clearing IO_LEVEL bit */
	val = fan5405_read_reg(client, fan5405_SP_CHARGER);
	if (val >= 0) {
		val &= ~(SPC_IOLEVEL_MASK << SPC_IOLEVEL_SHIFT);
		if (fan5405_write_reg(client, fan5405_SP_CHARGER, val) < 0) {
			pr_fan(ERROR, "%s : error!\n", __func__);
			return -1;
		}
	}

	val = fan5405_read_reg(client, fan5405_IBAT);
	if (val >= 0) {
		/* Don't write RESET bit. */
		val &= ~(IBAT_RESET_MASK << IBAT_RESET_SHIFT);

		/* Current ranges are from 550 to 1250 with a step of 100mA */
		/*(Charging current - Min current rating charger) / Step size */
		if (validval <= FAN5405_IOCHARGE_650MA)
			validval = FAN5405_IOCHARGE_650MA;
		else if (validval >= FAN5405_IOCHARGE_1250MA)
			validval = FAN5405_IOCHARGE_1250MA;
		validval =
		    validval * fan_charger->pdata->sense_r / SENSE_R_DEFAULT;
		if (validval < FAN5405_IOCHARGE_550MA)
			validval = FAN5405_IOCHARGE_550MA;
		val &= ~(IBAT_IOCHARGE_MASK << IBAT_IOCHARGE_SHIFT);
		val |= (((validval - FAN5405_IOCHARGE_550MA)/IOCHARGE_STEP_SIZE)
							<< IBAT_IOCHARGE_SHIFT);
		if (fan5405_write_reg(client, fan5405_IBAT, val) < 0) {
			pr_fan(ERROR, "%s : error!\n", __func__);
			return -1;
		}
	}

#ifdef custom_t_BATTERY
	charge_current = curr;
#endif

	return val;
}

#if 0
/* End of charge is determined by fuel gauge by monitoring batt voltage.
 * So no need to set the eoc current. */
static int fan5405_set_full_charge(unsigned int eoc)
{
	int ret = 0;
	int validval = eoc;

	pr_fan(VERBOSE, "%s : eoc =%d\n", __func__, eoc);

	if (eoc < 25 || eoc > 200)
		validval = 200; /* max top-off */

#ifdef NO_USE_TERMINATION_CURRENT
	validval = 25;	/* don't use charger eoc. */
#endif
	ret = fan5405_set_top_off(fan_charger->client, validval);

	return ret;
}
#endif

static void fan5405_timer_work_func(struct work_struct *work)
{
	int val = 0;
	struct fan5405_chip *p = container_of(work, struct fan5405_chip,
						timer_work.work);

	val = fan5405_read_reg(p->client, fan5405_CONTROL0);
	pr_fan(VERBOSE, "%s: val = 0x%x\n", __func__, val);
	if (val >=  0) {
		/*Reset t32 timer*/
		val |= CON0_TMR_RST_MASK << CON0_TMR_RST_SHIFT;
		val = fan5405_write_reg(p->client,
				fan5405_CONTROL0, val);
		if (val < 0)
			pr_fan(ERROR, "%s : Write error!\n", __func__);
	}

	schedule_delayed_work(&p->timer_work,
				msecs_to_jiffies(FAN5405_TIMER_RESET_PERIOD));
}

/**
 * fan5405_chrgr_connect: Start/Stop petting safety timer.
 * This is a blocking call.
 */
static int fan5405_chrgr_connect(unsigned int connect)
{

	if (connect == 1) {
		fan_charger->chrgr_connect = true;
		schedule_delayed_work(&fan_charger->timer_work, 0);
	} else {
		fan_charger->chrgr_connect = false;
		fan_charger->reg_saved = false;
		cancel_delayed_work_sync(&fan_charger->timer_work);
	}
	return 0;
}
static void fan5405_fault_restore(void)
{
	struct i2c_client *client = fan_charger->client;
	int val;
	int i = 0;

	pr_fan(FLOW, "%s\n", __func__);

	/* Disable charging */
	val = fan5405_read_reg(client, fan5405_CONTROL1);
	if (val >= 0) {
		val |= CON1_CE_MASK << CON1_CE_SHIFT;
		val = fan5405_write_reg(client,
				fan5405_CONTROL1, val);

		if (val < 0)
			pr_fan(ERROR, "%s : Write error!\n", __func__);
	}

	/* Under any fault, reset registers to last known good value */
	for (i = 0; i < reg_backup_tb_len; i++) {
		if (fan5405_reg_backup[i].addr != fan5405_IBAT) {
			pr_fan(FLOW, "Writing REG(0x%x) = 0x%x\n\n",
				fan5405_reg_backup[i].addr,
				fan5405_reg_backup[i].val);
			val = fan5405_write_reg(client,
					fan5405_reg_backup[i].addr,
					fan5405_reg_backup[i].val);
			if (val < 0)
				pr_fan(ERROR, "%s : Write error!\n", __func__);
		} else {
			/* Dont write RESET bit */
			fan5405_reg_backup[i].val &=
					~(IBAT_RESET_MASK << IBAT_RESET_SHIFT);
			pr_fan(FLOW, "Writing REG(0x%x) = 0x%x\n\n",
				fan5405_reg_backup[i].addr,
				fan5405_reg_backup[i].val);
			val = fan5405_write_reg(client,
					fan5405_reg_backup[i].addr,
					fan5405_reg_backup[i].val);
			if (val < 0)
				pr_fan(ERROR, "%s : Write error!\n", __func__);

		}

	}
}

static void fan5405_intr_work_func(struct work_struct *work)
{
	struct fan5405_chip *p = container_of(work, struct fan5405_chip,
								intr_work);
	int val;
	pr_fan(FLOW, "%s\n", __func__);

	if (!p) {
		pr_fan(ERROR, "%s: fan5405_chip is NULL\n", __func__);
		return ;
	}

	/* Check CONTROL0 register's STAT bits to know if its in fault state  */
	val = fan5405_read_reg(p->client, fan5405_CONTROL0);
	if (val < 0) {
		pr_fan(ERROR, "%s : read error:!\n", __func__);
		return;
	}

	val = val & CON0_FAULT_MASK;
	if ((val != NO_FAULT) && (val != SLEEP_MODE)) {
		pr_fan(ERROR, "FAN5405 Fault Occured: 0x%x\n", val);
		if (fan_charger->fault_count < FAULT_RESTORE_RETRIES) {
			fan5405_fault_restore();
			fan_charger->fault_count++;
		} else
			pr_fan(ERROR, "Fault restore retires overflow!!\n");
	} else if (fan_charger->fault_count) {
		pr_fan(ERROR, "Reset fan fault_count\n");
		fan_charger->fault_count = 0;
	}

	/* Read all the registers */
	pr_fan(FLOW, "0x0: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
			fan5405_read_reg(p->client, fan5405_CONTROL0),
			fan5405_read_reg(p->client, fan5405_CONTROL1),
			fan5405_read_reg(p->client, fan5405_OREG),
			fan5405_read_reg(p->client, fan5405_IC_INFO),
			fan5405_read_reg(p->client, fan5405_IBAT),
			fan5405_read_reg(p->client, fan5405_SP_CHARGER),
			fan5405_read_reg(p->client, fan5405_SAFETY));
	pr_fan(FLOW, "0x10: 0x%x\n",
			fan5405_read_reg(p->client, fan5405_MONITOR));
	return;
}

static irqreturn_t fan5405_irq_handler(int irq, void *data)
{
	struct fan5405_chip *p = (struct fan5405_chip *)data;

	pr_fan(FLOW, "%s\n", __func__);
	if (p->chrgr_connect && p->reg_saved)
		schedule_work(&(p->intr_work));

	/* There is no interrupt register in FAN5405 */
	return IRQ_HANDLED;
}

static int fan5405_irq_init(struct i2c_client *client)
{
	int ret = 0;
	if (client->irq) {
		/* Handle Rising edge. Because under FAULT condition, STAT pin
		pulses (goes low and then high after 128us)*/
		ret = request_threaded_irq(client->irq, NULL,
				fan5405_irq_handler,
				(IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
				IRQF_ONESHOT |
				IRQF_NO_SUSPEND),
				"fan5405_charger", fan_charger);

		if (ret) {
			pr_fan(ERROR, "%s: failed to reqeust IRQ\n", __func__);
			return ret;
		}

		ret = enable_irq_wake(client->irq);
		if (ret < 0)
			dev_err(&client->dev,
				"failed to enable wakeup src %d\n", ret);
	} else
		pr_fan(ERROR, "%s: FAN5405 IRQ is NULL\n", __func__);

	return ret;
}

static int fan5405_charger_hardware_init(struct i2c_client *client)
{
	int val;

	/* Set safety registers. ISAFE = 1250mA, VSAFE = 4.44v */
	val = SAFETY_ISAFE_MASK | SAFETY_VSAFE_MASK;

	val = fan5405_write_reg(client, fan5405_SAFETY, val);
	if (val < 0) {
		pr_fan(ERROR, "%s : error!\n", __func__);
		return -1;
	}
	return val;
}
static int fan5405_get_charging_status(void)
{
	struct i2c_client *client = fan_charger->client;
	int val;

	/* Check CONTROL0 register's STAT bits to know if battery is charging */
	val = fan5405_read_reg(client, fan5405_CONTROL0);
	if (val >= 0) {
#ifdef custom_t_BATTERY
		if(ifHiZ(client))
		{
			show_all_reg(client);
			//vibrator_ctrl_regulator(1);
			//exitHiZ(client);
		}
#endif		
		if (((val >> CON0_STAT_SHIFT) & CON0_STAT_MASK)
						== STAT_CHARGING)
			return 1;
	}

	return 0;
}
/**
 * fan5405_otg_enable_disable: Enable/Disbale Boost mode.
 * This is a blocking call.
 */
int fan5405_otg_enable_disable(int onoff)
{
	int val = 0;
	int ret;
	struct i2c_client *client = fan_charger->client;

	pr_fan(FLOW, "%s\n", __func__);
	val = fan5405_read_reg(client, fan5405_CONTROL1);
	if (val < 0) {
		pr_fan(ERROR, "error reading fan5405_CONTROL1\n");
		return -EAGAIN;
	}

	if (onoff) {
		ret = fan5405_write_reg(client, fan5405_CONTROL1,
				(val | OREG_OTG_EN_MASK));
		if (ret < 0)
			return -EAGAIN;
		pr_fan(FLOW, "Boost mode enabled\n");
		schedule_delayed_work(&fan_charger->timer_work, 0);
	} else {

		ret = fan5405_write_reg(client, fan5405_CONTROL1,
				(val & ~OREG_OTG_EN_MASK));
		if (ret < 0)
			return -EAGAIN;
		pr_fan(FLOW, "Boost mode disabled\n");
		cancel_delayed_work_sync(&fan_charger->timer_work);
	}
	return 0;
}
EXPORT_SYMBOL(fan5405_otg_enable_disable);

#ifdef CONFIG_DEBUG_FS
int fan5405_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t fan5405_debugfs_regread(struct file *file,
			   char const __user *buf, size_t count, loff_t *offset)
{
	u32 len = 0;
	int ret;
	u32 reg = 0xFF;
	char input_str[INPUT_STR_LEN];
	struct i2c_client *client = (struct i2c_client *)file->private_data;
	BUG_ON(!client);
	if (count > 100)
		len = 100;
	else
		len = count;

	if (copy_from_user(input_str, buf, len))
		return -EFAULT;
	/* coverity[secure_coding] */
	sscanf(input_str, "%x", &reg);

	if (!reg || reg == 0xFF) {
		pr_fan(ERROR, "invalid param !!\n");
		return -EFAULT;
	}

	ret = fan5405_read_reg(client, (int)reg);

	if (ret < 0) {
		pr_fan(ERROR, "%s: fan5405 reg read failed\n", __func__);
		return count;
	}
	pr_fan(FLOW, "Reg [0x%02x] = 0x%02x\n", reg, ret);
	return count;
}

static ssize_t fan5405_debugfs_regwrite(struct file *file,
			   char const __user *buf, size_t count, loff_t *offset)
{
	u32 len = 0;
	int ret;
	u32 reg = 0xFF;
	u32 value;
	char input_str[INPUT_STR_LEN];
	struct i2c_client *client = (struct i2c_client *)file->private_data;
	BUG_ON(!client);

	if (count > 100)
		len = 100;
	else
		len = count;

	if (copy_from_user(input_str, buf, len))
		return -EFAULT;
	/* coverity[secure_coding] */
	sscanf(input_str, "%x%x", &reg, &value);

	pr_fan(FLOW, " %x %x\n", reg, value);
	if (reg == 0xFF) {
		pr_fan(ERROR, "invalid param !!\n");
		return -EFAULT;
	}

	ret = fan5405_write_reg(client, (int)reg, (u8)value);
	if (ret < 0)
		pr_fan(ERROR, "%s:fan5405 write failed\n", __func__);
	return count;
}

static ssize_t fan5405_debugfs_regdump(struct file *file, char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct i2c_client *client = (struct i2c_client *)file->private_data;
	char out_str[OUTPUT_STR_LEN];
	int len = 0;
	int ret;
	u8 reg;
	memset(out_str, 0, sizeof(out_str));

	for (reg = 0x00; reg <= 0x06; reg += 1) {
		ret = fan5405_read_reg(client, (int)reg);
		if (ret < 0) {
			pr_fan(ERROR, "%s: fan5405 reg read failed\n",
					__func__);
			return count;
		}
		len += snprintf(out_str+len, sizeof(out_str) - len,
					"Reg[0x%02x]:  0x%02x\n", reg, ret);
	}
	reg = 0x10;
	ret = fan5405_read_reg(client, (int)reg);
	if (ret < 0) {
		pr_fan(ERROR, "%s: fan5405 reg read failed\n", __func__);
		return count;
	}
	len += snprintf(out_str+len, sizeof(out_str) - len,
					"Reg[0x%02x]:  0x%02x\n\n", reg, ret);
	return simple_read_from_buffer(user_buf, count, ppos, out_str, len);
}



static const struct file_operations debug_fan5405_read_fops = {
	.write = fan5405_debugfs_regread,
	.open = fan5405_debugfs_open,
};

static const struct file_operations debug_fan5405_write_fops = {
	.write = fan5405_debugfs_regwrite,
	.open = fan5405_debugfs_open,
};

static const struct file_operations debug_fan5405_dump_fops = {
	.read = fan5405_debugfs_regdump,
	.open = fan5405_debugfs_open,
};

static int debugfs_fan5405_chrgr_connect(void *data, u64 enable)
{
	if (enable == true)
		schedule_delayed_work(&fan_charger->timer_work, 0);
	else if (enable == false)
		cancel_delayed_work_sync(&fan_charger->timer_work);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(chrgr_connect_fops,
		NULL, debugfs_fan5405_chrgr_connect, "%llu\n");

static void fan5405_debugfs_init(void)
{
	if (fan_charger->dent_fan5405)
		return;

	fan_charger->dent_fan5405 = debugfs_create_dir("fan5405_charger", NULL);
	if (!fan_charger->dent_fan5405) {
		pr_fan(ERROR, "Failed to setup fan5405 charger debugfs\n");
		return;
	}

	if (!debugfs_create_file("regread", S_IWUSR | S_IRUSR,
			fan_charger->dent_fan5405, fan_charger->client,
						&debug_fan5405_read_fops))
		goto err;
	if (!debugfs_create_file("regwrite", S_IWUSR | S_IRUSR,
			fan_charger->dent_fan5405, fan_charger->client,
						&debug_fan5405_write_fops))
		goto err;
	if (!debugfs_create_file("reg_dump", S_IRUSR,
		fan_charger->dent_fan5405, fan_charger->client,
						&debug_fan5405_dump_fops))
		goto err;
	if (!debugfs_create_file("chrgr_connect", S_IWUSR | S_IRUSR,
		fan_charger->dent_fan5405, fan_charger->client,
					&chrgr_connect_fops))
		goto err;
	if (!debugfs_create_u32("debug_mask", S_IWUSR | S_IRUSR,
		fan_charger->dent_fan5405, &debug_mask))
		goto err;

	return;
err:
	pr_fan(ERROR, "Failed to setup fan5405 charger debugfs\n");
	debugfs_remove(fan_charger->dent_fan5405);
}
#endif
static void fan5405_parser_dt(struct i2c_client *client,
			      struct fan5405_platform_data *pdata)
{
	struct device_node *np;
	u32 pval;
	int ret;
	np = of_find_matching_node(NULL, fan5405_of_match);
	if (!np) {
		pr_err("set sense_r as default 68 mOhm\n");
		pdata->sense_r = 68;
		return;
	}
	ret = of_property_read_u32(np, "sense_r", &pval);
	if (!ret)
		pdata->sense_r = pval;
	else {
		dev_warn(&client->dev, "sense_r not provided\n");
		pdata->sense_r = 68;
	}
	dev_info(&client->dev, "fan5405_parser_dt sense_r: %d\n",
		 pdata->sense_r);
}

static int fan5405_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct fan5405_chip *chip;

	pr_fan(FLOW, "%s\n", __func__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	fan_charger = chip;
	chip->client = client;
	charge_mode = POWER_SUPPLY_TYPE_BATTERY;
	chip->pdata = kzalloc(sizeof(*chip->pdata), GFP_KERNEL);
	if (!chip->pdata) {
		kfree(chip);
		return -ENOMEM;
	}
	dev_info(&client->dev, "parse dts\n");
	fan5405_parser_dt(client, chip->pdata);
	dev_info(&client->dev, "sense_r: %d\n", chip->pdata->sense_r);
	i2c_set_clientdata(client, chip);

	mutex_init(&fan_charger->i2c_mutex_lock);
	wake_lock_init(&fan_charger->i2c_lock, WAKE_LOCK_SUSPEND,
						"fan5405_i2c");

    // Set 0x06 first than other write operation.
    fan5405_charger_hardware_init(client);

//custom_t:CJ i dont think it useful
#if custom_t_BATTERY
#if 0
    if(fan5405_get_charging_status())
    {
        pr_info("fan5405_set_charge 0 for D2153-BATT first read d2153_read_voltage\n");
        fan5405_set_charge(0);
    }
#endif	
#endif	
	
	INIT_WORK(&(chip->intr_work), fan5405_intr_work_func);
	INIT_DELAYED_WORK(&chip->timer_work, fan5405_timer_work_func);

#if defined(CONFIG_SEC_CHARGING_FEATURE)
	spa_agent_register(SPA_AGENT_SET_CHARGE,
			(void *)fan5405_set_charge, "fan5405-charger");
	spa_agent_register(SPA_AGENT_SET_CHARGE_CURRENT,
			(void *)fan5405_set_charge_current, "fan5405-charger");
/* End of charge is taken care by Fuel gauge */
/*	spa_agent_register(SPA_AGENT_SET_FULL_CHARGE,
			(void *)fan5405_set_full_charge, "fan5405-charger");
*/
	spa_agent_register(SPA_AGENT_SET_CHARGE_VOLTAGE,
			(void *)fan5405_set_charge_volt, "fan5405-charger");
	spa_agent_register(SPA_AGENT_GET_CHARGER_TYPE,
			(void *)fan5405_get_charger_type, "fan5405-charger");
	spa_agent_register(SPA_AGENT_GET_CHARGE_STATE,
			(void *)fan5405_get_charging_status, "fan5405-charger");

#else
	bcm_agent_register(BCM_AGENT_SET_CHARGE,
			(void *)fan5405_set_charge, "fan5405-charger");
	bcm_agent_register(BCM_AGENT_SET_CHARGE_CURRENT,
			(void *)fan5405_set_charge_current, "fan5405-charger");
	bcm_agent_register(BCM_AGENT_SET_CHARGE_VOLTAGE,
			(void *)fan5405_set_charge_volt, "fan5405-charger");
	bcm_agent_register(BCM_AGENT_GET_CHARGER_TYPE,
			(void *)fan5405_get_charger_type, "fan5405-charger");
	bcm_agent_register(BCM_AGENT_GET_CHARGE_STATE,
			(void *)fan5405_get_charging_status, "fan5405-charger");
	bcm_agent_register(BCM_AGENT_CHRGR_INSERT,
			(void *)fan5405_chrgr_connect, "fan5405-charger");
#endif

	fan5405_irq_init(client);

#ifdef CONFIG_DEBUG_FS
	fan5405_debugfs_init();
#endif
#ifdef custom_t_BATTERY
	show_all_reg(client);
#endif
	return 0;
}

static int fan5405_remove(struct i2c_client *client)
{
	struct fan5405_chip *chip = i2c_get_clientdata(client);
	mutex_destroy(&fan_charger->i2c_mutex_lock);
	kfree(chip);
	return 0;
}

static int fan5405_suspend_pre(struct device *dev)
{
	struct fan5405_chip *chip = dev_get_drvdata(dev);

	if (fan_charger->chrgr_connect == true)
		cancel_delayed_work_sync(&chip->timer_work);

	return 0;
}

static void fan5405_complete(struct device *dev)
{
	struct fan5405_chip *chip = dev_get_drvdata(dev);
	if (fan_charger->chrgr_connect == true)
		schedule_delayed_work(&chip->timer_work, 0);
}

static const struct i2c_device_id fan5405_id[] = {
	{ "fan5405", 0 },
	{ }
};

static const struct dev_pm_ops pm = {
	.prepare = fan5405_suspend_pre,
	.complete = fan5405_complete,
};

static struct i2c_driver fan5405_i2c_driver = {
	.driver	= {
		.name	= "fan5405",
		.pm = &pm,
		   .of_match_table = fan5405_of_match,
	},
	.probe		= fan5405_probe,
	.remove		= fan5405_remove,
	.id_table	= fan5405_id,
};

static int __init fan5405_init(void)
{
	return i2c_add_driver(&fan5405_i2c_driver);
}

static void __exit fan5405_exit(void)
{
	i2c_del_driver(&fan5405_i2c_driver);
}

subsys_initcall(fan5405_init);
module_exit(fan5405_exit);

MODULE_DESCRIPTION("FAN5405 charger control driver");
MODULE_AUTHOR("RENESAS");
MODULE_LICENSE("GPL");
