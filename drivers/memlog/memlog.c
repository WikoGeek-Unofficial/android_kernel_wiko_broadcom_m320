/*
 * drivers/memlog/memlog.c
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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/memblock.h>
#include <memlog/memlog.h>
#include <mach/ramdump.h>
#include <mach/memory.h>

#include <trace/events/irq.h>
#include <trace/events/sched.h>
#include <trace/events/workqueue.h>

struct memlog_header {
	unsigned char proc_size;
	unsigned char irq_size;
	unsigned char func_size;
	unsigned char dump_size;
	unsigned int proc_count;
	unsigned int irq_count;
	unsigned int func_count;
	unsigned int dump_count;
	unsigned char timestamp_size;
	unsigned char timestamp_count;
	unsigned char nr_cpus;
	unsigned char reserved2;
} __packed;
static struct memlog_header mh;

struct proc_log_entry {
	unsigned long long time;
	unsigned long pid;
	char data[TASK_COMM_LEN];
} __packed;

struct irq_log_entry {
	unsigned long long time;
	unsigned long data;
} __packed;

struct func_log_entry {
	unsigned long long time;
	unsigned long data;
} __packed;

struct dump_log_entry {
	unsigned long long time;
	unsigned int id;
	int data;
} __packed;

struct timestamp_log_entry {
	unsigned long long time[MAX_TIMESTAMP];
} __packed;

static char *logdata;
static char *pm_area;
unsigned long memlog_capture;

static DEFINE_PER_CPU(struct proc_log_entry *, proc_log);
static DEFINE_PER_CPU(unsigned int, proc_idx) = -1;

static DEFINE_PER_CPU(struct irq_log_entry *, irq_log);
static DEFINE_PER_CPU(unsigned int, irq_idx) = -1;

static DEFINE_PER_CPU(struct func_log_entry *, func_log);
static DEFINE_PER_CPU(unsigned int, func_idx) = -1;

static DEFINE_PER_CPU(struct dump_log_entry *, dump_log);
static DEFINE_PER_CPU(unsigned int, dump_idx) = -1;

static DEFINE_PER_CPU(struct timestamp_log_entry *, ts_log);

static struct resource *memlog_resource;

void __iomem *memory_log_get_pm_area_va(void)
{
	return (void __iomem *)pm_area;
}
EXPORT_SYMBOL_GPL(memory_log_get_pm_area_va);


phys_addr_t memory_log_get_pm_area_pa(void)
{
	return (phys_addr_t)memlog_resource->start + (logdata - pm_area);
}
EXPORT_SYMBOL_GPL(memory_log_get_pm_area_pa);


static inline void memory_log_proc(const char *name, unsigned long pid)
{
	int *idx;
	struct proc_log_entry *pl;
	struct proc_log_entry log;

	if (!logdata || !name || !memlog_capture)
		return;

	idx = &get_cpu_var(proc_idx);
	(*idx)++;
	if (*idx >= mh.proc_count)
		*idx = 0;

	pl = __this_cpu_read(proc_log) + *idx;
	put_cpu_var(proc_log);

	memset(log.data, 0, TASK_COMM_LEN);
	strlcpy(log.data, name, TASK_COMM_LEN);
	log.time = local_clock();
	log.pid = pid;
	memcpy(pl, &log, sizeof(*pl));
}
EXPORT_SYMBOL_GPL(memory_log_proc);

static inline void memory_log_worker(unsigned long func_addr,
		unsigned long pid, int in)
{
	char str[TASK_COMM_LEN];
	if (in)
		snprintf(str, TASK_COMM_LEN, ">%lx", func_addr);
	else
		snprintf(str, TASK_COMM_LEN, "<%lx", func_addr);
	memory_log_proc(str, pid);
}
EXPORT_SYMBOL_GPL(memory_log_worker);

void memory_log_irq(unsigned int irq, int in)
{
	int *idx;
	struct irq_log_entry *il;
	struct irq_log_entry log;

	if (!logdata || !memlog_capture)
		return;

	idx = &get_cpu_var(irq_idx);
	(*idx)++;
	if (*idx >= mh.irq_count)
		*idx = 0;

	il = __this_cpu_read(irq_log) + *idx;
	put_cpu_var(irq_log);

	log.time = local_clock();
	log.data = irq | (in ? IRQ_LOG_ENTRY_IN : 0);
	memcpy(il, &log, sizeof(*il));
}
EXPORT_SYMBOL_GPL(memory_log_irq);

void memory_log_func(unsigned long func_id, int in)
{
	int *idx;
	struct func_log_entry *fl;
	struct func_log_entry log;
	unsigned long flags = 0;

	if (!logdata || !memlog_capture)
		return;

	local_irq_save(flags);
	idx = &get_cpu_var(func_idx);
	(*idx)++;
	if (*idx >= mh.func_count)
		*idx = 0;

	fl = __this_cpu_read(func_log) + *idx;
	put_cpu_var(func_log);
	local_irq_restore(flags);

	log.time = local_clock();
	log.data = func_id | (in ? FUNC_LOG_ENTRY_IN : 0);
	memcpy(fl, &log, sizeof(*fl));
}
EXPORT_SYMBOL_GPL(memory_log_func);

void memory_log_dump_int(unsigned int dump_id, int dump_data)
{
	int *idx;
	struct dump_log_entry *dl;
	struct dump_log_entry log;
	unsigned long flags = 0;

	if (!logdata || !memlog_capture)
		return;

	local_irq_save(flags);
	idx = &get_cpu_var(dump_idx);
	(*idx)++;
	if (*idx >= mh.dump_count)
		*idx = 0;

	dl = __this_cpu_read(dump_log) + *idx;
	put_cpu_var(dump_log);
	local_irq_restore(flags);

	log.time = local_clock();
	log.id = dump_id;
	log.data = dump_data;
	memcpy(dl, &log, sizeof(*dl));
}
EXPORT_SYMBOL_GPL(memory_log_dump_int);

void memory_log_timestamp(unsigned int id)
{
	struct timestamp_log_entry *tl;
	unsigned long long time;

	if (!logdata || !memlog_capture)
		return;

	tl = __this_cpu_read(ts_log);

	time = local_clock();
	memcpy(&tl->time[id], &time, sizeof(time));
}
EXPORT_SYMBOL_GPL(memory_log_timestamp);

/* ftrace probes */
static void notrace
probe_irq_handler_entry(void *ignore, int irq, struct irqaction *action)
{
	memory_log_irq(irq, 1);
}
static void notrace
probe_irq_handler_exit(void *ignore, int irq, struct irqaction *action, int ret)
{
	memory_log_irq(irq, 0);
}

