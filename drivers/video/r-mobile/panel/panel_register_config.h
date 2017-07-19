/*
 * drivers/video/broadcom/panel/panel_register_config.h
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
#ifndef __PANEL_REGISTER_CONFIG_H__
#define __PANEL_REGISTER_CONFIG_H__
/*************************************/
/*									 */
/*		DSI Structure				 */
/*                                   */
/*************************************/
struct st_dsi {
	union					/* Address = H'E615 0060 */
	{
		unsigned long LONG;		/* Long word Access      */
		struct				/* Bit Access		 */
		{
			unsigned DIV:6;	/* DIV   */
			unsigned:1;
			unsigned EXSRC:1;	/* Clock Source Select
					0: PLL circuit 1 output x 1/2
					1: PLL circuit 2 output	*/
			unsigned  CKSTP:1;	/* Clock Stop		*/
			unsigned:23;
		} BIT;
	} DSITCKCR;
	union			/* Address = H'E615 0064 */
	{
		unsigned long LONG;		/* Long word Access	*/
		struct				/* Bit Access		*/
		{
			unsigned  DIV:6;	/* DIV			*/
			unsigned:2;
			unsigned  DSIPCKSTP:1;	/* DSIPCKSTP		*/
			unsigned:3;
			unsigned  EXSRC:3;/* Clock Source Select (Set A)*/
					/* 000: PLL circuit1 output × 1/2*/
					/* 001: PLL circuit2 output	*/
					/* 010: main clock	*/
					/* 011: EXTAL2		*/
			unsigned:1;
			unsigned  DIVB:6;	/* DIVB			*/
			unsigned:2;
			unsigned  EXSRCB:3;	/* Clock Source Select(Set B)*/
			unsigned  SELMON0:1;	/* select division ratio*/
			unsigned  SELMON1:1;	/* select multiplication ratio
						Read value:
						0: set A is selected.
						1: set B is selected.
						Write value:
						0: Keep the selected set.
						1: Shit set		*/
			unsigned  CKCHG:1;	/* CKCHG		*/
			unsigned  CKCHGEN0:1;	/* div automatic shift
						 enable bit		*/
			unsigned  CKCHGEN1:1;	/* multi automatic
						shift enable	*/
		} BIT;
	} DSI0PCKCR;
	union			/* Address = H'E615 006C*/
	{
		unsigned long LONG;		/* Long word Access	*/
		struct				/* Bit Access		*/
		{
			unsigned  PLLDSA:6;	/* PLL mult ratio set A	*/
			unsigned:2;
			unsigned  PLLDSB:6;	/* PLL multratio set B	*/
			unsigned:1;
			unsigned  PLLE:1;	/* DSI0 PHY PLL enable bit*/
			unsigned  BYPSL:1;	/* PLL BYPSL manual cntl bit*/
			unsigned  PWDNL:1;	/* PLL PWDNL manual cntl bit*/
			unsigned:1;
			unsigned  MANUAL:1;	/* PLL Manual Control enable */
			unsigned  LOCKUPTIME:12;/* LOCKUPTIME of DSI0 PHY PLL*/
		} BIT;
	} DSI0PHYCR;
	union			/* Address = H'FEAB 0004 */
	{
		unsigned long LONG;		/* Long word Access	*/
		struct				/* Bit Access		*/
		{
			unsigned  LANEEN:4;	/* Number of lane	*/
			unsigned:4;
			unsigned  ECCCEN:1;	/* ECC check in LP reception.*/
			unsigned  CRCCEN:1;	/* CRC check in LP reception.*/
			unsigned  EOTEN:1;	/* EoT packet trans in HS    */
			unsigned  CLSTPM:1;	/* clock lane SW/HW cntl mode*/
			unsigned  CONEN:1;      /* Contention detection      */
			unsigned  L123ULPSEN:1; /* ULPS transition outputs   */
			unsigned:18;
		} BIT;
	} SYSCONF;
	union			/* Address = H'FEAB 0008     */
	{
		unsigned long LONG;		/* Long word Access          */
		struct				/* Bit Access	*/
		{
			unsigned  CKPPVL:3;	/* TCLK-PREPARE	*/
			unsigned:1;
			unsigned  CKTRVL:4;	/* TCLK_TRAIL*/
			unsigned  HSPPVL:3;	/* THS-PREPARE */
			unsigned:1;
			unsigned  HSTRVL:4;	/* THS-TRAIL*/
			unsigned  WKUPVL:15;	/* TWAKEUP*/
			unsigned:1;
		} BIT;
	} TIMSET0;
	union			/* Address = H'FEAB 007C*/
	{
		unsigned long LONG;		/* Long word Access*/
		struct				/* Bit Access	*/
		{
			unsigned  CKPREVL:4;	/* TCLK-PRE*/
			unsigned  CKPOSTVL:6;	/* TCLK-POST*/
			unsigned:6;
			unsigned  HSZEROVL:6;	/* THS-ZERO*/
			unsigned:10;
		} BIT;
	} TIMSET1;
	union			/* Address = H'FEAB 0030*/
	{
		unsigned long LONG;		/* Long word Access*/
		struct				/* Bit Access	*/
		{
			unsigned  CLHERQ:1;	/* clk lane transm to HSmode*/
			unsigned:1;
			unsigned  ULENRQ:1;	/* Requests a transition ULPS*/
			unsigned:1;
			unsigned  RSTRRQ:1;	/* Req a reset trigger tran*/
			unsigned:1;
			unsigned  HTTORC:1;/* Clears the HS tran timeout rst*/
			unsigned:1;
			unsigned  HSTTOEN:1;/*HS tran timeout tmr cnt ebl*/
			unsigned  LPRTOEN:1;/*LP recept timeout timer cnt enb*/
			unsigned  TATOEN:1;/*TA timeout timer count enable*/
			unsigned  PRTOSRQ:1;/*start reset timeout timer	*/
			unsigned:20;
		} BIT;
	} DSICTRL;
	union		/* Address = H'FEAB 4020*/
	{
		unsigned long LONG;		/* Long word Access*/
		struct				/* Bit Access	*/
		{
			unsigned  DT:6;		/* Set the data type
							for the VM o/p*/
			unsigned  VCID:2;	/* Set the virtual channel*/
			unsigned  DISPPOL:1;	/* polarize the Disp signal*/
			unsigned  HSYNCPOL:1;	/* polarize the Hync signal*/
			unsigned  VSYNCPOL:1;	/* polarize the Vsync signal*/
			unsigned:1;
			unsigned  PCTYPE:4;/* Select packet conversion format*/
			unsigned  LCKSEL:2;/* LCD Data Reception Clock Select*/
			unsigned:2;
			unsigned  VSYNW:4;/* Vertical Sync Signal Width	*/
			unsigned:4;
			unsigned  ITLCEN:1;/* interlace method.		*/
			unsigned:3;
		} BIT;
	} VMCTR1;
	union		/* Address = H'FEAB 4024*/
	{
		unsigned long LONG;	/* Long word Access	*/
		struct			/* Bit Access		*/
		{
			unsigned  BL3BM:1;/* State setting during BLLP3 period*/
			unsigned  BL2BM:1;/* State setting during BLLP2 period*/
			unsigned:1;
			unsigned  HFPBM:1;/* State setting during HFP period*/
			unsigned  HBPBM:1;/* State setting during HBP period*/
			unsigned  HSABM:1;/* State setting during HSA period*/
			unsigned:2;
			unsigned  BL3IEN:1;/*Command trans during BLLP3 period*/
			unsigned  BL2IEN:1;/*Command trans during BLLP2 period*/
			unsigned  BL1IEN:1;/*Command trans during BLLP1 period*/
			unsigned:6;
			unsigned  BL2E:1;  /* BLLP2 period*/
			unsigned:3;
			unsigned  HSAE:1;/* HSA period		*/
			unsigned  HSEE:1;/* Sets the HSE output	*/
			unsigned  VSEE:1;/* Sets the VSE output	*/
			unsigned  BL1VFPBM:1;/* State set during BLLP1 period*/
			unsigned  BL1VBPBM:1;/* State set during BLLP1 period*/
			unsigned  BL1VSABM:1;/* State set during BLLP1 period*/
			unsigned:5;
		} BIT;
	} VMCTR2;
	union		/* Address = H'FEAB 4028*/
	{
		unsigned long LONG;	/* Long word Access	*/
		struct			/* Bit Access		*/
		{
			unsigned  HSALEN:16;  /* Please set volume of
						data (byte) of Blanking Packet
						 at the HSA period	*/
			unsigned  RGBLEN:16;	/* Please set volume of
						data (byte) of the image
						data of one line.	*/
		} BIT;
	} VMLEN1;
	union		/* Address = H'FEAB 402C*/
	{
		unsigned long LONG;		/* Long word Access*/
		struct				/* Bit Access*/
		{
			unsigned  HFPLEN:16;	/* Set volume of data
				(byte) of Blanking Packet at the HFP period*/
			unsigned  HBPLEN:16;	/* Set volume of data (byte)
				of Blanking Packet at the HBP period	*/
		} BIT;
	} VMLEN2;
	union		/* Address = H'FEAB 4030*/
	{
		unsigned long LONG;		/* Long word Access	*/
		struct				/* Bit Access	*/
		{
			unsigned  BL2LEN:16;	/* Set volume of data (byte)
				of Blanking Packet at the bllp2 period	*/
			unsigned  BL1LEN:16;	/* Set volume of data (byte)
				of Blanking Packet at the bllp1 period	*/
		} BIT;
	} VMLEN3;
	union		/* Address = H'FEAB 4034*/
	{
		unsigned long LONG;		/* Long word Access*/
		struct				/* Bit Access	*/
		{
			unsigned:16;
			unsigned  BL3LEN:16;	/* Set volume of data (byte)
				of Blanking Packet at the bllp3 period	*/
		} BIT;
	} VMLEN4;
	union		/* Address = H'FEAB 4000*/
	{
		unsigned long LONG;		/* Long word Access*/
		struct				/* Bit Access	*/
		{
			unsigned  VMEN:1;     /* Enables video_mode*/
			unsigned  FLEN:1;	/* Specifies whether or not to
					transit to LPS every time HS packet
					transmission is completed	*/
			unsigned MTEN:1; /* Enables the all packets trans*/
			unsigned:1;
			unsigned STRST:1; /* The transmission control state
					machine is reset when setting  to 1 */
			unsigned:27;
		} BIT;
	} DTCTR;
	union		/* Address = H'FE94 1448*/
	{
		unsigned long LONG;	/* Long word Access	*/
		struct			/* Bit Access		*/
		{
			unsigned  HTCN:11;/* Horizontal Character Count*/
			unsigned:5;
			unsigned  HDCN:11;/* Horizontal Display Character Cnt*/
			unsigned:5;
		} BIT;
	} MLDHCNR;
	union		/* Address = H'FE94 144C*/
	{
		unsigned long LONG;	/* Long word Access	*/
		struct			/* Bit Access		*/
		{
			unsigned  HSYNP:11;/* Horizontal Character Count*/
			unsigned:5;
			unsigned  HSYNW:5;/* Horizontal Display Character Cnt*/
			unsigned:11;
		} BIT;
	} MLDHSYNR;
	union		/* Address = H'FE94 04A0*/
	{
		unsigned long LONG;	/* Long word Access	*/
		struct			/* Bit Access		*/
		{
			unsigned  HSYNPAJ:3;/* Specifies the adjusting size of
						the sync signal output point.*/
			unsigned:5;
			unsigned  HSYNWAJ:3;/* Specifies the adjusting width
						horizontal sync signal.*/
			unsigned:5;
			unsigned  HTCNAJ:3;/* Specifies the total horizontal
						adjusting size	*/
			unsigned:5;
			unsigned  HDCN_ADPIX:3;/*Specifies the display
					horizontal adjusting size*/
			unsigned:5;
		} BIT;
	} MLDHAJR;
	union		/* Address = H'FE94 1450*/
	{
		unsigned long LONG;	/* Long word Access	*/
		struct			/* Bit Access		*/
		{
			unsigned  VTLN:13;/* Vertical Line Total Count*/
			unsigned:3;
			unsigned  VDLN:13;/* Vertical Display Line Count*/
			unsigned:3;
		} BIT;
	} MLDVLNR;
	union		/* Address = H'FE94 0454*/
	{
		unsigned long LONG;	/* Long word Access	*/
		struct			/* Bit Access		*/
		{
			unsigned  VSYNP:13;/* Vsync output position */
			unsigned:3;
			unsigned  VSYNW:5; /* Vsync width  */
			unsigned:11;
		} BIT;
	} MLDVSYNR;
	union		/* Address = H'FE94 0418*/
	{
		unsigned long LONG;	/* Long word Access*/
		struct			/* Bit Access*/
		{
			unsigned  MIFTYP:4;/* LCD Module Set	*/
			unsigned:6;
			unsigned  M_3D:2;/* 3D display mode set	*/
			unsigned  IFM:1;/* Interface Mode Set	*/
			unsigned  YM:1;/* YCbCr output mode set	*/
			unsigned  CSM:1;/* Camera type SYNC output set*/
			unsigned  VSSM:1;/* VSYNC slide output set	*/
			unsigned  DWCNT:1;/* Dot Clock Control		*/
			unsigned  HSCNT:1;/* HSYNC Signal Output Control*/
			unsigned:6;
			unsigned  DAPOL:1;/* Display Data Polarity Select*/
			unsigned  DIPOL:1;/* Display Enable Polarity Select*/
			unsigned  DWPOL:1;/* Dot Clock Polarity Select*/
			unsigned  HPOL:1;/* H Sync Signal Polarity Select*/
			unsigned  VPOL:2;/* V Sync Signal Polarity Select*/
			unsigned:1;
			unsigned  SDL:1; /* Synchronous LCDC0 and LCDC1*/
		} BIT;
	} MLDMT1R;
	union		/* Address = H'FE94 0400*/
	{
		unsigned long LONG;	/* Long word Access	*/
		struct			/* Bit Access		*/
		{
			unsigned  DCKPAT1:28;/* Dot Clock Pattern 1*/
			unsigned:3;
		} BIT;
	} MLDDCKPAT1R;
	union			/* Address = H'FE94 0404*/
	{
		unsigned long LONG;	/* Long word Access	*/
		struct			/* Bit Access		*/
		{
			unsigned  DCKPAT1:32;/* Dot Clock Pattern 1*/
		} BIT;
	} MLDDCKPAT2R;
	union			/* Address = H'FE94 0410*/
	{
		unsigned long LONG;	/* Long word Access	*/
		struct			/* Bit Access		*/
		{
			unsigned  MDCDR:6;/* Clock Division Ratio*/
			unsigned  MOSEL:1;/* LCD Output Clock Select*/
			unsigned:9;
			unsigned  ICKSEL:3;/* Input Clock Select
					000: M3 clock.
					001: MIPI-DSI 8byte clock
					010: MIPI-DSI 6byte clock
					011: HDMI clock
					101: External terminal */
			unsigned:13;
		} BIT;
	} LDDCKR;
union		/* Address = H'FE94 0410 */
	{
		unsigned long LONG;		/* Long word Access*/
		struct					/* Bit Access*/
		{
			unsigned:2;
			unsigned  CPCURNT:2;/* CP current setting
						0 : normal
						1 : x2
						2 : x3
						3 : x4 */
			unsigned:2;
			unsigned  LPFRESI:2;/* Loop filter resistance setting
						0 : normal
						1 : -1 step
						2 : +2 steps
						3 : +1 step */
			unsigned  VREGUOVOL:1;
				/* VCO regulator output voltage*/
			unsigned  PCREGUOVOL:1;
				/* PFD/CP regulator output voltage
						0 : normal
						1 : boost mode*/
			unsigned:22;
		} BIT;
	} PHYTEST;
};
#define REG_DUMP pr_info
#define REG_DBG pr_debug

