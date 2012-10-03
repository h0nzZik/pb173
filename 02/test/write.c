#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>

char *string="Ahoj svete\n";
int main(void)
{
	syscall(__NR_write, 1, string, strlen(string));
	return 0;
}
