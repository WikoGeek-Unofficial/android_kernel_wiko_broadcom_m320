/*
 * R8A66597 UDC (USB gadget)
 *
 * Copyright 2013  Broadcom Corporation
 * Copyright (C) 2006-2009 Renesas Solutions Corp.
 * Copyright (C) 2012 Renesas Mobile Corporation
 *
 * Author : Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/sh_clk.h>
#include <linux/pm_runtime.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>

#include <asm/dma.h>
#include <mach/hardware.h>
#include <mach/pm.h>
#include <mach/r8a7373.h>
#include <mach/setup-u2usb.h>
#include <linux/pm.h>

#include <linux/usb/otg.h>
#include <linux/usb/tusb1211.h>
#define USB_OTG_STATUS_SELECTOR  0xF000
#define USB_OTG_HOST_REQ_FLAG  0x0000

#include <linux/power_supply.h>
#ifdef CONFIG_SEC_CHARGING_FEATURE
#include <linux/spa_power.h>
#endif

#include <linux/bcm.h>


#include "r8a66597-udc.h"

#define DRIVER_VERSION	"2011-09-26"

#define DMA_ADDR_INVALID  (~(dma_addr_t)0)

#define error_log(fmt, ...) printk(fmt, ##__VA_ARGS__)

/* #define UDC_LOG */
#ifdef  UDC_LOG
#define udc_log(fmt, ...) printk(fmt, ##__VA_ARGS__)
#else
#define udc_log(fmt, ...)
#endif

#define DATA_CONTACT_DET_EN 0x10
#define CHGD_SERX_DP 0x2
#define CHGD_SERX_DM 1
#define DP_VSRC_EN 0x40
#define VDAT_DET 0x20
#define TUSB_SW_CONTROL	0x01
#define CHGD_IDP_SRC_EN	0x40
#define MAX_COUNT_DCD_TIMEOUT 270
#define PHY_READ_MASK 0x3F
#define FRAME_MASK 0x03FF
#define DMA_TIMEOUT 0x0027AC40
#define RECOVER_RESUME_COUNT 270
#define DCP_DETECT_MASK 0x3
#define MSEC_COUNT 1000000
#define INTSTS_REG_MASK 0xffff
#define PIPE_TIMEOUT_COUNT 100000

#define VBUS_HANDLE_IRQ_BASED
static int vbus_valid;
static int reset_resume_ctr;
static const char udc_name[] = "r8a66597_udc";
static const char usbhs_dma_name[] = "USBHS-DMA1";
static const char *r8a66597_ep_name[] = {
	"ep0", "ep1-bulk", "ep2-bulk", "ep3-bulk", "ep4-bulk", "ep5-bulk",
	"ep6-int", "ep7-int", "ep8-int",
#ifdef CONFIG_USB_R8A66597_TYPE_BULK_PIPES_12
	"ep9-bulk", "ep10-bulk", "ep11-bulk", "ep12-bulk", "ep13-bulk",
	"ep14-bulk", "ep15-bulk",
#else
	"ep9-int",
#endif
};

static struct cpufreq_lmt_node cpufreq_node;
static int dfs_started = -1;
static int r8a66597_set_vbus_draw(struct r8a66597 *r8a66597, int mamp);
static struct r8a66597_platdata *r8a66597_udc_of_init(
				struct platform_device *pdev);
void usb_drv_str_read(unsigned char *val)
{

	__raw_writew(PHY_SPADDR_INIT, USB_SPADDR);              /* set HSUSB.SPADDR */
	__raw_writew(PHY_VENDOR_SPECIFIC_ADDR_MASK, USB_SPEXADDR);    /* set HSUSB.SPEXADDR */
	__raw_writew(USB_SPRD, USB_SPCTRL);		/* set HSUSB.SPCTRL */
	mdelay(1);
	*val = __raw_readw(USB_SPRDAT);
}

static void usb_drv_str_write(unsigned char *val)
{
	u8 value= *val;
	__raw_writew(PHY_SPADDR_INIT, USB_SPADDR);		/* set HSUSB.SPADDR */
	__raw_writew(PHY_VENDOR_SPECIFIC_ADDR_MASK , USB_SPEXADDR);		/* set HSUSB.SPEXADDR */
	__raw_writew(value, USB_SPWDAT);       /* set HSUSB.SPWDAT */
	__raw_writew(USB_SPWR, USB_SPCTRL);		/* set HSUSB.SPCTRL */
	mdelay(1);
}

static void disable_controller(struct r8a66597 *r8a66597);
static void irq_ep0_write(struct r8a66597_ep *ep, struct r8a66597_request *req);
static void irq_packet_write(struct r8a66597_ep *ep,
				struct r8a66597_request *req);
static int r8a66597_queue(struct usb_ep *_ep, struct usb_request *_req,
			gfp_t gfp_flags);

static void transfer_complete(struct r8a66597_ep *ep,
		struct r8a66597_request *req, int status);

static inline u16 control_reg_get(struct r8a66597 *r8a66597, u16 pipenum);

static int usb_core_clk_ctrl(struct r8a66597 *r8a66597, bool clk_enable);

/*-------------------------------------------------------------------------*/
static unsigned int bootmode_offcharge;

static int __init is_bootmode_offcharge(char *str)
{
	if (!(strcmp(str, "charger"))) {
		udc_log("In charger mode\n");
		bootmode_offcharge = 1;
		}
	else {
		udc_log(" Not in charger mode\n");
		bootmode_offcharge = 0;
	}
	return 0;
}
early_param("androidboot.mode", is_bootmode_offcharge);

static inline u16 get_usb_speed(struct r8a66597 *r8a66597)
{
	return r8a66597_read(r8a66597, DVSTCTR0) & RHST;
}

static void enable_pipe_irq(struct r8a66597 *r8a66597, u16 pipenum,
		unsigned long reg)
{
	u16 tmp;

	tmp = r8a66597_read(r8a66597, INTENB0);
	r8a66597_bclr(r8a66597, BEMPE | NRDYE | BRDYE,
			INTENB0);
	r8a66597_bset(r8a66597, (1 << pipenum), reg);
	r8a66597_write(r8a66597, tmp, INTENB0);
}

static void disable_pipe_irq(struct r8a66597 *r8a66597, u16 pipenum,
		unsigned long reg)
{
	u16 tmp;

	tmp = r8a66597_read(r8a66597, INTENB0);
	r8a66597_bclr(r8a66597, BEMPE | NRDYE | BRDYE,
			INTENB0);
	r8a66597_bclr(r8a66597, (1 << pipenum), reg);
	r8a66597_write(r8a66597, tmp, INTENB0);
}



/**
 *** usb_phy_read - Read data of TUSB1211 register via PHY
 *** @return None
 ***/
static u8 usb_phy_read(u64 reg)
{
	u8 monreg;
	int counter = 0;
	__raw_writew((reg & PHY_READ_MASK), USB_SPADDR);
	__raw_writew(((reg >> 6) << 4), USB_SPEXADDR);
	/* Issue read comand */
	__raw_writew(__raw_readw(USB_SPCTRL) | USB_SPRD, USB_SPCTRL);
	udc_log("%s: Waiting for read Permission\n", __func__);
	monreg = __raw_readw(USB_SPCTRL);
	while (monreg & USB_SPRD) {
		usleep_range(10000, 11000);
		if (counter++ > 100) {
			/* Timeout occured. Looks Phy entered Low power mode  */
			pr_info("%s phy hang\n", __func__);
			break;
		}
		monreg = __raw_readw(USB_SPCTRL);
	}
	udc_log("%s: Read Permission granted\n", __func__);
	monreg = __raw_readw(USB_SPRDAT);
	udc_log("Value of PHY_READ is %x\n", monreg);
	return monreg;
}

/**
 *** usb_phy_write - Write data of TUSB1211 register via PHY
 *** @return None
 ***/
void usb_phy_write(u64 reg, u8 val)
{
	int counter = 0;
	u64 monreg;
	__raw_writew((reg & PHY_READ_MASK), USB_SPADDR);
	__raw_writew(((reg >> 6) << 4), USB_SPEXADDR);

	__raw_writew(val, USB_SPWDAT);

    /* Issue write comand */
	__raw_writew(__raw_readw(USB_SPCTRL) | USB_SPWR, USB_SPCTRL);
	udc_log("%s: Waiting for write Permission\n", __func__);
	monreg = __raw_readw(USB_SPCTRL);
	while (monreg & USB_SPWR) {
		usleep_range(10000, 11000);
		if (counter++ > 100) {
			/* Timeout occured. Looks Phy entered Low power mode  */
			pr_info("%s phy hang\n", __func__);
			break;
		}
		monreg = __raw_readw(USB_SPCTRL);
	}
	udc_log("%s: Write Permission granted\n", __func__);
}

static void get_charger_type(struct r8a66597 *r8a66597)
{
	u8 r_data = 0;
	int count = 0;
	int i;
	udc_log("%s:IN\n", __func__);

	if (vbus_valid == 0) {
#ifdef CONFIG_SEC_CHARGING_FEATURE
		spa_event_handler(SPA_EVT_CHARGER,
				 (void *)POWER_SUPPLY_TYPE_BATTERY);
#endif
		bcm_accy_event_handler(PMU_ACCY_EVT_OUT_CHRGR_TYPE,
				 (void *)POWER_SUPPLY_TYPE_BATTERY);

		if (r8a66597->charger_type == CHARGER_DCP) {
			usb_core_clk_ctrl(r8a66597, 0);
			if (wake_lock_active(&r8a66597->wake_lock))
				wake_unlock(&r8a66597->wake_lock);
		}
	} else {
			/* SW_CONTROL = 1 */
		r_data = usb_phy_read(TUSB1211_POWER_CONTROL_REG);
		usb_phy_write(TUSB1211_POWER_CONTROL_REG,
				r_data | (TUSB_SW_CONTROL));
		msleep(5);
		udc_log("%s:IN\ngoing for SW_USB_DET update\n", __func__);
		r_data = usb_phy_read(TUSB1211_POWER_CONTROL_REG);
		udc_log("%s:IN\nr_data:0x%x\n", __func__, r_data);
		msleep(2);
		/* SW_USB_DET = 0 */
		r_data = usb_phy_read(TUSB_VENDOR_SPECIFIC3);
		usb_phy_write(TUSB_VENDOR_SPECIFIC3,
			 r_data & (~TUSB_VENDOR_SPECIFIC3_SWUSBDET));
		msleep(5);
		if (!r8a66597->charger_detected) {/* reverse of HW FSM */
		/* DCD Timeout with idp_src enable */
		r_data = usb_phy_read(TUSB_VENDOR_SPECIFIC3);
		/* CHGD_IDP_SRC_EN for DCD timeout enable */
		usb_phy_write(TUSB_VENDOR_SPECIFIC3,
				r_data | (CHGD_IDP_SRC_EN));
		msleep(5);
		/* DCD Timeout max 2700 ms */
		while (count++ < MAX_COUNT_DCD_TIMEOUT) {
			msleep(10);
			r_data = usb_phy_read(TUSB_VENDOR_SPECIFIC4);
			if (!(r_data & CHGD_SERX_DP)) {
				udc_log("%s:DCD pass D+ < VLGC, continue Primary detect...\n", __func__);
				r_data = usb_phy_read(TUSB_VENDOR_SPECIFIC3);
				usb_phy_write(TUSB_VENDOR_SPECIFIC3,
						r_data & ~(CHGD_IDP_SRC_EN));
				break;
			}
			udc_log("%d: count\n", count);
		}
		r_data = usb_phy_read(TUSB1211_POWER_CONTROL_REG);
		usb_phy_write(TUSB1211_POWER_CONTROL_REG,
					r_data | (DP_VSRC_EN));
		msleep(1);/* delay 1 ms */
		for (i = 0; i < 20; i++) {
			r_data = usb_phy_read(TUSB1211_POWER_CONTROL_REG);
			udc_log("%s: POWER_CTRL:0x%x\n", __func__, r_data);
			udc_log("Count = %d \n", i);
			msleep(1);
		}
		if (!(r_data & VDAT_DET)) {
			udc_log("%s: Identified SDP\n", __func__);
			r8a66597->charger_type = CHARGER_SDP;
#ifdef CONFIG_SEC_CHARGING_FEATURE
			spa_event_handler(SPA_EVT_CHARGER,
				(void *)POWER_SUPPLY_TYPE_USB);

#endif
			bcm_accy_event_handler(PMU_ACCY_EVT_OUT_CHRGR_TYPE,
					(void *)POWER_SUPPLY_TYPE_USB);

			r_data = usb_phy_read(TUSB1211_POWER_CONTROL_REG);
			usb_phy_write(TUSB1211_POWER_CONTROL_REG,
					r_data & ~(DP_VSRC_EN));
			}
		else {
			udc_log("%s: Identified CDP/DCP\n", __func__);
			/* Implement Secondary Detection */
			r_data = usb_phy_read(TUSB1211_POWER_CONTROL_REG);
			usb_phy_write(TUSB1211_POWER_CONTROL_REG,
					r_data & ~(DP_VSRC_EN));
			/* Swap D+ AND D- LINES */
			usb_phy_write(TUSB_VENDOR_SPECIFIC1_CLR, 0x40);
			usleep_range(2000, 3000);
			r_data = usb_phy_read(TUSB1211_POWER_CONTROL_REG);
			usb_phy_write(TUSB1211_POWER_CONTROL_REG,
					r_data | (DP_VSRC_EN));
			msleep(20);/* Wait till VDAT_DET reflects */
			r_data = usb_phy_read(TUSB1211_POWER_CONTROL_REG);
			usb_phy_write(TUSB1211_POWER_CONTROL_REG,
					r_data & ~(DP_VSRC_EN));
			/* Reverse D+/D- Swapping */
			usb_phy_write(TUSB_VENDOR_SPECIFIC1_SET, 0x40);
			if (r_data & VDAT_DET) {
				pr_info("%s: Identified DCP\n", __func__);
				r8a66597->charger_type = CHARGER_DCP;
#ifdef CONFIG_SEC_CHARGING_FEATURE
				spa_event_handler(SPA_EVT_CHARGER,
					(void *)POWER_SUPPLY_TYPE_USB_DCP);
#endif

				bcm_accy_event_handler(
						PMU_ACCY_EVT_OUT_CHRGR_TYPE,
					(void *)POWER_SUPPLY_TYPE_USB_DCP);

				r_data =
				usb_phy_read(TUSB1211_POWER_CONTROL_REG);
				usb_phy_write(TUSB1211_POWER_CONTROL_REG,
						r_data | (DP_VSRC_EN));

				#if 0   /* BCM_ID_814296 */
				if (!bootmode_offcharge) {
					/* Release wake_lock to save power */
					usb_core_clk_ctrl(r8a66597, 0);
					if (wake_lock_active(&r8a66597->
								wake_lock))
						wake_unlock(&r8a66597->
								wake_lock);
				}
				#endif  /* #if 0 */
			} else {
				pr_info("%s: Identified CDP\n", __func__);
				r8a66597->charger_type = CHARGER_CDP;
#ifdef CONFIG_SEC_CHARGING_FEATURE
				spa_event_handler(SPA_EVT_CHARGER,
					(void *)POWER_SUPPLY_TYPE_USB_CDP);
#endif
				bcm_accy_event_handler(
						PMU_ACCY_EVT_OUT_CHRGR_TYPE,
					(void *)POWER_SUPPLY_TYPE_USB_CDP);
				}
			}
		}
	}
}


static void r8a66597_inform_vbus_power(struct r8a66597 *r8a66597, int ma)
{
	r8a66597_set_vbus_draw(r8a66597, ma);
}

