#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/mm.h>

#define OUR_PAGES 1
static void *mem_gfp;
static void *mem_vma;

static ssize_t my_read(struct file *filp, char __user *buf, size_t count,
		loff_t *off)
{
	static const char string[] = "to return\n";

	if (*off >= strlen(string) || !count)
		return 0;

	count = 1;

	if (put_user(string[*off], buf))
		return -EFAULT;

	*off += count;
	return count;
}

static int my_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{

	struct page *page;
	long offset;

	vmf->page = NULL;
	if (vmf->pgoff > 3)
		return VM_FAULT_SIGBUS;
	if (vmf->pgoff > 1) {
		offset = ((long)vmf->pgoff - 2) << PAGE_SHIFT;
		page = vmalloc_to_page(mem_vma + offset);
	} else {
		offset = (long)vmf->pgoff << PAGE_SHIFT;
		page = virt_to_page(mem_gfp + offset);
	}
	get_page(page);
	vmf->page = page;

	return 0;
}


static int my_mmap(struct file *filp, struct vm_area_struct *vma);

static const struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.read = my_read,
	.mmap = my_mmap,
};

static struct vm_operations_struct my_vm_ops = {
	.fault = my_fault,
};

static struct miscdevice my_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &my_fops,
	.name = "my_name",
};


static int my_mmap(struct file *filp, struct vm_area_struct *vma)
{
/*
	int rv;

	rv = -EINVAL;
	if (vma->vm_pgoff < 2){
		rv = remap_pfn_range(vma, vma->vm_start,
			virt_to_phys(mem_gfp)>>PAGE_SHIFT,
			OUR_PAGES<<PAGE_SHIFT, vma->vm_page_prot);
		pr_info("pfn range\n");
	}
	else if (vma->vm_pgoff < 4){
		pr_info("vma range %d\n", vma->vm_pgoff);
		rv = remap_vmalloc_range(vma, mem_vma, vma->vm_pgoff - 2);
	}
	return rv;
	*/
	vma->vm_ops = &my_vm_ops;
	vma->vm_private_data = NULL;
	return 0;
}

static int my_init(void)
{
	char *s1 = "Ahoj svete";
	char *s2 = "Hello world";
	/* ? */
	mem_gfp = (void *)__get_free_pages(GFP_KERNEL | __GFP_COMP, OUR_PAGES);
	if (mem_gfp == NULL)
		return -EIO;


	mem_vma = vmalloc_user(PAGE_SIZE * (1 << OUR_PAGES));

	if (mem_vma == NULL) {
		free_pages((long)mem_gfp, OUR_PAGES);
		return -EIO;
	}

	memset(mem_gfp, 0, PAGE_SIZE * (1<<OUR_PAGES));
	memcpy(mem_gfp, s1, strlen(s1));
	memcpy(mem_gfp + PAGE_SIZE, s2, strlen(s2));

	memcpy(mem_vma, s2, strlen(s2));
	memcpy(mem_vma + PAGE_SIZE, s1, strlen(s1));

	return misc_register(&my_misc);
}

static void my_exit(void)
{
	misc_deregister(&my_misc);
	free_pages((long)mem_gfp, OUR_PAGES);
	vfree(mem_vma);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
