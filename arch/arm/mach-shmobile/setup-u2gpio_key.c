#include <linux/init.h>
#include <linux/input.h>
#include <linux/of.h>

/*
 * This file is currently only compiled if CONFIG_RENESAS is unset, but specify
 * the condition explicitly for modify_home_keys(), in case something else is
 * added to this file and that changes.
 */
#ifndef CONFIG_RENESAS

static struct property home_code = {
	.name = "linux,code",
	.value = (u32[]) { cpu_to_be32(KEY_HOME) },
	.length = sizeof(u32),
};

static int __init modify_home_keys(void)
{
	/*
	 * In non-standard environments, the UI requires that phone's "Home"
	 * key(s) produce KEY_HOME, rather than KEY_HOMEPAGE. Before, this was
	 * handled by #ifdef CONFIG_RENESAS in the board platform data. In the
	 * DT world, we retain this Kconfig functionality by modifying the DT -
	 * any key that would produce KEY_HOMEPAGE is modified to produce
	 * KEY_HOME.
	 *
	 * This saves having to mess around with alternate DTs, changing build
	 * scripts or bootloaders.
	 *
	 * (Note that we assume that any node with a linux,code property works
	 * like a gpio-keys or gpio-keys-polled device subnode.)
	 */
	struct device_node *key;
	for_each_node_with_property(key, "linux,code") {
		/*
		 * Look for linux,input-type = EV_KEY or unspecified; and
		 *          linux,code = KEY_HOMEPAGE
		 */
		u32 type = EV_KEY;
		u32 code = KEY_RESERVED;
		of_property_read_u32(key, "linux,input-type", &type);
		of_property_read_u32(key, "linux,code", &code);
		if (type != EV_KEY || code != KEY_HOMEPAGE)
			continue;
		if (of_update_property(key, &home_code))
			pr_err("%s: u2_init_gpio_keys failed\n",
					of_node_full_name(key));
	}

	return 0;
}

/* Post-core, so we modify the DT before the machine_init from arch_initcall */
postcore_initcall(modify_home_keys);

#endif /* !CONFIG_RENESAS */