#ifdef CONFIG_HAVE_CLK
static void r8a66597_clk_enable(struct r8a66597 *r8a66597)
{
	if (r8a66597->pdata->clk_enable)
		r8a66597->pdata->clk_enable(1);
	if (!r8a66597->phy_active) {
		struct clk *uclk = clk_get(NULL, "vclk3_clk");
		if (IS_ERR(uclk)) {
			udc_log("%s() cannot get vclk3_clk\n", __func__);
			return;
		}
		if (!uclk->usecount) {
			if (clk_enable(uclk))
				error_log("USB: Error in Enabling vclk3\n");
		}

		if (gpio_is_valid(r8a66597->pdata->gpio_cs))
			gpio_set_value(r8a66597->pdata->gpio_cs, 1);

		r8a66597->phy_active = 1;
	}
	if (clk_enable(r8a66597->clk_dmac))
		error_log("USB: Error in Enabling dmac clk\n");
	if (clk_enable(r8a66597->clk))
		error_log("USB: Error in enabling r8a66597 clk\n");
}
static void r8a66597_clk_disable(struct r8a66597 *r8a66597)
{
	clk_disable(r8a66597->clk);
	clk_disable(r8a66597->clk_dmac);
	if (r8a66597->phy_active) {
		struct clk *uclk = clk_get(NULL, "vclk3_clk");
		if (IS_ERR(uclk)) {
			udc_log("%s() cannot get vclk3_clk\n",  __func__);
			return;
		}
		if (gpio_is_valid(r8a66597->pdata->gpio_cs))
			gpio_set_value(r8a66597->pdata->gpio_cs, 0);
		if (uclk->usecount)
			clk_disable(r8a66597->uclk);
		r8a66597->phy_active = 0;
	}
	if (r8a66597->pdata->clk_enable)
		r8a66597->pdata->clk_enable(0);
}

static int r8a66597_clk_get(struct r8a66597 *r8a66597,
			    struct platform_device *pdev)
{
	char clk_name[16];

	snprintf(clk_name, sizeof(clk_name), "usb%d", pdev->id);
	r8a66597->clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(r8a66597->clk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		return PTR_ERR(r8a66597->clk);
	}

	snprintf(clk_name, sizeof(clk_name), "dmac");
	/* We don't have any device resource defined for USBHS-DMAC */
	r8a66597->clk_dmac = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(r8a66597->clk_dmac)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		clk_put(r8a66597->clk);
		return PTR_ERR(r8a66597->clk_dmac);
	}

	r8a66597->uclk = clk_get(NULL, "vclk3_clk");
	if (IS_ERR(r8a66597->uclk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", "vclk3_clk");
		clk_put(r8a66597->clk_dmac);
		clk_put(r8a66597->clk);
		return PTR_ERR(r8a66597->uclk);
	}

	return 0;
}

static void r8a66597_clk_put(struct r8a66597 *r8a66597)
{
	clk_put(r8a66597->uclk);
	clk_put(r8a66597->clk_dmac);
	clk_put(r8a66597->clk);
}

#else
static int r8a66597_clk_get(struct r8a66597 *r8a66597,
			    struct platform_device *pdev)
{
	return 0;
}
#define r8a66597_clk_put(x)
#define r8a66597_clk_enable(x)
#define r8a66597_clk_disable(x)
#endif

static void r8a66597_dma_reset(struct r8a66597 *r8a66597)
{
	r8a66597_dma_bclr(r8a66597, IE | SP | DE | TE, USBHS_DMAC_CHCR(0));
	r8a66597_dma_bclr(r8a66597, IE | SP | DE | TE, USBHS_DMAC_CHCR(1));
	r8a66597_dma_bclr(r8a66597, DME, DMAOR);
	r8a66597_bset(r8a66597, BCLR, D0FIFOCTR);
	r8a66597_bset(r8a66597, BCLR, D1FIFOCTR);
	r8a66597_dma_bset(r8a66597, SWR_RST, SWR);
	udelay(100);
	r8a66597_dma_bclr(r8a66597, SWR_RST, SWR);

}

static int can_pullup(struct r8a66597 *r8a66597)
{
	return r8a66597->driver && r8a66597->softconnect;
}

static void r8a66597_set_pullup(struct r8a66597 *r8a66597)
{
	if (can_pullup(r8a66597)){
		r8a66597_bset(r8a66597, DPRPU, SYSCFG0);

		}
	else{
		r8a66597_bclr(r8a66597, DPRPU, SYSCFG0);
		r8a66597->gadget.speed = USB_SPEED_UNKNOWN; /* Set gadget to
							       disconnect when
							       pullup is off */
	}
}

static void r8a66597_usb_connect(struct r8a66597 *r8a66597)
{
	r8a66597_bset(r8a66597, CTRE, INTENB0);
	r8a66597_bset(r8a66597, BEMPE | BRDYE, INTENB0);
	r8a66597_bset(r8a66597, RESM | DVSE, INTENB0);
	r8a66597_set_pullup(r8a66597);
	r8a66597_dma_reset(r8a66597);
}

static void r8a66597_usb_disconnect(struct r8a66597 *r8a66597)
__releases(r8a66597->lock)
__acquires(r8a66597->lock)
{
	r8a66597_bclr(r8a66597, CTRE, INTENB0);
	r8a66597_bclr(r8a66597, BEMPE | BRDYE, INTENB0);
	r8a66597_bclr(r8a66597, RESM, INTENB0);
	r8a66597_bclr(r8a66597, DPRPU, SYSCFG0);

	r8a66597->gadget.speed = USB_SPEED_UNKNOWN;
	spin_unlock(&r8a66597->lock);

	r8a66597->usb_suspended = 0;
	r8a66597->driver->disconnect(&r8a66597->gadget);
	spin_lock(&r8a66597->lock);
	r8a66597_dma_reset(r8a66597);

	disable_controller(r8a66597);
	INIT_LIST_HEAD(&r8a66597->ep[0].queue);
}

#define USB_PORT_STAT_CONNECTION 0x0001
static void r8a66597_hnp_work(struct work_struct *work)
{
	struct r8a66597 *r8a66597 =
			container_of(work, struct r8a66597, hnp_work.work);
	struct usb_phy *otg = usb_get_phy(0);
	udc_log("%s (%d): IN\n", __func__, __LINE__);
	if (otg->state == OTG_STATE_B_PERIPHERAL ||
		otg->state == OTG_STATE_A_PERIPHERAL) {
		r8a66597_bset(r8a66597, HNPBTOA, DVSTCTR0);
		/* disable interrupts */
		r8a66597_write(r8a66597, 0, INTENB0);
		r8a66597_write(r8a66597, 0, INTENB1);
		r8a66597_write(r8a66597, 0, BRDYENB);
		r8a66597_write(r8a66597, 0, BEMPENB);
		r8a66597_write(r8a66597, 0, NRDYENB);
		/* clear status */
		r8a66597_write(r8a66597, 0, BRDYSTS);
		r8a66597_write(r8a66597, 0, NRDYSTS);
		r8a66597_write(r8a66597, 0, BEMPSTS);

		r8a66597_bset(r8a66597, BEMPE | NRDYE | BRDYE, INTENB0);
		r8a66597_bset(r8a66597, BRDY0, BRDYENB);
		r8a66597_bset(r8a66597, BEMP0, BEMPENB);
		r8a66597_bset(r8a66597, TRNENSEL, SOFCFG);
		r8a66597_bset(r8a66597, SIGNE | SACKE, INTENB1);
		r8a66597_bset(r8a66597, OVRCRE, INTENB1);

		r8a66597_bset(r8a66597, DCFM, SYSCFG0);
		r8a66597_bset(r8a66597, DRPD, SYSCFG0);
		r8a66597_bclr(r8a66597, DPRPU, SYSCFG0);
		r8a66597_bset(r8a66597, HSE, SYSCFG0);

		if (otg->state == OTG_STATE_B_PERIPHERAL) {
			otg->state = OTG_STATE_B_WAIT_ACON;
			mod_timer(&r8a66597->hnp_timer_fail,
						jiffies + msecs_to_jiffies(155));
		} else if (otg->state == OTG_STATE_A_PERIPHERAL) {
			otg->state = OTG_STATE_A_WAIT_BCON;
		}
		otg->port_status |= USB_PORT_STAT_CONNECTION;
		r8a66597_bclr(r8a66597, DTCHE, INTENB1);
		r8a66597_bset(r8a66597, ATTCHE, INTENB1);
		udc_log("%s\n", usb_otg_state_string(otg->state));

	}
	usb_put_phy(otg);
}

static void r8a66597_hnp_timer_fail(unsigned long _r8a66597)
{
	struct r8a66597 *r8a66597 = (struct r8a66597 *)_r8a66597;
	struct usb_phy *otg = usb_get_phy(0);
	u16 bwait = r8a66597->pdata->buswait ? r8a66597->pdata->buswait : 15;

	if (otg->state == OTG_STATE_B_WAIT_ACON) {

		/* disable interrupts */
		r8a66597_write(r8a66597, 0, INTENB0);
		r8a66597_write(r8a66597, 0, INTENB1);
		r8a66597_write(r8a66597, 0, BRDYENB);
		r8a66597_write(r8a66597, 0, BEMPENB);
		r8a66597_write(r8a66597, 0, NRDYENB);

		/* clear status */
		r8a66597_write(r8a66597, 0, BRDYSTS);
		r8a66597_write(r8a66597, 0, NRDYSTS);
		r8a66597_write(r8a66597, 0, BEMPSTS);

		/* start clock */
		r8a66597_write(r8a66597, bwait, SYSCFG1);
		r8a66597_bset(r8a66597, HSE, SYSCFG0);
		r8a66597_bset(r8a66597, USBE, SYSCFG0);
		r8a66597_bset(r8a66597, SCKE, SYSCFG0);

		r8a66597_usb_connect(r8a66597);
		otg->state = OTG_STATE_B_PERIPHERAL;
	}
	usb_put_phy(otg);
}

static int usb_core_clk_ctrl(struct r8a66597 *r8a66597, bool clk_enable)
{
	   static int usb_clk_status;
	   if (clk_enable) {
		if (!usb_clk_status) {
			pm_runtime_get_sync(r8a66597_to_dev(r8a66597));
			r8a66597_clk_enable(r8a66597);
			pr_info("%s, After clock enable - USB usage count:%d\n",
			__func__, atomic_read(&r8a66597_to_dev(r8a66597)->power.usage_count));
			udc_log("%s: SYSC_PSTR: %08x\n",
					__func__, __raw_readl(PSTR));
			udc_log("%s: SYSC_MSTPSR3: %08x\n",
					__func__, __raw_readl(MSTPSR3));
			udc_log("%s: SYSC_MMSTPCR2: %08x\n",
					__func__, __raw_readl(MMSTPCR2));

			usb_clk_status = 1;
		   }
		else
			return 0;
	  } else {
		if (usb_clk_status) {
		   	r8a66597_clk_disable(r8a66597);
			pm_runtime_put_sync(r8a66597_to_dev(r8a66597));
			pr_info("%s, After clock disable-USB usage count:%d\n",
			__func__, atomic_read(
			&r8a66597_to_dev(r8a66597)->power.usage_count));
			usb_clk_status = 0;
		   }
		else
			return 0;
	  }
	return 0;
}

static inline u16 control_reg_get_pid(struct r8a66597 *r8a66597, u16 pipenum)
{
	u16 pid = 0;
	unsigned long offset;

	if (pipenum == 0) {
		pid = r8a66597_read(r8a66597, DCPCTR) & PID;
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		pid = r8a66597_read(r8a66597, offset) & PID;
	} else {
		dev_err(r8a66597_to_dev(r8a66597), "unexpect pipe num (%d)\n",
			pipenum);
	}

	return pid;
}

static inline void control_reg_set_pid(struct r8a66597 *r8a66597, u16 pipenum,
		u16 pid)
{
	unsigned long offset;

	if (pipenum == 0) {
		r8a66597_mdfy(r8a66597, pid, PID, DCPCTR);
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		r8a66597_mdfy(r8a66597, pid, PID, offset);
	} else {
		dev_err(r8a66597_to_dev(r8a66597), "unexpect pipe num (%d)\n",
			pipenum);
	}
}

static void r8a66597_wait_pbusy(struct r8a66597 *r8a66597, u16 pipenum)
{
	u16 tmp;
	int i = 0;

	do {
		tmp = control_reg_get(r8a66597, pipenum);
		if (i++ > MSEC_COUNT) {	/* 1 msec */
			dev_err(r8a66597_to_dev(r8a66597),
				"%s: pipenum = %d, timeout \n",
				__func__, pipenum);
			break;
		}
		ndelay(1);
	} while ((tmp & PBUSY) != 0);
}

static inline void pipe_start(struct r8a66597 *r8a66597, u16 pipenum)
{
	control_reg_set_pid(r8a66597, pipenum, PID_BUF);
}

static inline void pipe_stop(struct r8a66597 *r8a66597, u16 pipenum)
{
	control_reg_set_pid(r8a66597, pipenum, PID_NAK);
	r8a66597_wait_pbusy(r8a66597, pipenum);
}

static inline void pipe_stall(struct r8a66597 *r8a66597, u16 pipenum)
{
	control_reg_set_pid(r8a66597, pipenum, PID_STALL);
}

static inline u16 control_reg_get(struct r8a66597 *r8a66597, u16 pipenum)
{
	u16 ret = 0;
	unsigned long offset;

	if (pipenum == 0) {
		ret = r8a66597_read(r8a66597, DCPCTR);
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		ret = r8a66597_read(r8a66597, offset);
	} else {
		dev_err(r8a66597_to_dev(r8a66597), "unexpect pipe num (%d)\n",
			pipenum);
	}

	return ret;
}

static inline void control_reg_sqclr(struct r8a66597 *r8a66597, u16 pipenum)
{
	unsigned long offset;

	pipe_stop(r8a66597, pipenum);

	if (pipenum == 0) {
		r8a66597_bset(r8a66597, SQCLR, DCPCTR);
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		r8a66597_bset(r8a66597, SQCLR, offset);
	} else {
		dev_err(r8a66597_to_dev(r8a66597), "unexpect pipe num (%d)\n",
			pipenum);
	}
}

static void control_reg_sqset(struct r8a66597 *r8a66597, u16 pipenum)
{
	unsigned long offset;

	pipe_stop(r8a66597, pipenum);

	if (pipenum == 0) {
		r8a66597_bset(r8a66597, SQSET, DCPCTR);
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		r8a66597_bset(r8a66597, SQSET, offset);
	} else {
		dev_err(r8a66597_to_dev(r8a66597),
			"unexpect pipe num(%d)\n", pipenum);
	}
}

static u16 control_reg_sqmon(struct r8a66597 *r8a66597, u16 pipenum)
{
	unsigned long offset;

	if (pipenum == 0) {
		return r8a66597_read(r8a66597, DCPCTR) & SQMON;
	} else if (pipenum < R8A66597_MAX_NUM_PIPE) {
		offset = get_pipectr_addr(pipenum);
		return r8a66597_read(r8a66597, offset) & SQMON;
	} else {
		dev_err(r8a66597_to_dev(r8a66597),
			"unexpect pipe num(%d)\n", pipenum);
	}

	return 0;
}

static u16 save_usb_toggle(struct r8a66597 *r8a66597, u16 pipenum)
{
	return control_reg_sqmon(r8a66597, pipenum);
}

static void restore_usb_toggle(struct r8a66597 *r8a66597, u16 pipenum,
				u16 toggle)
{
	if (toggle)
		control_reg_sqset(r8a66597, pipenum);
	else
		control_reg_sqclr(r8a66597, pipenum);
}

static inline int get_buffer_size(struct r8a66597 *r8a66597, u16 pipenum)
{
	u16 tmp;
	int size;

	if (pipenum == 0) {
		tmp = r8a66597_read(r8a66597, DCPCFG);
		if ((tmp & R8A66597_CNTMD) != 0)
			size = 256;
		else {
			tmp = r8a66597_read(r8a66597, DCPMAXP);
			size = tmp & MAXP;
		}
	} else {
		r8a66597_write(r8a66597, pipenum, PIPESEL);
		tmp = r8a66597_read(r8a66597, PIPECFG);
		if ((tmp & R8A66597_CNTMD) != 0) {
			tmp = r8a66597_read(r8a66597, PIPEBUF);
			size = ((tmp >> 10) + 1) * 64;
		} else {
			tmp = r8a66597_read(r8a66597, PIPEMAXP);
			size = tmp & MXPS;
		}
	}

	return size;
}

