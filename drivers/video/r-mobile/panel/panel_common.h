/*
 * drivers/video/r-mobile/panel/panel_common.h
 *
 * Copyright (C) 2013 Renesas Electronics Corporation
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
#ifndef __PANEL_COMMON_H__
#define __PANEL_COMMON_H__
#include <rtapi/screen_display.h>

#define MAX_PNAME_SIZE 20

#define MIPI_DSI_DCS_LONG_WRITE		(0x39)
#define MIPI_DSI_GEN_LONG_WRITE		(0x29)
#define MIPI_DSI_DCS_SHORT_WRITE_PARAM	(0x15)
#define MIPI_DSI_GEN_SHORT_WRITE_PARAM	(0x23)
#define MIPI_DSI_DCS_SHORT_WRITE	(0x05)
#define MIPI_DSI_GEN_SHORT_WRITE	(0x03)
#define MIPI_DSI_SET_MAX_RETURN_PACKET	(0x37)
#define MIPI_DSI_DCS_READ		(0x06)
#define MIPI_DSI_DELAY			(0x00)
#define MIPI_DSI_BLACK			(0x01)
#define MIPI_DSI_END			(0xFF)
#ifdef CONFIG_LDI_MDNIE_SUPPORT
#define MIPI_DSI_PANEL_MDNIE            (0x02)
#endif

#define LDO_3V 0
#define LDO_1V8 1
#define RDIDIF 0x4
#define RDID1 0xDA
#define RDID2 0xDB
#define RDID3 0xDC


enum PANEL_TYPE {
	PANEL_NONE,
	PANEL_NT35510,
	PANEL_VX5B3D,
	PANEL_NT35516,
	PANEL_HX8389B,
	PANEL_NT35510_HEAT,
	PANEL_S6E88A0,
	PANEL_NT35590,
	PANEL_HX8389B_TRULY,
	PANEL_HX8369B,
	PANEL_OTM8018B,
	PANEL_OTM9608A,
	PANEL_NT35512,
	PANEL_S6D78A0,
	/*custom_t peter  add lcd3 */
	PANEL_OTM8018B_BOE,
	PANEL_MAX,
};

enum lcdfreq_level_idx {
	LEVEL_NORMAL,           /* Default frequency */
	LEVEL_CHANGE,           /* Offset frequency  */
	LCDFREQ_LEVEL_END
};

struct lcd_info {
	enum lcdfreq_level_idx  level;  /* Current level */
	struct mutex            lock;   /* Lock for change frequency */
	struct device           *dev;   /* Hold device of LCD */
	struct device_attribute *attr;  /* Hold attribute info */
	struct lcd_device       *ld;    /* LCD device info */
	unsigned int                    ldi_enable;
	unsigned int                    power;
};

enum DISPCTRL_TYPE {
	DISPCTRL_LIST_END,
	DISPCTRL_WR_CMND,
	DISPCTRL_GEN_WR_CMND,
	DISPCTRL_WR_DATA,
	DISPCTRL_SLEEP_MS,
	DISPCTRL_DRAW_BLACK,
	DISPCTRL_PANEL_MDNIE
};

struct dispctrl_rec {
	enum DISPCTRL_TYPE type;
	unsigned char val;
};

struct esd_check_struct {
	struct dispctrl_rec *set0;
	struct dispctrl_rec *set1;
};

enum modes {
	LCD_CMD_ONLY,
	LCD_VID_ONLY,
	LCD_CMD_VID_BOTH,
};
enum video_burst_mode {
	LCD_VID_NON_BURST_SYNC_PULSE,
	LCD_VID_NON_BURST_SYNC_EVENT,
	LCD_VID_BURST,
	LCD_VID_NONE,
};

enum color_modes {
	RGB888,                /* bpp: 24	*/
	RGB565,                /* bpp: 16  */
	RGB666_LOOSELY_PACKED, /* bpp: 24  */
	RGB666,                /* bpp: 18	*/
	BGR888,                /* bpp: 24 */
	BGR565,                /* bpp: 16 */
	BGR666_LOOSELY_PACKED, /* bpp: 24 */
	BGR666                 /* bpp: 18 */
};

enum esd_check_types {
	ESD_DISABLED,
	ESD_GPIO,
	ESD_ID_SEQ,
	ESD_TE_GPIO,
};
#define PRECISION 100
#define PRECISION_FACT (PRECISION/10)

struct screen_disp_time_param {
	unsigned int hstrial_ns;
	unsigned int hstrial_ui;
	unsigned int hstrial_adj;
	unsigned int hsppvl_ns;
	unsigned int hsppvl_ui;
	unsigned int cktrvl_ns;
	unsigned int ckppvl_ns;
	unsigned int hszerovl_ns;
	unsigned int hszerovl_ui;
	unsigned int ckpostvl_ns;
	unsigned int ckpostvl_ui;
	unsigned int ckprevl_ns;
	unsigned int ckprevl_ui;
};

struct lcd_config {
	char                    *name; /* Panel name */
	enum modes		modes_supp; /* vid or cmd mode or both */
	unsigned short          max_lanes; /* max lanes supported */
	unsigned long           max_hs_bps; /* max hs bit rate */
	unsigned long           max_lp_bps; /* max lp bit rate */
	unsigned short          phy_width; /* Physical width of panel in mm */
	unsigned short          phy_height; /* Physical height of panel in mm */
	unsigned short		width; /* width in pixels */
	unsigned short		height; /* height in pixels */
	unsigned short          hbp; /* back porch */
	unsigned short          hsync; /* horizontal sync */
	unsigned short          hfp; /* horizontal front porch */
	unsigned short          vbp; /* vertial back proch */
	unsigned short          vsync; /* vertical sync */
	unsigned short          vfp; /* vertical front proch */
	unsigned short		pack_send_mode;
	unsigned short		vburst_mode; /* Burst or Non Burst Mode */
	unsigned int       color_mode; /* RGB or BGR format */
	unsigned int       cont_clock; /* stop or output HS clock */
	unsigned int       cmd_transmission; /* Send panel specific
					commands during operation */
	struct screen_disp_time_param *time_param;
	unsigned int		setup, pulse;
	struct dispctrl_rec	*init_seq_vid; /* init seq vid mode*/
	struct dispctrl_rec	*init_seq_cmd; /* init seq sequence cmd mode*/
	struct dispctrl_rec	*init_seq_min; /* initialization sequence */
	struct dispctrl_rec	*suspend_seq; /* command sequnce for suspend */
	struct dispctrl_rec	*panel_id;
	bool			verify_id;
};

struct fb_panel_func r_mobile_panel_func(int);


#endif /* __PANEL_COMMON_H__ */


