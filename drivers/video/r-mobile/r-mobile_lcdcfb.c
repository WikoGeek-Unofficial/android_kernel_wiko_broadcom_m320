/*
 * drivers/video/r-mobile/r-mobile_lcdcfb.c
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
/*
 * SuperH Mobile LCDC Framebuffer
 *
 * Copyright (c) 2008 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */


#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <video/sh_mobile_lcdc.h>
/*#include <media/sh_mobile_overlay.h>*/
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/ioport.h>
#include <linux/of_gpio.h>
#include "panels.h"
#include "panel/panel_common.h"
#include "panel/panel_info.h"
#include <mach/ramdump.h>

#include <rtapi/screen_display.h>

#define FB_SH_MOBILE_REFRESH 0

#define REFRESH_TIME_MSEC 100

#if FB_SH_MOBILE_REFRESH
#include <linux/mfis.h>
#define COUNT_MFIS_SUSPEND 10
#endif

#ifdef CONFIG_SAMSUNG_MHL
#define FB_SH_MOBILE_HDMI 1
#else
#define FB_SH_MOBILE_HDMI 0
#endif


#define FB_HDMI_STOP  0
#define FB_HDMI_START 1

#define SIDE_B_OFFSET 0x1000
#define MIRROR_OFFSET 0x2000

#ifdef CONFIG_MISC_R_MOBILE_COMPOSER_REQUEST_QUEUE
#include <linux/sh_mobile_composer.h>
#endif

#define FB_DRM_NUMBER		0x12345678
#define FB_BLACK_NUMBER		0x55555555

/*
 * This is hard-coded as 0 - the bit in the RT IRQ Generator Block - should be
 * configurable, like the host-side IRQ number
 */
#define IRQ_BIT_NUM		0

#define IRQC_GEN_STS0	0x000

#define CUST_LOGO_ADDR 0x48000000

/*
 * LCDC Register Offsets
 */

#define LCDC_MLDSA1R		0x0430
#define LCDC_MLDSA2R		0x1430


static struct panel_info  *panel_data;

static unsigned g_panel_index;

static unsigned int g_disp_boot_init;
static bool lcdc_shutdown;
static char __iomem *irqc_baseaddr;
static int vsync_status = 1;
static unsigned long rloader_buff_p;

struct fb_lcdc_ext_param {
	struct semaphore sem_lcd;
	int lcd_type;
	void *aInfo;
	unsigned short o_mode;
	unsigned short draw_mode;
	unsigned int phy_addr;
	unsigned int vir_addr;
	unsigned short rect_x;
	unsigned short rect_y;
	unsigned short rect_width;
	unsigned short rect_height;
	unsigned short alpha;
	unsigned short key_clr;
	unsigned short v4l2_state;
	unsigned short draw_bpp;
	unsigned int mem_size;
	unsigned short delay_flag;
	unsigned short refresh_on;
	struct delayed_work ref_work;
	unsigned short mfis_err_flag;
	unsigned short rotate;
	struct fb_panel_func panel_func;
	struct fb_hdmi_func hdmi_func;
	unsigned short hdmi_flag;
	int blank_lvl;
};

struct fb_lcdc_ext_param lcd_ext_param[CHAN_NUM];

struct semaphore   sh_mobile_sem_hdmi;

#if FB_SH_MOBILE_REFRESH
struct workqueue_struct *sh_mobile_wq;
#endif

struct sh_mobile_lcdc_vsync {
	struct task_struct *vsync_thread;
	wait_queue_head_t vsync_wait;
	ktime_t vsync_time;
	int vsync_flag;
	struct workqueue_struct *wq_vsync_off;
};

static struct sh_mobile_lcdc_vsync sh_lcd_vsync;
static DECLARE_DEFERRABLE_WORK (wk_vsync_off, NULL);
#define VSYNC_UPDATE_DELAY 2000

static u32 fb_debug;
#define DBG_PRINT(FMT, ARGS...)	      \
	do { \
		if (fb_debug)						\
			printk(KERN_INFO "%s(): " FMT, __func__, ##ARGS); \
	} while (0)

module_param(fb_debug, int, 0644);
MODULE_PARM_DESC(fb_debug, "SH LCD debug level");

static struct property device_disabled = {
	.name = "status",
	.length = sizeof "disabled",
	.value = "disabled",
};

static char *lcd_node_name[] = {
	"/lcdc@fe940000",
	"/lcdc2@fe940000",
	//++ custom_t peter add lcd3 
	"/lcdc3@fe940000"
	//-- custom_t peter
};

static char *rt_node_name[] = {
	"/rtboot",
	"/rtboot2",
	//++ custom_t peter add lcd3 
	"/rtboot3"
};

static unsigned long RoundUpToMultiple(unsigned long x, unsigned long y);
static unsigned long GCD(unsigned long x, unsigned long y);
static unsigned long LCM(unsigned long x, unsigned long y);
static int sh_mobile_fb_blank(int, struct fb_info *);
static int lcdc_suspend(void);
static int lcdc_resume(void);
void r_mobile_fb_err_msg(int value, char *func_name)
{

	switch (value) {
	case -1:
		printk(KERN_INFO "FB %s ret = -1 SMAP_LIB_DISPLAY_NG\n",
		       func_name);
		break;
	case -2:
		printk(KERN_INFO "FB %s ret = -2 SMAP_LIB_DISPLAY_PARAERR\n",
		       func_name);
		break;
	case -3:
		printk(KERN_INFO "FB %s ret = -3 SMAP_LIB_DISPLAY_SEQERR\n",
		       func_name);
		break;
	default:
		printk(KERN_INFO "FB %s ret = %d unknown RT-API error\n",
		       func_name, value);
		break;
	}

	return;
}

static int vsync_recv_thread(void *data)
{
	int ret;
	struct fb_lcdc_priv *priv = data;

	while (!kthread_should_stop()) {
		sh_lcd_vsync.vsync_flag = 0;
		queue_delayed_work(sh_lcd_vsync.wq_vsync_off,
			&wk_vsync_off, msecs_to_jiffies(VSYNC_UPDATE_DELAY));
		ret = wait_event_interruptible(sh_lcd_vsync.vsync_wait,
					       sh_lcd_vsync.vsync_flag);
		if (!ret) {
			sysfs_notify(&priv->dev->kobj, NULL, "vsync");
			cancel_delayed_work_sync(&wk_vsync_off);
		}
	}

	return 0;
}

static void sh_mobile_fb_vsync_acknowledge(struct fb_lcdc_priv *priv)
{
	writel_relaxed(BIT(IRQ_BIT_NUM), irqc_baseaddr + IRQC_GEN_STS0);
	/* Dummy read to ensure write has taken effect */
	readl(irqc_baseaddr + IRQC_GEN_STS0);
}

static irqreturn_t sh_mobile_lcdc_irq(int irq, void *data)
{
	struct fb_lcdc_priv *priv = data;

	sh_lcd_vsync.vsync_time = ktime_get();
	sh_lcd_vsync.vsync_flag = 1;
	vsync_status = 1;
	wake_up_interruptible(&sh_lcd_vsync.vsync_wait);

	sh_mobile_fb_vsync_acknowledge(priv);

	return IRQ_HANDLED;
}

void vsync_off_work(struct work_struct *work)
{
	vsync_status = 0;
}

int sh_mobile_vsync_status(void)
{
	return vsync_status;
}

static ssize_t sh_fb_vsync_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%llu\n",
			 ktime_to_ns(sh_lcd_vsync.vsync_time));
}

static DEVICE_ATTR(vsync, S_IRUGO, sh_fb_vsync_show, NULL);

static int sh_mobile_lcdc_setcolreg(u_int regno,
				    u_int red, u_int green, u_int blue,
				    u_int transp, struct fb_info *info)
{
	/* No. of hw registers */
	if (regno >= 256)
		return 1;

	/* grayscale works only partially under directcolor */
	if (info->var.grayscale) {
		/* grayscale = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}

#define CNVT_TOHW(val, width) ((((val)<<(width))+0x7FFF-(val))>>16)
	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:	/* FALL THROUGH */
	case FB_VISUAL_PSEUDOCOLOR:
	{
		red = CNVT_TOHW(red, info->var.red.length);
		green = CNVT_TOHW(green, info->var.green.length);
		blue = CNVT_TOHW(blue, info->var.blue.length);
		transp = CNVT_TOHW(transp, info->var.transp.length);
		break;
	}
	case FB_VISUAL_DIRECTCOLOR:
	{
		red = CNVT_TOHW(red, 8);	/* expect 8 bit DAC */
		green = CNVT_TOHW(green, 8);
		blue = CNVT_TOHW(blue, 8);
		/* hey, there is bug in transp handling... */
		transp = CNVT_TOHW(transp, 8);
		break;
	}
	}
#undef CNVT_TOHW
	/* Truecolor has hardware independent palette */
	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 v;

		if (regno >= 16)
			return 1;

		v = (red << info->var.red.offset) |
		    (green << info->var.green.offset) |
		    (blue << info->var.blue.offset) |
		    (transp << info->var.transp.offset);
		switch (info->var.bits_per_pixel) {
		case 16:	/* FALL THROUGH */
		case 24:	/* FALL THROUGH */
		case 32:
			((u32 *) (info->pseudo_palette))[regno] = v;
			break;
		case 8:		/* FALL THROUGH */
		default:
			break;
		}
	}

	return 0;
}

static struct fb_fix_screeninfo sh_mobile_lcdc_fix  = {
	.id		= "SH Mobile LCDC",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.accel		= FB_ACCEL_NONE,
	.xpanstep	= 0,
	.ypanstep	= 1,
	.ywrapstep	= 0,
};

