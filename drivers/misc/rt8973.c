/*
 * driver/misc/tsu6712.c - TSU6712 micro USB switch device driver
 *
 * Copyright (C) 2012 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/tsu6712.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <mach/setup-u2usb.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/input.h>
#include <linux/power_supply.h>
#include <linux/spa_power.h>
#include <linux/switch.h>
#include <linux/ctype.h>
#include <linux/wakelock.h>
#include <linux/bcm.h>
#ifdef CONFIG_USB_R8A66597
extern void send_usb_insert_event(int);
#else
void send_usb_insert_event(int i) {}
#endif

#define USE_LOCAL_CALLBACK
#define USE_USB_UART_SWITCH
//#define USE_TSU6712_OTG

/* TSU6721 I2C registers */
#define TSU6712_REG_DEVID		0x01
#define TSU6712_REG_CTRL		0x02
#define TSU6712_REG_INT1		0x03
#define TSU6712_REG_INT2		0x04
#define TSU6712_REG_INT1_MASK		0x05
#define TSU6712_REG_INT2_MASK		0x06
#define TSU6712_REG_ADC			0x07
#define TSU6712_REG_TIMING1		0x08
#define TSU6712_REG_TIMING2		0x09
#define TSU6712_REG_DEV_T1		0x0a
#define TSU6712_REG_DEV_T2		0x0b
#define TSU6712_REG_BTN1		0x0c
#define TSU6712_REG_BTN2		0x0d
#define TSU6712_REG_MANSW1		0x13
#define TSU6712_REG_MANSW2		0x14
#define TSU6712_REG_DEV_T3		0x15
#define TSU6712_REG_RESET	   0x1B
#define TSU6712_REG_TIMER_SET 0x20
#define TSU6712_REG_OCL_OCP_SET1 0x21
#define TSU6712_REG_OCL_OCP_SET2 0x22
#define TSU6712_REG_DEV_T4	   0x23

/* Control */
#define CON_USB_CHDN		(1 << 6)
#define CON_CHG_TYP			(1 << 5)
#define CON_SWITCH_OPEN		(1 << 4)
#define CON_RAW_DATA		(1 << 3)
#define CON_MANUAL_SW		(1 << 2)	/* Auto Config */
#define CON_WAIT			(1 << 1)
#define CON_INT_MASK		(1 << 0)
#define CON_MASK		(CON_SWITCH_OPEN | CON_RAW_DATA | \
	CON_MANUAL_SW | CON_WAIT)

/* Device Type 1 */
#define DEV_USB_OTG		(1 << 7)
#define DEV_DEDICATED_CHG	(1 << 6)
#define DEV_USB_CHG		(1 << 5)
#define DEV_CAR_KIT		(1 << 4)
#define DEV_UART		(1 << 3)
#define DEV_USB			(1 << 2)
#define DEV_AUDIO_2		(1 << 1)
#define DEV_AUDIO_1		(1 << 0)

#define DEV_T1_USB_MASK		(DEV_USB_OTG | DEV_USB_CHG | DEV_USB)
#define DEV_T1_UART_MASK	(DEV_UART)
#define DEV_T1_CHARGER_MASK	(DEV_DEDICATED_CHG | DEV_CAR_KIT)

/* Device Type 2 */
#define DEV_UNKNOWN_ACC		(1 << 7)
#define DEV_AV			(1 << 6)
#define DEV_TTY			(1 << 5)
#define DEV_PPD			(1 << 4)
#define DEV_JIG_UART_OFF	(1 << 3)
#define DEV_JIG_UART_ON		(1 << 2)
#define DEV_JIG_USB_OFF		(1 << 1)
#define DEV_JIG_USB_ON		(1 << 0)

/* Device Type 3 */
#define DEV_MHL		(1 << 0)
#define DEV_VBUS_DEB	(1 << 1)
#define DEV_AV_VBUS	(1 << 4)
#define DEV_APPLE_CHG (1 <<5)
#define DEV_T3_CHARGER_MASK	(DEV_APPLE_CHG)

#define DEV_T2_USB_MASK		(DEV_JIG_USB_OFF | DEV_JIG_USB_ON)
#define DEV_T2_UART_MASK	(DEV_JIG_UART_OFF)
#define DEV_T2_JIG_MASK		(DEV_JIG_USB_OFF | DEV_JIG_USB_ON | DEV_JIG_UART_OFF)
#define VBUS_VALID		(1 << 1)

/*
* Manual Switch
* D- [7:5] / D+ [4:2]
* 000: Open all / 001: USB / 010: AUDIO / 011: UART / 100: V_AUDIO
*/
#define SW_VAUDIO		((4 << 5) | (4 << 2) | (1 << 0))
#define SW_UART			((3 << 5) | (3 << 2))
#define SW_AUDIO		((2 << 5) | (2 << 2) | (3 << 0))
#define SW_DHOST		((1 << 5) | (1 << 2) | (1 << 0))
#define SW_AUTO			((0 << 5) | (0 << 2))
#define SW_USB_OPEN		(1 << 0)
#define SW_ALL_OPEN		(0)

/* Interrupt 1 */
#define INT_DCD_T		(1 << 3)
#define INT_CHGDET		(1 << 2)
#define INT_DETACH		(1 << 1)
#define INT_ATTACH		(1 << 0)

/* ADC table */
#define	ADC_GND			0x00
#define	ADC_MHL			0x01
#define	ADC_DOCK_PREV_KEY 0x04
#define	ADC_DOCK_NEXT_KEY 0x07
#define	ADC_DOCK_VOL_DN		0x0a
#define	ADC_DOCK_VOL_UP		0x0b
#define	ADC_DOCK_PLAY_PAUSE_KEY 0x0d
#define	ADC_AUDIO_DOCK	0x12
#define	ADC_CEA936ATYPE1_CHG	0x17
#define	ADC_JIG_USB_OFF		0x18
#define	ADC_JIG_USB_ON		0x19
#define	ADC_DESKDOCK		0x1a
#define	ADC_CEA936ATYPE2_CHG	0x1b
#define	ADC_JIG_UART_OFF	0x1c
#define	ADC_JIG_UART_ON		0x1d
#define	ADC_CARDOCK		0x1d
#define	ADC_OPEN		0x1f

/* Accy Type */
#define ACC_NONE			(0)
#define ACC_DESK_DOCK		(1 << 0)
#define ACC_CAR_DOCK		(1 << 1)
#define ACC_AUDIO_DOCK		(1 << 2)
#define ACC_MHL				(1 << 3)
#define ACC_UNKOWN_DOCK		(1 << 4)
#define SW_EN_USB_CONN 0x24

struct tsu6712_usbsw {
	struct i2c_client		*client;
	struct tsu6712_platform_data	*pdata;
	struct input_dev	*input;
	struct work_struct work;
	struct delayed_work vbus_work;
	struct delayed_work init_work;
	struct delayed_work dev_irq_work;
	struct mutex		mutex_lock;
	int				dev1;
	int				dev2;
	int				dev3;
	int				dev4;
	int				mansw;
	int            ovp;
	int			   previous_key;
	int				adc;
	int				attached_accy;
	int				mhl_ret;
	int				vbus;
	int				rev;
};

enum {
	DOCK_KEY_NONE			= 0,
	DOCK_KEY_VOL_UP_PRESSED,
	DOCK_KEY_VOL_UP_RELEASED,
	DOCK_KEY_VOL_DOWN_PRESSED,
	DOCK_KEY_VOL_DOWN_RELEASED,
	DOCK_KEY_PREV_PRESSED,
	DOCK_KEY_PREV_RELEASED,
	DOCK_KEY_PLAY_PAUSE_PRESSED,
	DOCK_KEY_PLAY_PAUSE_RELEASED,
	DOCK_KEY_NEXT_PRESSED,
	DOCK_KEY_NEXT_RELEASED,
};

