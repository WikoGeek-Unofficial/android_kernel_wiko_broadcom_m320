/*
 * drivers/video/r-mobile/panels.h
 *
 * Copyright (C) 2014 Broadcom Corporation
 * All rights reserved.
 *
 */

#ifndef __R_MOBILE_LCDCFB_H__
#define __R_MOBILE_LCDCFB_H__

#include "panel/panel_nt35516.h"

#include "panel/panel_nt35510.h"
#include "panel/panel_nt35590.h"
#include "panel/panel_hx8369b.h"
#include "panel/panel_hx8389b.h"
#include "panel/panel_otm8018b.h"
#include "panel/panel_hx8389b_truly.h"
#include "panel/panel_s6e88a0.h"
#include "panel/panel_otm9608a.h"
#include "panel/panel_nt35512.h"
#include "panel/panel_s6d78a0.h"
/*custom_t peter  add lcd3 */
#include "panel/panel_otm8018b_boe.h"

static __initdata struct lcd_config *cfgs[] = {
	[PANEL_NT35516] = &nt35516_config,
	[PANEL_NT35510_HEAT] = &nt35510_config,
	[PANEL_NT35590] = &nt35590_config,
	[PANEL_HX8369B] = &hx8369b_config,
	[PANEL_HX8389B] = &hx8389b_config,
	[PANEL_OTM8018B] = &otm8018b_config,
	[PANEL_HX8389B_TRULY] = &hx8389b_truly_config,
	[PANEL_S6E88A0] = &s6e88a0_config,
	[PANEL_OTM9608A] = &otm9608a_config,
	[PANEL_NT35512] = &nt35512_config,
	[PANEL_S6D78A0] = &s6d78a0_config,
	//++ custom_t peter add boe lcd
	[PANEL_OTM8018B_BOE] = &otm8018b_boe_config,
	//-- custom_t peter
};

struct lcd_config * __init get_panel_cfg(unsigned int index)
{
	if (index <= PANEL_NONE || index >= PANEL_MAX) {
		pr_err("Invalid panel index!\n");
		return NULL;
	}

	return cfgs[index];
}

struct lcd_config * __init get_panel_cfg_byname(const char *name)
{
	int i = 0;
	for (; i < PANEL_MAX; ++i)
		if (cfgs[i] && strcmp(cfgs[i]->name, name) == 0)
			return cfgs[i];
	return NULL;
}
#endif /*__R_MOBILE_LCDCFB_H__*/