#define EXTAL1	26000000
#define EXTAL2  48000000
#define PLL1	(EXTAL1 * 48)
#define PLL2	((EXTAL1 / 2) * 54)
#define M3_LCDC	156000000
#define LOCKUP_TIME 680
#define MAIN_CLK EXTAL1

#define PLL_DIV_MIN 11 /* Refer 1.6.35 HM */
#define PLL_DIV_MAX 62 /* Refer 1.6.35 HM */
#define PLL_MULT_MIN 12 /* Refer 1.6.37 HM */
#define PLL_MULT_MAX 33 /* Refer 1.6.37 HM */
#define PLL1_2 (PLL1 >> 1)
#define FPS 60

/* Clock Source Select */
#define EXSRC_PLL1 0
#define EXSRC_PLL2 1
#define EXSRC_MAIN_CLK 2
#define EXSRC_EXTAL2 3

#define DSIT_EXSRC EXSRC_PLL1 /* Clock Source Select */
#define DSIT_LPCLK 80 /* Target MHz */

#define DSI0PCKCR_EXSRC EXSRC_PLL1
#define DSI0PCKCR_EXSRCB EXSRC_PLL1
#define PHY_MANUAL 0
#define PHY_BYPSL 0
#define PHY_PWDNL 0

#define LANE_ONE 0x1
#define LANE_TWO 0x3
#define LANE_THREE 0x7
#define LANE_FOUR 0xF
#define STOP_OUT_HS_CLK 0
#define OUT_HS_CLK 1
#define SC_ECCCEN 1
#define SC_CRCCEN 1
#define SC_EOTEN 1
#define SC_CONEN 0
#define SC_L123ULPSEN 0

