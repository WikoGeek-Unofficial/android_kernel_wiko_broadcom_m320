/******************************************************************************/
/*                                                                            */
/*  Copyright 2014  Broadcom Corporation                                      */
/*                                                                            */
/*     Unless you and Broadcom execute a separate written software license    */
/*     agreement governing use of this software, this software is licensed    */
/*     to you under the terms of the GNU General Public License version 2     */
/*     (the GPL), available at                                                */
/*                                                                            */
/*          http://www.broadcom.com/licenses/GPLv2.php                        */
/*                                                                            */
/*     with the following added to such license:                              */
/*                                                                            */
/*     As a special exception, the copyright holders of this software give    */
/*     you permission to link this software with independent modules, and     */
/*     to copy and distribute the resulting executable under terms of your    */
/*     choice, provided that you also meet, for each linked independent       */
/*     module, the terms and conditions of the license of that module. An     */
/*     independent module is a module which is not derived from this          */
/*     software.  The special exception does not apply to any modifications   */
/*     of the software.                                                       */
/*                                                                            */
/*     Notwithstanding the above, under no circumstances may you combine      */
/*     this software in any way with any other Broadcom software provided     */
/*     under a license other than the GPL, without Broadcom's express prior   */
/*     written consent.                                                       */
/*                                                                            */
/******************************************************************************/
#ifndef __SH_CPUFREQ_H__
#define __SH_CPUFREQ_H__

/* cpufreq log levels */
#define SH_CPUFREQ_PRINT_ERROR		(1U << 0)
#define SH_CPUFREQ_PRINT_WARNING	(1U << 1)
#define SH_CPUFREQ_PRINT_UPDOWN		(1U << 2)
#define SH_CPUFREQ_PRINT_INIT		(1U << 3)
#define SH_CPUFREQ_PRINT_VERBOSE	(1U << 4)

struct sh_hp_thresholds {
		unsigned int thresh_plugout;
		unsigned int thresh_plugin;
};

struct sh_plat_hp_data {
	unsigned int in_samples;
	unsigned int out_samples;
	unsigned int es_samples;
	struct sh_hp_thresholds thresholds[CONFIG_NR_CPUS];
};

struct sh_cpufreq_plat_data {
	struct sh_plat_hp_data *hp_data;
};

#endif /*__SH_CPUFREQ_H__*/
