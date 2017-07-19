/*
 * drivers/video/r-mobile/panel/panel_otm9608a.h
 *
 * Copyright (C) 2014 Broadcom Corporation
 * All rights reserved.
 *
 */
#include "panel_common.h"

#define OTM9608A_TE_SCAN_LINE 960

#define OTM9608A_HS_TRAIL_NS 60
#define OTM9608A_HS_TRAIL_UI 4
#define OTM9608A_HS_TRAIL_ADJUST (25*PRECISION_FACT)
#define OTM9608A_HSPPVL_NS 40
#define OTM9608A_HSPPVL_UI 4
#define OTM9608A_CKTRVL_NS 60
#define OTM9608A_CKPPVL_NS 38
#define OTM9608A_HSZEROVL_NS 145
#define OTM9608A_HSZEROVL_UI 10
#define OTM9608A_CKPOSTVL_NS 60
#define OTM9608A_CKPOSTVL_UI 52
#define OTM9608A_CKPREVL_NS 1
#define OTM9608A_CKPREVL_UI 8

static struct screen_disp_time_param otm9608a_time_param __initdata = {
	.hstrial_ns = OTM9608A_HS_TRAIL_NS,
	.hstrial_ui = OTM9608A_HS_TRAIL_UI,
	.hstrial_adj = OTM9608A_HS_TRAIL_ADJUST,
	.hsppvl_ns = OTM9608A_HSPPVL_NS,
	.hsppvl_ui = OTM9608A_HSPPVL_UI,
	.cktrvl_ns = OTM9608A_CKTRVL_NS,
	.ckppvl_ns = OTM9608A_CKPPVL_NS,
	.hszerovl_ns = OTM9608A_HSZEROVL_NS,
	.hszerovl_ui = OTM9608A_HSZEROVL_UI,
	.ckpostvl_ns = OTM9608A_CKPOSTVL_NS,
	.ckpostvl_ui = OTM9608A_CKPOSTVL_UI,
	.ckprevl_ns = OTM9608A_CKPREVL_NS,
	.ckprevl_ui = OTM9608A_CKPREVL_UI,
};