static inline unsigned short mbw_value(struct r8a66597 *r8a66597)
{
	if (r8a66597->pdata->on_chip)
		return MBW_32;
	else
		return MBW_16;
}

static void r8a66597_change_curpipe(struct r8a66597 *r8a66597, u16 pipenum,
				    u16 isel, u16 fifosel)
{
	u16 tmp, mask, loop;
	int i = 0;

	if (!pipenum) {
		mask = ISEL | CURPIPE;
		loop = isel;
	} else {
		mask = CURPIPE;
		loop = pipenum;
	}
	r8a66597_mdfy(r8a66597, loop, mask, fifosel);

	do {
		tmp = r8a66597_read(r8a66597, fifosel);
		if (i++ > MSEC_COUNT / 5) {//custom_t peter broadcom patch
			dev_err(r8a66597_to_dev(r8a66597),
				"r8a66597: register%x, loop %x "
				"is timeout\n", fifosel, loop);
			break;
		}
		ndelay(1);
	} while ((tmp & mask) != loop);
}

static inline void pipe_change(struct r8a66597 *r8a66597, u16 pipenum)
{
	struct r8a66597_ep *ep = r8a66597->pipenum2ep[pipenum];

	if (ep->use_dma)
		r8a66597_bclr(r8a66597, DREQE, ep->fifosel);

	r8a66597_change_curpipe(r8a66597, pipenum, 0, ep->fifosel);

	r8a66597_bset(r8a66597, mbw_value(r8a66597), ep->fifosel);

	if (ep->use_dma)
		r8a66597_bset(r8a66597, DREQE, ep->fifosel);
}

static inline void pipe_dma_disable(struct r8a66597 *r8a66597, u16 pipenum)
{
	struct r8a66597_ep *ep = r8a66597->pipenum2ep[pipenum];

	if (ep->use_dma)
		r8a66597_bclr(r8a66597, DREQE, ep->fifosel);
}

/*
 * PIPE, transfer type and buffer size configuration (1 chunk = 64 bytes)
 *
 * pipe#			type		size@bufnum
 * -------------------------------------------------------------
 * PIPE0 = 0			CONTROL		256B@0x00
 * PIPE1 = 8 + (32 * 0)		ISOC or BULK	2KB@0x08 (1KB reserved for future use)
 * PIPE2 = 8 + (32 * 1)		ISOC or BULK	2KB@0x28 (1KB reserved for future use)
 * PIPE3 = 8 + 64 + (16 * 0)	BULK		1KB@0x48 with DBLB
 * PIPE4 = 8 + 64 + (16 * 1)	BULK		1KB@0x58 with DBLB
 * PIPE5 = 8 + 64 + (16 * 2)	BULK		1KB@0x68 with DBLB
 * PIPE6 = 4 + (1 * 0)		INT		64B@0x04
 * PIPE7 = 4 + (1 * 1)		INT		64B@0x05
 * PIPE8 = 4 + (1 * 2)		INT		64B@0x06
 * PIPE9 = 4 + (1 * 3)		INT		64B@0x07
 *
 * With extended bulk endpoints supported:
 * PIPE9 = 8 + 64 + (16 * 3)	BULK		1KB@0x78 with DBLB
 * PIPEA = 8 + 64 + (16 * 4)	BULK		1KB@0x88 with DBLB
 * PIPEB = 8 + 64 + (16 * 5)	BULK		1KB@0x98 with DBLB
 * PIPEC = 8 + 64 + (16 * 6)	BULK		1KB@0xA8 with DBLB
 * PIPED = 8 + 64 + (16 * 7)	BULK		1KB@0xB8 with DBLB
 * PIPEE = 8 + 64 + (16 * 8)	BULK		1KB@0xC8 with DBLB
 * PIPEF = 8 + 64 + (16 * 9)	BULK		1KB@0xD8 with DBLB
 *
 * Expression in C:
 *
 * #define R8A66597_BASE_PIPENUM_BULK	3
 * #define R8A66597_BASE_PIPENUM_ISOC	1
 * #define R8A66597_BASE_PIPENUM_INT	6
 *
 * u16 get_bufnum(int pipenum)
 * {
 *	u16 bufnum = 0;
 *
 *	if (pipenum < 0)
 *		BUG();
 *	else if (pipenum <= 2)
 *		bufnum = 8 + 32 * (pipenum - R8A66597_BASE_PIPENUM_ISOC);
 *	else if (pipenum <= 5)
 *		bufnum = 8 + 64 + 16 * (pipenum - R8A66597_BASE_PIPENUM_BULK);
 *	else if (pipenum <= 9)
 *		bufnum = 4 + (pipenum - R8A66597_BASE_PIPENUM_INT);
 *	else
 *		BUG();
 *
 *	return bufnum;
 * }
 */
static u16 pipenum2bufnum[R8A66597_MAX_NUM_PIPE] = {
	0, 0x08, 0x28, 0x48, 0x58, 0x68, 0x04, 0x05, 0x06, /* PIPE0..PIPE8 */
#ifdef CONFIG_USB_R8A66597_TYPE_BULK_PIPES_12
	0x78, 0x88, 0x98, 0xa8, 0xb8, 0xc8, 0xd8, /* PIPE9..PIPEF */
#else
	0x07, /* PIPE9 */
#endif
};

static int pipe_buffer_setting(struct r8a66597 *r8a66597,
		struct r8a66597_pipe_info *info)
{
	u16 bufnum = 0, buf_bsize = 0;
	u16 pipecfg = 0;
	u16 max_bufnum;

	if ((info->pipe <= 0) || (info->pipe >= R8A66597_MAX_NUM_PIPE))
		return -EINVAL;

	r8a66597_write(r8a66597, info->pipe, PIPESEL);

	if (info->dir_in)
		pipecfg |= R8A66597_DIR;
	pipecfg |= info->type;
	pipecfg |= info->epnum;
	switch (info->type) {
	case R8A66597_INT:
		buf_bsize = 0;
		break;
	case R8A66597_BULK:
		if (info->epnum == 0x1)
			buf_bsize = 15;
		else
			buf_bsize = 7;
		pipecfg |= R8A66597_DBLB;
		pipecfg |= R8A66597_CNTMD;
		if (!info->dir_in)
			pipecfg |= (R8A66597_SHTNAK | R8A66597_BFRE);
		break;
	case R8A66597_ISO:
		buf_bsize = 15;
		break;
	}

	bufnum = pipenum2bufnum[info->pipe];
	max_bufnum = r8a66597->pdata->max_bufnum ? : R8A66597_MAX_BUFNUM;
	if (buf_bsize && ((bufnum + 16) >= max_bufnum)) {
		dev_err(r8a66597_to_dev(r8a66597),
			"r8a66597 pipe memory is insufficient (%d,0x%x,0x%x)\n",
			info->pipe, bufnum, max_bufnum);
		return -ENOMEM;
	}

	r8a66597_write(r8a66597, pipecfg, PIPECFG);
	r8a66597_write(r8a66597, (buf_bsize << 10) | (bufnum), PIPEBUF);
	r8a66597_write(r8a66597, info->maxpacket, PIPEMAXP);
	if (info->interval)
		info->interval--;
	r8a66597_write(r8a66597, info->interval, PIPEPERI);

	return 0;
}

static void pipe_initialize(struct r8a66597_ep *ep)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;

	r8a66597_change_curpipe(r8a66597, 0, 0, ep->fifosel);

	r8a66597_write(r8a66597, ACLRM, ep->pipectr);
	r8a66597_write(r8a66597, 0, ep->pipectr);
	r8a66597_write(r8a66597, SQCLR, ep->pipectr);
	if (ep->use_dma) {
		r8a66597_change_curpipe(r8a66597, ep->pipenum, 0, ep->fifosel);

		r8a66597_bset(r8a66597, mbw_value(r8a66597), ep->fifosel);
	}
}

static void r8a66597_ep_setting(struct r8a66597 *r8a66597,
				struct r8a66597_ep *ep,
				const struct usb_endpoint_descriptor *desc,
				u16 pipenum, int dma)
{
	ep->use_dma = 0;
	ep->fifoaddr = CFIFO;
	ep->fifosel = CFIFOSEL;
	ep->fifoctr = CFIFOCTR;

	ep->pipectr = get_pipectr_addr(pipenum);
	if (usb_endpoint_xfer_bulk(desc) || usb_endpoint_xfer_isoc(desc)) {
		ep->pipetre = get_pipetre_addr(pipenum);
		ep->pipetrn = get_pipetrn_addr(pipenum);
	} else {
		ep->pipetre = 0;
		ep->pipetrn = 0;
	}
	ep->pipenum = pipenum;
	ep->ep.maxpacket = usb_endpoint_maxp(desc);
	r8a66597->pipenum2ep[pipenum] = ep;
	r8a66597->epaddr2ep[usb_endpoint_num(desc)]
		= ep;
	INIT_LIST_HEAD(&ep->queue);
}

static void r8a66597_ep_release(struct r8a66597_ep *ep)
{
	if (ep->pipenum == 0)
		return;
	ep->pipenum = 0;
	ep->busy = 0;
	/* Re-initialize ep.maxpacket in endpoint descriptor to
	   R8A66597_MAX_PACKET_SIZE */
	ep->ep.maxpacket = R8A66597_MAX_PACKET_SIZE;
}

static int alloc_pipe_config(struct r8a66597_ep *ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;
	struct r8a66597_pipe_info info;
	int dma = 0;
	int ret = 0;
	unsigned long flags;

	ep->ep.desc = desc;

	if (ep->pipenum)	/* already allocated pipe  */
		return 0;

	spin_lock_irqsave(&r8a66597->lock, flags);
	info.pipe = usb_endpoint_num(desc);

	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_BULK:
		info.type = R8A66597_BULK;
		dma = 1;
		break;
	case USB_ENDPOINT_XFER_INT:
		info.type = R8A66597_INT;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		info.type = R8A66597_ISO;
		break;
	default:
		dev_err(r8a66597_to_dev(r8a66597), "unexpect xfer type\n");
		ret = -EINVAL;
		goto out;
	}
	ep->type = info.type;

	info.epnum = usb_endpoint_num(desc);
	info.maxpacket = usb_endpoint_maxp(desc);
	info.interval = desc->bInterval;
	if (desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
		info.dir_in = 1;
	else
		info.dir_in = 0;

	ret = pipe_buffer_setting(r8a66597, &info);
	if (ret < 0) {
		dev_err(r8a66597_to_dev(r8a66597),
			"pipe_buffer_setting fail\n");
		goto out;
	}

	r8a66597_ep_setting(r8a66597, ep, desc, info.pipe, dma);
	pipe_initialize(ep);

out:
	spin_unlock_irqrestore(&r8a66597->lock, flags);

	return ret;
}

static int free_pipe_config(struct r8a66597_ep *ep)
{
	r8a66597_ep_release(ep);

	return 0;
}

/*-------------------------------------------------------------------------*/
static void pipe_irq_enable(struct r8a66597 *r8a66597, u16 pipenum)
{
	enable_irq_ready(r8a66597, pipenum);
	enable_irq_nrdy(r8a66597, pipenum);
}

static void pipe_irq_disable(struct r8a66597 *r8a66597, u16 pipenum)
{
	disable_irq_ready(r8a66597, pipenum);
	disable_irq_nrdy(r8a66597, pipenum);
}

/* if complete is true, gadget driver complete function is not call */
static void control_end(struct r8a66597 *r8a66597, unsigned ccpl)
{
	r8a66597->ep[0].internal_ccpl = ccpl;
	pipe_start(r8a66597, 0);
	r8a66597_bset(r8a66597, CCPL, DCPCTR);
}

static void start_ep0_write(struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;

	r8a66597_change_curpipe(r8a66597, 0, ISEL, CFIFOSEL);
	r8a66597_write(r8a66597, BCLR, ep->fifoctr);
	if (req->req.length == 0) {
		r8a66597_bset(r8a66597, BVAL, ep->fifoctr);
		pipe_start(r8a66597, 0);
		transfer_complete(ep, req, 0);
	} else {
		r8a66597_write(r8a66597, ~BEMP0, BEMPSTS);
		irq_ep0_write(ep, req);
	}
}

static void disable_fifosel(struct r8a66597 *r8a66597, u16 pipenum,
			    u16 fifosel)
{
	u16 tmp;

	tmp = r8a66597_read(r8a66597, fifosel) & CURPIPE;
	if (tmp == pipenum)
		r8a66597_change_curpipe(r8a66597, 0, 0, fifosel);
}

static void change_bfre_mode(struct r8a66597 *r8a66597, u16 pipenum,
			     int enable)
{
	struct r8a66597_ep *ep = r8a66597->pipenum2ep[pipenum];
	u16 tmp, toggle;

	/* check current BFRE bit */
	r8a66597_write(r8a66597, pipenum, PIPESEL);
	tmp = r8a66597_read(r8a66597, PIPECFG) & R8A66597_BFRE;
	if ((enable && tmp) || (!enable && !tmp))
		return;

	/* change BFRE bit */
	pipe_stop(r8a66597, pipenum);
	disable_fifosel(r8a66597, pipenum, CFIFOSEL);
	disable_fifosel(r8a66597, pipenum, D0FIFOSEL);
	disable_fifosel(r8a66597, pipenum, D1FIFOSEL);

	toggle = save_usb_toggle(r8a66597, pipenum);

	r8a66597_write(r8a66597, pipenum, PIPESEL);
	if (enable)
		r8a66597_bset(r8a66597, R8A66597_BFRE, PIPECFG);
	else
		r8a66597_bclr(r8a66597, R8A66597_BFRE, PIPECFG);

	/* initialize for internal BFRE flag */
	r8a66597_bset(r8a66597, ACLRM, ep->pipectr);
	r8a66597_bclr(r8a66597, ACLRM, ep->pipectr);

	restore_usb_toggle(r8a66597, pipenum, toggle);
}

static int usb_dma_check_alignment(void *buf, int size)
{
	return !((unsigned long)buf & (size - 1));
}

static int dmac_alloc_channel(struct r8a66597 *r8a66597,
				   struct r8a66597_ep *ep,
				   struct r8a66597_request *req)
{
	struct r8a66597_dma *dma;
	int ch, ret;

	if (!r8a66597_has_dmac(r8a66597))
		 return -ENODEV;

	/* Check transfer length */
	if (!req->req.length)
		return -EINVAL;

	/* Check transfer type */
	if (!usb_endpoint_xfer_bulk(ep->ep.desc))
		 return -EIO;

	/* Check buffer alignment */
	if (!usb_dma_check_alignment(req->req.buf, 8))
		 return -EINVAL;

	/* Find available DMA channels */
	if (ep->ep.desc->bEndpointAddress & USB_DIR_IN)
		 ch = USBHS_DMAC_IN_CHANNEL;
	else
		 ch = USBHS_DMAC_OUT_CHANNEL;

	if ((ch == USBHS_DMAC_OUT_CHANNEL) && (ep->pipenum != 2))
		return -EINVAL;

	if (r8a66597->dma[ch].used)
		 return -EBUSY;
	dma = &r8a66597->dma[ch];

	/* set USBHS-DMAC parameters */
	dma->channel = ch;
	dma->ep = ep;
	dma->used = 1;
	if (usb_dma_check_alignment(req->req.buf, 32)) {
		 dma->tx_size = 32;
		 dma->chcr_ts = TS_32;
	} else if (usb_dma_check_alignment(req->req.buf, 16)) {
		 dma->tx_size = 16;
		 dma->chcr_ts = TS_16;
	} else {
		 dma->tx_size = 8;
		 dma->chcr_ts = TS_8;
	}

	if (ep->ep.desc->bEndpointAddress & USB_DIR_IN) {
		dma->dir = 1;
		dma->expect_dmicr = USBHS_DMAC_DMICR_TE(ch);
	} else {
		 dma->dir = 0;
		 dma->expect_dmicr = USBHS_DMAC_DMICR_TE(ch) |
					USBHS_DMAC_DMICR_SP(ch) |
					USBHS_DMAC_DMICR_NULL(ch);
		 change_bfre_mode(r8a66597, ep->pipenum, 1);
	}

