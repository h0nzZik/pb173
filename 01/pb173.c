#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/io.h>
#include <linux/mm.h>

static int my_init(void)
{
	u32 *virt;
	void *map;
	phys_addr_t phys;
	struct page *sp;
	char buff[5];

	virt = ioremap_cache(0xdafe9000, 0x38);

	if (virt) {
		pr_info("signature: 0x%x\nlen: 0x%x\n", virt[0], virt[1]);
		memcpy(buff, virt, 4);
		buff[4] = '\0';
		pr_info("signature: %s\n", buff);
		iounmap(virt);
	}

	/*  */

	virt = (u32 *)__get_free_page(GFP_KERNEL);
	if (virt) {
		strcpy((char *)virt, "Ahoj svete\n");
		phys = virt_to_phys(virt);
		sp = virt_to_page(virt);
		SetPageReserved(sp);
		map = ioremap(phys, PAGE_SIZE);

		pr_info("virt:\t%p\n"
			"phys:\t%p\n"
			"sp:\t%p\n"
			"map:\t%p\n"
			"pfn:\t%lx\n", virt, (void *)phys, sp, map, page_to_pfn(sp));

		strcpy(map, "Skakal pes pres oves");
		pr_info("*virt == \"%s\"\n", (char *)virt);

		iounmap(map);
		ClearPageReserved(sp);
		free_page((unsigned long)virt);
	}

	return 0;
}

static void my_exit(void)
{
	pr_info("sizeof(struct page) == %d\n", sizeof(struct page));
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
