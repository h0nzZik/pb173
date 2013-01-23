#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/bitmap.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>


#include "combo.h"

/* some old stuff we can hide */
#if 0

struct dev_list {
	struct list_head head;
	struct pci_dev *dev;

};

static LIST_HEAD(pcidev_list);

static void save_pci_devices(void)
{
	struct pci_dev *dev;
	struct dev_list *entry;


	dev = NULL;
	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		pr_info("\n[pb173]\tdevice (vendor:device:irq) %x:%x:%x\n",
				dev->vendor, dev->device, dev->irq);

		entry = kmalloc(sizeof(*entry), GFP_KERNEL);
		if (entry) {
			/* acquire & add */
			entry->dev = pci_dev_get(dev);
			list_add(&entry->head, &pcidev_list);
			
		} else {
			pr_info("[pb173]\tkmalloc() failed\n");
		}
	}
}

static struct dev_list *was_there(struct pci_dev *dev)
{
	struct list_head *pos;
	struct list_head *tmp;

	struct dev_list *found;
	struct dev_list *curr;

	found = NULL;

	list_for_each_safe(pos, tmp, &pcidev_list) {
		curr = list_entry(pos, struct dev_list, head);
		if (curr->dev->bus->number != dev->bus->number)
			continue;
		if (PCI_SLOT(curr->dev->devfn) != PCI_SLOT(dev->devfn))
			continue;
		if (PCI_FUNC(curr->dev->devfn) != PCI_FUNC(dev->devfn))
			continue;
		/* found it */
		found = curr;
	}

	return found;
}

static void free_missing_devices(void)
{
	struct list_head *pos;
	struct list_head *tmp;

	struct dev_list *curr;

	list_for_each_safe(pos, tmp, &pcidev_list) {
		curr = list_entry(pos, struct dev_list, head);
		pr_info("[pv173]\tmissing pci device %02x:%02x\n", curr->dev->vendor, curr->dev->device);
		list_del_init(&curr->head);
		pci_dev_put(curr->dev);
		kfree(curr);
	}
}


static void compare_new_devices(void)
{
	struct pci_dev *dev;
	struct dev_list *found;
	dev = NULL;

	while (NULL != (dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev))) {
		found = was_there(dev);
		if (found) {
			list_del_init(&found->head);
			pci_dev_put(found->dev);
			kfree(found);
		} else {
			pr_info("[pb173]\tnew pci device: %02x:%02x\n", dev->vendor, dev->device);
		}
	}
}

#endif
static inline void combo_int_set_enabled(void __iomem *bar0, unsigned enabled)
{
	writel(enabled, bar0 + BAR0_INT_ENABLED);
}

static inline unsigned combo_int_get_enabled(void __iomem *bar0)
{
	return readl(bar0 + BAR0_INT_ENABLED);
}

static inline void combo_int_enable(void __iomem *bar0, unsigned int_no)
{
	unsigned enabled;

	enabled = int_no | combo_int_get_enabled(bar0);
	combo_int_set_enabled(bar0, enabled);
}

static inline void combo_int_disable(void __iomem *bar0, unsigned int_no)
{
	unsigned enabled;

	enabled = ~int_no & combo_int_get_enabled(bar0);
	combo_int_set_enabled(bar0, enabled);
}

static inline void combo_int_trigger(void __iomem *bar0, unsigned int_no)
{
	writel(int_no, bar0 + BAR0_INT_TRIGGER);
}

static inline void combo_int_clear(void __iomem *bar0, unsigned int_no)
{
	writel(int_no, bar0 + BAR0_INT_ACK);
}


static inline unsigned combo_int_get_raised(void __iomem *bar0)
{
	return readl(bar0 + BAR0_INT_RAISED);
}
/* DMA handling functions */

static void combo_dma_int_ack(void __iomem *bar0)
{
	long data;
	data = readl(bar0 + BAR0_DMA_CMD);
	data |= BAR0_DMA_CMD_INT_ACK;
	writel(data, bar0 + BAR0_DMA_CMD);
}


static void combo_dma_transfer_start(void __iomem *bar0, int use_ints)
{
	long data;

	data = BAR0_DMA_CMD_RUN | readl(bar0 + BAR0_DMA_CMD);
	if (!use_ints)
		data |= BAR0_DMA_CMD_INT_NO;
	writel(data, bar0 + BAR0_DMA_CMD);
}

