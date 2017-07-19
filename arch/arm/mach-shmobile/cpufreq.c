/*
 * arch/arm/mach-shmobile/cpufreq.c
 *
 * Copyright (C) 2012 Renesas Mobile Corporation
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
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/earlysuspend.h>
#include <linux/slab.h>
#include <asm/system.h>
#include <mach/common.h>
#include <mach/pm.h>
#include <linux/stat.h>
#include <linux/moduleparam.h>
#include <linux/export.h>
#include <memlog/memlog.h>
#include <linux/platform_data/sh_cpufreq.h>
#include <linux/cpumask.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <asm/uaccess.h>
#endif

#ifdef pr_fmt
#undef pr_fmt
#define pr_fmt(fmt) "dvfs[cpufreq.c<%d>]:" fmt, __LINE__
#endif

#define EXTAL1 26000
#define MODE_PER_STATE       3

/* Clocks State */
enum clock_state {
	MODE_NORMAL = 0,
	MODE_EARLY_SUSPEND,
	MODE_MOVIE720P,
	MODE_SUSPEND,
	MODE_NUM
};

enum {
	FREQ_LMT_NODE_ADD,
	FREQ_LMT_NODE_DEL,
	FREQ_LMT_NODE_UPDATE,
};

#define FREQ_TRANSITION_LATENCY  (CONFIG_SH_TRANSITION_LATENCY * NSEC_PER_USEC)

/* For change sampling rate & down factor dynamically */
#define SAMPLING_RATE_DEF FREQ_TRANSITION_LATENCY
#define SAMPLING_RATE_LOW 500000
#define SAMPLING_DOWN_FACTOR_DEF (CONFIG_SH_SAMPLING_DOWN_FACTOR)
#define SAMPLING_DOWN_FACTOR_LOW 1

#define INIT_STATE	1
#define BACK_UP_STATE	2
#define NORMAL_STATE	3
#define STOP_STATE	4

#ifndef CONFIG_HOTPLUG_CPU_MGR
#ifdef CONFIG_HOTPLUG_CPU
#define cpu_up_manager(x, y)    cpu_up(x)
#define cpu_down_manager(x, y)    cpu_down(x)
#else /*!CONFIG_HOTPLUG_CPU*/
#define cpu_up_manager(x, y) do { } while (0)
#define cpu_down_manager(x, y) do { } while (0)
#endif /*CONFIG_HOTPLUG_CPU*/
#endif /*CONFIG_HOTPLUG_CPU_MGR*/

