/*
 * Copyright (C) 2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

//Assign a value for each function that is registered
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <asm/arch_timer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#define PROF_DEPTH 20
#define DATA_DEPTH 1024
#define REGISTER_LOGGING_FUNC 0

enum
{
	FUNCTION_ENTER,
	FUNCTION_EXIT,
	FUNCTION_ARG
};

struct __attribute__((__packed__)) profiling_database
{
	char function_name[40];
	unsigned int function_arguments;
};
static struct profiling_database prof_db[PROF_DEPTH];
unsigned int prof_db_index = 0;

struct __attribute__((__packed__)) profiling_data
{
	u64 timestamp;
	unsigned int function_index;
	unsigned int func_enter_exit;
	unsigned int func_arg0;
};
static struct profiling_data prof_data[DATA_DEPTH];
static struct profiling_data prof_data_test[DATA_DEPTH];
unsigned int prof_data_index = 0;

struct semaphore access_sema;
#ifdef device_tree
static const struct of_device_id profiling_module[] = {
	{ .compatible = "bcm,wlan-profiling" },
	{}
};
MODULE_DEVICE_TABLE(of, profiling_module);
#endif
static struct proc_dir_entry *gProcParent;

unsigned int Log_data_profiling(unsigned int index, unsigned int arg,unsigned int arg0)
{
	struct timespec present_time;
	//int ret=-1;
	//ret=
	down(&access_sema);
	//if (ret)
	//{
		//printk("%s Sema failed\n",__func__);
		//return ret;
	//}
	read_persistent_clock(&present_time);
	printk("%s current time is %ld %ld\n", __func__,
			present_time.tv_sec, present_time.tv_nsec);
	prof_data[prof_data_index].function_index = index;
	prof_data[prof_data_index].timestamp = present_time.tv_nsec;//0;//arch_counter_get_cntpct();
	prof_data[prof_data_index].func_enter_exit = arg;
	prof_data[prof_data_index].func_arg0 = arg0;
	if(prof_data_index >= DATA_DEPTH)
	{
		prof_data_index = 0;
	}
	else
		prof_data_index++;
	up(&access_sema);
	return 0;
}
EXPORT_SYMBOL(Log_data_profiling);
unsigned int register_function_pointer(struct profiling_database  *function_details)
{
	//int ret = -1;
	printk("%s Enter with index %d \n",__func__,prof_db_index);
	//ret=
	down(&access_sema);
	//if (ret)
	//{
	//	printk("%s Sema failed\n",__func__);
	//	return ret;
	//}
	if (prof_db_index >= PROF_DEPTH)
	{
		printk("%s: Cant accommodate more requests\n", __func__);
		return -1;
	}
	strncpy(prof_db[prof_db_index].function_name,function_details->function_name,strlen(function_details->function_name));
	prof_db[prof_db_index].function_name[strlen(function_details->function_name)] = '\0';
	prof_db[prof_db_index].function_arguments = function_details->function_arguments;
	prof_db_index++;

	up(&access_sema);
	return (prof_db_index-1);
}
EXPORT_SYMBOL(register_function_pointer);

///PROC READ/WRITE
static int profiling_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	int ret = 0;/*i=0,*///offset1=0;
	int offset=0;
	int count=0;
	unsigned long data_error;
	printk("%s Enter offset %lu size asked %d pointer provided %p\n",__func__,(long unsigned int)*ppos,size,buf);

	if (*ppos >= (sizeof(struct profiling_data)* DATA_DEPTH))
		return 0;
	printk("trying to read data %lu\n",PAGE_SIZE);

	//ret=
	down(&access_sema);
	//if (ret)
	//{
	//	printk("%s Sema failed\n",__func__);
	//	return ret;
	//}

	if(((sizeof(struct profiling_data)* DATA_DEPTH) - *ppos )> PAGE_SIZE )
		count = PAGE_SIZE-16;
	else
		count = ((sizeof(struct profiling_data)* DATA_DEPTH) - *ppos );

	data_error = copy_to_user(buf, (((char*)prof_data) + *ppos),count/*PAGE_SIZE-16*/);

	offset += PAGE_SIZE-8;

	up(&access_sema);
		if(data_error)
			printk("error in copying left %lu \n",data_error);
	printk("DONE\n");
	*ppos+= count;//PAGE_SIZE-16;
	ret=count;//PAGE_SIZE-16;
	return ret;
}

static int profiling_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	printk("Nothing to write \n");
	return 0;
}

static int profiling_func_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	int ret = 0;//,i=0;
	printk("%s Enter offset %lu\n",__func__,(long unsigned int)*ppos);

	if (*ppos >0)
		return 0;
	printk("trying to read data %d\n",sizeof(struct profiling_database)* PROF_DEPTH);
	if(copy_to_user(buf, (void*) &prof_db,sizeof(struct profiling_database)* PROF_DEPTH))
		printk("error in copying\n");
	*ppos += sizeof(struct profiling_database)* PROF_DEPTH;
	ret = sizeof(struct profiling_database)* PROF_DEPTH;
	return ret;
}

