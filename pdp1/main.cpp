#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <pigpio.h>
#include <verilated.h>

#include "args.h"
#include "common.h"
#include "Vpdp1panel.h"

void
readmem(FILE *f, Vpdp1panel *dev)
{
	char buf[100], *s;
	int a, w;
	int i;

	a = 0;
	while(s = fgets(buf, 100, f)) {
		while(*s) {
			if(*s == ';')
				break;
			else if('0' <= *s && *s <= '7') {
				w = strtol(s, &s, 8);
				if(*s == ':') {
					a = w;
					s++;
				} else if(a < 0200000) {
					dev->pdp1panel__DOT__pdp1__DOT__memory[a++] = w & 0777777;
				}
			}else
				s++;
		}
	}
}
void
readmemf(const char *path, Vpdp1panel *dev)
{
	FILE *f;
	if(f = fopen(path, "r")) {
		readmem(f, dev);
		fclose(f);
	}
}

char *argv0;
void
usage(void)
{
	fprintf(stderr, "usage: %s\n", argv0);
	exit(1);
}

int
main(int argc, char *argv[])
{
	Panel panel;
	pthread_t th;

	ARGBEGIN {
	default:
		usage();
	} ARGEND;

	initGPIO();

	memset(&panel, 0, sizeof(panel));



	Vpdp1panel *dev = new Vpdp1panel;

	readmemf("out.mem", dev);

	pthread_create(&th, NULL, panelthread, &panel);

	dev->clk = 0;
	dev->reset = 1;
	for(int i = 0; i < 20; i++) {
		dev->clk ^= 1;
		dev->eval();
	}
	dev->pdp1panel__DOT__ac = rand() & 0777777;
	dev->pdp1panel__DOT__io = rand() & 0777777;
	dev->pdp1panel__DOT__mb = rand() & 0777777;
	dev->pdp1panel__DOT__ma = rand() & 07777;
	dev->pdp1panel__DOT__pc = rand() & 07777;
	dev->pdp1panel__DOT__ir = rand() & 077;

	for(;;) {
		dev->clk ^= 1;
		dev->sw0 = panel.sw0;
		dev->sw1 = panel.sw1;
		if(dev->sw_power) {
			panel.lights0 = dev->light0;
			panel.lights1 = dev->light1;
			panel.lights2 = dev->light2;
			dev->reset = 0;
		} else {
			dev->pdp1panel__DOT__ac = rand() & 0777777;
			dev->pdp1panel__DOT__io = rand() & 0777777;
			dev->pdp1panel__DOT__mb = rand() & 0777777;
			dev->pdp1panel__DOT__ma = rand() & 07777;
			dev->pdp1panel__DOT__pc = rand() & 07777;
			dev->pdp1panel__DOT__ir = rand() & 077;
			dev->reset = 1;
			panel.lights0 = 0;
			panel.lights1 = 0;
			panel.lights2 = 0;
		}
		dev->eval();
	}

	delete dev;
	return 0;
}
