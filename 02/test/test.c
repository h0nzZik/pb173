#include <unistd.h>
#include <stdio.h>
#include <sys/syscall.h>
int main(void)
{
	printf("= %ld\n", syscall(__NR_fork));
	return 0;
}
