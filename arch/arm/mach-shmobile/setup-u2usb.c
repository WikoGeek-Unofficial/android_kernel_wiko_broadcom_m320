#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/hwspinlock.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/r8a7373.h>
#include <mach/irqs.h>
#include <asm/hardware/cache-l2x0.h>
#include <linux/gpio_keys.h>
#include <linux/usb/r8a66597.h>
#include <mach/setup-u2usb.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/usb/tusb1211.h>

#define error_log(fmt, ...) pr_err(fmt, ##__VA_ARGS__)

#define TUSB_VENDOR_SPECIFIC1 0x80
#define VBUS_RETURN_VAL (val1>>7)
#define USBHS_DMAC_BIT (1 << 14)
#define USBHS_BIT (1 << 22)
#define SUSPEND_MON_BIT (1 << 14)
#define PRESET_BIT (1 << 13)
#define IDPULLUP_BIT (1 << 8)
#define PHY_HANG_CTR 100

static int is_vbus_powered(void)
{
	int val = 0;
	int val1 = 0;
	int count = 10;

	/* Extract bit VBSTS in INTSTS0 register */
	val = __raw_readw(HSUSB_INTSTS0) & VBSTS;

	while (--count) {
		msleep(1);
		val1 = __raw_readw(HSUSB_INTSTS0) & VBSTS;
		if (val != val1) {
			count = 10;
			val = val1;
		}
	}

	printk(KERN_INFO "Value of Status register INTSTS0: %x\n",
			__raw_readw(HSUSB_INTSTS0));
	return VBUS_RETURN_VAL;
}

#define LOCK_TIME_OUT_MS 1000
static void usbhs_module_reset(void)
{
	unsigned long flags = 0;
	int ret = 0;

	static int ctr;
/* To avoid EYE-diagram issue DRV-STR recommended by HW - team */
#define TUSB_MAX_DRV_STR 0x4f
#define TUSB_EOS_DRV_STR 0x43

	ret = hwspin_lock_timeout_irqsave(r8a7373_hwlock_cpg,
		LOCK_TIME_OUT_MS, &flags);
	if (ret < 0)
		printk(KERN_INFO "Can't lock hwlock_cpg\n");
	else {
		__raw_writel(__raw_readl(SRCR2) |
				USBHS_DMAC_BIT, SRCR2); /* USBHS-DMAC */
		__raw_writel(__raw_readl(SRCR3) | USBHS_BIT, SRCR3); /* USBHS */
		hwspin_unlock_irqrestore(r8a7373_hwlock_cpg, &flags);
	}
	udelay(50); /* wait for at least one EXTALR cycle */
	ret = hwspin_lock_timeout_irqsave(r8a7373_hwlock_cpg,
		LOCK_TIME_OUT_MS, &flags);
	if (ret < 0)
		printk(KERN_INFO "Can't lock hwlock_cpg\n");
	else {
		__raw_writel(__raw_readl(SRCR2) & ~USBHS_DMAC_BIT, SRCR2);
		__raw_writel(__raw_readl(SRCR3) & ~USBHS_BIT, SRCR3);
		hwspin_unlock_irqrestore(r8a7373_hwlock_cpg, &flags);
	}
	/* wait for SuspendM bit being cleared by hardware */
	while (!(__raw_readw(PHYFUNCTR) & SUSPEND_MON_BIT)) /* SUSMON */{
		mdelay(10);
		if (ctr++ > PHY_HANG_CTR) {
			printk(KERN_INFO "FATAL ERROR: PHY Hang and NOT COMING out from LP mode\n");
			printk(KERN_INFO "Recover from FATAL ERROR\n");
			ctr = 0;
			break;
		}
	}
	__raw_writew(__raw_readw(PHYFUNCTR) |
		PRESET_BIT, PHYFUNCTR); /* PRESET */
	while (__raw_readw(PHYFUNCTR) & PRESET_BIT) {
		mdelay(10);
		if (ctr++ > PHY_HANG_CTR) {
			printk(KERN_INFO "FATAL ERROR: PHY Hang and NOT COMING out from LP mode\n");
			printk(KERN_INFO "Recover from FATAL ERROR\n");
			ctr = 0;
			break;
		}
	}

	if (is_otg_mode) {
		__raw_writew(__raw_readw(PHYOTGCTR) |
		IDPULLUP_BIT, PHYOTGCTR); /* IDPULLUP */
		msleep(50);
	}
//Eye Diagram
		__raw_writew(PHY_SPADDR_INIT, USB_SPADDR);       /* set HSUSB.SPADDR*/
		__raw_writew(PHY_VENDOR_SPECIFIC_ADDR_MASK, USB_SPEXADDR);     /* set HSUSB.SPEXADDR*/
		/* To fix EYE-diagram issue set maximum drive strength */
		if (MUIC_IS_PRESENT)
			__raw_writew(TUSB_MAX_DRV_STR, USB_SPWDAT);
		else
			__raw_writew(TUSB_EOS_DRV_STR, USB_SPWDAT);

		__raw_writew(USB_SPWR, USB_SPCTRL);     /* set HSUSB.SPCTRL*/
		mdelay(1);
//Eye Diagram
}

static void usb_sw_reset(void)
{
	unsigned long flags = 0;
	int ret = 0;

	ret = hwspin_lock_timeout_irqsave(r8a7373_hwlock_cpg,
		LOCK_TIME_OUT_MS, &flags);
	if (ret < 0)
		printk(KERN_INFO "Can't lock hwlock_cpg\n");
	else {
		__raw_writel(__raw_readl(SRCR2) |
			USBHS_DMAC_BIT, SRCR2); /* USBHS-DMAC */
		__raw_writel(__raw_readl(SRCR3) | USBHS_BIT, SRCR3); /* USBHS */
		hwspin_unlock_irqrestore(r8a7373_hwlock_cpg, &flags);
	}
	udelay(50); /* wait for at least one EXTALR cycle */
	ret = hwspin_lock_timeout_irqsave(r8a7373_hwlock_cpg,
		LOCK_TIME_OUT_MS, &flags);
	if (ret < 0)
		printk(KERN_INFO "Can't lock hwlock_cpg\n");
	else {
		__raw_writel(__raw_readl(SRCR2) & ~USBHS_DMAC_BIT, SRCR2);
		__raw_writel(__raw_readl(SRCR3) & ~USBHS_BIT, SRCR3);
		hwspin_unlock_irqrestore(r8a7373_hwlock_cpg, &flags);
	}
}


struct r8a66597_platdata usbhs_func_data_d2153 = {
	.is_vbus_powered = is_vbus_powered,
	.module_start	= usbhs_module_reset,
	.module_stop	= usb_sw_reset,
	.on_chip	= 1,
	.buswait	= 5,
	.max_bufnum	= 0xff,
	.dmac		= 1,
	.gpio_cs	= 130,
	.gpio_rst	= 131,
};

#ifdef CONFIG_USB_R8A66597_HCD
struct r8a66597_platdata usb_host_data = {
	.module_start	= usbhs_module_reset,
	.on_chip = 1,
};

#endif /*CONFIG_USB_R8A66597_HCD*/

/*TUSB1211 OTG*/
struct r8a66597_platdata tusb1211_data = {
	.module_start = usbhs_module_reset,
};


static bool is_muic;
bool muic_is_present(void)
{
	return is_muic;
}
EXPORT_SYMBOL_GPL(muic_is_present);

void __init usb_init(bool is_muic_present)
{
	is_muic = is_muic_present;
}