	/* set r8a66597_ep paramters */
	ep->use_dma = 1;
	ep->dma = dma;
	if (dma->channel == 0) {
		 ep->fifoaddr = D0FIFO;
		 ep->fifosel = D0FIFOSEL;
		 ep->fifoctr = D0FIFOCTR;
	} else {
		 ep->fifoaddr = D1FIFO;
		 ep->fifosel = D1FIFOSEL;
		 ep->fifoctr = D1FIFOCTR;
	}

	/* dma mapping */
	ret = usb_gadget_map_request(&r8a66597->gadget, &req->req, dma->dir);

	/* Initialize pipe, if needed */
	if (!dma->initialized) {
		 pipe_initialize(ep);
		 dma->initialized = 1;
	}

	return ret;
}

static void dmac_free_channel(struct r8a66597 *r8a66597,
				struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	if (!r8a66597_has_dmac(r8a66597))
		return;

	usb_gadget_unmap_request(&r8a66597->gadget, &req->req, ep->dma->dir);

	r8a66597_bclr(r8a66597, DREQE, ep->fifosel);
	r8a66597_change_curpipe(r8a66597, 0, 0, ep->fifosel);

	ep->dma->used = 0;
	ep->use_dma = 0;
	ep->fifoaddr = CFIFO;
	ep->fifosel = CFIFOSEL;
	ep->fifoctr = CFIFOCTR;
}

#define FINAL_TRANSACTION_DATA_TRANSFER 0xFFFFFFFF
static void dmac_start(struct r8a66597 *r8a66597, struct r8a66597_ep *ep,
			 struct r8a66597_request *req)
{
	int ch = ep->dma->channel;

	if (req->req.length == 0)
		return;

	r8a66597_dma_bclr(r8a66597, DE, USBHS_DMAC_CHCR(ch));

	r8a66597_dma_write(r8a66597, (u32)req->req.dma, USBHS_DMAC_SAR(ch));
	r8a66597_dma_write(r8a66597, (u32)req->req.dma, USBHS_DMAC_DAR(ch));

	r8a66597_dma_write(r8a66597,
			DIV_ROUND_UP(req->req.length, ep->dma->tx_size),
			USBHS_DMAC_TCR(ch));
	r8a66597_dma_write(r8a66597, 0, USBHS_DMAC_CHCR(ch));
	r8a66597_dma_write(r8a66597, DMA_TIMEOUT, USBHS_DMAC_TOCSTR(ch));

	if (ep->dma->dir) {
		if ((req->req.length % ep->dma->tx_size) == 0)
			r8a66597_dma_write(r8a66597, FINAL_TRANSACTION_DATA_TRANSFER,
						USBHS_DMAC_TEND(ch));
		else
			r8a66597_dma_write(r8a66597,
					~(FINAL_TRANSACTION_DATA_TRANSFER >>
					(req->req.length &
					 (ep->dma->tx_size - 1))),
					USBHS_DMAC_TEND(ch));
	} else {
		r8a66597_dma_write(r8a66597, 0,  USBHS_DMAC_TEND(ch));
	}

	r8a66597_dma_bset(r8a66597, DME, DMAOR);

	if (!ep->dma->dir)
		r8a66597_dma_bset(r8a66597, NULLE, USBHS_DMAC_CHCR(ch));

	r8a66597_dma_bset(r8a66597, IE | ep->dma->chcr_ts, USBHS_DMAC_CHCR(ch));
	r8a66597_dma_bset(r8a66597, DE, USBHS_DMAC_CHCR(ch));
}

static void dmac_cancel(struct r8a66597_ep *ep, struct r8a66597_request *req)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;
	u32 chcr0, chcr1;

	if (!ep->use_dma)
		return;

	r8a66597_dma_bclr(r8a66597, DE | IE, USBHS_DMAC_CHCR(ep->dma->channel));
	if (!ep->dma->dir)
		r8a66597_bset(r8a66597, BCLR, ep->fifoctr);

	chcr0 = r8a66597_dma_read(r8a66597, USBHS_DMAC_CHCR(0));
	chcr1 = r8a66597_dma_read(r8a66597, USBHS_DMAC_CHCR(1));
	if (!(chcr0 & DE) && !(chcr1 & DE))
		r8a66597_dma_reset(r8a66597);
}

static void start_packet_write(struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;
	u16 tmp;

	if (!req->req.buf)
		dev_warn(r8a66597_to_dev(r8a66597),
			 "%s: buffer pointer is NULL\n", __func__);

	r8a66597_write(r8a66597, ~(1 << ep->pipenum), BRDYSTS);
	if (dmac_alloc_channel(r8a66597, ep, req) < 0) {
		/* PIO mode */
		pipe_change(r8a66597, ep->pipenum);
		disable_irq_empty(r8a66597, ep->pipenum);
		pipe_start(r8a66597, ep->pipenum);
		tmp = r8a66597_read(r8a66597, ep->fifoctr);
		if (unlikely((tmp & FRDY) == 0))
			pipe_irq_enable(r8a66597, ep->pipenum);
		else
			irq_packet_write(ep, req);
	} else {
		/* DMA mode */
		pipe_change(r8a66597, ep->pipenum);
		disable_irq_nrdy(r8a66597, ep->pipenum);
		pipe_start(r8a66597, ep->pipenum);
		enable_irq_nrdy(r8a66597, ep->pipenum);
		dmac_start(r8a66597, ep, req);
	}
}

static void start_packet_read(struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	struct r8a66597 *r8a66597 = ep->r8a66597;
	u16 pipenum = ep->pipenum;

	if (ep->pipenum == 0) {
		r8a66597_change_curpipe(r8a66597, 0, 0, CFIFOSEL);
		r8a66597_write(r8a66597, BCLR, ep->fifoctr);
		pipe_start(r8a66597, pipenum);
		pipe_irq_enable(r8a66597, pipenum);
	} else {
		pipe_stop(r8a66597, pipenum);
		if (ep->pipetre) {
			enable_irq_nrdy(r8a66597, pipenum);
			r8a66597_write(r8a66597, TRCLR, ep->pipetre);
			r8a66597_write(r8a66597,
				DIV_ROUND_UP(req->req.length, ep->ep.maxpacket),
				ep->pipetrn);
			r8a66597_bset(r8a66597, TRENB, ep->pipetre);
		}

		r8a66597_write(r8a66597, ~(1 << pipenum), BRDYSTS);
		if (dmac_alloc_channel(r8a66597, ep, req) < 0) {
			/* PIO mode */
			change_bfre_mode(r8a66597, ep->pipenum, 0);
			pipe_start(r8a66597, pipenum);	/* trigger once */
			pipe_irq_enable(r8a66597, pipenum);
		} else {
			pipe_change(r8a66597, pipenum);
			dmac_start(r8a66597, ep, req);
			pipe_start(r8a66597, pipenum);	/* trigger once */
		}
	}
}

static void start_packet(struct r8a66597_ep *ep, struct r8a66597_request *req)
{
	if (ep->ep.desc->bEndpointAddress & USB_DIR_IN)
		start_packet_write(ep, req);
	else
		start_packet_read(ep, req);
}

static void start_ep0(struct r8a66597_ep *ep, struct r8a66597_request *req)
{
	u16 ctsq;

	ctsq = r8a66597_read(ep->r8a66597, INTSTS0) & CTSQ;

	switch (ctsq) {
	case CS_RDDS:
		start_ep0_write(ep, req);
		break;
	case CS_WRDS:
		start_packet_read(ep, req);
		break;

	case CS_WRND:
		control_end(ep->r8a66597, 0);
		break;
	default:
		dev_err(r8a66597_to_dev(ep->r8a66597),
			"start_ep0: unexpect ctsq(%x)\n", ctsq);
		break;
	}
}

static void init_controller(struct r8a66597 *r8a66597)
{
	u16 bwait = r8a66597->pdata->buswait ? : 0xf;
	u16 vif = r8a66597->pdata->vif ? LDRV : 0;
	u16 irq_sense = r8a66597->irq_sense_low ? INTL : 0;
	u16 endian = r8a66597->pdata->endian ? BIGEND : 0;

	if (r8a66597->pdata->on_chip) {
		r8a66597_write(r8a66597, bwait, SYSCFG1);
		r8a66597_bset(r8a66597, HSE, SYSCFG0);

		r8a66597_bclr(r8a66597, USBE, SYSCFG0);
		r8a66597_bclr(r8a66597, DPRPU, SYSCFG0);
		r8a66597_bset(r8a66597, USBE, SYSCFG0);

		r8a66597_bset(r8a66597, SCKE, SYSCFG0);

		r8a66597_bset(r8a66597, irq_sense, INTENB1);
	} else {
		r8a66597_bset(r8a66597, vif | endian, PINCFG);
		r8a66597_bset(r8a66597, HSE, SYSCFG0);		/* High spd */
		r8a66597_mdfy(r8a66597, get_xtal_from_pdata(r8a66597->pdata),
				XTAL, SYSCFG0);

		r8a66597_bclr(r8a66597, USBE, SYSCFG0);
		r8a66597_bclr(r8a66597, DPRPU, SYSCFG0);
		r8a66597_bset(r8a66597, USBE, SYSCFG0);

		r8a66597_bset(r8a66597, XCKE, SYSCFG0);

		msleep(3);

		r8a66597_bset(r8a66597, PLLC, SYSCFG0);

		msleep(1);

		r8a66597_bset(r8a66597, SCKE, SYSCFG0);

		r8a66597_bset(r8a66597, irq_sense, INTENB1);
		r8a66597_write(r8a66597, BURST | CPU_ADR_RD_WR,
			       DMA0CFG);
	}
}

static void disable_controller(struct r8a66597 *r8a66597)
{
	if (r8a66597->pdata->on_chip) {
		r8a66597_bset(r8a66597, SCKE, SYSCFG0);
		r8a66597_bclr(r8a66597, UTST, TESTMODE);

		/* disable interrupts */
		r8a66597_write(r8a66597, 0, INTENB0);
		r8a66597_write(r8a66597, 0, INTENB1);
		r8a66597_write(r8a66597, 0, BRDYENB);
		r8a66597_write(r8a66597, 0, BEMPENB);
		r8a66597_write(r8a66597, 0, NRDYENB);

		/* clear status */
		r8a66597_write(r8a66597, 0, BRDYSTS);
		r8a66597_write(r8a66597, 0, NRDYSTS);
		r8a66597_write(r8a66597, 0, BEMPSTS);

		r8a66597_bclr(r8a66597, USBE, SYSCFG0);
		r8a66597_bclr(r8a66597, SCKE, SYSCFG0);
	} else {
		r8a66597_bclr(r8a66597, UTST, TESTMODE);
		r8a66597_bclr(r8a66597, SCKE, SYSCFG0);
		udelay(1);
		r8a66597_bclr(r8a66597, PLLC, SYSCFG0);
		udelay(1);
		udelay(1);
		r8a66597_bclr(r8a66597, XCKE, SYSCFG0);
	}
}

static void r8a66597_start_xclock(struct r8a66597 *r8a66597)
{
	u16 tmp;

	if (!r8a66597->pdata->on_chip) {
		tmp = r8a66597_read(r8a66597, SYSCFG0);
		if (!(tmp & XCKE))
			r8a66597_bset(r8a66597, XCKE, SYSCFG0);
	}
}

static struct r8a66597_request *get_request_from_ep(struct r8a66597_ep *ep)
{
	return list_entry(ep->queue.next, struct r8a66597_request, queue);
}

/*-------------------------------------------------------------------------*/
static void transfer_complete(struct r8a66597_ep *ep,
		struct r8a66597_request *req, int status)
__releases(r8a66597->lock)
__acquires(r8a66597->lock)
{
	int restart = 0;

	if (unlikely(ep->pipenum == 0)) {
		if (ep->internal_ccpl) {
			ep->internal_ccpl = 0;
			return;
		}
	}

	list_del_init(&req->queue);
	if (ep->r8a66597->gadget.speed == USB_SPEED_UNKNOWN)
		req->req.status = -ESHUTDOWN;
	else
		req->req.status = status;

	if (!list_empty(&ep->queue))
		restart = 1;

	if (ep->use_dma)
		dmac_free_channel(ep->r8a66597, ep, req);

	spin_unlock(&ep->r8a66597->lock);
	req->req.complete(&ep->ep, &req->req);
	spin_lock(&ep->r8a66597->lock);

	if (restart) {
		req = get_request_from_ep(ep);
		if (ep->ep.desc)
			start_packet(ep, req);
	}
}

static void irq_ep0_write(struct r8a66597_ep *ep, struct r8a66597_request *req)
{
	int i;
	u16 tmp;
	unsigned bufsize;
	size_t size;
	void *buf;
	u16 pipenum = ep->pipenum;
	struct r8a66597 *r8a66597 = ep->r8a66597;

	pipe_change(r8a66597, pipenum);
	r8a66597_bset(r8a66597, ISEL, ep->fifosel);

	i = 0;
	do {
		tmp = r8a66597_read(r8a66597, ep->fifoctr);
		if (i++ > PIPE_TIMEOUT_COUNT) {
			dev_err(r8a66597_to_dev(r8a66597),
				"pipe0 is busy. maybe cpu i/o bus "
				"conflict. please power off this controller.");
			return;
		}
		ndelay(1);
	} while ((tmp & FRDY) == 0);

	/* prepare parameters */
	bufsize = get_buffer_size(r8a66597, pipenum);
	buf = req->req.buf + req->req.actual;
	size = min(bufsize, req->req.length - req->req.actual);

	/* write fifo */
	if (req->req.buf) {
		if (size > 0)
			r8a66597_write_fifo(r8a66597, ep, buf, size);
		if ((size == 0) || ((size % ep->ep.maxpacket) != 0))
			r8a66597_bset(r8a66597, BVAL, ep->fifoctr);
	}

	/* update parameters */
	req->req.actual += size;

	/* check transfer finish */
	if ((!req->req.zero && (req->req.actual == req->req.length))
			|| (size % ep->ep.maxpacket)
			|| (size == 0)) {
		disable_irq_ready(r8a66597, pipenum);
		disable_irq_empty(r8a66597, pipenum);
	} else {
		disable_irq_ready(r8a66597, pipenum);
		enable_irq_empty(r8a66597, pipenum);
	}
	pipe_start(r8a66597, pipenum);
}

static void irq_packet_write(struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	u16 tmp;
	unsigned bufsize;
	size_t size;
	void *buf;
	u16 pipenum = ep->pipenum;
	struct r8a66597 *r8a66597 = ep->r8a66597;

	pipe_change(r8a66597, pipenum);
	tmp = r8a66597_read(r8a66597, ep->fifoctr);
	if (unlikely((tmp & FRDY) == 0)) {
		pipe_stop(r8a66597, pipenum);
		pipe_irq_disable(r8a66597, pipenum);
		dev_err(r8a66597_to_dev(r8a66597),
			"write fifo not ready. pipnum=%d\n", pipenum);
		return;
	}

	/* prepare parameters */
	bufsize = get_buffer_size(r8a66597, pipenum);
	buf = req->req.buf + req->req.actual;
	size = min(bufsize, req->req.length - req->req.actual);

	/* write fifo */
	if (req->req.buf) {
		r8a66597_write(r8a66597, ~(1 << pipenum), BEMPSTS);
		r8a66597_write_fifo(r8a66597, ep, buf, size);
		if ((size == 0)
				|| ((size % ep->ep.maxpacket) != 0)
				|| ((bufsize != ep->ep.maxpacket)
					&& (bufsize > size)))
			r8a66597_bset(r8a66597, BVAL, ep->fifoctr);
	}

	/* update parameters */
	req->req.actual += size;
	/* check transfer finish */
	if ((!req->req.zero && (req->req.actual == req->req.length))
			|| (size % ep->ep.maxpacket)
			|| (size == 0)) {
		disable_irq_ready(r8a66597, pipenum);
		enable_irq_empty(r8a66597, pipenum);
	} else {
		disable_irq_empty(r8a66597, pipenum);
		pipe_irq_enable(r8a66597, pipenum);
	}
}

