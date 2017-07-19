/*******************************************************************************
*     Copyright 2014 Broadcom Corporation.  All rights reserved.
*
*          @file     kernel/power/cpu_stats.c
*
* Unless you and Broadcom execute a separate written software license agreement
* governing use of this software, this software is licensed to you under the
* terms of the GNU General Public License version 2, available at
*          http://www.gnu.org/copyleft/gpl.html (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a license
* other than the GPL, without Broadcom's express prior written consent.
*******************************************************************************/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpuidle.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

struct cpu_state {
	bool stats_en;
	int num_of_freqs;
	int num_of_idlestates;
	u32 act_freq_inx;
	ktime_t start_time;
};

struct cpu_stats {
	bool cpu_in_idle;
	bool cpu_offlined;
	u64 *freq_stats;
	u64 *idle_stats_base;
	ktime_t last_time;
	ktime_t offline_enter_time;
	u64 total_offline_time;
};

static struct cpu_state state;
static DEFINE_PER_CPU(struct cpu_stats, stats_table);

static spinlock_t cpu_stats_spinlock;
struct cpufreq_frequency_table *freq_table;
static struct dentry *stats_dir, *stats_enable_dentry, *stats_display_dentry;


static u64 cpu_stats_freq_inx_update(int cpu, int freq_inx)
{
	struct cpu_stats *stats;
	ktime_t cur_time;

	cur_time = ktime_get();
	stats = &per_cpu(stats_table, cpu);
	return (u64)stats->freq_stats[freq_inx] +
		ktime_to_us(ktime_sub(cur_time, stats->last_time));
}

static int cpu_stats_cpufreq_update(int freq_inx)
{
	struct cpu_stats *stats;
	int cpu;
	ktime_t cur_time;

	cur_time = ktime_get();
	spin_lock(&cpu_stats_spinlock);
	for_each_possible_cpu(cpu) {
		stats = &per_cpu(stats_table, cpu);
		if ((stats->cpu_in_idle == false) &&
			(stats->cpu_offlined == false)) {
			stats->freq_stats[state.act_freq_inx] =
				cpu_stats_freq_inx_update(cpu,
					state.act_freq_inx);
			stats->last_time = cur_time;
		}
	}
	state.act_freq_inx = freq_inx;
	spin_unlock(&cpu_stats_spinlock);
	return 0;
}

static int cpu_stats_enable(bool en)
{
	struct cpuidle_device *dev;
	struct cpufreq_policy *policy;
	struct cpu_stats *stats;
	int i, cpu, freq_inx = -1;
	ktime_t cur_time;

	if (!en) {
		state.stats_en = 0;
		return 0;
	}

	cur_time = ktime_get();
	policy = cpufreq_cpu_get(0);
	if (!policy) {
		state.stats_en = 0;
		cpufreq_cpu_put(policy);
		return 0;
	}
	for_each_possible_cpu(cpu) {
		stats = &per_cpu(stats_table, cpu);
		stats->last_time = cur_time;
		for (i = 0; i < state.num_of_freqs; i++) {
			stats->freq_stats[i] = 0;
			if (policy->cur == freq_table[i].frequency)
				freq_inx = i;
		}
		dev = per_cpu(cpuidle_devices, cpu);
		for (i = 0; i < state.num_of_idlestates; i++)
			stats->idle_stats_base[i] = dev->states_usage[i].time;
		stats->cpu_in_idle = false;
		stats->total_offline_time = 0;
		stats->cpu_offlined = !cpu_online(cpu);
		if (stats->cpu_offlined)
			stats->offline_enter_time = ktime_get();
	}
	state.start_time = cur_time;
	state.act_freq_inx = (u32)freq_inx;
	state.stats_en = 1;
	cpufreq_cpu_put(policy);
	cpu_stats_cpufreq_update(freq_inx);
	return 0;
}

static u64 usec_to_msec(u64 time)
{
	do_div(time, USEC_PER_MSEC);
	return (u64)time;
}

static u32 cpu_stats_percentage(u64 cur_time, u64 opp_time)
{
	u64 total_time;

	total_time = cur_time - ktime_to_us(state.start_time);
	opp_time *= 100;
	do_div(opp_time, total_time);
	return (u32)opp_time;
}

