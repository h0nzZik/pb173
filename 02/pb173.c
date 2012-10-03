/*
 *	See test/test.py
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/ioctl.h>

#define MY_MAGIC	'?'
#define IOCTL_READ	_IOR(MY_MAGIC, 0, int *)
#define IOCTL_WRITE	_IOW(MY_MAGIC, 1, int)


int cnt;
char *my_string = "Ahoj";

struct pb173_data {
	int id;
	size_t len;
};


#define DEFAULT_LEN strlen(my_string)
static void set_len(struct file *filp, size_t len)
{
	size_t slen;
	struct pb173_data *data;

	data = filp->private_data;
	slen = strlen(my_string);

	if (len == 0)
		data->len = DEFAULT_LEN;
	else if (len > slen)
		data->len = slen;
	else
		data->len = len;
	return;
}

static void get_len(struct file *filp, int *ptr)
{
	struct pb173_data *data;

	data = filp->private_data;
	put_user(data->len, ptr);
	pr_info("[pb173]\tparameter: %p\n", ptr);
	return;
}

static long pb173_ioctl(struct file *filp,
		unsigned int cmd, unsigned long argument)
{
	union ioctl_arg {
		long l;
		size_t x;
		int *ptr;
	} arg;

	arg.l = argument;
	switch (cmd) {
	case IOCTL_READ:
		get_len(filp, arg.ptr);
		break;
	case IOCTL_WRITE:
		set_len(filp, arg.x);
		break;
	default:
		return -ENOTTY;
	}
	return 0;
}

static ssize_t pb173_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offp)
{
	char *string;
	size_t n = 0;

	/* buffer too big */
	if (count > 50)
		return -ENOSPC;

	string = kmalloc(count+1, GFP_KERNEL);
	if (string == NULL)
		return -ENOSPC;

	n = count - copy_from_user(string, buf, count);
	string[n] = '\0';
	pr_info("pb173:\t\"%s\"\n", string);
	kfree(string);
	return n;
}


static ssize_t pb173_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offp)
{
	size_t len;
	int n;
	struct pb173_data *data;

	if (count == 0)
		return 0;

	data = filp->private_data;
	if (count > data->len)
		len = data->len;
	else
		len = count;

	n = len-copy_to_user(buf, my_string, len);
/*	pr_info("copied %d bytes to %d\n", n, data->id); */
	return n;
}

static int pb173_open(struct inode *inode, struct file *filp)
{
	struct pb173_data *data;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->id = ++cnt;
	filp->private_data = data;
	set_len(filp, 0);
	pr_info("pb173[%d]\topened\n", data->id);
	return 0;
}

static int pb173_release(struct inode *inode, struct file *filp)
{
	struct pb173_data *data;

	data = filp->private_data;
	pr_info("pb173[%d]\tclosed\n", data->id);
	kfree(filp->private_data);
	filp->private_data = NULL;
	cnt--;
	return 0;
}


const struct file_operations fops = {
	.owner	= THIS_MODULE,
	.open	= pb173_open,
	.read	= pb173_read,
	.write	= pb173_write,
	.unlocked_ioctl = pb173_ioctl,
	.release = pb173_release,
};

struct miscdevice mdev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.fops	= &fops,
	.nodename = "pb173",
	.mode	= 0666,
};

static int pb173_init(void)
{
	int rv;
	rv = misc_register(&mdev);
	if (rv >= 0) {
		cnt = 0;
		pr_info("[pb173]:\tread: %x, write: %x\n",
			IOCTL_READ, IOCTL_WRITE);
	}
	return rv;
}

static void pb173_exit(void)
{
	pr_info("[pb173]\tunloaded.");
	misc_deregister(&mdev);
}

module_init(pb173_init);
module_exit(pb173_exit);

MODULE_LICENSE("GPL");
