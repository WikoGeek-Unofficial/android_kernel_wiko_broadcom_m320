/*
 * kernel/power/autosleep.c
 *
 * Opportunistic sleep support.
 *
 * Copyright (C) 2012 Rafael J. Wysocki <rjw@sisk.pl>
 */

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pm_wakeup.h>
//++ custom_t peter DACA-499 
#include <linux/syscalls.h>
#include <linux/atomic.h>
//-- custom_t peter
#include "power.h"

#ifdef CONFIG_PM_FORCE_SLEEP
#include <mach/pm.h>
#endif

static suspend_state_t autosleep_state;
static struct workqueue_struct *autosleep_wq;
/*
 * Note: it is only safe to mutex_lock(&autosleep_lock) if a wakeup_source
 * is active, otherwise a deadlock with try_to_suspend() is possible.
 * Alternatively mutex_lock_interruptible() can be used.  This will then fail
 * if an auto_sleep cycle tries to freeze processes.
 */
static DEFINE_MUTEX(autosleep_lock);
static struct wakeup_source *autosleep_ws;

#ifdef custom_t_DEBUG_SUSPEND
extern int get_active_wakeup_sources(void);
#endif
//++ custom_t peter DACA-499 
/* sys_sync workqueue */
static atomic_t sys_sync_count = ATOMIC_INIT(0);
static struct workqueue_struct *sys_sync_wq;

static void suspend_sys_sync(struct work_struct *work)
{
	pr_info("PM: sys_sync in progress ...\n");
	sys_sync();
	atomic_dec(&sys_sync_count);
	pr_info("sys_sync completed.\n");

}
static DECLARE_WORK(sys_sync_work, suspend_sys_sync);

void suspend_sys_sync_queue(void)
{
	int ret;

	ret = queue_work(sys_sync_wq, &sys_sync_work);
	if (ret)
		atomic_inc(&sys_sync_count);
}

static void sys_sync_handler(unsigned long);

#define SUSPEND_SYS_SYNC_TIMEOUT (HZ/4)

struct sys_sync_struct {
	struct timer_list timer;
	bool abort;
	struct completion *complete;
};

static void sys_sync_handler(unsigned long data)
{
	struct sys_sync_struct *sys_sync = (struct sys_sync_struct *)data;

	if (atomic_read(&sys_sync_count) == 0) {
		complete(sys_sync->complete);
	} else if (pm_wakeup_pending()) {
		/* suspend aborted */
		sys_sync->abort = true;
		complete(sys_sync->complete);
	} else {
		mod_timer(&sys_sync->timer, jiffies +
				SUSPEND_SYS_SYNC_TIMEOUT);
	}
}