static int append_to_buffer(struct seq_file *m, u64 act_time, ktime_t cur_time)
{
	return seq_printf(m, "%8llu(%u%c)\t", usec_to_msec(act_time),
		cpu_stats_percentage(ktime_to_us(cur_time), act_time), '%');
}

static ssize_t cpu_stats_print_logs(struct seq_file *m)
{
	struct cpu_stats *stats;
	struct cpuidle_driver *drv;
	struct cpuidle_device *dev;
	int i, j, cpu;
	u64 act_time, offline_time;
	ktime_t cur_time;

	for (i = 0; i < state.num_of_freqs; i++)
		seq_printf(m, "%10uKHz   ", freq_table[i].frequency);

	drv = cpuidle_get_driver();
	for (i = 0; i < state.num_of_idlestates; i++)
		seq_printf(m, "%9s\t", drv->states[i].name);

	seq_printf(m, "%13s\t", "Offline");

	seq_puts(m, "\n");

	cur_time = ktime_get();
	for_each_possible_cpu(cpu) {
		stats = &per_cpu(stats_table, cpu);
		for (i = 0; i < state.num_of_freqs; i++) {
			if (stats->cpu_in_idle == false &&
				stats->cpu_offlined == false &&
				i == state.act_freq_inx)
				act_time = cpu_stats_freq_inx_update(cpu, i);
			else
				act_time = stats->freq_stats[i];

			append_to_buffer(m, act_time, cur_time);
		}
		dev = per_cpu(cpuidle_devices, cpu);
		for (j = 0; j < state.num_of_idlestates; j++) {
			act_time = dev->states_usage[j].time -
				stats->idle_stats_base[j];
			append_to_buffer(m, act_time, cur_time);
		}

		if (stats->cpu_offlined)
			offline_time = stats->total_offline_time + ktime_to_us(
				ktime_sub(cur_time, stats->offline_enter_time));
		else
			offline_time = stats->total_offline_time;
		append_to_buffer(m, offline_time, cur_time);

		seq_puts(m, "\n");
	}

	seq_printf(m, "Total time:%llums\n", ktime_to_ms(ktime_sub(cur_time,
			state.start_time)));
	return 0;
}

static int cpu_stats_freq_change_handler(struct notifier_block *nb,
	unsigned long val, void *data)
{
	struct cpufreq_freqs *freq;
	struct cpufreq_policy *policy;
	unsigned int index;

	if (val != CPUFREQ_POSTCHANGE)
		return 0;

	freq = (struct cpufreq_freqs *)data;
	policy = cpufreq_cpu_get(0);
	if (!policy) {
		cpufreq_cpu_put(policy);
		return 0;
	}
	cpufreq_frequency_table_target(policy, freq_table, freq->new,
		CPUFREQ_RELATION_L, &index);
	cpufreq_cpu_put(policy);
	cpu_stats_cpufreq_update(index);
	return 0;
}

static struct notifier_block freq_change_notifier_block = {
	.notifier_call = cpu_stats_freq_change_handler,
};

static int cpu_stats_idle_state_handler(struct notifier_block *nb,
	unsigned long val, void *data)
{
	struct cpu_stats *stats;
	int cpu = smp_processor_id();

	stats = &per_cpu(stats_table, cpu);
	switch (val) {
	case IDLE_START:
		cpu_stats_cpufreq_update(state.act_freq_inx);
		stats->cpu_in_idle = true;
		break;
	case IDLE_END:
		stats->cpu_in_idle = false;
		stats->last_time = ktime_get();
		break;
	}
	return 0;
}

static struct notifier_block idle_state_notifier_block = {
	.notifier_call = cpu_stats_idle_state_handler,
};

