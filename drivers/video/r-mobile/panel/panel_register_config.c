/*
 * drivers/video/r-mobile/panel/panel_register_config.c
 *
 * Copyright (C) 2014 Broadcom Corporation
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
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include "panel_common.h"
#include "panel_info.h"
#include "panel_register_config.h"


static struct st_dsi P_DSI;
//static unsigned int hfp_adjust, total_pixel, pixclk;
static unsigned int total_pixel, pixclk;
unsigned int hfp_adjust = 0;

int calc_div_mult(struct lcd_config *lcd_cfg,
	struct panel_info *panel_config, struct fb_board_cfg *pdata)
{
	unsigned int div = PLL_DIV_MIN, mul = PLL_MULT_MIN;
	unsigned int target_div = PLL_DIV_MIN, target_mul = PLL_MULT_MIN;
	unsigned int clock_rate;
	unsigned int clock = PLL1_2;
	unsigned int h_total, v_total, byte_clk = 8;
	unsigned int transfer_rate;
	unsigned int diff, taget_fact;
	P_DSI.LDDCKR.BIT.MDCDR = CKR_MDCDR;
	P_DSI.LDDCKR.BIT.MOSEL = CKR_MOSEL;
	P_DSI.LDDCKR.BIT.ICKSEL = ICKSEL_8BYTE_CLK;

	REG_DBG("LDDCKR:0x%0x\n" , (int)P_DSI.LDDCKR.LONG);

	P_DSI.DSITCKCR.BIT.CKSTP = 0;               /* Do not set this bit */
	P_DSI.DSITCKCR.BIT.EXSRC = DSIT_EXSRC;      /* Clock Source PLL1×1/2*/
	if (P_DSI.DSITCKCR.BIT.EXSRC == DSIT_EXSRC)
		P_DSI.DSITCKCR.BIT.DIV = PLL1/(2*DSIT_LPCLK*1000000);
	else
		P_DSI.DSITCKCR.BIT.DIV = PLL2/(DSIT_LPCLK*1000000);

	/*hfp_adjust = ((((lcd_cfg->color_mode/8)/
		panel_config->lanes)-1)*panel_config->width);*/
	hfp_adjust = 0;
	switch (lcd_cfg->color_mode) {
	case RGB565:
	case BGR565:
		if (panel_config->lanes == 1)
			hfp_adjust = panel_config->width;
		break;
	case RGB666:
	case BGR666:
		if (panel_config->lanes == 1)
			hfp_adjust = (panel_config->width >> 2);
		if (panel_config->lanes == 2)
			hfp_adjust = (panel_config->width >> 3);
		break;
	case RGB888:
	case BGR888:
	case RGB666_LOOSELY_PACKED:
	case BGR666_LOOSELY_PACKED:
		if (panel_config->lanes == 1)
			hfp_adjust = (panel_config->width << 1);
		if (panel_config->lanes == 2)
			hfp_adjust = (panel_config->width >> 1);
		break;
	}

	h_total = (panel_config->width+panel_config->hbp+
			panel_config->hfp+panel_config->hsync+hfp_adjust);
	v_total = (panel_config->height+panel_config->vbp+
			panel_config->vfp+panel_config->vsync);
	total_pixel = h_total * v_total;
	transfer_rate = (total_pixel*FPS)*byte_clk;
	REG_DBG("panel_config->height %d %d\n",
		(int)panel_config->width, (int)panel_config->height);
	REG_DBG("total_pixel %d %d %d\n",
		(int)total_pixel, (int)transfer_rate,
		(int)panel_config->hs_bps);

	if (transfer_rate > panel_config->hs_bps) {
		byte_clk = 6;
		transfer_rate = (total_pixel*FPS)*byte_clk;
		if (transfer_rate > panel_config->hs_bps) {
			REG_DUMP("Error : Transfer Rate Over\n");
			return -1;
		} else
		P_DSI.LDDCKR.BIT.ICKSEL = ICKSEL_6BYTE_CLK;
	}
	clock = clock / 1000;
	transfer_rate = transfer_rate / 1000;
	REG_DBG("clock: %u transfer_rate:%u\n ",
		(int)clock, (int)transfer_rate);

	clock_rate = (clock*PLL_MULT_MIN)/(PLL_DIV_MIN);
	taget_fact = diff = abs(clock_rate-transfer_rate);
	REG_DBG("clock_rate: %u diff:%u\n ", (int)clock_rate, (int)diff);

	for (div = PLL_DIV_MIN; div <= PLL_DIV_MAX; div++) {
		for (mul = PLL_MULT_MIN; mul <= PLL_MULT_MAX; mul++) {
			clock_rate = (clock*mul)/(div);
			diff = abs(clock_rate-transfer_rate);
			if (diff < taget_fact) {
				target_div = div;
				target_mul = mul;
				taget_fact = diff;
			}
		}
	}
	P_DSI.DSI0PHYCR.BIT.LOCKUPTIME = LOCKUP_TIME;
	P_DSI.DSI0PHYCR.BIT.MANUAL = PHY_MANUAL;
	P_DSI.DSI0PHYCR.BIT.BYPSL = PHY_BYPSL;
	P_DSI.DSI0PHYCR.BIT.PWDNL = PHY_PWDNL;

	P_DSI.DSI0PHYCR.BIT.PLLE = 0;
	P_DSI.DSI0PCKCR.BIT.EXSRC = DSI0PCKCR_EXSRC;
	P_DSI.DSI0PHYCR.BIT.PLLDSA = target_mul-1;
	P_DSI.DSI0PCKCR.BIT.DIV = target_div-1;

	if ((pdata->desense_offset != 0) && (pdata->vmode != 0)) {
		clock = clock * 1000;
		transfer_rate = transfer_rate * 1000;

		transfer_rate = transfer_rate +
			pdata->desense_offset;

		if (transfer_rate > panel_config->hs_bps)
			REG_DBG("Error : Transfer Rate Over\n");
		clock = clock / 1000;
		transfer_rate = transfer_rate / 1000;
		REG_DBG("clock: %u transfer_rate:%u\n ",
			(int)clock, (int)transfer_rate);

		clock_rate = (clock*PLL_MULT_MIN)/(PLL_DIV_MIN);
		taget_fact = diff = abs(clock_rate-transfer_rate);
		REG_DBG("clock_rate: %u diff:%u\n ",
			(int)clock_rate, (int)diff);

		for (div = PLL_DIV_MIN; div <= PLL_DIV_MAX; div++) {
			for (mul = PLL_MULT_MIN; mul <= PLL_MULT_MAX; mul++) {
				clock_rate = (clock*mul)/(div);
				diff = abs(clock_rate-transfer_rate);
				if (diff < taget_fact) {
					target_div = div;
					target_mul = mul;
					taget_fact = diff;
				}
			}
		}
		P_DSI.DSI0PCKCR.BIT.EXSRCB = DSI0PCKCR_EXSRCB;
		P_DSI.DSI0PHYCR.BIT.PLLDSB = target_mul-1;
		P_DSI.DSI0PHYCR.BIT.PLLE = 1;
		P_DSI.DSI0PCKCR.BIT.DIVB = target_div-1;
		P_DSI.DSI0PCKCR.BIT.CKCHGEN0 = 1;
		P_DSI.DSI0PCKCR.BIT.CKCHGEN1 = 1;
	}
	return 0;
}