static void combo_dma_transfer_wait(void __iomem *bar0)
{
	long data;
	int i;

	i=0;
	do {
		msleep(1);
		data = readl(bar0 + BAR0_DMA_CMD); 
		i++;
	} while (data & BAR0_DMA_CMD_RUN && i < 1000);

	pr_info("[pb173]\ttransfer: %d miliseconds\n", i);
}

static void combo_dma_transfer_setup(void __iomem *bar0, 
		int src_bus, int dest_bus, dma_addr_t src, dma_addr_t dest, long bytes)
{
	long data;
	data = readl(bar0 + BAR0_DMA_CMD);

	data = 0;
	data |= BAR0_DMA_CMD_SRC(src_bus);
	data |= BAR0_DMA_CMD_DEST(dest_bus);

	writel(data, bar0 + BAR0_DMA_CMD);

	writel(dest, bar0 + BAR0_DMA_DEST);
	writel(src,  bar0 + BAR0_DMA_SRC);
	writel(bytes,bar0 + BAR0_DMA_NBYTES);
}




static void test_transfer_noint(struct combo_data *combo)
{
	static char *test_string = "Combo____";

	/* clear buffer */
	memset(combo->dma_virt, 0, combo->dma_nb);
	strcpy(combo->dma_virt, test_string);

	/* transfer string to memory inside PPC uc */
	combo_dma_transfer_setup(combo->bar0,
			COMBO_DMA_PCI,
			COMBO_DMA_PPC,
			combo->dma_phys,
			COMBO_DMA_PPC_BUFFER,
			strlen(test_string));

	combo_dma_transfer_start(combo->bar0, 0);
	combo_dma_transfer_wait(combo->bar0);

	/* transfer it back */
	combo_dma_transfer_setup(combo->bar0,
			COMBO_DMA_PPC,
			COMBO_DMA_PCI,
			COMBO_DMA_PPC_BUFFER,
			combo->dma_phys + strlen(test_string) + 1,
			10);
	combo_dma_transfer_start(combo->bar0, 0);
	combo_dma_transfer_wait(combo->bar0);

	combo->dma_virt[strlen(test_string) + 1 + 10] = 0;
	pr_info("received '%s'\n", combo->dma_virt+strlen(test_string) + 1);
}


static void test_transfer_int_in(struct combo_data *combo)
{
	pr_info("transfering in\n");

	/* transfer string from memory inside PPC uc */
	combo_dma_transfer_setup(combo->bar0,
			COMBO_DMA_PPC,
			COMBO_DMA_PCI,
			COMBO_DMA_PPC_BUFFER,
			combo->dma_phys + 30, 
			10);

	combo->way = 2; /* transfering in */
	combo_dma_transfer_start(combo->bar0, 1);
}

static void test_transfer_int_out(struct combo_data *combo)
{
	static char *test_string = "Interrupts_";

	pr_info("transfering out\n");
	memset(combo->dma_virt, 0, combo->dma_nb);
	strcpy(combo->dma_virt, test_string);

	/* transfer string to memory inside PPC uc */
	combo_dma_transfer_setup(combo->bar0,
			COMBO_DMA_PCI,
			COMBO_DMA_PPC,
			combo->dma_phys,
			COMBO_DMA_PPC_BUFFER,
			strlen(test_string));

	combo->way = 1;	/* transfering out */
	combo_dma_transfer_start(combo->bar0, 1);
}


/*	some interrupt functions */

static void combo_int_dump(const void __iomem *bar0)
{
	int r, e;

	r = readl(bar0 + BAR0_INT_RAISED);
	e = readl(bar0 + BAR0_INT_ENABLED);

	pr_info("[pb071]\tints raised: 0x%x, enabled: 0x%x\n", r, e);
}

#if 0
static void combo_print_build_info(void __iomem *bar0)
{
	/* id, revision, build date */
	int idrev;
	int id, rev;
	int btime;
	int y,m,dd,hh,mm;

	/* read built time and print it */
	btime = readl(bar0 + BAR0_BRIDGE_BUILD_DATE);

	mm = ((btime >>  0) & 0x0F) + 10 * ((btime >>  4) & 0x0F);
	hh = ((btime >>  8) & 0x0F) + 10 * ((btime >> 12) & 0x0F);
	dd = ((btime >> 16) & 0x0F) + 10 * ((btime >> 20) & 0x0F);
	m = (btime >> 24) & 0x0F;
	y = (btime >> 28) & 0x0F;

	pr_info("[[b173]\tbuilt: %d. of %d 200%d at %d:%d\n", dd, m, y, hh, m);

	/* read ID, revision & print it */
	idrev = readl(bar0 + BAR0_BRIDGE_ID_REV);

	id = (idrev >> 16) & 0xFFFF;
	rev = idrev & 0xFFFF;

	pr_info("[pb173]\tid: %x, rev: %x\n", id, rev);
}
#endif

