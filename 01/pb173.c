#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
/*
 *	Proc je tu ten wrapper pro kmalloc?
 *	Misto funkce pb173_foo je rozpoznana predchozi exportovana funkce
 *	(pb173_exported, pokud neexistuje, tak kmalloc)
 */
/********************************/
/*	<some useless stuff>	*/
int gl;
int pb173_exported(int bar)
{
	return 2*bar;
}
EXPORT_SYMBOL(pb173_exported);

int pb173_foo(int bar)
{
	return ++bar;
}
static int bar(int foo)
{
	return --foo;
}
/*	</some useless stuff>	*/
/********************************/
static int my_init(void)
{
	gl = pb173_foo(bar(0xDEADBEEF));
	pr_info("hello world\n");
	return 0;
}

static void logthissymbol(void *p)
{
	pr_info("0x%p => %pF\n", p, p);
	return;
}

static void my_exit(void)
{
	char *mem;

	logthissymbol(&gl);
	logthissymbol(pb173_foo);
	logthissymbol(pb173_exported);
	logthissymbol(my_init);

	logthissymbol(kmalloc);
	logthissymbol(printk);
	logthissymbol((void *)&jiffies);

	pr_info("\n***stack variable***\n");
	logthissymbol(&mem);
	pr_info("***string constant***\n");
	logthissymbol("muj_uzasny_retezec");
	pr_info("***return adress***\n");
	logthissymbol(__builtin_return_address(0));

	mem = kmalloc(100, GFP_KERNEL);
	if (mem) {
		pr_info("***kmalloc()ed memory***\n");
		logthissymbol(mem);
		strcpy(mem, "bye");
		pr_info("%s\n", mem);
		kfree(mem);
	}
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
