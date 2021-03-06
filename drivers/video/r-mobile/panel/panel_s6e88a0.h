/*
 * drivers/video/r-mobile/panel/panel_s6e88a0.h
 *
 * Copyright (C) 2014 Broadcom Corporation
 * All rights reserved.
 *
 */

#include "panel_common.h"

#define S6E88A0_HS_TRAIL_NS 60
#define S6E88A0_HS_TRAIL_UI 4
#define S6E88A0_HS_TRAIL_ADJUST (25*PRECISION_FACT)
#define S6E88A0_HSPPVL_NS 40
#define S6E88A0_HSPPVL_UI 4
#define S6E88A0_CKTRVL_NS 60
#define S6E88A0_CKPPVL_NS 38
#define S6E88A0_HSZEROVL_NS 145
#define S6E88A0_HSZEROVL_UI 10
#define S6E88A0_CKPOSTVL_NS 60
#define S6E88A0_CKPOSTVL_UI 52
#define S6E88A0_CKPREVL_NS 1
#define S6E88A0_CKPREVL_UI 8

static struct screen_disp_time_param s6e88a0_time_param __initdata = {
	.hstrial_ns = S6E88A0_HS_TRAIL_NS,
	.hstrial_ui = S6E88A0_HS_TRAIL_UI,
	.hstrial_adj = S6E88A0_HS_TRAIL_ADJUST,
	.hsppvl_ns = S6E88A0_HSPPVL_NS,
	.hsppvl_ui = S6E88A0_HSPPVL_UI,
	.cktrvl_ns = S6E88A0_CKTRVL_NS,
	.ckppvl_ns = S6E88A0_CKPPVL_NS,
	.hszerovl_ns = S6E88A0_HSZEROVL_NS,
	.hszerovl_ui = S6E88A0_HSZEROVL_UI,
	.ckpostvl_ns = S6E88A0_CKPOSTVL_NS,
	.ckpostvl_ui = S6E88A0_CKPOSTVL_UI,
	.ckprevl_ns = S6E88A0_CKPREVL_NS,
	.ckprevl_ui = S6E88A0_CKPREVL_UI,
};

static struct dispctrl_rec s6e88a0_initialize_vid[] __initdata = {
	{DISPCTRL_WR_CMND, 0xF0},
	{DISPCTRL_WR_DATA, 0x5A},
	{DISPCTRL_WR_DATA, 0x5A},

	{DISPCTRL_WR_CMND, 0xFC},
	{DISPCTRL_WR_DATA, 0x5A},
	{DISPCTRL_WR_DATA, 0x5A},

	{DISPCTRL_WR_CMND, 0xFD},
	{DISPCTRL_WR_DATA, 0x16},
	{DISPCTRL_WR_DATA, 0x55},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0A},
	{DISPCTRL_WR_DATA, 0x5A},
	{DISPCTRL_WR_DATA, 0x5D},
	{DISPCTRL_WR_DATA, 0x55},
	{DISPCTRL_WR_DATA, 0x0E},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x20},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0xD5},
	{DISPCTRL_WR_DATA, 0xD1},
	{DISPCTRL_WR_DATA, 0xDF},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x11},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x18},
	{DISPCTRL_WR_DATA, 0x0A},
	{DISPCTRL_WR_DATA, 0x0A},
	{DISPCTRL_WR_DATA, 0x0F},

	{DISPCTRL_WR_CMND, 0xB8},
	{DISPCTRL_WR_DATA, 0x38},
	{DISPCTRL_WR_DATA, 0x0B},
	{DISPCTRL_WR_DATA, 0x30},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x28},

	{DISPCTRL_WR_CMND, 0x11},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_SLEEP_MS, 20},

	/*SEQ_PANEL_CONDITION_SET*/
	{DISPCTRL_WR_CMND, 0xCB},
	{DISPCTRL_WR_DATA, 0x0E},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x30},
	{DISPCTRL_WR_DATA, 0x67},
	{DISPCTRL_WR_DATA, 0x89},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x61},
	{DISPCTRL_WR_DATA, 0x9C},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x69},
	{DISPCTRL_WR_DATA, 0x85},
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
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0F},
	{DISPCTRL_WR_DATA, 0x7A},
	{DISPCTRL_WR_DATA, 0x0F},
	{DISPCTRL_WR_DATA, 0x7A},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x23},
	{DISPCTRL_WR_DATA, 0x23},
	{DISPCTRL_WR_DATA, 0x23},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x80},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x0C},
	{DISPCTRL_WR_DATA, 0x01},

	{DISPCTRL_WR_CMND, 0xF4},
	{DISPCTRL_WR_DATA, 0x67},
	{DISPCTRL_WR_DATA, 0x0A},
	{DISPCTRL_WR_DATA, 0x95},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x11},

	{DISPCTRL_WR_CMND, 0xFC},
	{DISPCTRL_WR_DATA, 0xA5},
	{DISPCTRL_WR_DATA, 0xA5},

	 {DISPCTRL_WR_CMND, 0x29},
	 {DISPCTRL_WR_DATA, 0x00},
	 {DISPCTRL_WR_DATA, 0x00},
	 {DISPCTRL_LIST_END, 0},
};


static struct dispctrl_rec s6e88a0_suspend_cmnd[] __initdata = {
	{DISPCTRL_WR_CMND, 0x28},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_SLEEP_MS, 34},

	{DISPCTRL_WR_CMND, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_SLEEP_MS, 120},
	{DISPCTRL_LIST_END, 0},

};

static struct dispctrl_rec s6e88a0_id_seq[] __initdata = {
	{DISPCTRL_WR_CMND, 0x04},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_LIST_END, 0},
};


struct lcd_config s6e88a0_config __initdata = {
	.name = "S6E88A0",
	.modes_supp = LCD_VID_ONLY,
	.pack_send_mode = RT_DISPLAY_SEND_MODE_LP,
	.max_lanes = 2,
	.phy_width = 90,
	.phy_height = 154,
	.width = 480,
	.height = 800,
	.hbp = 8,
	.hfp = 100,
	.hsync = 8,
	.vbp = 2,
	.vfp = 13,
	.vsync = 1,
	.setup = 1000,
	.pulse = 100000,
	.init_seq_vid = s6e88a0_initialize_vid,
	.panel_id = s6e88a0_id_seq,
	.suspend_seq = s6e88a0_suspend_cmnd,
	.verify_id = false,
	.color_mode = RGB888,
	.vburst_mode = LCD_VID_NON_BURST_SYNC_EVENT,
	.max_hs_bps = 500 * 1000000,
	.cont_clock = 1,
	.cmd_transmission = 1,
	.time_param = &s6e88a0_time_param,
};

