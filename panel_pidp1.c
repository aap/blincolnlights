#include "common.h"
#include "panel_pidp1.h"

#include <signal.h>
#include <pigpio.h>
#include <unistd.h>

int ADDR[] = {4, 17, 27, 22};
int COLUMNS[] = {26, 19, 13, 6, 5, 11, 9, 10,
	18, 23, 24, 25, 8, 7, 12, 16, 20, 21};


void
disablePin(int pin)
{
	gpioSetMode(pin, PI_INPUT);
	gpioSetPullUpDown(pin, PI_PUD_OFF);
}

void
enablePin(int pin)
{
	gpioSetMode(pin, PI_INPUT);
	gpioSetPullUpDown(pin, PI_PUD_UP);
}

void
outputPin(int pin, int val)
{
	gpioSetMode(pin, PI_OUTPUT);
	gpioWrite(pin, val);
}

void
delay(int ms)
{
	usleep(ms * 1000);
}



void
disableColumns(void)
{
	int i;
	for(i = 0; i < nelem(COLUMNS); i++)
		disablePin(COLUMNS[i]);
}

void
enableColumns(void)
{
	int i;
	for(i = 0; i < nelem(COLUMNS); i++)
		enablePin(COLUMNS[i]);
}


void
setAddr(int a)
{
	outputPin(ADDR[0], !!(a&1));
	outputPin(ADDR[1], !!(a&2));
	outputPin(ADDR[2], !!(a&4));
	outputPin(ADDR[3], !!(a&8));
}

int dly = 1000;
int swdly = 1000;


void
setRow(int a, int l)
{
	// avoid ghosting
	for(int i = 0; i < 18; i++)
		outputPin(COLUMNS[i], 1);
	setAddr(a);
	for(int i = 0; i < 18; i++)
		outputPin(COLUMNS[i], (~l>>i)&1);
	usleep(dly);
}

int
readRow(int a)
{
	int sw;
	setAddr(a);
	usleep(swdly);
	sw = 0;
	for(int i = 0; i < 18; i++)
		sw |= !gpioRead(COLUMNS[i]) << i;
	return sw;
}

void
setLights(Panel *p)
{
	setRow(0, p->lights0);
	setRow(1, p->lights1);
	setRow(2, p->lights2);
	setRow(3, p->lights3);
	setRow(4, p->lights4);
	setRow(5, p->lights5);
	setRow(6, p->lights6);
}

void
readSwitches(Panel *p)
{
	int i, sw;
	static unsigned int cycle = 0;

	enableColumns();

	switch((cycle++) % 3) {
	case 0:
		p->sw0 = readRow(8);
		break;
	case 1:
		p->sw1 = readRow(9);
		break;
	case 2:
		p->sw2 = readRow(10);
		break;
	}
}

int doexit;

void*
panelthread(void *arg)
{
	Panel *p = (Panel*)arg;
	while(!doexit) {
		setLights(p);
		readSwitches(p);
	}

	gpioTerminate();
	exit(0);
}


void
interrupt(int sig)
{
	doexit = 1;
}

void
initGPIO(void)
{
        if(gpioInitialise() < 0)
                exit(1);
	signal(SIGINT, interrupt);
	disableColumns();
	setAddr(15);
}
