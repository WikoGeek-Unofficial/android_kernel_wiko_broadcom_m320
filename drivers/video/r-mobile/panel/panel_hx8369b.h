/*
 * drivers/video/broadcom/panel/panel_hx8369b.h
 *
 * Copyright (C) 2014 Broadcom Corporation
 * All rights reserved.
 *
 */
#include "panel_common.h"

#define HX8369B_HS_TRAIL_NS 60
#define HX8369B_HS_TRAIL_UI 4
#define HX8369B_HS_TRAIL_ADJUST (25*PRECISION_FACT)
#define HX8369B_HSPPVL_NS 40
#define HX8369B_HSPPVL_UI 4
#define HX8369B_CKTRVL_NS 60
#define HX8369B_CKPPVL_NS 38
#define HX8369B_HSZEROVL_NS 145
#define HX8369B_HSZEROVL_UI 10
#define HX8369B_CKPOSTVL_NS 60
#define HX8369B_CKPOSTVL_UI 52
#define HX8369B_CKPREVL_NS 1
#define HX8369B_CKPREVL_UI 8

static struct screen_disp_time_param hx8369b_time_param __initdata = {
	.hstrial_ns = HX8369B_HS_TRAIL_NS,
	.hstrial_ui = HX8369B_HS_TRAIL_UI,
	.hstrial_adj = HX8369B_HS_TRAIL_ADJUST,
	.hsppvl_ns = HX8369B_HSPPVL_NS,
	.hsppvl_ui = HX8369B_HSPPVL_UI,
	.cktrvl_ns = HX8369B_CKTRVL_NS,
	.ckppvl_ns = HX8369B_CKPPVL_NS,
	.hszerovl_ns = HX8369B_HSZEROVL_NS,
	.hszerovl_ui = HX8369B_HSZEROVL_UI,
	.ckpostvl_ns = HX8369B_CKPOSTVL_NS,
	.ckpostvl_ui = HX8369B_CKPOSTVL_UI,
	.ckprevl_ns = HX8369B_CKPREVL_NS,
	.ckprevl_ui = HX8369B_CKPREVL_UI,
};

static struct dispctrl_rec hx8369b_initialize_cmd[] __initdata = {
	{DISPCTRL_WR_CMND, 0xB9},
	{DISPCTRL_WR_DATA, 0xFF},
	{DISPCTRL_WR_DATA, 0x83},
	{DISPCTRL_WR_DATA, 0x69},