static int display_initialize(int lcd_num)
{
	screen_disp_delete disp_delete;

	int ret = 0;

	printk(KERN_INFO "enter display_initialize\n");

	lcd_ext_param[lcd_num].aInfo = screen_display_new();

	printk(KERN_INFO "%s, lcd_num=%d", __func__, lcd_num);

	if (lcd_ext_param[lcd_num].aInfo == NULL)
		return -2;

	if (lcd_ext_param[lcd_num].panel_func.panel_init) {
		ret = lcd_ext_param[lcd_num].panel_func.panel_init(
			panel_data, lcd_ext_param[lcd_num].mem_size);
		if (ret != 0) {
			printk(KERN_ALERT "r_mobile_panel_init error\n");
			disp_delete.handle = lcd_ext_param[lcd_num].aInfo;
			screen_display_delete(&disp_delete);
			return -1;
		}
	}

	disp_delete.handle = lcd_ext_param[lcd_num].aInfo;
	screen_display_delete(&disp_delete);

	printk(KERN_INFO "exit display_initialize\n");

	return 0;

}

int sh_mobile_lcdc_keyclr_set(unsigned short s_key_clr,
			      unsigned short output_mode)
{

	return 0;

}
EXPORT_SYMBOL(sh_mobile_lcdc_keyclr_set);

int sh_mobile_lcdc_alpha_set(unsigned short s_alpha,
			      unsigned short output_mode)
{

	return 0;

}
EXPORT_SYMBOL(sh_mobile_lcdc_alpha_set);

#if FB_SH_MOBILE_REFRESH
static void refresh_work(struct work_struct *work)
{

	int ret;
	screen_disp_set_lcd_refresh disp_refresh;
	screen_disp_delete disp_delete;
	void *screen_handle;
	int loop_count = COUNT_MFIS_SUSPEND;

	struct sh_mobile_lcdc_ext_param *extp =
		container_of(work, struct sh_mobile_lcdc_ext_param,
			     ref_work.work);

	if (extp->aInfo != NULL) {
		screen_handle = screen_display_new();
		disp_refresh.handle = screen_handle;
		disp_refresh.output_mode = extp->o_mode;
		disp_refresh.refresh_mode = RT_DISPLAY_REFRESH_ON;
		ret = screen_display_set_lcd_refresh(&disp_refresh);
		if (ret != SMAP_LIB_DISPLAY_OK)
			r_mobile_fb_err_msg(ret,
					    "screen_display_set_lcd_refresh");
		DBG_PRINT("screen_display_set_lcd_refresh ON\n");
		disp_delete.handle = screen_handle;
		screen_display_delete(&disp_delete);
		ret = mfis_drv_suspend();
		extp->refresh_on = 1;

		while ((ret != 0 && extp->mfis_err_flag == 0) && loop_count) {
			DBG_PRINT("mfis_drv_suspend err :sleep 100mS\n");
			msleep(100);
			ret = mfis_drv_suspend();
			loop_count--;
		}
		if (!loop_count)
			printk(KERN_ALERT "##mfis_drv_suspend ERR%d\n", ret);
	}

	extp->delay_flag = 0;
	return;
}
#endif


int sh_mobile_lcdc_refresh(unsigned short set_state,
			   unsigned short output_mode)
{

#if FB_SH_MOBILE_REFRESH
	int i, ret;
	screen_disp_set_lcd_refresh disp_refresh;
	screen_disp_delete disp_delete;
	void *screen_handle;
	for (i = 0 ; i < CHAN_NUM ; i++) {
		if (output_mode == lcd_ext_param[i].o_mode)
			break;
	}
	if (i >= CHAN_NUM) {
		printk(KERN_ALERT "lcdc_key_clr_set param ERR\n");
		return -1;
	}
	if (down_interruptible(&lcd_ext_param[i].sem_lcd)) {
		printk(KERN_ALERT "down_interruptible failed\n");
		return -1;
	}

	lcd_ext_param[i].v4l2_state = set_state;
	if (lcd_ext_param[i].v4l2_state == RT_DISPLAY_REFRESH_OFF) {
		if (lcd_ext_param[i].delay_flag == 1) {
			lcd_ext_param[i].mfis_err_flag = 1;
			cancel_delayed_work_sync(&lcd_ext_param[i].ref_work);
			lcd_ext_param[i].delay_flag = 0;
		}
		if (lcd_ext_param[i].refresh_on == 1) {
			if (lcd_ext_param[i].aInfo != NULL) {
				ret = mfis_drv_resume();
				if (ret != 0) {
					printk(KERN_ALERT
					       "##mfis_drv_resume ERR%d\n",
					       ret);
				}
				screen_handle = screen_display_new();
				disp_refresh.handle = screen_handle;
				disp_refresh.output_mode
					= lcd_ext_param[i].o_mode;
				disp_refresh.refresh_mode
					= RT_DISPLAY_REFRESH_OFF;
				ret = screen_display_set_lcd_refresh(
					&disp_refresh);
				if (ret != SMAP_LIB_DISPLAY_OK) {
					r_mobile_fb_err_msg(
						ret,
						"screen_display_set_lcd_refresh");
					disp_delete.handle = screen_handle;
					screen_display_delete(&disp_delete);
					up(&lcd_ext_param[i].sem_lcd);
					return -1;
				}
				lcd_ext_param[i].refresh_on = 0;
				disp_delete.handle = screen_handle;
				screen_display_delete(&disp_delete);
			}
		}
	} else {
		queue_delayed_work(
			sh_mobile_wq, &lcd_ext_param[i].ref_work, 0);
		lcd_ext_param[i].delay_flag = 1;
		lcd_ext_param[i].mfis_err_flag = 0;
	}
	up(&lcd_ext_param[i].sem_lcd);
#endif
	return 0;

}
EXPORT_SYMBOL(sh_mobile_lcdc_refresh);

#if FB_SH_MOBILE_HDMI
int sh_mobile_fb_hdmi_set(struct fb_hdmi_set_mode *set_mode)
{

	void *hdmi_handle;
	screen_disp_stop_hdmi disp_stop_hdmi;
	screen_disp_delete disp_delete;
	int ret;

	if (set_mode->start != SH_FB_HDMI_START &&
	    set_mode->start != SH_FB_HDMI_STOP) {
		DBG_PRINT("set_mode->start param\n");
		return -1;
	}
	if (set_mode->start == SH_FB_HDMI_START &&
	    (set_mode->format < SH_FB_HDMI_480P60 ||
	     set_mode->format > SH_FB_HDMI_576P50A43)) {
		DBG_PRINT("set_mode->format param\n");
		return -1;
	}

	if (set_mode->start == SH_FB_HDMI_STOP) {
		if (down_interruptible(&lcd_ext_param[0].sem_lcd)) {
			printk(KERN_ALERT "down_interruptible failed\n");
			return -1;
		}
#ifdef CONFIG_MISC_R_MOBILE_COMPOSER_REQUEST_QUEUE
#if SH_MOBILE_COMPOSER_SUPPORT_HDMI

		sh_mobile_composer_hdmiset(0);
#endif
#endif
		hdmi_handle = screen_display_new();
		disp_stop_hdmi.handle = hdmi_handle;
		ret = screen_display_stop_hdmi(&disp_stop_hdmi);
		if (ret != SMAP_LIB_DISPLAY_OK)
			r_mobile_fb_err_msg(ret, "screen_display_stop_hdmi");
		DBG_PRINT("screen_display_stop_hdmi ret = %d\n", ret);
		disp_delete.handle = hdmi_handle;
		screen_display_delete(&disp_delete);
		lcd_ext_param[0].hdmi_flag = FB_HDMI_STOP;
#ifdef CONFIG_MISC_R_MOBILE_COMPOSER_REQUEST_QUEUE
#if SH_MOBILE_COMPOSER_SUPPORT_HDMI

		sh_mobile_composer_hdmiset(1);
#endif
#endif
		up(&lcd_ext_param[0].sem_lcd);
		sh_mobile_lcdc_refresh(
			RT_DISPLAY_REFRESH_ON, RT_DISPLAY_LCD1);
	} else {
		sh_mobile_lcdc_refresh(
			RT_DISPLAY_REFRESH_OFF, RT_DISPLAY_LCD1);
		if (lcd_ext_param[0].hdmi_func.hdmi_set) {
			ret = lcd_ext_param[0].hdmi_func.hdmi_set(
				set_mode->format);
			if (ret) {
				printk(KERN_ALERT " error\n");
				return -1;
			}
			lcd_ext_param[0].hdmi_flag = FB_HDMI_START;
		}
	}

	return 0;
}
#endif

static int sh_mobile_fb_sync_set(struct device *dev, int vsyncval)
{
	struct fb_lcdc_priv *priv = dev_get_drvdata(dev);
	int ret;

	if (vsyncval != SH_FB_VSYNC_OFF && vsyncval != SH_FB_VSYNC_ON)
		return -1;

	if (vsyncval == SH_FB_VSYNC_ON) {
		if (lcd_ext_param[0].aInfo == NULL) {
			ret = display_initialize(0);
			if (ret == -1) {
				printk(KERN_ALERT
				       "err sync display_initialize\n");
				return -1;
			} else if (ret == -2) {
				printk(KERN_ALERT "nothing MFI driver\n");
				return -1;
			}
		}
		if (!priv->irq_enabled) {
			priv->irq_enabled = true;
			sh_mobile_fb_vsync_acknowledge(priv);
			enable_irq(priv->irq);
		}
	} else {
		if (priv->irq_enabled) {
			priv->irq_enabled = false;
			disable_irq(priv->irq);
			sh_mobile_fb_vsync_acknowledge(priv);
		}
	}

	return 0;

}

static int sh_mobile_fb_pan_display(struct fb_var_screeninfo *var,
				     struct fb_info *info)
{
	struct fb_lcdc_chan *ch = info->par;
	unsigned long new_pan_offset;

	int ret = 0;

/* onscreen buffer 2 */
	unsigned int i;
	unsigned short set_format;
	unsigned char  lcd_num;
	screen_disp_draw disp_draw;
	screen_disp_delete disp_delete;
	void *screen_handle;
	unsigned short set_buff_id;
#if FB_SH_MOBILE_REFRESH
	screen_disp_set_lcd_refresh disp_refresh;
#endif
	if (panel_data->esd_data.esd_check != ESD_DISABLED)
		mutex_lock(&panel_data->esd_data.esd_check_mutex);
	new_pan_offset = (var->yoffset * info->fix.line_length) +
		(var->xoffset * (info->var.bits_per_pixel / 8));