#define pr_cpufreq(level, args...)	\
	do {	\
		if (SH_CPUFREQ_PRINT_##level == SH_CPUFREQ_PRINT_ERROR) \
			pr_err(args); \
		else if (SH_CPUFREQ_PRINT_##level & hp_debug_mask) \
			pr_info(args); \
	} while (0) \

struct shmobile_hp_data {
	struct sh_plat_hp_data *pdata;
	struct cpumask hlgmask;
	struct cpumask locked_cpus;
	unsigned int *load_history;
	unsigned int history_idx;
	unsigned int in_samples;
	unsigned int out_samples;
	int hlg_enabled;
	int hlg_dynamic_disable;
};

/* CPU info */
struct shmobile_cpuinfo {
	struct shmobile_hp_data hpdata;
	struct plist_head min_lmt_list;
	struct plist_head max_lmt_list;
	struct cpufreq_governor *prev_gov;
	unsigned int active_min_lmt;
	unsigned int active_max_lmt;
	unsigned int freq;
	unsigned int req_rate[CONFIG_NR_CPUS];
	unsigned int freq_max;
	int suppress_es_clk_count;
	int clk_state;
	int override_qos;
	spinlock_t lock;
};

/**************** static ****************/
static unsigned int hp_debug_mask = SH_CPUFREQ_PRINT_ERROR |
				SH_CPUFREQ_PRINT_WARNING |
				SH_CPUFREQ_PRINT_UPDOWN |
				SH_CPUFREQ_PRINT_INIT;

struct shmobile_cpuinfo	the_cpuinfo;
static struct cpufreq_frequency_table *freq_table;
static struct cpufreq_lmt_node usr_min_lmt_node = {
	.name = "usr_min_lmt",
};

static struct cpufreq_lmt_node usr_max_lmt_node = {
	.name = "usr_max_lmt",
};

static struct pll0_zdiv {
	int pllratio;
	int div_rate;
	int waveform;
}
#ifdef CONFIG_CPUFREQ_OVERDRIVE
zdiv_table15[] = {
	{ PLLx56, DIV1_1, LLx16_16},	/* 1,456 MHz	*/
	{ PLLx49, DIV1_1, LLx14_16},	/* 1,274 MHz	*/
	{ PLLx42, DIV1_1, LLx12_16},	/* 1,092 MHz	*/
	{ PLLx35, DIV1_1, LLx10_16},	/*   910 MHz	*/
	{ PLLx56, DIV1_2, LLx8_16},	/*   728 MHz	*/
	{ PLLx49, DIV1_2, LLx7_16},	/*   637 MHz	*/
	{ PLLx42, DIV1_2, LLx6_16},	/*   546 MHz	*/
	{ PLLx35, DIV1_2, LLx5_16},	/*   455 MHz	*/
	{ PLLx56, DIV1_4, LLx4_16},	/*   364 MHz	*/
	{ PLLx42, DIV1_4, LLx3_16},	/*   273 MHz	*/
},
#endif
zdiv_table12[] = {
	{ PLLx46, DIV1_1, LLx16_16},	/* 1196    MHz	*/
	{ PLLx46, DIV1_1, LLx14_16},	/* 1046.5  MHz	*/
	{ PLLx46, DIV1_1, LLx12_16},	/*  897    MHz	*/
	{ PLLx46, DIV1_1, LLx10_16},	/*  747.5  MHz	*/
	{ PLLx46, DIV1_2, LLx9_16},	/*  672.75 MHz	*/
	{ PLLx46, DIV1_2, LLx7_16},	/*  523.25 MHz	*/
	{ PLLx46, DIV1_2, LLx6_16},	/*  448.5  MHz	*/
	{ PLLx46, DIV1_2, LLx5_16},	/*  373.75 MHz	*/
	{ PLLx46, DIV1_4, LLx4_16},	/*  299    MHz	*/
},
*zdiv_table = NULL;

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ONDEMAND
static int sampling_flag = INIT_STATE;
#else
static int sampling_flag = STOP_STATE;
#endif /* CONFIG_CPU_FREQ_DEFAULT_GOV_ONDEMAND */

static int shmobile_sysfs_init(void);
static void hlg_work_handler(struct work_struct *work);
static struct workqueue_struct *dfs_queue;
static DECLARE_WORK(hlg_work, hlg_work_handler);

#ifdef CONFIG_HOTPLUG_CPU
/*
 * pr_his_req: print requested frequency buffer
 *
 * Argument:
 *		None
 *
 * Return:
 *		None
 */
static inline void pr_his_req(void)
{
#ifdef DVFS_DEBUG_MODE
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;

	int i = 0;
	int j = hpdata->history_idx - 1;

	for (i = 0; i < hpdata->out_samples; i++, j--) {
		if (j < 0)
			j = hpdata->out_samples - 1;
		pr_cpufreq(VERBOSE, "[%2d]%07u\n", j, hpdata->load_history[j]);
	}
#endif
}

/*
 * hlg_cpu_updown: plug-in/plug-out cpus
 *
 * Arguments :
 *      @hlgmask : cpumask of requested online cpus
 *
 * Return :
 *		None
 */
static void hlg_cpu_updown(const struct cpumask *hlgmask)
{
	int cpu, ret = 0;

	for_each_present_cpu(cpu) {
		if (cpu_online(cpu) && !cpumask_test_cpu(cpu, hlgmask)) {
			pr_cpufreq(UPDOWN, "plug-out cpu<%d>...\n", cpu);
			ret = cpu_down_manager(cpu, DFS_HOTPLUG_ID);
			if (ret)
				pr_cpufreq(ERROR, "(%s):cpu%d down, err %d\n",
							__func__, cpu, ret);
			pr_cpufreq(UPDOWN, "plug-out cpu<%d> done\n", cpu);
		} else if (cpu_is_offline(cpu) &&
					cpumask_test_cpu(cpu, hlgmask)) {
			pr_cpufreq(UPDOWN, "plug-in cpu<%d>...\n", cpu);
			ret = cpu_up_manager(cpu, DFS_HOTPLUG_ID);
			if (ret)
				pr_cpufreq(ERROR, "(%s):cpu%d up, err %d\n",
						__func__, cpu, ret);
			pr_cpufreq(UPDOWN, "plug-in cpu<%d> done\n", cpu);
		}
	}
}

static inline void hp_clear_history(struct shmobile_hp_data *hpdata)
{
	struct sh_plat_hp_data *hpplat = hpdata->pdata;

	hpdata->history_idx = 0;
	memset(hpdata->load_history, 0,
		sizeof(*hpdata->load_history) * hpplat->out_samples);
}

/*
 * hlg_work_handler: work to handle hotplug of cpus
 *
 * Arguments :
 *            @work : work instance
 * Return :
 *			None
 */
static void hlg_work_handler(struct work_struct *work)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;
	struct cpumask hlgmask;

	spin_lock(&the_cpuinfo.lock);
	if (!hpdata->hlg_enabled || hpdata->hlg_dynamic_disable) {
		cpumask_copy(&hpdata->hlgmask, cpu_online_mask);
		hp_clear_history(hpdata);

		if (the_cpuinfo.clk_state != MODE_SUSPEND)
			cpumask_copy(&hlgmask, cpu_possible_mask);
		else
			cpumask_copy(&hlgmask, cpu_online_mask);

		goto cpu_down;
	}

	cpumask_copy(&hlgmask, &hpdata->hlgmask);
	cpumask_or(&hlgmask, &hlgmask, &hpdata->locked_cpus);

cpu_down:
	spin_unlock(&the_cpuinfo.lock);
	hlg_cpu_updown(&hlgmask);
}

/*
 * shmobile_cpufreq_load_info: callback from cpufreq governor to notify
 *                             current cpu load
 *
 * Arguments :
 *            @policy: cpu policy
 *            @load: current cpu load
 * Return :
 *			None
 */
static void shmobile_cpufreq_load_info(struct cpufreq_policy *policy,
				 unsigned int load)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;
	struct sh_plat_hp_data *hpplat = hpdata->pdata;
	int cnt, temp, cpu;
	unsigned long in_load = 0, out_load = 0;

	spin_lock(&the_cpuinfo.lock);

	if (hpdata->hlg_dynamic_disable)
		goto unlock;

	if (smp_processor_id())
		goto unlock;

	hpdata->load_history[hpdata->history_idx++] = load;
	if (hpdata->history_idx < hpdata->in_samples)
		goto unlock;

	cpumask_copy(&hpdata->hlgmask, cpu_online_mask);
	/* calculate in average of load */
	for (cnt = hpdata->history_idx - 1, temp = 0;
				temp < hpdata->in_samples; cnt--, temp++)
		in_load += hpdata->load_history[cnt];
	in_load = in_load / temp;

	/* any cpu's to plug-in, get them into mask */
	for_each_possible_cpu(cpu) {
		if (cpu == 0)
			continue;

		if (cpumask_test_cpu(cpu, &hpdata->locked_cpus))
			continue;

		if (in_load >= hpplat->thresholds[cpu].thresh_plugin)
			cpu_set(cpu, hpdata->hlgmask);
	}

	if (!cpumask_equal(&hpdata->hlgmask, cpu_online_mask))
		goto queue_work;

	/* calculate out average of load */
	if (hpdata->history_idx < hpdata->out_samples)
		goto unlock;

	for (cnt = 0; cnt < hpdata->history_idx; cnt++)
		out_load += hpdata->load_history[cnt];
	out_load = out_load / cnt;

	/* any cpu's to plug-out, get them into mask */
	for_each_possible_cpu(cpu) {
		if (cpu == 0)
			continue;

		if (cpumask_test_cpu(cpu, &hpdata->locked_cpus))
			continue;

		if (out_load <= hpplat->thresholds[cpu].thresh_plugout)
			cpu_clear(cpu, hpdata->hlgmask);
	}

	if (cpumask_equal(&hpdata->hlgmask, cpu_online_mask))
		goto clear_history;

queue_work:
	pr_his_req();
	/* schedule work on cpu0 to hotplug */
	queue_work_on(0, dfs_queue, &hlg_work);
clear_history:
	hp_clear_history(hpdata);
unlock:
	spin_unlock(&the_cpuinfo.lock);
}

static inline void shmobile_validate_thresholds(
			struct sh_plat_hp_data *hpdata)
{
	struct sh_hp_thresholds *tl = hpdata->thresholds;
	unsigned int min_freq = ~0;
	unsigned int max_freq = 0;
	int cpu, i;

	for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int freq = freq_table[i].frequency;
		if (freq < min_freq)
			min_freq = freq;
		if (freq > max_freq)
			max_freq = freq;
	}

	/* validate hotplug thresholds for non-boot cpus */
	for_each_possible_cpu(cpu) {
		if (cpu == 0)
			continue;

		BUG_ON(tl[cpu].thresh_plugout > max_freq ||
					tl[cpu].thresh_plugout < min_freq ||
					tl[cpu].thresh_plugin > max_freq ||
					tl[cpu].thresh_plugin < min_freq);
	}
}

#endif /* CONFIG_HOTPLUG_CPU */

/*
 * CPU doesn't seem to wakeup from suspend if frequency
 * is anything less than SUSPEND_CPUFREQ. As an workaroud
 * override the thermal limits with suspend frequency.
 */
static void cpufreq_override_thermal_limits(
			struct cpufreq_lmt_node *lmt_node)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	BUG_ON(!policy);
	if (!strcmp(lmt_node->name, "sys-suspend"))
		cpufreq_verify_within_limits(policy,
				lmt_node->lmt, policy->cpuinfo.max_freq);

	cpufreq_cpu_put(policy);
}

/*********************************************************************
 *		            Cpufreq QoS support					             *
 *********************************************************************/
static inline void cpufreq_switch_governor(struct cpufreq_policy *policy,
		unsigned int min)
{
	if (min == policy->cpuinfo.max_freq &&
			policy->governor != &cpufreq_gov_performance) {
		/* save current governor & switch to performance*/
		the_cpuinfo.prev_gov = policy->governor;
		policy->user_policy.governor = &cpufreq_gov_performance;
	} else if (min != policy->cpuinfo.max_freq &&
			policy->governor == &cpufreq_gov_performance &&
			the_cpuinfo.prev_gov) {
		policy->user_policy.governor = the_cpuinfo.prev_gov;
		the_cpuinfo.prev_gov = NULL;
	}
}

