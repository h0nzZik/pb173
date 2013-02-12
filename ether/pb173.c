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
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>


static const unsigned char my_packet[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x45, 0x00,
	0x00, 0xe4, 0x00, 0x00, 0x40, 0x00, 0x40, 0x01,
	0x3c, 0x17, 0x7f, 0x00, 0x00, 0x01, 0x7f, 0x00,
	0x00, 0x01, 0x08, 0x00, 0x5c, 0x67, 0x73, 0x95,
	0x00, 0x01, 0x77, 0x2c, 0x06, 0x4d, 0x00, 0x00,
	0x00, 0x00, 0x01, 0x92, 0x0e, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
	0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
	0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
	0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d,
	0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
	0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d,
	0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
	0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d,
	0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55,
	0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d,
	0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65,
	0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d,
	0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75,
	0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d,
	0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
	0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d,
	0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
	0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d,
	0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5,
	0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad,
	0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5,
	0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd,
	0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5,
	0xc6, 0xc7
};

static const unsigned long my_packet_size = sizeof(my_packet);


static struct net_device *my_device;


/**
 * When someone writes data into /dev/something,
 * this will send it to higher layer
 */
static ssize_t net_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offp)
{
	struct sk_buff *packet;
	long n;

	/* prepare packet */
	packet = netdev_alloc_skb(my_device, count);
	if (!packet)
		goto err_alloc;
	packet->len = count;
	packet->protocol = eth_type_trans(packet, my_device);

	/* fill with data & send it */
	n = copy_from_user(packet->data, buf, count);
	netif_rx(packet);

	return count - n;

err_alloc:
	pr_info("net_write() error\n");
	return -ENOMEM;
}


static struct file_operations net_fops = {
	.owner = THIS_MODULE,
	.write = net_write,
};


/* some interface in /dev */
static atomic_t md_use = ATOMIC_INIT(0);
static struct miscdevice net_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "pb173_net",
	.fops  = &net_fops,
};




/*
 * Private data
 */
struct my_netdev_struct {
	int id;
	struct timer_list timer;
};

static void my_timer_func(unsigned long data)
{
	struct net_device *dev;
	struct sk_buff *packet;
	struct my_netdev_struct *priv;

	dev = (void *)data;
	priv = netdev_priv(dev);

	pr_info("my_timer_func()\n");
	/* setup the timer */
	mod_timer(&priv->timer, jiffies + msecs_to_jiffies(1000));


	/* create new packet and send it to upper layer */
	packet = netdev_alloc_skb(dev, my_packet_size);
	if (!packet) {
		pr_info("can't allocate packet\n");
		return ;
	}
	memcpy(packet->data, my_packet, my_packet_size);
	packet->len = my_packet_size;
	(((char *)packet->data)[24])++;
	packet->protocol = eth_type_trans(packet, dev);
	netif_rx(packet);
}

/**
 * When someone wants to send a data through our device
 * @param skb	data to send
 * @param dev	target device
 */
static netdev_tx_t my_netdev_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	pr_info("[my_netdev]\tstart xmit\n");
	print_hex_dump_bytes("", 0, skb->data, skb->len);
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

/**
 * Deactivate IF
 * @param dev	network device to stop
 */
static int my_netdev_stop(struct net_device *dev)
{
	struct my_netdev_struct *priv;

	pr_info("[my_netdev]\tstop\n");
	priv = netdev_priv(dev);
	del_timer_sync(&priv->timer);

	/* unregister device */
	misc_deregister(&net_misc);

	atomic_dec(&md_use);
	return 0;
}


/**
 * Activate IF
 * @param dev	device to activate
 */
static int my_netdev_open(struct net_device *dev)
{
	struct my_netdev_struct *priv;


	if (atomic_inc_return(&md_use) != 1)
		goto err_atomic;

	if (misc_register(&net_misc))
		goto err_register;

	priv = netdev_priv(dev);
	init_timer(&priv->timer);
	setup_timer(&priv->timer, my_timer_func, (long)dev);
	mod_timer(&priv->timer, jiffies + msecs_to_jiffies(1000));

	pr_info("[my_netdev]\topened\n");
	return 0;

err_register:
err_atomic:
	atomic_dec(&md_use);
	return 0;
}


/**
 * Network device hook functions
 */
static struct net_device_ops my_netdev_ops = {
	.ndo_open = my_netdev_open,
	.ndo_stop = my_netdev_stop,
	.ndo_start_xmit = my_netdev_start_xmit,
};





/**
 * Module inicialization
 *
 */
static int pb173_init(void)
{
	int rv;

	pr_info("size: 0x%x\n", sizeof(struct net_device));
	my_device = alloc_etherdev(sizeof(struct my_netdev_struct));
	if (!my_device)
		return -ENOMEM;
	my_device->netdev_ops = &my_netdev_ops;
	random_ether_addr(my_device->dev_addr);
	rv = register_netdev(my_device);
	pr_info("rv: %d\n", rv);

	return 0;
}

/**
 * Module deinicialization
 */
static void pb173_exit(void)
{
	unregister_netdev(my_device);
	free_netdev(my_device);
}

module_init(pb173_init);
module_exit(pb173_exit);

MODULE_LICENSE("GPL");