static struct dispctrl_rec otm9608a_initialize_cmd[] __initdata = {
	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0xFF},
	{DISPCTRL_WR_DATA, 0x96},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x01},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x80},

	{DISPCTRL_WR_CMND, 0xFF},
	{DISPCTRL_WR_DATA, 0x96},
	{DISPCTRL_WR_DATA, 0x08},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0xA0},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x80},

	{DISPCTRL_WR_CMND, 0xB3},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x20},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xC0},

	{DISPCTRL_WR_CMND, 0xB3},
	{DISPCTRL_WR_DATA, 0x09},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x80},

	{DISPCTRL_WR_CMND, 0xC0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x48},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x47},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x10},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x92},

	{DISPCTRL_WR_CMND, 0xC0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x13},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xA2},

	{DISPCTRL_WR_CMND, 0xC0},
	{DISPCTRL_WR_DATA, 0x0C},
	{DISPCTRL_WR_DATA, 0x05},
	{DISPCTRL_WR_DATA, 0x02},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xB3},

	{DISPCTRL_WR_CMND, 0xC0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x50},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x81},

	{DISPCTRL_WR_CMND, 0xC1},
	{DISPCTRL_WR_DATA, 0x55},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x80},

	{DISPCTRL_WR_CMND, 0xC4},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x84},
	{DISPCTRL_WR_DATA, 0xFC},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xA0},

	{DISPCTRL_WR_CMND, 0xB3},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xA0},

	{DISPCTRL_WR_CMND, 0xC0},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xA0},

	{DISPCTRL_WR_CMND, 0xC4},
	{DISPCTRL_WR_DATA, 0x33},
	{DISPCTRL_WR_DATA, 0x09},
	{DISPCTRL_WR_DATA, 0x90},
	{DISPCTRL_WR_DATA, 0x2B},
	{DISPCTRL_WR_DATA, 0x33},
	{DISPCTRL_WR_DATA, 0x09},
	{DISPCTRL_WR_DATA, 0x90},
	{DISPCTRL_WR_DATA, 0x54},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x80},

	{DISPCTRL_WR_CMND, 0xC5},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0xA0},
	{DISPCTRL_WR_DATA, 0x11},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x90},

	{DISPCTRL_WR_CMND, 0xC5},
	{DISPCTRL_WR_DATA, 0xD6},
	{DISPCTRL_WR_DATA, 0x57},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x57},
	{DISPCTRL_WR_DATA, 0x33},
	{DISPCTRL_WR_DATA, 0x33},
	{DISPCTRL_WR_DATA, 0x34},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xA0},

	{DISPCTRL_WR_CMND, 0xC5},
	{DISPCTRL_WR_DATA, 0x96},
	{DISPCTRL_WR_DATA, 0x57},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x57},
	{DISPCTRL_WR_DATA, 0x33},
	{DISPCTRL_WR_DATA, 0x33},
	{DISPCTRL_WR_DATA, 0x34},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xB0},

	{DISPCTRL_WR_CMND, 0xC5},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0xAC},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x71},
	{DISPCTRL_WR_DATA, 0xB1},
	{DISPCTRL_WR_DATA, 0x83},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0xD9},
	{DISPCTRL_WR_DATA, 0x61},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x80},

	{DISPCTRL_WR_CMND, 0xC6},
	{DISPCTRL_WR_DATA, 0x64},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xB0},

	{DISPCTRL_WR_CMND, 0xC6},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x1F},
	{DISPCTRL_WR_DATA, 0x12},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xB7},

	{DISPCTRL_WR_CMND, 0xB0},
	{DISPCTRL_WR_DATA, 0x10},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xC0},

	{DISPCTRL_WR_CMND, 0xB0},
	{DISPCTRL_WR_DATA, 0x55},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xB1},

	{DISPCTRL_WR_CMND, 0xB0},
	{DISPCTRL_WR_DATA, 0x03},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x81},

	{DISPCTRL_WR_CMND, 0xD6},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0xE1},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x0D},
	{DISPCTRL_WR_DATA, 0x13},
	{DISPCTRL_WR_DATA, 0x0F},
	{DISPCTRL_WR_DATA, 0x07},
	{DISPCTRL_WR_DATA, 0x11},
	{DISPCTRL_WR_DATA, 0x0B},
	{DISPCTRL_WR_DATA, 0x0A},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x0B},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x0D},
	{DISPCTRL_WR_DATA, 0x0E},
	{DISPCTRL_WR_DATA, 0x09},
	{DISPCTRL_WR_DATA, 0x01},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0xE2},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x0F},
	{DISPCTRL_WR_DATA, 0x15},
	{DISPCTRL_WR_DATA, 0x0E},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x0B},
	{DISPCTRL_WR_DATA, 0x0C},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x0B},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x0E},
	{DISPCTRL_WR_DATA, 0x0D},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x80},

	{DISPCTRL_WR_CMND, 0xcb},
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

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x90},

	{DISPCTRL_WR_CMND, 0xcb},
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
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xa0},

	{DISPCTRL_WR_CMND, 0xcb},
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
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xb0},

	{DISPCTRL_WR_CMND, 0xcb},
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

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xc0},

	{DISPCTRL_WR_CMND, 0xcb},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x08},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xd0},

	{DISPCTRL_WR_CMND, 0xcb},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x04},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xe0},

	{DISPCTRL_WR_CMND, 0xcb},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x08},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xf0},

	{DISPCTRL_WR_CMND, 0xcb},
	{DISPCTRL_WR_DATA, 0xff},
	{DISPCTRL_WR_DATA, 0xff},
	{DISPCTRL_WR_DATA, 0xff},
	{DISPCTRL_WR_DATA, 0xff},
	{DISPCTRL_WR_DATA, 0xff},
	{DISPCTRL_WR_DATA, 0xff},
	{DISPCTRL_WR_DATA, 0xff},
	{DISPCTRL_WR_DATA, 0xff},
	{DISPCTRL_WR_DATA, 0xff},
	{DISPCTRL_WR_DATA, 0xff},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x80},

	{DISPCTRL_WR_CMND, 0xcc},
	{DISPCTRL_WR_DATA, 0x26},
	{DISPCTRL_WR_DATA, 0x25},
	{DISPCTRL_WR_DATA, 0x23},
	{DISPCTRL_WR_DATA, 0x24},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0f},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0d},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0b},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x90},

	{DISPCTRL_WR_CMND, 0xcc},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x09},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x26},
	{DISPCTRL_WR_DATA, 0x25},
	{DISPCTRL_WR_DATA, 0x21},
	{DISPCTRL_WR_DATA, 0x22},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xa0},

	{DISPCTRL_WR_CMND, 0xcc},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0e},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0c},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0a},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xb0},

	{DISPCTRL_WR_CMND, 0xcc},
	{DISPCTRL_WR_DATA, 0x25},
	{DISPCTRL_WR_DATA, 0x26},
	{DISPCTRL_WR_DATA, 0x21},
	{DISPCTRL_WR_DATA, 0x22},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0a},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0c},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0e},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xb0},

	{DISPCTRL_WR_CMND, 0xcc},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x25},
	{DISPCTRL_WR_DATA, 0x26},
	{DISPCTRL_WR_DATA, 0x23},
	{DISPCTRL_WR_DATA, 0x24},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xd0},

	{DISPCTRL_WR_CMND, 0xcc},
	{DISPCTRL_WR_DATA, 0x09},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0b},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0d},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x0f},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x80},

	{DISPCTRL_WR_CMND, 0xce},
	{DISPCTRL_WR_DATA, 0x8a},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x89},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x88},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x87},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x06},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x90},

	{DISPCTRL_WR_CMND, 0xce},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xa0},

	{DISPCTRL_WR_CMND, 0xce},
	{DISPCTRL_WR_DATA, 0x38},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0xc1},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x38},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0xc2},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xb0},

	{DISPCTRL_WR_CMND, 0xce},
	{DISPCTRL_WR_DATA, 0x38},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0xc3},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x30},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0xc4},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xc0},

	{DISPCTRL_WR_CMND, 0xce},
	{DISPCTRL_WR_DATA, 0x38},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0xbd},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x38},
	{DISPCTRL_WR_DATA, 0x05},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0xbe},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xd0},

	{DISPCTRL_WR_CMND, 0xce},
	{DISPCTRL_WR_DATA, 0x38},
	{DISPCTRL_WR_DATA, 0x04},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0xbf},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x38},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0x03},
	{DISPCTRL_WR_DATA, 0xc0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x06},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x80},

	{DISPCTRL_WR_CMND, 0xcf},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x90},

	{DISPCTRL_WR_CMND, 0xcf},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xa0},

	{DISPCTRL_WR_CMND, 0xcf},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xb0},

	{DISPCTRL_WR_CMND, 0xcf},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0xf0},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x10},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0xc0},

	{DISPCTRL_WR_CMND, 0xcf},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x02},
	{DISPCTRL_WR_DATA, 0x20},
	{DISPCTRL_WR_DATA, 0x20},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x01},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_DATA, 0x02},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0xd8},
	{DISPCTRL_WR_DATA, 0xA7},
	{DISPCTRL_WR_DATA, 0xA7},

	{DISPCTRL_WR_CMND, 0x00},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0xff},
	{DISPCTRL_WR_DATA, 0xff},
	{DISPCTRL_WR_DATA, 0xff},
	{DISPCTRL_WR_DATA, 0xff},

	{DISPCTRL_WR_CMND, 0x3a},
	{DISPCTRL_WR_DATA, 0x77},

	{DISPCTRL_WR_CMND, 0x35},
	{DISPCTRL_WR_DATA, 0x00},

	{DISPCTRL_WR_CMND, 0x44},
	{DISPCTRL_WR_DATA, (OTM9608A_TE_SCAN_LINE & 0xFF00) >> 8},
	{DISPCTRL_WR_DATA, (OTM9608A_TE_SCAN_LINE & 0xFF)},

	{DISPCTRL_WR_CMND, 0x11},
	{DISPCTRL_SLEEP_MS, 120},
	{DISPCTRL_DRAW_BLACK, 0},

	{DISPCTRL_WR_CMND, 0x29},
	{DISPCTRL_SLEEP_MS, 10},
	{DISPCTRL_LIST_END, 0},
};