int configuarion_setting(struct lcd_config *lcd_cfg,
	struct panel_info *panel_config, struct fb_board_cfg *pdata)
{
	P_DSI.SYSCONF.BIT.LANEEN = LANE_ONE;
	if (panel_config->lanes == 2)
		P_DSI.SYSCONF.BIT.LANEEN = LANE_TWO;
	if (panel_config->lanes == 3)
		P_DSI.SYSCONF.BIT.LANEEN = LANE_THREE;
	if (panel_config->lanes == 4)
		P_DSI.SYSCONF.BIT.LANEEN = LANE_FOUR;

	P_DSI.SYSCONF.BIT.ECCCEN = SC_ECCCEN;
	P_DSI.SYSCONF.BIT.CRCCEN = SC_CRCCEN;
	P_DSI.SYSCONF.BIT.EOTEN = SC_EOTEN;
	P_DSI.SYSCONF.BIT.CLSTPM =
		lcd_cfg->cont_clock; /* HW: 0, SW : 1*/
	P_DSI.SYSCONF.BIT.CONEN = SC_CONEN;
	P_DSI.SYSCONF.BIT.L123ULPSEN = SC_L123ULPSEN;

	if (pdata->vmode)
		P_DSI.DTCTR.BIT.VMEN = 1;
	else
		P_DSI.DTCTR.BIT.VMEN = 0;
	P_DSI.DTCTR.BIT.FLEN = DTC_FLEN;
	P_DSI.DTCTR.BIT.MTEN = DTC_MTEN;
	P_DSI.DTCTR.BIT.STRST = DTC_STRST;