/* interval timer */
static void combo_timer_function(unsigned long combo_data)
{
	struct combo_data *data = (void *)combo_data;
	pr_info("[pb173]\tgoing to trigger int 0x1000 (at %lu pm ;))\n", jiffies);
	mod_timer(&data->timer, jiffies + msecs_to_jiffies(1000));
	combo_int_trigger(data->bar0, 0x1000);
}


static void combo_interrupt_tasklet(unsigned long data)
{
	struct combo_data *combo;
	combo = (struct combo_data *)data;
	combo->dma_virt[40]='\0';
	pr_info("[tasklet]\treceived: '%s'\n", combo->dma_virt + 30);
}

static void combo_handle_dma_interrupt(struct combo_data *combo)
{
	combo_dma_int_ack(combo->bar0);

	if (combo->way == 1) {
	/* transfer out finished */
		test_transfer_int_in(combo);
	} else if (combo->way == 2) {
	/* finished transfer in */
		tasklet_schedule(&combo->tasklet);
		combo->way = 0;
	} else {
		pr_info("[pb173]\tunknown transfer way\n");
	}
}

static void combo_do_interrupt(int irq, struct combo_data *data, struct pt_regs *regs, int int_no)
{
	combo_int_dump(data->bar0);
	pr_info("[pb173]\tinterrupt 0x%x on irq %d, jiffies == %lu\n", int_no, irq, jiffies);

	switch (int_no) {
		case COMBO_INT_DMA:
			pr_info("interrupt from DMA\n");
			combo_handle_dma_interrupt(data);
			break;
		default:
			break;
	}
}

static irqreturn_t combo_irq_handler (int irq, void *combo_data, struct pt_regs *regs)
{
	struct combo_data *data;
	unsigned ints;
	int which;
	data = combo_data;


	ints = combo_int_get_raised(data->bar0);

	if (ints == 0) {
		pr_info("[pb173]\tinterrupt.. but wait. what interrupt? %lu\n", jiffies); // 
		return IRQ_NONE;
	}
	which = find_first_bit((unsigned long *)&ints, sizeof(ints)*8);	// je toto ok? s tim pretypovanim..

	combo_do_interrupt(irq, data, regs, which);
	combo_int_clear(data->bar0, 1<<which);


	return IRQ_HANDLED;
}

/* /dev device */
static atomic_t miscdev_in_use = ATOMIC_INIT(0);
static struct combo_data *miscdev_combo_data;

static int combo_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int rv;

	/* hope that ioremap() returns physical address */
	rv = remap_pfn_range(vma, vma->vm_start,
			virt_to_phys(miscdev_combo_data->dma_virt) >> PAGE_SHIFT,
			PAGE_SIZE, vma->vm_page_prot);
	if (rv)
		pr_info("[pb173]\tmmap(): remap_pfn_range() failed\n");
	return rv;
}


static struct file_operations combo_fops = {
	.owner = THIS_MODULE,
	.mmap = combo_mmap,
};


static struct miscdevice combo_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "combo_dma",
	.fops = &combo_fops,
	.nodename = "combo_dma_name",
	.mode = 0666,
};