static struct tsu6712_usbsw *local_usbsw;
static enum cable_type_t set_cable_status;
static struct wake_lock acc_wakelock;

#define SWITCH_AT       103
#define SWITCH_ISI      104
#define SWITCH_MODECHAN_02 105
#define SWITCH_MODECHAN_00 106
#define MUIC_INTERRUPT_QUEUE_MAX 8
#define MUIC_INTERRUPT_QUEUE_MIN 0
#define CABLE_USB_ATTACHED 100
#define CABLE_USB_DETACHED 101
#define CABLE_AC_ATTACHED 200
#define CABLE_AC_DETACHED 201
#define CABLE_UART_ATTACHED 200
#define CABLE_UART_DETACHED 201

#if defined(CONFIG_VIDEO_MHL_V1) || defined(CONFIG_VIDEO_MHL_V2)
#define MHL_DEVICE 2
extern u8 mhl_onoff_ex(bool onoff);
#endif
extern struct class *sec_class;

static void tsu6712_reg_init(struct tsu6712_usbsw *usbsw);

#if 1
struct interrupt_element
{
	u32	intr;
	u32	dev;
	u8	adc;
};

DEFINE_MUTEX(__MUIC_INTERRUPT_QUEUE_LOCK);
static struct interrupt_element __MUIC_INTERRUPT_QUEUE[10];
static int __MUIC_INTERRUPT_QUEUE_INDEX = -1;

static atomic_t jig_force_rel_wklock;

static int tsu6712_pop_queue(struct interrupt_element* val)
{
	mutex_lock(&__MUIC_INTERRUPT_QUEUE_LOCK);

	if (__MUIC_INTERRUPT_QUEUE_INDEX < MUIC_INTERRUPT_QUEUE_MIN)
	{
		pr_err("%s : queue empty\n", __func__);
		mutex_unlock(&__MUIC_INTERRUPT_QUEUE_LOCK);
		return __MUIC_INTERRUPT_QUEUE_INDEX;
	}

	val->intr = __MUIC_INTERRUPT_QUEUE[__MUIC_INTERRUPT_QUEUE_INDEX].intr;
	val->dev = __MUIC_INTERRUPT_QUEUE[__MUIC_INTERRUPT_QUEUE_INDEX].dev;
	val->adc = __MUIC_INTERRUPT_QUEUE[__MUIC_INTERRUPT_QUEUE_INDEX].adc;
	__MUIC_INTERRUPT_QUEUE_INDEX--;
	mutex_unlock(&__MUIC_INTERRUPT_QUEUE_LOCK);

	return __MUIC_INTERRUPT_QUEUE_INDEX + 1;
}

static void tsu6712_push_queue(struct interrupt_element* val)
{
	mutex_lock(&__MUIC_INTERRUPT_QUEUE_LOCK);
	
	if (__MUIC_INTERRUPT_QUEUE_INDEX > MUIC_INTERRUPT_QUEUE_MAX)
	{
		pr_err("%s : index overflow %d\n", __func__, __MUIC_INTERRUPT_QUEUE_INDEX);
		__MUIC_INTERRUPT_QUEUE_INDEX = -1;
	}

	__MUIC_INTERRUPT_QUEUE_INDEX++;
	__MUIC_INTERRUPT_QUEUE[__MUIC_INTERRUPT_QUEUE_INDEX].intr = val->intr;
	__MUIC_INTERRUPT_QUEUE[__MUIC_INTERRUPT_QUEUE_INDEX].dev = val->dev;
	__MUIC_INTERRUPT_QUEUE[__MUIC_INTERRUPT_QUEUE_INDEX].adc = val->adc;
	mutex_unlock(&__MUIC_INTERRUPT_QUEUE_LOCK);
}
#endif

static int tsu6712_write_reg(struct i2c_client *client, u8 reg, u8 data)
{
	int ret = 0;
	u8 buf[2];
	struct i2c_msg msg[1];

	mutex_lock(&local_usbsw->mutex_lock);
	buf[0] = reg;
	buf[1] = data;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = buf;

	pr_info("%s : reg[0x%2x] data[0x%2x]\n", __func__, buf[0], buf[1]);

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret != 1) {
		pr_err("i2c Write Failed (ret=%d)\n", ret);
		mutex_unlock(&local_usbsw->mutex_lock);
		return -1;
	}
	mutex_unlock(&local_usbsw->mutex_lock);

	return 0;
}

static int tsu6712_read_reg(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret = 0;
	u8 buf[1];
	u8 regtemp;
	struct i2c_msg msg[2];

	if (!MUIC_IS_PRESENT) {
		return 0;
	} else {
	mutex_lock(&local_usbsw->mutex_lock);
	buf[0] = reg;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = buf;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = buf;

	regtemp = buf[0];
	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret != 2) {
		pr_err("i2c Read Failed (ret=%d)\n", ret);
		mutex_unlock(&local_usbsw->mutex_lock);
		return -1;
	}
	*data = buf[0];
	pr_info("%s : reg[0x%2x] data[0x%2x]\n", __func__, regtemp, *data);

	mutex_unlock(&local_usbsw->mutex_lock);
	return 0;
	}
}

static int tsu6712_read_word_reg(struct i2c_client *client, u8 reg, int *data)
{
	int ret = 0;
	u8 data1, data2;
	ret = tsu6712_read_reg(client, reg, &data1);
	if (ret < 0) {
		pr_err("i2c Read Failed (ret=%d)\n", ret);
		return -1;
	}
	ret =	tsu6712_read_reg(client, reg + 1, &data2);
	if (ret < 0) {
		pr_err("i2c Read Failed (ret=%d)\n", ret);
		return -1;
	}

	*data = 0;
	*data = (int)((data2<<8) | data1);

	pr_info("%s : reg[0x%2x], reg[0x%2x]  data [0x%4x]\n", __func__, reg, reg + 1, *data);

	return 0;
}

static unsigned int calib_selected_interface;

#ifdef USE_USB_UART_SWITCH
static int usb_uart_switch_state;
char at_isi_switch_buf[1000] = {0};
int KERNEL_LOG = 1;

static struct switch_dev switch_usb_uart = {
        .name = "tsu6712",
};
#endif

#ifdef USE_LOCAL_CALLBACK
static struct switch_dev switch_dock = {
	.name = "dock",
};

static int tsu6712_ex_init(void)
{
	int ret;

	/* for CarDock, DeskDock */
	ret = switch_dev_register(&switch_dock);
	if (ret < 0) {
		pr_err("Failed to register dock switch. %d\n", ret);
		return ret;
	}

#ifdef USE_USB_UART_SWITCH
	/* for usb uart switch */
	ret = switch_dev_register(&switch_usb_uart);
	if (ret < 0) {
		pr_err("Failed to register dock switch. %d\n", ret);
		return ret;
	}
#endif

	return 0;
}

static void tsu6712_otg_cb(bool attached)
{
	set_cable_status = attached ? CABLE_TYPE_USB : CABLE_TYPE_NONE;
	switch (set_cable_status) {
	case CABLE_TYPE_USB:
		break;
	case CABLE_TYPE_NONE:
		break;
	default:
		break;
	}
}

