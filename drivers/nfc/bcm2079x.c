/*****************************************************************************
* Copyright 2013 Broadcom Corporation.  All rights reserved.
 *
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
 *
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/version.h>
#include <linux/pm_wakeup.h>
#include <linux/timer.h>

#define USE_PLATFORM_DATA

#ifdef USE_PLATFORM_DATA
#include <linux/nfc/bcm2079x.h>
#else
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#endif

#define MAX_BUFFER_SIZE		780

#define PACKET_HEADER_SIZE_NCI	(4)
#define PACKET_HEADER_SIZE_HCI	(3)
#define PACKET_TYPE_NCI		(16)
#define PACKET_TYPE_HCIEV	(4)
#define MAX_PACKET_SIZE		(PACKET_HEADER_SIZE_NCI + 255)

#define BCMNFC_MAGIC	0xFA
/*
 * BCMNFC power control via ioctl
 * BCMNFC_POWER_CTL(0): power off
 * BCMNFC_POWER_CTL(1): power on
 * BCMNFC_WAKE_CTL(0): wake off
 * BCMNFC_WAKE_CTL(1): wake on
 */
#define BCMNFC_POWER_CTL	_IO(BCMNFC_MAGIC, 0x01)
#define BCMNFC_WAKE_CTL         _IO(BCMNFC_MAGIC, 0x05)
#define BCMNFC_SET_ADDR		_IO(BCMNFC_MAGIC, 0x07)


struct bcm2079x_dev {
	wait_queue_head_t read_wq;
	struct mutex read_mutex;
	struct i2c_client *client;
	struct miscdevice bcm2079x_device;
	unsigned int wake_gpio;
	unsigned int en_gpio;
	unsigned int irq;
	bool irq_enabled;
	unsigned int count_read;
	unsigned int count_irq;
	unsigned int original_address;
	struct wakeup_source *host_wake_ws;
	struct timer_list wake_timer;
};


static void bcm2079x_init_stat(struct bcm2079x_dev *bcm2079x_dev)
{
	bcm2079x_dev->count_read = 0;
	bcm2079x_dev->count_irq = 0;
}

static irqreturn_t bcm2079x_dev_irq_handler(int irq, void *dev_id)
{
	struct bcm2079x_dev *bcm2079x_dev = dev_id;

	bcm2079x_dev->count_irq++;
	__pm_stay_awake(bcm2079x_dev->host_wake_ws);
	mod_timer(&bcm2079x_dev->wake_timer, jiffies + msecs_to_jiffies(2000));
	wake_up(&bcm2079x_dev->read_wq);

	return IRQ_HANDLED;
}

void wake_timer_callback(unsigned long data)
{
	struct bcm2079x_dev *bcm2079x_dev = (struct bcm2079x_dev *)data;
	__pm_relax(bcm2079x_dev->host_wake_ws);

}

