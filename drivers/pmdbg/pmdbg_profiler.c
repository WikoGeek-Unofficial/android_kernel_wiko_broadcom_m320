/*********************************************************************
 * *
 * *  Copyright 2013 Broadcom Corporation
 * *
 * *  Unless you and Broadcom execute a separate written software license
 * *  agreement governing use of this software, this software is licensed
 * *  to you under the terms of the GNU
 * *  General Public License version 2 (the GPL), available at
 * *  http://www.broadcom.com/licenses/GPLv2.php with the following added
 * *  to such license:
 * *  As a special exception, the copyright holders of this software give
 * *  you permission to link this software with independent modules, and
 * *  to copy and distribute the resulting executable under terms of your
 * *  choice, provided that you also meet, for each linked independent module,
 * *  the terms and conditions of the license of that module. An independent
 * *  module is a module which is not derived from this software.  The special
 * *  exception does not apply to any modifications of the software.
 * *  Notwithstanding the above, under no circumstances may you combine this
 * *  software in any way with any other Broadcom software provided under a
 * *  license other than the GPL, without Broadcom's express prior written
 * *  consent.
 * ***********************************************************************/

#include <linux/ktime.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/cpu_pm.h>
#include "pmdbg_profiler.h"
#include "mach/r8a7373.h"
#include "pmdbg_hw.h"

#define PROF_BUFF_SIZE    1000
#define BUF_SIZE	1024

#define PLL22ST			BIT(12)
#define PLL3ST			BIT(11)
#define PLL2ST			BIT(10)
#define PLL1ST			BIT(9)
#define PLL0ST			BIT(8)

#define IS_PD_ON(pstr, mask)		((pstr & mask) ? 1 : 0)
#define IS_PLL_ON(pllst, mask)		((pllst & mask) ? 1 : 0)

struct prof_sample {
	ktime_t time;
	u32 pstr;
	u32 pllecr;
	u32 mstpsr[7];
};

struct pd_info {
	char *name;
	int sts_mask;
};

struct pll_info {
	char *name;
	unsigned int sts_mask;
};

struct clk_info {
	char *name;
	void  __iomem *reg;
};

static struct prof_sample *prof_buf;
static atomic_t prof_buf_idx;
static atomic_t mon_state;

static char buf_reg[BUF_SIZE];

LOCAL_DECLARE_MOD_SHOW(profiler, profiler_init, profiler_exit, profiler_show);
DECLARE_CMD(mon, monitor_cmd);

static struct pd_info pd_info_tbl[] = {
	{"A2SL", POWER_A2SL},
	{"A3SM", POWER_A3SM},
	{"A3SG", POWER_A3SG},
	{"A3SP", POWER_A3SP},
	{"C4", POWER_C4},
	{"A2RI", POWER_A2RI},
	{"A2RV", POWER_A2RV},
	{"A3R", POWER_A3R},
	{"A4RM", POWER_A4RM},
	{"A4MP", POWER_A4MP},
	{"A4LC", POWER_A4LC},
	{"A4SF", POWER_A4SF},
	{"D4", POWER_D4},
	{"C4CL", POWER_C4CL},
};

static struct pll_info pll_info_tbl[] = {
	{"PLL0", PLL0ST},
	{"PLL1", PLL1ST},
	{"PLL2", PLL2ST},
	{"PLL22", PLL22ST},
	{"PLL3", PLL3ST},
};

static struct clk_info clk_info_tbl[] = {
	{"MSTPSR0", MSTPSR0},
	{"MSTPSR1", MSTPSR1},
	{"MSTPSR2", MSTPSR2},
	{"MSTPSR3", MSTPSR3},
	{"MSTPSR4", MSTPSR4},
	{"MSTPSR5", MSTPSR5},
};

static int idle_notify_cb(struct notifier_block *self, unsigned long cmd,
			     void *v)
{
	int cur_idx;
	int i;

	if (!atomic_read(&mon_state))
		return 0;

	switch (cmd) {
	case CPU_CLUSTER_PM_ENTER:
		cur_idx = atomic_read(&prof_buf_idx);
		if (cur_idx >= PROF_BUFF_SIZE)
			break;

		prof_buf[cur_idx].time = ktime_get();
		prof_buf[cur_idx].pstr = rreg32(PSTR);
		prof_buf[cur_idx].pllecr = rreg32(PLLECR);

		for (i = 0; i < ARRAY_SIZE(clk_info_tbl); i++)
			prof_buf[cur_idx].mstpsr[i] =
				rreg32(clk_info_tbl[i].reg);

		cur_idx++;
		atomic_set(&prof_buf_idx, cur_idx);
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:
	case CPU_CLUSTER_PM_EXIT:
		break;
	};

	return NOTIFY_OK;
}

static struct notifier_block idle_notify = {
	.notifier_call = idle_notify_cb,
};

static int start_monitor(void)
{
	int ret = 0;

	FUNC_MSG_IN;

	atomic_set(&prof_buf_idx, 0);
#ifdef CONFIG_CPU_IDLE
	/**
	 * Memory leak??
	 * Should have been released
	 * during stop call
	 */
	BUG_ON(prof_buf != NULL);

	prof_buf = kzalloc(PROF_BUFF_SIZE * sizeof(struct prof_sample), 0);
	if (!prof_buf) {
		ret = -ENOMEM;
		goto exit;
	}

	cpu_pm_register_notifier(&idle_notify);
	atomic_set(&mon_state, 1);
#else
	MSG_INFO("CPUIDLE is not enabled\n");
	ret = -EINVAL;
#endif
exit:
	FUNC_MSG_RET(ret);
}

