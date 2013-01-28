#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

static unsigned long PAGE_SIZE;



int main(int argc, char **argv)
{
	char *map1, *map2;
	int fd;

	//return my_zero();

	PAGE_SIZE = sysconf(_SC_PAGE_SIZE);
	if ((long)PAGE_SIZE < 0)
		err(1, "sysconf(_SC_PAGE_SIZE)");

	if (argc < 2)
		errx(2, "usage: %s <dev_node>", argv[0]);

	fd = open(argv[1], O_RDWR);
	if (fd < 0)
		err(3, "cannot open '%s'", argv[1]);

	map1 = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	if (map1 == MAP_FAILED)
		err(4, "mmap1");


	char mem[PAGE_SIZE];


	memcpy(mem, map1, PAGE_SIZE);
	unsigned int i;
	for (i=0; i<PAGE_SIZE; i++) {
		printf("%c %x\n", isalpha(mem[i])?mem[i]:'-', mem[i]);
	}
	munmap(map1, PAGE_SIZE);
	close(fd);

	return 0;
}