static void tsu6712_usb_cdp_cb(bool attached)
{
	set_cable_status = attached ? CABLE_TYPE_USB : CABLE_TYPE_NONE;
	pr_info("tsu6712_usb_cdp_cb attached %d\n", attached);

	switch (set_cable_status) {
	case CABLE_TYPE_USB:
		pr_info("USB CDP attached : MUIC send switch state 100");
		usb_uart_switch_state = CABLE_USB_ATTACHED;
		send_usb_insert_event(1);
		pr_info("%s USB CDP attached\n", __func__);
#ifdef CONFIG_SEC_CHARGING_FEATURE
		spa_event_handler(SPA_EVT_CHARGER, (void *) POWER_SUPPLY_TYPE_USB_CDP);
#endif
		bcm_accy_event_handler(
				PMU_ACCY_EVT_OUT_CHRGR_TYPE,
				(void *)POWER_SUPPLY_TYPE_USB_CDP);
		switch_set_state(&switch_usb_uart, CABLE_USB_ATTACHED);
		break;

	case CABLE_TYPE_NONE:
		pr_info("USB detached : MUIC send switch state 101");
		usb_uart_switch_state = CABLE_USB_DETACHED;
		send_usb_insert_event(0);
		pr_info("%s USB CDP removed\n", __func__);
#ifdef CONFIG_SEC_CHARGING_FEATURE
		spa_event_handler(SPA_EVT_CHARGER, (void *) POWER_SUPPLY_TYPE_BATTERY);
#endif
		bcm_accy_event_handler(PMU_ACCY_EVT_OUT_CHRGR_TYPE,
				 (void *)POWER_SUPPLY_TYPE_BATTERY);
		switch_set_state(&switch_usb_uart, CABLE_USB_DETACHED);
		break;
	default:
		break;
	}
}

static void tsu6712_smartdock_cb(bool attached)
{
	pr_info("tsu6712_smartdock_cb attached %d\n", attached);
}

static void tsu6712_dock_cb(int attached)
{
	pr_info("%s attached %d\n", __func__, attached);
	switch_set_state(&switch_dock, attached);
}

static void tsu6712_ovp_cb(bool attached)
{
	pr_info("%s:%s\n",__func__,(attached?"TRUE":"FALSE"));
#ifdef CONFIG_SEC_CHARGING_FEATURE
	spa_event_handler(SPA_EVT_OVP, (void*) attached);
#endif
}

/* UUS - usb uart switch start */
static void tsu6712_usb_cb(bool attached)
{
	pr_info("tsu6712_usb_cb attached %d\n", attached);

	set_cable_status = attached ? CABLE_TYPE_USB : CABLE_TYPE_NONE;

#ifdef USE_USB_UART_SWITCH
		if(attached)
		{
			printk("USB attached : MUIC send switch state 100");
			usb_uart_switch_state = CABLE_USB_ATTACHED;
			switch_set_state(&switch_usb_uart, CABLE_USB_ATTACHED);
		}
		else
		{
			printk("USB detached : MUIC send switch state 101");
			usb_uart_switch_state = CABLE_USB_DETACHED;
			switch_set_state(&switch_usb_uart, CABLE_USB_DETACHED);
		}
#endif
	switch (set_cable_status) {
	case CABLE_TYPE_USB:
#ifdef CONFIG_SEC_CHARGING_FEATURE
		spa_event_handler(SPA_EVT_CHARGER, (void*) POWER_SUPPLY_TYPE_USB);
#endif
		bcm_accy_event_handler(PMU_ACCY_EVT_OUT_CHRGR_TYPE,
				(void *)POWER_SUPPLY_TYPE_USB);
		send_usb_insert_event(1);
		pr_info("%s USB attached\n",__func__);
		break;

	case CABLE_TYPE_NONE:
#ifdef CONFIG_SEC_CHARGING_FEATURE
		spa_event_handler(SPA_EVT_CHARGER, (void*) POWER_SUPPLY_TYPE_BATTERY);
#endif
		bcm_accy_event_handler(PMU_ACCY_EVT_OUT_CHRGR_TYPE,
				 (void *)POWER_SUPPLY_TYPE_BATTERY);
		send_usb_insert_event(0);
		pr_info("%s USB removed\n",__func__);
		break;
	default:
		break;
	}
}
/*UUS - usb uart switch end */
//#define FG_TEST

static void tsu6712_charger_cb(bool attached)
{
	pr_info("tsu6712_charger_cb attached %d\n", attached);
	set_cable_status = attached ? CABLE_TYPE_AC : CABLE_TYPE_NONE;

	switch (set_cable_status) {
	case CABLE_TYPE_AC:
#ifdef FG_TEST
		usb_uart_switch_state = CABLE_AC_ATTACHED;
		switch_set_state(&switch_usb_uart, CABLE_AC_ATTACHED);
#endif
#ifdef CONFIG_SEC_CHARGING_FEATURE
		spa_event_handler(SPA_EVT_CHARGER, (void*) POWER_SUPPLY_TYPE_USB_DCP);
#endif
		bcm_accy_event_handler(PMU_ACCY_EVT_OUT_CHRGR_TYPE,
				(void *)POWER_SUPPLY_TYPE_USB_DCP);
		send_usb_insert_event(0);
		pr_info("%s TA attached\n",__func__);
		break;
	case CABLE_TYPE_NONE:
#ifdef FG_TEST
		usb_uart_switch_state = CABLE_AC_DETACHED;
		switch_set_state(&switch_usb_uart, CABLE_AC_DETACHED);
#endif
#ifdef CONFIG_SEC_CHARGING_FEATURE
		spa_event_handler(SPA_EVT_CHARGER, (void*) POWER_SUPPLY_TYPE_BATTERY);
#endif
		bcm_accy_event_handler(PMU_ACCY_EVT_OUT_CHRGR_TYPE,
				 (void *)POWER_SUPPLY_TYPE_BATTERY);
		pr_info("%s TA removed\n",__func__);
		break;
	default:
		break;
	}
}

static void tsu6712_jig_cb(bool attached)
{
	pr_info("tsu6712_jig_cb attached %d\n", attached);
	set_cable_status = CABLE_TYPE_NONE;
}

static void tsu6712_uart_cb(bool attached)
{
	pr_info("tsu6712_uart_cb attached %d\n", attached);
	set_cable_status = CABLE_TYPE_NONE;

#ifdef USE_USB_UART_SWITCH
   if(attached)
   {
      printk("UART attached : send switch state 200");
      usb_uart_switch_state = CABLE_UART_ATTACHED;
      switch_set_state(&switch_usb_uart, CABLE_UART_ATTACHED);
   }
   else
   {
      printk("UART detached : send switch state 201");
      usb_uart_switch_state = CABLE_UART_DETACHED;
      switch_set_state(&switch_usb_uart, CABLE_UART_DETACHED);
   }
#endif

#ifndef CONFIG_SEC_MAKE_LCD_TEST
	if(attached)
		wake_lock(&acc_wakelock);
	else
		wake_unlock(&acc_wakelock);
#endif
}

void jig_force_sleep(void)
{
#ifdef CONFIG_HAS_WAKELOCK
	if(wake_lock_active(&acc_wakelock))
	{
		wake_unlock(&acc_wakelock);
		pr_info("Force unlock jig_wakelock\n");
	}
#else
	pr_info("Warning : %s - Empty function!!!\n", __func__);
#endif
}
EXPORT_SYMBOL(jig_force_sleep);

static struct tsu6712_platform_data tsu6712_pdata = {
	.usb_cb = tsu6712_usb_cb,
	.charger_cb = tsu6712_charger_cb,
	.jig_cb = tsu6712_jig_cb,
	.uart_cb = tsu6712_uart_cb,
	.otg_cb = tsu6712_otg_cb,
	.ovp_cb = tsu6712_ovp_cb,
	.dock_cb = tsu6712_dock_cb,
	.ex_init = tsu6712_ex_init,
	.usb_cdp_cb = tsu6712_usb_cdp_cb,
	.smartdock_cb = tsu6712_smartdock_cb,
};
#endif

static ssize_t tsu6712_show_control(struct device *dev,
struct device_attribute *attr,
	char *buf)
{
	struct tsu6712_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	u8 value;

	tsu6712_read_reg(client,TSU6712_REG_CTRL,&value);

	return snprintf(buf, 13, "CONTROL: %02x\n", value);
}

