#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/broadcom/bcm-bt-rfkill.h>
#include <linux/broadcom/bcm-bt-lpm.h>
#ifdef CONFIG_BCM_BZHW
#include <linux/broadcom/bcm_bzhw.h>
#endif
#include <mach/dev-bt.h>
#include <mach/r8a7373.h>

#ifdef CONFIG_BCM_BZHW
void __init add_bcm_bzhw_device(int gpio_bt_wake, int gpio_host_wake)
{
	struct platform_device *pdev;
	struct bcm_bzhw_platform_data data = {
		.gpio_bt_wake   = gpio_bt_wake,
		.gpio_host_wake = gpio_host_wake,
	};
	pdev = platform_device_register_data(NULL, "bcm_bzhw", -1,
					     &data, sizeof data);
	if (IS_ERR(pdev))
		pr_err("failed to register bcm_bzhw device %ld", PTR_ERR(pdev));
}

#endif


void __init add_bcmbt_lpm_device(int gpio_bt_wake, int gpio_host_wake)
{
	struct platform_device *pdev;
	struct bcm_bt_lpm_platform_data data = {
		.bt_wake_gpio = gpio_bt_wake,
		.host_wake_gpio = gpio_host_wake,
	};
	pdev = platform_device_register_data(NULL, "bcm-bt-lpm", -1,
					     &data, sizeof data);
	if (IS_ERR(pdev))
		pr_err("failed to register bcm-bt-lpm device %ld",
			PTR_ERR(pdev));
}