static unsigned int bcm2079x_dev_poll(struct file *filp, poll_table *wait)
{
	struct bcm2079x_dev *bcm2079x_dev = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &bcm2079x_dev->read_wq, wait);
	if (bcm2079x_dev->count_irq > 0)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static ssize_t bcm2079x_dev_read(struct file *filp, char __user *buf,
				  size_t count, loff_t *offset)
{
	struct bcm2079x_dev *bcm2079x_dev = filp->private_data;
	unsigned char tmp[MAX_BUFFER_SIZE];
	int total, len, ret;

	total = 0;
	len = 0;

	if (bcm2079x_dev->count_irq > 0)
		bcm2079x_dev->count_irq--;

	bcm2079x_dev->count_read++;
	if (count > MAX_BUFFER_SIZE)
		count = MAX_BUFFER_SIZE;

	mutex_lock(&bcm2079x_dev->read_mutex);

	/* Read the first 4 bytes to include
	the length of the NCI or HCI packet. */
	ret = i2c_master_recv(bcm2079x_dev->client,
		tmp, PACKET_HEADER_SIZE_NCI);
	if (ret == PACKET_HEADER_SIZE_NCI) {
		total = ret;
		/* First byte is the packet type */
		switch (tmp[0]) {
		case PACKET_TYPE_NCI:
			len = tmp[PACKET_HEADER_SIZE_NCI-1];
			break;

		case PACKET_TYPE_HCIEV:
			len = tmp[PACKET_HEADER_SIZE_HCI-1];
			if (len == 0)
				total--;
		/*Since payload is 0, decrement total size (from 4 to 3) */
			else
				len--;
		/*First byte of payload is in tmp[3] already */
			break;

		default:
			len = 0;/*Unknown packet byte */
			break;
		}

		/* make sure full packet fits in the buffer */
		if (len > 0 && (len + total) <= count) {
			/* read the remainder of the packet. */
			ret = i2c_master_recv(bcm2079x_dev->client,
				tmp + total, len);
			if (ret < 0) {
				mutex_unlock(&bcm2079x_dev->read_mutex);
				return ret;
			}
				total += len;
		}
	} else {
		mutex_unlock(&bcm2079x_dev->read_mutex);
		if (ret < 0)
			return ret;
		else {
			dev_err(&bcm2079x_dev->client->dev,
				"received only %d bytes as header\n",
				ret);
		return -EIO;
		}
	}

	mutex_unlock(&bcm2079x_dev->read_mutex);

	if (total > count || copy_to_user(buf, tmp, total)) {
		dev_err(&bcm2079x_dev->client->dev,
			"failed to copy to user space, total = %d\n", total);
		total = -EFAULT;
	}

	if (bcm2079x_dev->count_irq == 0) {
		del_timer(&bcm2079x_dev->wake_timer);
		__pm_relax(bcm2079x_dev->host_wake_ws);
	}

	return total;
}

static ssize_t bcm2079x_dev_write(struct file *filp, const char __user *buf,
				   size_t count, loff_t *offset)
{
	struct bcm2079x_dev *bcm2079x_dev = filp->private_data;
	char tmp[MAX_BUFFER_SIZE];
	int ret;

	if (count > MAX_BUFFER_SIZE) {
		dev_err(&bcm2079x_dev->client->dev, "out of memory\n");
		return -ENOMEM;
	}

	if (copy_from_user(tmp, buf, count)) {
		dev_err(&bcm2079x_dev->client->dev,
			"failed to copy from user space\n");
		return -EFAULT;
	}

	mutex_lock(&bcm2079x_dev->read_mutex);
	/* Write data */

	ret = i2c_master_send(bcm2079x_dev->client, tmp, count);

	if (ret < 0)
		dev_err(&bcm2079x_dev->client->dev,
			"Error in write %d\n", ret);
	else if (ret < count) {
			dev_err(&bcm2079x_dev->client->dev,
				"failed to write %d bytes, %d bytes were written\n",
				count, ret);
			ret = -EIO;
	}
	mutex_unlock(&bcm2079x_dev->read_mutex);

	return ret;
}

static int bcm2079x_dev_open(struct inode *inode, struct file *filp)
{
	int ret = 0;

	struct bcm2079x_dev *bcm2079x_dev = container_of(filp->private_data,
							   struct bcm2079x_dev,
							   bcm2079x_device);

	filp->private_data = bcm2079x_dev;
	bcm2079x_init_stat(bcm2079x_dev);

	return ret;
}