	P_DSI.DSICTRL.BIT.CLHERQ = CLHERQ_TO_HS;
	P_DSI.DSICTRL.BIT.ULENRQ = DSIC_ULENRQ;
	P_DSI.DSICTRL.BIT.RSTRRQ = DSIC_RSTRRQ;
	P_DSI.DSICTRL.BIT.HTTORC = DSIC_HTTORC;
	P_DSI.DSICTRL.BIT.HSTTOEN = DSIC_HSTTOEN;
	P_DSI.DSICTRL.BIT.LPRTOEN = DSIC_LPRTOEN;
	P_DSI.DSICTRL.BIT.TATOEN = DSIC_TATOEN;
	P_DSI.DSICTRL.BIT.PRTOSRQ = DSIC_PRTOSRQ;

	P_DSI.MLDVSYNR.BIT.VSYNP = panel_config->height +
		panel_config->vfp + 1;
	P_DSI.MLDVSYNR.BIT.VSYNW = panel_config->vsync;

	P_DSI.MLDHSYNR.BIT.HSYNW = panel_config->hsync / 8;
	P_DSI.MLDHSYNR.BIT.HSYNP = (panel_config->width +
		panel_config->hfp + hfp_adjust) / 8;

	P_DSI.MLDHCNR.BIT.HDCN = panel_config->width / 8;
	P_DSI.MLDHCNR.BIT.HTCN = (panel_config->width + panel_config->hbp +
		panel_config->hfp + panel_config->hsync + hfp_adjust) / 8;

	{
		int mod, htotal;
		mod = panel_config->width / 8;
		P_DSI.MLDHAJR.BIT.HDCN_ADPIX =
			(panel_config->width - (mod * 8));
		htotal = (panel_config->width + panel_config->hbp +
			panel_config->hfp + panel_config->hsync + hfp_adjust);
		mod = htotal / 8;
		P_DSI.MLDHAJR.BIT.HTCNAJ = (htotal - (mod * 8));
		mod = panel_config->hsync / 8;
		P_DSI.MLDHAJR.BIT.HSYNWAJ = (panel_config->hsync - (mod * 8));
		htotal = (panel_config->width + panel_config->hfp + hfp_adjust);
		mod = htotal / 8;
		P_DSI.MLDHAJR.BIT.HSYNPAJ = (htotal - (mod * 8));
	}
	P_DSI.MLDVLNR.BIT.VDLN = panel_config->height;
	P_DSI.MLDVLNR.BIT.VTLN = panel_config->height +
		panel_config->vfp + panel_config->vbp + panel_config->vsync;

	P_DSI.MLDMT1R.BIT.MIFTYP = MLD_MIFTYP;
	P_DSI.MLDMT1R.BIT.M_3D = MLD_M_3D;
	P_DSI.MLDMT1R.BIT.IFM = MLD_IFM;
	P_DSI.MLDMT1R.BIT.YM = MLD_YM;
	P_DSI.MLDMT1R.BIT.CSM = MLD_CSM;
	P_DSI.MLDMT1R.BIT.VSSM = MLD_VSSM;
	P_DSI.MLDMT1R.BIT.DWCNT = MLD_DWCNT;
	P_DSI.MLDMT1R.BIT.HSCNT = MLD_HSCNT;
	P_DSI.MLDMT1R.BIT.DAPOL = MLD_DAPOL;
	P_DSI.MLDMT1R.BIT.DIPOL = MLD_DIPOL;
	P_DSI.MLDMT1R.BIT.DWPOL = MLD_DWPOL;
	P_DSI.MLDMT1R.BIT.HPOL = MLD_HPOL;
	P_DSI.MLDMT1R.BIT.VPOL = MLD_VPOL;
	P_DSI.MLDMT1R.BIT.SDL = MLD_SDL;

	P_DSI.MLDDCKPAT1R.BIT.DCKPAT1 = MLDDCKPAT1R_DCKPAT1;
	P_DSI.MLDDCKPAT2R.BIT.DCKPAT1 = MLDDCKPAT2R_DCKPAT1;
	return 0;
}
int calc_timeset(struct lcd_config *lcd_cfg,
	struct panel_info *panel_config, struct fb_board_cfg *pdata)
{
	unsigned int DSIT, DSIP, differential_clk, dot_clock, hsbyte;
	unsigned int UI, HS_TRAIL;
	unsigned int dot_clock_ns, hsbyte_ns, hsht_period, hsbyte_ns_div;
	unsigned int HS_PREPARE_HS_ZERO;
	unsigned int precision = PRECISION;
	unsigned int vburst_mode;

	if (P_DSI.DSITCKCR.BIT.EXSRC == DSIT_EXSRC)
		DSIT = (PLL1)/(2*(P_DSI.DSITCKCR.BIT.DIV+1));
	else
		DSIT = (PLL2)/(P_DSI.DSITCKCR.BIT.DIV+1);

	DSIP = 1;

