/*
 * drivers/video/r-mobile/panel/panel_info.h
 *
 * Copyright (C) 2014 Broadcom Corporation
 * All rights reserved.
 *
 */

#include "panel_common.h"
#include <video/sh_mobile_lcdc.h>

#ifndef __DISPLAY_DRV_H__
#define __DISPLAY_DRV_H__

#define CHAN_NUM 2

#define LCD_MASK_DSITCKCR	0x000000BF
#define LCD_MASK_DSI0PCKCR	0xFF3F703F
#define LCD_MASK_DSI0PHYCR	0x000F3F3F
#define LCD_MASK_SYSCONF	0x00000F0F
#define LCD_MASK_TIMSET0	0x7FFFF7F7
#define LCD_MASK_TIMSET1	0x003F03FF
#define LCD_MASK_DSICTRL	0x00000601
#define LCD_MASK_VMCTR1		0x00F3F03F
#define LCD_MASK_VMCTR2		0x07E2073B
#define LCD_MASK_VMLEN1		0xFFFFFFFF
#define LCD_MASK_VMLEN2		0xFFFFFFFF
#define LCD_MASK_VMLEN3		0xFFFFFFFF
#define LCD_MASK_VMLEN4		0xFFFF0000
#define LCD_MASK_DTCTR		0x00000002
#define LCD_MASK_MLDHCNR	0x07FF07FF
#define LCD_MASK_MLDHSYNR	0x001F07FF
#define LCD_MASK_MLDHAJR	0x07070707
#define LCD_MASK_MLDVLNR	0x1FFF1FFF
#define LCD_MASK_MLDVSYNR	0x001F1FFF
#define LCD_MASK_MLDMT1R	0x1F03FCCF
#define LCD_MASK_LDDCKR		0x0007007F
#define LCD_MASK_MLDDCKPAT1R	0x0FFFFFFF
#define LCD_MASK_MLDDCKPAT2R	0xFFFFFFFF
#define LCD_MASK_PHYTEST	0x000003CC

struct esd_data {
	unsigned char	esd_check; /* 0: disabled, 1 : gpio, 2: ID seq */
	struct delayed_work	esd_check_work; /* Work which checks for
						esd for every few secs */
	struct work_struct	esd_detect_work; /* This is queued from the
						interrupt context */
	struct mutex		esd_check_mutex; /* Mutex to access esd
						related data */
	struct completion	esd_te_irq_done; /* esd irq completion */
	struct workqueue_struct	*panel_esd_wq; /* workqueue to handle
					esd_check_work and esd_detect_work*/
	int			esd_detect_irq; /* IRQ No of ESD */
	int			esd_dur; /* For what amount of time
					ESD has to be checked periodicaly */
	int			esd_check_flag; /* During suspend and this
					flag is set to disabled and eanbled
					respectively. i.e. to stop work from
					rescheduling itself */
	unsigned int		esd_irq_portno; /* GPIO port no of ESD */
	bool			esd_irq_req; /* Indicates whether interrupt
					handler has been assigned or not */
};

struct panel_info {
	char		name[MAX_PNAME_SIZE];
	int		clk_src;
	struct regulator_bulk_data reg[2];
	struct platform_device *pdev;
	struct hw_reset_info rst;
	struct fb_info	*info[CHAN_NUM];
	unsigned long	panel_dsi_irq;
	struct hw_detect_info detect;
	struct resource	*lcdc_resources;
	/* state variables */
	bool		pwr_supp;
	atomic_t	dsi_read_ena;
	bool		init_now;
	unsigned char	is_disp_boot_init;
	unsigned short	pack_send_mode;
	unsigned short	col_fmt_bpp;
	unsigned long       hs_bps;
	unsigned long       lp_bps;
	unsigned char       lanes;
	unsigned int       cont_clock; /* stop or output HS clock */
	unsigned int	width;
	unsigned int	height;
	unsigned int	pixclock;
	bool		vmode;
	unsigned short	hbp, hsync, hfp; /* horizontal margins */
	unsigned short	vbp, vsync, vfp; /* vertical margins */
	unsigned short	phy_width; /* width in mm */
	unsigned short	phy_height; /* height in mm */
	unsigned char	*init_seq; /* initialization seq */
	unsigned char	*init_seq_min; /* init seq minimal */
	unsigned char	*suspend_seq; /* suspend seq */
	screen_disp_lcd_if	lcd_if_param;
	screen_disp_lcd_if	lcd_if_param_mask;
	unsigned char	*panel_id;
	phys_addr_t	fb_start; /* Physical adderss of FB */
	unsigned int	fb_size;
	unsigned int	virt_addr; /* FB mapped virt address */
	bool		verify_id;
	struct esd_data	esd_data;
};

/* TODO: Remove and make this file generic.
 * This should be taken care in upcoming patches, which are
 * being worked on.
 */
int s6e88a0_bklt_probe(struct panel_info *panel_data);
int s6e88a0_backlightinit(struct panel_info *panel_data);
int s6e88a0_bklt_resume(void);
int s6e88a0_bklt_suspend(void);
void s6e88a0_bklt_remove(void);

int panel_dsi_read(int, int, int, char*, struct panel_info *);
int panel_dsi_write(void *, unsigned char *, struct panel_info *);
extern void r8a7373_set_mode(bool vmode);
int update_register(struct lcd_config *lcd_cfg,
	struct panel_info *panel_config, struct fb_board_cfg *pdata);
unsigned int get_hfp_adjust(void);

#ifdef CONFIG_LDI_MDNIE_SUPPORT
void set_mdnie_enable(void);
#endif

#endif /* __DISPLAY_DRV_H__ */
