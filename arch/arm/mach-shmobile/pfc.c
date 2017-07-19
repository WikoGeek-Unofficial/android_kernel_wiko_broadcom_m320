#include <linux/init.h>

#include "sh-pfc.h"

const char * __initdata sh_primary_pfc;

int __init sh_pfc_register_mappings(struct pinctrl_map *maps, unsigned num_maps)
{
	unsigned n;

	for (n = 0; n < num_maps; n++) {
		maps[n].ctrl_dev_name = sh_primary_pfc;
		if (!maps[n].dev_name)
			maps[n].dev_name = sh_primary_pfc;
	}

	return pinctrl_register_mappings(maps, num_maps);
}
