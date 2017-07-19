/*
 * arch/arm/mach-shmobile/setup-u2csi2.c
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
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>

#include <media/sh_mobile_csi2.h>

#include <mach/irqs.h>
#include <mach/setup-u2csi2.h>
#include <mach/r8a7373.h>

struct sh_csi2_log_data {
	int voltage[4];
	u32 cpg[13];
	u32 sysc[1];
	u32 gpio_dr[3];
	u8 gpio_cr[5];
};

static struct sh_csi2_log_data csi2_log_data;

static void csi2_camera_voltage_log(void *data, int first, int main_cam)
{
	struct sh_csi2_log_data *log = data;

	printk(KERN_ALERT "%s first=%d main_cam=%d\n",
		__func__, first, main_cam);

	if (first) {
		struct regulator *regulator;

		/* Voltage */
		regulator = regulator_get(NULL, "vmipi");
		if (IS_ERR(regulator)) {
			pr_err("%s:err vmipi regulator_get ret = %ld\n",
				 __func__ , PTR_ERR(regulator));
			return;
		}
		log->voltage[0] = regulator_get_voltage(regulator);
		regulator_put(regulator);

		regulator = regulator_get(NULL, "cam_sensor_a");
		if (IS_ERR(regulator)) {
			pr_err("%s:err cam_sensor_a regulator_get ret = %ld\n",
				__func__ , PTR_ERR(regulator));
			return;
		}
		log->voltage[1] = regulator_get_voltage(regulator);
		regulator_put(regulator);

		regulator = regulator_get(NULL, "cam_sensor_io");
		if (IS_ERR(regulator)) {
			pr_err("%s:err cam_sensor_io regulator_get ret = %ld\n",
				 __func__ , PTR_ERR(regulator));
			return;
		}
		log->voltage[2] = regulator_get_voltage(regulator);
		regulator_put(regulator);

		if (main_cam) {
			regulator = regulator_get(NULL, "cam_af");
			if (IS_ERR(regulator)) {
				pr_err("%s:err cam_af regulator_get ret = %ld\n",
					 __func__ , PTR_ERR(regulator));
				return;
			}
			log->voltage[3] = regulator_get_voltage(regulator);
			regulator_put(regulator);
		}

		/* CPG */
		log->cpg[0] = __raw_readl(FRQCRA);
		log->cpg[1] = __raw_readl(FRQCRB);
		log->cpg[2] = __raw_readl(VCLKCR1);
		log->cpg[3] = __raw_readl(VCLKCR2);
		log->cpg[4] = __raw_readl(PLLECR);
		log->cpg[5] = __raw_readl(PLL1CR);
		log->cpg[6] = __raw_readl(PLL2CR);
		log->cpg[7] = __raw_readl(MSTPSR0);
		log->cpg[8] = __raw_readl(MSTPSR1);
		log->cpg[9] = __raw_readl(MSTPSR2);
		log->cpg[10] = __raw_readl(SMSTPCR0);
		log->cpg[11] = __raw_readl(SMSTPCR1);
		log->cpg[12] = __raw_readl(SMSTPCR2);

		/* SYSC */
		log->sysc[0] = __raw_readl(PSTR);

		/* GPIO */
		log->gpio_cr[0] = __raw_readb(GPIO_BASE + 3);
		log->gpio_cr[1] = __raw_readb(GPIO_BASE + 16);
		log->gpio_cr[2] = __raw_readb(GPIO_BASE + 20);
		log->gpio_cr[3] = __raw_readb(GPIO_BASE + 45);
		log->gpio_cr[4] = __raw_readb(GPIO_BASE + 91);
		log->gpio_dr[0] = __raw_readl(GPIO_PORTL031_000DR);
		log->gpio_dr[1] = __raw_readl(GPIO_PORTL063_032DR);
		log->gpio_dr[2] = __raw_readl(GPIO_PORTL095_064DR);
	}

	printk(KERN_ALERT "VLDO05=%d [vmipi]\n", log->voltage[0]);
	printk(KERN_ALERT "VLDO12=%d [cam_sensor_a]\n", log->voltage[1]);
	printk(KERN_ALERT "VLDO17=%d [cam_sensor_io]\n", log->voltage[2]);
	if (main_cam)
		printk(KERN_ALERT "VLDO13=%d [cam_af]\n", log->voltage[3]);
	printk(KERN_ALERT
		"FRQCRA[%08X] "
		"FRQCRB[%08X] "
		"VCLKCR1[%08X] "
		"VCLKCR2[%08X]\n",
		log->cpg[0],
		log->cpg[1],
		log->cpg[2],
		log->cpg[3]);
	printk(KERN_ALERT
		"PLLECR[%08X] "
		"PLL1CR[%08X] "
		"PLL2CR[%08X] "
		"MSTPSR0[%08X]\n",
		log->cpg[4],
		log->cpg[5],
		log->cpg[6],
		log->cpg[7]);
	printk(KERN_ALERT
		"MSTPSR1[%08X] "
		"MSTPSR2[%08X] "
		"SMSTPCR0[%08X] "
		"SMSTPCR1[%08X]\n",
		log->cpg[8],
		log->cpg[9],
		log->cpg[10],
		log->cpg[11]);
	printk(KERN_ALERT
		"SMSTPCR2[%08X] "
		"PSTR[%08X]\n",
		log->cpg[12],
		log->sysc[0]);
	printk(KERN_ALERT
		"PORT3CR[%02X] "
		"PORT16CR[%02X] "
		"PORT20CR[%02X] "
		"PORT45CR[%02X] "
		"PORT91CR[%02X]\n",
		log->gpio_cr[0],
		log->gpio_cr[1],
		log->gpio_cr[2],
		log->gpio_cr[3],
		log->gpio_cr[4]);
	printk(KERN_ALERT
		"PORT031_000DR[%08X] "
		"PORT063_032DR[%08X] "
		"PORT095_064DR[%08X]\n",
		log->gpio_dr[0],
		log->gpio_dr[1],
		log->gpio_dr[2]);
}