static int cpufreq_lmt_update_policy(int lmt_type)
{
	unsigned int min, max;
	struct cpufreq_policy *policy;

	policy = cpufreq_cpu_get(0);
	if (!policy) {
		pr_cpufreq(ERROR, "cpufreq driver not initialized\n");
		return -EINVAL;
	}

	switch (lmt_type) {
	case FREQ_MAX_LIMIT:
		if (plist_head_empty(&the_cpuinfo.max_lmt_list))
			max = policy->cpuinfo.max_freq;
		else
			max = plist_first(&the_cpuinfo.max_lmt_list)->prio;

		if (max == the_cpuinfo.active_max_lmt)
			goto out;

		the_cpuinfo.active_max_lmt = policy->user_policy.max = max;
		cpufreq_verify_within_limits(policy, policy->min, max);
		break;

	case FREQ_MIN_LIMIT:
		if (plist_head_empty(&the_cpuinfo.min_lmt_list))
			min = policy->cpuinfo.min_freq;
		else
			min = plist_last(&the_cpuinfo.min_lmt_list)->prio;

		if (min == the_cpuinfo.active_min_lmt)
			goto out;

		the_cpuinfo.active_min_lmt = policy->user_policy.min = min;
		cpufreq_verify_within_limits(policy, min, policy->max);
		cpufreq_switch_governor(policy, min);
		break;

	default:
		BUG();
	}

out:
	cpufreq_cpu_put(policy);
	return 0;
}

static int cpufreq_lmt_update(struct cpufreq_lmt_node *lmt_node, int action)
{
	struct plist_head *head;

	head = (lmt_node->lmt_type == FREQ_MAX_LIMIT) ?
		&the_cpuinfo.max_lmt_list : &the_cpuinfo.min_lmt_list;

	switch (action) {
	case FREQ_LMT_NODE_ADD:
		plist_node_init(&lmt_node->node, lmt_node->lmt);
		plist_add(&lmt_node->node, head);
		break;
	case FREQ_LMT_NODE_DEL:
		plist_del(&lmt_node->node, head);
		break;
	case FREQ_LMT_NODE_UPDATE:
		plist_del(&lmt_node->node, head);
		plist_node_init(&lmt_node->node, lmt_node->lmt);
		plist_add(&lmt_node->node, head);
		break;
	default:
		BUG();
	}

	return cpufreq_lmt_update_policy(lmt_node->lmt_type);
}

int cpufreq_add_lmt_req(struct cpufreq_lmt_node *lmt_node,
		const char *client_name, unsigned int lmt, int lmt_type)
{
	int ret;

	spin_lock(&the_cpuinfo.lock);

	if (unlikely(the_cpuinfo.override_qos)) {
		ret = -EPERM;
		goto unlock;
	}

	if (!lmt_node || lmt_node->valid) {
		ret = -EINVAL;
		goto unlock;
	}

	if (lmt_type != FREQ_MAX_LIMIT && lmt_type != FREQ_MIN_LIMIT) {
		ret = -EINVAL;
		goto unlock;
	}

	lmt_node->lmt = lmt;
	lmt_node->name = client_name;
	lmt_node->lmt_type = lmt_type;
	lmt_node->valid = 1;

	ret = cpufreq_lmt_update(lmt_node, FREQ_LMT_NODE_ADD);
	if (!ret) {
		cpufreq_override_thermal_limits(lmt_node);
		spin_unlock(&the_cpuinfo.lock);
		ret = cpufreq_update_policy(0);
		if (ret) {
			pr_cpufreq(ERROR, "Error while adding QoS(%d)\n", ret);
			cpufreq_del_lmt_req(lmt_node);
		}
		goto out;
	}

unlock:
	spin_unlock(&the_cpuinfo.lock);
out:
	return ret;
}
EXPORT_SYMBOL(cpufreq_add_lmt_req);

int cpufreq_del_lmt_req(struct cpufreq_lmt_node *lmt_node)
{
	int ret;

	spin_lock(&the_cpuinfo.lock);
	if (!lmt_node || !lmt_node->valid) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = cpufreq_lmt_update(lmt_node, FREQ_LMT_NODE_DEL);
	memset(lmt_node, 0, sizeof(*lmt_node));

unlock:
	spin_unlock(&the_cpuinfo.lock);

	if (!ret)
		ret = cpufreq_update_policy(0);

	return ret;
}
EXPORT_SYMBOL(cpufreq_del_lmt_req);

int cpufreq_update_lmt_req(struct cpufreq_lmt_node *lmt_node, int lmt)
{
	int ret = 0;

	spin_lock(&the_cpuinfo.lock);
	if (!lmt_node || !lmt_node->valid) {
		ret = -EINVAL;
		goto unlock;
	}

	if (lmt_node->lmt == lmt)
		goto unlock;

	lmt_node->lmt = lmt;
	ret = cpufreq_lmt_update(lmt_node, FREQ_LMT_NODE_UPDATE);
	if (!ret) {
		spin_unlock(&the_cpuinfo.lock);
		ret = cpufreq_update_policy(0);
		goto out;
	}

unlock:
	spin_unlock(&the_cpuinfo.lock);
out:
	return ret;
}
EXPORT_SYMBOL(cpufreq_update_lmt_req);

int cpufreq_get_cpu_min_limit(void)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;

	return cpumask_weight(&hpdata->locked_cpus);
}
EXPORT_SYMBOL(cpufreq_get_cpu_min_limit);

int cpufreq_set_cpu_min_limit(int input)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;
	unsigned int nr_cpus = num_possible_cpus();
	int cnt;

	if (!input || input > nr_cpus)
		return -EINVAL;

	spin_lock(&the_cpuinfo.lock);
	cpumask_clear(&hpdata->locked_cpus);
	for (cnt = 0; cnt < input; cnt++)
		cpumask_set_cpu(cnt, &hpdata->locked_cpus);

	/* If locked cpus are offline, bring them online */
	if (cpumask_equal(&hpdata->locked_cpus, cpu_online_mask)) {
		spin_unlock(&the_cpuinfo.lock);
		return 0;
	}

	queue_work_on(0, dfs_queue, &hlg_work);
	spin_unlock(&the_cpuinfo.lock);
	return 0;
}
EXPORT_SYMBOL(cpufreq_set_cpu_min_limit);

static int cpufreq_update_usr_lmt_req(int val, int lmt_typ)
{
	if (lmt_typ != FREQ_MAX_LIMIT &&
			lmt_typ != FREQ_MIN_LIMIT)
		return -EINVAL;

	if (lmt_typ == FREQ_MAX_LIMIT) {
		if (!usr_max_lmt_node.valid)
			cpufreq_add_lmt_req(&usr_max_lmt_node,
				usr_max_lmt_node.name, val, lmt_typ);
		else
			cpufreq_update_lmt_req(&usr_max_lmt_node, val);
	} else {
		if (!usr_min_lmt_node.valid)
			cpufreq_add_lmt_req(&usr_min_lmt_node,
			usr_min_lmt_node.name, val, lmt_typ);
		else
			cpufreq_update_lmt_req(&usr_min_lmt_node, val);
	}

	return 0;
}

/*
 * __to_freq_level: convert from frequency to frequency level
 *
 * Argument:
 *		@freq: the frequency will be set
 *
 * Return:
 *		freq level
 */
static int __to_freq_level(unsigned int freq)
{
	int idx;

	for (idx = 0; freq_table[idx].frequency != CPUFREQ_TABLE_END; idx++)
		if (freq_table[idx].frequency == freq)
			return idx;

	return CPUFREQ_ENTRY_INVALID;
}

