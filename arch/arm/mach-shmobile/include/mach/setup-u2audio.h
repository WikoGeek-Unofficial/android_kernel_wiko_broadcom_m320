/*
 * arch/arm/mach-shmobile/include/mach/setup-u2audio.h
 *
 * Copyright (C) 2013 Renesas Mobile Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */
#ifndef __ASM_ARCH_SETUP_U2AUDIO_H
#define __ASM_ARCH_SETUP_U2AUDIO_H

#include <linux/types.h>

#define	DEVICE_NONE		0
#define	DEVICE_EXIST	1

#if defined(CONFIG_SND_SOC_SH4_FSI)
void u2audio_init(int aad_gpio_port);
#else
static inline void u2audio_init(void) {}
#endif

#endif /* __ASM_ARCH_SETUP_U2AUDIO_H */
