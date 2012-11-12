#include <linux/module.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/kfifo.h>

/* Fifo device */
#define PB173_FIFO_SIZE (8*1024)

static DECLARE_KFIFO(pb173_fifo, unsigned char, PB173_FIFO_SIZE);
static DEFINE_MUTEX(fifo_read_mutex);
static DEFINE_MUTEX(fifo_write_mutex);

static ssize_t pb173_fifo_read(struct file *filp, char __user *buf,
	size_t count, loff_t *offp)
{
	ssize_t no;
	int rv;

	if (mutex_lock_interruptible(&fifo_read_mutex))
		return -ERESTARTSYS;

	rv = kfifo_to_user(&pb173_fifo, buf, count, &no);

	mutex_unlock(&fifo_read_mutex);

	if (rv)
		return -EFAULT;
	else
		return no;
}


static ssize_t pb173_fifo_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offp)
{
	ssize_t no;
	int rv;

	pr_info("fifo_write\n");

	if (mutex_lock_interruptible(&fifo_write_mutex))
		return -ERESTARTSYS;

	rv = kfifo_from_user(&pb173_fifo, buf, count, &no);

	mutex_unlock(&fifo_write_mutex);
	if (rv)
		return -EFAULT;
	else
		return no;
}



static const struct file_operations fifo_device_fops = {
	.owner		= THIS_MODULE,
	.read		= pb173_fifo_read,
	.write		= pb173_fifo_write,
};



static struct miscdevice fifo_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.fops		= &fifo_device_fops,
	.nodename	= "pb173_fifo",
	.name		= "pb173_fifo",
	.mode		= 0666,
};




/* shared read/write buffer */
#define BUFSIZE	(20*1024*1024)
static char *buffer;
static DEFINE_MUTEX(buffer_mutex);


/*****************************************/
/*		write device		 */

/* Only one can open write device */
static atomic_t wd_free = ATOMIC_INIT(1);

/* */
static ssize_t my_pretty_write(char *to, size_t avail, loff_t *ppos,
		const char __user *from, size_t count, unsigned mdelay)
{
	size_t i;
	char x;
	loff_t pos;

	pos = *ppos;
	if (pos < 0)
		return -EINVAL;

	/* bound check */
	if (pos >= avail || count == 0)
		return 0;

	if (count > avail - pos)
		count = avail - pos;

	/* lock buffer */
	mutex_lock(&buffer_mutex);
	/* write 'bytes' characters */
	for (i = 0; i < count; i++) {
		get_user(x, from + i);
		to[pos + i] = x;
		msleep(mdelay);
	}
	/* unlock buffer */
	mutex_unlock(&buffer_mutex);
	*ppos = pos + i;

	return i;
}

static ssize_t pb173_wd_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offp)
{
	size_t n;

	/* how many bytes? */
	if (count > 5)
		count = 5;

	n = my_pretty_write(buffer, BUFSIZE, offp, buf, count, 20);

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

static const struct file_operations write_device_fops = {
	.owner		= THIS_MODULE,
	.open		= pb173_wd_open,
	.write		= pb173_wd_write,
	.release	= pb173_wd_release,
};

static struct miscdevice write_device = {
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

	if (count == 0)
		return 0;

	mutex_lock(&buffer_mutex);
	n = simple_read_from_buffer(buf, count, offp, buffer, BUFSIZE);
	mutex_unlock(&buffer_mutex);

	return n;
}

static const struct file_operations read_device_fops = {
	.owner		= THIS_MODULE,
	.read		= pb173_rd_read,
};



static struct miscdevice read_device = {
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

static union {
	atomic_t atomic;
	__u16 debug;
} hello_count;

static const char *hello_string = "Ahoj";

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

static const struct file_operations hello_device_fops = {
	.owner		= THIS_MODULE,
	.open		= pb173_hello_open,
	.read		= pb173_hello_read,
	.write		= pb173_hello_write,
	.unlocked_ioctl = pb173_hello_ioctl,
	.release	= pb173_hello_release,
};



static struct miscdevice hello_device = {
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

static struct {
	struct dentry *rootdir;
	struct dentry *counter;
	struct dentry *core;


} debug;

static const struct file_operations debug_bintext_fops = {
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

static int pb173_init_memory(void)
{
	int i;
	void *addr;
	phys_addr_t paddr;

	buffer = vmalloc(BUFSIZE);
	if (buffer == NULL)
		return -ENOMEM;

	memset(buffer, 0, BUFSIZE);
	for (i = 0; i < BUFSIZE / PAGE_SIZE; i++) {
		addr = buffer + i*PAGE_SIZE;
		paddr = page_to_phys(vmalloc_to_page(addr));
		snprintf(addr, PAGE_SIZE, "%p:0x%llx\n", addr, (unsigned long long)paddr);
	}
	return 0;
}

static int pb173_init(void)
{
	int rv;

	/* clear counter */
	atomic_set(&hello_count.atomic, 0);
	pr_info("init\n");


	rv = misc_register(&fifo_device);
	if (rv < 0)
		goto error_fifo;
	INIT_KFIFO(pb173_fifo);

	rv = misc_register(&read_device);
	if (rv < 0)
		goto error_rdev;

	rv = misc_register(&write_device);
	if (rv < 0)
		goto error_wdev;

	rv = misc_register(&hello_device);
	if (rv < 0)
		goto error_hdev;


	/* some debugging stuff */
	if (create_debugfs_entries() != 0) {
		rv = -EIO;
		goto error_debugfs;
	}

	rv = pb173_init_memory();
	if (rv < 0) {
		rv = -ENOMEM;
		goto error_mem;
	}

	/* print core */
/*
	print_hex_dump_bytes("", DUMP_PREFIX_NONE,
			THIS_MODULE->module_core,
			THIS_MODULE->core_text_size);
*/

	pr_info("[pb173]:\tread: %lx, write: %lx\n",
		(unsigned long) IOCTL_READ,
		(unsigned long) IOCTL_WRITE);

	return 0;
	/* error handling */
error_mem:
	remove_debugfs_entries();
error_debugfs:
	misc_deregister(&hello_device);
error_hdev:
	misc_deregister(&write_device);
error_wdev:
	misc_deregister(&read_device);
error_rdev:
	misc_deregister(&fifo_device);
error_fifo:

	pr_warn("error while loading pb173\n");
	return -rv;
}

static void pb173_exit(void)
{
	vfree(buffer);
	remove_debugfs_entries();
	misc_deregister(&read_device);
	misc_deregister(&write_device);
	misc_deregister(&hello_device);
	misc_deregister(&fifo_device);

	pr_info("[pb173]\tunloaded.\n");
}

module_init(pb173_init);
module_exit(pb173_exit);

MODULE_LICENSE("GPL");