static long bcm2079x_dev_unlocked_ioctl(struct file *filp,
					 unsigned int cmd, unsigned long arg)
{
	struct bcm2079x_dev *bcm2079x_dev = filp->private_data;

	switch (cmd) {
	case BCMNFC_POWER_CTL:
		dev_dbg(&bcm2079x_dev->client->dev,
			 "%s, BCMNFC_POWER_CTL (%x, %lx):\n", __func__, cmd,
			arg);
		if (arg == 1) {	/* Power On */
			bcm2079x_init_stat(bcm2079x_dev);
			gpio_set_value(bcm2079x_dev->en_gpio, 1);
			if (bcm2079x_dev->irq_enabled == false) {
				enable_irq(bcm2079x_dev->irq);
				bcm2079x_dev->irq_enabled = true;
			}
		} else {
			if (bcm2079x_dev->irq_enabled == true) {
				bcm2079x_dev->irq_enabled = false;
				disable_irq(bcm2079x_dev->irq);

				}
			gpio_set_value(bcm2079x_dev->en_gpio, 0);
		}
		break;
	case BCMNFC_WAKE_CTL:
		dev_dbg(&bcm2079x_dev->client->dev,
			 "%s, BCMNFC_WAKE_CTL (%x, %lx):\n", __func__, cmd,
			     arg);
		gpio_set_value(bcm2079x_dev->wake_gpio, arg);

		break;

	default:
		dev_err(&bcm2079x_dev->client->dev,
			"%s, unknown cmd (%x, %lx)\n", __func__, cmd, arg);
		return 0;
	}

	return 0;
}

static const struct file_operations bcm2079x_dev_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.poll = bcm2079x_dev_poll,
	.read = bcm2079x_dev_read,
	.write = bcm2079x_dev_write,
	.open = bcm2079x_dev_open,
	.unlocked_ioctl = bcm2079x_dev_unlocked_ioctl
};

static int bcm2079x_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	int ret = 0;
	struct bcm2079x_dev *bcm2079x_dev;

#ifdef USE_PLATFORM_DATA
	struct bcm2079x_i2c_platform_data *platform_data;
	platform_data = client->dev.platform_data;
#else
	struct device_node *np = client->dev.of_node;
#endif

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "need I2C_FUNC_I2C\n");
		ret = -EIO;
		goto err_exit;
	}

	bcm2079x_dev = devm_kzalloc(&client->dev,
		sizeof(*bcm2079x_dev), GFP_KERNEL);
	if (bcm2079x_dev == NULL) {
		dev_err(&client->dev,
			"failed to allocate memory for module data\n");
		ret = -ENOMEM;
		goto err_exit;
	}

#ifdef USE_PLATFORM_DATA
	bcm2079x_dev->wake_gpio = platform_data->wake_gpio;
	bcm2079x_dev->en_gpio = platform_data->en_gpio;
	bcm2079x_dev->irq = gpio_to_irq(platform_data->irq_gpio);
	platform_data->init(platform_data);

#else
	/*
	 * Obtain the GPIO.
	 */
	bcm2079x_dev->wake_gpio =
			of_get_named_gpio(np, "wake-gpios", 0);
	if (gpio_is_valid(bcm2079x_dev->wake_gpio)) {
		ret = devm_gpio_request_one(&client->dev,
			bcm2079x_dev->wake_gpio,
		       GPIOF_OUT_INIT_LOW,
		       "bcm2079x_wake");
		if (ret) {
			dev_err(&client->dev, "Can not request wake-gpio %d\n",
				bcm2079x_dev->wake_gpio);
			goto err_exit;
		}
	} else {
		dev_err(&client->dev, "Can not find wake-gpio %d\n",
			bcm2079x_dev->wake_gpio);
		ret = bcm2079x_dev->wake_gpio;
		goto err_exit;

	}

	bcm2079x_dev->en_gpio =
			of_get_named_gpio(np, "en-gpios", 0);
	if (gpio_is_valid(bcm2079x_dev->en_gpio)) {
		ret = devm_gpio_request_one(&client->dev, bcm2079x_dev->en_gpio,
		       GPIOF_OUT_INIT_LOW,
		       "bcm2079x_en");
		if (ret) {
			dev_err(&client->dev, "Can not request en-gpio %d\n",
				bcm2079x_dev->en_gpio);
			goto err_exit;
		}
	} else {
		dev_err(&client->dev, "Can not find en-gpio %d\n",
			bcm2079x_dev->en_gpio);
		ret = bcm2079x_dev->en_gpio;
		goto err_exit;

	}


	/*
	 * Obtain the interrupt pin.
	 */
	bcm2079x_dev->irq = irq_of_parse_and_map(np, 0);
	if (!bcm2079x_dev->irq) {
		dev_err(&client->dev, "can not get irq\n");
		goto err_exit;
	}

	dev_info(&client->dev,
		"bcm2079x_probe gpio en %d, wake %d, irq %d\n",
		bcm2079x_dev->en_gpio,
		bcm2079x_dev->wake_gpio,
		bcm2079x_dev->irq);
