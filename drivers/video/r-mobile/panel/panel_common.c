/*
 * drivers/video/r-mobile/panel/panel_common.c
 *
 * Copyright (C) 2014 Broadcom Corporation
 * All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <rtapi/screen_display.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <mach/memory-r8a7373.h>
#include "panel_common.h"
#include "panel_info.h"
#include <rtapi/screen_display.h>
#include <video/sh_mobile_lcdc.h>
#include <rtapi/system_pwmng.h>
#include <linux/uaccess.h>
#include <linux/lcd.h>

#define INIT_RETRY_COUNT 3
#define SLEEP_RANGE 500
#define STABILIZE_TIME 120000
#define REGULATE_TIME 1000

/****************************************************
	Enable this macro to switch to offset frequency
	through sysfs.
****************************************************/
/*#define LCD_FREQUENCY_CONTROL 1 */

#if defined(LCD_FREQUENCY_CONTROL)
#include <mach/r8a7373.h>
#endif

#define ESD_CHECK_DISABLE 0
#define ESD_CHECK_ENABLE 1

/* Enable this macro to read the panel id from the
   debug fs interface */
/* #define DEBUG_PANEL_ID */

#ifdef DEBUG_PANEL_ID
#include <linux/debugfs.h>
#endif

#if defined(LCD_FREQUENCY_CONTROL)
static struct lcd_info lcd_freq_data;
#endif

static struct lcd_info lcd_info_data;
static char *esd_devname  = "panel_esd_irq";
static int panel_simple_reset(struct panel_info *);

enum {
	DISP_OFF,
	DISP_ON,
} DISP_PWR_CTRL;

static unsigned int irq_portno;
static struct fb_panel_info r_mobile_info;
struct panel_info *g_panel_data;
static int panel_resume(void *);
static int panel_suspend(void *);
static void disp_pwr_ctrl(struct panel_info *panel_data, bool on);
static int panel_draw(void *, struct panel_info *);
static void change_dsi_mode(struct panel_info *panel_data,
				void *screen_handle, int dsi_mode);
static void disp_gpio_reset(struct panel_info *panel_data);

#if defined(LCD_FREQUENCY_CONTROL)

static ssize_t level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	pr_info("DSI0PCKCR : 0x%0x DSI0PHYCR:0x%0x\n",
		readl(DSI0PCKCR), readl(DSI0PHYCR));
	return sprintf(buf, "%d\n", lcd_freq_data.level);
}

static ssize_t level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int value;
	int ret;
	int dual_freq_pckcr, dual_freq_phycr;
	struct timeval beforetime;
	struct timeval aftertime;
	ret = kstrtoul(buf, 0, (unsigned long *)&value);

	pr_debug("\t%s :: value=%d\n", __func__, value);

	if (value >= LCDFREQ_LEVEL_END) {
		count = -EINVAL;
		goto out;
	}
	dual_freq_pckcr = readl(DSI0PCKCR) & (0x3F<<16);
	dual_freq_phycr = readl(DSI0PHYCR) & (0x3F<<8);
	if ((dual_freq_pckcr != 0) && (dual_freq_phycr != 0)) {
		do_gettimeofday(&beforetime);
		if (value == 0) { /* set A */
			if ((readl(DSI0PCKCR) & 0x18000000) != 0)
				writel((readl(DSI0PCKCR) & ~0x18000000)
					| 0x20000000, DSI0PCKCR);
			else
				pr_info("All ready Set A\n");
			while ((readl(DSI0PCKCR) & 0x18000000) != 0)
				;
		} else	{ /*set B */
			if ((readl(DSI0PCKCR) & 0x18000000) == 0)
				writel((readl(DSI0PCKCR) & ~0x18000000)
					| 0x20000000, DSI0PCKCR);
			else
				pr_info("All ready Set B\n");
			while ((readl(DSI0PCKCR) & 0x18000000) == 0)
				;
		}
		do_gettimeofday(&aftertime);
		pr_info("CHANGE_TIME: %03lums\n",
			(aftertime.tv_usec-beforetime.tv_usec)/1000);
		pr_info("DSI0PCKCR : 0x%0x DSI0PHYCR:0x%0x\n",
			readl(DSI0PCKCR), readl(DSI0PHYCR));

		lcd_freq_data.level	= value;
		goto out;
	} else {
		pr_info("Level Unchanged : Dual frequency not configured\n");
	}

out:
	return count;
}
static DEVICE_ATTR(level, S_IRUGO|S_IWUSR, level_show, level_store);

static struct attribute *lcdfreq_attributes[] = {
	&dev_attr_level.attr,
	NULL,
};

static struct attribute_group lcdfreq_attr_group = {
	.name = "lcdfreq",
	.attrs = lcdfreq_attributes,
};
static int lcd_frequency_register(struct device *dev)
{
	struct lcd_info *lcdfreq = NULL;
	int ret;

	pr_debug("%s\n", __func__);

	lcdfreq = &lcd_freq_data;
	lcdfreq->dev = dev;
	lcdfreq->level = LEVEL_NORMAL;

	ret = sysfs_create_group(&lcdfreq->dev->kobj, &lcdfreq_attr_group);
	if (ret < 0) {
		pr_err("fail to add sysfs entries, %d\n", __LINE__);
		return ret;
	}

	pr_debug("%s is done\n", __func__);

	return 0;
}
#endif

static struct lcd_ops panel_lcd_ops = {
	.set_power = NULL,
	.get_power = NULL,
};