	for (i = 0 ; i < CHAN_NUM ; i++) {
		if (ch->cfg.chan == lcd_ext_param[i].lcd_type)
			break;
	}
	lcd_num = i;
	if (lcd_num >= CHAN_NUM)
		return -EINVAL;

	if (down_interruptible(&lcd_ext_param[lcd_num].sem_lcd)) {
		printk(KERN_ALERT "down_interruptible failed\n");
		return -ERESTARTSYS;
	}

	/* DRM */
	if (var->reserved[1] == FB_DRM_NUMBER)
		set_buff_id = RT_DISPLAY_BUFFER_B;
	else if (var->reserved[1] == FB_BLACK_NUMBER)
		set_buff_id = RT_DISPLAY_DRAW_BLACK;
	else
		set_buff_id = RT_DISPLAY_BUFFER_A;

	/* Set the source address for the next refresh */

	if (lcd_ext_param[lcd_num].aInfo == NULL) {
		ret = display_initialize(lcd_num);
		if (ret == -1) {
			up(&lcd_ext_param[lcd_num].sem_lcd);
			return -EIO;
		} else if (ret == -2)
			printk(KERN_ALERT "nothing MFI driver\n");
	}
	if (lcd_ext_param[lcd_num].aInfo != NULL) {
		screen_handle = screen_display_new();
#if FB_SH_MOBILE_REFRESH
		if (lcd_ext_param[lcd_num].v4l2_state
		    == RT_DISPLAY_REFRESH_ON) {
			if (lcd_ext_param[lcd_num].delay_flag == 1) {
				lcd_ext_param[lcd_num].mfis_err_flag = 1;
				cancel_delayed_work_sync(
					&lcd_ext_param[lcd_num].ref_work);
				lcd_ext_param[lcd_num].delay_flag = 0;
			}
			if (lcd_ext_param[lcd_num].refresh_on == 1) {
				ret = mfis_drv_resume();
				if (ret != 0) {
					printk(KERN_ALERT
					       "##mfis_drv_resume "
					       "ERR%d\n", ret);
				}
				disp_refresh.handle = screen_handle;
				disp_refresh.output_mode
					= lcd_ext_param[lcd_num].o_mode;
				disp_refresh.refresh_mode
					= RT_DISPLAY_REFRESH_OFF;
				ret = screen_display_set_lcd_refresh(
					&disp_refresh);
				if (ret != SMAP_LIB_DISPLAY_OK) {
					r_mobile_fb_err_msg(
						ret,
						"screen_display_set_lcd_refresh");
					up(&lcd_ext_param[lcd_num].sem_lcd);
					disp_delete.handle = screen_handle;
					screen_display_delete(&disp_delete);
					return -EIO;
				}
				lcd_ext_param[lcd_num].refresh_on = 0;
			}
		}
#endif
		if (var->bits_per_pixel == 16)
			set_format = RT_DISPLAY_FORMAT_RGB565;
		else if (var->bits_per_pixel == 24)
			set_format = RT_DISPLAY_FORMAT_RGB888;
		else
			set_format = RT_DISPLAY_FORMAT_ARGB8888;

#ifdef CONFIG_FB_SH_MOBILE_DOUBLE_BUF
		disp_draw.handle = screen_handle;
		if (var->reserved[1] == FB_BLACK_NUMBER)
			disp_draw.output_mode = RT_DISPLAY_LCD1;
		else
			disp_draw.output_mode =
				lcd_ext_param[lcd_num].draw_mode;

		disp_draw.draw_rect.x = lcd_ext_param[lcd_num].rect_x;
		disp_draw.draw_rect.y = lcd_ext_param[lcd_num].rect_y;
		disp_draw.draw_rect.width =
			lcd_ext_param[lcd_num].rect_width;
		disp_draw.draw_rect.height =
			lcd_ext_param[lcd_num].rect_height;
		disp_draw.format = set_format;
		disp_draw.buffer_id = set_buff_id;
		disp_draw.buffer_offset = new_pan_offset;
		disp_draw.rotate = lcd_ext_param[lcd_num].rotate;
		DBG_PRINT("screen_display_draw handle %x\n",
			  (unsigned int)disp_draw.handle);
		DBG_PRINT("screen_display_draw output_mode %d\n",
			  disp_draw.output_mode);
		DBG_PRINT("screen_display_draw draw_rect.x %d\n",
			  disp_draw.draw_rect.x);
		DBG_PRINT("screen_display_draw draw_rect.y %d\n",
			  disp_draw.draw_rect.y);
		DBG_PRINT("screen_display_draw draw_rect.width %d\n",
			  disp_draw.draw_rect.width);
		DBG_PRINT("screen_display_draw draw_rect.height %d\n",
			  disp_draw.draw_rect.height);
		DBG_PRINT("screen_display_draw format %d\n", disp_draw.format);
		DBG_PRINT("screen_display_draw buffer_id %d\n",
			  disp_draw.buffer_id);
		DBG_PRINT("screen_display_draw buffer_offset %d\n",
			  disp_draw.buffer_offset);
		DBG_PRINT("screen_display_draw rotate %d\n", disp_draw.rotate);
		ret = screen_display_draw(&disp_draw);
		DBG_PRINT("screen_display_draw return %d\n", ret);
		if (ret != SMAP_LIB_DISPLAY_OK) {
			r_mobile_fb_err_msg(ret, "screen_display_draw");
			up(&lcd_ext_param[lcd_num].sem_lcd);
			disp_delete.handle = screen_handle;
			screen_display_delete(&disp_delete);
			return -EIO;
		}
#else

		memcpy((void *) panel_data->virt_addr,
		       (void *)(info->screen_base + new_pan_offset),
		       (lcd_ext_param[lcd_num].rect_width *
			lcd_ext_param[lcd_num].rect_height *
			var->bits_per_pixel / 8));

		disp_draw.handle = screen_handle;
		disp_draw.output_mode = lcd_ext_param[lcd_num].draw_mode;
		disp_draw.draw_rect.x = lcd_ext_param[lcd_num].rect_x;
		disp_draw.draw_rect.y = lcd_ext_param[lcd_num].rect_y;
		disp_draw.draw_rect.width =
			lcd_ext_param[lcd_num].rect_width;
		disp_draw.draw_rect.height =
			lcd_ext_param[lcd_num].rect_height;
		disp_draw.format = set_format;
		disp_draw.buffer_id = set_buff_id;
		disp_draw.buffer_offset = 0;
		disp_draw.rotate = lcd_ext_param[lcd_num].rotate;
		ret = screen_display_draw(&disp_draw);
		if (ret != SMAP_LIB_DISPLAY_OK) {
			r_mobile_fb_err_msg(ret, "screen_display_draw");
			up(&lcd_ext_param[lcd_num].sem_lcd);
			disp_delete.handle = screen_handle;
			screen_display_delete(&disp_delete);
			return -EIO;
		}
#endif

#if FB_SH_MOBILE_REFRESH
		if (lcd_ext_param[lcd_num].v4l2_state
		    == RT_DISPLAY_REFRESH_ON) {
			queue_delayed_work(
				sh_mobile_wq, &lcd_ext_param[lcd_num].ref_work,
				msecs_to_jiffies(REFRESH_TIME_MSEC));
			lcd_ext_param[lcd_num].delay_flag = 1;
			lcd_ext_param[lcd_num].mfis_err_flag = 0;
		}
#endif
		disp_delete.handle = screen_handle;
		screen_display_delete(&disp_delete);
	}

	up(&lcd_ext_param[lcd_num].sem_lcd);
	if (panel_data->esd_data.esd_check != ESD_DISABLED)
		mutex_unlock(&panel_data->esd_data.esd_check_mutex);
	return 0;
}

static int sh_mobile_ioctl(struct fb_info *info, unsigned int cmd,
		       unsigned long arg)
{
	int retval;
#if FB_SH_MOBILE_HDMI
	struct fb_hdmi_set_mode hdmi_set;
#endif
	int vsyncval;

	switch (cmd) {
	case FBIO_WAITFORVSYNC:
		retval = 0;
		break;

#if FB_SH_MOBILE_HDMI
	case SH_MOBILE_FB_HDMI_SET:
		if (arg == 0) {
			retval = -EINVAL;
			break;
		}
		if (copy_from_user(&hdmi_set, (void *)arg,
				   sizeof(struct fb_hdmi_set_mode))) {
			printk(KERN_ALERT "copy_from_user failed\n");
			retval = -EFAULT;
			break;
		}
		if (sh_mobile_fb_hdmi_set(&hdmi_set)) {
			retval = -EINVAL;
			break;
		} else {
			retval = 0;
			break;
		}
#endif
	case SH_MOBILE_FB_ENABLEVSYNC:
		if (arg == 0) {
			retval = -EINVAL;
			break;
		}
		if (get_user(vsyncval, (int __user *)arg)) {
			printk(KERN_ALERT "get_user failed\n");
			retval = -EFAULT;
			break;
		}
		if (sh_mobile_fb_sync_set(info->dev, vsyncval)) {
			retval = -EINVAL;
			break;
		} else {
			retval = 0;
			break;
		}
	default:
		retval = -ENOIOCTLCMD;
		break;
	}
	return retval;
}

static int sh_mobile_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long start;
	unsigned long off;
	u32 len;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;

	off = vma->vm_pgoff << PAGE_SHIFT;
	start = info->fix.smem_start;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.smem_len);

	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;

	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

	/* Accessing memory will be done non-cached. */
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	/* To stop the swapper from even considering these pages */
	vma->vm_flags |= (VM_IO | VM_DONTEXPAND | VM_DONTDUMP);

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}
static int sh_mobile_fb_check_var(struct fb_var_screeninfo *var,
				  struct fb_info *info)
{
	switch (var->bits_per_pixel) {
	case 16: /* RGB 565 */
		var->red.offset    = 11;
		var->red.length    = 5;
		var->green.offset  = 5;
		var->green.length  = 6;
		var->blue.offset   = 0;
		var->blue.length   = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 24: /* RGB 888 */
		var->red.offset    = 0;
		var->red.length    = 8;
		var->green.offset  = 8;
		var->green.length  = 8;
		var->blue.offset   = 16;
		var->blue.length   = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 32: /* ARGB 8888*/
		var->red.offset    = 16;
		var->red.length    = 8;
		var->green.offset  = 8;
		var->green.length  = 8;
		var->blue.offset   = 0;
		var->blue.length   = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	default:
		return -EINVAL;

	}
	return 0;
}

