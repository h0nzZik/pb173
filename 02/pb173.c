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
#include <linux/debugfs.h>

#define MY_MAGIC	'?'
#define IOCTL_READ	_IOR(MY_MAGIC, 0, int)	/* ... */
#define IOCTL_WRITE	_IOW(MY_MAGIC, 1, int)


__u16 count;			/* count of opened instances */
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
	pr_info("st: %x\n", cmd);
	arg.l = argument;
	switch (cmd) {
	case IOCTL_READ:
		get_len(filp, arg.ptr);
		break;
	case IOCTL_WRITE:
		set_len(filp, arg.x);
		break;
	default:
		return 0;
	}
	return 0;
}

static ssize_t pb173_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offp)
{
	char *string;
	size_t n = 0;
	struct pb173_data *data;
	data = filp->private_data;

	/* buffer too big */
	if (count > 50)
		return -ENOSPC;

	string = kmalloc(count+1, GFP_KERNEL);
	if (string == NULL)
		return -ENOSPC;

	n = count - copy_from_user(string, buf, count);
	string[n] = '\0';
	pr_info("pb173[%d]:\t'%s'\n", data->id, string);
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
	return n;
}

static int pb173_open(struct inode *inode, struct file *filp)
{
	struct pb173_data *data;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->id = ++count;
	filp->private_data = data;
	set_len(filp, 0);
	pr_info("pb173[%d]:\topened\n", data->id);
	return 0;
}

static int pb173_release(struct inode *inode, struct file *filp)
{
	struct pb173_data *data;

	data = filp->private_data;
	pr_info("pb173[%d]:\tclosed\n", data->id);
	kfree(filp->private_data);
	filp->private_data = NULL;
	count--;
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



/*	some debugging stuff */


static ssize_t core_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	int rv;
	if (ppos && *ppos != 0)
		return 0;

	rv = simple_read_from_buffer(buf,
			THIS_MODULE->core_text_size, ppos,
			THIS_MODULE->module_core, count);
	return rv;
}

struct {
	struct dentry *rootdir;
	struct dentry *counter;
	struct dentry *core;


} debug;

const struct file_operations debug_bintext_fops = {
	.owner	= THIS_MODULE,
	.read	= core_read,
};

static void remove_debugfs_entries(void)
{
	debugfs_remove_recursive(debug.rootdir);
	memset(&debug, 0, sizeof(debug));
}

static int _create_debugfs_entries(void)
{
	/*	clear all	*/
	memset(&debug, 0, sizeof(debug));

	/*	create root directory	*/
	debug.rootdir = debugfs_create_dir("pb171", NULL);
	if (IS_ERR_OR_NULL(debug.rootdir)) {
		debug.rootdir = NULL;
		return -1;
	}

	/*	create instance counter	*/
	debug.counter = debugfs_create_u16("counter", 0444,
				debug.rootdir, &count);
	if (IS_ERR_OR_NULL(debug.counter)) {
		debug.counter = NULL;
		return -1;
	}

	/*	create 'core' file	*/
	debug.core = debugfs_create_file("core", 0444,
			debug.rootdir, NULL, &debug_bintext_fops);
	if (IS_ERR_OR_NULL(debug.core)) {
		debug.core = NULL;
		return -1;
	}

	return 0;
}

static int create_debugfs_entries(void)
{
	if (_create_debugfs_entries() != 0) {
		remove_debugfs_entries();
		return -1;
	}
	return 0;
}

static int pb173_init(void)
{
	int rv;

	/* clear counter */
	count = 0;

	/* register device */
	rv = misc_register(&mdev);
	if (rv >= 0) {
		count = 0;
		/* :-( */
		pr_info("[pb173]:\tread: %lx, write: %lx\n",
			(unsigned long) IOCTL_READ,
			(unsigned long) IOCTL_WRITE);
	} else
		return rv;

	/* some debugging stuff */
	if (create_debugfs_entries() != 0) {
		misc_deregister(&mdev);
		return -EIO;
	}
	pr_info("[pb173]:\tsizeof(int) == %d\n", sizeof(int));
	/* print core */
	print_hex_dump_bytes("", DUMP_PREFIX_NONE,
			THIS_MODULE->module_core,
			THIS_MODULE->core_text_size);

	return 0;
}

static void pb173_exit(void)
{
	remove_debugfs_entries();
	misc_deregister(&mdev);

	pr_info("[pb173]\tunloaded.\n");
}

module_init(pb173_init);
module_exit(pb173_exit);

MODULE_LICENSE("GPL");
