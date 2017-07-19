/*
 * rtds_memory_drv_cma.h
 *	 RT CMA driver function file.
 *
 * Copyright (C) 2013 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __RTDS_MEMORY_DRV_CMA_H__
#define __RTDS_MEMORY_DRV_CMA_H__

enum {
	OMX_MDL_ID = 0,
	DISPLAY_MDL_ID,
	CMA_DEV_CNT
};

unsigned int rt_cma_drv_alloc(int id, unsigned int size, void **cpu_addr);
int rt_cma_drv_free(int id, unsigned int size, unsigned int phys_addr,
		void *cpu_addr);

#endif /* __RTDS_MEMORY_DRV_CMA_H__ */