static struct dispctrl_rec otm9608a_suspend_cmd[] __initdata = {
	{DISPCTRL_WR_CMND, 0x28},
	{DISPCTRL_WR_CMND, 0x10},
	{DISPCTRL_SLEEP_MS, 120},
	{DISPCTRL_LIST_END, 0},
};

static struct dispctrl_rec otm9608a_panel_id_seq[] __initdata = {
	{DISPCTRL_WR_CMND, RDID1},
	{DISPCTRL_WR_DATA, 0x40},
	{DISPCTRL_WR_CMND, RDID2},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_WR_CMND, RDID3},
	{DISPCTRL_WR_DATA, 0x00},
	{DISPCTRL_LIST_END, 0},
};

struct lcd_config otm9608a_config __initdata = {
	.name = "OTM9608A",
	.modes_supp = LCD_CMD_ONLY,
	.pack_send_mode = RT_DISPLAY_SEND_MODE_LP,
	.max_lanes = 2,
	.phy_width = 54,
	.phy_height = 95,
	.width = 540,
	.height = 960,
	.hbp = 8,
	.hfp = 8,
	.hsync = 8,
	.vbp = 8,
	.vfp = 8,
	.vsync = 4,
	.setup = 1000,
	.pulse = 5000,
	.init_seq_vid = NULL,
	.init_seq_cmd = otm9608a_initialize_cmd,
	.suspend_seq = otm9608a_suspend_cmd,
	.panel_id = otm9608a_panel_id_seq,
	.verify_id = false,
	.color_mode = RGB888,
	.vburst_mode = LCD_VID_NONE,
	.max_hs_bps = 500 * 1000000,
	.cont_clock = 0,
	.cmd_transmission = 0,
	.time_param = &otm9608a_time_param,
};