/*
 * __clk_get_rate: get the set of clocks
 *
 * Argument:
 * Return:
 *     address of element of array clocks_div_data
 *     NULL: if input parameters are invaid
 */
static int __clk_get_rate(struct clk_rate *rate)
{
	unsigned int target_freq = the_cpuinfo.freq;
	int clkmode = 0;
	int ret = 0;
	int level = 0;

	if (!rate) {
		pr_cpufreq(ERROR, "invalid parameter<NULL>\n");
		return -EINVAL;
	}

	if (target_freq <= FREQ_MID_UPPER_LIMIT)
		level++;
	if (target_freq <= FREQ_MIN_UPPER_LIMIT)
		level++;

	if (the_cpuinfo.clk_state == MODE_SUSPEND)
		clkmode = 0;
	else if (the_cpuinfo.clk_state == MODE_EARLY_SUSPEND &&
			the_cpuinfo.suppress_es_clk_count)
		clkmode = MODE_NORMAL * MODE_PER_STATE + level + 1;
	else
		clkmode = the_cpuinfo.clk_state * MODE_PER_STATE + level + 1;

	/* get clocks setting according to clock mode */
	ret = pm_get_clock_mode(clkmode, rate);
	if (ret)
		pr_cpufreq(ERROR, "error! fail to get clock mode<%d>\n",
				clkmode);

	return ret;
}

/*
 * __set_rate: set SYS-CPU frequency
 *`
 * Argument:
 *		@freq: the frequency will be set
 *
 * Return:
 *     0: setting is normal
 *     negative: operation fail
 */
static int __set_rate(unsigned int freq)
{
#ifndef ZFREQ_MODE
	unsigned int div_rate;
	unsigned int pllratio;
#endif /* ZFREQ_MODE */
	int ret, level;

	level = __to_freq_level(freq);
	if (level == CPUFREQ_ENTRY_INVALID)
		return -EINVAL;

#ifndef ZFREQ_MODE
	div_rate = zdiv_table[level].div_rate;
	ret = pm_set_syscpu_frequency(div_rate);
	if (ret)
		return ret;

	/* change PLL0 if need */
	pllratio = zdiv_table[level].pllratio;
	if (pm_get_pll_ratio(PLL0) != pllratio) {
		ret = pm_set_pll_ratio(PLL0, pllratio);
		if (ret)
			return ret;
	}
#else
	ret = pm_set_syscpu_frequency(zdiv_table[level].waveform);
#endif /* ZFREQ_MODE */

	memory_log_dump_int(PM_DUMP_ID_DFS_FREQ, freq);
	the_cpuinfo.freq = freq;

	return ret;
}

static inline void __change_sampling_values(void)
{
	int ret = 0;
	static unsigned int downft = SAMPLING_DOWN_FACTOR_DEF;
	static unsigned int samrate = SAMPLING_RATE_DEF;
	struct cpufreq_policy *policy;

	policy = cpufreq_cpu_get(0);

	if (!policy)
		return;
	if (strcmp(policy->governor->name, "ondemand") != 0)
		return;

	if (STOP_STATE == sampling_flag) /* ondemand gov is stopped! */
		return;
	if (INIT_STATE == sampling_flag) {
		samplrate_downfact_change(policy, SAMPLING_RATE_DEF,
					SAMPLING_DOWN_FACTOR_DEF,
					0);
		sampling_flag = NORMAL_STATE;
	}
	if ((the_cpuinfo.clk_state == MODE_EARLY_SUSPEND &&
		the_cpuinfo.freq <= FREQ_MIN_UPPER_LIMIT) ||
		the_cpuinfo.clk_state == MODE_SUSPEND) {
		if (NORMAL_STATE == sampling_flag) {/* Backup old values */
			samplrate_downfact_get(policy, &samrate, &downft);
			sampling_flag = BACK_UP_STATE;
		}
		ret = samplrate_downfact_change(policy, SAMPLING_RATE_LOW,
				SAMPLING_DOWN_FACTOR_LOW, 1);
	} else { /* Need to restore the previous values if any */
		if (BACK_UP_STATE == sampling_flag) {
			sampling_flag = NORMAL_STATE;
			ret = samplrate_downfact_change(policy, samrate,
					downft, 0);
		}
	}
	if (ret)
		pr_cpufreq(ERROR, "%s: samplrate_downfact_change() err<%d>\n",
			__func__, ret);
	cpufreq_cpu_put(policy);
}

/*
 * __set_sys_clocks: verify and set system clocks
 *`
 * Argument:
 *		none
 *
 * Return:
 *		none
 */
static inline int __set_sys_clocks(void)
{
	int ret;
	struct clk_rate rate;

	/* get and set clocks setting based on new SYS-CPU clock */
	ret = __clk_get_rate(&rate);
	if (!ret)
		ret = pm_set_clocks(rate);
	__change_sampling_values();

	return ret;
}

/*
 * __set_all_clocks: set SYS-CPU frequency and other clocks
 *`
 * Argument:
 *		@freq: the SYS frequency will be set
 *
 * Return:
 *     0: setting is normal
 *     negative: operation fail
 */
static inline int __set_all_clocks(unsigned int z_freq)
{
	int ret;

	ret = __set_rate(z_freq);
	if (ret)
		return ret;

	return __set_sys_clocks();
}

static inline void suppress_es_sys_clocks(bool set)
{

	if (!set && !the_cpuinfo.suppress_es_clk_count)
		return;

	the_cpuinfo.suppress_es_clk_count = set ?
		the_cpuinfo.suppress_es_clk_count + 1 :
		the_cpuinfo.suppress_es_clk_count - 1;
}

int cpufreq_set_clocks_mode(int mode, bool set)
{
	int ret;

	spin_lock(&the_cpuinfo.lock);
	switch (mode) {
	case CPUFREQ_MODE_MOVIE:
		the_cpuinfo.clk_state = MODE_MOVIE720P;
		break;
	case CPUFREQ_MODE_SUSPEND:
		the_cpuinfo.clk_state = MODE_SUSPEND;
		break;
	case CPUFREQ_MODE_NORMAL:
		the_cpuinfo.clk_state = MODE_NORMAL;
		break;
	case CPUFREQ_MODE_ES_SUPPRESS:
		suppress_es_sys_clocks(set);
		break;
	case CPUFREQ_MODE_EARLY_SUSPEND:
		the_cpuinfo.clk_state = MODE_EARLY_SUSPEND;
		break;
	default:
		break;
	}

	ret = __set_sys_clocks();
	spin_unlock(&the_cpuinfo.lock);

	return ret;
}
EXPORT_SYMBOL(cpufreq_set_clocks_mode);

#ifdef CONFIG_PM_FORCE_SLEEP
void suspend_cpufreq_hlg_work(void)
{
#ifdef CONFIG_HOTPLUG_CPU
	cancel_work_sync(&hlg_work);
#endif
}
EXPORT_SYMBOL(suspend_cpufreq_hlg_work);
#endif

void disable_hotplug_duringPanic(void)
{
#ifdef CONFIG_HOTPLUG_CPU
	the_cpuinfo.hpdata.hlg_enabled = 0;
#endif /* DYNAMIC_HOTPLUG_CPU */
}
EXPORT_SYMBOL(disable_hotplug_duringPanic);

