/*
 * rt_boot.c
 *		booting rt_cpu.
 *
 * Copyright (C) 2012-2013 Renesas Electronics Corporation
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

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/platform_data/rt_boot_pdata.h>
#include <mach/memory.h>
#include "log_kernel.h"
#include "rt_boot_drv.h"
#include "rt_boot_local.h"
#include <linux/of.h>

MODULE_AUTHOR("Renesas Electronics Corp.");
MODULE_DESCRIPTION("Device Driver (RT Boot)");


#define OS_KIND_LINUX	(0x50)
#define DRIVER_NAME		"rtboot"

/* prototype */
static int rtboot_init(void);
static void rtboot_exit(void);


static struct miscdevice g_device;

static const struct file_operations g_fops = {
	.owner          = THIS_MODULE,
};

/* boot_info */
struct rt_boot_info g_rtboot_info;

static int parse_dt_data(struct rt_boot_platform_data *pdata,
		struct device_node *rt_node)
{
	int ret;
	int out;

	if (!rt_node->child) {
		MSG_ERROR(
		"[RTBOOTK]:screen 0 not found\n");
		return -1;
	}

	ret = of_property_read_u32(rt_node->child,
			"renesas,height", &out);
	if (ret) {
		MSG_ERROR(
		"[RTBOOTK]:error in getting height\n");
		return -1;
	}

	pdata->screen0.height = out;
	ret = of_property_read_u32(rt_node->child,
			"renesas,width", &out);
	if (ret) {
		MSG_ERROR(
		"[RTBOOTK]:error in getting width\n");
		return -1;
	}

	pdata->screen0.width = out;
	ret = of_property_read_u32(rt_node->child,
			"renesas,stride", &out);
	if (ret) {
		MSG_ERROR(
		"[RTBOOTK]:error in getting stride\n");
		return -1;
	}

	pdata->screen0.stride = out;
	ret = of_property_read_u32(rt_node->child,
			"renesas,mode", &out);
	if (ret) {
		MSG_ERROR(
		"[RTBOOTK]:error in getting mode\n");
		return -1;
	}

	pdata->screen0.mode = out;
	rt_node = of_get_next_child(rt_node, rt_node->child);
	if (!rt_node) {
		MSG_ERROR("[RTBOOTK]DT:screen1 not found\n");
		return 0;
	}

	ret = of_property_read_u32(rt_node,
			"renesas,height", &out);
	if (ret) {
		MSG_ERROR(
		"[RTBOOTK]:err to get height for screen1\n");
		return -1;
	}

	pdata->screen1.height = out;
	ret = of_property_read_u32(rt_node,
			"renesas,width", &out);
	if (ret) {
		MSG_ERROR(
		"[RTBOOTK]:err to get width for screen1\n");
		return -1;
	}
	pdata->screen1.width = out;
	ret = of_property_read_u32(rt_node,
			"renesas,stride", &out);
	if (ret) {
		MSG_ERROR(
		"[RTBOOTK]:err to get stride for screen1\n");
		return -1;
	}
	pdata->screen1.stride = out;
	ret = of_property_read_u32(rt_node,
			"renesas,mode", &out);
	if (ret) {
		MSG_ERROR(
		"[RTBOOTK]:err to get mode for screen 1\n");
		return -1;
	}
	pdata->screen1.mode = out;

	return 0;
}

static const struct of_device_id rt_boot_of_match[] = {
	{.compatible = "renesas,rt-boot",},
	{},
};

MODULE_DEVICE_TABLE(of, rt_boot_of_match);