#endif

	gpio_set_value(bcm2079x_dev->en_gpio, 0);
	bcm2079x_dev->client = client;

	/* init mutex and queues */
	init_waitqueue_head(&bcm2079x_dev->read_wq);
	mutex_init(&bcm2079x_dev->read_mutex);

	bcm2079x_dev->bcm2079x_device.minor = MISC_DYNAMIC_MINOR;
	bcm2079x_dev->bcm2079x_device.name = "bcm2079x";
	bcm2079x_dev->bcm2079x_device.fops = &bcm2079x_dev_fops;

	ret = misc_register(&bcm2079x_dev->bcm2079x_device);
	if (ret) {
		dev_err(&client->dev, "misc_register failed %d\n", ret);
		goto err_misc_register;
	}

	bcm2079x_dev->host_wake_ws = wakeup_source_register(
					"bcm2079x-ws");
	if (bcm2079x_dev->host_wake_ws == NULL) {
		dev_err(&client->dev,
			"failed to register host-wake wakeup source\n");
		goto err_misc_register;
	}

	setup_timer(&bcm2079x_dev->wake_timer,
		wake_timer_callback, (int)bcm2079x_dev);

	ret = request_irq(bcm2079x_dev->irq,
			bcm2079x_dev_irq_handler,
			IRQF_NO_SUSPEND | IRQF_TRIGGER_RISING,
			client->name, bcm2079x_dev);
	if (ret) {
		dev_err(&client->dev, "request_irq %d failed %d\n",
			bcm2079x_dev->irq, ret);
		goto err_request_irq_failed;
	}

	ret = irq_set_irq_wake(bcm2079x_dev->irq, 1);
	if (ret) {
		dev_err(&client->dev, "irq_set_irq_wake %d failed %d\n",
			bcm2079x_dev->irq, ret);
		goto err_request_irq_failed;
	}

	disable_irq_nosync(bcm2079x_dev->irq);
	bcm2079x_dev->irq_enabled = false;

	bcm2079x_dev->original_address = client->addr;

	i2c_set_clientdata(client, bcm2079x_dev);

	return 0;

err_request_irq_failed:
	misc_deregister(&bcm2079x_dev->bcm2079x_device);
err_misc_register:
	mutex_destroy(&bcm2079x_dev->read_mutex);
err_exit:
	return ret;
}

static int bcm2079x_remove(struct i2c_client *client)
{
	struct bcm2079x_dev *bcm2079x_dev;

	bcm2079x_dev = i2c_get_clientdata(client);
	free_irq(client->irq, bcm2079x_dev);
	misc_deregister(&bcm2079x_dev->bcm2079x_device);
	mutex_destroy(&bcm2079x_dev->read_mutex);
	wakeup_source_unregister(bcm2079x_dev->host_wake_ws);

	return 0;
}

static const struct i2c_device_id bcm2079x_id[] = {
	{"bcm2079x", 0},
	{}
};

static const struct of_device_id bcm2079x_match[] = {
	{.compatible = "bcm,nfc-i2c"},
};

static struct i2c_driver bcm2079x_driver = {
	.id_table = bcm2079x_id,
	.probe = bcm2079x_probe,
	.remove = bcm2079x_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "bcm2079x",
		   .of_match_table = bcm2079x_match,
	},
};

/*
 * module load/unload record keeping
 */

static int __init bcm2079x_dev_init(void)
{
	return i2c_add_driver(&bcm2079x_driver);
}
module_init(bcm2079x_dev_init);

static void __exit bcm2079x_dev_exit(void)
{
	i2c_del_driver(&bcm2079x_driver);
}
module_exit(bcm2079x_dev_exit);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("NFC bcm2079x driver");
MODULE_LICENSE("GPL");