static void irq_packet_read(struct r8a66597_ep *ep,
				struct r8a66597_request *req)
{
	u16 tmp;
	int rcv_len, bufsize, req_len;
	int size;
	void *buf;
	u16 pipenum = ep->pipenum;
	struct r8a66597 *r8a66597 = ep->r8a66597;
	int finish = 0;

	pipe_change(r8a66597, pipenum);
	tmp = r8a66597_read(r8a66597, ep->fifoctr);
	if (unlikely((tmp & FRDY) == 0)) {
		req->req.status = -EPIPE;
		pipe_stop(r8a66597, pipenum);
		pipe_irq_disable(r8a66597, pipenum);
		dev_err(r8a66597_to_dev(r8a66597), "read fifo not ready (%d)",
			pipenum);
		return;
	}

	/* prepare parameters */
	rcv_len = tmp & DTLN;
	bufsize = get_buffer_size(r8a66597, pipenum);

	buf = req->req.buf + req->req.actual;
	req_len = req->req.length - req->req.actual;
	if (rcv_len < bufsize)
		size = min(rcv_len, req_len);
	else
		size = min(bufsize, req_len);

	/* update parameters */
	req->req.actual += size;

	/* check transfer finish */
	if ((!req->req.zero && (req->req.actual == req->req.length))
			|| (size % ep->ep.maxpacket)
			|| (size == 0)) {
		pipe_stop(r8a66597, pipenum);
		pipe_irq_disable(r8a66597, pipenum);
		finish = 1;
	}

	/* read fifo */
	if (req->req.buf) {
		if (size == 0)
			r8a66597_write(r8a66597, BCLR, ep->fifoctr);
		else
			r8a66597_read_fifo(r8a66597, ep->fifoaddr, buf, size);
	}

	if ((ep->pipenum != 0) && finish)
		transfer_complete(ep, req, 0);
}

static void irq_pipe_ready(struct r8a66597 *r8a66597, u16 status, u16 enb)
{
	u16 check;
	u16 pipenum;
	struct r8a66597_ep *ep;
	struct r8a66597_request *req;

	if ((status & BRDY0) && (enb & BRDY0)) {
		r8a66597_write(r8a66597, ~BRDY0, BRDYSTS);

		ep = &r8a66597->ep[0];
		req = get_request_from_ep(ep);
		irq_packet_read(ep, req);
	} else {
		for (pipenum = 1; pipenum < R8A66597_MAX_NUM_PIPE; pipenum++) {
			check = 1 << pipenum;
			if ((status & check) && (enb & check)) {
				r8a66597_write(r8a66597, ~check, BRDYSTS);
				ep = r8a66597->pipenum2ep[pipenum];
				req = get_request_from_ep(ep);
				if (ep->ep.desc->bEndpointAddress & USB_DIR_IN)
					irq_packet_write(ep, req);
				else
					irq_packet_read(ep, req);
			}
		}
	}
}

static void irq_pipe_empty(struct r8a66597 *r8a66597, u16 status,
				u16 enb, u16 intenb0)
{
	u16 tmp;
	u16 check;
	u16 pipenum;
	struct r8a66597_ep *ep;
	struct r8a66597_request *req;

	if ((status & BEMP0) && (enb & BEMP0)) {
		r8a66597_write(r8a66597, ~BEMP0, BEMPSTS);

		ep = &r8a66597->ep[0];
		req = get_request_from_ep(ep);
		irq_ep0_write(ep, req);
	} else {
		for (pipenum = 1; pipenum < R8A66597_MAX_NUM_PIPE; pipenum++) {
			check = 1 << pipenum;
			if ((status & check) && (enb & check)) {
				r8a66597_write(r8a66597, ~check, BEMPSTS);
				tmp = control_reg_get(r8a66597, pipenum);
				if ((tmp & INBUFM) == 0) {
					control_reg_set_pid(
					r8a66597, pipenum, PID_NAK);
					r8a66597_write(
					r8a66597,
					(intenb0 & ~(BEMPE | NRDYE | BRDYE)),
					INTENB0);
					r8a66597_write(
					r8a66597, (enb & (~(1 << pipenum))),
					BEMPENB);
					r8a66597_bclr(
					r8a66597, (1 << pipenum), BRDYENB);
					r8a66597_bclr(
					r8a66597, (1 << pipenum), NRDYENB);
					r8a66597_write(
					r8a66597, intenb0, INTENB0);
					/*pipe_stop(r8a66597, pipenum);*/
					/*moved to before register accesses */
					/*control_reg_set_pid(
					r8a66597, pipenum, PID_NAK);*/
					r8a66597_wait_pbusy(r8a66597, pipenum);
					ep = r8a66597->pipenum2ep[pipenum];
					req = get_request_from_ep(ep);
					if (!list_empty(&ep->queue))
						transfer_complete(ep, req, 0);
				}
			}
		}
	}
}

static void get_status(struct r8a66597 *r8a66597, struct usb_ctrlrequest *ctrl)
__releases(r8a66597->lock)
__acquires(r8a66597->lock)
{
	struct r8a66597_ep *ep;
	u16 pid;
	u16 status = 0;
	u16 w_index = le16_to_cpu(ctrl->wIndex);
	u8 otg_status = 0;

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		status = r8a66597->device_status;
		break;
	case USB_RECIP_INTERFACE:
		status = 0;
		break;
	case USB_RECIP_ENDPOINT:
		ep = r8a66597->epaddr2ep[w_index & USB_ENDPOINT_NUMBER_MASK];
		pid = control_reg_get_pid(r8a66597, ep->pipenum);
		if (pid == PID_STALL)
			status = 1 << USB_ENDPOINT_HALT;
		else
			status = 0;
		break;
	default:
		pipe_stall(r8a66597, 0);
		return;		/* exit */
	}

	r8a66597->ep0_data = cpu_to_le16(status);
	r8a66597->ep0_req->buf = &r8a66597->ep0_data;
	r8a66597->ep0_req->length = 2;

	if (ctrl->bRequestType & USB_DIR_IN) {
		if ((w_index == USB_OTG_STATUS_SELECTOR) && (is_otg_mode)) {
			struct usb_phy *otg = usb_get_phy(0);
			if (1 == otg->flags)
				otg_status = 0x01;/*1 -> USB_OTG_HOST_REQ_FLAG*/
			else
				otg_status = 0x00;

			memcpy(r8a66597->ep0_req->buf, &otg_status, 1);
			r8a66597->ep0_req->length = 1;
			r8a66597->host_request_flag = 1;
			udc_log("USB_OTG_STATUS_SELECTOR, send status = %x\n",
								otg_status);
			usb_put_phy(otg);
		}
	}

	/* AV: what happens if we get called again before that gets through? */
	spin_unlock(&r8a66597->lock);
	r8a66597_queue(r8a66597->gadget.ep0, r8a66597->ep0_req, GFP_KERNEL);
	spin_lock(&r8a66597->lock);
}

static void clear_feature(struct r8a66597 *r8a66597,
				struct usb_ctrlrequest *ctrl)
{
	u16 w_value = le16_to_cpu(ctrl->wValue);

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		switch (w_value) {
		case USB_DEVICE_REMOTE_WAKEUP:
			control_end(r8a66597, 1);
			break;
		case USB_DEVICE_TEST_MODE:
			control_end(r8a66597, 1);
			break;
		default:
			pipe_stall(r8a66597, 0);
			break;
		}
		break;
	case USB_RECIP_INTERFACE:
		control_end(r8a66597, 1);
		break;
	case USB_RECIP_ENDPOINT:
		switch (w_value) {
		case USB_ENDPOINT_HALT: {
			struct r8a66597_ep *ep;
			struct r8a66597_request *req;
			u16 w_index = le16_to_cpu(ctrl->wIndex);

			ep = r8a66597->epaddr2ep[w_index & USB_ENDPOINT_NUMBER_MASK];
			if (!ep->wedge) {
				control_reg_sqclr(r8a66597, ep->pipenum);
				spin_unlock(&r8a66597->lock);
				usb_ep_clear_halt(&ep->ep);
				spin_lock(&r8a66597->lock);
			}

			control_end(r8a66597, 1);

			req = get_request_from_ep(ep);
			if (ep->busy) {
				ep->busy = 0;
				if (list_empty(&ep->queue))
					break;
				start_packet(ep, req);
			} else if (!list_empty(&ep->queue))
				pipe_start(r8a66597, ep->pipenum);
			break;
		}
		default:
			pipe_stall(r8a66597, 0);
			break;
		}
		break;
	default:
		pipe_stall(r8a66597, 0);
		break;
	}
}

static void set_feature(struct r8a66597 *r8a66597, struct usb_ctrlrequest *ctrl)
{
	u16 tmp;
	int timeout = 3000;
	u16 w_value = le16_to_cpu(ctrl->wValue);

	switch (ctrl->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		switch (w_value) {
		case USB_DEVICE_REMOTE_WAKEUP:
			control_end(r8a66597, 1);
			break;
		case USB_DEVICE_TEST_MODE:
			control_end(r8a66597, 1);
			/* Wait for the completion of status stage */
			do {
				tmp = r8a66597_read(r8a66597, INTSTS0) & CTSQ;
				udelay(1);
			} while (tmp != CS_IDST || timeout-- > 0);

			if (tmp == CS_IDST)
				r8a66597_bset(r8a66597,
						le16_to_cpu(ctrl->wIndex >> 8),
						TESTMODE);
			break;
		case USB_DEVICE_B_HNP_ENABLE:
			if (is_otg_mode) {
				r8a66597->gadget.b_hnp_enable = 1;
				control_end(r8a66597, 1);
			}
			break;
		default:
			pipe_stall(r8a66597, 0);
			break;
		}
		break;
	case USB_RECIP_INTERFACE:
		control_end(r8a66597, 1);
		break;
	case USB_RECIP_ENDPOINT:
		switch (w_value) {
		case USB_ENDPOINT_HALT: {
			struct r8a66597_ep *ep;
			u16 w_index = le16_to_cpu(ctrl->wIndex);

			ep = r8a66597->epaddr2ep[w_index & USB_ENDPOINT_NUMBER_MASK];
			pipe_stall(r8a66597, ep->pipenum);
			mdelay(1);/* allow pipe to stall.fix for CV failure */
			control_end(r8a66597, 1);
			break;
		}
		default:
			pipe_stall(r8a66597, 0);
			break;
		}
		break;
	default:
		pipe_stall(r8a66597, 0);
		break;
	}
}

/* if return value is true, call class driver's setup() */
static int setup_packet(struct r8a66597 *r8a66597, struct usb_ctrlrequest *ctrl)
{
	u16 *p = (u16 *)ctrl;
	unsigned long offset = USBREQ;
	int i, ret = 0;

	/* read fifo */
	r8a66597_write(r8a66597, ~VALID, INTSTS0);

	for (i = 0; i < 4; i++)
		p[i] = r8a66597_read(r8a66597, offset + i*2);

	/* check request */
	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (ctrl->bRequest) {
		case USB_REQ_GET_STATUS:
			get_status(r8a66597, ctrl);
			break;
		case USB_REQ_CLEAR_FEATURE:
			clear_feature(r8a66597, ctrl);
			break;
		case USB_REQ_SET_FEATURE:
			set_feature(r8a66597, ctrl);
			break;
		default:
			ret = 1;
			break;
		}
	} else
		ret = 1;
	return ret;
}

static int r8a66597_set_vbus_draw(struct r8a66597 *r8a66597, int mamp)
{
	udc_log("%s,%d\n", __func__, mamp);
	switch (mamp) {
	case SUSPEND_CURRENT:
		r8a66597->vbus_current = SUSPEND_CURRENT;
		schedule_delayed_work(&r8a66597->spa_work, 0);
		break;
	case UNCONFIG_CURRENT:
		r8a66597->vbus_current = UNCONFIG_CURRENT;
		schedule_delayed_work(&r8a66597->spa_work,
					msecs_to_jiffies(200));
		break;
	case CONFIG_CURRENT:
		r8a66597->vbus_current = CONFIG_CURRENT;
		schedule_delayed_work(&r8a66597->spa_work,
					msecs_to_jiffies(200));
		break;
	default:
		udc_log("%s,%d\n", __func__, mamp);
		break;
	}
	return 1;
}

static void r8a66597_update_usb_speed(struct r8a66597 *r8a66597)
{
	u16 speed = get_usb_speed(r8a66597);

	switch (speed) {
	case HSMODE:
		r8a66597->gadget.speed = USB_SPEED_HIGH;
		break;
	case FSMODE:
		r8a66597->gadget.speed = USB_SPEED_FULL;
		break;
	default:
		r8a66597->gadget.speed = USB_SPEED_UNKNOWN;
		udc_log("%s:%s",__func__, "USB speed unknown\n");
	}
}

static void r8a66597_usb_resume(struct r8a66597 *r8a66597)
{
	if (r8a66597->usb_suspended && r8a66597->driver->resume) {
		udc_log("%s\n", __func__);
		spin_unlock(&r8a66597->lock);
		r8a66597->driver->resume(&r8a66597->gadget);
		r8a66597->usb_suspended = 0;
		spin_lock(&r8a66597->lock);
	}
}

static void irq_device_state(struct r8a66597 *r8a66597)
{

	u16 dvsq;
	struct usb_phy *otg = NULL;

	if (is_otg_mode)
		otg = usb_get_phy(0);

	dvsq = r8a66597_read(r8a66597, INTSTS0) & DVSQ;
	r8a66597_write(r8a66597, ~DVST, INTSTS0);

	if (dvsq == DS_DFLT) {
		r8a66597_usb_resume(r8a66597);

		/* bus reset */
#ifndef CONFIG_USB_MTP_SAMSUNG
		spin_unlock(&r8a66597->lock);
		r8a66597->driver->disconnect(&r8a66597->gadget);
		spin_lock(&r8a66597->lock);
#endif
	  udc_log("%s: USB BUS Reset speed = %d\n", __func__, r8a66597->gadget.speed);
		r8a66597_update_usb_speed(r8a66597);
		spin_unlock(&r8a66597->lock);
#ifdef custom_t_BATTERY
		r8a66597_inform_vbus_power(r8a66597, 500);
#else
		r8a66597_inform_vbus_power(r8a66597, 100);
#endif
		spin_lock(&r8a66597->lock);
		if (MUIC_IS_PRESENT) {
			if (++reset_resume_ctr > RECOVER_RESUME_COUNT) {
				/*More then 1 sec*/
			printk(KERN_INFO "%s: usb state stuck in DS_DFLT\nGoing to perform phyreset\n",__func__);
			r8a66597->is_active=0;
			r8a66597->vbus_active=0;
			if (!wake_lock_active(&r8a66597->wake_lock))
				wake_lock(&r8a66597->wake_lock);
			schedule_delayed_work(&r8a66597->vbus_work, 0);
			reset_resume_ctr = 0;
			return;
			}
		}
		r8a66597_update_usb_speed(r8a66597);
		if (r8a66597->gadget.speed == USB_SPEED_UNKNOWN)
			r8a66597_bclr(r8a66597, HSE, SYSCFG0);

		if (is_otg_mode) {
			if (otg->state == OTG_STATE_A_SUSPEND) {
				otg->state = OTG_STATE_A_PERIPHERAL;
				udc_log("%s\n",
					usb_otg_state_string(otg->state));
			}
		}
	}
	if (r8a66597->old_dvsq == DS_CNFG && dvsq != DS_CNFG){
		udc_log("%s: USB Not Config speed = %d\n", __func__, r8a66597->gadget.speed);
		r8a66597_update_usb_speed(r8a66597);
		reset_resume_ctr = 0;
		}
	if ((dvsq == DS_CNFG || dvsq == DS_ADDS)
			&& r8a66597->gadget.speed == USB_SPEED_UNKNOWN)
		{
			udc_log("%s: USB Config speed = %d\n", __func__, r8a66597->gadget.speed);
			r8a66597_update_usb_speed(r8a66597);
			reset_resume_ctr = 0;
		}

	if (dvsq & DS_SUSP){
		reset_resume_ctr=0;
		if (r8a66597->driver->suspend) {
			udc_log("%s:suspended in UDC\n", __func__);
			spin_unlock(&r8a66597->lock);
			r8a66597->usb_suspended = 1;
			r8a66597->driver->suspend(&r8a66597->gadget);
			spin_lock(&r8a66597->lock);
		}
	}

	if (is_otg_mode) {
		if ((r8a66597->old_dvsq == DS_CNFG) && (dvsq & DS_SPD_CNFG)
			&& r8a66597->gadget.b_hnp_enable
				&& r8a66597->host_request_flag) {
			schedule_delayed_work(&r8a66597->hnp_work, 0);
			r8a66597->host_request_flag = 0;
		}
		usb_put_phy(otg);
	}

	r8a66597->old_dvsq = dvsq;
}

