#include <linux/module.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/debugfs.h>
#include <linux/delay.h>



/* shared read/write buffer */
#define BUFSIZE	128
char buffer[BUFSIZE];
static rwlock_t buffer_lock = __RW_LOCK_UNLOCKED(buffer_lock);

/*****************************************/
/*		write device		 */

/* Only one can open write device */
atomic_t wd_free = ATOMIC_INIT(1);

static ssize_t pb173_wd_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offp)
{
	unsigned long flags;
	size_t n;

	/* how many bytes? */
	if (count > 5)
		count = 5;

	/* lock, write & unlock */
	write_lock_irqsave(&buffer_lock, flags);
	n = simple_write_to_buffer(buffer, BUFSIZE, offp, buf, count);
	write_unlock_irqrestore(&buffer_lock, flags);

	msleep(20);
	return n;
}

static int pb173_wd_open(struct inode *inode, struct file *filp)
{
	/* exclusive write access */
	if (filp->f_mode & FMODE_WRITE) {
		if (atomic_dec_and_test(&wd_free) == 0) {
			atomic_inc(&wd_free);
			return -EBUSY;
		}
	}

	pr_info("pb173-write:\topened\n");
	return 0;
}


static int pb173_wd_release(struct inode *inode, struct file *filp)
{
	if (filp->f_mode & FMODE_WRITE)
		atomic_inc(&wd_free);

	pr_info("pb173-write:\tclosed\n");
	return 0;
}

const struct file_operations write_device_fops = {
	.owner		= THIS_MODULE,
	.open		= pb173_wd_open,
	.write		= pb173_wd_write,
	.release	= pb173_wd_release,
};

struct miscdevice write_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.fops		= &write_device_fops,
	.nodename	= "pb173_write",
	.name		= "pb173_write",
	.mode		= 0222,
};

/****************************************/
/*		read device		*/

static ssize_t pb173_rd_read(struct file *filp, char __user *buf,
	size_t count, loff_t *offp)
{
	int n;
	unsigned long flags;

	if (count == 0)
		return 0;

	read_lock_irqsave(&buffer_lock, flags);
	n = simple_read_from_buffer(buf, count, offp, buffer, BUFSIZE);
	read_unlock_irqrestore(&buffer_lock, flags);

	return n;
}

const struct file_operations read_device_fops = {
	.owner		= THIS_MODULE,
	.read		= pb173_rd_read,
};



struct miscdevice read_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.fops		= &read_device_fops,
	.nodename	= "pb173_read",
	.name		= "pb173_read",
	.mode		= 0444,
};

/****************************************/
/*	'hello world' read/write device	*/

#define MY_MAGIC	'?'
#define IOCTL_READ	_IOR(MY_MAGIC, 0, int)	/* ... */
#define IOCTL_WRITE	_IOW(MY_MAGIC, 1, int)

union {
	atomic_t atomic;
	__u16 debug;
} hello_count;

const char *hello_string = "Ahoj";

struct pb173_hello {
	int id;
	atomic_t len;
};

#define DEFAULT_LEN strlen(hello_string)
static void pb173_hello_set_len(struct file *filp, size_t len)
{
	size_t slen;
	struct pb173_hello *data;

	data = filp->private_data;
	slen = strlen(hello_string);

	if (len == 0)
		len = DEFAULT_LEN;
	else if (len > slen)
		len = slen;

	atomic_set(&data->len, len);
	return;
}

static void pb173_hello_get_len(struct file *filp, int __user *ptr)
{
	struct pb173_hello *data;

	data = filp->private_data;
	put_user(atomic_read(&data->len), ptr);
	return;
}


static ssize_t pb173_hello_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offp)
{
	char *string;
	size_t n = 0;
	struct pb173_hello *data;
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


static ssize_t pb173_hello_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offp)
{
	size_t len;
	size_t slen;
	int n;
	struct pb173_hello *data;

	if (count == 0)
		return 0;

	data = filp->private_data;
	slen = atomic_read(&data->len);
	if (count > slen)
		len = slen;
	else
		len = count;

	n = len-copy_to_user(buf, hello_string, len);
	return n;
}
static int pb173_hello_open(struct inode *inode, struct file *filp)
{
	struct pb173_hello *data;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	filp->private_data = data;
	data->id = atomic_inc_return(&hello_count.atomic);
	pb173_hello_set_len(filp, 0);
	pr_info("pb173_hello:\topened %d\n", data->id);

	return 0;
}

static int pb173_hello_release(struct inode *inode, struct file *filp)
{
	struct pb173_hello *data;

	data = filp->private_data;
	pr_info("pb173_hello:\tclosed %d\n", data->id);
	kfree(data);
	filp->private_data = NULL;
	atomic_dec(&hello_count.atomic);
	return 0;
}

static long pb173_hello_ioctl(struct file *filp,
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
		pb173_hello_get_len(filp, arg.ptr);
		break;
	case IOCTL_WRITE:
		pb173_hello_set_len(filp, arg.x);
		break;
	default:
		return 0;
	}
	return 0;
}

const struct file_operations hello_device_fops = {
	.owner		= THIS_MODULE,
	.open		= pb173_hello_open,
	.read		= pb173_hello_read,
	.write		= pb173_hello_write,
	.unlocked_ioctl = pb173_hello_ioctl,
	.release	= pb173_hello_release,
};



struct miscdevice hello_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.fops		= &hello_device_fops,
	.nodename	= "pb173_hello",
	.name		= "pb173_hello",
	.mode		= 0666,
};





/*	some debugging stuff */

static ssize_t core_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	ssize_t rv;

	rv = simple_read_from_buffer(buf, count, ppos,
			THIS_MODULE->module_core,
			THIS_MODULE->core_text_size);
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
	debug.counter = debugfs_create_u16("hello_count", 0444,
				debug.rootdir, &hello_count.debug);
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
	atomic_set(&hello_count.atomic, 0);
	pr_info("init\n");
	/* register read device */
	rv = misc_register(&read_device);
	if (rv < 0) {
		pr_info("can't register read device\n");
		return rv;
	}

	/* :-( */
	pr_info("[pb173]:\tread: %lx, write: %lx\n",
		(unsigned long) IOCTL_READ,
		(unsigned long) IOCTL_WRITE);


	/* register write device */
	rv = misc_register(&write_device);
	if (rv < 0) {
		misc_deregister(&read_device);
		pr_info("can't register write device\n");
		return rv;
	}
	/* clear buffer */
	memset(buffer, 0, sizeof(buffer));

	rv = misc_register(&hello_device);
	if (rv < 0) {
		misc_deregister(&read_device);
		misc_deregister(&write_device);
		pr_info("can't register hello device\n");
		return rv;
	}

	/* some debugging stuff */
	if (create_debugfs_entries() != 0) {
		misc_deregister(&read_device);
		misc_deregister(&write_device);
		misc_deregister(&hello_device);
		return -EIO;
	}
	pr_info("[pb173]:\tsizeof(int) == %d\n", sizeof(int));
	/* print core */
/*
	print_hex_dump_bytes("", DUMP_PREFIX_NONE,
			THIS_MODULE->module_core,
			THIS_MODULE->core_text_size);
*/
	return 0;
}

static void pb173_exit(void)
{
	remove_debugfs_entries();
	misc_deregister(&read_device);
	misc_deregister(&write_device);
	misc_deregister(&hello_device);

	pr_info("[pb173]\tunloaded.\n");
}

module_init(pb173_init);
module_exit(pb173_exit);

MODULE_LICENSE("GPL");
