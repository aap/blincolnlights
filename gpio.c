#include "common.h"
#include "gpio.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

volatile GPIO *gpio;

void
opengpio(void)
{
	int fd;

	fd = open("/dev/gpiomem", O_RDWR);
	if(fd < 0) {
		fprintf(stderr, "no gpio\n");
		exit(1);
	}

	gpio = (GPIO*)mmap(0, 4096, PROT_READ+PROT_WRITE, MAP_SHARED, fd, 0);
}