	{DISPCTRL_WR_CMND, 0xB1},
	{DISPCTRL_WR_DATA, 0x0B},
	{DISPCTRL_WR_DATA, 0x83},
	{DISPCTRL_WR_DATA, 0x77},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x11},
	{DISPCTRL_WR_DATA, 0x11},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x0C},
	{DISPCTRL_WR_DATA, 0x22},

	{DISPCTRL_WR_CMND, 0xC6},
	{DISPCTRL_WR_DATA, 0x41},
	{DISPCTRL_WR_DATA, 0xFF},
	{DISPCTRL_WR_DATA, 0x7A},
	{DISPCTRL_WR_DATA, 0xFF},

	{DISPCTRL_WR_CMND, 0xE3},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0xC0},
	{DISPCTRL_WR_DATA, 0x73},
	{DISPCTRL_WR_DATA, 0x50},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x34},
	{DISPCTRL_WR_DATA, 0xC4},
	{DISPCTRL_WR_DATA, 0x00},
	/*Powersetting End*/
	{DISPCTRL_SLEEP_MS, 10},
	/* Initializing Start */

	{DISPCTRL_WR_CMND, 0xBA},
	{DISPCTRL_WR_DATA, 0x31},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x16},
	{DISPCTRL_WR_DATA, 0xCA},
	{DISPCTRL_WR_DATA, 0xB0},
	{DISPCTRL_WR_DATA, 0x0A},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x28},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x21},
	{DISPCTRL_WR_DATA, 0x21},
	{DISPCTRL_WR_DATA, 0x9A},
	{DISPCTRL_WR_DATA, 0x1A},
	{DISPCTRL_WR_DATA, 0x8F},

	{DISPCTRL_WR_CMND, 0x3A},
	{DISPCTRL_WR_DATA, 0x70},

	{DISPCTRL_WR_CMND, 0xB3},
	{DISPCTRL_WR_DATA, 0x83},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x31},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x13},
	{DISPCTRL_WR_DATA, 0x06},

	{DISPCTRL_WR_CMND, 0xB4},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0xCC},
	{DISPCTRL_WR_DATA, 0x0C},

	{DISPCTRL_WR_CMND, 0xEA},
	{DISPCTRL_WR_DATA, 0x72},

	{DISPCTRL_WR_CMND, 0xB2},
	{DISPCTRL_WR_DATA, 0x03},

	{DISPCTRL_WR_CMND, 0xD5},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0D},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x12},
	{DISPCTRL_WR_DATA, 0x40},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x60},
	{DISPCTRL_WR_DATA, 0x37},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0F},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x05},
	{DISPCTRL_WR_DATA, 0x47},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x18},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x89},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x11},
	{DISPCTRL_WR_DATA, 0x33},
	{DISPCTRL_WR_DATA, 0x55},
	{DISPCTRL_WR_DATA, 0x77},
	{DISPCTRL_WR_DATA, 0x31},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x98},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x66},
	{DISPCTRL_WR_DATA, 0x44},
	{DISPCTRL_WR_DATA, 0x22},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x89},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x22},
	{DISPCTRL_WR_DATA, 0x44},
	{DISPCTRL_WR_DATA, 0x66},
	{DISPCTRL_WR_DATA, 0x20},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x98},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x77},
	{DISPCTRL_WR_DATA, 0x55},
	{DISPCTRL_WR_DATA, 0x33},
	{DISPCTRL_WR_DATA, 0x11},
	{DISPCTRL_WR_DATA, 0x13},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0xCF},
	{DISPCTRL_WR_DATA, 0xFF},
	{DISPCTRL_WR_DATA, 0xFF},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0xCF},
	{DISPCTRL_WR_DATA, 0xFF},
	{DISPCTRL_WR_DATA, 0xFF},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x8C},
	{DISPCTRL_WR_DATA, 0x5A},
	/* Initializing End */
	/* Gamma Setting Start*/

	{DISPCTRL_WR_CMND, 0xE0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x0D},
	{DISPCTRL_WR_DATA, 0x0C},
	{DISPCTRL_WR_DATA, 0x3F},
	{DISPCTRL_WR_DATA, 0x19},
	{DISPCTRL_WR_DATA, 0x2D},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x0F},
	{DISPCTRL_WR_DATA, 0x0E},
	{DISPCTRL_WR_DATA, 0x14},
	{DISPCTRL_WR_DATA, 0x17},
	{DISPCTRL_WR_DATA, 0x15},
	{DISPCTRL_WR_DATA, 0x16},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x11},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x0C},
	{DISPCTRL_WR_DATA, 0x12},
	{DISPCTRL_WR_DATA, 0x3F},
	{DISPCTRL_WR_DATA, 0x19},
	{DISPCTRL_WR_DATA, 0x2C},
	{DISPCTRL_WR_DATA, 0x05},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x0E},
	{DISPCTRL_WR_DATA, 0x12},
	{DISPCTRL_WR_DATA, 0x16},
	{DISPCTRL_WR_DATA, 0x14},
	{DISPCTRL_WR_DATA, 0x15},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x12},
	{DISPCTRL_WR_DATA, 0x01},

	{DISPCTRL_WR_CMND, 0xC1},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x18},
	{DISPCTRL_WR_DATA, 0x1F},
	{DISPCTRL_WR_DATA, 0x27},
	{DISPCTRL_WR_DATA, 0x2E},
	{DISPCTRL_WR_DATA, 0x34},
	{DISPCTRL_WR_DATA, 0x3E},
	{DISPCTRL_WR_DATA, 0x48},
	{DISPCTRL_WR_DATA, 0x50},
	{DISPCTRL_WR_DATA, 0x58},
	{DISPCTRL_WR_DATA, 0x60},
	{DISPCTRL_WR_DATA, 0x68},
	{DISPCTRL_WR_DATA, 0x70},
	{DISPCTRL_WR_DATA, 0x78},
	{DISPCTRL_WR_DATA, 0x80},
	{DISPCTRL_WR_DATA, 0x88},
	{DISPCTRL_WR_DATA, 0x90},
	{DISPCTRL_WR_DATA, 0x98},
	{DISPCTRL_WR_DATA, 0xA0},
	{DISPCTRL_WR_DATA, 0xA8},
	{DISPCTRL_WR_DATA, 0xB0},
	{DISPCTRL_WR_DATA, 0xB8},
	{DISPCTRL_WR_DATA, 0xC0},
	{DISPCTRL_WR_DATA, 0xC8},
	{DISPCTRL_WR_DATA, 0xD0},
	{DISPCTRL_WR_DATA, 0xD8},
	{DISPCTRL_WR_DATA, 0xE0},
	{DISPCTRL_WR_DATA, 0xE8},
	{DISPCTRL_WR_DATA, 0xF0},
	{DISPCTRL_WR_DATA, 0xF7},
	{DISPCTRL_WR_DATA, 0xFF},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x18},
	{DISPCTRL_WR_DATA, 0x1F},
	{DISPCTRL_WR_DATA, 0x27},
	{DISPCTRL_WR_DATA, 0x2E},
	{DISPCTRL_WR_DATA, 0x34},
	{DISPCTRL_WR_DATA, 0x3E},
	{DISPCTRL_WR_DATA, 0x48},
	{DISPCTRL_WR_DATA, 0x50},
	{DISPCTRL_WR_DATA, 0x58},
	{DISPCTRL_WR_DATA, 0x60},
	{DISPCTRL_WR_DATA, 0x68},
	{DISPCTRL_WR_DATA, 0x70},
	{DISPCTRL_WR_DATA, 0x78},
	{DISPCTRL_WR_DATA, 0x80},
	{DISPCTRL_WR_DATA, 0x88},
	{DISPCTRL_WR_DATA, 0x90},
	{DISPCTRL_WR_DATA, 0x98},
	{DISPCTRL_WR_DATA, 0xA0},
	{DISPCTRL_WR_DATA, 0xA8},
	{DISPCTRL_WR_DATA, 0xB0},
	{DISPCTRL_WR_DATA, 0xB8},
	{DISPCTRL_WR_DATA, 0xC0},
	{DISPCTRL_WR_DATA, 0xC8},
	{DISPCTRL_WR_DATA, 0xD0},
	{DISPCTRL_WR_DATA, 0xD8},
	{DISPCTRL_WR_DATA, 0xE0},
	{DISPCTRL_WR_DATA, 0xE8},
	{DISPCTRL_WR_DATA, 0xF0},
	{DISPCTRL_WR_DATA, 0xF7},
	{DISPCTRL_WR_DATA, 0xFF},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x18},
	{DISPCTRL_WR_DATA, 0x1F},
	{DISPCTRL_WR_DATA, 0x27},
	{DISPCTRL_WR_DATA, 0x2E},
	{DISPCTRL_WR_DATA, 0x34},
	{DISPCTRL_WR_DATA, 0x3E},
	{DISPCTRL_WR_DATA, 0x48},
	{DISPCTRL_WR_DATA, 0x50},
	{DISPCTRL_WR_DATA, 0x58},
	{DISPCTRL_WR_DATA, 0x60},
	{DISPCTRL_WR_DATA, 0x68},
	{DISPCTRL_WR_DATA, 0x70},
	{DISPCTRL_WR_DATA, 0x78},
	{DISPCTRL_WR_DATA, 0x80},
	{DISPCTRL_WR_DATA, 0x88},
	{DISPCTRL_WR_DATA, 0x90},
	{DISPCTRL_WR_DATA, 0x98},
	{DISPCTRL_WR_DATA, 0xA0},
	{DISPCTRL_WR_DATA, 0xA8},
	{DISPCTRL_WR_DATA, 0xB0},
	{DISPCTRL_WR_DATA, 0xB8},
	{DISPCTRL_WR_DATA, 0xC0},
	{DISPCTRL_WR_DATA, 0xC8},
	{DISPCTRL_WR_DATA, 0xD0},
	{DISPCTRL_WR_DATA, 0xD8},
	{DISPCTRL_WR_DATA, 0xE0},
	{DISPCTRL_WR_DATA, 0xE8},
	{DISPCTRL_WR_DATA, 0xF0},
	{DISPCTRL_WR_DATA, 0xF7},
	{DISPCTRL_WR_DATA, 0xFF},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	/* Gamma Setting End*/


	{DISPCTRL_WR_CMND, 0x11},
	{DISPCTRL_SLEEP_MS, 120},
	{DISPCTRL_DRAW_BLACK, 0},

