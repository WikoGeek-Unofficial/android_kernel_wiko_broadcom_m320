/*
 * arch/arm/mach-shmobile/include/mach/setup-u2ion.h
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
#ifndef __SETUP_U2ION_H
#define __SETUP_U2ION_H

#if defined(CONFIG_ION)
void __init u2_add_ion_device(void);
#else
static inline void u2_add_ion_device(void) {}
#endif

#endif /* __SETUP_U2ION_H */