	if (P_DSI.DSI0PCKCR.BIT.EXSRC == EXSRC_PLL1)
		DSIP = (PLL1)/(2*(P_DSI.DSI0PCKCR.BIT.DIV+1));
	if (P_DSI.DSI0PCKCR.BIT.EXSRC == EXSRC_PLL2)
		DSIP = (PLL2)/((P_DSI.DSI0PCKCR.BIT.DIV+1));
	if (P_DSI.DSI0PCKCR.BIT.EXSRC == EXSRC_MAIN_CLK)
		DSIP = (MAIN_CLK)/((P_DSI.DSI0PCKCR.BIT.DIV+1));
	if (P_DSI.DSI0PCKCR.BIT.EXSRC == EXSRC_EXTAL2)
		DSIP = (EXTAL2)/((P_DSI.DSI0PCKCR.BIT.DIV+1));
	differential_clk = DSIP * (P_DSI.DSI0PHYCR.BIT.PLLDSA + 1);
	hsbyte = differential_clk / (8*1000);
	hsbyte_ns = (precision*1000000) / hsbyte;

	differential_clk = differential_clk / 1000;
	UI = (1*1000000*precision) / differential_clk;

	P_DSI.PHYTEST.BIT.VREGUOVOL = 0;
	P_DSI.PHYTEST.BIT.PCREGUOVOL = 1;
	P_DSI.PHYTEST.BIT.LPFRESI = 0;
	P_DSI.PHYTEST.BIT.CPCURNT = 0;
	if (differential_clk > 200) {
		P_DSI.PHYTEST.BIT.VREGUOVOL = 1;
		P_DSI.PHYTEST.BIT.LPFRESI = 2;
		P_DSI.PHYTEST.BIT.CPCURNT = 3;
	}

	if (P_DSI.LDDCKR.BIT.ICKSEL == ICKSEL_M3_CLK)
		dot_clock = M3_LCDC;
	if (P_DSI.LDDCKR.BIT.ICKSEL == ICKSEL_8BYTE_CLK)
		dot_clock = hsbyte;
	if (P_DSI.LDDCKR.BIT.ICKSEL == ICKSEL_6BYTE_CLK)
		dot_clock = (hsbyte * 8) / 6;
	dot_clock_ns = (precision*1000000)/dot_clock;
	pixclk = dot_clock_ns * 10;
	if ((P_DSI.LDDCKR.BIT.ICKSEL == ICKSEL_HDMI_CLK)
		|| (P_DSI.LDDCKR.BIT.ICKSEL == ICKSEL_EXT_CLK)) {
		REG_DUMP("Error : ICKSEL not supported\n");
		return -1;
	}

	if ((1*8*UI) > (lcd_cfg->time_param->hstrial_ns*precision+
	1*lcd_cfg->time_param->hstrial_ui*UI))
		HS_TRAIL = (1*8*UI);
	else
		HS_TRAIL = (lcd_cfg->time_param->hstrial_ns*precision)+
		((1*lcd_cfg->time_param->hstrial_ui*UI));

	hsbyte_ns_div = (hsbyte_ns - 1);

	P_DSI.TIMSET0.BIT.WKUPVL = DSIT / (4 * 1000);

	P_DSI.TIMSET0.BIT.HSTRVL = ((((((HS_TRAIL+(hsbyte_ns_div*1))*
		precision)/hsbyte_ns))) + lcd_cfg->time_param->hstrial_adj)
		/precision;
	P_DSI.TIMSET0.BIT.HSPPVL =
	((((lcd_cfg->time_param->hsppvl_ns*precision)+
	(lcd_cfg->time_param->hsppvl_ui*UI)+
		(hsbyte_ns_div*1))/hsbyte_ns));
	P_DSI.TIMSET0.BIT.CKTRVL = (lcd_cfg->time_param->cktrvl_ns*
		precision+hsbyte_ns_div)/hsbyte_ns;
	P_DSI.TIMSET0.BIT.CKPPVL = (lcd_cfg->time_param->ckppvl_ns*
		precision+hsbyte_ns_div)/hsbyte_ns;

	HS_PREPARE_HS_ZERO = (lcd_cfg->time_param->hszerovl_ns
	*precision)+(lcd_cfg->time_param->hszerovl_ui*UI);
	P_DSI.TIMSET1.BIT.HSZEROVL = ((HS_PREPARE_HS_ZERO +
		(hsbyte_ns_div*1)) / hsbyte_ns);
	P_DSI.TIMSET1.BIT.CKPOSTVL =
	(((((lcd_cfg->time_param->ckpostvl_ns*precision)
	+(lcd_cfg->time_param->ckpostvl_ui*UI)) +
		(hsbyte_ns_div*1)) / hsbyte_ns));
	P_DSI.TIMSET1.BIT.CKPREVL =
		(((((lcd_cfg->time_param->ckprevl_ui*
		UI)+(hsbyte_ns_div * 1))
		/ hsbyte_ns))) + lcd_cfg->time_param->ckprevl_ns;