static ssize_t tsu6712_show_device_type(struct device *dev,
struct device_attribute *attr,
	char *buf)
{
	struct tsu6712_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	u8 value;

	tsu6712_read_reg(client,TSU6712_REG_DEV_T1,&value);

	return snprintf(buf, 11, "DEVICE_TYPE: %02x\n", value);
}

static ssize_t tsu6712_show_manualsw(struct device *dev,
struct device_attribute *attr, char *buf)
{
	struct tsu6712_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	u8 value;

	tsu6712_read_reg(client,TSU6712_REG_MANSW1,&value);

	if (value == SW_VAUDIO)
		return snprintf(buf, 7, "VAUDIO\n");
	else if (value == SW_UART)
		return snprintf(buf, 5, "UART\n");
	else if (value == SW_AUDIO)
		return snprintf(buf, 6, "AUDIO\n");
	else if (value == SW_DHOST)
		return snprintf(buf, 6, "DHOST\n");
	else if (value == SW_AUTO)
		return snprintf(buf, 5, "AUTO\n");
	else
		return snprintf(buf, 4, "%x", value);
}

static ssize_t tsu6712_set_manualsw(struct device *dev,
struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct tsu6712_usbsw *usbsw = dev_get_drvdata(dev);
	struct i2c_client *client = usbsw->client;
	u8 value;
	unsigned int path = 0;
	int ret;

	tsu6712_read_reg(client,TSU6712_REG_CTRL,&value);

	if ((value & ~CON_MANUAL_SW) !=	(CON_SWITCH_OPEN | CON_RAW_DATA | CON_WAIT))
		return 0;

	if (!strncmp(buf, "VAUDIO", 6)) {
		path = SW_VAUDIO;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "UART", 4)) {
		path = SW_UART;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "AUDIO", 5)) {
		path = SW_AUDIO;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "DHOST", 5)) {
		path = SW_DHOST;
		value &= ~CON_MANUAL_SW;
	} else if (!strncmp(buf, "AUTO", 4)) {
		path = SW_AUTO;
		value |= CON_MANUAL_SW;
	} else {
		dev_err(dev, "Wrong command\n");
		return 0;
	}

	usbsw->mansw = path;

	ret = tsu6712_write_reg(client, TSU6712_REG_MANSW1, path);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	ret = tsu6712_write_reg(client, TSU6712_REG_CTRL, value);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return count;
}

#ifdef USE_USB_UART_SWITCH
static ssize_t tsu6712_show_UUS_state(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	return snprintf(buf, 4, "%d\n", usb_uart_switch_state);
}

#define	SWITCH_AT	103
#define	SWITCH_ISI	104

/* AT-ISI Separation starts */
#ifdef CONFIG_PHONET
extern int stop_isi;
#endif
static int isi_mode; /* initialized to 0 */
char at_isi_mode[100] = {0};

static ssize_t ld_show_mode(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	strcpy(buf, at_isi_mode);
	printk("LD MODE from TSU %s\n", at_isi_mode);
	return 3;
}

static ssize_t ld_show_manualsw(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

ssize_t ld_set_manualsw(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	printk(KERN_CRIT"ld_set_manualsw invoked\n");
	if (0 == strncmp(buf, "AT+ATSTART", 10)) {
		printk(KERN_CRIT"switch at %d\n", SWITCH_AT);
		memset((char *)at_isi_mode, 0, 100);
		strcpy((char *)at_isi_mode, "at");
		switch_set_state(&switch_usb_uart, SWITCH_AT);
#ifdef CONFIG_PHONET
		stop_isi = 1;
#endif
		isi_mode = 0;
	}
	if (0 == strncmp(buf, "AT+MODECHAN=0,2", 15)) {
		printk(KERN_CRIT"modechan 0,2 %d\n", SWITCH_MODECHAN_02);
		memset((char *)at_isi_mode, 0, 100);
		strcpy((char *)at_isi_mode, "at");
		switch_set_state(&switch_usb_uart, SWITCH_MODECHAN_02);
#ifdef CONFIG_PHONET
		stop_isi = 1;
#endif
		isi_mode = 0;
	}

	if (0 == strncmp(buf, "AT+ISISTART", 11)) {
		printk(KERN_CRIT"switch isi %d\n", SWITCH_ISI);
		memset((char *)at_isi_mode, 0, 100);
		strcpy((char *)at_isi_mode, "isi");
		switch_set_state(&switch_usb_uart, SWITCH_ISI);
#ifdef CONFIG_PHONET
		stop_isi = 0;
#endif
		isi_mode = 1;
	}

	if (0 == strncmp(buf, "AT+MODECHAN=0,0", 15)) {
		printk(KERN_CRIT"modechan 0,0 %d\n", SWITCH_MODECHAN_00);
		memset((char *)at_isi_mode, 0, 100);
		strcpy((char *)at_isi_mode, "isi");
		switch_set_state(&switch_usb_uart, SWITCH_MODECHAN_00);
#ifdef CONFIG_PHONET
		stop_isi = 0;
#endif
		isi_mode = 1;
	}
	return count;
}

static ssize_t ld_show_switch_buf(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	strcpy(buf, at_isi_switch_buf);
	printk("BUF from TSU %s\n", at_isi_switch_buf);
	return strlen(at_isi_switch_buf);
}

ssize_t ld_set_switch_buf(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int i;
	char temp[100];
	char *ptr = NULL;
	char *ptr2 = NULL;
	char atbuf[] = "AT+ATSTART\r";
	char atmodechanbuf[] = "AT+MODECHAN=0,2\r";
	char isi_cmd_buf[] = "switch isi";
	int error = 0;

	/* If UART is not connected ignore this sysfs access*/
	if (CABLE_UART_ATTACHED != usb_uart_switch_state)
		return 0;

	memset(temp, 0, 100);
	for (i = 0; i < count; i++)
		temp[i] = toupper(buf[i]);

	/* This will clear any partial commands due to intermediate inputs */
	if ((!strcmp("AT+MODECHAN=0, 0", buf)) \
			 || (!strcmp("AT+ISISTART", buf))) {
		memset(at_isi_switch_buf, 0, 400);
	}

	strncat((char *)at_isi_switch_buf, temp, count);

	if ((strncmp((char *)at_isi_switch_buf, "\n", 1) == 0) || \
	    (strncmp((char *)at_isi_switch_buf, "\r", 1) == 0) || \
	    (strncmp((char *)at_isi_switch_buf, "\r\n", 2) == 0)) {
		memset(at_isi_switch_buf, 0, 400);
		KERNEL_LOG = 0;
		return UART_EMPTY_CRLF;
	}

	if (strstr(at_isi_switch_buf, "\r\n"))
		printk("###TEST0### r n\n");
	else if (strstr(at_isi_switch_buf, "\t\n"))
		printk("###TEST1### t n\n");
	else if (strstr(at_isi_switch_buf, "\n"))
		printk("###TEST2### n\n");

	ptr = strstr(atbuf, at_isi_switch_buf);
	ptr2 = strstr(atmodechanbuf, at_isi_switch_buf);
	if ( ((NULL == ptr) || (ptr != atbuf)) &&
	     ((NULL == ptr2) || (ptr2 != atmodechanbuf)) ) {
		if (strstr("AT+ISISTART", at_isi_switch_buf) == NULL &&
		    strstr("AT+MODECHAN=0,0", at_isi_switch_buf) == NULL)
			error = 1;
	}

	if (strstr(at_isi_switch_buf, atbuf) != NULL) {
		KERNEL_LOG = 0;
		memset(at_isi_switch_buf, 0, 400);
		return UART_AT_MODE;
	} else if (strstr(at_isi_switch_buf, atmodechanbuf) != NULL) {
		KERNEL_LOG = 0;
		memset(at_isi_switch_buf, 0, 400);
		return UART_AT_MODE_MODECHAN;
	} else if (strstr(at_isi_switch_buf, "AT+ISISTART") != NULL ||
		   strstr(at_isi_switch_buf, "AT+MODECHAN=0,0") != NULL) {
		/*do not switch to isi mode if isi mode already set*/
		if (isi_mode == 0) {
			KERNEL_LOG = 0;

			ld_set_manualsw(NULL, NULL, at_isi_switch_buf,
				strlen(at_isi_switch_buf));
			memset(at_isi_switch_buf, 0, 400);
			return count;
		}
	}
	/* this sends response if at+isistart is given in isi mode */
	if (strstr(at_isi_switch_buf, "AT+ISISTART\r") != NULL ||
		strstr(at_isi_switch_buf, "AT+MODECHAN=0,0\r") != NULL) {
		memset(at_isi_switch_buf, 0, 400);
		ld_set_manualsw(NULL, NULL, isi_cmd_buf, strlen(isi_cmd_buf));
		return UART_INVALID_MODE;
	}
	if (error != 0) {
		count = UART_INVALID_MODE;
		memset(at_isi_switch_buf, 0, 400);
	}

	return count;
}
#endif

int get_kernel_log_status(void)
{
	return KERNEL_LOG;
}
EXPORT_SYMBOL(get_kernel_log_status);
#if defined(CONFIG_SEC_COMMON)
static ssize_t adc_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	struct tsu6712_usbsw *usbsw = dev_get_drvdata(dev);
	u8 val;

	tsu6712_read_reg(usbsw->client, TSU6712_REG_ADC, &val);

	return sprintf(buf, "%x\n", val);
}
#endif