#ifdef CONFIG_EARLYSUSPEND
/*
 * function: change clock state and set clocks, support for early suspend state
 * in system suspend.
 *
 * Argument:
 *		@h: the early_suspend interface
 *
 * Return:
 *		none
 */
static void shmobile_cpufreq_early_suspend(struct early_suspend *h)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;

	pr_cpufreq(VERBOSE, "++ %s\n", __func__);
	cpufreq_set_clocks_mode(CPUFREQ_MODE_EARLY_SUSPEND, true);

	spin_lock(&the_cpuinfo.lock);
	hpdata->in_samples = hpdata->pdata->es_samples;
	hpdata->out_samples = hpdata->pdata->es_samples;
	hpdata->history_idx = 0;
	spin_unlock(&the_cpuinfo.lock);

	pr_cpufreq(VERBOSE, "-- %s\n", __func__);
}

/*
 * shmobile_cpufreq_late_resume: change clock state and set clocks, support for
 * late resume state in system suspend.
 *
 * Argument:
 *		@h: the early_suspend interface
 *
 * Return:
 *		none
 */
static void shmobile_cpufreq_late_resume(struct early_suspend *h)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;

	pr_cpufreq(VERBOSE, "++ %s\n", __func__);
	cpufreq_set_clocks_mode(CPUFREQ_MODE_NORMAL, true);

	spin_lock(&the_cpuinfo.lock);
	hpdata->in_samples = hpdata->pdata->in_samples;
	hpdata->out_samples = hpdata->pdata->out_samples;
	hpdata->history_idx = 0;
	spin_unlock(&the_cpuinfo.lock);

	pr_cpufreq(VERBOSE, "-- %s\n", __func__);
}

static struct early_suspend shmobile_cpufreq_suspend = {
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 50,
	.suspend = shmobile_cpufreq_early_suspend,
	.resume  = shmobile_cpufreq_late_resume,
};
#endif /* CONFIG_EARLYSUSPEND */

/*
 * shmobile_cpufreq_verify: verify the limit frequency of the policy with the
 * limit frequency of the CPU.
 *
 * Argument:
 *		@policy: the input policy
 *
 * Return:
 *		0: normal
 *		negative: operation fail
 */
static int shmobile_cpufreq_verify(struct cpufreq_policy *policy)
{
	unsigned int min = 0, max = 0;
	struct cpufreq_policy *cur_policy;

	spin_lock(&the_cpuinfo.lock);

	if (unlikely(the_cpuinfo.override_qos))
		goto no_qos;

	if (!plist_head_empty(&the_cpuinfo.max_lmt_list))
		max = plist_first(&the_cpuinfo.max_lmt_list)->prio;

	if (!plist_head_empty(&the_cpuinfo.min_lmt_list))
		min = plist_last(&the_cpuinfo.min_lmt_list)->prio;

	/*
	 * If min qos is max frequency, enforce performance
	 * governor when ondemand is requested. Upon qos removal,
	 * governor will be switched back to Ondemand
	 */
	cur_policy = cpufreq_cpu_get(0);
	if (!cur_policy)
		return -EINVAL;

	if (min == cur_policy->cpuinfo.max_freq &&
		policy->governor != &cpufreq_gov_performance &&
		cur_policy->governor != policy->governor) {
		the_cpuinfo.prev_gov = policy->governor;
		policy->governor = &cpufreq_gov_performance;
	}
	cpufreq_cpu_put(cur_policy);

no_qos:
	if (max && policy->max > max)
		policy->max = max;
	if (min && policy->min < min)
		policy->min = min;
	spin_unlock(&the_cpuinfo.lock);

	return cpufreq_frequency_table_verify(policy, freq_table);
}

/*
 * shmobile_cpufreq_getspeed: Retrieve the current frequency of a SYS-CPU.
 *
 * Argument:
 *		@cpu: the ID of CPU
 *
 * Return:
 *		the frequency value of input CPU.
 */
static unsigned int shmobile_cpufreq_getspeed(unsigned int cpu)
{
	return the_cpuinfo.freq;
}

/*
 * shmobile_cpufreq_target: judgle frequencies
 *
 * Argument:
 *		@policy: the policy
 *		@target_freq: the target frequency passed from CPUFreq framework
 *		@relation: not used
 *
 * Return:
 *		0: normal
 *		negative: operation fail
 */
static int shmobile_cpufreq_target(struct cpufreq_policy *policy,
	unsigned int target_freq, unsigned int relation)
{
	struct cpufreq_freqs freqs;
	unsigned int new_freq;
	unsigned int index;
	int ret = 0;

	spin_lock(&the_cpuinfo.lock);

	if (cpufreq_frequency_table_target(policy, freq_table, target_freq,
				relation, &index)) {
		ret = -EINVAL;
		goto done;
	}

	freqs.flags = 0;
	freqs.old = the_cpuinfo.freq;
	freqs.new = new_freq = freq_table[index].frequency;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);
	ret = __set_all_clocks(new_freq);
	if (ret) {
		pr_cpufreq(ERROR, "Failed to set target freq (%u)\n", new_freq);
		goto done;
	}
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

done:
	spin_unlock(&the_cpuinfo.lock);

	return ret;
}

/*
 * shmobile_cpufreq_init: initialize the DFS module.
 *
 * Argument:
 *		@policy: the policy will change the frequency.
 *
 * Return:
 *		0: normal initialization
 *		negative: operation fail
 */
static int shmobile_cpufreq_init(struct cpufreq_policy *policy)
{
	BUG_ON(!policy);

	/* for other governors which are used frequency table */
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);

	policy->cpuinfo.transition_latency = FREQ_TRANSITION_LATENCY;
	policy->cur = the_cpuinfo.freq = pm_get_syscpu_frequency();

	/* policy sharing between non-boot CPUs */
	cpumask_setall(policy->cpus);

	return cpufreq_frequency_table_cpuinfo(policy, freq_table);
}

static struct freq_attr *shmobile_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver shmobile_cpufreq_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= shmobile_cpufreq_verify,
	.target		= shmobile_cpufreq_target,
	.get		= shmobile_cpufreq_getspeed,
#ifdef CONFIG_HOTPLUG_CPU
	.load		= shmobile_cpufreq_load_info,
#endif
	.init		= shmobile_cpufreq_init,
	.attr		= shmobile_cpufreq_attr,
	.name		= "shmobile"
};

static int shmobile_policy_changed_notifier(struct notifier_block *nb,
			unsigned long type, void *data)
{
	int i, enable_hlg = 0;
	struct cpufreq_policy *policy = data;
	static const char * const governors[] = {
		"conservative", "ondemand", "interactive"
	};

	if (CPUFREQ_GOVERNOR_CHANGE_NOTIFY != type)
		return 0;

	spin_lock(&the_cpuinfo.lock);
	/* governor switched? update hotplug status */
	for (i = 0; i < ARRAY_SIZE(governors); i++)
		if (strcmp(governors[i], policy->governor->name) == 0) {
			enable_hlg = 1;
			break;
		}

	if (!enable_hlg) {
		the_cpuinfo.hpdata.hlg_enabled = 0;
		queue_work_on(0, dfs_queue, &hlg_work);
	} else {
		the_cpuinfo.hpdata.hlg_enabled = 1;
	}

	/* For changing sampling rate dynamically */
	if (strcmp(policy->governor->name, "ondemand") == 0) {
		if (STOP_STATE != sampling_flag)
			goto end;
		/* when governor is changed from non-ondemand */
		sampling_flag = INIT_STATE;
		spin_unlock(&the_cpuinfo.lock);
		__change_sampling_values();
		return 0;
	} else {
		sampling_flag = STOP_STATE;
	}

end:
	spin_unlock(&the_cpuinfo.lock);
	return 0;
}
static struct notifier_block policy_notifier = {
	.notifier_call = shmobile_policy_changed_notifier,
};

