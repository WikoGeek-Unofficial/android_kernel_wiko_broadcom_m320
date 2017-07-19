/*
 * arch/arm/mach-shmobile/include/mach/dev-touchpanel.h
 *
 * Copyright (C) 2013 Renesas Mobile Corporation
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
#ifndef __ASM_MACH_DEV_TSP_H
#define __ASM_MACH_DEV_TSP_H

extern void __init tsp_cyttsp4_init(void);
extern void __init tsp_ektf2227_init(void);
extern void  __init tsp_synaptics2202_init(void);
extern unsigned int cyttsp4_enable_gpio;
extern unsigned int cyttsp4_enable_gpio_rev00;
extern unsigned int int_gpio;

#endif /* __ASM_MACH_DEV_TSP_H */