static int profiling_func_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	printk("Nothing to write \n");
	return 0;
}

static const struct file_operations profiling_proc_fops = {
 .owner = THIS_MODULE,
 .write = profiling_write,
 .read  = profiling_read,
};
static const struct file_operations profiling_func_fops = {
 .owner = THIS_MODULE,
 .write = profiling_func_write,
 .read  = profiling_func_read,
};
static int profiling_probe(struct platform_device *pdev)
{
	struct proc_dir_entry *profiling_proc_file;
	struct proc_dir_entry *profiling_func_file;
	int i;
	// allocate memory for x number of pointers to profiling_database
	strncpy(prof_db[prof_db_index].function_name,"register_function_pointer",strlen("register_function_pointer"));
	prof_db[prof_db_index].function_name[strlen("register_function_pointer")] = '\0';
	prof_db[prof_db_index].function_arguments = 2;
	prof_db_index++;
	//temp

	strncpy(prof_db[prof_db_index].function_name,"dhdsdio_dpc",strlen("dhdsdio_dpc"));
	prof_db[prof_db_index].function_name[strlen("dhdsdio_dpc")] = '\0';
	prof_db[prof_db_index].function_arguments = 2;
	prof_db_index++;

	strncpy(prof_db[prof_db_index].function_name,"dhdsdio_readframes",strlen("dhdsdio_readframes"));
	prof_db[prof_db_index].function_name[strlen("dhdsdio_readframes")] = '\0';
	prof_db[prof_db_index].function_arguments = 2;
	prof_db_index++;

	strncpy(prof_db[prof_db_index].function_name,"dhdsdio_sendfromq",strlen("dhdsdio_sendfromq"));
	prof_db[prof_db_index].function_name[strlen("dhdsdio_sendfromq")] = '\0';
	prof_db[prof_db_index].function_arguments = 2;
	prof_db_index++;

	strncpy(prof_db[prof_db_index].function_name,"dhdsdio_txpkt",strlen("dhdsdio_txpkt"));
	prof_db[prof_db_index].function_name[strlen("dhdsdio_txpkt")] = '\0';
	prof_db[prof_db_index].function_arguments = 2;
	prof_db_index++;

	strncpy(prof_db[prof_db_index].function_name,"sdioh_request_packet",strlen("sdioh_request_packet"));
	prof_db[prof_db_index].function_name[strlen("sdioh_request_packet")] = '\0';
	prof_db[prof_db_index].function_arguments = 2;
	prof_db_index++;

	printk("profiling_probe -- %p\n",(void*)pdev);

	sema_init(&access_sema,1);

	//Create proc file to read/write
	/* create the /proc file */

	profiling_proc_file = proc_create("profiling", 0, gProcParent,&profiling_proc_fops);
	profiling_func_file = proc_create("func_desc", 0, gProcParent,&profiling_func_fops);

	for (i = 0;i < DATA_DEPTH;i++)
	{
			prof_data_test[i].timestamp = (long unsigned int) i;
			prof_data_test[i].function_index = i;
			prof_data_test[i].func_enter_exit=i;
			prof_data_test[i].func_arg0=i;
	}
	if (profiling_proc_file == NULL) {
		printk("Error: Could not initialize /proc/%s\n",
			"profiling");
		return -ENOMEM;
	}
	//Create proc file to read/write

	return 0;
}
static int profiling_remove(struct platform_device *pdev)
{
	printk("probe -- %p\n",(void*)pdev);
	remove_proc_entry("profiling", gProcParent);

	//delete semaphore TBD

	return 0;
}
static struct platform_driver profiling_driver = {
	.driver		= {
		.name	= "profiling",
		.owner	= THIS_MODULE,
#ifdef device_tree
		.of_match_table = of_match_ptr(profiling_module),
#endif
	},
	.probe		= profiling_probe,
	.remove		= profiling_remove,
};

static int __init profiling_init(void)
{
	gProcParent = proc_mkdir("bcm-profiling", NULL);
	if (gProcParent == NULL) {
		pr_err("%s: profiling proc failed\n", __func__);
		return -ENOMEM;
	}

	pr_debug("%s: Enter\n", __func__);
	return platform_driver_register(&profiling_driver);
}
static void __exit profiling_exit(void)
{
	remove_proc_entry("bcm-profiling", NULL);
	platform_driver_unregister(&profiling_driver);
}
module_init(profiling_init);
module_exit(profiling_exit);

MODULE_DESCRIPTION("Profiling interface");
MODULE_AUTHOR("Broadcom");
MODULE_LICENSE("GPL v2");