int suspend_sys_sync_wait(void)
{
	struct sys_sync_struct sys_sync;
	DECLARE_COMPLETION_ONSTACK(sys_sync_complete);
	int ret = 0;

	sys_sync.abort = false;
	sys_sync.complete = &sys_sync_complete;
	init_timer_on_stack(&sys_sync.timer);

	sys_sync.timer.function = sys_sync_handler;
	sys_sync.timer.data = (unsigned long)&sys_sync;
	sys_sync.timer.expires = jiffies + SUSPEND_SYS_SYNC_TIMEOUT;

	if (atomic_read(&sys_sync_count) != 0) {
		add_timer(&sys_sync.timer);
		wait_for_completion(&sys_sync_complete);
	}

	if (sys_sync.abort) {
		pr_info("suspend aborted during sys_sync\n");
		ret = -EAGAIN;
	}

	del_timer_sync(&sys_sync.timer);
	destroy_timer_on_stack(&sys_sync.timer);

	return ret;
}
//-- custom_t peter
static void try_to_suspend(struct work_struct *work)
{
	unsigned int initial_count, final_count;
#ifdef custom_t_DEBUG_SUSPEND
	extern void print_active_wakeup_sources(void);
	pr_info("PM_Debug: try_to_suspend autosleep_state %d\n",autosleep_state);
	get_active_wakeup_sources();
	
#ifdef CONFIG_PM_FORCE_SLEEP
	if (pm_is_force_sleep()) {
	pr_info("PM_Debug: pm_is_force_sleep is TRUE !!! , will call pm_suspend force");	
	}
#endif
	
#endif

#ifdef CONFIG_PM_FORCE_SLEEP
	if (pm_is_force_sleep()) {
		mutex_lock(&autosleep_lock);
		pm_suspend(autosleep_state);
		mutex_unlock(&autosleep_lock);
		goto out;
	}
#endif

	if (!pm_get_wakeup_count(&initial_count, true))
		goto out;

	mutex_lock(&autosleep_lock);

	if (!pm_save_wakeup_count(initial_count) ||
		system_state != SYSTEM_RUNNING) {
		mutex_unlock(&autosleep_lock);
		goto out;
	}

	if (autosleep_state == PM_SUSPEND_ON) {
		mutex_unlock(&autosleep_lock);
		return;
	}

#ifdef custom_t_DEBUG_SUSPEND
	pr_info("PM_Debug: try_to_suspend try to call %s\n",autosleep_state >= PM_SUSPEND_MAX ? "hibernate":"pm_suspend");
	get_active_wakeup_sources();
#endif

	if (autosleep_state >= PM_SUSPEND_MAX)
		hibernate();
	else
		pm_suspend(autosleep_state);

	mutex_unlock(&autosleep_lock);

	if (!pm_get_wakeup_count(&final_count, false))
	{
#ifdef custom_t_DEBUG_SUSPEND
		pr_info("PM_Debug: pm_get_wakeup_count 2nd return false; initial_count %d final_count %d\n",initial_count,final_count);
#endif
		goto out;
	}
	/*
	 * If the wakeup occured for an unknown reason, wait to prevent the
	 * system from trying to suspend and waking up in a tight loop.
	 */
	if (final_count == initial_count)
		schedule_timeout_uninterruptible(HZ / 2);

 out:
	queue_up_suspend_work();
}

static DECLARE_WORK(suspend_work, try_to_suspend);

void queue_up_suspend_work(void)
{
	if (autosleep_state > PM_SUSPEND_ON)
		queue_work(autosleep_wq, &suspend_work);
}

suspend_state_t pm_autosleep_state(void)
{
	return autosleep_state;
}

int pm_autosleep_lock(void)
{
	return mutex_lock_interruptible(&autosleep_lock);
}

void pm_autosleep_unlock(void)
{
	mutex_unlock(&autosleep_lock);
}

int pm_autosleep_set_state(suspend_state_t state)
{

#ifdef custom_t_DEBUG_SUSPEND
	pr_info("PM_Debug: pm_autosleep_set_state %d -> %d\n",autosleep_state,state);
	if(state == 3)
		get_active_wakeup_sources();

#endif

#ifndef CONFIG_HIBERNATION
	if (state >= PM_SUSPEND_MAX)
		return -EINVAL;
#endif

	__pm_stay_awake(autosleep_ws);

	mutex_lock(&autosleep_lock);

	autosleep_state = state;

	__pm_relax(autosleep_ws);

	if (state > PM_SUSPEND_ON) {
		pm_wakep_autosleep_enabled(true);
		queue_up_suspend_work();
	} else {
		pm_wakep_autosleep_enabled(false);
	}

	mutex_unlock(&autosleep_lock);
	return 0;
}

int __init pm_autosleep_init(void)
{
	autosleep_ws = wakeup_source_register("autosleep");
	if (!autosleep_ws)
		return -ENOMEM;

	autosleep_wq = alloc_ordered_workqueue("autosleep", 0);
//++ custom_t peter DACA-499 
	if (!autosleep_wq)
		goto err_exit;
 
	sys_sync_wq =
		alloc_ordered_workqueue("suspend_sys_sync", 0);
	if (sys_sync_wq)
		return 0;
err_exit:
	//-- custom_t peter
	wakeup_source_unregister(autosleep_ws);
	return -ENOMEM;
}