/* JIRA ID 1362/1396
Sysfs interface to release and acquire
uart-wakelock from user space */
ssize_t ld_uart_wakelock(struct device *dev,
                                    struct device_attribute *attr,
                                    const char *buf, size_t count)
{
	int ret;
	int buf_val = 0;

	ret = sscanf(buf, "%d", &buf_val);
	if (1 != ret) {
	      printk(KERN_ERR \
	              "ld_uart_wakelock - Failed to read value\n");
	      return -EINVAL;
	}
	if (buf_val == 1) {
	      /* Release wakelock
	      to allow device to get into deep sleep
		   when UART JIG is disconnected
		   */
		pr_err("release jig_wakelock\n");
		atomic_set(&jig_force_rel_wklock, 1);
	      wake_unlock(&acc_wakelock);
	      ret = count ;
	} else if (buf_val == 0) {
	      /* Acquire wakelock
	      to avoid device getting into deep sleep
	      when UART JIG is connected*/
		pr_err("hold jig_wakelock\n");
	      atomic_set(&jig_force_rel_wklock, 0);
	      wake_lock(&acc_wakelock);
	      ret = count ;
	}

	return ret ;
}
#if defined(CONFIG_SEC_COMMON)
static DEVICE_ATTR(adc, S_IRUGO, adc_show, NULL);
#endif
static DEVICE_ATTR(control, S_IRUGO, tsu6712_show_control, NULL);
static DEVICE_ATTR(device_type, S_IRUGO, tsu6712_show_device_type, NULL);
static DEVICE_ATTR(switch, S_IRUGO | S_IWUSR,	tsu6712_show_manualsw, tsu6712_set_manualsw);
//static DEVICE_ATTR(usb_state, S_IRUGO, tsu6712_show_usb_state, NULL);
//static DEVICE_ATTR(adc, S_IRUGO, tsu6712_show_adc, NULL);
//static DEVICE_ATTR(reset_switch, S_IWUSR | S_IWGRP, NULL, tsu6712_reset);
#ifdef USE_USB_UART_SWITCH
/* AT-ISI Separation starts */
static DEVICE_ATTR(at_isi_switch, S_IRUGO | S_IWUSR, ld_show_manualsw, ld_set_manualsw);
static DEVICE_ATTR(at_isi_mode, S_IRUGO | S_IWUSR, ld_show_mode, NULL);
static DEVICE_ATTR(at_isi_switch_buf, S_IRUGO | S_IWUSR,	ld_show_switch_buf, ld_set_switch_buf);
/* AT-ISI Separation Ends */
static DEVICE_ATTR(UUS_state, S_IRUGO, tsu6712_show_UUS_state, NULL);
#endif
/* JIRA ID 1362/1396
Sysfs interface to release and acquire uart-wakelock from user space */
static DEVICE_ATTR(uart_wakelock, S_IRUGO | S_IWUSR, NULL, ld_uart_wakelock);

static struct attribute *tsu6712_attributes[] = {
	&dev_attr_control.attr,
	&dev_attr_device_type.attr,
	&dev_attr_switch.attr,
#ifdef USE_USB_UART_SWITCH
	&dev_attr_at_isi_switch.attr,	/* AT-ISI Separation */
	&dev_attr_at_isi_mode.attr,		/* AT-ISI Separation */
	&dev_attr_at_isi_switch_buf.attr,	/* AT-ISI Separation */
	&dev_attr_UUS_state.attr,
#endif
	/* JIRA ID 1362/1396
	uart-wakelock release */
	&dev_attr_uart_wakelock.attr,
	NULL
};

static const struct attribute_group tsu6712_group = {
	.attrs = tsu6712_attributes,
};