#define DTC_FLEN 1
#define DTC_MTEN 1
#define DTC_STRST 0

#define CLHERQ_TO_HS 1
#define CLHERQ_FROM_HS 0
#define DSIC_ULENRQ  0
#define DSIC_RSTRRQ  0
#define DSIC_HTTORC  0
#define DSIC_HSTTOEN  0
#define DSIC_LPRTOEN  0
#define DSIC_TATOEN  0
#define DSIC_PRTOSRQ  0

#define MLD_M_3D  0
#define MLD_IFM   0
#define MLD_YM   0
#define MLD_CSM   0
#define MLD_VSSM   0
#define MLD_DWCNT   0
#define MLD_HSCNT   0
#define MLD_DAPOL   0
#define MLD_DIPOL   0
#define MLD_DWPOL   1
#define MLD_HPOL   0
#define MLD_VPOL   0
#define MLD_SDL   0
#define MLD_MIFTYP 0xB /* Need to take from Table 11.5 */

#define MLDDCKPAT1R_DCKPAT1   0
#define MLDDCKPAT2R_DCKPAT1   0

#define ICKSEL_M3_CLK 0
#define ICKSEL_8BYTE_CLK 1
#define ICKSEL_6BYTE_CLK 2
#define ICKSEL_HDMI_CLK 3
#define ICKSEL_EXT_CLK 4

#define CKR_MDCDR  0
#define CKR_MOSEL  1

#define VML_BL2LEN	 0
#define VML_BL1LEN	 0
#define VML_BL3LEN	 0

#define RGB888_DT 0x3E
#define BGR888_DT 0x3E
#define RGB565_DT 0x0E
#define BGR565_DT 0x0E
#define RGB666_LOOSELY_PACKED_DT 0x2E
#define BGR666_LOOSELY_PACKED_DT 0x2E
#define RGB666_DT 0x1E
#define BGR666_DT 0x1E

#define RGB888_PCTYPE 0
#define BGR888_PCTYPE 8
#define RGB565_PCTYPE 1
#define BGR565_PCTYPE 9
#define RGB666_LOOSELY_PACKED_PCTYPE 2
#define BGR666_LOOSELY_PACKED_PCTYPE 10
#define RGB666_PCTYPE 3
#define BGR666_PCTYPE 11

#define VMC_VCID  0
#define VMC_DISPPOL  0
#define VMC_HSYNCPOL  0
#define VMC_VSYNCPOL  0
#define VMC_ITLCEN  0

#endif