static int setup_cpufreq_table(struct device *dev,
				unsigned int pll0_ratio,
				struct pll0_zdiv *zdiv,
				unsigned int num)
{
	unsigned int i;

	freq_table = devm_kzalloc(dev,
			(num + 1) * sizeof(*freq_table), GFP_KERNEL);
	if (!freq_table)
		return -ENOMEM;

	/* hotplug thresholds for non-boot cpus with in freq limits */
	for (i = 0; i < num; i++) {
		freq_table[i].index = i;
		freq_table[i].frequency = pll0_ratio * EXTAL1 *
				zdiv[i].waveform / 16;
	}

	freq_table[i].index = i;
	freq_table[i].frequency = CPUFREQ_TABLE_END;

	the_cpuinfo.active_min_lmt = freq_table[i-1].frequency;
	the_cpuinfo.active_max_lmt = freq_table[0].frequency;
	zdiv_table = zdiv;

	return 0;
}

/*
 * rmobile_cpufreq_init: register the cpufreq driver with the cpufreq
 * governor driver.
 *
 * Arguments:
 *		none.
 *
 * Return:
 *		0: normal registration
 *		negative: operation fail
 */
static int __init cpufreq_drv_probe(struct platform_device *pdev)
{
	int ret, size, i;
	struct sh_cpufreq_plat_data *pdata = pdev->dev.platform_data;
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;

	if (!pdata) {
		pr_cpufreq(ERROR, "No platform init data supplied.\n");
		return -ENODEV;
	}

#ifdef CONFIG_HOTPLUG_CPU
	if (!pdata->hp_data) {
		pr_cpufreq(ERROR, "No hotplug params supplied.\n");
		return -ENODEV;
	}
#endif

#ifdef CONFIG_CPUFREQ_OVERDRIVE
	pm_set_pll_ratio(PLL0, PLLx56);
	ret = setup_cpufreq_table(&pdev->dev, PLLx56, zdiv_table15,
						ARRAY_SIZE(zdiv_table15));
#else
	pm_set_pll_ratio(PLL0, PLLx46);
	ret = setup_cpufreq_table(&pdev->dev, PLLx46, zdiv_table12,
						ARRAY_SIZE(zdiv_table12));
#endif
	if (ret) {
		pr_cpufreq(ERROR, "Failed to setup freq table\n");
		return ret;
	}

	pr_cpufreq(INIT, " PLL0 = x%d", pm_get_pll_ratio(PLL0));
	ret = pm_setup_clock();
	if (ret) {
		pr_cpufreq(ERROR, "Failed to setup pm clocks\n");
		return ret;
	}

	plist_head_init(&the_cpuinfo.min_lmt_list);
	plist_head_init(&the_cpuinfo.max_lmt_list);
	spin_lock_init(&the_cpuinfo.lock);
	the_cpuinfo.clk_state = MODE_NORMAL;
	the_cpuinfo.freq_max = FREQ_MAX;
	the_cpuinfo.override_qos = 0;

#ifdef CONFIG_HOTPLUG_CPU
	hpdata->pdata = pdata->hp_data;
	hpdata->out_samples = hpdata->pdata->out_samples;
	hpdata->in_samples = hpdata->pdata->in_samples;
	size = sizeof(*hpdata->load_history) *
					hpdata->pdata->out_samples;

	hpdata->load_history = devm_kzalloc(&pdev->dev, size,
					GFP_KERNEL);
	if (!hpdata->load_history)
		return -ENOMEM;

	hpdata->history_idx = 0;
	hpdata->hlg_enabled = 1;
	shmobile_validate_thresholds(hpdata->pdata);

	dfs_queue = alloc_workqueue("dfs_queue", WQ_HIGHPRI |
			WQ_CPU_INTENSIVE | WQ_MEM_RECLAIM, 1);
	if (!dfs_queue)
		return -ENOMEM;

	cpumask_set_cpu(0, &hpdata->locked_cpus);
#endif /* CONFIG_HOTPLUG_CPU */

	/* register cpufreq driver to cpufreq core */
	ret = cpufreq_register_driver(&shmobile_cpufreq_driver);
	if (ret) {
		pr_cpufreq(ERROR, "Failed to register driver\n");
		goto free_queue;
	}

	ret = cpufreq_register_notifier(&policy_notifier,
					CPUFREQ_POLICY_NOTIFIER);
	if (ret) {
		pr_cpufreq(ERROR, "Failed to register for notifier\n");
		goto unreg_cpufreq;
	}

	ret = shmobile_sysfs_init();
	if (ret)
		goto unreg_cpufreq;

	for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++)
		pr_cpufreq(INIT, "[%2d]:%7u KHz", i, freq_table[i].frequency);

#ifdef CONFIG_EARLYSUSPEND
	register_early_suspend(&shmobile_cpufreq_suspend);
#endif

	return ret;

unreg_cpufreq:
	cpufreq_unregister_driver(&shmobile_cpufreq_driver);
free_queue:
#ifdef DYNAMIC_HOTPLUG_CPU
	destroy_workqueue(dfs_queue);
#endif

	return ret;
}

static struct platform_driver cpufreq_driver = {
	.driver = {
		.name = "shmobile-cpufreq",
	},
};

static int __init cpufreq_drv_init(void)
{
	return platform_driver_probe(&cpufreq_driver, cpufreq_drv_probe);
}

module_init(cpufreq_drv_init);

/*
 * Append SYSFS interface for MAX/MIN frequency limitation control
 * path: /sys/power/{cpufreq_table, cpufreq_limit_max, cpufreq_limit_min}
 *	- cpufreq_table: listup all available frequencies
 *	- cpufreq_limit_max: maximum frequency current supported
 *	- cpufreq_limit_min: minimum frequency current supported
 */
static inline struct attribute *find_attr_by_name(struct attribute **attr,
				const char *name)
{
	/* lookup for an attibute(by name) from attribute list */
	while ((attr) && (*attr)) {
		if (strcmp(name, (*attr)->name) == 0)
			return *attr;
		attr++;
	}

	return NULL;
}
/*
 * show_available_freqs - show available frequencies
 * /sys/power/cpufreq_table
 */
static ssize_t show_available_freqs(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	unsigned int prev = 0;
	ssize_t count = 0;
	int i = 0;

	/* show all available freuencies */
	for (i = 0; (freq_table[i].frequency != CPUFREQ_TABLE_END); i++) {
		if (freq_table[i].frequency == prev)
			continue;
		count += sprintf(&buf[count], "%d ", freq_table[i].frequency);
		prev = freq_table[i].frequency;
	}
	count += sprintf(&buf[count], "\n");

	return count;
}
/* for cpufreq_limit_max & cpufreq_limit_min, create a shortcut from
 * default sysfs node (/sys/devices/system/cpu/cpu0/cpufreq/) so bellow
 * mapping table indicate the target name and alias which will be use
 * for creating shortcut.
 */
static struct {
	const char *target;
	const char *alias;
} attr_mapping[] = {
	{"scaling_max_freq", "cpufreq_max_limit"},
	{"scaling_min_freq", "cpufreq_min_limit"}
};