static int panel_simple_reset(struct panel_info *panel_data)
{
	void *screen_handle;
	screen_disp_stop_lcd disp_stop_lcd;
	screen_disp_start_lcd start_lcd;
	screen_disp_delete disp_delete;
	int ret;

	void *system_handle;
	system_pmg_param powarea_end_notify;
	system_pmg_param powarea_start_notify;
	system_pmg_delete pmg_delete;
	struct screen_disp_set_rt_standby set_rt_standby;

	system_handle = system_pwmng_new();

	pr_err("%s\n", __func__);
	atomic_set(&panel_data->dsi_read_ena, 0);

	screen_handle =  screen_display_new();

	/* RT Standby Mode */
	set_rt_standby.handle		= screen_handle;
	set_rt_standby.rt_standby	= DISABLE_RT_STANDBY;
	screen_display_set_rt_standby(&set_rt_standby);

	ret = panel_dsi_write(screen_handle, panel_data->suspend_seq,
						panel_data);
	if (ret) {
		pr_err("panel_dsi_write error %d\n", ret);
		goto out;
	}
	disp_stop_lcd.handle		= screen_handle;
	disp_stop_lcd.output_mode	= RT_DISPLAY_LCD1;
	ret = screen_display_stop_lcd(&disp_stop_lcd);
	if (ret != SMAP_LIB_DISPLAY_OK)
		pr_err("display_stop_lcd err!\n");
	/* End suspend sequence */

	/* Start suspend sequence */
	/* GPIO control */
	disp_pwr_ctrl(panel_data, DISP_OFF);

	/* Notifying the Beginning of Using Power Area */
	pr_info("End A4LC power area\n");
	powarea_end_notify.handle		= system_handle;
	powarea_end_notify.powerarea_name	= RT_PWMNG_POWERAREA_A4LC;
	ret = system_pwmng_powerarea_end_notify(&powarea_end_notify);
	if (ret != SMAP_LIB_PWMNG_OK) {
		pr_err("system_pwmng_powerarea_end_notify err!\n");
		goto out;
	}

	msleep(20);

	/* Notifying the Beginning of Using Power Area */
	pr_info("Start A4LC power area\n");
	powarea_start_notify.handle		= system_handle;
	powarea_start_notify.powerarea_name	= RT_PWMNG_POWERAREA_A4LC;
	ret = system_pwmng_powerarea_start_notify(&powarea_start_notify);
	if (ret != SMAP_LIB_PWMNG_OK) {
		pr_err("system_pwmng_powerarea_start_notify err!\n");
		goto out;
	}

	/* LCD panel reset */

	disp_pwr_ctrl(panel_data, DISP_ON);

	msleep(20);
	/* Start resume sequence */
	/* Start a display to LCD */
	start_lcd.handle	= screen_handle;
	start_lcd.output_mode	= RT_DISPLAY_LCD1;
	ret = screen_display_start_lcd(&start_lcd);
	if (ret != SMAP_LIB_DISPLAY_OK) {
		pr_err("disp_start_lcd err!\n");
		goto out;
	}

	change_dsi_mode(panel_data, screen_handle, DSI_FROMHS);

	disp_gpio_reset(panel_data);

	change_dsi_mode(panel_data, screen_handle, DSI_TOHS);

	atomic_set(&panel_data->dsi_read_ena, 1);
	/* Transmit DSI command peculiar to a panel */
	ret = panel_dsi_write(screen_handle, panel_data->init_seq,
								panel_data);
	if (ret != 0) {
		pr_err("panel_dsi_write err!\n");
		goto out;
	}
	/* End resume sequence */

	/* Return from a black screen */
	ret = panel_draw(screen_handle, panel_data);
	if (ret != 0) {
		pr_err("panel_draw error!\n");
		goto out;
	}

out:
	/* RT Standby Mode */
	set_rt_standby.handle		= screen_handle;
	set_rt_standby.rt_standby	= ENABLE_RT_STANDBY;
	screen_display_set_rt_standby(&set_rt_standby);

	disp_delete.handle = screen_handle;
	screen_display_delete(&disp_delete);

	pmg_delete.handle = system_handle;
	system_pwmng_delete(&pmg_delete);

	return ret;
}

static void lcd_esd_detect(struct work_struct *work)
{
	int retry = INIT_RETRY_COUNT;
	struct panel_info *panel_data = container_of(work,
					struct panel_info,
					esd_data.esd_detect_work);

	/* For the disable entering suspend */
	mutex_lock(&panel_data->esd_data.esd_check_mutex);

	pr_debug("[LCD] %s\n", __func__);

	/* esd recovery */
	while ((panel_simple_reset(panel_data)) &&
		(panel_data->esd_data.esd_check_flag == ESD_CHECK_ENABLE)) {
		if (retry <= 0) {
			void *screen_handle;
			screen_disp_stop_lcd disp_stop_lcd;
			screen_disp_delete disp_delete;
			int ret;

			screen_handle =  screen_display_new();

			disp_pwr_ctrl(panel_data, DISP_OFF);

			disp_stop_lcd.handle		= screen_handle;
			disp_stop_lcd.output_mode	= RT_DISPLAY_LCD1;
			ret = screen_display_stop_lcd(&disp_stop_lcd);
			if (ret != SMAP_LIB_DISPLAY_OK)
				pr_err("display_stop_lcd err!\n");

			disp_delete.handle = screen_handle;
			screen_display_delete(&disp_delete);

			panel_data->esd_data.esd_check_flag = ESD_CHECK_DISABLE;
			pr_err("retry count 0!\n");
			break;
		}
		retry--;
		msleep(20);
	}

	if (panel_data->esd_data.esd_check_flag == ESD_CHECK_ENABLE)
		enable_irq(panel_data->esd_data.esd_detect_irq);

	/* Enable suspend */
	mutex_unlock(&panel_data->esd_data.esd_check_mutex);
}

