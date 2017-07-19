/*
 * rtds_memory_drv_main.c
 *	 RT domain shared memory driver API function file.
 *
 * Copyright (C) 2012-2013 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/jiffies.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include <system_memory.h>
#include "log_kernel.h"
#include "rtds_memory_drv.h"
#include "rtds_memory_drv_main.h"
#include "rtds_memory_drv_common.h"
#include "rtds_memory_drv_sub.h"
#include "iccom_drv.h"
#include "system_rtload_internal.h"

/* file operation */
static const struct file_operations	g_rtds_memory_fops = {
	.owner			= THIS_MODULE,
	.open			= rtds_memory_drv_open,
	.release		= rtds_memory_drv_close,
	.mmap			= rtds_memory_drv_mapping,
	.unlocked_ioctl		= rtds_memory_drv_ioctl
};

/* miscdevice info */
static struct miscdevice	g_rtds_memory_device;

/* Asynchronous thread info */
static struct task_struct	*g_rtds_memory_thread_info;

/* Section info */
rtds_memory_section_info	g_rtds_memory_section_info;

/* ICCOM driver handle */
void				*g_rtds_memory_iccom_handle;

struct completion		g_rtds_memory_completion;
struct semaphore		g_rtds_memory_apmem_rttrig_sem;
spinlock_t			g_rtds_memory_lock_recv_queue;
spinlock_t			g_rtds_memory_lock_cache_all;
struct list_head		g_rtds_memory_list_rcv_event;
static unsigned int		g_rtds_memory_mpro_startup_flag;
spinlock_t			g_rtds_memory_lock_create_mem;
struct list_head		g_rtds_memory_list_create_mem;
struct list_head		g_rtds_memory_list_mpro;
struct semaphore		g_rtds_memory_mpro_sem;
spinlock_t			g_rtds_memory_lock_mpro;
struct list_head		g_rtds_memory_list_shared_mem;
struct list_head		g_rtds_memory_list_leak_mpro;
struct semaphore		g_rtds_memory_shared_mem;
struct list_head		g_rtds_memory_list_map_rtmem;
spinlock_t			g_rtds_memory_lock_map_rtmem;
struct list_head		g_rtds_memory_list_reg_phymem;
struct semaphore		g_rtds_memory_phy_mem;
struct semaphore		g_rtds_memory_send_sem;
long				g_rtds_memory_sem_jiffies;
static struct proc_dir_entry	*g_rtds_memory_proc_entry;
static struct proc_dir_entry	*g_rtds_memory_proc_mem_info;
static struct proc_dir_entry	*g_rtds_memory_proc_mpro;

#ifdef RTDS_SUPPORT_DRM
spinlock_t			g_rtds_memory_lock_cma;
struct list_head		g_rtds_memory_list_cma;
#endif

static ssize_t rtds_mem_procfile_read(struct seq_file *, void *);
static int rtds_get_cmdline(pid_t pid, char *buf, int len);

/*Debugfs dentry structures*/
static struct dentry *g_rtds_memory_debug_dentry;
static struct dentry *g_rtds_memory_debug_shared_mem_entry;
static struct dentry *g_rtds_memory_debug_mpro_entry;
static struct dentry *g_rtds_memory_debug_leak_mpro_entry;
static struct dentry *g_rtds_memory_debug_map_rtmem_entry;
static struct dentry *g_rtds_memory_debug_create_mem_entry;
static struct dentry *g_rtds_memory_debug_rcv_event_entry;
static struct dentry *g_rtds_memory_debug_reg_phymem_entry;

static int rtds_memory_debug_shared_mem_show(struct seq_file *s, void *unused)
{
	rtds_memory_app_memory_table *mem_table;
	char task_comm[256];
	char *cmdline = task_comm;
	phys_addr_t phys_addr;
	unsigned long total_size = 0;

	seq_puts(s, "RTDS: Mpro Mapping List\n");
	RTDS_MEM_DOWN_TIMEOUT(&g_rtds_memory_shared_mem);
	if (list_empty(&g_rtds_memory_list_shared_mem)) {
		seq_puts(s, "(empty)\n");
		goto shared_mem_list_empty;
	}
	seq_printf(s,
		   "%4.s %5.s %10.s %12.s %12.s %10.s %10.s %8.s %8s %8.s %10.s %8.s %8.s %s\n",
		   "pid", "tgid", "size", "rt_wb_addr", "rt_nc_addr",
		   "phys_addr", "app_addr", "mem_attr", "apmem_id", "event",
		   "app_cache", "rt_cache", "trigger", "name");
	seq_puts(s, "--------------------------------------------------\n");
	list_for_each_entry(mem_table, &g_rtds_memory_list_shared_mem,
			    list_head) {
		total_size += mem_table->memory_size;
		if (rtds_get_cmdline(mem_table->tgid, task_comm,
				    sizeof(task_comm)))
			cmdline = task_comm;
		else
			cmdline = mem_table->task_info->comm;
		if (mem_table->pages == NULL)
			phys_addr = 0x00000000;
		else
			phys_addr = page_to_phys(*mem_table->pages);
		seq_printf(s, "%4u %5u %7lu KB %12lx %12lx %10x %10lx %8u %8u %8lu %10lu %8lu %8d %s\n",
				mem_table->pid, mem_table->tgid,
				(mem_table->memory_size)>>10,
				mem_table->rt_wb_addr, mem_table->rt_nc_addr,
				phys_addr, mem_table->app_addr,
				mem_table->mem_attr, mem_table->apmem_id,
				mem_table->event, mem_table->app_cache,
				mem_table->rt_cache, mem_table->trigger,
				cmdline);
	}
	seq_puts(s, "--------------------------------------------------\n");
	seq_printf(s, "Total: %lu KB\n", total_size>>10);
	seq_puts(s, "--------------------------------------------------\n");
shared_mem_list_empty:
	up(&g_rtds_memory_shared_mem);

	return 0;
}

static int rtds_memory_debug_shared_mem_open(struct inode *inode,
					     struct file *file)
{
	return single_open(file, rtds_memory_debug_shared_mem_show,
			   inode->i_private);
}