static int sh_mobile_fb_set_par(struct fb_info *info)
{
	unsigned long ulLCM;
	int bpp = info->var.bits_per_pixel;

#ifdef CONFIG_FB_SH_MOBILE_RGB888
	/* calculate info->fix.smem_len by its maximum size */
	if (bpp == 24)
		bpp = 32;
#endif
	info->fix.line_length = RoundUpToMultiple(info->var.xres, 32)
		* (bpp / 8);

	/* 4kbyte align */
	ulLCM = LCM(info->fix.line_length, 0x1000);
	info->fix.smem_len = RoundUpToMultiple(
			info->fix.line_length*info->var.yres, ulLCM);

	info->var.reserved[0] = info->fix.smem_len;
	info->fix.smem_len *= 2;

	info->var.yres_virtual = info->fix.smem_len
		/ info->fix.line_length;

#ifdef CONFIG_FB_SH_MOBILE_RGB888
	/* calculate other values again using actual bpp */
	bpp = info->var.bits_per_pixel;
	if (bpp == 24) {
		int smem_len;

		info->fix.line_length = RoundUpToMultiple(info->var.xres, 32)
			* (bpp / 8);
		smem_len = info->fix.line_length * info->var.yres;

		info->var.reserved[0] = smem_len;
		smem_len *= 2;

		/* 4kbyte align */
		smem_len = RoundUpToMultiple(smem_len, 0x1000);

		info->var.yres_virtual = smem_len
			/ info->fix.line_length;
	}
#endif
	return 0;
}

static struct fb_ops sh_mobile_lcdc_ops = {
	.owner          = THIS_MODULE,
	.fb_setcolreg	= sh_mobile_lcdc_setcolreg,
	.fb_check_var	= sh_mobile_fb_check_var,
	.fb_set_par	= sh_mobile_fb_set_par,
	.fb_read        = fb_sys_read,
	.fb_write       = fb_sys_write,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_pan_display = sh_mobile_fb_pan_display,
	.fb_ioctl       = sh_mobile_ioctl,
	.fb_mmap	= sh_mobile_mmap,
	.fb_blank	= sh_mobile_fb_blank,
};

static int sh_mobile_lcdc_set_bpp(struct fb_var_screeninfo *var, int bpp)
{
	switch (bpp) {
	case 16: /* RGB 565 */
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 24: /* RGB 888 */
		var->red.offset    = 0;
		var->red.length    = 8;
		var->green.offset  = 8;
		var->green.length  = 8;
		var->blue.offset   = 16;
		var->blue.length   = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 32: /* ARGB 8888 */
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	default:
		return -EINVAL;
	}
	var->bits_per_pixel = bpp;
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;
	return 0;
}

static int lcdc_suspend(void)
{
	int lcd_num = 0, ret;
	screen_disp_delete disp_delete;
	void *suspend_handle;
#if FB_SH_MOBILE_REFRESH
	screen_disp_set_lcd_refresh disp_refresh;
#endif
	if ((lcd_ext_param[lcd_num].aInfo == NULL) && (!lcdc_shutdown)) {
		pr_info("Ensuring display initialize before continuing suspend\n");
		down(&lcd_ext_param[lcd_num].sem_lcd);
		ret = display_initialize(lcd_num);
		up(&lcd_ext_param[lcd_num].sem_lcd);
		if (ret == -1) {
			pr_err("ERROR display_initialize\n");
			return -EIO;
		} else if (ret == -2) {
			pr_err("nothing MFI driver\n");
			return -EIO;
		}
	} else if (lcd_ext_param[lcd_num].aInfo == NULL) {
		pr_err("%s: suspending without display_initialize\n"
						, __func__);
	}

	suspend_handle = screen_display_new();

	down(&sh_mobile_sem_hdmi);

	for (lcd_num = 0; lcd_num < CHAN_NUM; lcd_num++) {
		if (lcd_ext_param[lcd_num].aInfo != NULL) {
			if (lcd_ext_param[lcd_num].blank_lvl
					!= FB_BLANK_UNBLANK) {
				continue;
			}
			lcd_ext_param[lcd_num].blank_lvl = FB_BLANK_NORMAL;
			down(&lcd_ext_param[lcd_num].sem_lcd);
#if FB_SH_MOBILE_REFRESH
			if (lcd_ext_param[lcd_num].v4l2_state
			    == RT_DISPLAY_REFRESH_ON) {
				if (lcd_ext_param[lcd_num].delay_flag == 1) {
					lcd_ext_param[lcd_num].mfis_err_flag
						= 1;
					cancel_delayed_work_sync(
						&lcd_ext_param[lcd_num].
						ref_work);
					lcd_ext_param[lcd_num].delay_flag = 0;
				}
				if (lcd_ext_param[lcd_num].refresh_on == 1) {
					mfis_drv_resume();
					disp_refresh.handle = suspend_handle;
					disp_refresh.output_mode
						= lcd_ext_param[lcd_num].
						o_mode;
					disp_refresh.refresh_mode
						= RT_DISPLAY_REFRESH_OFF;
					screen_display_set_lcd_refresh(
						&disp_refresh);
					lcd_ext_param[lcd_num].refresh_on = 0;
				}
			}
#endif

			if (lcd_ext_param[lcd_num].panel_func.panel_suspend) {
				lcd_ext_param[lcd_num].
					panel_func.panel_suspend(panel_data);
			}

			up(&lcd_ext_param[lcd_num].sem_lcd);
		}
	}

#if FB_SH_MOBILE_HDMI
	if (lcd_ext_param[0].hdmi_flag == FB_HDMI_START) {
		if (lcd_ext_param[0].hdmi_func.hdmi_suspend)
			lcd_ext_param[0].hdmi_func.hdmi_suspend();
	}
#endif
	up(&sh_mobile_sem_hdmi);

	disp_delete.handle = suspend_handle;
	screen_display_delete(&disp_delete);

	return 0;
}

static int lcdc_resume(void)
{
	unsigned int lcd_num;

	down(&sh_mobile_sem_hdmi);

	for (lcd_num = 0; lcd_num < CHAN_NUM; lcd_num++) {
		if (lcd_ext_param[lcd_num].aInfo != NULL) {
			if (lcd_ext_param[lcd_num].blank_lvl
				!= FB_BLANK_NORMAL) {
				continue;
			}
			lcd_ext_param[lcd_num].blank_lvl = FB_BLANK_UNBLANK;
			if (lcd_ext_param[lcd_num].panel_func.panel_resume) {
				lcd_ext_param[lcd_num].
					panel_func.panel_resume(panel_data);
			}

		}
	}

#if FB_SH_MOBILE_HDMI
	if (lcd_ext_param[0].hdmi_flag == FB_HDMI_START) {
		if (lcd_ext_param[0].hdmi_func.hdmi_resume)
			lcd_ext_param[0].hdmi_func.hdmi_resume();
	}
#endif

	up(&sh_mobile_sem_hdmi);

	return 0;
}

static int sh_mobile_fb_blank(int blank, struct fb_info *info)
{
	if ((blank == FB_BLANK_NORMAL) || (blank == FB_BLANK_POWERDOWN))
		return lcdc_suspend();
	else if (blank == FB_BLANK_UNBLANK)
		return lcdc_resume();
	else
		return -EINVAL;
}

static int fb_lcdc_remove(struct platform_device *pdev);

static unsigned long RoundUpToMultiple(unsigned long x, unsigned long y)
{
	unsigned long div = x / y;
	unsigned long rem = x % y;

	return (div + ((rem == 0) ? 0 : 1)) * y;
}

static unsigned long GCD(unsigned long x, unsigned long y)
{
	while (y != 0) {
		unsigned long r = x % y;
		x = y;
		y = r;
	}
	return x;
}

static unsigned long LCM(unsigned long x, unsigned long y)
{
	unsigned long gcd = GCD(x, y);

	return (gcd == 0) ? 0 : ((x / gcd) * y);
}

/* This function generates a list of command to be send to the panel in the
   following format

   +------+-------------+----------------+--------------+---------------------+
   | SIZE | TYPE OR TAG | Command & Data |  . . . . . . | 0 (null terminated) |
   +------+-------------+----------------+--------------+---------------------+

   for seq[i].type == DISPCTRL_DRAW_BLACK,  format will be
   +---+--------------------+---+
   | 1 | 1 (MIPI_DSI_BLACK) | 0 |
   +---+--------------------+---+

*/