static int cpu_state_handler(struct notifier_block *nb,
	unsigned long val, void *hcpu)
{
	struct cpu_stats *stats = &per_cpu(stats_table, (long)hcpu);

	switch (val) {
	case CPU_DEAD:
		cpu_stats_cpufreq_update(state.act_freq_inx);
		stats->cpu_offlined = true;
		stats->offline_enter_time = ktime_get();
		break;
	case CPU_ONLINE:
		stats->cpu_offlined = false;
		stats->total_offline_time +=
			(u64) ktime_to_us(ktime_sub(ktime_get(),
						stats->offline_enter_time));
		stats->last_time = ktime_get();
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block cpu_notifier_block = {
	.notifier_call = cpu_state_handler,
};

static int show_stats_enable(struct seq_file *m, void *unused)
{
	return seq_printf(m, "%u\n", state.stats_en);
}

static int store_stats_enable(struct file *file, const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	bool stats_enable;
	int n;
	char buf[32];

	if (copy_from_user(buf, user_buf, sizeof(buf)))
		return -EFAULT;

	sscanf(buf, "%d\n", &n);
	stats_enable = !!n;
	if (state.stats_en == stats_enable)
		return count;

	if (stats_enable == 1) {
		cpufreq_register_notifier(&freq_change_notifier_block,
			CPUFREQ_TRANSITION_NOTIFIER);
		idle_notifier_register(&idle_state_notifier_block);
		register_cpu_notifier(&cpu_notifier_block);
		cpu_stats_enable(1);
	} else {
		cpufreq_unregister_notifier(&freq_change_notifier_block,
			CPUFREQ_TRANSITION_NOTIFIER);
		idle_notifier_unregister(&idle_state_notifier_block);
		unregister_cpu_notifier(&cpu_notifier_block);
		cpu_stats_enable(0);
	}
	return count;
}

static int open_stats_enable(struct inode *inode, struct file *file)
{
	return single_open(file, show_stats_enable, NULL);
}

static const struct file_operations stats_enable_fops = {
	.open = open_stats_enable,
	.read = seq_read,
	.write = store_stats_enable,
	.llseek = seq_lseek,
	.release = single_release,
};

static int show_stats_display(struct seq_file *m, void *unused)
{
	if (!state.stats_en)
		return seq_puts(m, "cpu stats are not enabled\n");
	return cpu_stats_print_logs(m);
}

static int open_stats_display(struct inode *inode, struct file *file)
{
	return single_open(file, show_stats_display, NULL);
}

static const struct file_operations stats_display_fops = {
	.open = open_stats_display,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int cpu_stats_debugfs_init(void)
{
	stats_dir = debugfs_create_dir("cpu_stats", NULL);
	if (!stats_dir)
		return -ENODEV;

	stats_enable_dentry = debugfs_create_file("enable", S_IRWXU|S_IRWXG,
		stats_dir, NULL, &stats_enable_fops);
	stats_display_dentry = debugfs_create_file("display", S_IRUGO,
		stats_dir, NULL, &stats_display_fops);
	if (!stats_display_dentry && !stats_enable_dentry)
		return -ENODEV;

	return 0;
}



int cpu_stats_init(void)
{
	struct cpuidle_driver *drv;
	struct cpu_stats *stats;
	int i = 0, ret;

	drv = cpuidle_get_driver();
	if (!drv || (cpufreq_get_current_driver() == NULL)) {
		pr_err("cpufreq/cpuidle drivers are not registered\n");
		return -EINVAL;
	}

	state.num_of_idlestates = drv->state_count;
	freq_table = cpufreq_frequency_get_table(0);
	while (freq_table[i].frequency != CPUFREQ_TABLE_END)
		i++;
	state.num_of_freqs = i;

	for_each_possible_cpu(i) {
		stats = &per_cpu(stats_table, i);
		stats->freq_stats = kcalloc(state.num_of_freqs, sizeof(u64),
			GFP_KERNEL);
		stats->idle_stats_base = kcalloc(state.num_of_idlestates,
			sizeof(u64), GFP_KERNEL);
	}
	ret = cpu_stats_debugfs_init();
	if (ret)
		goto err;
	return 0;

err:
	for_each_possible_cpu(i) {
		stats = &per_cpu(stats_table, i);
		kfree(stats->freq_stats);
		kfree(stats->idle_stats_base);
	}
	return ret;
}
late_initcall(cpu_stats_init);