static void irq_control_stage(struct r8a66597 *r8a66597)
__releases(r8a66597->lock)
__acquires(r8a66597->lock)
{
	struct usb_ctrlrequest ctrl;
	u16 ctsq;

	ctsq = r8a66597_read(r8a66597, INTSTS0) & CTSQ;
	r8a66597_write(r8a66597, ~CTRT, INTSTS0);

	switch (ctsq) {
	case CS_IDST: {
		struct r8a66597_ep *ep;
		struct r8a66597_request *req;
		ep = &r8a66597->ep[0];
		req = get_request_from_ep(ep);
		transfer_complete(ep, req, 0);
		break;
	}

	case CS_RDDS:
	case CS_WRDS:
	case CS_WRND:
		if (setup_packet(r8a66597, &ctrl)) {
			spin_unlock(&r8a66597->lock);
			if (r8a66597->driver->setup(&r8a66597->gadget, &ctrl)
				< 0)
				pipe_stall(r8a66597, 0);
			spin_lock(&r8a66597->lock);
		}
		break;
	case CS_RDSS:
	case CS_WRSS:
		control_end(r8a66597, 0);
		break;
	default:
		dev_err(r8a66597_to_dev(r8a66597),
			"ctrl_stage: unexpect ctsq(%x)\n", ctsq);
		break;
	}
}

static irqreturn_t r8a66597_irq(int irq, void *_r8a66597)
{
	struct r8a66597 *r8a66597 = _r8a66597;
	u16 intsts0;
	u16 intenb0;
	u16 brdysts, bempsts;
	u16 brdyenb, bempenb;
	u16 mask0;
	u16 syscfg;

	spin_lock(&r8a66597->lock);

	syscfg = r8a66597_read(r8a66597, SYSCFG0);
	r8a66597->role = syscfg & DCFM;

	intsts0 = r8a66597_read(r8a66597, INTSTS0);
	intenb0 = r8a66597_read(r8a66597, INTENB0);

	mask0 = intsts0 & intenb0;
	if (mask0) {
		if (mask0 & VBINT) {
			r8a66597_write(r8a66597, INTSTS_REG_MASK & ~VBINT,
					INTSTS0);
			r8a66597_start_xclock(r8a66597);

			/* start vbus sampling */
			r8a66597->old_vbus = r8a66597_read(r8a66597, INTSTS0)
					& VBSTS;
			r8a66597->scount = R8A66597_MAX_SAMPLING;

			mod_timer(&r8a66597->timer,
					jiffies + msecs_to_jiffies(50));
		}
		if (intsts0 & DVST)
			irq_device_state(r8a66597);

		if ((intsts0 & BRDY) && (intenb0 & BRDYE)
			&& (!r8a66597->role)) {
			brdysts = r8a66597_read(r8a66597, BRDYSTS);
			brdyenb = r8a66597_read(r8a66597, BRDYENB);
			irq_pipe_ready(r8a66597, brdysts, brdyenb);
		}
		if ((intsts0 & BEMP) && (intenb0 & BEMPE)
			&& (!r8a66597->role)) {
			bempenb = r8a66597_read(r8a66597, BEMPENB);
			bempsts = r8a66597_read(r8a66597, BEMPSTS);
			irq_pipe_empty(r8a66597, bempsts, bempenb, intenb0);
		}

		if (intsts0 & CTRT)
			irq_control_stage(r8a66597);
		if (intsts0 & RESM) {
			r8a66597_usb_resume(r8a66597);
			r8a66597_bclr(r8a66597,  RESM, INTSTS0);
			r8a66597_dma_reset(r8a66597);
		}
	}

	spin_unlock(&r8a66597->lock);
	return IRQ_HANDLED;
}

static void dma_write_complete(struct r8a66597 *r8a66597,
			       struct r8a66597_dma *dma)
{
	struct r8a66597_ep *ep = dma->ep;
	struct r8a66597_request *req = get_request_from_ep(ep);
	int ch = dma->channel;
	u16 tmp;

	r8a66597_dma_bclr(r8a66597, DE | IE | TOE, USBHS_DMAC_CHCR(ch));
	req->req.actual += req->req.length;

	/* Clear interrupt flag for next transfer. */
	r8a66597_write(r8a66597, ~(1 << ep->pipenum), BRDYSTS);

	if (req->req.zero && !(req->req.actual % ep->ep.maxpacket)) {
		/* Send zero-packet by irq_packet_write(). */
		tmp = control_reg_get(r8a66597, ep->pipenum);
		if (tmp & BSTS)
			irq_packet_write(ep, req);
		else
			enable_irq_ready(r8a66597, ep->pipenum);
	} else {
		/* To confirm the end of transmit */
		enable_irq_empty(r8a66597, ep->pipenum);
	}
	r8a66597_dma_bclr(r8a66597, TE | DE, USBHS_DMAC_CHCR(ch));
}

static unsigned long usb_dma_calc_received_size(struct r8a66597 *r8a66597,
						struct r8a66597_dma *dma,
						u16 size)
{
	struct r8a66597_ep *ep = dma->ep;
	int ch = dma->channel;
	unsigned long received_size;

	/*
	 * DAR will increment the value every transfer-unit-size,
	 * but the "size" (DTLN) will be set within MaxPacketSize.
	 *
	 * The calucuation would be:
	 *   (((DAR-SAR) - TransferUnitSize) & ~MaxPacketSize) + DTLN.
	 *
	 * Be careful that if the "size" is zero, no correction is needed.
	 * Just return (DAR-SAR) as-is.
	 */
	received_size = r8a66597_dma_read(r8a66597, USBHS_DMAC_DAR(ch)) -
			r8a66597_dma_read(r8a66597, USBHS_DMAC_SAR(ch));
	if (size) {
		received_size -= dma->tx_size;
		received_size &= ~(ep->ep.maxpacket - 1);
		received_size += size; /* DTLN */
	}

	return received_size;
}

static void dma_read_complete(struct r8a66597 *r8a66597,
			      struct r8a66597_dma *dma)
{
	struct r8a66597_ep *ep = dma->ep;
	struct r8a66597_request *req;
	int ch = dma->channel;
	unsigned short tmp, size;

	/* Clear interrupt flag for next transfer. */
	r8a66597_write(r8a66597, ~(1 << ep->pipenum), BRDYSTS);

	tmp = r8a66597_read(r8a66597, ep->fifoctr);
	size = tmp & DTLN;
	r8a66597_bset(r8a66597, BCLR, ep->fifoctr);
	req = get_request_from_ep(ep);

	req->req.actual += usb_dma_calc_received_size(r8a66597, dma, size);

	if (r8a66597_dma_read(r8a66597, USBHS_DMAC_CHCR(ch)) & NULLF) {
		/*
		 * When a NULL packet is received during a DMA transfer,
		 * the DMA transfer can be suspended and resumed on each
		 * channel independently in the following sequence.
		 */
		u32 chcr;

		r8a66597_bclr(r8a66597, DREQE, ep->fifosel);
		/* wait for the internal bus to be stabilized (20clk@ZS) */
		udelay(1);
		chcr = r8a66597_dma_read(r8a66597, USBHS_DMAC_CHCR(ch));
		chcr = (chcr & ~(NULLF | DE)) | FTE;
		r8a66597_dma_write(r8a66597, chcr, USBHS_DMAC_CHCR(ch));
		r8a66597_dma_bclr(r8a66597, IE | SP, USBHS_DMAC_CHCR(ch));
	} else {
		r8a66597_dma_bclr(r8a66597, IE | SP | DE, USBHS_DMAC_CHCR(ch));
	}

	r8a66597_dma_bclr(r8a66597, TE, USBHS_DMAC_CHCR(ch));
	pipe_stop(r8a66597, ep->pipenum);
	transfer_complete(ep, req, 0);
}

static irqreturn_t r8a66597_dma_irq(int irq, void *_r8a66597)
{
	struct r8a66597 *r8a66597 = _r8a66597;
	unsigned long flags;
	u32 dmicrsts;
	int ch;
	irqreturn_t ret = IRQ_NONE;

        spin_lock_irqsave(&r8a66597->lock, flags);

	dmicrsts = r8a66597_dma_read(r8a66597, DMICR);

	for (ch = 0; ch < R8A66597_MAX_DMA_CHANNELS; ch++) {
		if (!(dmicrsts & r8a66597->dma[ch].expect_dmicr))
			continue;
		ret = IRQ_HANDLED;

		if (r8a66597->dma[ch].dir)
			dma_write_complete(r8a66597, &r8a66597->dma[ch]);
		else
			dma_read_complete(r8a66597, &r8a66597->dma[ch]);
	}

	spin_unlock_irqrestore(&r8a66597->lock, flags);
	return ret;
}

static void r8a66597_timer(unsigned long _r8a66597)
{
	struct r8a66597 *r8a66597 = (struct r8a66597 *)_r8a66597;
	unsigned long flags;
	u16 tmp;

	spin_lock_irqsave(&r8a66597->lock, flags);
	tmp = r8a66597_read(r8a66597, SYSCFG0);
	if (r8a66597->scount > 0) {
		tmp = r8a66597_read(r8a66597, INTSTS0) & VBSTS;
		if (tmp == r8a66597->old_vbus) {
			r8a66597->scount--;
			if (r8a66597->scount == 0) {
				if (tmp == VBSTS) {
					if (r8a66597->pdata->module_start)
						r8a66597->pdata->module_start();

					init_controller(r8a66597);
					r8a66597_usb_connect(r8a66597);
				} else {
					r8a66597_usb_disconnect(r8a66597);

					if (r8a66597->pdata->module_stop)
						r8a66597->pdata->module_stop();

					/* for subsequent VBINT detection */
					init_controller(r8a66597);
					r8a66597_bset(r8a66597, VBSE, INTENB0);
				}
			} else {
				mod_timer(&r8a66597->timer,
					jiffies + msecs_to_jiffies(50));
			}
		} else {
			r8a66597->scount = R8A66597_MAX_SAMPLING;
			r8a66597->old_vbus = tmp;
			mod_timer(&r8a66597->timer,
					jiffies + msecs_to_jiffies(50));
		}
	}
	spin_unlock_irqrestore(&r8a66597->lock, flags);
}

/*-------------------------------------------------------------------------*/
static int r8a66597_enable(struct usb_ep *_ep,
			 const struct usb_endpoint_descriptor *desc)
{
	struct r8a66597_ep *ep;

	ep = container_of(_ep, struct r8a66597_ep, ep);
	return alloc_pipe_config(ep, desc);
}

static int r8a66597_disable(struct usb_ep *_ep)
{
	struct r8a66597_ep *ep;
	struct r8a66597_request *req;
	unsigned long flags;

	ep = container_of(_ep, struct r8a66597_ep, ep);
	BUG_ON(!ep);

	while (!list_empty(&ep->queue)) {
		req = get_request_from_ep(ep);
		spin_lock_irqsave(&ep->r8a66597->lock, flags);
		pipe_stop(ep->r8a66597, ep->pipenum);
		dmac_cancel(ep, req);
		pipe_irq_disable(ep->r8a66597, ep->pipenum);
		r8a66597_write(ep->r8a66597, ~(1 << ep->pipenum), BRDYSTS);
		r8a66597_write(ep->r8a66597, ~(1 << ep->pipenum), BEMPSTS);
		transfer_complete(ep, req, -ECONNRESET);
		spin_unlock_irqrestore(&ep->r8a66597->lock, flags);
	}

	return free_pipe_config(ep);
}

