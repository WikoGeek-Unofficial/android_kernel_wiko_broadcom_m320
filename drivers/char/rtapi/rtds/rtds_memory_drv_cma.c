/*
 * rtds_memory_drv_cma.c
 *	 RT CMA device driver function file.
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

#define pr_fmt(fmt) "rt-drm: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include "rtds_memory_drv_cma.h"

struct device	*rt_cma_dev[CMA_DEV_CNT];

static int rt_cma_drv_probe(struct platform_device *pdev);
static int rt_cma_drv_remove(struct platform_device *pdev);
static int rt_cma_drv_init(void);
static void rt_cma_drv_exit(void);

static struct platform_driver rt_cma_driver = {
	.probe		= rt_cma_drv_probe,
	.remove		= rt_cma_drv_remove,
	.driver		= {
		.name	= "rt_cma_dev",
		.owner	= THIS_MODULE,
	},
};

unsigned int rt_cma_drv_alloc(int id, unsigned int size, void **cpu_addr)
{
	unsigned int phys_addr = 0;
#ifdef CONFIG_CMA
	void *virt_addr;
	dma_addr_t dma_addr;

	virt_addr = dma_alloc_coherent(rt_cma_dev[id], size, &dma_addr,
			GFP_KERNEL);
	if (virt_addr == NULL) {
		pr_err("Failed cma alloc of (%#x) for id(%d)\n", size, id);
		*cpu_addr = NULL;
		goto out;
	}
	*cpu_addr = virt_addr;
	/* Assumption is device is not linked to iommu */
	phys_addr = (unsigned int)dma_addr;
out:
#else
	phys_addr = get_mem_resource("drm")->start;
	*cpu_addr = NULL;
#endif
	pr_debug("drm alloc id(%d) size(%#x) va(%p) pa(%#x)\n",
			id, size, *cpu_addr, phys_addr);
	return phys_addr;
}

int rt_cma_drv_free(int id, unsigned int size, unsigned int phys_addr,
		void *cpu_addr)
{
	pr_debug("drm free id(%d) size(%#x) va(%p) pa(%#x)\n",
			id, size, cpu_addr, phys_addr);
#ifdef CONFIG_CMA
	dma_free_coherent(rt_cma_dev[id], size, cpu_addr,
			(dma_addr_t)phys_addr);
#endif
	return 0;
}

static int rt_cma_drv_probe(struct platform_device *pdev)
{
	switch (pdev->id) {
	case OMX_MDL_ID:
	case DISPLAY_MDL_ID:
		rt_cma_dev[pdev->id] = &pdev->dev;
		pr_info("probe - id(%d) dev(%p)\n", pdev->id,
				rt_cma_dev[pdev->id]);
		break;
	default:
		pr_err("Illegal id(%d)\n", pdev->id);
		break;
	}
	return 0;
}

static int rt_cma_drv_remove(struct platform_device *pdev)
{
	return 0;
}

static int __init rt_cma_drv_init(void)
{
	return platform_driver_register(&rt_cma_driver);
}

static void __exit rt_cma_drv_exit(void)
{
	platform_driver_unregister(&rt_cma_driver);
	return;
}

module_init(rt_cma_drv_init);
module_exit(rt_cma_drv_exit);