#define TSU6712_RESET_REG_MASK 0x0a
void tsu6712_manual_switching(int path)
{
	struct i2c_client *client = local_usbsw->client;
	u8 value;
	unsigned int data = 0;
	int ret;

	tsu6712_read_reg(client,TSU6712_REG_CTRL,&value);

	if ((value & ~CON_MANUAL_SW) !=	(CON_SWITCH_OPEN | CON_RAW_DATA | CON_WAIT))
		return;

	if (path == SWITCH_PORT_VAUDIO) {
		data = SW_VAUDIO;
		value &= ~CON_MANUAL_SW;
	} else if (path ==  SWITCH_PORT_UART) {
		data = SW_UART;
		value &= ~CON_MANUAL_SW;
	} else if (path ==  SWITCH_PORT_AUDIO) {
		data = SW_AUDIO;
		value &= ~CON_MANUAL_SW;
	} else if (path ==  SWITCH_PORT_USB) {
		data = SW_DHOST;
		value &= ~CON_MANUAL_SW;
	} else if (path ==  SWITCH_PORT_AUTO) {
		data = SW_AUTO;
		value |= CON_MANUAL_SW;
	} else if (path ==  SWITCH_PORT_USB_OPEN) {
		data = SW_USB_OPEN;
		value &= ~CON_MANUAL_SW;
	} else if (path ==  SWITCH_PORT_ALL_OPEN) {
		data = SW_ALL_OPEN;
		value &= ~CON_MANUAL_SW;
	} else {
		pr_info("%s: wrong path (%d)\n", __func__, path);
		return;
	}

	local_usbsw->mansw = data;

	/* path for FTM sleep */
	if (path ==  SWITCH_PORT_ALL_OPEN) {
		ret = tsu6712_write_reg(client, TSU6712_REG_RESET, TSU6712_RESET_REG_MASK);
		if (ret < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);

		ret = tsu6712_write_reg(client,	TSU6712_REG_MANSW1, data);
		if (ret < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);

		ret = tsu6712_write_reg(client,	TSU6712_REG_MANSW2, data);
		if (ret < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);

		ret = tsu6712_write_reg(client,	TSU6712_REG_CTRL, value);
		if (ret < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	} else {
		ret = tsu6712_write_reg(client,	TSU6712_REG_MANSW1, data);
		if (ret < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);

		ret = tsu6712_write_reg(client,	TSU6712_REG_CTRL, value);
		if (ret < 0)
			dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	}
}
EXPORT_SYMBOL(tsu6712_manual_switching);


static u8 IntrMask;
static u8 DcdTimeountCount;
//static u8 DcdTimeountMask;
static u8 DcdTimeounStatus;
#define DEVICE_TYPE_MASK 0xFF
#define DEVICE_BIT_MASK 8
#define INETRRUPT_BITS_MASK ((1<<6)|(1<<0))
#define INTERRUPT_CHARGER_MASK 0x00
static void tsu6712_detect_dev(struct tsu6712_usbsw *usbsw, u32 device_type, u8 adc_val)
{
//	int ret;
	unsigned char val1, val2, adc, other_cable;
	struct tsu6712_platform_data *pdata = usbsw->pdata;
	struct i2c_client *client = usbsw->client;

	val1 = device_type & DEVICE_TYPE_MASK;
	val2 = (device_type >> DEVICE_BIT_MASK) & DEVICE_TYPE_MASK;
	adc = adc_val;

	pr_info("%s : dev1:0x%x, dev2:0x%x, adc:0x%x,\n", __func__, val1, val2, adc);


/*	if (adc == 0x10)
		val2 = DEV_SMARTDOCK;*/

	/* Attached */
	if (val1 || val2) {
		/* USB */
		if (val1 & DEV_USB || val2 & DEV_T2_USB_MASK) {
			dev_info(&client->dev, "usb connect\n");
			if (pdata->usb_cb)
				pdata->usb_cb(TSU6712_ATTACHED);
		}/* USB_CDP */
		else if (val1 & DEV_USB_CHG) {
			dev_info(&client->dev, "usb_cdp connect\n");
			if (pdata->usb_cdp_cb)
				pdata->usb_cdp_cb(TSU6712_ATTACHED);
		}/* UART */
		else if (val1 & DEV_T1_UART_MASK || val2 & DEV_T2_UART_MASK) {
			dev_info(&client->dev, "uart connect\n");
			/* temp code for rt8973 bugs */
			if(usbsw->rev < 1) {
				tsu6712_read_reg(client, TSU6712_REG_INT1_MASK, &IntrMask);
				tsu6712_write_reg(client, TSU6712_REG_INT1_MASK, IntrMask|INETRRUPT_BITS_MASK);
			}
			if (pdata->uart_cb)
				pdata->uart_cb(TSU6712_ATTACHED);
		}/* CHARGER */
		else if (val1 & DEV_T1_CHARGER_MASK) {
			dev_info(&client->dev, "connect\n");
			if ((val1 & DEV_CAR_KIT)
				&& (adc == ADC_CEA936ATYPE1_CHG)) {
				tsu6712_read_reg(client,
					TSU6712_REG_CTRL, &other_cable);
				dev_info(&client->dev,
				"%s : other_cable:0x%x\n", __func__,
					other_cable);
				other_cable &= ~CON_MANUAL_SW;
				tsu6712_write_reg(client,
					TSU6712_REG_CTRL, other_cable);
				dev_info(&client->dev,
				"%s : other_cable:0x%x\n", __func__,
					other_cable);
				tsu6712_write_reg(client,
					TSU6712_REG_MANSW1, SW_EN_USB_CONN);
				dev_info(&client->dev, "usb connect_other\n");
				if (pdata->usb_cb)
					pdata->usb_cb(TSU6712_ATTACHED);
				}
			else if (pdata->charger_cb) {
				dev_info(&client->dev, "charger connect\n");
				pdata->charger_cb(TSU6712_ATTACHED);
			}
		}/* JIG */
		else if (val2 & DEV_T2_JIG_MASK) {
			dev_info(&client->dev, "jig connect\n");
			if (pdata->jig_cb)
				pdata->jig_cb(TSU6712_ATTACHED);
		}/* unknown acc -->dock*/
		else if (val2 & DEV_UNKNOWN_ACC) {
			dev_info(&client->dev, "unknown acc: dock connected\n");
			usbsw->attached_accy = ACC_UNKOWN_DOCK;
		}

		if(usbsw->ovp)
			pdata->ovp_cb(true);

	}/* Detached */
	else {
		/* USB */
		if (usbsw->dev1 & DEV_USB ||	usbsw->dev2 & DEV_T2_USB_MASK) {
			atomic_set(&jig_force_rel_wklock, 0);
			dev_info(&client->dev, "usb disconnect\n");
			if (pdata->usb_cb)
				pdata->usb_cb(TSU6712_DETACHED);
		}/* USB CDP */
		else if (usbsw->dev1 & DEV_USB_CHG) {
			atomic_set(&jig_force_rel_wklock, 0);
			dev_info(&client->dev, "usb_cdp disconnect\n");
			if (pdata->usb_cdp_cb)
				pdata->usb_cdp_cb(TSU6712_DETACHED);
		}/* UART */
		else if (usbsw->dev1 & DEV_T1_UART_MASK ||usbsw->dev2 & DEV_T2_UART_MASK) {
			atomic_set(&jig_force_rel_wklock, 0);
			dev_info(&client->dev, "uart disconnect\n");
			/* temp code for rt8973 bugs */
			if(usbsw->rev < 1)
				tsu6712_write_reg(client,TSU6712_REG_INT1_MASK, IntrMask);
			if (pdata->uart_cb)
				pdata->uart_cb(TSU6712_DETACHED);
		}/* CHARGER */
		else if (usbsw->dev1 & DEV_T1_CHARGER_MASK || usbsw->dev3 & DEV_T3_CHARGER_MASK) {
			atomic_set(&jig_force_rel_wklock, 0);
			dev_info(&client->dev, "charger disconnect\n");
			if(DcdTimeounStatus) {
				dev_info(&client->dev, "dcd time out charger unplugged\n");
				tsu6712_write_reg(client,TSU6712_REG_INT1_MASK, 0x00);
				DcdTimeounStatus = 0;
			}
			if ((usbsw->dev1 & DEV_CAR_KIT)
				&& (usbsw->adc == ADC_CEA936ATYPE1_CHG)) {
				dev_info(&client->dev, "other_usb_disconnect\n");
				if (pdata->usb_cb)
					pdata->usb_cb(TSU6712_DETACHED);
				}
			else if (pdata->charger_cb) {
				dev_info(&client->dev, "charger disconnect\n");
				pdata->charger_cb(TSU6712_DETACHED);
				}
		}/* JIG */
		else if (usbsw->dev2 & DEV_T2_JIG_MASK) {
			atomic_set(&jig_force_rel_wklock, 0);
			dev_info(&client->dev, "jig disconnect\n");
			if (pdata->jig_cb)
				pdata->jig_cb(TSU6712_DETACHED);
		}/* Desk Dock */
		else if( usbsw->dev2 & ACC_UNKOWN_DOCK) {
			atomic_set(&jig_force_rel_wklock, 0);
			dev_info(&client->dev, "unknown acc: dock disconnected\n");
			usbsw->attached_accy = ACC_NONE;
			if(usbsw->vbus)
				if (pdata->charger_cb)
					pdata->charger_cb(TSU6712_DETACHED);
		}
		tsu6712_read_reg(client, TSU6712_REG_CTRL, &other_cable);
		dev_info(&client->dev,
			"%s : other_cable:0x%x\n", __func__, other_cable);
		other_cable |= CON_MANUAL_SW;
		tsu6712_write_reg(client, TSU6712_REG_CTRL, other_cable);
		dev_info(&client->dev,
			"%s : other_cable:0x%x\n", __func__, other_cable);
		dev_info(&client->dev, "usb_disconnect_other\n");
	}

	usbsw->dev1 = val1;
	usbsw->dev2 = val2;
	usbsw->adc = adc;
}

#define TSU6712_CHIP_ID_MASK 3
#define TSU6712_RESET_MASK 0x1
#define TSU6712_REG_RST_BIT (0x08)
#define TSU6712_REG_CTRL_INT_MASK 0x1
static void tsu6712_reg_init(struct tsu6712_usbsw *usbsw)
{
	struct i2c_client *client = usbsw->client;
	u8 ctrl;
	u8 value;
	int ret;
	pr_info("%s\n", __func__);

	tsu6712_read_reg(client, TSU6712_REG_DEVID, &value);
	usbsw->rev = (int)(value >> TSU6712_CHIP_ID_MASK);
	pr_info("rt8973 chip rev is %x",usbsw->rev);

	tsu6712_write_reg(client, TSU6712_REG_RESET, TSU6712_RESET_MASK);
	msleep(10);

	tsu6712_read_reg(client, TSU6712_REG_CTRL, &value);
        if(usbsw->rev < 1) {
	value = (value | TSU6712_REG_RST_BIT);
        }
	ctrl = (value & (~TSU6712_REG_CTRL_INT_MASK));
	ret = tsu6712_write_reg(client, TSU6712_REG_CTRL, ctrl);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
}

void muic_set_vbus(int vbus)
{
	if(!local_usbsw)
		return ;

	pr_info("%s: VBUS detected %d\n", __func__, vbus);
	local_usbsw->vbus = vbus > 0 ? 1 : 0;
	schedule_delayed_work(&local_usbsw->vbus_work, msecs_to_jiffies(1000));
}
EXPORT_SYMBOL(muic_set_vbus);

static void tsu6712_vbus_func(struct work_struct *work)
{
	struct tsu6712_usbsw *usbsw = container_of(work, struct tsu6712_usbsw, vbus_work.work);
	struct tsu6712_platform_data *pdata = usbsw->pdata;

	pr_info("%s\n", __func__);

	if(!usbsw->ovp)
	{
		if(usbsw->vbus)
		{
			if(usbsw->attached_accy > ACC_NONE)
			{
				if (pdata->charger_cb)
					pdata->charger_cb(TSU6712_ATTACHED);
			}
		}
		else
		{
			if(usbsw->attached_accy > ACC_NONE)
			{
				if (pdata->charger_cb)
					pdata->charger_cb(TSU6712_DETACHED);
			}
		}
	}
}

#define TSU_DETECT_INTR_MASK 0xff
#define DCD_TIMEOUT_MASK 3
#define INTR1_REG_DETACH_MASK (0xfd)
#define OVP_INTR_MASK 0x10
static void tsu6712_detect_func(struct work_struct *work)
{
	struct tsu6712_usbsw *usbsw = container_of(work, struct tsu6712_usbsw, work);
	struct interrupt_element element;
	int intr = 0, ret;
	u32 dev = 0;
	u8 adc = 0,dcd_t, ctrl, value;

	pr_info("%s\n", __func__);

	memset(&element, 0, 1);
	if(tsu6712_pop_queue(&element)   >= 0)
	{
		intr = element.intr;

		dev = element.dev;
		adc = element.adc;
		pr_info("%s : intr=[0x%2x], dev=[0x%8x], adc=[0x%2x]\n", __func__, intr, dev, adc);
	}
	dcd_t = (u8)(intr & TSU_DETECT_INTR_MASK);
	if(dcd_t & INT_DCD_T) {
		DcdTimeountCount++;
		if (DcdTimeountCount < DCD_TIMEOUT_MASK) {
			tsu6712_read_reg(usbsw->client, TSU6712_REG_CTRL, &value);

			ctrl = value & ~(u8)(CON_USB_CHDN | CON_CHG_TYP);
			ret = tsu6712_write_reg(usbsw->client, TSU6712_REG_CTRL, ctrl);
			if (ret < 0)
				dev_err(&usbsw->client->dev, "%s: err %d\n", __func__, ret);

			msleep(20);
			ctrl |= (u8)(CON_USB_CHDN | CON_CHG_TYP);
			ret = tsu6712_write_reg(usbsw->client, TSU6712_REG_CTRL, ctrl);
			if (ret < 0)
				dev_err(&usbsw->client->dev, "%s: err %d\n", __func__, ret);
			return;
			} else {
				if(DcdTimeounStatus == 0) {
					pr_info("%s dcd time out mask interrupt\n", __func__);
					//tsu6712_read_reg(usbsw->client,TSU6712_REG_INT1_MASK, &DcdTimeountMask); //DcdTimeountMask-->0
					tsu6712_write_reg(usbsw->client, TSU6712_REG_INT1_MASK, INTR1_REG_DETACH_MASK); /*1111 1101*/
				}
				DcdTimeounStatus = 1;
				dev = DEV_DEDICATED_CHG;
			}

		}

	DcdTimeountCount = 0;
	
	tsu6712_detect_dev(usbsw, dev, adc);

	if (intr & OVP_INTR_MASK) /* ovp */
	{
		printk("ovp enable !!\n");
		usbsw->ovp = 1;
		usbsw->pdata->ovp_cb(true);
	} else if (((intr & OVP_INTR_MASK) == 0) && usbsw->ovp == 1)
	{
		printk("ovp disable !!\n");
		usbsw->ovp = 0;
		usbsw->pdata->ovp_cb(false);
	}
}


static void tsu6712_irq_func(struct work_struct *work)
{
	struct tsu6712_usbsw *usbsw = container_of(work, struct tsu6712_usbsw, dev_irq_work.work);
	struct i2c_client *client = usbsw->client;
	int intr, dev;
	u8 adc;
	struct interrupt_element element;

	tsu6712_read_word_reg(client, TSU6712_REG_INT1, &intr);	// 2 byte
	tsu6712_read_word_reg(client, TSU6712_REG_DEV_T1, &dev);	// 2 byte
	tsu6712_read_reg(client, TSU6712_REG_ADC, &adc);

	element.dev = dev;
	element.intr = intr;
	element.adc = adc;

	tsu6712_push_queue(&element);
	schedule_work(&usbsw->work);
}

static irqreturn_t tsu6712_irq_thread(int irq, void *data)
{
	struct tsu6712_usbsw *usbsw = data;

	pr_info("%s\n", __func__);

	schedule_delayed_work(&usbsw->dev_irq_work, msecs_to_jiffies(50));

	return IRQ_HANDLED;
}

static int tsu6712_irq_init(struct tsu6712_usbsw *usbsw)
{
	struct i2c_client *client = usbsw->client;
	int ret;

	if (client->irq) {
		ret =  request_irq(client->irq, tsu6712_irq_thread,
			(IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND | IRQF_ONESHOT),
			"tsu6712 micro USB", usbsw);
		if (ret) {
			dev_err(&client->dev, "failed to reqeust IRQ\n");
			return ret;
		}

		ret = enable_irq_wake(client->irq);
		if (ret < 0)
			dev_err(&client->dev,"failed to enable wakeup src %d\n", ret);
	}
	else
		return 1;

	return 0;
}

static void tsu6712_init_func(struct work_struct *work)
{
	int ret;
	struct tsu6712_usbsw *usbsw = container_of(work, struct tsu6712_usbsw, init_work.work);

	ret = tsu6712_irq_init(usbsw);
	if (ret)
	{
		dev_err(&usbsw->client->dev,"failed to enable  irq init %s\n", __func__);
		return ;
	}

	/* initial cable detection */
	tsu6712_irq_thread(0, usbsw);
}

#define USB_FS	"usb_atparser"

static int tsu6712_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct tsu6712_usbsw *usbsw;
	struct input_dev *input;
#if defined(CONFIG_SEC_COMMON)
	struct device *switch_dev;
#endif
	int ret = 0;

	pr_info("%s\n", __func__);

	if (!i2c_check_functionality(to_i2c_adapter(client->dev.parent), I2C_FUNC_I2C))
		return -EIO;

	input = input_allocate_device();
	usbsw = kzalloc(sizeof(struct tsu6712_usbsw), GFP_KERNEL);
	if (!usbsw || !input) {
		dev_err(&client->dev, "failed to allocate driver data\n");
		input_free_device(input);
		kfree(usbsw);
		return -ENOMEM;
	}

	usbsw->input = input;
	input->name = client->name;
	input->phys = "deskdock-key/input0";
	input->dev.parent = &client->dev;
	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0001;

	/* Enable auto repeat feature of Linux input subsystem */
	__set_bit(EV_REP, input->evbit);
	input_set_capability(input, EV_KEY, KEY_VOLUMEUP);
	input_set_capability(input, EV_KEY, KEY_VOLUMEDOWN);
	input_set_capability(input, EV_KEY, KEY_PLAYPAUSE);
	input_set_capability(input, EV_KEY, KEY_PREVIOUSSONG);
	input_set_capability(input, EV_KEY, KEY_NEXTSONG);

	ret = input_register_device(input);
	if (ret)
		dev_err(&client->dev,"input_register_device %s: err %d\n", __func__, ret);

	usbsw->client = client;
	if(client->dev.platform_data)
		usbsw->pdata = client->dev.platform_data;
	else
		usbsw->pdata = &tsu6712_pdata;

	if (!usbsw->pdata)
		goto fail1;

	i2c_set_clientdata(client, usbsw);
	mutex_init(&usbsw->mutex_lock);
	local_usbsw = usbsw;
	usbsw->dev1 = 0;
	usbsw->dev2 = 0;
	usbsw->dev3 = 0;
	usbsw->dev4 = 0;
	usbsw->mansw = 0;
	usbsw->ovp = 0;
	usbsw->previous_key = 0;
	usbsw->adc = 0;
	usbsw->attached_accy = ACC_NONE;
	usbsw->mhl_ret = 0;
	usbsw->vbus = 0;
	set_cable_status = CABLE_TYPE_NONE;

#if defined(CONFIG_SEC_COMMON)
	switch_dev = device_create(sec_class, NULL, 0, NULL, "switch");

	if (device_create_file(switch_dev, &dev_attr_adc) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_adc.attr.name);

	dev_set_drvdata(switch_dev,usbsw);
#endif

	ret = sysfs_create_group(&client->dev.kobj, &tsu6712_group);
	if (ret) {
		dev_err(&client->dev,"failed to create tsu6712 attribute group\n");
		goto fail2;
	}


	ret = sysfs_create_link(kernel_kobj, &client->dev.kobj, USB_FS);
	if (ret) {
		pr_err("%s sysfs_create_link fail\n", __func__);
		goto fail2;
	}

	wake_lock_init(&acc_wakelock, WAKE_LOCK_SUSPEND, "jig_wakelock");

	if (MUIC_IS_PRESENT) {
		INIT_DELAYED_WORK(&usbsw->vbus_work, tsu6712_vbus_func);
		INIT_DELAYED_WORK(&usbsw->init_work, tsu6712_init_func);
		INIT_DELAYED_WORK(&usbsw->dev_irq_work, tsu6712_irq_func);
		INIT_WORK(&usbsw->work, tsu6712_detect_func);
		tsu6712_reg_init(usbsw);
	}
	if(usbsw->pdata->ex_init)
		usbsw->pdata->ex_init();
	if (calib_selected_interface == 1) {
		printk("FAKEMUIC : Calling uart_cb");
		tsu6712_uart_cb(TSU6712_ATTACHED);
	}
	if (MUIC_IS_PRESENT)
	schedule_delayed_work(&usbsw->init_work, msecs_to_jiffies(1500));
	else {
		pr_info("Fake uart attach\n");
		usb_uart_switch_state = CABLE_UART_ATTACHED;
		switch_set_state(&switch_usb_uart, CABLE_UART_ATTACHED);
	}
	return 0;
fail2:
	sysfs_remove_group(&client->dev.kobj, &tsu6712_group);
	mutex_destroy(&usbsw->mutex_lock);
	i2c_set_clientdata(client, NULL);
fail1:
	input_unregister_device(input);
	input_free_device(input);
	kfree(usbsw);
	return ret;
}