static struct usb_request *r8a66597_alloc_request(struct usb_ep *_ep,
						gfp_t gfp_flags)
{
	struct r8a66597_request *req;

	req = kzalloc(sizeof(struct r8a66597_request), gfp_flags);
	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void r8a66597_free_request(struct usb_ep *_ep, struct usb_request *_req)
{
	struct r8a66597_request *req;

	req = container_of(_req, struct r8a66597_request, req);
	kfree(req);
}

static int r8a66597_queue(struct usb_ep *_ep, struct usb_request *_req,
			gfp_t gfp_flags)
{
	struct r8a66597_ep *ep;
	struct r8a66597_request *req;
	unsigned long flags;
	int request = 0;

	ep = container_of(_ep, struct r8a66597_ep, ep);
	req = container_of(_req, struct r8a66597_request, req);

	if (ep->r8a66597->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	spin_lock_irqsave(&ep->r8a66597->lock, flags);

	if (list_empty(&ep->queue))
		request = 1;

	list_add_tail(&req->queue, &ep->queue);
	req->req.actual = 0;
	req->req.status = -EINPROGRESS;

	if (ep->ep.desc == NULL)	/* control */
		start_ep0(ep, req);
	else {
		if (request && !ep->busy)
			start_packet(ep, req);
	}

	spin_unlock_irqrestore(&ep->r8a66597->lock, flags);

	return 0;
}

static int r8a66597_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct r8a66597_ep *ep;
	struct r8a66597_request *req;
	unsigned long flags;

	ep = container_of(_ep, struct r8a66597_ep, ep);
	req = container_of(_req, struct r8a66597_request, req);

	spin_lock_irqsave(&ep->r8a66597->lock, flags);
	if (!list_empty(&ep->queue)) {
		pipe_stop(ep->r8a66597, ep->pipenum);
		if (ep->pipetrn)
			r8a66597_write(ep->r8a66597, TRCLR, ep->pipetre);
		dmac_cancel(ep, req);
		pipe_irq_disable(ep->r8a66597, ep->pipenum);
		r8a66597_write(ep->r8a66597, ~(1 << ep->pipenum), BRDYSTS);
		r8a66597_write(ep->r8a66597, ~(1 << ep->pipenum), BEMPSTS);
		transfer_complete(ep, req, -ECONNRESET);
	}
	spin_unlock_irqrestore(&ep->r8a66597->lock, flags);

	return 0;
}

static int r8a66597_set_halt(struct usb_ep *_ep, int value)
{
	struct r8a66597_ep *ep;
	unsigned long flags;
	int ret = 0;

	ep = container_of(_ep, struct r8a66597_ep, ep);

	spin_lock_irqsave(&ep->r8a66597->lock, flags);
	if (!list_empty(&ep->queue)) {
		ret = -EAGAIN;
		goto out;
	}
	if (value) {
		ep->busy = 1;
		pipe_stall(ep->r8a66597, ep->pipenum);
	} else {
		ep->busy = 0;
		ep->wedge = 0;
		pipe_stop(ep->r8a66597, ep->pipenum);
	}

out:
	spin_unlock_irqrestore(&ep->r8a66597->lock, flags);
	return ret;
}

static int r8a66597_set_wedge(struct usb_ep *_ep)
{
	struct r8a66597_ep *ep;
	unsigned long flags;

	ep = container_of(_ep, struct r8a66597_ep, ep);

	if (!ep || !ep->ep.desc)
		return -EINVAL;

	spin_lock_irqsave(&ep->r8a66597->lock, flags);
	ep->wedge = 1;
	spin_unlock_irqrestore(&ep->r8a66597->lock, flags);

	return usb_ep_set_halt(_ep);
}

static void r8a66597_fifo_flush(struct usb_ep *_ep)
{
	struct r8a66597_ep *ep;
	unsigned long flags;

	ep = container_of(_ep, struct r8a66597_ep, ep);
	spin_lock_irqsave(&ep->r8a66597->lock, flags);
	if (list_empty(&ep->queue) && !ep->busy) {
		pipe_stop(ep->r8a66597, ep->pipenum);
		r8a66597_bclr(ep->r8a66597, BCLR, ep->fifoctr);
		r8a66597_write(ep->r8a66597, ACLRM, ep->pipectr);
		r8a66597_write(ep->r8a66597, 0, ep->pipectr);
	}
	spin_unlock_irqrestore(&ep->r8a66597->lock, flags);
}

static struct usb_ep_ops r8a66597_ep_ops = {
	.enable		= r8a66597_enable,
	.disable	= r8a66597_disable,

	.alloc_request	= r8a66597_alloc_request,
	.free_request	= r8a66597_free_request,

	.queue		= r8a66597_queue,
	.dequeue	= r8a66597_dequeue,

	.set_halt	= r8a66597_set_halt,
	.set_wedge	= r8a66597_set_wedge,
	.fifo_flush	= r8a66597_fifo_flush,
};

/*-------------------------------------------------------------------------*/
static struct r8a66597 *the_controller;

static int r8a66597_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	struct r8a66597 *r8a66597 = gadget_to_r8a66597(gadget);
	int ret = 0;
	u16 bwait = 0;

	if (!driver
			|| driver->max_speed < USB_SPEED_HIGH
			|| !driver->setup)
		return -EINVAL;

	/* hook up the driver */
	r8a66597->driver = driver;

		if (r8a66597->is_active) {
			udc_log("%s: IN, no powerup\n", __func__);
			udc_log("%s: USB clock enable called by\n", __func__);
			usb_core_clk_ctrl(r8a66597, 1);
			bwait = r8a66597->pdata->buswait ?
			r8a66597->pdata->buswait : 15;
			if (r8a66597->pdata->module_start)
				r8a66597->pdata->module_start();

			/* start clock */
			r8a66597_write(r8a66597, bwait, SYSCFG1);
			r8a66597_bset(r8a66597, HSE, SYSCFG0);
			r8a66597_bset(r8a66597, USBE, SYSCFG0);
			r8a66597_bset(r8a66597, SCKE, SYSCFG0);
			r8a66597_bset(r8a66597, CTRE, INTENB0);
			r8a66597_bset(r8a66597, BEMPE | BRDYE, INTENB0);
			r8a66597_bset(r8a66597, RESM | DVSE, INTENB0);
			if (r8a66597->pdata->is_vbus_powered()) {
				udc_log("%s: IN, vbuspowered\n",
				__func__);
					vbus_valid = 1;

				if (!wake_lock_active(&r8a66597->
							wake_lock))
					wake_lock(&r8a66597->wake_lock);
					schedule_delayed_work(&r8a66597->
								vbus_work, 0);
			} else {
				udc_log("%s: IN, no vbuspowered\n",
						 __func__);
				r8a66597->is_active = 0;
				udc_log("%s: USB clock disable\n", __func__);
				usb_core_clk_ctrl(r8a66597, 0);
			}
		} else {
			init_controller(r8a66597);
			r8a66597_bset(r8a66597, VBSE, INTENB0);
			if (r8a66597_read(r8a66597, INTSTS0) & VBSTS) {
				r8a66597_start_xclock(r8a66597);
				/* start vbus sampling */
				r8a66597->old_vbus =
					r8a66597_read(r8a66597,
					INTSTS0) & VBSTS;
				r8a66597->scount = R8A66597_MAX_SAMPLING;
				mod_timer(&r8a66597->timer,
				jiffies + msecs_to_jiffies(50));
			}
		}
	return ret;
}

static int r8a66597_stop(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	struct r8a66597 *r8a66597 = gadget_to_r8a66597(gadget);
	unsigned long flags;

	cancel_delayed_work_sync(&r8a66597->vbus_work);

	spin_lock_irqsave(&r8a66597->lock, flags);
	r8a66597_bclr(r8a66597, VBSE, INTENB0);
	disable_controller(r8a66597);
	spin_unlock_irqrestore(&r8a66597->lock, flags);

#ifdef CONFIG_HAVE_CLK
	if (r8a66597->is_active) {
		udc_log("%s: USB clock disable called by\n", __func__);
		usb_core_clk_ctrl(r8a66597, 0);
		r8a66597->is_active = 0;
		udc_log("%s: power %s\n"
			, __func__, r8a66597->is_active ? "up" : "down");
	}
#endif

	r8a66597->driver = NULL;
	return 0;
}

/*-------------------------------------------------------------------------*/
static int r8a66597_get_frame(struct usb_gadget *_gadget)
{
	struct r8a66597 *r8a66597 = gadget_to_r8a66597(_gadget);
	return r8a66597_read(r8a66597, FRMNUM) & FRAME_MASK;
}

static int r8a66597_set_selfpowered(struct usb_gadget *gadget, int is_self)
{
	struct r8a66597 *r8a66597 = gadget_to_r8a66597(gadget);

	if (is_self)
		r8a66597->device_status |= 1 << USB_DEVICE_SELF_POWERED;
	else
		r8a66597->device_status &= ~(1 << USB_DEVICE_SELF_POWERED);

	return 0;
}

static int r8a66597_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{
	return r8a66597_set_vbus_draw(gadget_to_r8a66597(gadget), mA);
}

static void usb_reinitialize(struct r8a66597 *r8a66597)
{
	u16 bwait = r8a66597->pdata->buswait ? : 0xf;
	udc_log("%s: \n", __func__);
	if (r8a66597->pdata->module_stop)
		r8a66597->pdata->module_stop();
	udelay(10);
        if (r8a66597->pdata->module_start)
                r8a66597->pdata->module_start();
        /* start clock */
        r8a66597_write(r8a66597, bwait, SYSCFG1);
        r8a66597_bset(r8a66597, HSE, SYSCFG0);
        r8a66597_bset(r8a66597, USBE, SYSCFG0);
        r8a66597_bset(r8a66597, SCKE, SYSCFG0);
        r8a66597_bclr(r8a66597, DRPD, SYSCFG0);
}

static int r8a66597_pullup(struct usb_gadget *gadget, int is_on)
{
	struct r8a66597 *r8a66597 = gadget_to_r8a66597(gadget);
	unsigned long flags;
	udc_log("%s:\n", __func__);
	r8a66597->softconnect = (is_on != 0);
	if (r8a66597->vbus_active) {
		if (r8a66597->softconnect == 0)
			usb_reinitialize(r8a66597);
		else {
			spin_lock_irqsave(&r8a66597->lock, flags);
			r8a66597_usb_connect(r8a66597);
			spin_unlock_irqrestore(&r8a66597->lock, flags);
		}
	}
	return 0;
}

static int r8a66597_vbus_session(struct usb_gadget *gadget , int is_active)
{
	struct r8a66597 *r8a66597 = gadget_to_r8a66597(gadget);
	if (!is_active) { /* Disconnect pullup to support ioctl */
		r8a66597_bclr(r8a66597,	DPRPU, SYSCFG0);
	}

	return 0;
}

static void r8a66597_spa_work(struct work_struct *data)
{
	struct r8a66597 *r8a66597 =
			container_of(data, struct r8a66597, spa_work.work);
	unsigned int vbus_accy_current = r8a66597->vbus_current;
	udc_log("%s:IN\n", __func__);
#ifdef CONFIG_SEC_CHARGING_FEATURE
	switch (r8a66597->vbus_current) {
	case SUSPEND_CURRENT:
		r8a66597->vbus_current = SPA_EVT_USB_SUSPEND;
		break;
	case UNCONFIG_CURRENT:
		r8a66597->vbus_current = SPA_EVT_USB_UNCONFIG;
		break;
	case CONFIG_CURRENT:
		r8a66597->vbus_current = SPA_EVT_USB_CONFIG;
		break;
	}
	mutex_lock(&r8a66597->mutex);
	spa_event_handler(r8a66597->vbus_current,
					(void *)POWER_SUPPLY_TYPE_USB);
	mutex_unlock(&r8a66597->mutex);
#endif
	bcm_accy_event_handler(PMU_ACCY_EVT_OUT_CHRG_CURR,
				 (void *)vbus_accy_current);
}

extern void inform_muic_usb_event(int);
static void r8a66597_vbus_work(struct work_struct *work)
{
	struct r8a66597 *r8a66597 =
			container_of(work, struct r8a66597, vbus_work.work);
	struct usb_phy *otg = NULL;
	u16 syscfg = 0;
	u16 bwait = r8a66597->pdata->buswait ? : 0xf;
	unsigned long flags;
	int vbus_state = 0, ret = 0;

	if (is_otg_mode)
		otg = usb_get_phy(0);

	udc_log("%s: IN\n", __func__);
	if (!r8a66597->is_active && !r8a66597->vbus_active) {
		udc_log("%s: IN,powering up and phyreset\n", __func__);
		udc_log("%s: USB clock enable called by\n", __func__);
		usb_core_clk_ctrl(r8a66597, 1);
		if (r8a66597->pdata->module_start)
			r8a66597->pdata->module_start();
	}
	if (!MUIC_IS_PRESENT) {
		get_charger_type(r8a66597);
		if (r8a66597->charger_type == CHARGER_DCP) {
			udc_log("DCP Charger in VBUS WORK\n");
			return;
		}
		usb_phy_write(TUSB_VENDOR_SPECIFIC3_SET, 0x07);
	}

	if (is_otg_mode) {
		udc_log("\n HSUSB:UDC: %s(%d): INTSTS0:=%#x, Role:= %d\n",
			__func__, __LINE__, r8a66597_read(r8a66597, INTSTS0),
				r8a66597->role);
		syscfg = r8a66597_read(r8a66597, SYSCFG0);
		r8a66597->role = syscfg & DCFM;
		/*If the controller is in Host mode, return without doing
		* anything */
		if (r8a66597->role) {
			wake_unlock(&r8a66597->wake_lock);
			return;
		}
		otg = usb_get_phy(0);
		udc_log("\n HSUSB:UDC: %s(%d): INTSTS0:=%#x, Role:= %d\n",
				__func__, __LINE__, r8a66597_read(r8a66597,
				INTSTS0), r8a66597->role);
		if ((r8a66597_read(r8a66597, INTSTS0) & VBSTS)
						&& (!r8a66597->role)) {
			udc_log("\n HSUSB:UDC: %s(%d): OTG_STATE= %s -> %s\n",
			__func__, __LINE__, usb_otg_state_string(otg->state),
				usb_otg_state_string(OTG_STATE_B_PERIPHERAL));
			otg->state = OTG_STATE_B_PERIPHERAL;
		} else if (otg->state ==  OTG_STATE_B_PERIPHERAL) {
			udc_log("\n HSUSB:UDC: %s(%d): OTG_STATE= %s -> %s\n",
			__func__, __LINE__, usb_otg_state_string(otg->state),
				usb_otg_state_string(OTG_STATE_B_IDLE));
			otg->state = OTG_STATE_B_IDLE;
			r8a66597->gadget.b_hnp_enable = 0;
		}
		udc_log("%s\n", usb_otg_state_string(otg->state));
		usb_put_phy(otg);
	}

#if defined VBUS_HANDLE_IRQ_BASED
	vbus_state = vbus_valid;
#else
	vbus_state = r8a66597->pdata->is_vbus_powered();
#endif
	/* Clear VBUS Interrupt after reading */
	if (r8a66597_read(r8a66597, INTSTS0) & VBINT)
		r8a66597_bclr(r8a66597, VBINT, INTSTS0);
	dev_dbg(r8a66597_to_dev(r8a66597), "VBUS %s => %s\n",
	r8a66597->vbus_active ? "on" : "off",
		r8a66597->is_active ? "on" : "off");

	if ((r8a66597->vbus_active ^ vbus_state) == 0) {
		udc_log("%s: IN xor,r8a-isactive=%d\n",
			__func__, r8a66597->is_active);
		if (!vbus_state) {
			usb_core_clk_ctrl(r8a66597, 0);
			if (wake_lock_active(&r8a66597->wake_lock))
				wake_unlock(&r8a66597->wake_lock);
		}
		return;
	}

	if (vbus_state) {
		udc_log("%s: IN,vbus_state=%d\n",
			__func__, vbus_valid);
		/* start clock */
		r8a66597_write(r8a66597, bwait, SYSCFG1);
		r8a66597_bset(r8a66597, HSE, SYSCFG0);
		r8a66597_bset(r8a66597, USBE, SYSCFG0);
		r8a66597_bset(r8a66597, SCKE, SYSCFG0);
		r8a66597_usb_connect(r8a66597);
		if (!MUIC_IS_PRESENT)
			inform_muic_usb_event(1);
		ret = cpufreq_add_lmt_req(&cpufreq_node, r8a66597->gadget.name,
					  FREQ_MAX, FREQ_MIN_LIMIT);
		if (ret) {
			pr_info("%s ERROR in cpufreq_add_lmt_req\n", __func__);
			dfs_started = 1;
		} else
			dfs_started = 0;
		pr_info("%s(): cpufreq_add_lmt_req\n", __func__);
	} else {
		udc_log("%s: IN,vbus_state=%d\n",
			__func__, vbus_valid);
		spin_lock_irqsave(&r8a66597->lock, flags);
		r8a66597_usb_disconnect(r8a66597);
		spin_unlock_irqrestore(&r8a66597->lock, flags);
		if (!MUIC_IS_PRESENT)
			inform_muic_usb_event(0);
		reset_resume_ctr = 0;
		/* stop clock */
		r8a66597_bclr(r8a66597, HSE, SYSCFG0);
		r8a66597_bclr(r8a66597, SCKE, SYSCFG0);
		r8a66597_bclr(r8a66597, USBE, SYSCFG0);
		/* Module reset */
		if (r8a66597->pdata->module_stop)
			r8a66597->pdata->module_stop();
		udelay(10);
		udc_log("%s: USB clock disable called by\n", __func__);
		usb_core_clk_ctrl(r8a66597, 0);
		if (!dfs_started) {
			cpufreq_del_lmt_req(&cpufreq_node);
			pr_info("%s(): cpufreq_del_lmt_req\n", __func__);
			dfs_started = 1;
		}
		wake_unlock(&r8a66597->wake_lock);
	}

	r8a66597->vbus_active = vbus_state;
	r8a66597->is_active = vbus_state;
}

static const struct usb_gadget_ops r8a66597_gadget_ops = {
	.get_frame		= r8a66597_get_frame,
	.vbus_session		= r8a66597_vbus_session,
	.vbus_draw		= r8a66597_vbus_draw,
	.udc_start		= r8a66597_start,
	.udc_stop		= r8a66597_stop,
	.pullup			= r8a66597_pullup,
	.set_selfpowered	= r8a66597_set_selfpowered,
};

static int __exit r8a66597_remove(struct platform_device *pdev)
{
	struct r8a66597		*r8a66597 = dev_get_drvdata(&pdev->dev);

	usb_del_gadget_udc(&r8a66597->gadget);
	del_timer_sync(&r8a66597->timer);
	iounmap(r8a66597->reg);
	if (r8a66597_has_dmac(r8a66597))
		iounmap(r8a66597->dmac_reg);
	free_irq(platform_get_irq(pdev, 0), r8a66597);
	free_irq(platform_get_irq(pdev, 1), r8a66597);
	r8a66597_free_request(&r8a66597->ep[0].ep, r8a66597->ep0_req);

	if (r8a66597->is_active) {
		udc_log("%s: USB clock disable called by\n", __func__);
		usb_core_clk_ctrl(r8a66597, 0);
	}

	if (r8a66597->pdata->on_chip) {
		if (r8a66597->is_active) {
			udc_log("%s: USB clock disable called by\n",
				__func__);
			usb_core_clk_ctrl(r8a66597, 0);
			r8a66597->is_active = 0;
		}
		r8a66597_clk_put(r8a66597);
		pm_runtime_disable(r8a66597_to_dev(r8a66597));
	}

	wake_lock_destroy(&r8a66597->wake_lock);
	device_unregister(&r8a66597->gadget.dev);
	dev_set_drvdata(&pdev->dev, NULL);
	kfree(r8a66597);
	return 0;
}