static char * __init get_seq(struct dispctrl_rec *seq)
{
	unsigned int list_len = 0, cmnd_len = 0;
	unsigned char *buff, *curr, *alloc;
	bool is_generic = false;

	unsigned int i = 0;
	for (; (seq[i].type != DISPCTRL_LIST_END); ++i) {
		if (seq[i].type == DISPCTRL_WR_CMND ||
				seq[i].type == DISPCTRL_GEN_WR_CMND)
			list_len += 3; /* 1 for size, 1 for type or tag
					  1 more for the command */
		else if (seq[i].type == DISPCTRL_WR_DATA)
			list_len++;
		else	/* DRAW_BLACK, SLEEP_MS, DISPCTRL_PANEL_MDNIE */
			list_len += 3;
	}

	list_len++; /* one extra for NULL */

	alloc = kzalloc(list_len, GFP_KERNEL);

	if (!alloc) {
		pr_err("Unable to allocate Memory !\n");
		return NULL;
	}
	buff = alloc;
	curr = buff;
	for (i = 0; (seq[i].type != DISPCTRL_LIST_END); ++i) {
		is_generic = false;
		switch (seq[i].type) {

		case DISPCTRL_GEN_WR_CMND:
			is_generic = true;
		case DISPCTRL_WR_CMND:
			cmnd_len = 1;
			curr = buff;
			buff += 2; /* skip the size and type */
			*buff++ = seq[i].val;
			while (seq[i + 1].type == DISPCTRL_WR_DATA) {
				cmnd_len++;
				i++;
				*buff++ = seq[i].val;
			}
			*curr++ = cmnd_len;
			if (cmnd_len > 2) {
				if (is_generic)
					*curr = (unsigned char)
							MIPI_DSI_GEN_LONG_WRITE;
				else
					*curr = MIPI_DSI_DCS_LONG_WRITE;
			} else if (cmnd_len == 2) {
				if (is_generic)
					*curr = MIPI_DSI_GEN_SHORT_WRITE_PARAM;
				else
					*curr = MIPI_DSI_DCS_SHORT_WRITE_PARAM;
			} else if (cmnd_len == 1) {
				if (is_generic)
					*curr = MIPI_DSI_GEN_SHORT_WRITE;
				else
					*curr = MIPI_DSI_DCS_SHORT_WRITE;
			} else
				pr_err("Invalid command lengh !\n");
			break;

		case DISPCTRL_DRAW_BLACK:
			*buff++ = 1; /* size of the payload */
			*buff++ = MIPI_DSI_BLACK;
			*buff++ = seq[i].val;
			break;
		case DISPCTRL_SLEEP_MS:
			*buff++ = 1; /*size of the payload */
			*buff++ = MIPI_DSI_DELAY;
			*buff++ = seq[i].val;
			break;
#ifdef CONFIG_LDI_MDNIE_SUPPORT
		case DISPCTRL_PANEL_MDNIE:
			*buff++ = 1;
			*buff++ = MIPI_DSI_PANEL_MDNIE;
			*buff++ = 0;
			break;
#endif
		default:
			pr_err("Invalid control list type %d\n", seq[i].type);
		}
	}
	*buff = 0; /* NULL termination */

	if (alloc - (buff - list_len + 1))
		pr_err("Sequence calculation mismatch !\n");

	return alloc;
}

int __init populate_panel_data(
			struct platform_device *pdev,
			struct panel_info  *panel_data,
			struct fb_board_cfg *pdata,
			struct lcd_config *cfg)
{
	strcpy(panel_data->name, cfg->name);

	panel_data->clk_src = pdata->clk_src;

	panel_data->reg[LDO_3V].supply = pdata->reg[LDO_3V].supply;

	panel_data->reg[LDO_1V8].supply = pdata->reg[LDO_1V8].supply;

	panel_data->rst.rst_gpio_num = pdata->rst.rst_gpio_num;

	panel_data->panel_dsi_irq = pdata->panel_dsi_irq;

	panel_data->lcdc_resources = pdata->lcdc_resources = pdev->resource;

	panel_data->col_fmt_bpp = pdata->col_fmt_bpp;

	panel_data->cont_clock = cfg->cont_clock;

	panel_data->hs_bps = ((pdata->hs_bps > cfg->max_hs_bps)
		|| (pdata->hs_bps == 0))
				? cfg->max_hs_bps : pdata->hs_bps;

	panel_data->lp_bps = ((pdata->lp_bps > cfg->max_lp_bps)
		|| (pdata->lp_bps == 0))
				? cfg->max_lp_bps : pdata->lp_bps;

	panel_data->lanes = ((pdata->lanes > cfg->max_lanes)
		|| (pdata->lanes == 0))
				? cfg->max_lanes : pdata->lanes;

	if (pdata->vmode && !(cfg->modes_supp == LCD_VID_ONLY ||
				  cfg->modes_supp == LCD_CMD_VID_BOTH)) {
		pr_err("Video mode is not supported by the panel !\n");
		r8a7373_set_mode(0);
		pdata->vmode = 0;
	}

	if (!pdata->vmode && !(cfg->modes_supp == LCD_CMD_ONLY ||
				  cfg->modes_supp == LCD_CMD_VID_BOTH)) {
		pr_err("Command mode is not supported by the panel !\n");
		r8a7373_set_mode(1);
		pdata->vmode = 1;
	}

	panel_data->vmode = pdata->vmode;

	panel_data->hbp = cfg->hbp;
	panel_data->hfp = cfg->hfp;
	panel_data->hsync = cfg->hsync;
	panel_data->vbp = cfg->vbp;
	panel_data->vfp = cfg->vfp;
	panel_data->vsync = cfg->vsync;
	panel_data->phy_width = cfg->phy_width;
	panel_data->phy_height = cfg->phy_height;
	panel_data->width = (pdata->width <= cfg->width)
				? pdata->width : cfg->width;
	panel_data->height = (pdata->height <= cfg->height)
				? pdata->height : cfg->height;

	panel_data->rst.setup = cfg->setup;
	panel_data->rst.pulse = cfg->pulse;
	panel_data->pack_send_mode = cfg->pack_send_mode;
	panel_data->verify_id = cfg->verify_id;

	update_register(cfg, panel_data, pdata);


	if (pdata->vmode) {
		if (cfg->init_seq_vid) {
			panel_data->init_seq = get_seq(cfg->init_seq_vid);
		} else {
			pr_err("Video mode sequence not initialized!\n");
			goto err_init_seq;
		}
	} else {
		if (cfg->init_seq_cmd) {
			panel_data->init_seq = get_seq(cfg->init_seq_cmd);
		} else {
			pr_err("Command mode sequence not initialized!\n");
			goto err_init_seq;
		}
	}


	if (!panel_data->init_seq)
		goto err_init_seq;

	panel_data->suspend_seq = get_seq(cfg->suspend_seq);
	if (!panel_data->suspend_seq)
		goto err_suspend_seq;

	if (cfg->init_seq_min) {
		panel_data->init_seq_min = get_seq(cfg->init_seq_min);
		if (!panel_data->init_seq_min)
			goto err_init_seq_min;
	}

	if (cfg->panel_id) {
		panel_data->panel_id = get_seq(cfg->panel_id);
		if (!panel_data->panel_id)
			goto err_panel_id;
	}

	return 0;

err_panel_id:
	kfree(panel_data->init_seq_min);
	pr_err("Error: err_lcd_freq\n");
err_init_seq_min:
	kfree(panel_data->suspend_seq);
	pr_err("Error: err_init_seq_min\n");
err_suspend_seq:
	kfree(panel_data->init_seq);
	pr_err("Error: err_suspend_seq\n");
err_init_seq:
	pr_err("Error: err_init_seq\n");
	return -1;
}

int atoi(char *s)
{
	bool negative = false;
	unsigned long int val = 0;

	if (!s || !strlen(s)) {
		pr_err("%s : Null string or zero length\n", __func__);
		return 0;
	}

	switch (s[0]) {

	case '-':
		negative = true;
	case '+':
		s++;
	default:
		break;

	}

	if (kstrtoul(s, 16, &val) < 0)
		return 0;

	return  negative ? val * -1 : val;
}

int __init lcd_boot_init(char *panel)
{
	int panel_idx = PANEL_MAX;
	struct lcd_config *lcd_cfg;
	struct device_node *np, *rt_np;
	int i;
	if (panel && strlen(panel)) {
		panel_idx = atoi(panel);
		pr_err("Recieved  panel index is %d\n", panel_idx);

		if (panel_idx <= PANEL_NONE || panel_idx >= PANEL_MAX) {
			pr_err("%s : Invalid panel id (%d) from BOOT args !\n",
							__func__, panel_idx);
			g_panel_index = PANEL_MAX;

			/* Disable all LCD/RT node except the default one */
			for (i = 1; i < ARRAY_SIZE(lcd_node_name); i++) {
				np = of_find_node_by_path(lcd_node_name[i]);
				if (np) {
					pr_info(
					"DT:%s and %s node marked as disabled\n",
						lcd_node_name[i],
						rt_node_name[i]);
					of_update_property(np,
						&device_disabled);
					rt_np = of_find_node_by_path(
							rt_node_name[i]);
					if (rt_np) {
						of_update_property(rt_np,
							&device_disabled);
					} else {
						pr_err("DT:%s node not found\n",
							rt_node_name[i]);
					}
					of_node_put(rt_np);
				}
				of_node_put(np);
			}

			return -1;
		}

		g_panel_index = panel_idx;
		if (g_panel_index != PANEL_MAX)
			lcd_cfg = get_panel_cfg(g_panel_index);

		for (i = 0; i < ARRAY_SIZE(lcd_node_name); i++) {
			np = of_find_node_by_path(lcd_node_name[i]);
			if (np) {
				if (!of_property_match_string(np,
						"pname", lcd_cfg->name)) {
					pr_err("DT:%s node marked as status okay\n",
							lcd_node_name[i]);
				} else {
					pr_err(
					"DT:%s and %s node marked as status disabled\n",
						lcd_node_name[i],
						rt_node_name[i]);
					of_update_property(np,
						&device_disabled);
					rt_np = of_find_node_by_path(
							rt_node_name[i]);
					if (rt_np) {
						of_update_property(rt_np,
							&device_disabled);
					} else {
						pr_err("DT:%s node not found, so not disabled\n",
							rt_node_name[i]);
					}
					of_node_put(rt_np);
				}
			} else {
				pr_err("DT:%s node not found, so not disabled\n",
							lcd_node_name[i]);
			}
			of_node_put(np);
		}
		g_disp_boot_init = 1;
		return 0;
	}
	return -1;
}

__setup("lcd_panel=", lcd_boot_init);
extern unsigned int hfp_adjust;

static int fb_lcdc_init(struct platform_device *pdev, int i,
				struct fb_board_cfg *pdata)
{
	int error;
	struct fb_info *info;
	struct fb_panel_info panel_info;
	struct fb_lcdc_chan_cfg *cfg;
	void __iomem *temp;
	void __iomem *lcdc_base;
	void __iomem *rloader_buff_v;

	if (!lcd_ext_param[i].panel_func.panel_info)
		return 0;

	cfg = &pdata->priv->ch[i].cfg;

	panel_data->info[i] = framebuffer_alloc(0, &pdev->dev);
	if (!panel_data->info[i]) {
		dev_err(&pdev->dev, "unable to allocate fb_info\n");
		error = -ENOMEM;
		goto fb_lcdc_init_err1;
	}

	panel_info = lcd_ext_param[i].panel_func.panel_info(panel_data);