static int stop_monitor(void)
{
	FUNC_MSG_IN;

	atomic_set(&mon_state, 0);
#ifdef CONFIG_CPU_IDLE
	cpu_pm_unregister_notifier(&idle_notify);
	kfree(prof_buf);
	prof_buf = NULL;
#endif
	atomic_set(&prof_buf_idx, 0);

	FUNC_MSG_RET(0);
}

static void get_info(char *buf)
{
	int i = 0, j = 0;
	unsigned int cur = 0;
	ktime_t tdelta;
	char buff[350];
	int len = 0;
	FUNC_MSG_IN;

	cur = atomic_read(&prof_buf_idx);
	if (cur < PROF_BUFF_SIZE) {
		snprintf(buf, 100, "Buffer not full.. Wait.. idx = %d\n", cur);
		return;
	}
	memset(buff, 0, sizeof(buff));

	len += snprintf(buff, 10, "TIME ");
	for (j = 0; j < ARRAY_SIZE(pd_info_tbl); j++)
		len += snprintf(buff + len, 50, "%s ", pd_info_tbl[j].name);

	for (j = 0; j < ARRAY_SIZE(pll_info_tbl); j++)
		len += snprintf(buff + len, 50, "%s ", pll_info_tbl[j].name);

	for (j = 0; j < ARRAY_SIZE(clk_info_tbl); j++)
		len += snprintf(buff + len, 50, "%s ", clk_info_tbl[j].name);

	pr_info("%s\n", buff);
	memset(buff, 0, sizeof(buff));
	len = 0;

	for (i = 0; i < cur; i++) {
		len += snprintf(buff + len, 50, "%llu",
				ktime_to_ms(prof_buf[i].time));
		for (j = 0; j < ARRAY_SIZE(pd_info_tbl); j++)
			len += snprintf(buff + len, 50, "%2d",
					IS_PD_ON(prof_buf[i].pstr,
						pd_info_tbl[j].sts_mask));

		for (j = 0; j < ARRAY_SIZE(pll_info_tbl); j++)
			len += snprintf(buff + len, 50, "%2d",
					IS_PLL_ON(prof_buf[i].pllecr,
						pll_info_tbl[j].sts_mask));
		for (j = 0; j < 7; j++)
			len += snprintf(buff + len, 50, " %x",
					prof_buf[i].mstpsr[j]);


		pr_info("%s\n", buff);
		memset(buff, 0, sizeof(buff));
		len = 0;
	}
	tdelta = ktime_sub(prof_buf[PROF_BUFF_SIZE - 1].time, prof_buf[0].time);
	snprintf(buf, 50, "Total Sampling time: %llu\n", ktime_to_ms(tdelta));
	FUNC_MSG_OUT;
}


static int monitor_cmd(char *para, int size)
{
	int ret = 0;
	char item[PAR_SIZE];
	int para_sz = 0;
	int pos = 0;
	char *s = buf_reg;
	FUNC_MSG_IN;

	memset(buf_reg, 0, sizeof(buf_reg));

	para = strim(para);

	ret = get_word(para, size, 0, item, &para_sz);
	if (ret <= 0) {
		ret = -EINVAL;
		goto fail;
	}
	pos = ret;

	ret = strncmp(item, "start", sizeof("start"));
	if (0 == ret) {
		if (atomic_read(&mon_state)) {
			s += snprintf(s, 100, "profiler is already started\n");
			goto end;
		}
		ret = start_monitor();
		if (ret)
			goto fail;
		s += sprintf(s, "Started profiler");
		goto end;
	}

	ret = strncmp(item, "get", sizeof("get"));
	if (0 == ret) {
		if (!atomic_read(&mon_state)) {
			s += snprintf(s, 100,
					"profiler has not been started yet");
			ret = -1;
			goto end;
		}

		get_info(s);
		goto end;
	}
	ret = strncmp(item, "stop", sizeof("stop"));
	if (0 == ret) {
		if (!atomic_read(&mon_state)) {
			s += snprintf(s, 100,
					"profiler has not been started yet");
			ret = -1;
			goto end;
		}
		ret = stop_monitor();
		if (ret)
			goto fail;
		s += snprintf(s, 100, "profier is stopped");
		goto end;
	}
fail:
	s += sprintf(s, "FAILED");
end:
	MSG_INFO("%s\n", buf_reg);
	FUNC_MSG_RET(ret);
}

static void profiler_show(char **buf)
{
	FUNC_MSG_IN;
	*buf = buf_reg;
	FUNC_MSG_OUT;
}

static int profiler_init(void)
{
	FUNC_MSG_IN;
	ADD_CMD(profiler, mon);
	FUNC_MSG_RET(0);
}

static void profiler_exit(void)
{
	FUNC_MSG_IN;
	DEL_CMD(profiler, mon);
	FUNC_MSG_OUT;
}