static void nop_completion(struct usb_ep *ep, struct usb_request *r)
{
}

static int __init r8a66597_dmac_ioremap(struct r8a66597 *r8a66597,
					  struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "USBHS-DMA");
	if (!res) {
		dev_err(&pdev->dev, "platform_get_resource error(dmac).\n");
		return -ENODEV;
	}

	r8a66597->dmac_reg = ioremap(res->start, resource_size(res));
	if (r8a66597->dmac_reg == NULL) {
		dev_err(&pdev->dev, "ioremap error(dmac).\n");
		return -ENOMEM;
	}

	return 0;
}

static int __init r8a66597_probe(struct platform_device *pdev)
{
	struct resource *res, *ires, *ires1;
	int irq, irq1;
	void __iomem *reg = NULL;
	void __iomem *dma_reg = NULL;
	struct r8a66597 *r8a66597 = NULL;
	int ret = 0;
	int i;
	unsigned long irq_trigger;

	/* initialize ucd */
	r8a66597 = kzalloc(sizeof(struct r8a66597), GFP_KERNEL);
	if (r8a66597 == NULL) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "kzalloc error\n");
		goto clean_up;
	}

	/* If device tree node is present then parse and populate pdata
	Or fallback in platform_data */

	if (pdev->dev.of_node) {
		r8a66597->pdata = r8a66597_udc_of_init(pdev);
		if (IS_ERR(r8a66597->pdata)) {
			dev_err(&pdev->dev, "platform data not available\n");
			ret = PTR_ERR(r8a66597->pdata);
			goto clean_up;
		}
	} else {
		r8a66597->pdata = pdev->dev.platform_data;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "platform_get_resource error.\n");
		goto clean_up;
	}

	ires = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	irq = ires->start;
	irq_trigger = ires->flags & IRQF_TRIGGER_MASK;

	if (irq < 0) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "platform_get_irq error.\n");
		goto clean_up;
	}

	ires1 = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	irq1 = ires1->start;

	if (irq1 < 0) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "platform_get_irq error.\n");
		goto clean_up;
	}

	reg = ioremap(res->start, resource_size(res));
	if (reg == NULL) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "ioremap error.\n");
		goto clean_up;
	}

	r8a66597->is_active = 0;
	spin_lock_init(&r8a66597->lock);
	dev_set_drvdata(&pdev->dev, r8a66597);
	r8a66597->irq_sense_low = irq_trigger == IRQF_TRIGGER_LOW;

	r8a66597->gadget.ops = &r8a66597_gadget_ops;
	r8a66597->gadget.max_speed = USB_SPEED_HIGH;
	r8a66597->gadget.name = udc_name;

	mutex_init(&r8a66597->mutex);
	wake_lock_init(&r8a66597->wake_lock, WAKE_LOCK_SUSPEND, udc_name);
	INIT_DELAYED_WORK(&r8a66597->vbus_work, r8a66597_vbus_work);
	INIT_DELAYED_WORK(&r8a66597->spa_work, r8a66597_spa_work);

	if (is_otg_mode) {
		r8a66597->gadget.is_otg = 1;
		INIT_DELAYED_WORK(&r8a66597->hnp_work, r8a66597_hnp_work);
		init_timer(&r8a66597->hnp_timer_fail);
		r8a66597->hnp_timer_fail.function = r8a66597_hnp_timer_fail;
		r8a66597->hnp_timer_fail.data = (unsigned long)r8a66597;
	}

	init_timer(&r8a66597->timer);
	r8a66597->timer.function = r8a66597_timer;
	r8a66597->timer.data = (unsigned long)r8a66597;
	r8a66597->reg = reg;
	r8a66597->phy_active = 1;
	udc_log("%s: IN\n", __func__);

	ret = usb_add_gadget_udc(&pdev->dev, &r8a66597->gadget);
		if (ret)
			goto err_add_udc;

	if (r8a66597->pdata->on_chip) {
		pm_runtime_enable(&pdev->dev);
		ret = r8a66597_clk_get(r8a66597, pdev);
		if (ret < 0)
			goto clean_up_dev;
		if (!r8a66597->is_active) {
			udc_log("%s: USB clock enable called by\n", __func__);
			usb_core_clk_ctrl(r8a66597, 1);
			r8a66597->is_active = 1;
			udc_log("%s: IN, no trnscr\n", __func__);
		}
	}

	if (r8a66597->pdata->dmac) {
		ret = r8a66597_dmac_ioremap(r8a66597, pdev);
		if (ret < 0)
			goto clean_up2;
	}

	/* Disabling DMA interrupts to avoid spurios DMA interrupt because
	* request_irq of DMA fails when enabling PDC order changes */
	r8a66597_dma_bclr(r8a66597, IE | SP | DE | TE, USBHS_DMAC_CHCR(0));
	r8a66597_dma_bclr(r8a66597, IE | SP | DE | TE, USBHS_DMAC_CHCR(1));

	if (is_otg_mode)
		ret = request_irq(irq, r8a66597_irq, IRQF_SHARED,
				udc_name, r8a66597);
	else
		ret = request_irq(irq, r8a66597_irq, 0, udc_name, r8a66597);

	if (ret < 0) {
		dev_err(&pdev->dev, "request_irq error (%d)\n", ret);
		goto clean_up2;
	}

	ret = request_irq(irq1, r8a66597_dma_irq, 0, usbhs_dma_name, r8a66597);
	if (ret < 0) {
		dev_err(&pdev->dev, "request_irq error (%d)\n", ret);
		goto clean_up4;
	}

	if (gpio_is_valid(r8a66597->pdata->gpio_cs)) {
		ret = devm_gpio_request_one(&pdev->dev,
						r8a66597->pdata->gpio_cs,
						GPIOF_OUT_INIT_HIGH,
						"USB PHY Chip Select");
		if (ret < 0) {
			dev_err(&pdev->dev, "gpio_request error (%d)\n", ret);
			goto clean_up4;
		}
	}

	if (gpio_is_valid(r8a66597->pdata->gpio_rst)) {
		ret = devm_gpio_request_one(&pdev->dev,
						r8a66597->pdata->gpio_rst,
						GPIOF_OUT_INIT_LOW,
						"USB PHY nReset");
		if (ret < 0) {
			dev_err(&pdev->dev, "gpio_request error (%d)\n", ret);
			goto clean_up4;
		}
		ndelay(100); /* assert RESET_N (min pulse width 100 nsecs) */
		gpio_set_value(r8a66597->pdata->gpio_rst, 1);
	}

	INIT_LIST_HEAD(&r8a66597->gadget.ep_list);
	r8a66597->gadget.ep0 = &r8a66597->ep[0].ep;
	INIT_LIST_HEAD(&r8a66597->gadget.ep0->ep_list);
	for (i = 0; i < R8A66597_MAX_NUM_PIPE; i++) {
		struct r8a66597_ep *ep = &r8a66597->ep[i];

		if (i != 0) {
			INIT_LIST_HEAD(&r8a66597->ep[i].ep.ep_list);
			list_add_tail(&r8a66597->ep[i].ep.ep_list,
					&r8a66597->gadget.ep_list);
		}
		ep->r8a66597 = r8a66597;
		INIT_LIST_HEAD(&ep->queue);
		ep->ep.name = r8a66597_ep_name[i];
		ep->ep.ops = &r8a66597_ep_ops;
		ep->ep.maxpacket = 512;
	}
	r8a66597->ep[0].ep.maxpacket = 64;
	r8a66597->ep[0].pipenum = 0;
	r8a66597->ep[0].fifoaddr = CFIFO;
	r8a66597->ep[0].fifosel = CFIFOSEL;
	r8a66597->ep[0].fifoctr = CFIFOCTR;
	r8a66597->ep[0].pipectr = get_pipectr_addr(0);
	r8a66597->pipenum2ep[0] = &r8a66597->ep[0];
	r8a66597->epaddr2ep[0] = &r8a66597->ep[0];

	the_controller = r8a66597;

	r8a66597->ep0_req = r8a66597_alloc_request(&r8a66597->ep[0].ep,
							GFP_KERNEL);
	if (r8a66597->ep0_req == NULL) {
		ret = -ENOMEM;
		goto clean_up3;
	}
	r8a66597->ep0_req->complete = nop_completion;

	dev_info(&pdev->dev, "version %s\n", DRIVER_VERSION);
	udc_log("%s: USB clock disable called by\n", __func__);
	usb_core_clk_ctrl(r8a66597, 0);
	return 0;

err_add_udc:
	r8a66597_free_request(&r8a66597->ep[0].ep, r8a66597->ep0_req);
clean_up4:
	free_irq(irq1, r8a66597);
clean_up3:
	free_irq(irq, r8a66597);
clean_up2:
	if (r8a66597->pdata->on_chip) {
			if (r8a66597->is_active) {
				udc_log("%s: USB clock disable called by\n",
					__func__);
				usb_core_clk_ctrl(r8a66597, 0);
				r8a66597->is_active = 0;
		}
		r8a66597_clk_put(r8a66597);
		pm_runtime_disable(r8a66597_to_dev(r8a66597));
	}
clean_up_dev:
	device_unregister(&r8a66597->gadget.dev);
clean_up:
	dev_set_drvdata(&pdev->dev, NULL);
	if (r8a66597) {
		if (r8a66597->dmac_reg)
			iounmap(r8a66597->dmac_reg);
		if (r8a66597->ep0_req)
			r8a66597_free_request(&r8a66597->ep[0].ep,
						r8a66597->ep0_req);
		kfree(r8a66597);
	}
	if (reg)
		iounmap(reg);

	if (dma_reg)
		iounmap(dma_reg);
	return ret;
}

#if defined VBUS_HANDLE_IRQ_BASED
void send_usb_insert_event(int is_connected)
{
	struct r8a66597 *r8a66597 = the_controller;
	if ((r8a66597 == NULL) || (r8a66597->driver == NULL)) {
		pr_err("r8a66597 is Null or USB gadget is not ready\n");
		return;
	}
	if (!wake_lock_active(&r8a66597->wake_lock))
		wake_lock(&r8a66597->wake_lock);

	printk(KERN_INFO "USBD][send_usb_insert_event] isConnected=%d\n",\
							is_connected);
	vbus_valid = is_connected;
	schedule_delayed_work(&r8a66597->vbus_work, msecs_to_jiffies(200));
}
EXPORT_SYMBOL_GPL(send_usb_insert_event);
#endif

void tusb_drive_event(int event, unsigned char *val)
{
      printk(KERN_INFO "USBD][tusb_drive_event] event=%d\n",\
                                                        event);
if (event)	 //read event
usb_drv_str_read(val);
else		//write event
usb_drv_str_write(val);
}
EXPORT_SYMBOL_GPL(tusb_drive_event);

static void r8a66597_udc_shutdown(struct platform_device *dev)
{
	struct r8a66597 *r8a66597 = the_controller;
	bool result;
        udc_log("%s: IN\n", __func__);
	if (delayed_work_pending(&r8a66597->vbus_work))
		cancel_delayed_work_sync(&r8a66597->vbus_work);
	else {
		result = flush_work(&r8a66597->vbus_work.work);
		if (!result)
			udc_log("VBus Work is Idle\n");
	}		
}
#ifdef CONFIG_PM

static int r8a66597_udc_suspend(struct device *dev)
{
	struct r8a66597 *r8a66597 = the_controller;
	udc_log("%s: IN\n", __func__);
	if (delayed_work_pending(&r8a66597->vbus_work))
			cancel_delayed_work_sync(&r8a66597->vbus_work);

	if (gpio_is_valid(r8a66597->pdata->gpio_cs))
		gpio_set_value(r8a66597->pdata->gpio_cs, 0);
	if (gpio_is_valid(r8a66597->pdata->gpio_rst))
		gpio_set_value(r8a66597->pdata->gpio_rst, 0);
	pinctrl_pm_select_sleep_state(dev);
	/*save the state of the phy before suspend*/
	r8a66597->phy_active_sav = r8a66597->phy_active;

	if (!r8a66597->is_active || r8a66597->role) {
		udc_log("%s, USB device usage count: %d\n",
		__func__, atomic_read(&r8a66597_to_dev
		(r8a66597)->power.usage_count));
		return 0;
	}
	r8a66597->is_active = 0;
	printk(KERN_INFO "%s, USB device usage count: %d\n",
	 __func__, atomic_read(&r8a66597_to_dev
		(r8a66597)->power.usage_count));

	return 0;
}

static int r8a66597_udc_resume(struct device *dev)
{
	struct r8a66597 *r8a66597 = the_controller;
	udc_log("%s: IN\n", __func__);
	/*restore the state of the phy before resume*/
	r8a66597->phy_active = r8a66597->phy_active_sav;
	pinctrl_pm_select_default_state(dev);
	if (gpio_is_valid(r8a66597->pdata->gpio_rst))
		gpio_set_value(r8a66597->pdata->gpio_rst, 1);

	if (!vbus_valid)
		if (gpio_is_valid(r8a66597->pdata->gpio_cs))
			gpio_set_value(r8a66597->pdata->gpio_cs, 0);

	return 0;
}
#else
#define r8a66597_udc_suspend	NULL
#define r8a66597_udc_resume		NULL
#endif	/* CONFIG_PM */

static const struct dev_pm_ops r8a66597_udc_pm_ops = {
	.suspend	= &r8a66597_udc_suspend,
	.resume		= &r8a66597_udc_resume,
};

#ifdef CONFIG_OF
static struct r8a66597_platdata *
r8a66597_udc_of_init(struct platform_device *pdev) {

	struct r8a66597_platdata *pdata, *aux_pdata;
	struct device_node *np = pdev->dev.of_node;
	int on_chip, buswait, max_bufnum, dmac;

	if (!np) {
		dev_err(&pdev->dev, "device node not found\n");
		return NULL;
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "could not allocate memory for pdata\n");
		return ERR_PTR(-ENOMEM);
	}


	if (!of_property_read_u32(np, "on_chip", &on_chip))
		pdata->on_chip = on_chip;

	if (!of_property_read_u32(np, "buswait", &buswait))
		pdata->buswait = buswait;

	if (!of_property_read_u32(np, "max_bufnum", &max_bufnum))
		pdata->max_bufnum = max_bufnum;

	if (!of_property_read_u32(np, "dmac", &dmac))
		pdata->dmac = dmac;

	pdata->gpio_rst = of_get_named_gpio(np, "rst-gpios", 0);
	pdata->gpio_cs = of_get_named_gpio(np, "cs-gpios", 0);

	/* if platform data is supplied using AUXDATA, then get the callbacks
	and other params from there.
	TODO:This is a temporary solution until we move all the dependencies
	from board file */

	if (pdev->dev.platform_data) {
		aux_pdata = pdev->dev.platform_data;
		pdata->is_vbus_powered = aux_pdata->is_vbus_powered;
		pdata->module_start = aux_pdata->module_start;
		pdata->module_stop = aux_pdata->module_stop;
	}
	return pdata;
}

static const struct of_device_id r8a66597_udc_of_match[] = {
	{ .compatible = "renesas,r8a66597-udc" },
	{}
};
MODULE_DEVICE_TABLE(of, r8a66597_udc_of_match);
#endif


/*-------------------------------------------------------------------------*/
static struct platform_driver r8a66597_driver = {
	.remove =	__exit_p(r8a66597_remove),
	.shutdown =	r8a66597_udc_shutdown,
	.driver		= {
		.name = udc_name,
		.of_match_table = of_match_ptr(r8a66597_udc_of_match),
		.pm	= &r8a66597_udc_pm_ops,
	},
};

module_platform_driver_probe(r8a66597_driver, r8a66597_probe);

MODULE_DESCRIPTION("R8A66597 USB gadget driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yoshihiro Shimoda");
MODULE_ALIAS("platform:r8a66597_udc");