/*
 * cpufreq_max_limit/cpufreq_min_limit - show max/min frequencies
 * this handler is used for both MAX/MIN limit
 */
static ssize_t show_freq(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	ssize_t ret = -EINVAL;
	struct cpufreq_policy cur_policy;
	struct kobj_type *ktype = NULL;
	struct attribute *att = NULL;
	int i = 0;

	ret = cpufreq_get_policy(&cur_policy, 0);
	if (ret)
		return -EINVAL;

	ktype = get_ktype(&cur_policy.kobj);
	if (!ktype)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(attr_mapping); i++) {
		if (strcmp(attr->attr.name, attr_mapping[i].alias) == 0) {
			att = find_attr_by_name(ktype->default_attrs,
				attr_mapping[i].target);

			/* found the attibute, pass message to its ops */
			if (att && ktype->sysfs_ops && ktype->sysfs_ops->show)
					return ktype->sysfs_ops->show(
					&cur_policy.kobj, att, buf);
		}
	}

	return -EINVAL;
}

/*
 * cpufreq_max_limit/cpufreq_min_limit - store max/min frequencies
 * this handler is used for both MAX/MIN limit
 */
static ssize_t store_freq(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct cpufreq_policy *cur_policy;
	int freq = 0;
	unsigned int min_freq = 0;
	unsigned int max_freq = 0;

	if (sscanf(buf, "%d", &freq) != 1)
		return -EINVAL;

	cur_policy = cpufreq_cpu_get(0);
	if (!cur_policy)
		return -EINVAL;

	if (strcmp(attr->attr.name, "cpufreq_max_limit") == 0) {
		max_freq = freq < 0 ? cur_policy->cpuinfo.max_freq : freq;
		cpufreq_update_usr_lmt_req(max_freq, FREQ_MAX_LIMIT);
	} else if (strcmp(attr->attr.name, "cpufreq_min_limit") == 0) {
		min_freq = freq < 0 ? cur_policy->cpuinfo.min_freq : freq;
		cpufreq_update_usr_lmt_req(min_freq, FREQ_MIN_LIMIT);
	}

	cpufreq_cpu_put(cur_policy);
	return count;
}

static ssize_t show_cur_freq(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", the_cpuinfo.freq);
}

static ssize_t store_override(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int override;

	if (sscanf(buf, "%d", &override) != 1)
		return -EINVAL;

	the_cpuinfo.override_qos = !!override;

	return count;
}

static ssize_t show_override(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", the_cpuinfo.override_qos);
}

static struct kobj_attribute cpufreq_table_attribute =
	__ATTR(cpufreq_table, 0444, show_available_freqs, NULL);
static struct kobj_attribute cur_freq_attribute =
	__ATTR(cpufreq_cur_freq, 0444, show_cur_freq, NULL);
static struct kobj_attribute max_limit_attribute =
	__ATTR(cpufreq_max_limit, 0644, show_freq, store_freq);
static struct kobj_attribute min_limit_attribute =
	__ATTR(cpufreq_min_limit, 0644, show_freq, store_freq);
static struct kobj_attribute override_qos_attribute =
	__ATTR(cpufreq_override_qos, 0644, show_override, store_override);

/*
 * Create a group of attributes so that can create and destroy them all
 * at once.
 */
static struct attribute *attrs[] = {
	&min_limit_attribute.attr,
	&max_limit_attribute.attr,
	&cpufreq_table_attribute.attr,
	&cur_freq_attribute.attr,
	&override_qos_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

#ifdef CONFIG_DEBUG_FS
static int debugfs_hp_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t debugfs_read_lmt_req(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	u32 len = 0;
	char out_buf[1000];
	struct cpufreq_lmt_node *lmt_node;

	len += snprintf(out_buf + len, sizeof(out_buf) - len,
		"*** CPUFREQ min limit requests ***\n");
	plist_for_each_entry(lmt_node, &the_cpuinfo.min_lmt_list, node) {
		len += snprintf(out_buf + len,
				sizeof(out_buf) - len,
				"Name: %s, limit: %d, valid: %d\n",
				lmt_node->name, lmt_node->lmt,
				lmt_node->valid);
	}

	len += snprintf(out_buf + len, sizeof(out_buf) - len,
		"*** CPUFREQ max limit requests ***\n");
	plist_for_each_entry(lmt_node, &the_cpuinfo.max_lmt_list, node) {
		len += snprintf(out_buf + len,
				sizeof(out_buf) - len,
				"Name: %s, limit: %d, valid: %d\n",
				lmt_node->name, lmt_node->lmt,
				lmt_node->valid);
	}

	return simple_read_from_buffer(user_buf, count, ppos, out_buf,
				       len);
}

static const struct file_operations debugfs_lmt_req_ops = {
	.open = debugfs_hp_open,
	.read = debugfs_read_lmt_req,
};

static ssize_t debugfs_hp_disable_store(struct file *file,
			char const __user *buf, size_t count, loff_t *offset)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;
	char in_buf[10];
	int hp_en;

	memset(in_buf, 0, sizeof(in_buf));
	if (copy_from_user(in_buf, buf, count))
		return -EFAULT;

	sscanf(in_buf, "%d", &hp_en);

	spin_lock(&the_cpuinfo.lock);
	hpdata->hlg_dynamic_disable = !!hp_en;
	queue_work_on(0, dfs_queue, &hlg_work);
	spin_unlock(&the_cpuinfo.lock);

	return count;
}

static ssize_t debugfs_hp_disable_show(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;
	char out_buf[10];
	u32 len = 0;

	memset(out_buf, 0, sizeof(out_buf));
	len += snprintf(out_buf + len, sizeof(out_buf) - len,
			"%d\n", hpdata->hlg_dynamic_disable);

	return simple_read_from_buffer(user_buf, count, ppos,
			out_buf, len);
}

static const struct file_operations debugfs_hp_disable = {
	.open = debugfs_hp_open,
	.write = debugfs_hp_disable_store,
	.read = debugfs_hp_disable_show,
};

static ssize_t debugfs_hp_thresholds_show(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;
	struct sh_plat_hp_data *hpplat = hpdata->pdata;
	char out_buf[500];
	u32 len = 0;
	int idx;

	memset(out_buf, 0, sizeof(out_buf));
	len += snprintf(out_buf + len, sizeof(out_buf) - len,
				"cpu\t\tplugout\t\tplugin\n");

	for_each_possible_cpu(idx)
		len += snprintf(out_buf + len, sizeof(out_buf) - len,
			"cpu%u\t\t%u\t\t%u\n", idx,
			hpplat->thresholds[idx].thresh_plugout,
			hpplat->thresholds[idx].thresh_plugin);

	return simple_read_from_buffer(user_buf, count, ppos,
			out_buf, len);
}

static ssize_t debugfs_hp_thresholds_store(struct file *file,
			char const __user *buf, size_t count, loff_t *offset)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;
	struct sh_plat_hp_data *hpplat = hpdata->pdata;
	struct cpufreq_policy *cur_policy;
	char in_buf[100];
	u32 cpu, thresh_plugout, thresh_plugin;
	int ret = count;

	memset(in_buf, 0, sizeof(in_buf));

	if (count > sizeof(in_buf) - 1)
		return -EFAULT;

	if (copy_from_user(in_buf, buf, count))
		return -EFAULT;

	cur_policy = cpufreq_cpu_get(0);
	if (!cur_policy)
		return -EINVAL;

	sscanf(in_buf, "%u%u%u", &cpu, &thresh_plugout, &thresh_plugin);

	if (!cpu || !cpu_possible(cpu)) {
		ret = -EINVAL;
		goto out;
	}

	if (thresh_plugout > cur_policy->cpuinfo.max_freq ||
			thresh_plugout < cur_policy->cpuinfo.min_freq ||
			thresh_plugin > cur_policy->cpuinfo.max_freq ||
			thresh_plugin < cur_policy->cpuinfo.min_freq) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock(&the_cpuinfo.lock);
	hpplat->thresholds[cpu].thresh_plugout = thresh_plugout;
	hpplat->thresholds[cpu].thresh_plugin = thresh_plugin;
	spin_unlock(&the_cpuinfo.lock);