	P_DSI.VMCTR2.BIT.BL3BM = 0;
	P_DSI.VMCTR2.BIT.BL2BM = 0;

	if (lcd_cfg->cmd_transmission == 1)
		P_DSI.VMCTR2.BIT.BL1IEN = 1;
	else
		P_DSI.VMCTR2.BIT.BL1IEN = 0;

	P_DSI.VMCTR2.BIT.BL2IEN = 0;
	P_DSI.VMCTR2.BIT.BL3IEN = 0;

	vburst_mode = lcd_cfg->vburst_mode;
	if ((!pdata->vmode)
		&& (vburst_mode != LCD_VID_NONE)) {
		pr_err("Video parameter set for command mode\n");
		vburst_mode = LCD_VID_NONE;
	}

	if (vburst_mode == 2)
		P_DSI.VMCTR2.BIT.BL2E = 1;
	else
		P_DSI.VMCTR2.BIT.BL2E = 0;
	if (vburst_mode == 0) {
		P_DSI.VMCTR2.BIT.HSAE = 1;
		P_DSI.VMCTR2.BIT.HSEE = 1;
		P_DSI.VMCTR2.BIT.VSEE = 1;
	} else {
		P_DSI.VMCTR2.BIT.HSAE = 0;
		P_DSI.VMCTR2.BIT.HSEE = 0;
		P_DSI.VMCTR2.BIT.VSEE = 0;
	}
	P_DSI.VMCTR2.BIT.BL1VFPBM = 0;
	P_DSI.VMCTR2.BIT.BL1VBPBM = 0;
	P_DSI.VMCTR2.BIT.BL1VSABM = 0;

	/* Set to 0 to remain in LPS mode if possible */
	if (P_DSI.SYSCONF.BIT.CLSTPM == 0)
		hsht_period = 130;
	else
		hsht_period = 62;

	P_DSI.VMCTR2.BIT.HSABM = 0;
	P_DSI.VMCTR2.BIT.HBPBM = 0;
	P_DSI.VMCTR2.BIT.HFPBM = 0;

	if (((dot_clock_ns * panel_config->hsync) <
		(hsbyte_ns * hsht_period))
		&& (P_DSI.VMCTR2.BIT.HSAE == 1))
			P_DSI.VMCTR2.BIT.HSABM = 1;
	if ((dot_clock_ns * panel_config->hbp) <
		(hsbyte_ns * hsht_period))
			P_DSI.VMCTR2.BIT.HBPBM = 1;
	if (((dot_clock_ns * panel_config->hfp) <
		(hsbyte_ns * hsht_period)) &&
		(P_DSI.VMCTR2.BIT.BL2E == 0))
			P_DSI.VMCTR2.BIT.HFPBM = 1;

	switch (lcd_cfg->color_mode) {
	case RGB888:
		P_DSI.VMCTR1.BIT.DT	= RGB888_DT;
		P_DSI.VMCTR1.BIT.PCTYPE = RGB888_PCTYPE;
		break;
	case BGR888:
		P_DSI.VMCTR1.BIT.DT	= BGR888_DT;
		P_DSI.VMCTR1.BIT.PCTYPE = BGR888_PCTYPE;
		break;
	case RGB565:
		P_DSI.VMCTR1.BIT.DT	= RGB565_DT;
		P_DSI.VMCTR1.BIT.PCTYPE = RGB565_PCTYPE;
		break;
	case BGR565:
		P_DSI.VMCTR1.BIT.DT	= BGR565_DT;
		P_DSI.VMCTR1.BIT.PCTYPE = BGR565_PCTYPE;
		break;
	case RGB666_LOOSELY_PACKED:
		P_DSI.VMCTR1.BIT.DT	= RGB666_LOOSELY_PACKED_DT;
		P_DSI.VMCTR1.BIT.PCTYPE = RGB666_LOOSELY_PACKED_PCTYPE;
		break;
	case BGR666_LOOSELY_PACKED:
		P_DSI.VMCTR1.BIT.DT	= BGR666_LOOSELY_PACKED_DT;
		P_DSI.VMCTR1.BIT.PCTYPE = BGR666_LOOSELY_PACKED_PCTYPE;
		break;
	case RGB666:
		P_DSI.VMCTR1.BIT.DT	= RGB666_DT;
		P_DSI.VMCTR1.BIT.PCTYPE = RGB666_PCTYPE;
		break;
	case BGR666:
		P_DSI.VMCTR1.BIT.DT	= BGR666_DT;
		P_DSI.VMCTR1.BIT.PCTYPE = BGR666_PCTYPE;
		break;
	default:
		REG_DUMP("ERROR color_mode not supported %0x\n",
			lcd_cfg->color_mode);
		break;
	}

