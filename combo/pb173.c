#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#define COMBO_VENDOR	0x18ec
#define COMBO_DEVICE	0xc058

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
			pr_warn("[pb173]\tkmalloc() failed\n");
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

/* combo */
#define BAR0_INT_RAISED		0x0040
#define BAR0_INT_ENABLED	0x0044
#define BAR0_INT_TRIGGER	0x0048
#define BAR0_INT_ACK		0x004A

void combo_dump_registers(const void __iomem *bar0)
{
	int r, e, a;

	r = readl(bar0 + BAR0_INT_RAISED);
	e = readl(bar0 + BAR0_INT_TRIGGER);
	a = readl(bar0 + BAR0_INT_ACK);

	pr_info("%x, %x, %x\n", r, e, a);
}

/*
struct combo_data{
	void __iomem *bar0;
};
*/

static irqreturn_t combo_irq_handler (int irq, void *data, struct pt_regs *regs)
{
	pr_info("interrupt\n");
	return IRQ_HANDLED;
}




static int my_probe(struct pci_dev *dev, const struct pci_device_id *dev_id)
{
	int rv;
	void __iomem *addr;

	/* id, revision, build date */
	int idrev;
	int id, rev;
	int btime;
	int y,m,dd,hh,mm;


	pr_info("[pb173]\tnew device: %02x:%02x\n", dev_id->vendor, dev_id->device);
	pr_info("[pb173]\tbus no: %x, slot: %x, func: %x\n", dev->bus->number,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	if (pci_enable_device(dev) < 0) {
		pr_info("[pb173]\tcan't enable device\n");
		return -EIO;
	}

	rv = pci_request_region(dev, 0, "some_name");
	if (rv < 0) {
		pr_info("[pb173]\tfailed registering region\n");
		pci_disable_device(dev);
		return -EIO;
	}

	pr_info("bar0: %llx\n", (unsigned long long)pci_resource_start(dev, 0));



	addr = pci_ioremap_bar(dev, 0);
	pr_info("mapped to %p\n", addr);
	if (addr == NULL) {
		pci_release_region(dev, 0);
		pci_disable_device(dev);
		return -EIO;
	}
	pr_info("ok\n");
	pci_set_drvdata(dev, (void *)addr);


	/* read built time and print it */
	btime = readl(addr + 0x0004);

	mm = ((btime >>  0) & 0x0F) + 10 * ((btime >>  4) & 0x0F);
	hh = ((btime >>  8) & 0x0F) + 10 * ((btime >> 12) & 0x0F);
	dd = ((btime >> 16) & 0x0F) + 10 * ((btime >> 20) & 0x0F);
	m = (btime >> 24) & 0x0F;
	y = (btime >> 28) & 0x0F;

	pr_info("[[b173]\tbuilt: %d. of %d 200%d at %d:%d\n", dd, m, y, hh, m);

	/* read ID, revision & print it */
	idrev = readl(addr + 0x0000);

	id = (idrev >> 16) & 0xFFFF;
	rev = idrev & 0xFFFF;

	pr_info("[pb173]\tid: %x, rev: %x\n", id, rev);


	combo_dump_registers(addr);

	/* irq handler */
	rv = request_irq(dev->irq, combo_irq_handler, IRQF_SHARED, "combo_driver", addr);

	return rv;
}

static void my_remove(struct pci_dev *dev)
{
	void __iomem *addr;
	pr_info("[pb173]\tremoving %x:%x\n", dev->vendor, dev->device);
	pr_info("[pb173]\tbus no: %x, slot: %x, func: %x\n", dev->bus->number,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));

	addr = (void __iomem *) pci_get_drvdata(dev);
	free_irq(dev->irq, addr);
	iounmap(addr);
	pci_release_region(dev, 0);
	pci_disable_device(dev);
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

	save_pci_devices();
	return 0;
}

static void my_exit(void)
{
	pci_unregister_driver(&combo_driver);

	compare_new_devices();
	free_missing_devices();
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");