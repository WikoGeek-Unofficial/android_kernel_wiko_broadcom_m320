/*
 * Copyright (C) 2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/syscalls.h>
//#include <linux/mmc/ext_sdio.h>
#include "wlan_perf.h"

//static struct external_sdio_device_data *device_sdhci;

/* wlan_sdio_gpio_config
	Configure reset and host wake gpios for wlan device
*/
static int wlan_sdio_gpio_config(struct wlan_sdio_kona_cfg *dev_cfg, int en)
{
	int rc = -1;

	pr_debug("%s: Enter reset %d host wake %d\n",
			__func__,
			dev_cfg->bcm_wlan_reset,
			dev_cfg->bcm_wlan_host_wake);
	if (en)	{
		if (dev_cfg->bcm_wlan_reset >= 0) {
			rc = gpio_request(dev_cfg->bcm_wlan_reset,
					"bcm_wlan_reset");
			if (rc < 0) {
				pr_err("%s: gpio reset request failed %d\n",
					__func__,
					dev_cfg->bcm_wlan_reset);
				return rc;
			}
			pr_debug("%s: REG=%x\n",
				__func__,
				dev_cfg->bcm_wlan_reset);
			rc = gpio_direction_output(dev_cfg->bcm_wlan_reset, 1);
			if (rc < 0)
				goto free_reset;
			gpio_set_value(dev_cfg->bcm_wlan_reset, 1);
		}
		gpio_set_value(dev_cfg->bcm_wlan_reset, 0);
		gpio_set_value(dev_cfg->bcm_wlan_reset, 1);
	} else {
		gpio_set_value(dev_cfg->bcm_wlan_reset, 0);
		gpio_free(dev_cfg->bcm_wlan_reset);
	}
	return 0;
	gpio_free(dev_cfg->bcm_wlan_host_wake);
free_reset:
	gpio_free(dev_cfg->bcm_wlan_reset);
	return rc;
}
/* wlan_sdio_power_up
	Power up wlan Device
*/
int wlan_sdio_power_up(struct wlan_sdio_kona_cfg *dev_cfg, int onoff)
{
	pr_debug("%s: Enter\n", __func__);
	return wlan_sdio_gpio_config(dev_cfg, 1);
}
/* wlan_sdio_power_down
	Power down wlan Device
*/
int wlan_sdio_power_down(struct wlan_sdio_kona_cfg *dev_cfg)
{
	pr_debug("%s: Enter\n", __func__);
	return wlan_sdio_gpio_config(dev_cfg, 0);
}
/* wlan_sdio_card_emulate
	Trigger card detect
*/
int wlan_sdio_card_emulate(struct wlan_sdio_kona_cfg *cfg, int insert)
{
//	if (device_sdhci->cardemulate)
//		device_sdhci->
//			cardemulate(device_sdhci->sdhci_host_handler, insert);
	return 0;
}
/*	wlan_sdhci_probe
	Probe function for ext_sdhci_device
*/
static int wlan_sdhci_probe(struct platform_device *pdev)
{
	pr_debug("%s Enter\n", __func__);
//	device_sdhci =
//		(struct external_sdio_device_data *)pdev->dev.platform_data;
	return 0;
}
/* wlan_sdhci_remove
	Remove function for ext_sdhci_device
*/
static int wlan_sdhci_remove(struct platform_device *pdev)
{
//	device_sdhci = NULL;
	return 0;
}
/* platform_driver
	Driver Structure for ext_sdhci device
*/
static struct platform_driver wlan_sdhci_driver = {
	.driver		= {
		.name	= "ext_sdhci",
	},
	.probe		= wlan_sdhci_probe,
	.remove		= wlan_sdhci_remove,
};
/* wlan_sdhci_init
	initialization function for wlan_sdhci
*/
static int __init wlan_sdhci_init(void)
{
	pr_debug("%s Enter\n", __func__);
	return platform_driver_register(&wlan_sdhci_driver);
}
/* wlan_sdhci_exit
	exit function for wlan_sdhci
*/
static void __exit wlan_sdhci_exit(void)
{
	platform_driver_unregister(&wlan_sdhci_driver);
}
module_init(wlan_sdhci_init);
module_exit(wlan_sdhci_exit);

MODULE_DESCRIPTION("Wlan SDHCI Driver for Broadcom Kona platform");
MODULE_AUTHOR("Broadcom");
MODULE_LICENSE("GPL v2");