	P_DSI.VMCTR1.BIT.VCID = VMC_VCID;
	P_DSI.VMCTR1.BIT.DISPPOL = VMC_DISPPOL;
	P_DSI.VMCTR1.BIT.HSYNCPOL = VMC_HSYNCPOL;
	P_DSI.VMCTR1.BIT.VSYNCPOL = VMC_VSYNCPOL;

	P_DSI.VMCTR1.BIT.LCKSEL = P_DSI.LDDCKR.BIT.ICKSEL;
	if ((P_DSI.VMCTR2.BIT.BL1VSABM == 1) && (P_DSI.VMCTR2.BIT.VSEE == 1))
		P_DSI.VMCTR1.BIT.VSYNW = P_DSI.MLDVSYNR.BIT.VSYNP;
	else
		P_DSI.VMCTR1.BIT.VSYNW = 0;
	P_DSI.VMCTR1.BIT.ITLCEN = VMC_ITLCEN;

	switch (lcd_cfg->color_mode) {
	case RGB565:
	case BGR565:
		P_DSI.VMLEN1.BIT.RGBLEN = (panel_config->width * 16)/8;
		break;
	case RGB666:
	case BGR666:
		P_DSI.VMLEN1.BIT.RGBLEN = (panel_config->width * 18)/8;
		break;
	case RGB888:
	case BGR888:
	case RGB666_LOOSELY_PACKED:
	case BGR666_LOOSELY_PACKED:
		P_DSI.VMLEN1.BIT.RGBLEN = (panel_config->width * 24)/8;
		break;
	}
	if (P_DSI.VMCTR2.BIT.HSABM == 1) {
		P_DSI.VMLEN1.BIT.HSALEN = ((P_DSI.MLDHSYNR.BIT.HSYNW * 8 +
			P_DSI.MLDHAJR.BIT.HSYNWAJ) * panel_config->lanes)
			- 10;
	}

	P_DSI.VMLEN3.BIT.BL2LEN	= VML_BL2LEN;
	P_DSI.VMLEN3.BIT.BL1LEN	= VML_BL1LEN;

	P_DSI.VMLEN4.BIT.BL3LEN	= VML_BL3LEN;

	P_DSI.VMLEN2.BIT.HBPLEN	= 0;
	P_DSI.VMLEN2.BIT.HFPLEN = 0;
	if (P_DSI.VMCTR2.BIT.HBPBM == 1) {
		unsigned int htcn, hsyncp, hsyncw, h_total;
		htcn = P_DSI.MLDHCNR.BIT.HTCN*8+P_DSI.MLDHAJR.BIT.HTCNAJ;
		hsyncp = P_DSI.MLDHSYNR.BIT.HSYNP*8+P_DSI.MLDHAJR.BIT.HSYNPAJ;
		hsyncw = P_DSI.MLDHSYNR.BIT.HSYNW*8+P_DSI.MLDHAJR.BIT.HSYNWAJ;
		hsyncw = hsyncw*P_DSI.VMCTR2.BIT.HSEE;
		h_total = htcn - hsyncp - hsyncw;
		REG_DBG("htcn	: %d\n", htcn);
		REG_DBG("hsyncp : %d\n", hsyncp);
		REG_DBG("hsyncw : %d\n", hsyncw);
		REG_DBG("h_total : %d\n", h_total);

		P_DSI.VMLEN2.BIT.HBPLEN =
			(((h_total*dot_clock_ns)+hsbyte_ns_div)/hsbyte_ns)
			*panel_config->lanes-10;

		REG_DBG("P_DSI.VMLEN2.BIT.HBPLEN : %d\n",
			P_DSI.VMLEN2.BIT.HBPLEN);
	}
	if (P_DSI.VMCTR2.BIT.HFPBM == 1) {
		unsigned int hdcn, hsyncp, h_total;
		hsyncp = P_DSI.MLDHSYNR.BIT.HSYNP*8+P_DSI.MLDHAJR.BIT.HSYNPAJ;
		REG_DBG("hsyncp : %d\n", hsyncp);
		hdcn = P_DSI.MLDHCNR.BIT.HDCN*8+P_DSI.MLDHAJR.BIT.HDCN_ADPIX;
		hdcn = hdcn * hsbyte_ns * 3;
		REG_DBG("hdcn	: %d\n", hdcn);
		hdcn = hdcn/(dot_clock_ns * panel_config->lanes);
		REG_DBG("hdcn	: %d\n", hdcn);
		h_total = hsyncp - hdcn;
		REG_DBG("h_total : %d\n", h_total);
		P_DSI.VMLEN2.BIT.HFPLEN = ((h_total * dot_clock_ns *
		panel_config->lanes) + hsbyte_ns_div)/hsbyte_ns - 12;
		REG_DBG("P_DSI.VMLEN2.BIT.HFPLEN : %d\n",
			P_DSI.VMLEN2.BIT.HFPLEN);
	}
	REG_DBG("VMLEN2 : %0x\n", (int)P_DSI.VMLEN2.LONG);
	return 0;
}
int update_register(struct lcd_config *lcd_cfg,
	struct panel_info *panel_config, struct fb_board_cfg *pdata)
{
	if (panel_config->hsync < 8)
		panel_config->hsync = 8;
	if (panel_config->vsync < 1)
		panel_config->vsync = 1;