static const struct file_operations rtds_memory_debug_shared_mem_fops = {
	.open = rtds_memory_debug_shared_mem_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtds_memory_debug_mpro_show(struct seq_file *s, void *unused)
{
	rtds_memory_app_memory_table *mem_table;
	char task_comm[256];
	char *cmdline = task_comm;
	phys_addr_t phys_addr;
	unsigned long flag, total_size = 0;

	seq_puts(s, "RTDS: Mpro Events List\n");
	spin_lock_irqsave(&g_rtds_memory_lock_mpro, flag);
	if (list_empty(&g_rtds_memory_list_mpro)) {
		seq_puts(s, "(empty)\n");
		goto mpro_list_empty;
	}
	seq_printf(s,
		   "%4.s %5.s %10.s %12.s %12.s %10.s %8s %8.s %8.s %8.s %8.s %s\n",
		   "pid", "tgid", "size", "rt_wb_addr", "rt_nc_addr",
		   "phys_addr", "apmem_id", "event", "app_cache",
		   "rt_cache", "trigger", "name");
	seq_puts(s, "--------------------------------------------------\n");
	list_for_each_entry(mem_table, &g_rtds_memory_list_mpro,
			    list_head_mpro) {
		if (rtds_get_cmdline(mem_table->tgid, task_comm,
				    sizeof(task_comm)))
			cmdline = task_comm;
		else
			cmdline = mem_table->task_info->comm;
		if (mem_table->pages == NULL)
			phys_addr = 0x00000000;
		else
			phys_addr = page_to_phys(*mem_table->pages);
		total_size += mem_table->memory_size;
		seq_printf(s, "%4u %5u %7lu KB %12lx %12lx %10x %8u %8lu %8lu %8lu %8d %s\n",
				mem_table->pid, mem_table->tgid,
				(mem_table->memory_size)>>10,
				mem_table->rt_wb_addr, mem_table->rt_nc_addr,
				phys_addr, mem_table->apmem_id,
				mem_table->event, mem_table->app_cache,
				mem_table->rt_cache, mem_table->trigger,
				cmdline);
	}
	seq_puts(s, "--------------------------------------------------\n");
	seq_printf(s, "Total: %lu KB\n", total_size>>10);
	seq_puts(s, "--------------------------------------------------\n");
mpro_list_empty:
	spin_unlock_irqrestore(&g_rtds_memory_lock_mpro, flag);

	return 0;
}

static int rtds_memory_debug_mpro_open(struct inode *inode,
					     struct file *file)
{
	return single_open(file, rtds_memory_debug_mpro_show,
			   inode->i_private);
}

static const struct file_operations rtds_memory_debug_mpro_fops = {
	.open = rtds_memory_debug_mpro_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtds_memory_debug_leak_mpro_show(struct seq_file *s, void *unused)
{
	rtds_memory_app_memory_table *mem_table;
	char task_comm[256];
	char *cmdline = task_comm;
	phys_addr_t phys_addr;
	unsigned long total_size = 0;

	seq_puts(s, "RTDS: Mpro Leak List\n");
	RTDS_MEM_DOWN_TIMEOUT(&g_rtds_memory_shared_mem);
	if (list_empty(&g_rtds_memory_list_leak_mpro)) {
		seq_puts(s, "(empty)\n");
		goto mpro_leak_list_empty;
	}
	seq_printf(s,
		   "%4.s %5.s %10.s %12.s %12.s %10.s %8s %8.s %8.s %8.s %8.s %s\n",
		   "pid", "tgid", "size", "rt_wb_addr", "rt_nc_addr",
		   "phys_addr", "apmem_id", "event", "app_cache",
		   "rt_cache", "trigger", "name");
	seq_puts(s, "--------------------------------------------------\n");
	list_for_each_entry(mem_table, &g_rtds_memory_list_leak_mpro,
			    list_head_leak) {
		if (rtds_get_cmdline(mem_table->tgid, task_comm,
				    sizeof(task_comm)))
			cmdline = task_comm;
		else
			cmdline = mem_table->task_info->comm;
		if (mem_table->pages == NULL)
			phys_addr = 0x00000000;
		else
			phys_addr = page_to_phys(*mem_table->pages);
		total_size += mem_table->memory_size;
		seq_printf(s, "%4u %5u %7lu KB %12lx %12lx %10x %8u %8lu %8lu %8lu %8d %s\n",
				mem_table->pid, mem_table->tgid,
				(mem_table->memory_size)>>10,
				mem_table->rt_wb_addr, mem_table->rt_nc_addr,
				phys_addr, mem_table->apmem_id,
				mem_table->event, mem_table->app_cache,
				mem_table->rt_cache, mem_table->trigger,
				cmdline);
	}
	seq_puts(s, "--------------------------------------------------\n");
	seq_printf(s, "Total: %lu KB\n", total_size>>10);
	seq_puts(s, "--------------------------------------------------\n");
mpro_leak_list_empty:
	up(&g_rtds_memory_shared_mem);

	return 0;
}

static int rtds_memory_debug_leak_mpro_open(struct inode *inode,
					     struct file *file)
{
	return single_open(file, rtds_memory_debug_leak_mpro_show,
			   inode->i_private);
}

static const struct file_operations rtds_memory_debug_leak_mpro_fops = {
	.open = rtds_memory_debug_leak_mpro_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtds_memory_debug_map_rtmem_show(struct seq_file *s, void *unused)
{
	rtds_memory_rtmem_table *rtmem_table;
	unsigned long flag;

	seq_puts(s, "RTDS: RT Shared Memory Mapping\n");
	spin_lock_irqsave(&g_rtds_memory_lock_map_rtmem, flag);
	if (list_empty(&g_rtds_memory_list_map_rtmem)) {
		seq_puts(s, "(empty)\n");
		goto rtmem_map_list_empty;
	}
	seq_printf(s, "%4.s %5.s %12.s %16.s\n",
		      "pid", "tgid", "rt_addr", "open_count");
	seq_puts(s, "--------------------------------------------------\n");
	list_for_each_entry(rtmem_table, &g_rtds_memory_list_map_rtmem,
			    list_head) {
		seq_printf(s, "%4u %5u %12lx %16lu\n",
			      rtmem_table->pid, rtmem_table->tgid,
			      rtmem_table->rt_addr, rtmem_table->open_count);
	}
	seq_puts(s, "--------------------------------------------------\n");
rtmem_map_list_empty:
	spin_unlock_irqrestore(&g_rtds_memory_lock_map_rtmem, flag);

	return 0;
}

static int rtds_memory_debug_map_rtmem_open(struct inode *inode,
					    struct file *file)
{
	return single_open(file, rtds_memory_debug_map_rtmem_show,
			   inode->i_private);
}

static const struct file_operations rtds_memory_debug_map_rtmem_fops = {
	.open = rtds_memory_debug_map_rtmem_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtds_memory_debug_create_mem_show(struct seq_file *s, void *unused)
{
	rtds_memory_create_queue *create_list;
	char task_comm[256];
	char *cmdline = task_comm;
	unsigned long flag, total_size = 0;

	seq_puts(s, "RTDS: Physical Memory List\n");
	spin_lock_irqsave(&g_rtds_memory_lock_create_mem, flag);
	if (list_empty(&g_rtds_memory_list_create_mem)) {
		seq_puts(s, "(empty)\n");
		goto create_mem_list_empty;
	}
	seq_printf(s, "%4.s %5.s %10.s %12.s %10.s %10.s %s\n",
		      "pid", "tgid", "size", "app_addr", "app_cache",
		      "phys_addr", "name");
	seq_puts(s, "--------------------------------------------------\n");
	list_for_each_entry(create_list, &g_rtds_memory_list_create_mem,
			    queue_header) {
		if (rtds_get_cmdline(create_list->tgid, task_comm,
				    sizeof(task_comm)))
			cmdline = task_comm;
		else
			cmdline = create_list->task_info->comm;
		total_size += create_list->mem_size;
		seq_printf(s, "%4u %5u %7u KB %12lx %10lu %10x %s\n",
			      create_list->pid, create_list->tgid,
			      (create_list->mem_size>>10),
			      create_list->app_addr, create_list->app_cache,
			      page_to_phys(create_list->page), cmdline);
	}
	seq_puts(s, "--------------------------------------------------\n");
	seq_printf(s, "Total: %lu KB\n", total_size>>10);
	seq_puts(s, "--------------------------------------------------\n");
create_mem_list_empty:
	spin_unlock_irqrestore(&g_rtds_memory_lock_create_mem, flag);

	return 0;
}

static int rtds_memory_debug_create_mem_open(struct inode *inode,
					     struct file *file)
{
	return single_open(file, rtds_memory_debug_create_mem_show,
			   inode->i_private);
}

static const struct file_operations rtds_memory_debug_create_mem_fops = {
	.open = rtds_memory_debug_create_mem_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtds_memory_debug_rcv_event_show(struct seq_file *s, void *unused)
{
	rtds_memory_rcv_event_queue *rcv_event;
	unsigned long flag, total_size = 0;

	seq_puts(s, "RTDS: RT Triggered Events List\n");
	spin_lock_irqsave(&g_rtds_memory_lock_recv_queue, flag);
	if (list_empty(&g_rtds_memory_list_rcv_event)) {
		seq_puts(s, "(empty)\n");
		goto rcv_event_list_empty;
	}
	seq_printf(s,
		   "%5.s %10.s %8.s %10.s %8.s %12.s %8.s %10.s %10.s %8.s\n",
		   "event", "size", "rt_cache", "rt_trigger", "apmem_id",
		   "phys_addr", "mem_attr", "leak_data", "leak_size",
		   "map_id");
	seq_puts(s, "--------------------------------------------------\n");
	list_for_each_entry(rcv_event, &g_rtds_memory_list_rcv_event,
			    queue_header) {
		total_size += rcv_event->mem_size;
		seq_printf(s,
			   "%5u %7u KB %8u %10u %8u %12x %8u %10u %10u %8u\n",
			   rcv_event->event, (rcv_event->mem_size)>>10,
			   rcv_event->rt_cache, rcv_event->rt_trigger,
			   rcv_event->apmem_id, rcv_event->phys_addr,
			   rcv_event->mem_attr, *rcv_event->leak_data,
			   rcv_event->leak_size, rcv_event->map_id);
	}
	seq_puts(s, "--------------------------------------------------\n");
	seq_printf(s, "Total: %lu KB\n", total_size>>10);
	seq_puts(s, "--------------------------------------------------\n");
rcv_event_list_empty:
	spin_unlock_irqrestore(&g_rtds_memory_lock_recv_queue, flag);

	return 0;
}
static int rtds_memory_debug_rcv_event_open(struct inode *inode,
					    struct file *file)
{
	return single_open(file, rtds_memory_debug_rcv_event_show,
			   inode->i_private);
}

static const struct file_operations rtds_memory_debug_rcv_event_fops = {
	.open = rtds_memory_debug_rcv_event_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int rtds_memory_debug_reg_phymem_show(struct seq_file *s, void *unused)
{
	rtds_memory_phymem_table *phymem_table;
	unsigned long total_size = 0;

	seq_puts(s, "RTDS: Contiguous Physical Memory List\n");
	RTDS_MEM_DOWN_TIMEOUT(&g_rtds_memory_phy_mem);
	if (list_empty(&g_rtds_memory_list_reg_phymem)) {
		seq_puts(s, "(empty)\n");
		goto reg_phymem_list_empty;
	}
	seq_printf(s, "%12.s %10.s %12.s\n", "phy_addr", "map_size", "rt_addr");
	seq_puts(s, "--------------------------------------------------\n");
	list_for_each_entry(phymem_table, &g_rtds_memory_list_reg_phymem,
			    list_header) {
		total_size += phymem_table->map_size;
		seq_printf(s, "%12lx %7lu KB %12lx\n",
			      phymem_table->phy_addr,
			      (phymem_table->map_size)>>10,
			      phymem_table->rt_addr);
	}
	seq_puts(s, "--------------------------------------------------\n");
	seq_printf(s, "Total: %lu KB\n", total_size>>10);
	seq_puts(s, "--------------------------------------------------\n");
reg_phymem_list_empty:
	up(&g_rtds_memory_phy_mem);

	return 0;
}

static int rtds_memory_debug_reg_phymem_open(struct inode *inode,
					     struct file *file)
{
	return single_open(file, rtds_memory_debug_reg_phymem_show,
			   inode->i_private);
}

static const struct file_operations rtds_memory_debug_reg_phymem_fops = {
	.open = rtds_memory_debug_reg_phymem_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*****************************************************************************
 * Function   : rtds_memory_drv_open
 * Description: This function open RTDS MEMORY driver.
 * Parameters : inode   -   inode information
 *		fp	-   file descriptor
 * Returns    : SMAP_OK		-   Success
 *		SMAP_MEMORY	-   No memory
 *		SMAP_NG		-   FatalError
 *****************************************************************************/
int rtds_memory_drv_open(
		struct inode	*inode,
		struct file	*fp
)
{
	rtds_memory_drv_data		*rtds_memory_data_p;
	rtds_memory_mpro_control	*mpro_control_p;

	MSG_MED("[RTDSK]IN |[%s]\n", __func__);
	MSG_MED("[RTDSK]   |inode_p[0x%08X]\n", (u32)inode);
	MSG_MED("[RTDSK]   |fp[0x%08X]\n", (u32)fp);
	MSG_LOW("[RTDSK]   |major[%d]\n", imajor(inode));
	MSG_LOW("[RTDSK]   |minor[%d]\n", iminor(inode));

	/* Allocate memory for rtds_memory_mpro_control */
	mpro_control_p = kmalloc(sizeof(*mpro_control_p), GFP_KERNEL);
	if (NULL == mpro_control_p) {
		MSG_ERROR("[RTDSK]ERR| rtds_memory_mpro_control NotMemory\n");
		MSG_MED("[RTDSK]OUT|[%s] ret = SMAP_MEMORY\n", __func__);
		return SMAP_MEMORY;
	}

	/* Allocate memory for rtds_memory_drv_data */
	rtds_memory_data_p = kmalloc(sizeof(*rtds_memory_data_p), GFP_KERNEL);
	if (NULL == rtds_memory_data_p) {
		MSG_ERROR("[RTDSK]ERR| rtds_memory_drv_data NotMemory\n");
		MSG_MED("[RTDSK]OUT|[%s] ret = SMAP_MEMORY\n", __func__);
		kfree(mpro_control_p);
		return SMAP_MEMORY;
	}

	memset(mpro_control_p, 0, sizeof(*mpro_control_p));
	memset(rtds_memory_data_p, 0, sizeof(*rtds_memory_data_p));

	fp->private_data = rtds_memory_data_p;
	rtds_memory_data_p->mpro_control = mpro_control_p;

	MSG_MED("[RTDSK]OUT|[%s]ret = SMAP_OK\n", __func__);

	return SMAP_OK;
}

/*****************************************************************************
 * Function   : rtds_memory_drv_close
 * Description: This function close RTDS MEMORY driver.
 * Parameters : inode   -   inode information
 *		fp	-   file descriptor
 * Returns    : SMAP_OK	-   Success
 *
 *****************************************************************************/
int rtds_memory_drv_close(
		struct inode	*inode,
		struct file	*fp
)
{
	rtds_memory_drv_data	*rtds_memory_data_p = fp->private_data;

	MSG_MED("[RTDSK]IN |[%s]\n", __func__);
	MSG_MED("[RTDSK]   |inode_p[0x%08X]\n", (u32)inode);
	MSG_MED("[RTDSK]   |fp[0x%08X]\n", (u32)fp);

	/* Release memory for rtds_memory_drv_data */
	if (rtds_memory_data_p) {
		kfree(rtds_memory_data_p->mpro_control);
		kfree(rtds_memory_data_p);
		fp->private_data = NULL;
	}

	MSG_MED("[RTDSK]OUT|[%s]ret = SMAP_OK\n", __func__);

	return SMAP_OK;
}

/*****************************************************************************
 * Function   : rtds_memory_drv_ioctl
 * Description: This function controls I/O for RTDS MEMORY driver.
 * Parameters : inode   -   inode information
 *		fp	-   file descriptor
 * Returns    : SMAP_OK		-   Success
 *		SMAP_NG		-   Fatal error
 *		SMAP_MEMORY	-   No memory
 *
 *****************************************************************************/
long rtds_memory_drv_ioctl(
		struct file	*fp,
		unsigned int	cmd,
		unsigned long	arg
)
{
	int			ret = SMAP_OK;
	rtds_memory_drv_data	*rtds_memory_data_p = fp->private_data;
	rtds_memory_ioctl_data	ioctl_data;
	struct task_struct	*task = current;

	MSG_MED("[RTDSK]IN |[%s]\n", __func__);
	MSG_MED("[RTDSK]   |fp[0x%08X]\n", (u32)fp);
	MSG_MED("[RTDSK]   |command[0x%08X]\n", (u32)cmd);
	MSG_MED("[RTDSK]   |argument[0x%08X]\n", (u32)arg);

	task->flags |= PF_FREEZER_SKIP;
	memset(&ioctl_data, 0, sizeof(ioctl_data));

	if (0 != arg) {
		/* Check validation of user space pointer */
		if (!access_ok(VERIFY_READ, (void __user *)arg,
				_IOC_SIZE(cmd))) {
			ret = SMAP_NG;
			MSG_ERROR("[RTDSK]ERR| parameter error\n");
			MSG_MED("[RTDSK]OUT|[%s]ret = %d\n", __func__, ret);
			return ret;
		}

		/* Copy data from user space to kernel space */
		if (copy_from_user(&ioctl_data, (int __user *)arg,
			sizeof(ioctl_data))) {
			ret = SMAP_NG;
			MSG_ERROR("[RTDSK]ERR| copy_from_user error\n");
			MSG_MED("[RTDSK]OUT|[%s]ret = %d\n", __func__, ret);
			return ret;
		}
		MSG_LOW("[RTDSK]   |ioctl_data->size[%d]\n",
			ioctl_data.data_size);
	}

	switch (cmd) {
	case IOCTL_MEM_CMD_WR_SET_MPRO:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_SET_MPRO\n");

		if (RTDS_MEM_MPRO_ACTIVE == g_rtds_memory_mpro_startup_flag) {
			MSG_MED("[RTDSK]   |MPRO is already activated.\n");
			break;
		}

		/* Start Mpro */
		ret = rtds_memory_drv_init_mpro(fp);
		if (SMAP_OK == ret) {
			g_rtds_memory_mpro_startup_flag = RTDS_MEM_MPRO_ACTIVE;
			MSG_MED("[RTDSK]   |Start MPRO.\n");
			while (1) {
				rtds_memory_check_shared_apmem(fp,
					&(rtds_memory_data_p->mem_map));
			}
		} else {
			MSG_ERROR("[RTDSK]ERR|" \
				  " rtds_memory_drv_init_mpro failed ret[%d]\n",
				  ret);
			panic("[RTDSK]ERR|" \
			      "[%s][%d]rtds_memory_drv_init_mpro failed[%d]\n",
			      __func__, __LINE__, ret);
		}

		break;

	case IOCTL_MEM_CMD_WR_INIT:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_INIT\n");

		if (ioctl_data.data_size > UINT_MAX) {
			ret = -EINVAL;
			return ret;
		}
		/* Initialise RTDS memory handle parameter */
		ret = rtds_memory_ioctl_init_data(fp,
			(void __user *)ioctl_data.data,
			(size_t)ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_EXIT:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_EXIT\n");

		/* Unmap shared RT memory */
		if (0 != rtds_memory_data_p->var_app_addr) {
			(void)rtds_memory_close_shared_rtmem(
					rtds_memory_data_p->var_app_addr,
					rtds_memory_data_p->var_area_len);
		}
		break;

	case IOCTL_MEM_CMD_WR_OPEN_MEMORY:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_OPEN_MEMORY\n");

		/* Create App shared memory */
		ret = rtds_memory_ioctl_open_apmem(fp,
						(void __user *)ioctl_data.data,
						ioctl_data.data_size,
						&(rtds_memory_data_p->mem_map)
						);
		break;

	case IOCTL_MEM_CMD_WR_CLOSE_MEMORY:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_CLOSE_MEMORY\n");

		/* Close App shared memory */
		ret = rtds_memory_ioctl_close_apmem(
			(void __user *)ioctl_data.data, ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_SHARE_MEMORY:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_SHARE_MEMORY\n");

		/* Share App shared memory */
		ret = rtds_memory_ioctl_share_apmem(fp,
						(void __user *)ioctl_data.data,
						ioctl_data.data_size,
						&(rtds_memory_data_p->mem_map)
						);
		break;

	case IOCTL_MEM_CMD_WR_FLUSH_CACHE:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_FLUSH_CACHE\n");

		ret = rtds_memory_ioctl_cache_flush(
						(void __user *)ioctl_data.data,
						ioctl_data.data_size
						);
		break;

	case IOCTL_MEM_CMD_WR_CLEAR_CACHE:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_CLEAR_CACHE\n");

		ret = rtds_memory_ioctl_cache_clear(
						(void __user *)ioctl_data.data,
						ioctl_data.data_size
						);
		break;

	case IOCTL_MEM_CMD_WR_RTMAP:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_RTMAP\n");

		ret = rtds_memory_ioctl_map_mpro((void __user *)ioctl_data.data,
						ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_RTUNMAP:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_RTUNMAP\n");

		ret = rtds_memory_ioctl_unmap_mpro(
						(void __user *)ioctl_data.data,
						ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_RTMAP_PNC:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_RTMAP_PNC\n");

		ret = rtds_memory_ioctl_map_pnc_mpro(
						(void __user *)ioctl_data.data,
						ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_RTUNMAP_PNC:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_RTUNMAP_PNC\n");

		ret = rtds_memory_ioctl_unmap_pnc_mpro(
						(void __user *)ioctl_data.data,
						ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_RTMAP_PNC_NMA:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_RTMAP_PNC_NMA\n");

		ret = rtds_memory_ioctl_map_pnc_nma_mpro(
						(void __user *)ioctl_data.data,
						ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_GET_MEMSIZE:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_GET_MEMSIZE\n");

		/* Get App shared memory size */
		ret = rtds_memory_ioctl_get_memsize(
						(void __user *)ioctl_data.data,
						ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_GET_PAGESINFO:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_GET_PAGESINFO\n");

		/* Get memory of page descriptor */
		ret = rtds_memory_ioctl_get_pagesinfo(
						(void __user *)ioctl_data.data,
						ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_PHYMEM:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_PHYMEM\n");
		ret = rtds_memory_ioctl_change_phymem_address(
						(void __user *)ioctl_data.data,
						ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_CHANGE_PHY_PMB:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_CHANGE_PHY_PMB\n");
		{
			system_mem_phy_change_rtpmbaddr phy_change_rtaddr;
			if (copy_from_user(&phy_change_rtaddr,
				(int __user *)ioctl_data.data,
				(unsigned long)sizeof(phy_change_rtaddr))) {
				MSG_ERROR("[RTDSK]ERR| copy_from_user (L:%d)\n",
					 __LINE__);
				ret = SMAP_NG;
				break;
			}
			ret = system_memory_phy_change_rtpmbaddr(
							&phy_change_rtaddr);

			if (ioctl_data.data_size > UINT_MAX) {
				ret = -EINVAL;
				return ret;
			}

			if (copy_to_user((int __user *)ioctl_data.data,
				&phy_change_rtaddr,
				(unsigned long)ioctl_data.data_size)) {
				MSG_ERROR("[RTDSK]ERR| copy_to_user (L:%d)\n",
					__LINE__);
			}
		}
		break;

	case IOCTL_MEM_CMD_WR_CHANGE_PMB_PHY:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_CHANGE_PMB_PHY\n");
		{
			system_mem_rtpmb_change_phyaddr rt_change_phyaddr;
			if (copy_from_user(&rt_change_phyaddr,
				(int __user *)ioctl_data.data,
				(unsigned long)sizeof(rt_change_phyaddr))) {
				MSG_ERROR("[RTDSK]ERR| copy_from_user (L:%d)\n",
					__LINE__);
				ret = SMAP_NG;
				break;
			}
			ret = system_memory_rtpmb_change_phyaddr(
							&rt_change_phyaddr);

			if (ioctl_data.data_size > UINT_MAX) {
				ret = -EINVAL;
				return ret;
			}

			if (copy_to_user((int __user *)ioctl_data.data,
				&rt_change_phyaddr,
				(unsigned long)ioctl_data.data_size)) {
				MSG_ERROR("[RTDSK]ERR| copy_to_user (L:%d)\n",
					__LINE__);
			}
		}
		break;
#ifdef RTDS_SUPPORT_DRM
	case IOCTL_MEM_CMD_WR_ALLOC_CMA:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_ALLOC_CMA\n");

		ret = rtds_memory_ioctl_alloc_cma(fp,
					(void __user *)ioctl_data.data,
					ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_FREE_CMA:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_FREE_CMA\n");

		ret = rtds_memory_ioctl_free_cma(
					(void __user *)ioctl_data.data,
					ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_RTMAP_MA:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_RTMAP_MA\n");

		ret = rtds_memory_ioctl_map_mpro_ma(
					(void __user *)ioctl_data.data,
					ioctl_data.data_size);
		break;

	case IOCTL_MEM_CMD_WR_RTUNMAP_MA:
		MSG_MED("[RTDSK]   |IOCTL_MEM_CMD_WR_RTUNMAP_MA\n");

		ret = rtds_memory_ioctl_unmap_mpro_ma(
					(void __user *)ioctl_data.data,
					ioctl_data.data_size);
		break;
#endif
	default:
		MSG_ERROR("[RTDSK]ERR| No command\n");

		ret = SMAP_NG;
		break;
	}

	if (SMAP_OK != ret)
		MSG_ERROR("[RTDSK]ERR| ioctl function error\n");

	MSG_MED("[RTDSK]OUT|[%s]ret = %d\n", __func__, ret);
	return ret;
}

/*****************************************************************************
 * Function   : rtds_memory_drv_mapping
 * Description: This function does mapping.
 * Parameters : inode	-   inode information
 *		fp	-   file descriptor
 * Returns    : 0	-   Success(always)
 *
 *****************************************************************************/
int rtds_memory_drv_mapping(
		struct file		*fp,
		struct vm_area_struct	*vm_area
)
{
	int			ret;
	rtds_memory_drv_data	*rtds_memory_data_p = fp->private_data;
	MSG_MED("[RTDSK]IN |[%s]\n", __func__);
	MSG_MED("[RTDSK]   |fp[0x%08X]\n", (unsigned int)fp);
	MSG_MED("[RTDSK]   |vm_pgoff[0x%08X]\n",
		(unsigned int)vm_area->vm_pgoff);

	/* Mapping memory */
	ret = rtds_memory_map_shared_memory(&(rtds_memory_data_p->mem_map),
						vm_area);
	/*
	 * Always return 0, because this function is called by OS.
	 */
	MSG_MED("[RTDSK]OUT|[%s]ret = SMAP_OK\n", __func__);
	return 0;
}

/*****************************************************************************
 * Function   : rtds_memory_thread_apmem_rttrig
 * Description: This function is asynchronous receive thread from RT domain.
 * Parameters : none
 * Returns    : SMAP_OK -   Success
 *****************************************************************************/
int rtds_memory_thread_apmem_rttrig(
		void	*vp
)
{
	int				ret = SMAP_OK;
	rtds_memory_rcv_event_queue	*recv_queue;

	MSG_MED("[RTDSK]IN |[%s]\n", __func__);

	while (1) {
		/* Release semaphore */
		up(&g_rtds_memory_apmem_rttrig_sem);

		/* Wait for completion from RT Domain */
		wait_for_completion(&g_rtds_memory_completion);

		/* Get receive event */
		recv_queue = rtds_memory_get_recv_queue();
		if (NULL == recv_queue) {
			MSG_ERROR("[RTDSK]ERR| receive queue is NULL\n");
			panic("[RTDSK]ERR|[%s][%d]receive queue is NULL\n",
				__func__, __LINE__);
		} else {
			MSG_LOW("[RTDSK]   |event     [0x%08X]\n",
				recv_queue->event);
			MSG_LOW("[RTDSK]   |mem_size  [0x%08X]\n",
				recv_queue->mem_size);
			MSG_LOW("[RTDSK]   |rt_cache  [0x%08X]\n",
				recv_queue->rt_cache);
			MSG_LOW("[RTDSK]   |rt_trigger[0x%08X]\n",
				recv_queue->rt_trigger);
			MSG_LOW("[RTDSK]   |apmem_id  [0x%08X]\n",
				recv_queue->apmem_id);
			MSG_LOW("[RTDSK]   |leak_data [0x%08X]\n",
				(u32)recv_queue->leak_data);
			MSG_LOW("[RTDSK]   |leak_size [0x%08X]\n",
				recv_queue->leak_size);
#ifdef RTDS_SUPPORT_RT_TRIG_DRM
			MSG_LOW("[RTDSK]   |phys_addr [0x%08X]\n",
				recv_queue->phys_addr);
			MSG_LOW("[RTDSK]   |mem_attr  [0x%08X]\n",
				recv_queue->mem_attr);
#endif

			switch (recv_queue->event) {
			case RTDS_MEM_DRV_EVENT_APMEM_OPEN:
				MSG_MED("[RTDSK]   |" \
					"RTDS_MEM_DRV_EVENT_APMEM_OPEN\n");
				/* Open App shared memory */
				ret = rtds_memory_open_rttrig_shared_apmem(
					recv_queue->mem_size,
					recv_queue->rt_cache,
					recv_queue->rt_trigger);
				if (SMAP_OK != ret) {
					MSG_ERROR("[RTDSK]ERR| rtds_memory_" \
						"open_rttrig_shared_apmem " \
						"failed ret[%d]\n", ret);
					panic("[RTDSK]ERR|[%s][%d]rtds_memory" \
						"_open_rttrig_shared_apmem" \
						" failed[%d]\n",
						__func__, __LINE__, ret);
				}
				break;

			case RTDS_MEM_DRV_EVENT_APMEM_CLOSE:
				MSG_MED("[RTDSK]   |" \
					"RTDS_MEM_DRV_EVENT_APMEM_CLOSE\n");
				/* Close App shared memory */
				ret = rtds_memory_close_rttrig_shared_apmem(
					recv_queue->apmem_id);
				if (SMAP_OK != ret) {
					MSG_ERROR("[RTDSK]ERR| rtds_memory_" \
						"close_rttrig_shared_apmem " \
						"failed ret[%d]\n", ret);
					panic("[RTDSK]ERR|[%s][%d]rtds_memory" \
						"_close_rttrig_shared_apmem" \
						" failed[%d]\n",
						 __func__, __LINE__, ret);
				}
				break;

			case RTDS_MEM_DRV_EVENT_APMEM_DELETE:
				MSG_MED("[RTDSK]   |" \
					"RTDS_MEM_DRV_EVENT_APMEM_DELETE\n");

				break;

			case RTDS_MEM_DRV_EVENT_FATAL:
				MSG_MED("[RTDSK]   |" \
					"RTDS_MEM_DRV_EVENT_FATAL\n");
				rtds_memory_dump_mpro();
				break;

			case RTDS_MEM_DRV_EVENT_DELETE_LEAK_MEM:
				MSG_MED("[RTDSK]   |" \
					"RTDS_MEM_DRV_EVENT_DELETE_LEAK_MEM\n");
				/* Delete leak memory */
				(void)rtds_memory_delete_rttrig_leak_memory(
					recv_queue->leak_data,
					recv_queue->leak_size);
				break;


#ifdef RTDS_SUPPORT_RT_TRIG_DRM
			case RTDS_MEM_DRV_EVENT_CMA_OPEN:
				MSG_MED("[RTDSK]   |" \
					"RTDS_MEM_DRV_EVENT_CMA_OPEN\n");
				ret = rtds_memory_open_rttrig_cma(
					recv_queue->mem_size,
					recv_queue->map_id,
					recv_queue->mem_attr,
					recv_queue->rt_trigger);
				if (SMAP_OK != ret) {
					MSG_ERROR("[RTDSK]ERR| rtds_memory" \
						"_open_rttrig_cma[%d]\n", ret);
					panic("[RTDSK]ERR|[%s][%d]rtds_memory" \
						"_open_rttrig_cma failed[%d]\n",
						__func__, __LINE__, ret);
				}

				break;

			case RTDS_MEM_DRV_EVENT_CMA_CLOSE:
				MSG_MED("[RTDSK]   |" \
					"RTDS_MEM_DRV_EVENT_CMA_CLOSE\n");
				ret = rtds_memory_close_rttrig_cma(
					recv_queue->apmem_id);
				if (SMAP_OK != ret) {
					MSG_ERROR("[RTDSK]ERR| rtds_memory" \
						"_close_rttrig_cma[%d]\n", ret);
					panic("[RTDSK]ERR|[%s][%d]rtds_memory" \
						"_close_rttrig_cma failed[%d]\n"
						, __func__, __LINE__, ret);
				}
				break;

			case RTDS_MEM_DRV_EVENT_APMEM_MAP:
				MSG_MED("[RTDSK]   |" \
					"RTDS_MEM_DRV_EVENT_APMEM_MAP\n");
				ret = rtds_memory_map_rttrig_shared_apmem(
					recv_queue->phys_addr,
					recv_queue->mem_size,
					recv_queue->rt_cache,
					recv_queue->rt_trigger);
				if (SMAP_OK != ret) {
					MSG_ERROR("[RTDSK]ERR| rtds_memory" \
						"_map_rttrig_shared_apmem[%d]\n"
						, ret);
					panic("[RTDSK]ERR|[%s][%d]rtds_memory" \
						"_map_rttrig_shared_apmem" \
						" failed[%d]\n",
						__func__, __LINE__, ret);
				}
				break;

			case RTDS_MEM_DRV_EVENT_APMEM_UNMAP:
				MSG_MED("[RTDSK]   |" \
					"RTDS_MEM_DRV_EVENT_APMEM_UNMAP\n");
				ret = rtds_memory_unmap_rttrig_shared_apmem(
					recv_queue->apmem_id);
				if (SMAP_OK != ret) {
					MSG_ERROR("[RTDSK]ERR|" \
						" rtds_memory_unmap_rttrig" \
						"_shared_apmem failed ret[%d]\n"
						, ret);
					panic("[RTDSK]ERR|[%s][%d]rtds_memory" \
						"_unmap_rttrig_shared_apmem" \
						" failed[%d]\n",
						__func__, __LINE__, ret);
				}
				break;
#endif
			default:
				MSG_ERROR("[RTDSK]ERR| No EVENT[0x%08X]\n",
					recv_queue->event);
				panic("[RTDSK]ERR|" \
					" thread_apmem illegal event\n");
				break;
			}
			/* Delete receive event */
			rtds_memory_delete_recv_queue(recv_queue);
		}
	}

	MSG_MED("[RTDSK]OUT|[%s]ret = SMAP_OK\n", __func__);
	return ret;
}

static int rtds_mem_procfile_read(struct seq_file *file, void *v)
{
	rtds_memory_app_memory_table *m_table = NULL;
	struct list_head l_list;
	struct rtds_mem_dump_info *dump_list;
	struct rtds_mem_dump_info *next;
	bool find;
	unsigned int sum = 0;
	char comm[256];
	char *cmdline = comm;
	int n;

	INIT_LIST_HEAD(&l_list);
	RTDS_MEM_DOWN_TIMEOUT(&g_rtds_memory_shared_mem);

	list_for_each_entry(m_table,
				&g_rtds_memory_list_shared_mem, list_head) {
		find = false;
		list_for_each_entry(dump_list, &l_list, list) {
			if (m_table->task_info->tgid == dump_list->tgid) {
				dump_list->size += m_table->memory_size;
				find = true;
				break;
			}
		}
#if 1 /* VSS */
		if (!find) {
#else /* RSS */
		if ((!find) &&
		    ((m_table->event == RTDS_MEM_RT_CREATE_EVENT) ||
		    (m_table->event == RTDS_MEM_MAP_PNC_EVENT) ||
		    (m_table->event == RTDS_MEM_MAP_PNC_NMA_EVENT))) {
#endif
			dump_list = kmalloc(sizeof(struct rtds_mem_dump_info),
					    GFP_KERNEL);
			if (NULL == dump_list)
				goto error;
			dump_list->size = m_table->memory_size;
			dump_list->tgid = m_table->task_info->tgid;
			dump_list->task_info = m_table->task_info;
			if ((m_table->rt_nc_addr) &&
			    (m_table->event != RTDS_MEM_MAP_EVENT))
				dump_list->size += m_table->memory_size;
			list_add_tail(&(dump_list->list), &l_list);
		}
	}

	up(&g_rtds_memory_shared_mem);

	/* show dump info */
	n = seq_printf(file, "%-4s %7s %s\n", "TGID", "SIZE", "NAME");
	if (n < 0)
		goto error;
	list_for_each_entry_safe(dump_list, next, &l_list, list) {

		if (rtds_get_cmdline(dump_list->tgid, comm, sizeof(comm)))
			cmdline = comm;
		else
			cmdline = dump_list->task_info->comm;

		n = seq_printf(file, "%4d %6dK %s\n", (u32)dump_list->tgid,
			       (dump_list->size / SZ_1K), cmdline);
		if (n < 0)
			goto error;
		sum += dump_list->size;
		list_del(&(dump_list->list));
		kfree(dump_list);
	}
	n = seq_printf(file, "%-4s-%-8s-%s\n", "----", "--------", "----");
	if (n < 0)
		goto error;

	n = seq_printf(file, "%-4s %6dK %s\n", "", (sum / SZ_1K), "TOTAL");
	if (n < 0)
		goto error;

	return 0;
error:
	list_for_each_entry_safe(dump_list, next, &l_list, list) {
		list_del(&(dump_list->list));
		kfree(dump_list);
	}
	return -1;
}

static int rtds_mem_procfile_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtds_mem_procfile_read, NULL);
}

static const struct file_operations proc_mem_info_fops = {
	.open = rtds_mem_procfile_open,
	.read = seq_read,
};

static int rtds_mem_procinfo_mpro_open(
	struct inode *inode, struct file *file);
static void *mproinfo_s_start(
	struct seq_file *m, loff_t *pos);
static void *mproinfo_s_next(struct seq_file *m, void *p, loff_t *pos);
static void mproinfo_s_stop(struct seq_file *m, void *p);
static int mproinfo_s_show(struct seq_file *m, void *p);


static const struct file_operations g_procinfo_mpro_op = {
	.open		= rtds_mem_procinfo_mpro_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static const struct seq_operations g_rtds_mem_mpro_seq_op = {
	.start	= mproinfo_s_start,
	.next	= mproinfo_s_next,
	.stop	= mproinfo_s_stop,
	.show	= mproinfo_s_show,
};

static int rtds_mem_procinfo_mpro_open(
	struct inode *inode, struct file *file)
{
	return seq_open(file, &g_rtds_mem_mpro_seq_op);
}

static void *mproinfo_s_start(
	struct seq_file *m, loff_t *pos)
{
	rtds_memory_app_memory_table *m_table;
	struct list_head *list;
	int i;

	MSG_MED("[%s]%d\n", __func__, (u32)*pos);

	if (*pos == 0)
		seq_printf(m, "%-4s %-4s %3s %-10s %-10s %-10s %7s %s\n",
			 "PID", "TGID", "ID", "WB_ADDR", "NC_ADDR",
			 "PHY_ADDR", "SIZE", "NAME");

	RTDS_MEM_DOWN_TIMEOUT(&g_rtds_memory_shared_mem);

	if (list_empty(&g_rtds_memory_list_shared_mem))
		return NULL;

	list = g_rtds_memory_list_shared_mem.next;

	m_table = list_entry(list,
			     rtds_memory_app_memory_table,
			     list_head);
	for (i = 0; i < *pos; i++) {
		m_table = list_entry(list,
				     rtds_memory_app_memory_table,
				     list_head);
		if ((u32)m_table == (u32)&g_rtds_memory_list_shared_mem)
			return NULL;
		list = m_table->list_head.next;
	}

	return m_table;
}

static void *mproinfo_s_next(struct seq_file *m, void *p, loff_t *pos)
{
	rtds_memory_app_memory_table *m_table;
	struct list_head *list;

	++*pos;
	m_table = (rtds_memory_app_memory_table *)p;
	list = m_table->list_head.next;

	if ((u32)list == (u32)&g_rtds_memory_list_shared_mem) {
		MSG_MED("[%s]END!!\n", __func__);
		return NULL;
	}
	m_table = list_entry(list,
			     rtds_memory_app_memory_table,
			     list_head);

	return m_table;
}

static void mproinfo_s_stop(struct seq_file *m, void *p)
{
	MSG_MED("[%s]\n", __func__);
	up(&g_rtds_memory_shared_mem);
}

static int mproinfo_s_show(struct seq_file *m, void *p)
{
	rtds_memory_app_memory_table *m_table;
	char comm[256];
	char *cmdline = comm;
	unsigned int phys_addr;
	unsigned int nc_addr;
	unsigned int nc_mem_size = 0;

	m_table = (rtds_memory_app_memory_table *)p;
	if (rtds_get_cmdline(m_table->task_info->pid, comm, sizeof(comm)))
		cmdline = comm;
	else
		cmdline = m_table->task_info->comm;

	if (m_table->event == RTDS_MEM_MAP_EVENT) {
		nc_addr = 0;
		phys_addr = m_table->rt_nc_addr;
	} else {
		nc_addr = m_table->rt_nc_addr;
		phys_addr = m_table->phys_addr;
	}

	if ((m_table->rt_nc_addr) &&
	    (m_table->event != RTDS_MEM_MAP_EVENT))
		nc_mem_size = m_table->memory_size;

	seq_printf(m, "%4d %4d %3d 0x%08x 0x%08x 0x%08x %6dK %s\n",
			(u32)m_table->task_info->pid,
			(u32)m_table->task_info->tgid,
			m_table->apmem_id,
			(u32)m_table->rt_wb_addr,
			(u32)nc_addr,
			(u32)phys_addr,
			(u32)((m_table->memory_size + nc_mem_size) / SZ_1K),
			cmdline);
	return 0;
}

static int rtds_get_cmdline(
	pid_t pid, char *buf, int len
)
{
	char cmdline_name[255];
	struct file *fp = NULL;
	unsigned int size;

	if (len <= 0)
		goto error;

	if (sprintf(cmdline_name, "/proc/%d/cmdline", pid) < 0)
		goto error;

	fp = filp_open(cmdline_name, O_RDONLY, 0);
	if (IS_ERR(fp))
		goto error;

	size = kernel_read(fp, 0, buf, len);
	filp_close(fp, NULL);
	return size;

error:
	size = sprintf(buf, "unknown");
	if (fp && !IS_ERR(fp))
		filp_close(fp, NULL);
	return size;
}


/*****************************************************************************
 * Function   : rtds_memory_init_module
 * Description: This function initialise RTDS MEMORY driver.
 * Parameters : none
 * Returns    : SMAP_OK -   Success
 *		SMAP_NG -   Fatal error
 *****************************************************************************/
int rtds_memory_init_module(
		void
)
{
	int				ret;
	iccom_drv_init_param		iccom_init;
	iccom_drv_cleanup_param		iccom_cleanup;
	get_section_header_param	section;
	system_rt_section_header	section_header;
	iccom_drv_send_cmd_param	send_cmd;

	MSG_HIGH("[RTDSK]IN |[%s]\n", __func__);

	/* Register device driver */
	g_rtds_memory_device.name	= RTDS_MEMORY_DRIVER_NAME;
	g_rtds_memory_device.fops	= &g_rtds_memory_fops;
	g_rtds_memory_device.minor	= MISC_DYNAMIC_MINOR;
	ret = misc_register(&g_rtds_memory_device);
	if (0 != ret) {
		MSG_ERROR("[RTDSK]ERR| misc_register failed ret[%d]\n", ret);
		MSG_HIGH("[RTDSK]OUT|[%s]ret = SMAP_NG\n", __func__);
		return SMAP_NG;
	}

	memset(&iccom_init, 0, sizeof(iccom_init));
	/* Set callback for asynchronous response */
	iccom_init.comp_notice = rtds_memory_rcv_comp_notice;
	g_rtds_memory_mpro_startup_flag = RTDS_MEM_MPRO_INACTIVE;

	/* Create ICCOM driver handle */
	g_rtds_memory_iccom_handle = iccom_drv_init(&iccom_init);
	if (NULL == g_rtds_memory_iccom_handle) {
		MSG_ERROR("[RTDSK]ERR| iccom_drv_init failed\n");
		ret = misc_deregister(&g_rtds_memory_device);
		if (0 != ret)
			MSG_ERROR("[RTDSK]ERR|" \
				  " misc_deregister failed ret[%d]\n", ret);

		MSG_HIGH("[RTDSK]OUT|[%s]ret = SMAP_NG\n", __func__);
		return SMAP_NG;
	}

	/* Initialise section info */
	memset(&g_rtds_memory_section_info, 0,
		sizeof(g_rtds_memory_section_info));

	/* Initialise LIST head */
	memset(&g_rtds_memory_list_rcv_event, 0,
		sizeof(g_rtds_memory_list_rcv_event));
	memset(&g_rtds_memory_list_create_mem, 0,
		sizeof(g_rtds_memory_list_create_mem));
	memset(&g_rtds_memory_list_mpro, 0,
		sizeof(g_rtds_memory_list_mpro));
	memset(&g_rtds_memory_list_shared_mem, 0,
		sizeof(g_rtds_memory_list_shared_mem));
	memset(&g_rtds_memory_list_leak_mpro, 0,
		sizeof(g_rtds_memory_list_leak_mpro));
	memset(&g_rtds_memory_list_map_rtmem, 0,
		sizeof(g_rtds_memory_list_map_rtmem));
	memset(&g_rtds_memory_list_reg_phymem, 0,
		sizeof(g_rtds_memory_list_reg_phymem));

	INIT_LIST_HEAD(&g_rtds_memory_list_rcv_event);
	INIT_LIST_HEAD(&g_rtds_memory_list_create_mem);
	INIT_LIST_HEAD(&g_rtds_memory_list_mpro);
	INIT_LIST_HEAD(&g_rtds_memory_list_shared_mem);
	INIT_LIST_HEAD(&g_rtds_memory_list_leak_mpro);
	INIT_LIST_HEAD(&g_rtds_memory_list_map_rtmem);
	INIT_LIST_HEAD(&g_rtds_memory_list_reg_phymem);
#ifdef RTDS_SUPPORT_DRM
	INIT_LIST_HEAD(&g_rtds_memory_list_cma);
#endif

	/* Initialise spin_lock */
	spin_lock_init(&g_rtds_memory_lock_recv_queue);
	spin_lock_init(&g_rtds_memory_lock_cache_all);
	spin_lock_init(&g_rtds_memory_lock_create_mem);
	spin_lock_init(&g_rtds_memory_lock_mpro);
	spin_lock_init(&g_rtds_memory_lock_map_rtmem);
#ifdef RTDS_SUPPORT_DRM
	spin_lock_init(&g_rtds_memory_lock_cma);
#endif

	/* Initialise completion */
	init_completion(&g_rtds_memory_completion);

	/* Initialise semapore */
	init_MUTEX(&g_rtds_memory_apmem_rttrig_sem);
	init_MUTEX_LOCKED(&g_rtds_memory_mpro_sem);
	init_MUTEX(&g_rtds_memory_shared_mem);
	init_MUTEX(&g_rtds_memory_phy_mem);
	init_MUTEX(&g_rtds_memory_send_sem);

	/* Convert timeout value */
	g_rtds_memory_sem_jiffies = msecs_to_jiffies(RTDS_MEM_WAIT_TIMEOUT);

	/* Get section info */
	section.section_header = &section_header;
	ret = sys_get_section_header(&section);
	if (SMAP_OK != ret) {
		MSG_ERROR("[RTDSK]ERR| sys_get_section_header failed[%d]\n",
				ret);
		goto out;
	}

	g_rtds_memory_section_info.var_address	= section_header.memmpl_address;
	g_rtds_memory_section_info.var_length	= section_header.memmpl_size;
	g_rtds_memory_section_info.sh_pmb_offset = section_header.sh_pmb_offset;
	g_rtds_memory_section_info.sh_pmb_nc_offset =
						section_header.sh_pmb_nc_offset;
	g_rtds_memory_section_info.mfi_pmb_offset =
						section_header.mfi_pmb_offset;

	/* I/O remap */
	g_rtds_memory_section_info.kernel_var_addr = ioremap_nocache(
					g_rtds_memory_section_info.var_address,
					g_rtds_memory_section_info.var_length);
	if (NULL == g_rtds_memory_section_info.kernel_var_addr) {
		MSG_ERROR("[RTDSK]ERR| ioremap_nocache failed\n");
		goto out;
	}

	MSG_MED("[RTDSK]   |addr(section)[0x%08X]\n",
		(u32)g_rtds_memory_section_info.var_address);
	MSG_MED("[RTDSK]   |length(section)[0x%08X]\n",
		(u32)g_rtds_memory_section_info.var_length);
	MSG_MED("[RTDSK]   |addr(kernel)[0x%08X]\n",
		(u32)g_rtds_memory_section_info.kernel_var_addr);

	/*
	 *   Register asynchronous event to RT domain
	 */

	/* send EVENT_MEMORY_OPENAPPMEMORY */
	send_cmd.handle		= g_rtds_memory_iccom_handle;
	send_cmd.task_id	= TASK_MEMORY;
	send_cmd.function_id	= EVENT_MEMORY_OPENAPPMEMORY;
	send_cmd.send_mode	= ICCOM_DRV_ASYNC;
	send_cmd.send_size	= 0;
	send_cmd.send_data	= NULL;
	send_cmd.recv_size	= 0;
	send_cmd.recv_data	= NULL;

	ret = iccom_drv_send_command(&send_cmd);
	if (SMAP_OK != ret) {
		MSG_ERROR("[RTDSK]ERR|send error" \
			  "[EVENT_MEMORY_OPENAPPMEMORY]\n");
		goto out;
	}

	MSG_LOW("[RTDSK]   |Send [EVENT_MEMORY_OPENAPPMEMORY]\n");


	/* send EVENT_MEMORY_CLOSEAPPMEMORY
	 * It does not change send_command parameter as follows.
	 *  send_cmd.handle      = g_rtds_memory_iccom_handle;
	 *  send_cmd.task_id     = TASK_MEMORY;
	 *  send_cmd.send_mode   = ICCOM_DRV_ASYNC;
	 *  send_cmd.send_size   = 0;
	 *  send_cmd.send_data   = NULL;
	 *  send_cmd.recv_size   = 0;
	 *  send_cmd.recv_data   = NULL;
	 */
	send_cmd.function_id = EVENT_MEMORY_CLOSEAPPMEMORY;
	ret = iccom_drv_send_command(&send_cmd);
	if (SMAP_OK != ret) {
		MSG_ERROR("[RTDSK]ERR|send error" \
			  "[EVENT_MEMORY_CLOSEAPPMEMORY]\n");
		goto out;
	}
	MSG_LOW("[RTDSK]   |Send [EVENT_MEMORY_CLOSEAPPMEMORY]\n");

#ifdef RTDS_SUPPORT_RT_TRIG_DRM
	/* send EVENT_MEMORY_OPERATECTGMEMORY
	 * It does not change send_command parameter as follows.
	 *  send_cmd.handle      = g_rtds_memory_iccom_handle;
	 *  send_cmd.task_id     = TASK_MEMORY;
	 *  send_cmd.send_mode   = ICCOM_DRV_ASYNC;
	 *  send_cmd.send_size   = 0;
	 *  send_cmd.send_data   = NULL;
	 *  send_cmd.recv_size   = 0;
	 *  send_cmd.recv_data   = NULL;
	 */
	send_cmd.function_id = EVENT_MEMORY_OPERATECTGMEMORY;
	ret = iccom_drv_send_command(&send_cmd);
	if (SMAP_OK != ret) {
		MSG_ERROR("[RTDSK]ERR|send error" \
			  "[EVENT_MEMORY_OPERATECTGMEMORY]\n");
		goto out;
	}
	MSG_LOW("[RTDSK]   |Send [EVENT_MEMORY_OPERATECTGMEMORY]\n");
#endif

	/* Run asynchronous thread */
	g_rtds_memory_thread_info = kthread_run(rtds_memory_thread_apmem_rttrig,
						NULL,
						"rtds_memory_th_async");
	if (NULL == g_rtds_memory_thread_info) {
		MSG_ERROR("[RTDSK]ERR|kthread_run failed[NULL]\n");
		goto out;
	}
	/* Create debugfs entries */
	g_rtds_memory_debug_dentry = debugfs_create_dir("rtds", NULL);
	g_rtds_memory_debug_shared_mem_entry = debugfs_create_file("shared_mem",
					0664, g_rtds_memory_debug_dentry, NULL,
					&rtds_memory_debug_shared_mem_fops);
	g_rtds_memory_debug_mpro_entry = debugfs_create_file("mpro", 0664,
					g_rtds_memory_debug_dentry, NULL,
					&rtds_memory_debug_mpro_fops);
	g_rtds_memory_debug_leak_mpro_entry = debugfs_create_file("mpro_leak",
					0664, g_rtds_memory_debug_dentry, NULL,
					&rtds_memory_debug_leak_mpro_fops);
	g_rtds_memory_debug_map_rtmem_entry = debugfs_create_file("rtmem_map",
					0664, g_rtds_memory_debug_dentry, NULL,
					&rtds_memory_debug_map_rtmem_fops);
	g_rtds_memory_debug_create_mem_entry = debugfs_create_file("create_mem",
					0664, g_rtds_memory_debug_dentry, NULL,
					&rtds_memory_debug_create_mem_fops);
	g_rtds_memory_debug_rcv_event_entry = debugfs_create_file("rcv_event",
					0664, g_rtds_memory_debug_dentry, NULL,
					&rtds_memory_debug_rcv_event_fops);
	g_rtds_memory_debug_reg_phymem_entry = debugfs_create_file("reg_phymem",
					0664, g_rtds_memory_debug_dentry, NULL,
					&rtds_memory_debug_reg_phymem_fops);

	g_rtds_memory_proc_entry = NULL;
	g_rtds_memory_proc_mem_info = NULL;
	g_rtds_memory_proc_mpro = NULL;
	/* 0644: S_IRUSR | S_IWUSR| S_IRGRP| S_IROTH */
	g_rtds_memory_proc_entry =
		proc_mkdir(RTDS_MEMORY_DRIVER_NAME, NULL);
	if (!g_rtds_memory_proc_entry) {
		MSG_ERROR("[RTDSK]ERR|L.%d Error creating proc entry\n",
			  __LINE__);
		goto out;
	}

	g_rtds_memory_proc_mem_info =
		proc_create("mem_info",
			    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
			    g_rtds_memory_proc_entry, &proc_mem_info_fops);
	if (!g_rtds_memory_proc_mem_info) {
		MSG_ERROR("[RTDSK]ERR|L.%d Error creating proc entry\n",
			  __LINE__);
		goto out;
	}

	g_rtds_memory_proc_mpro =
		proc_create("maps",
			    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
			    g_rtds_memory_proc_entry, &g_procinfo_mpro_op);
	if (!g_rtds_memory_proc_mpro) {
		MSG_ERROR("[RTDSK]ERR|L.%d Error creating proc entry\n",
			  __LINE__);
		goto out;
	}

	MSG_HIGH("[RTDSK]OUT|[%s]ret = SMAP_OK\n", __func__);

	return SMAP_OK;
out:
	g_rtds_memory_section_info.var_address = 0;
	g_rtds_memory_section_info.var_length  = 0;
	g_rtds_memory_section_info.kernel_var_addr = NULL;
	iccom_cleanup.handle = g_rtds_memory_iccom_handle;
	iccom_drv_cleanup(&iccom_cleanup);
	ret = misc_deregister(&g_rtds_memory_device);
	if (0 != ret)
		MSG_ERROR("[RTDSK]ERR| misc_deregister failed ret[%d]\n", ret);
	if (g_rtds_memory_proc_mem_info)
		remove_proc_entry("mem_info", g_rtds_memory_proc_entry);
	if (g_rtds_memory_proc_entry)
		remove_proc_entry(RTDS_MEMORY_DRIVER_NAME, NULL);

	MSG_HIGH("[RTDSK]OUT|[%s]ret = SMAP_NG\n", __func__);

	return SMAP_NG;
}

/*****************************************************************************
 * Function   : rtds_memory_exit_module
 * Description: This function exits RTDS MEMORY driver.
 * Parameters : none
 * Returns    : none
 *
 *****************************************************************************/
void rtds_memory_exit_module(
		void
)
{
	int			ret;
	iccom_drv_cleanup_param	iccom_cleanup;

	MSG_HIGH("[RTDSK]IN |[%s]\n", __func__);

	/* Stop asynchronous thread */
	ret = kthread_stop(g_rtds_memory_thread_info);
	if (0 != ret)
		MSG_ERROR("[RTDSK]ERR| kthread_stop failed ret[%d]\n", ret);

	/* Cleanup iccom driver handle */
	iccom_cleanup.handle = g_rtds_memory_iccom_handle;
	iccom_drv_cleanup(&iccom_cleanup);

	/* Unregister device driver */
	ret = misc_deregister(&g_rtds_memory_device);
	if (0 != ret)
		MSG_ERROR("[RTDSK]ERR| misc_deregister failed ret[%d]\n", ret);

	remove_proc_entry("mem_info", g_rtds_memory_proc_entry);
	remove_proc_entry("maps", g_rtds_memory_proc_entry);
	remove_proc_entry(RTDS_MEMORY_DRIVER_NAME, NULL);

	/* Remove debugfs entries */
	debugfs_remove(g_rtds_memory_debug_dentry);
	debugfs_remove(g_rtds_memory_debug_shared_mem_entry);
	debugfs_remove(g_rtds_memory_debug_mpro_entry);
	debugfs_remove(g_rtds_memory_debug_leak_mpro_entry);
	debugfs_remove(g_rtds_memory_debug_map_rtmem_entry);
	debugfs_remove(g_rtds_memory_debug_create_mem_entry);
	debugfs_remove(g_rtds_memory_debug_rcv_event_entry);
	debugfs_remove(g_rtds_memory_debug_reg_phymem_entry);

	MSG_HIGH("[RTDSK]OUT|[%s]\n", __func__);

	return;
}
module_init(rtds_memory_init_module);
module_exit(rtds_memory_exit_module);