	info = panel_data->info[i];
	info->fbops = &sh_mobile_lcdc_ops;
	info->var.xres = info->var.xres_virtual
		= panel_data->width;
	info->var.yres =  panel_data->height;
	/* Default Y virtual resolution is 2x panel size */
	info->var.width = panel_data->phy_width;
	info->var.height = panel_data->phy_height;
	info->var.pixclock = panel_data->pixclock;

	info->var.left_margin = panel_data->hbp;
	//info->var.right_margin = panel_data->hfp;
	info->var.right_margin = panel_data->hfp + hfp_adjust;
	info->var.upper_margin = panel_data->vbp;
	info->var.lower_margin = panel_data->vfp;
	info->var.hsync_len = panel_data->hsync;
	info->var.vsync_len = panel_data->vsync;

	info->var.activate = FB_ACTIVATE_NOW;
	error = sh_mobile_lcdc_set_bpp(&info->var,
			panel_data->col_fmt_bpp);
	if (error)
		goto fb_lcdc_init_err2;

	info->fix = sh_mobile_lcdc_fix;
	sh_mobile_fb_set_par(info);
	lcd_ext_param[i].mem_size = info->fix.smem_len;

#ifdef CONFIG_FB_SH_MOBILE_DOUBLE_BUF
	/* onscreen buffer 2 */
	temp = dma_alloc_coherent(&pdev->dev, info->fix.smem_len,
			&pdata->priv->ch[i].dma_handle, GFP_KERNEL);
	if (!temp) {
		dev_err(&pdev->dev, "unable to allocate buffer\n");
		error = -ENOMEM;
		goto fb_lcdc_init_err2;
	}
	panel_data->fb_start = pdata->priv->ch[i].dma_handle;
	panel_data->fb_size = info->fix.smem_len;

	panel_data->virt_addr = (unsigned int) temp;
	register_ramdump_split("Framebuffer0", panel_data->fb_start,
				panel_data->fb_start +
					(panel_data->fb_size / 2) - 1);
	register_ramdump_split("Framebuffer1",
			panel_data->fb_start + (panel_data->fb_size / 2),
				panel_data->fb_start + panel_data->fb_size - 1);

#else
	buf = dma_alloc_coherent(&pdev->dev, info->fix.smem_len,
			&pdata->priv->ch[i].dma_handle, GFP_KERNEL);
	if (!buf) {
		dev_err(&pdev->dev, "unable to allocate buffer\n");
		error = -ENOMEM;
		goto fb_lcdc_init_err2;
	}
	temp = ioremap(panel_data->fb_start, info->fix.smem_len);
	if (NULL == temp) {
		error = -ENOMEM;
		goto fb_lcdc_init_err3;
	} else {
		panel_data->virt_addr = (unsigned int) temp;
	}
#endif
	info->pseudo_palette = &pdata->priv->ch[i].pseudo_palette;
	info->flags = FBINFO_FLAG_DEFAULT;

	error = fb_alloc_cmap(&info->cmap, PALETTE_NR, 0);
	if (error < 0) {
		dev_err(&pdev->dev, "unable to allocate cmap\n");
		goto fb_lcdc_init_err4;
	}
	fb_set_cmap(&info->cmap, info);


#ifdef CONFIG_FB_SH_MOBILE_DOUBLE_BUF
	/* onscreen buffer 2 */
	info->fix.smem_start = panel_data->fb_start;
	info->screen_base = (char __iomem *)panel_data->virt_addr;
#else
	memset(buf, 0, info->fix.smem_len);
	info->fix.smem_start = pdata->priv->ch[i].dma_handle;
	info->screen_base = buf;
#endif
	info->device = &pdev->dev;
	info->par = &pdata->priv->ch[i];

	if (panel_data->vmode) {
		unsigned long lcdc0_phys, lcdc0_sz;
		struct resource *res;

		/* If customer doesn't pass fb=** in bootargs,
		 * update here */
		if (!rloader_buff_p)
			rloader_buff_p = CUST_LOGO_ADDR;

		if (!request_mem_region(rloader_buff_p,
					info->fix.smem_len/2, "LCDC_DRIVER")) {
			pr_err("Error in request_mem_region !\n");
			error = -ENOMEM;
			goto fb_lcdc_init_err5;
		}
		rloader_buff_v = ioremap(rloader_buff_p,
				info->fix.smem_len/2);

		if (rloader_buff_v) {
			memcpy((void *)panel_data->virt_addr,
					(const void *)rloader_buff_v,
					info->fix.smem_len/2);
			iounmap(rloader_buff_v);
		} else {
			pr_err("ioremap failed for rloader buffer\n");
		}

		release_region(rloader_buff_p, info->fix.smem_len/2);


		/* Get physical address of LCDC0 from board file */
		res = platform_get_resource_byname(pdev,
				IORESOURCE_MEM, "lcdc0");
		if (!res) {
			pr_err("Couldn't get LCDC0 base register\n");
			error = -ENOMEM;
			goto fb_lcdc_init_err5;
		}
		lcdc0_phys = res->start;
		lcdc0_sz = PAGE_ALIGN(res->end - res->start + 1);

		if (!request_mem_region(lcdc0_phys, lcdc0_sz,
					"LCDC_DRIVER")) {
			pr_err("Error in request_mem_region !\n");
			error = -ENOMEM;
			goto fb_lcdc_init_err5;
		}
		lcdc_base = ioremap(lcdc0_phys, SZ_1M);
		if (lcdc_base) {
			writel(panel_data->fb_start,
					lcdc_base + LCDC_MLDSA1R);
			writel(panel_data->fb_start,
					lcdc_base + LCDC_MLDSA2R);
			iounmap(lcdc_base);
		} else {
			pr_err("ioremap failed\n");
		}

		release_region(lcdc0_phys, SZ_1M);
	} else {
		memset(temp, 0 , info->fix.smem_len);
	}

	return 0;

fb_lcdc_init_err5:
	fb_dealloc_cmap(&info->cmap);

fb_lcdc_init_err4:
	iounmap((void __iomem *)panel_data->virt_addr);
	panel_data->virt_addr = 0;

#ifndef CONFIG_FB_SH_MOBILE_DOUBLE_BUF
fb_lcdc_init_err3:
#endif
	dma_free_coherent(&pdev->dev, info->fix.smem_len,
			info->screen_base,
			pdata->priv->ch[i].dma_handle);
	info->screen_base = NULL;

fb_lcdc_init_err2:
	framebuffer_release(panel_data->info[i]);
	panel_data->info[i] = NULL;

fb_lcdc_init_err1:
	return error;
}


static void fb_lcdc_deinit(struct platform_device *pdev, int i)
{
	struct fb_info *info;

	if (!lcd_ext_param[i].panel_func.panel_info)
		return;

	info = panel_data->info[i];

	fb_dealloc_cmap(&info->cmap);

	iounmap((void __iomem *)panel_data->virt_addr);
	panel_data->virt_addr = 0;

#ifndef CONFIG_FB_SH_MOBILE_DOUBLE_BUF
	dma_free_coherent(&pdev->dev, info->fix.smem_len,
			info->screen_base,
			priv->ch[i].dma_handle);
	info->screen_base = NULL;
#endif

	framebuffer_release(panel_data->info[i]);
	panel_data->info[i] = NULL;
}

static int parse_dt_panel_data(struct device *p_device,
			struct panel_info  *panel_data)
{
	struct device_node *np = p_device->of_node;
	int val;
	if (!of_find_property(np, "te-irq", NULL)) {
		dev_err(p_device, "DT property te-irq not found!\n");
		panel_data->detect.detect_gpio = -1;
		goto err_gpio;
	} else {
		panel_data->detect.detect_gpio = of_get_named_gpio(np,
						"te-irq", 0);
	}

	/* To enable ESD check include the field "esd-check"
	in the dt file.esd-check:
	( 0: Disabled, 1: GPIO, 2: ID SEQ, 3: TE_GPIO video mode only ) */
	if (of_property_read_u32(np, "esd-check", &val))
		panel_data->esd_data.esd_check = 0;
	else  if (g_panel_index != PANEL_MAX)
		panel_data->esd_data.esd_check = val;

	/*  ESD periodicaly check time(ms) */
	if (of_property_read_u32(np, "esd-dur-ms", &val))
		panel_data->esd_data.esd_dur = 1000; /* default */
	else
		panel_data->esd_data.esd_dur = val;

	if (panel_data->esd_data.esd_check == ESD_GPIO ||
		panel_data->esd_data.esd_check == ESD_TE_GPIO) {
		if (!of_find_property(np, "esd-irq", NULL)) {
			panel_data->esd_data.esd_irq_portno = -1;
			goto err_gpio;
		} else {
			panel_data->esd_data.esd_irq_portno =
						of_get_named_gpio(np,
							"esd-irq", 0);
		}
	}

	return 0;
err_gpio:
	return -ENODEV;
}

