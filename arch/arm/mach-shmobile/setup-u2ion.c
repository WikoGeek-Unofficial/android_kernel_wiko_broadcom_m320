/*
 * arch/arm/mach-shmobile/setup-u2ion.c
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
#include <asm/sizes.h>
#include <linux/ion.h>
#include <linux/platform_device.h>
#include <mach/memory.h>
#include <mach/setup-u2ion.h>

/* The heap-ids are hard-coded to heap type because the sgx driver
 * userspace assumes so. */
static struct ion_platform_heap u2_ion_heap[] = {
	{
		.type = ION_HEAP_TYPE_SYSTEM,
		.id = ION_HEAP_TYPE_SYSTEM,
		.name = "ion-system",
	},
#ifdef CONFIG_ION_R_MOBILE_USE_BOOTLOADER_AREA
	{
		.type = ION_HEAP_TYPE_CARVEOUT,
		.id = ION_HEAP_TYPE_CARVEOUT,
		.name = "ion-carveout",
	},
#endif
};

static struct ion_platform_data u2evm_ion_data = {
	.nr = ARRAY_SIZE(u2_ion_heap),
	.heaps = u2_ion_heap,
};

static struct platform_device u2evm_ion_device = {
	.name = "ion-r-mobile",
	.id = -1,
	.dev = {
		.platform_data = &u2evm_ion_data,
	},
};

void __init u2_add_ion_device(void)
{
	int ret, i;
	struct ion_platform_heap *heap;
	struct resource *rp;
	rp = get_mem_resource("bootloader");
	if (rp) {
		for (i = 0; i < ARRAY_SIZE(u2_ion_heap); i++) {
			heap = &u2_ion_heap[i];
			if (!strcmp(heap->name , "ion-carveout")) {
				heap->base = rp->start;
				heap->size = resource_size(rp);
			}
		}
	}

	ret = platform_device_register(&u2evm_ion_device);
	if (ret)
		pr_err("%s: failed to register ion device %d\n", __func__, ret);
}

