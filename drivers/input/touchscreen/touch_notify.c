/*
 * Copyright 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * A copy of the GPL is available at
 * http://www.broadcom.com/licenses/GPLv2.php, or by writing to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#include <linux/notifier.h>
#include <linux/export.h>

static BLOCKING_NOTIFIER_HEAD(touch_notifier_list);

/**
 *	touch_register_client - register a client notifier
 *	@nb: notifier block to callback on events
 */
int touch_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&touch_notifier_list, nb);
}
EXPORT_SYMBOL(touch_register_client);

/**
 *	touch_unregister_client - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int touch_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&touch_notifier_list, nb);
}
EXPORT_SYMBOL(touch_unregister_client);


/**
 * touch_notifier_call_chain - notify clients of touch events
 *
 */
int touch_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&touch_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(touch_notifier_call_chain);