static int parse_dt_data(struct device *p_device, struct fb_board_cfg *pdata)
{
	int ret;
	bool mode;
	const char *out_string;
	unsigned int i;
	struct device_node *np = p_device->of_node;
	struct device_node *rt_node;

	ret = of_property_read_string(np, "pname", &out_string);
	if (ret) {
		dev_err(p_device, "DT property pname not found!\n");
		goto of_read_error;
	}
	pdata->pname = (char *)out_string;
	rt_node = of_parse_phandle(np, "rt-boot", 0);
	ret = of_property_read_u32(rt_node->child,
			"renesas,mode", (u32 *)&mode);
	if (ret) {
		dev_err(p_device, "DT property renesas,mode from RT not found!\n");
		goto of_read_error;
	}
	/* if rtboot mode = 1 then command mode, mode = 0 then video mode
	 * and vmode = 1 for video mode, vmode = 0 for command mode
	 */
	pdata->vmode = !mode;
	ret = of_property_read_u32(rt_node->child,
			"renesas,height", &i);
	if (ret) {
		dev_err(p_device,
		"DT property renesas,height from RT not found!\n");
		goto of_read_error;
	}

	pdata->height = i;
	ret = of_property_read_u32(rt_node->child,
			"renesas,width", &i);
	if (ret) {
		dev_err(p_device,
		"DT property renesas,width from RT not found!\n");
		goto of_read_error;
	}

	pdata->width = i;
	of_node_put(rt_node);

	ret = of_property_read_u32_index(np,
			"renesas,clock-source", 1, &pdata->clk_src);
	if (ret) {
		dev_err(p_device, "DT property renesas,clock-source not found!\n");
		goto of_read_error;
	}
	ret = of_property_read_string_index(np,
			"regulators", 0, &pdata->reg[LDO_3V].supply);
	if (ret) {
		dev_err(p_device, "DT property regulator pdata->reg[LDO_3V].supply not found!\n");
		goto of_read_error;
	}
	ret = of_property_read_string_index(np,
			"regulators", 1, &pdata->reg[LDO_1V8].supply);
	if (ret) {
		dev_err(p_device, "DT property regulators pdata->reg[LDO_1V8].supply not found!\n");
		goto of_read_error;
	}
	if (!of_find_property(np, "reset-gpio", NULL)) {
		dev_err(p_device, "DT property reset-gpio not found!\n");
		pdata->rst.rst_gpio_num = -1;
		goto of_read_error;
	} else {
		pdata->rst.rst_gpio_num = of_get_named_gpio(np,
						"reset-gpio", 0);
	}

	if (!of_find_property(np, "panel-dsi-irq", NULL)) {
		pdata->panel_dsi_irq = -1;
	} else {
		ret = of_property_read_u32(np, "panel-dsi-irq", &i);
		if (ret) {
			dev_err(p_device, "DT: property panel-dsi-irq not found!\n");
			goto of_read_error;
		}
		pdata->panel_dsi_irq = i;
	}
	ret = of_property_read_u32(np, "renesas,col-fmt-bpp", &i);
	if (ret) {
		dev_err(p_device, "DT property col-fmt-bpp not found!\n");
		goto of_read_error;
	}
	pdata->col_fmt_bpp = i;
	ret = of_property_read_u32_index(np,
		"renesas,channel", 1, &pdata->ch[0].chan);
	if (ret) {
		dev_err(p_device, "DT property renesas,channel not found!\n");
		goto of_read_error;
	}
	ret = of_property_read_u32(np,
			"renesas,desense-offset", &i);
	if (ret) {
		dev_err(p_device,
		"DT property renesas,desense-offset not found!\n");
		goto of_read_error;
	}

	pdata->desense_offset = i;
	return ret;
of_read_error:
	ret = -ENODEV;

	return ret;
}

static const struct of_device_id sh_mobile_lcdc_fb_of_match[] = {
	{.compatible = "renesas,lcdc-shmobile",},
	{},
};

MODULE_DEVICE_TABLE(of, sh_mobile_lcdc_fb_of_match);
//++ lcd  sys  by sw
static struct kobject *lcd_panel_info_kobj;

static ssize_t lcd_show_ic_name(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	sprintf(buf,"%s",panel_data->name);
	printk("%s\n",buf);
        return strlen(buf);
	
}
static DEVICE_ATTR(lcd_name, 0444,	lcd_show_ic_name, NULL);

static void lcd_panel_info_sysfs_deinit(void)
{
	if(lcd_panel_info_kobj != NULL)
	{
		sysfs_remove_file(lcd_panel_info_kobj, &dev_attr_lcd_name.attr);
		kobject_del(lcd_panel_info_kobj);
	}
}	


static int lcd_panel_info_sysfs_init(void)
{
	int ret ;

	lcd_panel_info_kobj = kobject_create_and_add("lcd_panel_info", NULL) ;
	if (lcd_panel_info_kobj == NULL) {
		printk(KERN_ERR "[elan]%s: subsystem_register failed\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
	ret = sysfs_create_file(lcd_panel_info_kobj, &dev_attr_lcd_name.attr);
	if (ret) {
		printk(KERN_ERR "[elan]%s: sysfs_create_file failed\n", __func__);
		return ret;
	}
	return 0 ;
}
//--
static int __ref fb_lcdc_probe(struct platform_device *pdev)
{
	struct fb_info *info = NULL;
	struct lcd_config *lcd_cfg;
	struct resource *res, *ires;
	int error = 0;
	int i, j;
	struct fb_panel_info panel_info;
	struct clk *pck, *tck;
	struct fb_board_cfg *pdata =  pdev->dev.platform_data;
#ifndef CONFIG_FB_SH_MOBILE_DOUBLE_BUF
	void *buf = NULL;
#endif

	pr_err("%s:  start!\n", __func__);

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "no platform data defined\n");
		error = -EINVAL;
		goto err0;
	}

	if (pdev->dev.of_node) {
		if (parse_dt_data(&pdev->dev, pdata)) {
			dev_err(&pdev->dev,
				"DT:failed parse_dt_data error\n");
			error = -EINVAL;
			goto err0;
		}
	} else {
		pr_err("%s: no DT\n", __func__);
	}
	if (g_panel_index != PANEL_MAX)
		lcd_cfg = get_panel_cfg(g_panel_index);
	else {
		pr_err("Taking panel name from Board!\n");
		lcd_cfg = get_panel_cfg_byname(pdata->pname);
	}

	if (lcd_cfg)
		pr_err("Panel Configuration found for %s\n", lcd_cfg->name);
	else {
		pr_err("No Panel Configuration could be found for %d!\n",
								g_panel_index);
		error = -ENXIO;
		goto err1;
	}

	panel_data = kmalloc(sizeof(struct panel_info), GFP_KERNEL);
	if (!panel_data) {
		pr_err("Unable to allocate memory to panel_data !\n");
		error = -ENOMEM;
		goto err1;
	}
	if (pdev->dev.of_node) {
		if (parse_dt_panel_data(&pdev->dev, panel_data)) {
			dev_err(&pdev->dev,
				"DT:failed parse_dt_panel_data error\n");
			error = -EINVAL;
			goto err2;
		}
	}
	panel_data->pdev = pdev;

	if (populate_panel_data(pdev, panel_data, pdata, lcd_cfg)) {
		pr_err("Unable to populate display driver data !\n");
		error = -EINVAL;
		goto err2;
	}

	if (g_disp_boot_init)
		panel_data->is_disp_boot_init = true;

	res = platform_get_resource_byname(pdev,
			IORESOURCE_MEM, "irq_generator");
	if (pdev->dev.of_node) {
		ires = platform_get_resource_byname(pdev,
				IORESOURCE_MEM, "rt_irq");
		i = ires->start;
	} else {
		i = platform_get_irq(pdev, 1);
	}
	if (!res || i < 0) {
		dev_err(&pdev->dev, "cannot get platform resources\n");
		error = -ENOENT;
		goto err2;
	}

	pdata->priv = kzalloc(sizeof(struct fb_lcdc_priv), GFP_KERNEL);
	if (!pdata->priv) {
		pr_err("cannot allocate device data\n");
		error = -ENOMEM;
		goto err2;
	}

	pdata->priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, pdata->priv);
	/* pdata = pdev->dev.platform_data;*/

	pdata->priv->irq = i;
	pdata->priv->irq_enabled = true;
	error = request_irq(i, sh_mobile_lcdc_irq, IRQF_TRIGGER_RISING,
			    dev_name(&pdev->dev), pdata->priv);
	if (error) {
		dev_err(&pdev->dev, "unable to request irq\n");
		goto err3;
	}

	enable_irq_wake(i);

	/* irq base address */
	irqc_baseaddr = ioremap(res->start, resource_size(res));
	if (NULL == irqc_baseaddr) {
		dev_err(&pdev->dev, "ioremap for irqc_baseaddr fail\n");
		error = -ENOMEM;
		goto err4;
	}

#if FB_SH_MOBILE_REFRESH
	sh_mobile_wq = create_singlethread_workqueue("sh_mobile_lcd");
	if (NULL == sh_mobile_wq) {
		dev_err(&pdev->dev, "create_singlethread_workqueue fail\n");
		error = -ENOMEM;
		goto err5;
	}