	calc_div_mult(lcd_cfg, panel_config, pdata);
	configuarion_setting(lcd_cfg, panel_config, pdata);
	calc_timeset(lcd_cfg, panel_config, pdata);
	panel_config->pixclock = pixclk;
	panel_config->lcd_if_param.dsitckcr    = P_DSI.DSITCKCR.LONG;
	panel_config->lcd_if_param.dsi0pckcr   = P_DSI.DSI0PCKCR.LONG;
	panel_config->lcd_if_param.dsi0phycr   = P_DSI.DSI0PHYCR.LONG;
	panel_config->lcd_if_param.sysconf     = P_DSI.SYSCONF.LONG;
	panel_config->lcd_if_param.timset0     = P_DSI.TIMSET0.LONG;
	panel_config->lcd_if_param.timset1     = P_DSI.TIMSET1.LONG;
	panel_config->lcd_if_param.dsictrl     = P_DSI.DSICTRL.LONG;
	panel_config->lcd_if_param.vmctr1      = P_DSI.VMCTR1.LONG;
	panel_config->lcd_if_param.vmctr2      = P_DSI.VMCTR2.LONG;
	panel_config->lcd_if_param.vmlen1      = P_DSI.VMLEN1.LONG;
	panel_config->lcd_if_param.vmlen2      = P_DSI.VMLEN2.LONG;
	panel_config->lcd_if_param.vmlen3      = P_DSI.VMLEN3.LONG;
	panel_config->lcd_if_param.vmlen4      = P_DSI.VMLEN4.LONG;
	panel_config->lcd_if_param.dtctr       = P_DSI.DTCTR.LONG;
	panel_config->lcd_if_param.mldhcnr     = P_DSI.MLDHCNR.LONG;
	panel_config->lcd_if_param.mldhsynr    = P_DSI.MLDHSYNR.LONG;
	panel_config->lcd_if_param.mldhajr     = P_DSI.MLDHAJR.LONG;
	panel_config->lcd_if_param.mldvlnr     = P_DSI.MLDVLNR.LONG;
	panel_config->lcd_if_param.mldvsynr    = P_DSI.MLDVSYNR.LONG;
	panel_config->lcd_if_param.mldmt1r     = P_DSI.MLDMT1R.LONG;
	panel_config->lcd_if_param.lddckr      = P_DSI.LDDCKR.LONG;
	panel_config->lcd_if_param.mlddckpat1r = P_DSI.MLDDCKPAT1R.LONG;
	panel_config->lcd_if_param.mlddckpat2r = P_DSI.MLDDCKPAT2R.LONG;
	panel_config->lcd_if_param.phytest = P_DSI.PHYTEST.LONG;