static void csi20_log_output(void *data, int first)
{
	csi2_camera_voltage_log(data, first, 1);
}

static void csi21_log_output(void *data, int first)
{
	csi2_camera_voltage_log(data, first, 0);
}

struct sh_csi2_client_config csi20_clients[] = {
	{
		.phy		= SH_CSI2_PHY_MAIN,
		.lanes		= 0xF,
		.channel	= 0,
		/*.pdev	will be set during camera setup */
	},
};

struct sh_csi2_pdata csi20_info = {
	.type		= SH_CSI2C,
	.clients	= csi20_clients,
	.num_clients	= ARRAY_SIZE(csi20_clients),
	.flags		= SH_CSI2_ECC | SH_CSI2_CRC,
	.ipr		= 0x24,
	.ipr_set	= (0x0001 << 8),
	.imcr		= 0x1D0,
	.imcr_set	= (0x01 << 2),
	.priv		= NULL,
	.cmod_name	= "csi20",
	.log_output	= csi20_log_output,
	.log_data	= &csi2_log_data,
};

struct resource csi20_resources[] = {
	[0] = {
		.name	= "CSI20",
		.start	= 0xfeaa0000,
		.end	= 0xfeaa0fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x17a0),
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device csi20_device = {
	.name   = "sh-mobile-csi2",
	.id     = 0,
	.num_resources	= ARRAY_SIZE(csi20_resources),
	.resource	= csi20_resources,
	.dev    = {
		.platform_data = &csi20_info,
	},
};

struct sh_csi2_client_config csi21_clients[] = {
	{
		.phy		= SH_CSI2_PHY_SUB,
		.lanes		= 1,
		.channel	= 0,
		/*.pdev	will be set during camera setup */
	},
};

struct sh_csi2_pdata csi21_info = {
	.type		= SH_CSI2C,
	.clients	= csi21_clients,
	.num_clients	= ARRAY_SIZE(csi21_clients),
	.flags		= SH_CSI2_ECC | SH_CSI2_CRC,
	.ipr		= 0x44,
	.ipr_set	= (0x0001 << 0),
	.imcr		= 0x1E0,
	.imcr_set	= (0x01 << 0),
	.priv		= NULL,
	.cmod_name	= "csi21",
	.log_output	= csi21_log_output,
	.log_data	= &csi2_log_data,
};

struct resource csi21_resources[] = {
	[0] = {
		.name	= "CSI21",
		.start	= 0xfeaa8000,
		.end	= 0xfeaa8fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x1be0),
		.flags  = IORESOURCE_IRQ,
	},
};

struct platform_device csi21_device = {
	.name   = "sh-mobile-csi2",
	.id     = 1,
	.num_resources	= ARRAY_SIZE(csi21_resources),
	.resource	= csi21_resources,
	.dev    = {
		.platform_data = &csi21_info,
	},
};