#endif

	j = 0;
	for (i = 0; i < ARRAY_SIZE(pdata->ch); i++) {
		pdata->priv->ch[j].lcdc = pdata->priv;
		memcpy(&pdata->priv->ch[j].cfg,
				&pdata->ch[i], sizeof(pdata->ch[i]));

		switch (pdata->ch[i].chan) {
		case LCDC_CHAN_MAINLCD:
			lcd_ext_param[i].panel_func =
				r_mobile_panel_func(RT_DISPLAY_LCD1);
			if (!lcd_ext_param[i].panel_func.panel_info) {
				j++;
				break;
			}
			panel_info =
				lcd_ext_param[i].panel_func.panel_info
								(panel_data);

			lcd_ext_param[i].o_mode = RT_DISPLAY_LCD1;
#if FB_SH_MOBILE_HDMI
			lcd_ext_param[i].draw_mode = RT_DISPLAY_LCDHDMI;
#ifdef CONFIG_MISC_R_MOBILE_COMPOSER_REQUEST_QUEUE
#if SH_MOBILE_COMPOSER_SUPPORT_HDMI > 1
			/* disable mirror to HDMI */
			lcd_ext_param[i].draw_mode = RT_DISPLAY_LCD1;
#endif
#endif
#else
			lcd_ext_param[i].draw_mode = RT_DISPLAY_LCD1;
#endif

			lcd_ext_param[i].rect_x = SH_MLCD_RECTX;
			lcd_ext_param[i].rect_y = SH_MLCD_RECTY;

			lcd_ext_param[i].rect_width = panel_data->width;
			lcd_ext_param[i].rect_height = panel_data->height;

			lcd_ext_param[i].alpha = 0xFF;
			lcd_ext_param[i].key_clr = SH_MLCD_TRCOLOR;
			lcd_ext_param[i].v4l2_state = RT_DISPLAY_REFRESH_ON;
			lcd_ext_param[i].delay_flag = 0;
			lcd_ext_param[i].refresh_on = 0;
			lcd_ext_param[i].phy_addr = panel_data->fb_start;
			if (lcd_ext_param[i].rect_height
			    > lcd_ext_param[i].rect_width) {
				lcd_ext_param[i].rotate
					= RT_DISPLAY_ROTATE_270;
			} else {
				lcd_ext_param[i].rotate
					= RT_DISPLAY_ROTATE_0;
			}
#if FB_SH_MOBILE_REFRESH
			INIT_DELAYED_WORK(&lcd_ext_param[i].ref_work,
					  refresh_work);
#endif
			lcd_ext_param[i].blank_lvl = FB_BLANK_UNBLANK;
			j++;
			break;
		case LCDC_CHAN_SUBLCD:
			lcd_ext_param[i].panel_func =
					r_mobile_panel_func(RT_DISPLAY_LCD2);
			if (!lcd_ext_param[i].panel_func.panel_info) {
				j++;
				break;
			}
			panel_info =
				lcd_ext_param[i].panel_func.panel_info
								(panel_data);

			lcd_ext_param[i].o_mode = RT_DISPLAY_LCD2;
			lcd_ext_param[i].draw_mode = RT_DISPLAY_LCD2;
			lcd_ext_param[i].rect_x = SH_SLCD_RECTX;
			lcd_ext_param[i].rect_y = SH_SLCD_RECTY;

			lcd_ext_param[i].rect_width = panel_data->width;
			lcd_ext_param[i].rect_height = panel_data->height;
			lcd_ext_param[i].alpha = 0xFF;
			lcd_ext_param[i].key_clr = SH_SLCD_TRCOLOR;
			lcd_ext_param[i].v4l2_state = RT_DISPLAY_REFRESH_ON;
			lcd_ext_param[i].delay_flag = 0;
			lcd_ext_param[i].refresh_on = 0;
			/* SUBLCD undefined */
			lcd_ext_param[i].phy_addr = panel_data->fb_start;
			lcd_ext_param[i].rotate = RT_DISPLAY_ROTATE_0;
#if FB_SH_MOBILE_REFRESH
			INIT_DELAYED_WORK(&lcd_ext_param[i].ref_work,
					  refresh_work);
#endif
			lcd_ext_param[i].blank_lvl = FB_BLANK_UNBLANK;
			j++;
			break;
		}
	}

	if (!j) {
		dev_err(&pdev->dev, "no channels defined\n");
		error = -EINVAL;
		goto err6;
	}

	for (i = 0; i < j; i++) {
		error = fb_lcdc_init(pdev, i, pdata);

		if (error) {
			dev_err(&pdev->dev, "fb_lcdc_init error\n");
			for (--i; i >= 0; --i)
				fb_lcdc_deinit(pdev, i);
			goto err6;
		}
	}

	for (i = 0; i < j; i++) {
		struct fb_lcdc_chan *ch = pdata->priv->ch + i;

		info = panel_data->info[i];
		error = register_framebuffer(info);
		if (error < 0) {
			for (--i; i >= 0; --i) {
				info = panel_data->info[i];
				unregister_framebuffer(info);
				goto err7;
			}
		}

		dev_info(info->dev,
			 "registered %s/%s as %dx%d %dbpp.\n",
			 pdev->name,
			 (ch->cfg.chan == LCDC_CHAN_MAINLCD) ?
			 "mainlcd" : "sublcd",
			 (int) info->var.xres,
			 (int) info->var.yres,
			  panel_data->col_fmt_bpp);

		if (lcd_ext_param[i].panel_func.panel_probe) {
			lcd_ext_param[i].panel_func.
				panel_probe(info, panel_data);
		}

		lcd_ext_param[i].aInfo = NULL;
		lcd_ext_param[i].lcd_type = ch->cfg.chan;
		lcd_ext_param[i].draw_bpp = panel_data->col_fmt_bpp;
		sema_init(&lcd_ext_param[i].sem_lcd, 1);
	}

	sema_init(&sh_mobile_sem_hdmi, 1);

#if FB_SH_MOBILE_HDMI

	lcd_ext_param[0].hdmi_func = r_mobile_hdmi_func();
	lcd_ext_param[0].hdmi_flag = FB_HDMI_STOP;
#endif
	error = device_create_file(pdata->priv->ch[0].lcdc->dev,
							&dev_attr_vsync);

	if (error) {
		printk(KERN_ALERT "device_create_file error\n");
		goto err8;
	}

	init_waitqueue_head(&sh_lcd_vsync.vsync_wait);

	sh_lcd_vsync.wq_vsync_off =
		create_singlethread_workqueue("vsync_cpuidle_wq");
	if (sh_lcd_vsync.wq_vsync_off == NULL) {
		dev_err(info->dev, "failed to create work queue\n");
		error = -ENOMEM;
		goto err9;
	}
	INIT_DELAYED_WORK (&wk_vsync_off, vsync_off_work);

	fb_debug = 0;

	//++add lcd sys
	lcd_panel_info_sysfs_init();
	//--
	tck = clk_get(&pdev->dev, "dsit_clk");
	if (IS_ERR_OR_NULL(tck)) {
		pr_err("Error getting dsit_clk\n");
		error = -EINVAL;
		goto err10;
	}

	pck = clk_get(&pdev->dev, "dsip_clk");
	if (IS_ERR_OR_NULL(pck)) {
		pr_err("Error getting dsip_clk\n");
		error = -EINVAL;
		goto err11;
	}

	/* This is mainly for usecount, clock handling is done in RT-Domain */
	clk_enable(tck);
	clk_enable(pck);

	/* Kthread_run must be execute at the end to make sure there
	 * is no error after that. Otherwise, the err_process will be
	 * blocked when trying to stop the kthread.
	 */
	sh_lcd_vsync.vsync_thread = kthread_run(vsync_recv_thread,
						pdata->priv->ch[0].lcdc,
						"sh-fb-vsync");
	if (IS_ERR(sh_lcd_vsync.vsync_thread)) {
		error = PTR_ERR(sh_lcd_vsync.vsync_thread);
		pr_err("kthread_run error\n");
		goto err12;
	}

	pr_err("Probe successful !!\n");
	return 0;

err12:
	clk_disable(pck);
	clk_disable(tck);

	clk_put(pck);

err11:
	clk_put(tck);

err10:
	destroy_workqueue(sh_lcd_vsync.wq_vsync_off);
	sh_lcd_vsync.wq_vsync_off = NULL;

err9:
	device_remove_file(pdata->priv->ch[0].lcdc->dev, &dev_attr_vsync);

err8:
	for (i = 0; i < j; i++) {
		if (lcd_ext_param[i].panel_func.panel_remove)
			lcd_ext_param[i].panel_func.panel_remove(panel_data);

		info = panel_data->info[i];
		error = unregister_framebuffer(info);
	}

err7:
	for (i = 0; i < j; i++)
		fb_lcdc_deinit(pdev, i);

err6:
#if FB_SH_MOBILE_REFRESH
	destroy_workqueue(sh_mobile_wq);
	sh_mobile_wq = NULL;

err5:
#endif
	iounmap(irqc_baseaddr);
	irqc_baseaddr = NULL;

err4:
	free_irq(pdata->priv->irq, pdata->priv);
	pdata->priv->irq = 0;

err3:
	platform_set_drvdata(pdev, NULL);

	kfree(pdata->priv);
	pdata->priv = NULL;

err2:
	kfree(panel_data);
	panel_data = NULL;

err1:
	pdata = NULL;

err0:
	return error;
}

static int fb_lcdc_remove(struct platform_device *pdev)
{
	struct fb_lcdc_priv *priv = platform_get_drvdata(pdev);
	struct fb_info *info;
	int i;
	//++ add lcd sys by custom_t:sw
	lcd_panel_info_sysfs_deinit();
	//--
	destroy_workqueue(sh_lcd_vsync.wq_vsync_off);
#if FB_SH_MOBILE_REFRESH
	destroy_workqueue(sh_mobile_wq);
#endif

	for (i = 0; i < ARRAY_SIZE(priv->ch); i++)
		if (priv->ch[i].info->dev)
			unregister_framebuffer(priv->ch[i].info);

	for (i = 0; i < ARRAY_SIZE(priv->ch); i++) {
		info = priv->ch[i].info;

		if (!info || !info->device)
			continue;

		if (lcd_ext_param[i].panel_func.panel_remove) {
			lcd_ext_param[i].
				panel_func.panel_remove(panel_data);
		}


#ifdef CONFIG_FB_SH_MOBILE_DOUBLE_BUF
		if (panel_data->virt_addr != 0)
			iounmap((void __iomem *)panel_data->virt_addr);
#else
		if (info->screen_base != NULL)
			dma_free_coherent(&pdev->dev, info->fix.smem_len,
					  info->screen_base,
					  priv->ch[i].dma_handle);

		if (panel_data->virt_addr != 0)
			iounmap((void __iomem *)panel_data->virt_addr);
#endif
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}

	kthread_stop(sh_lcd_vsync.vsync_thread);

	if (priv->irq)
		free_irq(priv->irq, priv);

	kfree(priv);
	kfree(panel_data);
	return 0;
}


static void fb_lcdc_shutdown(struct platform_device *pdev)
{
	lcdc_shutdown = true;
	lcdc_suspend();
}

int setup_get_rloader_buff_add(char *addr)
{
	if (addr || strlen(addr)) {
		int ret;
		ret = kstrtoul(addr, 16, &rloader_buff_p);
		if (ret != 0)
			pr_err("kstrtoul returned ret=%d\n", ret);
	}
	pr_info("Assuming rloader logo buffer to be at 0x%x\n",
		(uint32_t)rloader_buff_p);
	return 0;
}
__setup("fb=", setup_get_rloader_buff_add);

static struct platform_driver sh_mobile_lcdc_driver = {
	.driver		= {
		.name		= "sh_mobile_lcdc_fb",
		.owner		= THIS_MODULE,
		.of_match_table = sh_mobile_lcdc_fb_of_match,
#if 0
		.pm		= &sh_mobile_lcdc_dev_pm_ops,
#endif
	},
	.probe		= fb_lcdc_probe,
	.remove		= fb_lcdc_remove,
	.shutdown   = fb_lcdc_shutdown,
};

module_platform_driver(sh_mobile_lcdc_driver);

MODULE_DESCRIPTION("SuperH Mobile LCDC Framebuffer driver");
MODULE_AUTHOR("Renesas Electronics");

	MODULE_LICENSE("GPL v2");