static int rt_boot_probe(struct platform_device *pd)
{
	int ret;
	unsigned int bootaddr = 0;
	struct rt_boot_platform_data *pdata;
#ifdef SECURE_BOOT_ENABLE
	uint32_t phys_cert_addr;
	uint32_t cert_size;
#endif

	MSG_MED("[RTBOOTK]IN |[%s]\n", __func__);

	pdata = (struct rt_boot_platform_data *)pd->dev.platform_data;

	if (!pdata) {
		pdata = devm_kzalloc(&pd->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			MSG_ERROR("[RTBOOTK]: failed to allocate memory\n");
			return 1;
		}
	}

	if (pd->dev.of_node) {
		if (parse_dt_data(pdata, pd->dev.of_node)) {
			MSG_ERROR("[RTBOOTK] failed to parse dt data\n");
			return 1;
		}
		pd->dev.platform_data = pdata;
	} else {
		MSG_ERROR("[RTBOOTK]:no device tree\n");
	}

	memset(&g_rtboot_info, 0, sizeof(g_rtboot_info));

	g_device.name  = DRIVER_NAME;
	g_device.fops  = &g_fops;
	g_device.minor = MISC_DYNAMIC_MINOR;

	ret = misc_register(&g_device);
	if (0 != ret) {
		MSG_ERROR("[RTBOOTK]   |misc_register failed ret[%d]\n", ret);
		MSG_HIGH("[RTBOOTK]OUT|[%s] ret = 1\n", __func__);
		return 1;
	}

	do_ioremap_register();

	if (0 != read_rt_image(&bootaddr, pdata)) {
		MSG_ERROR("[RTBOOTK]   |read_rt_image() Error\n");
		do_iounmap_register();
		ret = misc_deregister(&g_device);
		if (0 != ret)
			MSG_ERROR("[RTBOOTK]   |misc_deregister failed ret[%d]\n", ret);
		MSG_HIGH("[RTBOOTK]OUT|[%s] ret = 1\n", __func__);
		return 1;
	}

#ifdef SECURE_BOOT_ENABLE
	phys_cert_addr = (get_mem_resource("tune_up_values")->start +
			g_rtboot_info.image_size + 0x00001000) & (0xFFFFF000);
	cert_size = read_rt_cert(phys_cert_addr);
	if (cert_size == 0) {
		MSG_ERROR("[RTBOOTK]   |read_rt_cert failed\n");
		do_iounmap_register();
		ret = misc_deregister(&g_device);
		if (0 != ret)
			MSG_ERROR("[RTBOOTK]   |misc_deregister failed ret[%d]\n", ret);
		MSG_HIGH("[RTBOOTK]OUT|[%s] ret = 1\n", __func__);
		return 1;
	}

	ret = sec_hal_memcpy((uint32_t)g_rtboot_info.boot_addr,
			(uint32_t)(get_mem_resource("tune_up_values")->start,
			(uint32_t)g_rtboot_info.image_size);
	if (ret == SEC_HAL_CMN_RES_OK) {
		ret = sec_hal_authenticate(phys_cert_addr, cert_size, NULL);
		if (ret != SEC_HAL_CMN_RES_OK)
			MSG_ERROR("[RTBOOTK]   |sec_hal_authenticate ret[%d], phys_cert_addr[0x%08x], cert_size[%d]\n",
				ret, phys_cert_addr, cert_size);
	}

	if (SEC_HAL_CMN_RES_OK != ret) {
		MSG_ERROR("[RTBOOTK]   |RT boot secure error\n");
		MSG_ERROR("[RTBOOTK]   |boot_addr[0x%08x], image_size[%d]\n", g_rtboot_info.boot_addr, g_rtboot_info.image_size);
		do_iounmap_register();
		ret = misc_deregister(&g_device);
		if (0 != ret)
			MSG_ERROR("[RTBOOTK]   |misc_deregister failed ret[%d]\n", ret);
		MSG_HIGH("[RTBOOTK]OUT|[%s] ret = 1\n", __func__);
		return 1;
	}
	MSG_ERROR("[RTBOOTK]   |secure boot on\n");
#else
	MSG_LOW("[RTBOOTK]   |write_rt_imageaddr bootaddr[%x]\n", bootaddr);

	MSG_LOW("[RTBOOTK]   |write_rt_imageaddr start\n");
	write_rt_imageaddr(bootaddr);
#endif
	{
		unsigned long long tim;
		unsigned long __iomem *addr_status;
		tim = local_clock();
		addr_status = ioremap(g_rtboot_info.command_area_address, 12);
		__raw_writel(upper_32_bits(tim), addr_status + 1);
		__raw_writel(lower_32_bits(tim), addr_status + 2);
		iounmap(addr_status);
	}
	MSG_LOW("[RTBOOTK]   |stop_rt_interrupt start\n");
	stop_rt_interrupt();
	MSG_LOW("[RTBOOTK]   |init_rt_register start\n");
	init_rt_register();
	MSG_LOW("[RTBOOTK]   |write_os_kind start\n");
	write_os_kind(OS_KIND_LINUX);
	MSG_LOW("[RTBOOTK]   |start_rt_cpu start\n");
	start_rt_cpu();
	MSG_LOW("[RTBOOTK]   |wait_rt_cpu start\n");
	ret = wait_rt_cpu(MAX_POLLING_COUNT);

	if (0 != ret) {
		MSG_ERROR("[RTBOOTK]   |RT boot error\n");
		{
			unsigned long __iomem *addr_status;
			addr_status = ioremap(g_rtboot_info.command_area_address, 4);
			if (addr_status) {
				MSG_ERROR("[RTBOOTK]   |RT boot status [%d]\n",
						__raw_readl(addr_status));
				iounmap(addr_status);
			}
		}
		do_iounmap_register();
		ret = misc_deregister(&g_device);
		if (0 != ret)
			MSG_ERROR("[RTBOOTK]   |misc_deregister failed ret[%d]\n", ret);
		MSG_HIGH("[RTBOOTK]OUT|[%s] ret = 1\n", __func__);
		return 1;
	}

	MSG_LOW("[RTBOOTK]   |write_req_comp start\n");
	write_req_comp();

	MSG_MED("[RTBOOTK]OUT|[%s] ret = 0\n", __func__);

	return 0;
}

static void rtboot_exit(void)
{
	int ret;

	MSG_MED("[RTBOOTK]IN |[%s]\n", __func__);

	do_iounmap_register();

	ret = misc_deregister(&g_device);
	if (0 != ret)
		MSG_ERROR("[RTBOOTK]   |misc_deregister failed ret[%d]\n", ret);

	MSG_MED("[RTBOOTK]OUT|[%s]\n", __func__);
}


int rtboot_get_section_header(struct rt_boot_info *info)
{
	int ret = 1;

	MSG_MED("[RTBOOTK]IN |[%s]\n", __func__);

	if (info) {
		*info = g_rtboot_info;
		ret = 0;
	}

	MSG_MED("[RTBOOTK]OUT|[%s]\n", __func__);

	return ret;
}
EXPORT_SYMBOL(rtboot_get_section_header);


static struct platform_driver rt_boot_pdriver = {
	.probe	= rt_boot_probe,
	.driver	= {
		.name	= "rt_boot",
		.owner	= THIS_MODULE,
		.of_match_table = rt_boot_of_match,
	},
};

static int rtboot_init(void)
{
	int ret = platform_driver_register(&rt_boot_pdriver);
	if (ret) {
		pr_err("%s: platform_driver_unregister failed  %d\n",
				__func__, ret);
	}
	return ret;
}

module_init(rtboot_init);
module_exit(rtboot_exit);