static void notrace
probe_sched_switch(void *ignore,
		struct task_struct *prev, struct task_struct *next)
{
	memory_log_proc(next->comm, next->pid);
}

static void notrace
probe_workqueue_execute_start(void *ignore,
			  struct work_struct *work)
{
	memory_log_worker((unsigned long)work->func, task_pid_nr(current), 1);
}
static void notrace
probe_workqueue_execute_end(void *ignore,
			  struct work_struct *work)
{
	memory_log_worker((unsigned long)work->func, task_pid_nr(current), 0);
}

/* this is used as ratio to fill the buffers */
#define MIN_PROC_COUNT		1
#define MIN_IRQ_COUNT		2
#define MIN_FUNC_COUNT		2
#define MIN_DUMP_COUNT		2

static int __init init_memlog(void)
{
	int ret = 0;
	int cpu;

	unsigned int memlog_size;
	unsigned int per_cpu_buffers_min_size;
	unsigned int per_cpu_buffers_multiply;
	char *memlog_data;
	memlog_resource = get_mem_resource("memlog");
	memlog_size = resource_size(memlog_resource);

	if (memlog_resource->start > memblock_end_of_DRAM())
		logdata = (char __force *)ioremap_nocache(
				memlog_resource->start, memlog_size);
	else
		logdata = (char __force *)ioremap_wc(memlog_resource->start,
				memlog_size);
	if (!logdata) {
		pr_err("ioremap: failed");
		ret = -ENOMEM;
		goto exit;
	}

	memset(logdata, 0, memlog_size);

	register_ramdump_split("memlog", memlog_resource->start,
			memlog_resource->end);

	/* put pm are at the end of memlog data and check that is aligned
	 * when changing memlog location and size this makes it sure that
	 * pm area stays aligned. */
	pm_area = logdata + memlog_size - (PM_ENTRY_SIZE * num_possible_cpus());
	BUG_ON(!IS_ALIGNED((unsigned long)pm_area, PM_ENTRY_SIZE));

	/* Fill the memlog area dynamically. */
	/* subtract header & timestamp area & pm area to get buffer left for
	 * per cpu buffers */
	memlog_size -= sizeof(struct memlog_header);
	memlog_size -= sizeof(struct timestamp_log_entry) * num_possible_cpus();
	memlog_size -= PM_ENTRY_SIZE * num_possible_cpus();

	/* min space needed for memlog per cpu buffers */
	per_cpu_buffers_min_size = num_possible_cpus() *
			((sizeof(struct proc_log_entry) * MIN_PROC_COUNT) +
			(sizeof(struct irq_log_entry) * MIN_IRQ_COUNT) +
			(sizeof(struct func_log_entry) * MIN_FUNC_COUNT) +
			(sizeof(struct dump_log_entry) * MIN_DUMP_COUNT));

	per_cpu_buffers_multiply = memlog_size / per_cpu_buffers_min_size;

	memlog_data = logdata + sizeof(struct memlog_header);

	for_each_possible_cpu(cpu) {
		per_cpu(proc_log, cpu) = (struct proc_log_entry *)memlog_data;
		memlog_data += sizeof(struct proc_log_entry) * MIN_PROC_COUNT *
				per_cpu_buffers_multiply;
	}
	for_each_possible_cpu(cpu) {
		per_cpu(irq_log, cpu) = (struct irq_log_entry *)memlog_data;
		memlog_data += sizeof(struct irq_log_entry) * MIN_IRQ_COUNT *
				per_cpu_buffers_multiply;
	}
	for_each_possible_cpu(cpu) {
		per_cpu(func_log, cpu) = (struct func_log_entry *)memlog_data;
		memlog_data += sizeof(struct func_log_entry) * MIN_FUNC_COUNT *
				per_cpu_buffers_multiply;
	}
	for_each_possible_cpu(cpu) {
		per_cpu(dump_log, cpu) = (struct dump_log_entry *)memlog_data;
		memlog_data += sizeof(struct dump_log_entry) * MIN_DUMP_COUNT *
				per_cpu_buffers_multiply;
	}

	for_each_possible_cpu(cpu) {
		per_cpu(ts_log, cpu) =
				(struct timestamp_log_entry *)memlog_data;
		memlog_data += sizeof(struct timestamp_log_entry);
	}

	mh.proc_size = sizeof(struct proc_log_entry);
	mh.proc_count = MIN_PROC_COUNT * per_cpu_buffers_multiply;
	mh.irq_size = sizeof(struct irq_log_entry);
	mh.irq_count = MIN_IRQ_COUNT * per_cpu_buffers_multiply;
	mh.func_size = sizeof(struct func_log_entry);
	mh.func_count = MIN_FUNC_COUNT * per_cpu_buffers_multiply;
	mh.dump_size = sizeof(struct dump_log_entry);
	mh.dump_count = MIN_DUMP_COUNT * per_cpu_buffers_multiply;
	mh.timestamp_size = sizeof(struct timestamp_log_entry);
	mh.timestamp_count = MAX_TIMESTAMP;
	mh.nr_cpus = num_possible_cpus();
	mh.reserved2 = 0xFF;
	/* copy header to memlog non-cache area for decoder */
	memcpy(logdata, &mh, sizeof(struct memlog_header));

	/* everything ready to take events */
	smp_mb();
	memlog_capture = 1;

	ret = register_trace_irq_handler_entry(probe_irq_handler_entry, NULL);
	if (ret) {
		pr_err("Couldn't activate tracepoint probe to %pf\n",
				probe_irq_handler_entry);
	}

	ret = register_trace_irq_handler_exit(probe_irq_handler_exit, NULL);
	if (ret) {
		pr_err("Couldn't activate tracepoint probe to %pf\n",
				probe_irq_handler_exit);
	}

	ret = register_trace_sched_switch(probe_sched_switch, NULL);
	if (ret) {
		pr_err("Couldn't activate tracepoint probe to %pf\n",
				probe_sched_switch);
	}

	ret = register_trace_workqueue_execute_start(
			probe_workqueue_execute_start, NULL);
	if (ret) {
		pr_err("Couldn't activate tracepoint probe to %pf\n",
				probe_workqueue_execute_start);
	}

	ret = register_trace_workqueue_execute_end(
			probe_workqueue_execute_end, NULL);
	if (ret) {
		pr_err("Couldn't activate tracepoint probe to %pf\n",
				probe_workqueue_execute_end);
	}

	return 0;

exit:
	return ret;
}

pure_initcall(init_memlog);

MODULE_DESCRIPTION("Memory Log");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Renesas Mobile Corp.");

