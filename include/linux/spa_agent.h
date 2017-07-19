/* Samsung Power and Charger Agent
 * 
 * include/linux/spa_agent.h
 *
 * Agent driver for SPA driver.
 *
 * Copyright (C) 2012, Samsung Electronics.
 *
 * This program is free software. You can redistribute it and/or modify
 * it under the terms of the GNU(General Public License version 2) as 
 * published by the Free Software Foundation.
 */

#ifndef __SPA_AGENT_H
#define __SPA_AGENT_H

enum spa_agent_feature
{
	SPA_AGENT_SET_CHARGE,
	SPA_AGENT_SET_CHARGE_CURRENT,
	SPA_AGENT_SET_CHARGE_VOLTAGE,
	SPA_AGENT_SET_FULL_CHARGE,
	SPA_AGENT_GET_FULL_CHARGE,
	SPA_AGENT_GET_VOLTAGE,
	SPA_AGENT_GET_TEMP,
	SPA_AGENT_GET_CAPACITY,
	SPA_AGENT_GET_BATT_PRESENCE_PMIC,
	SPA_AGENT_GET_BATT_PRESENCE_CHARGER,
	SPA_AGENT_GET_CHARGER_TYPE,
	SPA_AGENT_GET_CURRENT,
	SPA_AGENT_GET_CHARGE_STATE,
	SPA_AGENT_CTRL_FG,
	SPA_AGENT_MAX,
};

typedef union
{
	int (*set_charge)(unsigned int en);
	int (*set_charge_current)(unsigned int curr);
	int (*set_charge_voltage)(unsigned int mvolt);
	int (*set_full_charge)(unsigned int eoc);
	int (*get_full_charge)(void);
	int (*get_voltage)(unsigned int opt);
	int (*get_temp)(unsigned int opt);
	int (*get_capacity)(void);
	int (*get_batt_presence_pmic)(void);
	int (*get_batt_presence_charger)(void);
	int (*get_charger_type)(void);
	int (*get_spa_current)(unsigned int opt);
	int (*get_charge_state)(void);
	int (*ctrl_fg)(void *data);
	int (*dummy)(void);
} SPA_AGENT_FN_T;

struct spa_agent_fn 
{
	SPA_AGENT_FN_T fn;
	char *agent_name;
};
int spa_agent_register(unsigned int agent_id, void *fn, const char *agent_name);
#endif