out:
	cpufreq_cpu_put(cur_policy);
	return ret;
}

static const struct file_operations debugfs_hp_thresholds = {
	.open = debugfs_hp_open,
	.write = debugfs_hp_thresholds_store,
	.read = debugfs_hp_thresholds_show,
};

static ssize_t debugfs_hp_samples_show(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;
	char out_buf[10];
	u32 len = 0;

	memset(out_buf, 0, sizeof(out_buf));
	len += snprintf(out_buf + len, sizeof(out_buf) - len,
		"%d %d\n", hpdata->in_samples, hpdata->out_samples);

	return simple_read_from_buffer(user_buf, count, ppos,
			out_buf, len);
}

static ssize_t debugfs_hp_samples_store(struct file *file,
			char const __user *buf, size_t count, loff_t *offset)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;
	struct sh_plat_hp_data *hpplat = hpdata->pdata;
	unsigned int in_sample, out_sample, *new_history;
	char in_buf[10];
	int ret = count;

	memset(in_buf, 0, sizeof(in_buf));
	if (copy_from_user(in_buf, buf, count))
		return -EFAULT;

	sscanf(in_buf, "%d%d", &in_sample, &out_sample);

	spin_lock(&the_cpuinfo.lock);
	if (in_sample > out_sample)
		goto unlock;

	if (out_sample <= hpplat->out_samples) {
		/* existing array is sufficient, update count and index */
		if (hpdata->history_idx >= out_sample)
			hpdata->history_idx = out_sample - 1;

		hpplat->in_samples = hpdata->in_samples = in_sample;
		hpplat->out_samples = hpdata->out_samples = out_sample;
		goto unlock;
	}

	new_history = kzalloc(sizeof(*hpdata->load_history) * out_sample,
					GFP_KERNEL);
	if (!new_history) {
		ret = -ENOMEM;
		goto unlock;
	}

	/* copy the existing buffer */
	memcpy(new_history, hpdata->load_history,
		sizeof(*hpdata->load_history) * hpplat->out_samples);
	kfree(hpdata->load_history);

	hpdata->load_history = new_history;
	hpplat->in_samples = hpdata->in_samples = in_sample;
	hpplat->out_samples = hpdata->out_samples = out_sample;

unlock:
	spin_unlock(&the_cpuinfo.lock);
	return ret;
}

static const struct file_operations debugfs_hp_samples = {
	.open = debugfs_hp_open,
	.write = debugfs_hp_samples_store,
	.read = debugfs_hp_samples_show,
};

static ssize_t debugfs_hp_es_samples_show(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;
	struct sh_plat_hp_data *hpplat = hpdata->pdata;
	char out_buf[10];
	u32 len = 0;

	memset(out_buf, 0, sizeof(out_buf));
	len += snprintf(out_buf + len, sizeof(out_buf) - len,
			"%d\n", hpplat->es_samples);

	return simple_read_from_buffer(user_buf, count, ppos,
			out_buf, len);
}

static ssize_t debugfs_hp_es_samples_store(struct file *file,
			char const __user *buf, size_t count, loff_t *offset)
{
	struct shmobile_hp_data *hpdata = &the_cpuinfo.hpdata;
	struct sh_plat_hp_data *hpplat = hpdata->pdata;
	unsigned int new_sample;
	char in_buf[10];
	int ret = count;

	memset(in_buf, 0, sizeof(in_buf));
	if (copy_from_user(in_buf, buf, count))
		return -EFAULT;

	sscanf(in_buf, "%d", &new_sample);
	spin_lock(&the_cpuinfo.lock);

	if (new_sample > hpplat->out_samples) {
		ret = -EINVAL;
		goto unlock;
	}

	hpplat->es_samples = new_sample;

unlock:
	spin_unlock(&the_cpuinfo.lock);

	return ret;
}

static const struct file_operations debugfs_hp_es_samples = {
	.open = debugfs_hp_open,
	.write = debugfs_hp_es_samples_store,
	.read = debugfs_hp_es_samples_show,
};

static struct dentry *debug_dir;

static int cpufreq_debug_init(void)
{
	struct dentry *debug_file;
	int ret = 0;

	debug_dir = debugfs_create_dir("sh_cpufreq", NULL);
	if (IS_ERR_OR_NULL(debug_dir)) {
		ret = PTR_ERR(debug_dir);
		goto err;
	}

#ifdef CONFIG_HOTPLUG_CPU
	debug_file = debugfs_create_file("hp_samples", S_IWUSR,
		debug_dir, NULL, &debugfs_hp_samples);
	if (IS_ERR_OR_NULL(debug_file)) {
		ret = PTR_ERR(debug_file);
		goto err;
	}

	debug_file = debugfs_create_file("hp_es_samples", S_IWUSR,
		debug_dir, NULL, &debugfs_hp_es_samples);
	if (IS_ERR_OR_NULL(debug_file)) {
		ret = PTR_ERR(debug_file);
		goto err;
	}

	debug_file = debugfs_create_file("hp_thresholds", S_IWUSR,
			debug_dir, NULL, &debugfs_hp_thresholds);
	if (IS_ERR_OR_NULL(debug_file)) {
		ret = PTR_ERR(debug_file);
		goto err;
	}

	debug_file = debugfs_create_file("hp_disable", S_IWUSR | S_IRUGO,
			debug_dir, NULL, &debugfs_hp_disable);
	if (IS_ERR_OR_NULL(debug_file)) {
		ret = PTR_ERR(debug_file);
		goto err;
	}

	debug_file = debugfs_create_u32("debug_mask", S_IWUSR | S_IRUGO,
			debug_dir, &hp_debug_mask);
	if (IS_ERR_OR_NULL(debug_file)) {
		ret = PTR_ERR(debug_file);
		goto err;
	}

#endif

	debug_file = debugfs_create_file("limit_requests", S_IWUSR | S_IRUGO,
			debug_dir, NULL, &debugfs_lmt_req_ops);
	if (IS_ERR_OR_NULL(debug_file)) {
		ret = PTR_ERR(debug_file);
		goto err;
	}

	return 0;
err:
	if (!IS_ERR_OR_NULL(debug_dir))
		debugfs_remove_recursive(debug_dir);

	return ret;
}

#else /* CONFIG_DEBUG_FS */

static int cpufreq_debug_init(void)
{
	return 0;
}

#endif /* CONFIG_DEBUG_FS */

static int shmobile_sysfs_init(void)
{
	int ret;
	/* Create the files associated with power kobject */
	ret = sysfs_create_group(power_kobj, &attr_group);
	if (ret) {
		pr_cpufreq(ERROR, "failed to create sysfs\n");
		return ret;
	}

	ret = cpufreq_debug_init();
	if (ret)
		pr_cpufreq(ERROR, "failed to create debugfs\n");

	return ret;
}
