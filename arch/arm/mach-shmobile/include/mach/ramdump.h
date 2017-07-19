/*
 * arch/arm/mach-shmobile/include/mach/rmc_ramdump.h
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

#ifndef __RMC_RAMDUMP_H__
#define __RMC_RAMDUMP_H__

#include <linux/types.h>
#include <linux/init.h>
#include <mach/pm.h>
#include "../../pmRegisterDef.h"

enum hw_register_width {
	HW_REG_8BIT	= 1,
	HW_REG_16BIT	= 2,
	HW_REG_32BIT	= 4,
};
struct hw_register_range {
	phys_addr_t start;
	/* End address is included in the reading range */
	phys_addr_t end;
	enum hw_register_width width;
	/* todo: at the moment addresses are incremented by 4 bytes
	 * but int he future there might be need to increment by byte. */
	unsigned int inc;
	/* Power area which needs to be on before reading registers
	 * This is PTSR register bit mask */
	unsigned int pa;
	/* This one of the module stop registers */
	void __iomem *msr;
	/* This is module stop register bit mask */
	unsigned int msb;
};

#define DEFINE_RES_RAMDUMP(_start, _end, _width, _pa, _msr, _msb)	\
	{								\
		.start = (_start),					\
		.end = (_end),						\
		.width = (_width),					\
		.pa = (_pa),						\
		.msr = (_msr),						\
		.msb = (_msb),						\
	}

/* can we find these from some header? */
#define MSTPST525 (1 << 25)
#define MSTO007 (1 << 7)


/* Hardware-Register info */
struct	hw_register_dump {
	ulong syscpstr;
	ulong rwdt_condition[3];
	ulong swdt_condition[3];
	ulong dbg_setting[3];
	ulong cmt15_condition[4];
	ulong cpg_setting_00[129];
	ulong cpg_setting_01[97];
	ulong sysc_setting_00_0[32];
	ulong sysc_setting_00_1[32];
	ulong sysc_setting_01[32];
	ulong sysc_rescnt[3];
	ulong intc_sys_info[4];
	ulong sutc_condition[4];
	ulong intc_bb_info[2];
	ulong iic0_setting[3];
	ulong iicb_setting[3];
	ulong gic_setting[2];
	ulong sbsc_setting_00[72];
	ulong sbsc_setting_01[17];
	ulong sbsc_mon_setting;
	ulong sbsc_phy_setting_00[2];
	ulong sbsc_phy_setting_01;
	ulong sbsc_phy_setting_02[28];
	ulong ipmmu_setting[64];
};

struct ramdump_plat_data {
	unsigned long reg_dump_base;
	size_t reg_dump_size;
	/* this is the size for each core */
	size_t core_reg_dump_size;
	u32 num_resources;
	struct hw_register_range *hw_register_range;
};

#ifdef CONFIG_SH_RAMDUMP
extern void register_ramdump_split(const char *name, phys_addr_t start,
		phys_addr_t end);
#else
static inline void register_ramdump_split(const char *name,
		phys_addr_t start, phys_addr_t end) {}
#endif

#endif /*__RMC_RAMDUMP_H__*/
