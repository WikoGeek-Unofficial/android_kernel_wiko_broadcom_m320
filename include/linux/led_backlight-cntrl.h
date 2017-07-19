/*
*Copyright 2013 Broadcom Corporation.  All rights reserved.
*
* *       @file   include/linux/led_backlight-cntrl.h 
* *
* * Unless you and Broadcom execute a separate written software license agreement
* * governing use of this software, this software is licensed to you under the
* * terms of the GNU General Public License version 2, available at
* * http://www.gnu.org/copyleft/gpl.html (the "GPL").
* *
* * Notwithstanding the above, under no circumstances may you combine this
* * software in any way with any other Broadcom software provided under a license
* * other than the GPL, without Broadcom's express prior written consent.
* *******************************************************************************/

/*
 *  * Generic backlight driver data
 *   * - see drivers/video/backlight/led_backlight-cntrl.h
 *    */
#ifndef __LINUX_BACKLIGHT_CTRL_H
#define __LINUX_BACKLIGHT_CTRL_H

struct platform_led_backlight_data {
        unsigned int max_brightness;
	unsigned int dft_brightness;
	const char *tpu_channel;
};
#endif