static int __init calib_sel_if_setup(char *str)
{
	unsigned mode = memparse(str, &str);
	if (mode) {
		calib_selected_interface = mode;
	} else {
		calib_selected_interface = 0;
	}
	return 0;
}
early_param("calib_if", calib_sel_if_setup);

void inform_muic_usb_event(int event)
{
	if (event == 1) {
		usb_uart_switch_state = CABLE_UART_DETACHED;
		switch_set_state(&switch_usb_uart, CABLE_UART_DETACHED);
		printk("%s USB attached : MUIC send switch state 100\n",__func__);
		usb_uart_switch_state = CABLE_USB_ATTACHED;
		switch_set_state(&switch_usb_uart, CABLE_USB_ATTACHED);
	}
	if (event == 0) {
		printk("%s USB detached : MUIC send switch state 101\n",__func__);
		usb_uart_switch_state = CABLE_USB_DETACHED;
		switch_set_state(&switch_usb_uart, CABLE_USB_DETACHED);
		usb_uart_switch_state = CABLE_UART_ATTACHED;
		switch_set_state(&switch_usb_uart, CABLE_UART_ATTACHED);
	}
}
EXPORT_SYMBOL_GPL(inform_muic_usb_event);

static int tsu6712_remove(struct i2c_client *client)
{
	struct tsu6712_usbsw *usbsw = i2c_get_clientdata(client);

	cancel_work_sync(&usbsw->work);
	if (client->irq) {
		disable_irq_wake(client->irq);
		free_irq(client->irq, usbsw);
	}
	sysfs_remove_group(&client->dev.kobj, &tsu6712_group);
	mutex_destroy(&usbsw->mutex_lock);
	i2c_set_clientdata(client, NULL);
	input_unregister_device(usbsw->input);
	input_free_device(usbsw->input);
	sysfs_delete_link(kernel_kobj, &client->dev.kobj, USB_FS);
	kfree(usbsw);
	return 0;
}