static irqreturn_t lcd_esd_irq_handler(int irq, void *dev_id)
{
	struct esd_data *p_esd = &g_panel_data->esd_data;

	if (dev_id == &p_esd->esd_irq_req) {
		pr_debug("[LCD] %s\n", __func__);

		if (ESD_TE_GPIO == p_esd->esd_check) {
			/* TE signal triggered */
			complete(&p_esd->esd_te_irq_done);
		} else {
			disable_irq_nosync(p_esd->esd_detect_irq);
			queue_work(p_esd->panel_esd_wq,
					&p_esd->esd_detect_work);
		}
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

void disp_pwr_ctrl(struct panel_info *panel_data, bool on)
{
	pr_info("%s\n", __func__);
	if (on) {
		/* power already supplied */
		if (panel_data->pwr_supp) {
			pr_err("Already power supply!\n");
			return;
		}
		if (regulator_enable(panel_data->reg[LDO_1V8].consumer))
			pr_err("Failed to enable regulator\n");
		usleep_range(REGULATE_TIME, REGULATE_TIME + SLEEP_RANGE);

		if (regulator_enable(panel_data->reg[LDO_3V].consumer))
			pr_err("Failed to enable regulator\n");
		usleep_range(REGULATE_TIME, REGULATE_TIME + SLEEP_RANGE);

		panel_data->pwr_supp = true;
	} else {

		/* power already not supplied */
		if (!panel_data->pwr_supp) {
			pr_err("Already not power supply!\n");
			return;
		}

		/* GPIO control */
		gpio_direction_output(panel_data->rst.rst_gpio_num, 0);

		usleep_range(REGULATE_TIME, REGULATE_TIME + SLEEP_RANGE);
		regulator_disable(panel_data->reg[LDO_3V].consumer);
		regulator_disable(panel_data->reg[LDO_1V8].consumer);
		usleep_range(REGULATE_TIME, REGULATE_TIME + SLEEP_RANGE);

		panel_data->pwr_supp = false;
	}
}
static void disp_gpio_reset(struct panel_info *panel_data)
{
	unsigned int pulse = panel_data->rst.pulse;
	unsigned int setup = panel_data->rst.setup;
	pr_info("%s\n", __func__);

	gpio_direction_output(panel_data->rst.rst_gpio_num, 1);
	usleep_range(setup, setup + SLEEP_RANGE);

	gpio_direction_output(panel_data->rst.rst_gpio_num, 0);
	usleep_range(pulse, pulse + SLEEP_RANGE);

	gpio_direction_output(panel_data->rst.rst_gpio_num, 1);
	usleep_range(STABILIZE_TIME, STABILIZE_TIME + SLEEP_RANGE);
}

static void change_dsi_mode(struct panel_info *panel_data,
			void *screen_handle, int dsi_mode)
{
	screen_disp_set_dsi_mode set_dsi_mode;

	if (panel_data->cont_clock == 1) {
		pr_info("%s\n", __func__);
		/* Set DSI mode (FROMHS,TOHS) */
		set_dsi_mode.handle			= screen_handle;
		set_dsi_mode.dsimode		= dsi_mode;
		screen_display_set_dsi_mode(&set_dsi_mode);
	}

}

int panel_dsi_read(int id, int reg, int len, char *buf,
				struct panel_info *panel_data)
{
	void *screen_handle;
	screen_disp_write_dsi_short write_dsi_s;
	screen_disp_read_dsi_short read_dsi_s;
	screen_disp_delete disp_delete;
	int ret = 0;

	pr_debug("%s\n", __func__);

	if (!g_panel_data)
		return -EAGAIN;

	/* Possibility that this function is called by external functions */
	if (!panel_data)
		panel_data = g_panel_data;

	if (!atomic_read(&panel_data->dsi_read_ena)) {
		pr_err("sequence error!\n");
		return -EINVAL;
	}

	if ((len <= 0) || (len > 60) || (buf == NULL)) {
		pr_err("argument error!\n");
		return -EINVAL;
	}

	screen_handle = screen_display_new();

	/* Set maximum return packet size */
	write_dsi_s.handle		= screen_handle;
	write_dsi_s.output_mode	= RT_DISPLAY_LCD1;
	write_dsi_s.data_id		= MIPI_DSI_SET_MAX_RETURN_PACKET;
	write_dsi_s.reg_address	= len;
	write_dsi_s.write_data		= 0x00;
	write_dsi_s.reception_mode	= RT_DISPLAY_RECEPTION_ON;
	ret = screen_display_write_dsi_short_packet(&write_dsi_s);
	if (ret != SMAP_LIB_DISPLAY_OK) {
		pr_err("disp_write_dsi_short err!\n");
		goto out;
	}

	/* DSI read */
	read_dsi_s.handle		= screen_handle;
	read_dsi_s.output_mode		= RT_DISPLAY_LCD1;
	read_dsi_s.data_id		= id;
	read_dsi_s.reg_address		= reg;
	read_dsi_s.write_data		= 0;
	read_dsi_s.data_count		= len;
	read_dsi_s.read_data		= &buf[0];
	ret = screen_display_read_dsi_short_packet(&read_dsi_s);
	if (ret != SMAP_LIB_DISPLAY_OK)
		pr_err("disp_dsi_read err! ret = %d\n", ret);

out:
	disp_delete.handle = screen_handle;
	screen_display_delete(&disp_delete);

	return ret;
}



static int panel_draw(void *screen_handle, struct panel_info *panel_data)
{
	screen_disp_draw disp_draw;
	int ret;

	pr_debug("%s\n", __func__);

	/* Memory clean */
	disp_draw.handle = screen_handle;
	disp_draw.output_mode = RT_DISPLAY_LCD1;
	disp_draw.draw_rect.x = 0;
	disp_draw.draw_rect.y = 0;
	disp_draw.draw_rect.width = panel_data->width;
	disp_draw.draw_rect.height = panel_data->height;
#ifdef CONFIG_FB_SH_MOBILE_RGB888
	disp_draw.format = RT_DISPLAY_FORMAT_RGB888;
#else
	disp_draw.format = RT_DISPLAY_FORMAT_ARGB8888;
#endif
	disp_draw.buffer_id = RT_DISPLAY_BUFFER_A;
	disp_draw.buffer_offset = 0;
	disp_draw.rotate = RT_DISPLAY_ROTATE_270;
	ret = screen_display_draw(&disp_draw);
	if (ret != SMAP_LIB_DISPLAY_OK) {
		pr_err("screen_display_draw err!\n");
		return -1;
	}

	return 0;
}

static int panel_draw_black(void *screen_handle,
				struct panel_info *panel_data, int clear_fb)
{
	u32 panel_width = panel_data->width;
	u32 panel_height = panel_data->height;
	screen_disp_draw disp_draw;
	int ret;

	pr_debug("%s\n", __func__);

	disp_draw.handle = screen_handle;
	if (clear_fb) {
		pr_debug("num_registered_fb = %d\n", num_registered_fb);
		if (!num_registered_fb) {
			pr_err("num_registered_fb err!\n");
			return -1;
		}

		if (!registered_fb[0]->fix.smem_start) {
			pr_err("registered_fb[0]->fix.smem_start is NULL err!\n");
			return -1;
		}

		pr_err("registerd_fb[0]-> fix.smem_start: %08x\n"
			"screen_base :%08x\n"
			"fix.smem_len :%08x\n",
			(unsigned)(registered_fb[0]->fix.smem_start),
			(unsigned)(registered_fb[0]->screen_base),
			(unsigned)(registered_fb[0]->fix.smem_len));

		/* Clean FB */
		memset(registered_fb[0]->screen_base, 0x0,
			registered_fb[0]->fix.smem_len);

		disp_draw.output_mode = RT_DISPLAY_LCD1;
		disp_draw.buffer_id  = RT_DISPLAY_BUFFER_A;
	}

	else {
		disp_draw.output_mode = RT_DISPLAY_LCD1_ASYNC;
		disp_draw.buffer_id  = RT_DISPLAY_DRAW_BLACK;
	}

	disp_draw.draw_rect.x = 0;
	disp_draw.draw_rect.y = 0;
	disp_draw.draw_rect.width = panel_width;
	disp_draw.draw_rect.height = panel_height;
#ifdef CONFIG_FB_SH_MOBILE_RGB888
	disp_draw.format = RT_DISPLAY_FORMAT_RGB888;
#else
	disp_draw.format = RT_DISPLAY_FORMAT_ARGB8888;
#endif
	disp_draw.buffer_offset = 0;
	disp_draw.rotate = RT_DISPLAY_ROTATE_270;
	ret = screen_display_draw(&disp_draw);
	if (ret != SMAP_LIB_DISPLAY_OK) {
		pr_err("screen_display_draw err!\n");
		return -1;
	}

	return 0;
}

int panel_dsi_write(void *lcd_handle,
				  unsigned char *cmdset,
				  struct panel_info  *panel_data)
{
	int ret;
	int i = 0;
	unsigned char size, type, *data;
	screen_disp_write_dsi_short write_dsi_s;
	screen_disp_write_dsi_long write_dsi_l;

	/* Incase this function is called by external functions*/
	if (!panel_data)
		panel_data = g_panel_data;

	pr_debug("%s\n", __func__);
	while (cmdset[i]) {
		size = cmdset[i++];
		type = cmdset[i++];
		data = &cmdset[i++];

		switch (type) {
		case MIPI_DSI_DCS_LONG_WRITE:
		case MIPI_DSI_GEN_LONG_WRITE:
			write_dsi_l.handle        = lcd_handle;
			write_dsi_l.output_mode   = RT_DISPLAY_LCD1;
			write_dsi_l.data_id       = type;
			write_dsi_l.data_count    = size;
			write_dsi_l.write_data    = data;
			write_dsi_l.reception_mode = RT_DISPLAY_RECEPTION_OFF;
			write_dsi_l.send_mode     = panel_data->pack_send_mode;
			ret = screen_display_write_dsi_long_packet(
					&write_dsi_l);
			if (ret != SMAP_LIB_DISPLAY_OK) {
				pr_err("display_write_dsi_long err %d!\n", ret);
				return -1;
			}
			break;
		case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		case MIPI_DSI_GEN_SHORT_WRITE_PARAM:
			write_dsi_s.handle        = lcd_handle;
			write_dsi_s.output_mode   = RT_DISPLAY_LCD1;
			write_dsi_s.data_id       = type;
			write_dsi_s.reg_address   = data[0];
			write_dsi_s.write_data    = data[1];
			write_dsi_s.reception_mode = RT_DISPLAY_RECEPTION_OFF;
			ret = screen_display_write_dsi_short_packet(
							&write_dsi_s);
			if (ret != SMAP_LIB_DISPLAY_OK) {
				pr_err("disp_write_dsi_short err %d!\n", ret);
				return -1;
			}
			break;
		case MIPI_DSI_DCS_SHORT_WRITE:
		case MIPI_DSI_GEN_SHORT_WRITE:
			write_dsi_s.handle        = lcd_handle;
			write_dsi_s.output_mode   = RT_DISPLAY_LCD1;
			write_dsi_s.data_id       = type;
			write_dsi_s.reg_address   = data[0];
			write_dsi_s.write_data    = 0x00;
			write_dsi_s.reception_mode = RT_DISPLAY_RECEPTION_OFF;
			ret = screen_display_write_dsi_short_packet(
					&write_dsi_s);
			if (ret != SMAP_LIB_DISPLAY_OK) {
				pr_err("disp_write_dsi_short err %d!\n", ret);
				return -1;
			}
			break;
		case MIPI_DSI_BLACK:
		{
			if (!panel_data->init_now)
				panel_draw(lcd_handle, panel_data);
			ret = panel_draw_black(lcd_handle, panel_data, 0);
			if (ret != 0)
				return -1;

			break;
		}
		case MIPI_DSI_DELAY:
			msleep(data[0]);
			break;
#ifdef CONFIG_LDI_MDNIE_SUPPORT
		case MIPI_DSI_PANEL_MDNIE:
			set_mdnie_enable();
			break;
#endif
		default:
			pr_err("Undefined command err!\n");
			return -1;
		}
		i += size - 1;
	}

	return 0;
}



static int panel_check(struct panel_info *panel_data)
{
	int ret = 0;
	int i, len, command;
	unsigned char read_data[20];
	char *parse = panel_data->panel_id;
	char *param;
	/* loop until size is 0 */
	len = parse[0];
	while (len != 0) {
		command = parse[2];
		param = &parse[3];
		ret = panel_dsi_read(MIPI_DSI_DCS_READ, command, len - 1,
					&read_data[0], panel_data);
		for (i = 0; i < len - 1; ++i) {
			if (read_data[i] != param[i]) {
				pr_err("ESD Detected !\n");
				ret = -1;
				goto out;
			}
		}
		parse = parse + len + 2; /*+2 for size and type */
		len = parse[0];
	}
out:
	return ret;
}

static void panel_esd_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct panel_info *panel_data = container_of(dwork,
					struct panel_info,
					esd_data.esd_check_work);
	struct esd_data *p_esd = &panel_data->esd_data;
	int esd_result = -EIO;

	mutex_lock(&p_esd->esd_check_mutex);

	if (ESD_TE_GPIO == p_esd->esd_check) {
		unsigned long jiff_in = 0;

		enable_irq(p_esd->esd_detect_irq);
		usleep_range(1000, 1001);
		/* mark as incomplete before waiting the esd signal */
		INIT_COMPLETION(p_esd->esd_te_irq_done);
		jiff_in = wait_for_completion_timeout(
				&p_esd->esd_te_irq_done,
				msecs_to_jiffies(200));
		if (!jiff_in) {
			pr_err("Wait esd gpio irq timeout\n");
			esd_result = -EIO;
		} else
			esd_result = 0;
		disable_irq(p_esd->esd_detect_irq);
	} else
		esd_result = panel_check(panel_data);

	if (esd_result) {
		/* ESD fail deteced */
		if (panel_simple_reset(panel_data))
			pr_err("Error in resetting the Panel!\n");
	}

	if (p_esd->esd_check_flag == ESD_CHECK_ENABLE)
		queue_delayed_work(p_esd->panel_esd_wq,
			dwork,
			msecs_to_jiffies(p_esd->esd_dur));

	mutex_unlock(&p_esd->esd_check_mutex);
}

int panel_init(void *data, unsigned int mem_size)
{
	void *screen_handle;
	struct panel_info *panel_data = (struct panel_info  *) data;
	screen_disp_start_lcd start_lcd;
	screen_disp_stop_lcd disp_stop_lcd;
	screen_disp_set_lcd_if_param set_lcd_if_param;
	screen_disp_set_address set_address;
	screen_disp_delete disp_delete;
	int ret = 0;
	int retry_count = INIT_RETRY_COUNT;

	void *system_handle;
	system_pmg_param powarea_start_notify;
	system_pmg_delete pmg_delete;
	struct screen_disp_set_rt_standby set_rt_standby;

	pr_info("%s\n", __func__);

	panel_data->init_now = true;

	pr_info("Start A4LC power area\n");
	system_handle = system_pwmng_new();

	/* Notifying the Beginning of Using Power Area */
	powarea_start_notify.handle		= system_handle;
	powarea_start_notify.powerarea_name	= RT_PWMNG_POWERAREA_A4LC;
	ret = system_pwmng_powerarea_start_notify(&powarea_start_notify);
	if (ret != SMAP_LIB_PWMNG_OK)
		pr_err("system_pwmng_powerarea_start_notify err!\n");
	pmg_delete.handle = system_handle;
	system_pwmng_delete(&pmg_delete);


	screen_handle = screen_display_new();

	/* RT Standby Mode (DISABLE_RT_STANDBY, ENABLE_RT_STANDBY) */
	set_rt_standby.handle	= screen_handle;
	set_rt_standby.rt_standby	= DISABLE_RT_STANDBY;
	screen_display_set_rt_standby(&set_rt_standby);

	set_lcd_if_param.handle			= screen_handle;
	set_lcd_if_param.port_no		= irq_portno;
	set_lcd_if_param.lcd_if_param		= &panel_data->lcd_if_param;
	set_lcd_if_param.lcd_if_param_mask	=
						&panel_data->lcd_if_param_mask;

	ret = screen_display_set_lcd_if_parameters(&set_lcd_if_param);
	if (ret != SMAP_LIB_DISPLAY_OK) {
		pr_err("disp_set_lcd_if_parameters err!\n");
		goto out;
	}

	/* Setting FB address */
	set_address.handle	= screen_handle;
	set_address.output_mode	= RT_DISPLAY_LCD1;
	set_address.buffer_id	= RT_DISPLAY_BUFFER_A;
	set_address.address	= panel_data->fb_start;
	set_address.size	= panel_data->fb_size;

	ret = screen_display_set_address(&set_address);
	if (ret != SMAP_LIB_DISPLAY_OK) {
		pr_err("disp_set_address err!\n");
		goto out;
	}

	/* Start a display to LCD */
	start_lcd.handle	= screen_handle;
	start_lcd.output_mode	= RT_DISPLAY_LCD1;

	ret = screen_display_start_lcd(&start_lcd);
	if (ret != SMAP_LIB_DISPLAY_OK) {
		pr_err("disp_start_lcd err!\n");
		goto out;
	}

retry:
	atomic_set(&panel_data->dsi_read_ena, 1);

	/* TODO: make it generic and should be taken care in
	 * upcoming pathes */
	if (strcmp(panel_data->name, "S6E88A0") == 0)
		ret = s6e88a0_backlightinit(panel_data);

	if (!panel_data->is_disp_boot_init) {
		if (panel_data->init_seq_min &&
				retry_count == INIT_RETRY_COUNT)
			ret = panel_dsi_write(screen_handle,
						panel_data->init_seq_min,
						panel_data);
		else
			ret = panel_dsi_write(screen_handle,
						panel_data->init_seq,
						panel_data);
	}
	panel_draw_black(screen_handle, panel_data, 1);

	if (ret != 0) {
		pr_err("panel_dsi_write err!\n");
		atomic_set(&panel_data->dsi_read_ena, 0);

		if (retry_count == 0) {
			pr_err("retry count 0!\n");

			disp_pwr_ctrl(panel_data, DISP_OFF);

			disp_stop_lcd.handle		= screen_handle;
			disp_stop_lcd.output_mode	= RT_DISPLAY_LCD1;
			ret = screen_display_stop_lcd(&disp_stop_lcd);
			if (ret != SMAP_LIB_DISPLAY_OK)
				pr_err("display_stop_lcd err!\n");

			ret = -ENODEV;
			goto out;
		} else {
			retry_count--;

			disp_pwr_ctrl(panel_data, DISP_OFF);

			/* Stop a display to LCD */
			disp_stop_lcd.handle		= screen_handle;
			disp_stop_lcd.output_mode	= RT_DISPLAY_LCD1;
			ret = screen_display_stop_lcd(&disp_stop_lcd);
			if (ret != SMAP_LIB_DISPLAY_OK) {
				pr_err("display_stop_lcd err!\n");
				goto out;
			}

			disp_delete.handle = screen_handle;
			screen_display_delete(&disp_delete);
			screen_handle = screen_display_new();

			disp_pwr_ctrl(panel_data, DISP_ON);

			/* Start a display to LCD */
			start_lcd.handle	= screen_handle;
			start_lcd.output_mode	= RT_DISPLAY_LCD1;

			ret = screen_display_start_lcd(&start_lcd);
			if (ret != SMAP_LIB_DISPLAY_OK) {
				pr_err("disp_start_lcd err!\n");
				goto out;
			}

			change_dsi_mode(panel_data, screen_handle, DSI_FROMHS);

			disp_gpio_reset(panel_data);

			change_dsi_mode(panel_data, screen_handle, DSI_TOHS);

			goto retry;
		}
	}

	if (panel_data->lcd_if_param.dtctr & 0x01)
		pr_err("Panel initialized in video mode\n");
	else
		pr_err("Panel initialized in Command mode\n");

	if (panel_data->esd_data.esd_check != ESD_DISABLED) {
		struct esd_data *p_esd = &panel_data->esd_data;
		p_esd->esd_check_flag = ESD_CHECK_ENABLE;
		pr_info("Enable ESD type %d\n", p_esd->esd_check);

		if (p_esd->esd_check == ESD_ID_SEQ ||
				p_esd->esd_check == ESD_TE_GPIO) {
			queue_delayed_work(p_esd->panel_esd_wq,
				&p_esd->esd_check_work,
				msecs_to_jiffies(p_esd->esd_dur));
		}
		if (p_esd->esd_check == ESD_GPIO ||
				p_esd->esd_check == ESD_TE_GPIO) {
			unsigned long flags;
			if (p_esd->esd_check == ESD_GPIO)
				flags = IRQF_ONESHOT | IRQF_TRIGGER_HIGH;
			else
				flags = IRQF_TRIGGER_FALLING;
			ret = request_irq(p_esd->esd_detect_irq,
					lcd_esd_irq_handler,
					flags,
					esd_devname,
					&p_esd->esd_irq_req);
			if (ret != 0)
				pr_err("request_irq err! =%d\n", ret);
			else {
				if (p_esd->esd_check == ESD_TE_GPIO)
					disable_irq(p_esd->esd_detect_irq);
				p_esd->esd_irq_req = true;
			}
		} else
			pr_err("Invalid ESD check request !\n");
	}

out:
	/* RT Standby Mode */
	set_rt_standby.handle		= screen_handle;
	set_rt_standby.rt_standby	= ENABLE_RT_STANDBY;
	screen_display_set_rt_standby(&set_rt_standby);

	disp_delete.handle = screen_handle;
	screen_display_delete(&disp_delete);

	panel_data->init_now = false;
	return ret;
}



int panel_suspend(void *data)
{
	void *screen_handle;
	struct panel_info *panel_data = (struct panel_info *) data;
	screen_disp_stop_lcd disp_stop_lcd;
	screen_disp_delete disp_delete;
	int ret = 0;

	void *system_handle;
	system_pmg_param powarea_end_notify;
	system_pmg_delete pmg_delete;

	if (panel_data->esd_data.esd_check != ESD_DISABLED) {
		panel_data->esd_data.esd_check_flag = ESD_CHECK_DISABLE;
		mutex_lock(&panel_data->esd_data.esd_check_mutex);

		if (panel_data->esd_data.esd_check == ESD_ID_SEQ ||
			panel_data->esd_data.esd_check == ESD_TE_GPIO) {
			cancel_delayed_work_sync
				(&panel_data->esd_data.esd_check_work);
		}
		if (panel_data->esd_data.esd_check == ESD_GPIO ||
			panel_data->esd_data.esd_check == ESD_TE_GPIO) {
			if (panel_data->esd_data.esd_irq_req)
				free_irq(panel_data->esd_data.esd_detect_irq,
					&panel_data->esd_data.esd_irq_req);

			panel_data->esd_data.esd_irq_req = false;
		}
	}


	pr_info("%s\n", __func__);

	screen_handle = screen_display_new();

	atomic_set(&panel_data->dsi_read_ena, 0);


	/* TODO: make it generic and should be taken care in
	 * upcoming pathes */
	if (strcmp(panel_data->name, "S6E88A0") == 0)
		s6e88a0_bklt_suspend();
	else
		ret = panel_dsi_write(screen_handle, panel_data->suspend_seq,
								panel_data);
	if (ret != 0) {
		pr_err("panel_dsi_write err!\n");
		/* continue */
	}

	/* Stop a display to LCD */
	disp_stop_lcd.handle		= screen_handle;
	disp_stop_lcd.output_mode	= RT_DISPLAY_LCD1;
	ret = screen_display_stop_lcd(&disp_stop_lcd);
	if (ret != SMAP_LIB_DISPLAY_OK)
		pr_err("display_stop_lcd err!\n");

	disp_pwr_ctrl(panel_data, DISP_OFF);

	disp_delete.handle = screen_handle;
	screen_display_delete(&disp_delete);


	pr_debug("End A4LC power area\n");
	system_handle = system_pwmng_new();

	/* Notifying the Beginning of Using Power Area */
	powarea_end_notify.handle		= system_handle;
	powarea_end_notify.powerarea_name	= RT_PWMNG_POWERAREA_A4LC;
	ret = system_pwmng_powerarea_end_notify(&powarea_end_notify);
	if (ret != SMAP_LIB_PWMNG_OK)
		pr_err("system_pwmng_powerarea_end_notify err!\n");

	pmg_delete.handle = system_handle;
	system_pwmng_delete(&pmg_delete);

	if (panel_data->esd_data.esd_check)
		mutex_unlock(&panel_data->esd_data.esd_check_mutex);
	return 0;
}

int panel_resume(void *data)
{
	void *screen_handle;
	struct panel_info *panel_data = (struct panel_info *) data;
	screen_disp_start_lcd start_lcd;
	screen_disp_stop_lcd disp_stop_lcd;
	screen_disp_delete disp_delete;
	unsigned char read_data[60];
	int retry_count_dsi;
	int ret = 0;
	int retry_count = INIT_RETRY_COUNT;
	struct screen_disp_set_rt_standby set_rt_standby;

	void *system_handle;
	system_pmg_param powarea_start_notify;
	system_pmg_delete pmg_delete;


	pr_info("%s\n", __func__);

	if (panel_data->esd_data.esd_check != ESD_DISABLED)
		mutex_lock(&panel_data->esd_data.esd_check_mutex);

	pr_debug("Start A4LC power area\n");
	system_handle = system_pwmng_new();

	/* Notifying the Beginning of Using Power Area */
	powarea_start_notify.handle		= system_handle;
	powarea_start_notify.powerarea_name	= RT_PWMNG_POWERAREA_A4LC;
	ret = system_pwmng_powerarea_start_notify(&powarea_start_notify);
	if (ret != SMAP_LIB_PWMNG_OK)
		pr_err("system_pwmng_powerarea_start_notify err!\n");
	pmg_delete.handle = system_handle;
	system_pwmng_delete(&pmg_delete);

	screen_handle = screen_display_new();

	/* RT Standby Mode (DISABLE_RT_STANDBY, ENABLE_RT_STANDBY) */
	set_rt_standby.handle	= screen_handle;
	set_rt_standby.rt_standby	= DISABLE_RT_STANDBY;
	screen_display_set_rt_standby(&set_rt_standby);

retry:

	disp_pwr_ctrl(panel_data, DISP_ON);

	/* TODO: set_dsi_mode not working properly
	in case of HD panel. Fix this in
	upcoming pathes */

	 if (strcmp(panel_data->name, "NT35590") == 0)
		disp_gpio_reset(panel_data);

	/* Start a display to LCD */
	start_lcd.handle	= screen_handle;
	start_lcd.output_mode	= RT_DISPLAY_LCD1;
	ret = screen_display_start_lcd(&start_lcd);
	if (ret != SMAP_LIB_DISPLAY_OK) {
		pr_err("disp_start_lcd err!\n");
		goto out;
	}

	if (strcmp(panel_data->name, "NT35590") != 0) {
		change_dsi_mode(panel_data, screen_handle, DSI_FROMHS);
		disp_gpio_reset(panel_data);
		change_dsi_mode(panel_data, screen_handle, DSI_TOHS);
	}

	atomic_set(&panel_data->dsi_read_ena, 1);


	retry_count_dsi = INIT_RETRY_COUNT;


	if (panel_data->panel_id && panel_data->verify_id) {
		do {
			ret = panel_dsi_read(MIPI_DSI_DCS_READ, 0x04, 4,
					&read_data[0], panel_data);
			if (ret == 0) {
				pr_err("read_data(RDID0) = %02X\n",
								read_data[0]);
				pr_err("read_data(RDID1) = %02X\n",
								read_data[1]);
				pr_err("read_data(RDID2) = %02X\n",
								read_data[2]);
			}

			retry_count_dsi--;

			if (retry_count_dsi == 0) {
				pr_err("Diff LCD ID or DSI read problem\n");
				break;
			}
		} while (read_data[0] != panel_data->panel_id[3]
			|| read_data[1] != panel_data->panel_id[7]
			|| read_data[2] != panel_data->panel_id[11]);
	}

	/* TODO: make it generic and should be taken care in
	 * upcoming pathes */
	if (strcmp(panel_data->name, "S6E88A0") == 0)
		s6e88a0_bklt_resume();
	else
		ret = panel_dsi_write(screen_handle, panel_data->init_seq,
							panel_data);

	if (ret != 0) {
		pr_err("panel_dsi_write err!\n");
		atomic_set(&panel_data->dsi_read_ena, 0);
		if (retry_count == 0) {
			pr_err("retry count 0!\n");

			disp_pwr_ctrl(panel_data, DISP_OFF);

			disp_stop_lcd.handle		= screen_handle;
			disp_stop_lcd.output_mode	= RT_DISPLAY_LCD1;
			ret = screen_display_stop_lcd(&disp_stop_lcd);
			if (ret != SMAP_LIB_DISPLAY_OK)
				pr_err("display_stop_lcd err!\n");

			ret = -ENODEV;
			goto out;
		} else {
			retry_count--;

			disp_pwr_ctrl(panel_data, DISP_OFF);

			/* Stop a display to LCD */
			disp_stop_lcd.handle		= screen_handle;
			disp_stop_lcd.output_mode	= RT_DISPLAY_LCD1;
			ret = screen_display_stop_lcd(&disp_stop_lcd);
			if (ret != SMAP_LIB_DISPLAY_OK)
				pr_err("display_stop_lcd err!\n");

			goto retry;
		}
	}

	if (panel_data->esd_data.esd_check != ESD_DISABLED) {
		struct esd_data *p_esd = &panel_data->esd_data;
		/* Wait for end of check ESD */
		p_esd->esd_check_flag = ESD_CHECK_ENABLE;
		pr_info("Enable ESD  type %d\n", p_esd->esd_check);

		if (p_esd->esd_check == ESD_ID_SEQ ||
			p_esd->esd_check == ESD_TE_GPIO) {
			queue_delayed_work(p_esd->panel_esd_wq,
				&p_esd->esd_check_work,
				msecs_to_jiffies(p_esd->esd_dur));
		}
		if (p_esd->esd_check == ESD_GPIO ||
			p_esd->esd_check == ESD_TE_GPIO) {
			unsigned long flags;
			if (p_esd->esd_check == ESD_GPIO)
				flags = IRQF_ONESHOT | IRQF_TRIGGER_HIGH;
			else
				flags = IRQF_TRIGGER_FALLING;
			ret = request_irq(p_esd->esd_detect_irq,
					lcd_esd_irq_handler,
					flags,
					esd_devname,
					&p_esd->esd_irq_req);
			if (ret != 0)
				pr_err("request_irq err! =%d\n", ret);
			else {
				if (p_esd->esd_check == ESD_TE_GPIO)
					disable_irq(p_esd->esd_detect_irq);
				p_esd->esd_irq_req = true;
			}
		}
	}

	if (panel_data->esd_data.esd_check)
		mutex_unlock(&panel_data->esd_data.esd_check_mutex);
out:
	/* RT Standby Mode */
	set_rt_standby.handle		= screen_handle;
	set_rt_standby.rt_standby	= ENABLE_RT_STANDBY;
	screen_display_set_rt_standby(&set_rt_standby);

	disp_delete.handle = screen_handle;
	screen_display_delete(&disp_delete);

	return ret;
}

#ifdef DEBUG_PANEL_ID
static struct dentry *read_id_fs;

static ssize_t panel_read_id_func(struct file *filp, char __user *u_buffer,
			size_t count, loff_t *pos)
{
	static int read_end;
	int ret = 0;
	int i, len, command;
	unsigned char read_data[20];
	struct panel_info *panel_data = filp->f_inode->i_private;
	char *parse = panel_data->panel_id;
	char *param;
	unsigned char buffer[50] = {0}, temp[7] = {0};

	if (read_end) {
		read_end = 0;
		return 0;
	}

	while (parse[0] != 0) {
		len = parse[0];
		command = parse[2];
		param = &parse[3];
		ret = panel_dsi_read(MIPI_DSI_DCS_READ, command, len - 1,
					&read_data[0], panel_data);
		for (i = 0; i < len - 1; i++) {
			sprintf(temp, "%x, ", read_data[i]);
			strcat(buffer, temp);
		}
		parse = parse + len + 2; /*+2 for size and type */
	}

	strcat(buffer, "\n");
	if (copy_to_user(u_buffer, buffer, strlen(buffer)))
		return -EFAULT;

	read_end = 1;
	return strlen(buffer);
}

static const struct file_operations debug_fops = {
	.owner = THIS_MODULE,
	.read = panel_read_id_func,
};
#endif

int panel_probe(struct fb_info *info, void *data)
{
	struct platform_device *pdev;
	struct resource *res_irq_port;
	struct panel_info *panel_data = (struct panel_info  *) data;

	int ret;
	pr_info("%s\n", __func__);

	g_panel_data = panel_data;

	/* GPIO control */
	gpio_request(panel_data->rst.rst_gpio_num , NULL);
	/* fb parent device info to platform_device */
	pdev = to_platform_device(info->device);

	/* get resource info from platform_device */
	if (pdev->dev.of_node) {
		irq_portno =    panel_data->detect.detect_gpio;
	} else {
		res_irq_port	= platform_get_resource_byname(pdev,
							IORESOURCE_IRQ,
							"panel_irq_port");
		if (!res_irq_port) {
			pr_err("panel_irq_port is NULL!\n");
			return -ENODEV;
		}
		irq_portno = res_irq_port->start;
	}
	panel_data->reg[LDO_3V].consumer = regulator_get(NULL,
					panel_data->reg[LDO_3V].supply);

	panel_data->reg[LDO_1V8].consumer = regulator_get(NULL,
					panel_data->reg[LDO_1V8].supply);


	if (panel_data->reg[LDO_3V].consumer == NULL ||
		panel_data->reg[LDO_1V8].consumer == NULL) {
		pr_err("regulator_get failed\n");
		return -ENODEV;
	}

	ret = regulator_enable(panel_data->reg[LDO_1V8].consumer);
	if (ret) {
		pr_err("regulator_enable failed\n");
		return -ENODEV;
	}
	ret = regulator_enable(panel_data->reg[LDO_3V].consumer);
	if (ret) {
		pr_err("regulator_enable failed\n");
		return -ENODEV;
	}
	panel_data->pwr_supp = true;

	atomic_set(&panel_data->dsi_read_ena, 0);

	/* TODO: make it generic and should be taken care in
	 * upcoming pathes */
	if (strcmp(panel_data->name, "S6E88A0") == 0)
		s6e88a0_bklt_probe(panel_data);

	if (panel_data->esd_data.esd_check != ESD_DISABLED) {
		struct esd_data *p_esd = &panel_data->esd_data;

		p_esd->esd_check_flag = ESD_CHECK_DISABLE;
		mutex_init(&p_esd->esd_check_mutex);
		p_esd->panel_esd_wq = create_workqueue("panel_esd_wq");

		if (p_esd->esd_check == ESD_ID_SEQ ||
			p_esd->esd_check == ESD_TE_GPIO) {
			INIT_DELAYED_WORK(&p_esd->esd_check_work,
						panel_esd_check_work);
		}
		if (p_esd->esd_check == ESD_GPIO ||
			p_esd->esd_check == ESD_TE_GPIO) {
			/* get resource info from platform_device */
			if (!pdev->dev.of_node) {
				res_irq_port =
					platform_get_resource_byname(pdev,
							IORESOURCE_IRQ,
							"panel_esd_irq_port");
				if (!res_irq_port) {
					pr_err("panel_esd_irq_port is NULL!\n");
					return -ENODEV;
				}
				p_esd->esd_irq_portno = res_irq_port->start;
			}

			/* GPIO control */
			gpio_request(p_esd->esd_irq_portno, NULL);
			gpio_direction_input(p_esd->esd_irq_portno);

			INIT_WORK(&p_esd->esd_detect_work, lcd_esd_detect);
			p_esd->esd_detect_irq =
				gpio_to_irq(p_esd->esd_irq_portno);
			init_completion(&p_esd->esd_te_irq_done);
		}
	}
#ifdef DEBUG_PANEL_ID
	read_id_fs = debugfs_create_file("read_panel_id", 0644,
					NULL, panel_data, &debug_fops);
	if (read_id_fs == NULL)
		pr_err("Unable to create debugfs file !\n");
#endif
	/* clear internal info */
	memset(&lcd_info_data, 0, sizeof(lcd_info_data));

	/* register sysfs for LCD */
	lcd_info_data.ld = lcd_device_register("panel",
						&pdev->dev,
						&lcd_info_data,
						&panel_lcd_ops);
	if (IS_ERR(lcd_info_data.ld)) {
		pr_err("lcd_device_register failed\n");
		return PTR_ERR(lcd_info_data.ld);
	}
#if defined(LCD_FREQUENCY_CONTROL)
	/* register sysfs for LCD frequency control */
	ret = lcd_frequency_register(info->device);
#endif
	lcd_info_data.power = FB_BLANK_UNBLANK;
	return 0;
}

struct fb_panel_info panel_info(void *data)
{
	return r_mobile_info;
}

int panel_remove(void *data)
{
	struct panel_info *panel_data = (struct panel_info *) data;
	pr_info("%s\n", __func__);
	if (strcmp(panel_data->name, "S6E88A0") == 0)
		s6e88a0_bklt_remove();

	if (panel_data->esd_data.esd_check != ESD_DISABLED) {
		panel_data->esd_data.esd_check_flag = ESD_CHECK_DISABLE;
		if (panel_data->esd_data.esd_check == ESD_ID_SEQ ||
			panel_data->esd_data.esd_check == ESD_TE_GPIO) {
			cancel_delayed_work_sync(
					&panel_data->esd_data.esd_check_work);
		}
		if (panel_data->esd_data.esd_check == ESD_GPIO ||
			panel_data->esd_data.esd_check == ESD_TE_GPIO) {
			free_irq(panel_data->esd_data.esd_detect_irq,
					&panel_data->esd_data.esd_irq_req);
			gpio_free(panel_data->esd_data.esd_irq_portno);
		}
		mutex_destroy(&panel_data->esd_data.esd_check_mutex);
	}
	gpio_free(panel_data->rst.rst_gpio_num);
	/* unregister sysfs for LCD */
	lcd_device_unregister(lcd_info_data.ld);

	return 0;
}

struct fb_panel_func r_mobile_panel_func(int panel)
{

	struct fb_panel_func panel_func;

	pr_info("%s\n", __func__);

	memset(&panel_func, 0, sizeof(struct fb_panel_func));

/* e.g. support (panel=RT_DISPLAY_LCD1) */

	if (panel == RT_DISPLAY_LCD1) {
		panel_func.panel_init   = panel_init;
		panel_func.panel_probe  = panel_probe;
		panel_func.panel_remove = panel_remove;
		panel_func.panel_info   = panel_info;
		panel_func.panel_suspend = panel_suspend;
		panel_func.panel_resume = panel_resume;
	}

	return panel_func;
}

