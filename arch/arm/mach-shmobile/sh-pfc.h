#ifndef _SH_PFC_H
#define _SH_PFC_H

#include <linux/pinctrl/machine.h>

extern const char *sh_primary_pfc;

/* Helper macros and function to avoid needing to name the PFC up-front */
#define SH_PFC_MUX_GROUP(dev, state, grp, func)			\
	PIN_MAP_MUX_GROUP(dev, state, NULL, grp, func)

#define SH_PFC_MUX_GROUP_DEFAULT(dev, grp, func)		\
	PIN_MAP_MUX_GROUP_DEFAULT(dev, NULL, grp, func)

#define SH_PFC_MUX_GROUP_HOG(state, grp, func)			\
	PIN_MAP_MUX_GROUP_HOG(NULL, state, grp, func))

#define SH_PFC_MUX_GROUP_HOG_DEFAULT(grp, func)			\
	PIN_MAP_MUX_GROUP_HOG_DEFAULT(NULL, grp, func))

#define SH_PFC_CONFIGS_PIN(dev, state, pin, cfgs)		\
	PIN_MAP_CONFIGS_PIN(dev, state, NULL, pin, cfgs)

#define SH_PFC_CONFIGS_PIN_DEFAULT(dev, pin, cfgs)		\
	PIN_MAP_CONFIGS_PIN_DEFAULT(dev, NULL, pin, cfgs)

#define SH_PFC_CONFIGS_PIN_HOG(state, pin, cfgs)		\
	PIN_MAP_CONFIGS_PIN_HOG(NULL, state, pin, cfgs)

#define SH_PFC_CONFIGS_PIN_HOG_DEFAULT(pin, cfgs)		\
	PIN_MAP_CONFIGS_PIN_HOG_DEFAULT(NULL, pin, cfgs)

#define SH_PFC_CONFIGS_GROUP(dev, state, group, cfgs)		\
	PIN_MAP_CONFIGS_GROUP(dev, state, NULL, group, cfgs)

#define SH_PFC_CONFIGS_GROUP_DEFAULT(dev, group, cfgs)		\
	PIN_MAP_CONFIGS_GROUP_DEFAULT(dev, NULL, group, cfgs)

#define SH_PFC_CONFIGS_GROUP_HOG(state, group, cfgs)		\
	PIN_MAP_CONFIGS_GROUP_HOG(NULL, state, group, cfgs)

#define SH_PFC_CONFIGS_GROUP_HOG_DEFAULT(group, cfgs)		\
	PIN_MAP_CONFIGS_GROUP_HOG_DEFAULT(NULL, group, cfgs)

/* Modifies the map to fill in the pfc, then passes it to pinctrl */
int sh_pfc_register_mappings(struct pinctrl_map *maps, unsigned num_maps);

#endif /* SH_PFC_H_ */
