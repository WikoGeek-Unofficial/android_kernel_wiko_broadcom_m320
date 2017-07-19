/*
 * include/linux/r-mobile/r_mobile_ion.h
 *     This file is part of r_mobile ion implementation.
 *
 * Copyright (C) 2011-2013 Renesas Electronics Corporation
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

#ifndef __R_MOBILE_ION_H__
#define __R_MOBILE_ION_H__

#include <linux/ion.h>

/**
 *  Acquire the ION device. It is created during ION probe
 *  and the global handle is shared with clients.
 *  It is client's responsibility to check for valid ION device
 *  and make sure that the acquisition of ION device is done
 *  after ION device probe is over.
 *
 *  Returns the ION device
 */
struct ion_device *r_mobile_ion_device_get(void);

/**
 *  Get the RT mapped (iommu) address for the ION buffer. The mapping
 *  is created when the buffer is created.
 *  The mapping gets destroyed when the buffer is destroyed.
 *
 *  Returns the RT mapped address
 **/
unsigned int r_mobile_ion_rt_addr_get(struct ion_client *client,
		struct ion_handle *handle);

#endif	/* __R_MOBILE_ION_H__ */