static int tsu6712_suspend(struct i2c_client *client,
	pm_message_t state)
{
	return 0;
}

static int tsu6712_resume(struct i2c_client *client)
{
	struct tsu6712_usbsw *usbsw = i2c_get_clientdata(client);
	pr_info("In tsu6712_resume\n");

	if (atomic_read(&jig_force_rel_wklock))
		return 0;

	if ((usbsw->dev1 & DEV_T1_UART_MASK) || (usbsw->dev2 & DEV_T2_UART_MASK)) {
		if (!wake_lock_active(&acc_wakelock))
			wake_lock(&acc_wakelock);
	}
	return 0;
}

static const struct i2c_device_id tsu6712_id[] = {
	{"rt8973", 0},
	{}
};

static struct i2c_driver tsu6712_i2c_driver = {
	.driver = {
		.name = "rt8973",
	},
	.probe = tsu6712_probe,
	.remove = tsu6712_remove,
	.suspend = tsu6712_suspend,
	.resume = tsu6712_resume,
	.id_table = tsu6712_id,
};

static int __init tsu6712_init(void)
{
	int ret;

	ret = i2c_add_driver(&tsu6712_i2c_driver);

	if(ret < 0)
		pr_err("rt8973 i2c_add_driver fail\n");

	return ret;
}

static void __exit tsu6712_exit(void)
{
	i2c_del_driver(&tsu6712_i2c_driver);
}

late_initcall(tsu6712_init);
module_exit(tsu6712_exit);

MODULE_AUTHOR("SAMSUNG");
MODULE_DESCRIPTION("TSU6712 USB Switch driver");
MODULE_LICENSE("GPL");