	panel_config->lcd_if_param_mask.dsitckcr    = LCD_MASK_DSITCKCR;
	panel_config->lcd_if_param_mask.dsi0pckcr   = LCD_MASK_DSI0PCKCR;
	panel_config->lcd_if_param_mask.dsi0phycr   = LCD_MASK_DSI0PHYCR;
	panel_config->lcd_if_param_mask.sysconf     = LCD_MASK_SYSCONF;
	panel_config->lcd_if_param_mask.timset0     = LCD_MASK_TIMSET0;
	panel_config->lcd_if_param_mask.timset1     = LCD_MASK_TIMSET1;
	panel_config->lcd_if_param_mask.dsictrl     = LCD_MASK_DSICTRL;
	panel_config->lcd_if_param_mask.vmctr1      = LCD_MASK_VMCTR1;
	panel_config->lcd_if_param_mask.vmctr2      = LCD_MASK_VMCTR2;
	panel_config->lcd_if_param_mask.vmlen1      = LCD_MASK_VMLEN1;
	panel_config->lcd_if_param_mask.vmlen2      = LCD_MASK_VMLEN2;
	panel_config->lcd_if_param_mask.vmlen3      = LCD_MASK_VMLEN3;
	panel_config->lcd_if_param_mask.vmlen4      = LCD_MASK_VMLEN4;
	panel_config->lcd_if_param_mask.dtctr       = LCD_MASK_DTCTR;
	panel_config->lcd_if_param_mask.mldhcnr     = LCD_MASK_MLDHCNR;
	panel_config->lcd_if_param_mask.mldhsynr    = LCD_MASK_MLDHSYNR;
	panel_config->lcd_if_param_mask.mldhajr     = LCD_MASK_MLDHAJR;
	panel_config->lcd_if_param_mask.mldvlnr     = LCD_MASK_MLDVLNR;
	panel_config->lcd_if_param_mask.mldvsynr    = LCD_MASK_MLDVSYNR;
	panel_config->lcd_if_param_mask.mldmt1r     = LCD_MASK_MLDMT1R;
	panel_config->lcd_if_param_mask.lddckr      = LCD_MASK_LDDCKR;
	panel_config->lcd_if_param_mask.mlddckpat1r = LCD_MASK_MLDDCKPAT1R;
	panel_config->lcd_if_param_mask.mlddckpat2r = LCD_MASK_MLDDCKPAT2R;
	panel_config->lcd_if_param_mask.phytest     = LCD_MASK_PHYTEST;
	REG_DUMP("\n");

	REG_DUMP("DSITCKCR:0x%0x\t\n", panel_config->lcd_if_param.dsitckcr);
	REG_DUMP("DSI0PCKCR:0x%0x\t\n", panel_config->lcd_if_param.dsi0pckcr);
	REG_DUMP("DSI0PHYCR:0x%0x\t\n", panel_config->lcd_if_param.dsi0phycr);
	REG_DUMP("SYSCONF:0x%0x\t\n", panel_config->lcd_if_param.sysconf);
	REG_DUMP("TIMSET0:0x%0x\t\n", panel_config->lcd_if_param.timset0);
	REG_DUMP("TIMSET1:0x%0x\t\n", panel_config->lcd_if_param.timset1);
	REG_DUMP("DSICTRL:0x%0x\t\n", panel_config->lcd_if_param.dsictrl);
	REG_DUMP("VMCTR1:0x%0x\t\n", panel_config->lcd_if_param.vmctr1);
	REG_DUMP("VMCTR2:0x%0x\t\n", panel_config->lcd_if_param.vmctr2);
	REG_DUMP("VMLEN1:0x%0x\t\n", panel_config->lcd_if_param.vmlen1);
	REG_DUMP("VMLEN2:0x%0x\t\n", panel_config->lcd_if_param.vmlen2);
	REG_DUMP("VMLEN3:0x%0x\t\n", panel_config->lcd_if_param.vmlen3);
	REG_DUMP("VMLEN4:0x%0x\t\n", panel_config->lcd_if_param.vmlen4);
	REG_DUMP("DTCTR:0x%0x\t\n", panel_config->lcd_if_param.dtctr);
	REG_DUMP("MLDHCNR:0x%0x\t\n", panel_config->lcd_if_param.mldhcnr);
	REG_DUMP("MLDHSYNR:0x%0x\t\n", panel_config->lcd_if_param.mldhsynr);
	REG_DUMP("MLDHAJR:0x%0x\t\n", panel_config->lcd_if_param.mldhajr);
	REG_DUMP("MLDVLNR:0x%0x\t\n", panel_config->lcd_if_param.mldvlnr);
	REG_DUMP("MLDVSYNR:0x%0x\t\n", panel_config->lcd_if_param.mldvsynr);
	REG_DUMP("MLDMT1R:0x%0x\t\n", panel_config->lcd_if_param.mldmt1r);
	REG_DUMP("LDDCKR:0x%0x\t\n", panel_config->lcd_if_param.lddckr);
	REG_DUMP("MLDDCKPAT1R:0x%0x\t\n",
		panel_config->lcd_if_param.mlddckpat1r);
	REG_DUMP("MLDDCKPAT2R:0x%0x\t\n",
		panel_config->lcd_if_param.mlddckpat2r);
	REG_DUMP("PHYTEST:0x%0x\t\n",
		panel_config->lcd_if_param.phytest);
	REG_DUMP("PIXCLOCK:%d\t\n",
		panel_config->pixclock);

	return 0;
}

unsigned int get_hfp_adjust(void)
{
	return hfp_adjust;
}