static int my_probe(struct pci_dev *dev, const struct pci_device_id *dev_id)
{
	int rv;
	void __iomem *bar0;
	struct combo_data *data;

	pr_info("[pb173]\t***\tnew device: %02x:%02x\t***\n", dev_id->vendor, dev_id->device);
	pr_info("[pb173]\tbus no: %x, slot: %x, func: %x\n", dev->bus->number,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));


	/* in use? */
	if (atomic_inc_return(&miscdev_in_use) != 1) {
		atomic_dec(&miscdev_in_use);
		pr_info("[pb173]\tOK, OK. There are too much combo's\n");
		return -EINVAL;
	}

	if (pci_enable_device(dev) < 0) {
		pr_info("[pb173]\tcan't enable device\n");
		goto error_enable;
	}

	rv = pci_request_region(dev, 0, "some_name");
	if (rv < 0) {
		pr_info("[pb173]\tfailed registering region\n");
		goto error_request;
	}

	bar0 = pci_ioremap_bar(dev, 0);

	if (bar0 == NULL) {
		pr_info("[pb173]\tcan't map bar0\n");
		goto error_map;
	}

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		pr_info("[pb173]\tkmalloc() failed\n");
		goto error_kmalloc;
	}

	data->bar0 = bar0;
	pci_set_drvdata(dev, data);

	/* irq handler */
	rv = request_irq(dev->irq, (irq_handler_t)combo_irq_handler, 
			IRQF_SHARED, "combo_driver", (void *)data);
	if (rv) {
		pr_info("can't register irq handler: %d\n", rv);
		goto error_req_irq;
	}

	/* set timer */
	setup_timer(&data->timer, combo_timer_function, (long)data);
	data->timer.data = (long)data;
	mod_timer(&data->timer, jiffies + msecs_to_jiffies(10000));

	combo_int_enable(bar0, 0x1000);
	combo_int_enable(bar0, 0x0100);
	combo_int_dump(bar0);



	/* setup DMA */
	rv = pci_set_dma_mask(dev, DMA_BIT_MASK(32));
	if (rv)
		goto error_dma;

	pci_set_master(dev);

	data->dma_nb = 100;
	data->dma_virt = dma_alloc_coherent(&dev->dev, data->dma_nb, &data->dma_phys, GFP_KERNEL);
	if (!data->dma_virt)
		goto error_dma;


	
	/* register misc device */
	miscdev_combo_data = data;
	rv = misc_register(&combo_misc);
	if (rv)
		goto error_misc;


	data->way = 0; /* we don't use interrupts */

	/* tasklet */
	tasklet_init(&data->tasklet, combo_interrupt_tasklet, (unsigned long)data);
	
	/* test noint */
	test_transfer_noint(data);


	/* test ints */
	test_transfer_int_out(data);
	return 0;

error_misc:
	dma_free_coherent(&dev->dev, 100, data->dma_virt, data->dma_phys);
error_dma:
	combo_int_disable(data->bar0, 0x1000);
	combo_int_disable(data->bar0, 0x0100);
	del_timer_sync(&data->timer);
error_req_irq:
	kfree(data);
error_kmalloc:
	iounmap(bar0);
error_map:
	pci_release_region(dev, 0);
error_request:
	pci_disable_device(dev);
error_enable:

	miscdev_combo_data = NULL;
	atomic_dec(&miscdev_in_use);
	return -EIO;
}

static void my_remove(struct pci_dev *dev)
{
	struct combo_data *data;

	/* deregister misc device */
	miscdev_combo_data = NULL;
	misc_deregister(&combo_misc);
	atomic_dec(&miscdev_in_use);

	data = pci_get_drvdata(dev);
	tasklet_kill(&data->tasklet);
	dma_free_coherent(&dev->dev, 100, data->dma_virt, data->dma_phys);
	combo_int_disable(data->bar0, 0x1000);
	combo_int_disable(data->bar0, 0x0100);
	del_timer_sync(&data->timer);
	pr_info("[pb173]\tremoving %x:%x\n", dev->vendor, dev->device);
	pr_info("[pb173]\tbus no: %x, slot: %x, func: %x\n", dev->bus->number,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));

	free_irq(dev->irq, data);
	iounmap(data->bar0);
	data->bar0 = NULL;
	kfree(data);
	pci_release_region(dev, 0);
	pci_disable_device(dev);
	pr_info("[pb173]\t*** removed ***\n");
}

static struct pci_device_id id_table[] = {
	{
		PCI_DEVICE(COMBO_VENDOR, COMBO_DEVICE),
		.driver_data=(kernel_ulong_t)NULL,
	}
};

static struct pci_driver combo_driver = {
	.name = "my_driver",
	.probe = my_probe,
	.remove = my_remove,
	.id_table = id_table,
};

static int my_init(void)
{
	int rv;


	rv = pci_register_driver(&combo_driver);
	if (rv < 0) {
		return -EIO;
	}

//	save_pci_devices();
	return 0;
}

static void my_exit(void)
{
	pci_unregister_driver(&combo_driver);

//	compare_new_devices();
//	free_missing_devices();
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