#ifdef CONFIG_LDI_MDNIE_SUPPORT
	{DISPCTRL_PANEL_MDNIE, 0}
#endif
	{DISPCTRL_WR_CMND, 0x29},
	{DISPCTRL_SLEEP_MS, 10},
	{DISPCTRL_DRAW_BLACK, 0},
	{DISPCTRL_SLEEP_MS, 20},
	{DISPCTRL_LIST_END, 0},
};

static struct dispctrl_rec hx8369b_initialize_cmd_1[] __initdata = {
	{DISPCTRL_DRAW_BLACK, 0},
	{DISPCTRL_LIST_END, 0},
};

static struct dispctrl_rec hx8369b_suspend_cmd[] __initdata = {
	{DISPCTRL_WR_CMND, 0x28},
	{DISPCTRL_SLEEP_MS, 150},

	{DISPCTRL_WR_CMND, 0x10},
	{DISPCTRL_SLEEP_MS, 150},
	{DISPCTRL_LIST_END, 0},
};

static struct dispctrl_rec hx8369b_panel_id_seq[] __initdata = {
	{DISPCTRL_WR_CMND, RDIDIF},
	{DISPCTRL_WR_DATA, 0x7A},
	{DISPCTRL_WR_DATA, 0xFB},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x82},
	{DISPCTRL_LIST_END, 0},
};

struct lcd_config hx8369b_config __initdata = {
	.name = "HX8369B",
	.modes_supp = LCD_VID_ONLY,
	.pack_send_mode = RT_DISPLAY_SEND_MODE_HS,
	.max_lanes = 2,
	.phy_width = 54,
	.phy_height = 95,
	.width = 480,
	.height = 800,
	.hbp = 55,
	.hfp = 215,
	.hsync = 65,
	.vbp = 19,
	.vfp = 6,
	.vsync = 4,
	.setup = 2000,
	.pulse = 2000,
	.init_seq_vid = hx8369b_initialize_cmd,
	.init_seq_min = hx8369b_initialize_cmd_1,
	.suspend_seq = hx8369b_suspend_cmd,
	.panel_id = hx8369b_panel_id_seq,
	.verify_id = false,
	.color_mode = RGB888,
	.vburst_mode = LCD_VID_NON_BURST_SYNC_EVENT,
	.max_hs_bps = 500 * 1000000,
	.cont_clock = 0,
	.cmd_transmission = 1,
	.time_param = &hx8369b_time_param,
};
