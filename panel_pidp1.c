#include "common.h"
#include "gpio.h"
#include "panel_pidp1.h"

#include <signal.h>
#include <unistd.h>
#include <pthread.h>


int ADDR[] = {4, 17, 27, 22};
int COLUMNS[] = {26, 19, 13, 6, 5, 11, 9, 10,
	18, 23, 24, 25, 8, 7, 12, 16, 20, 21};
u32 addrmsk, colmsk;

void
inRow(void)
{
	for(int i = 0; i < nelem(COLUMNS); i++)
		INP_GPIO(COLUMNS[i]);
}

void
outRow(void)
{
	for(int i = 0; i < nelem(COLUMNS); i++)
		OUT_GPIO(COLUMNS[i]);
}

// unused here, only for simple LED driver
void
setRow(int l)
{
	int bit = 0;
	for(int i = 0; i < nelem(COLUMNS); i++)
		bit |= ((l>>i)&1)<<COLUMNS[i];
	gpio->clr0 = bit & colmsk;
	gpio->set0 = ~bit & colmsk;
}

void
setAddr(int a)
{
	int bit = 0;
	for(int i = 0; i < nelem(ADDR); i++)
		bit |= ((a>>i)&1)<<ADDR[i];
	gpio->set0 = bit & addrmsk;
	gpio->clr0 = ~bit & addrmsk;
}


typedef struct PanelLamps PanelLamps;
struct PanelLamps
{
	u8 lamps[7][18];
	Panel *p;
};


#include "pwmtab.inc"

void
lightRow(int a, u8 *l)
{
	setAddr(15);

	for(int phase = 0; phase < 31; phase++) {
		int bit = 0;
		for(int i = 0; i < nelem(COLUMNS); i++)
			bit |= pwmtable[l[i]][phase]<<COLUMNS[i];
		gpio->clr0 = bit & colmsk;
		gpio->set0 = ~bit & colmsk;
		if(phase == 0)
			setAddr(a);
		usleep(10);
	}

	setAddr(15);
	nsleep(50);
}

u32
readRow(int a)
{
	setAddr(a);
	usleep(1000);
	int sw = 0777777;
	for(int i = 0; i < nelem(COLUMNS); i++)
		if(gpio->lev0 & (1<<COLUMNS[i]))
			sw &= ~(1<<i);
	return sw;
}

void
setLights(PanelLamps *p)
{
	outRow();
	lightRow(0, p->lamps[0]);
	lightRow(1, p->lamps[1]);
	lightRow(2, p->lamps[2]);
	lightRow(3, p->lamps[3]);
	lightRow(4, p->lamps[4]);
	lightRow(5, p->lamps[5]);
	lightRow(6, p->lamps[6]);
}

void
readSwitches(Panel *p)
{
	static u32 cycle = 0;

	inRow();
	int i = (cycle++) % 3;
	(&p->sw0)[i] = readRow(8+i);
}

void
countRow(u32 *on, u32 bits)
{
	for(int i = 0; i < 18; i++)
		on[i] += (bits>>i) & 1;
}

// not ideal yet at all, but better than nothing
void*
lampthread(void *arg)
{
	const int nsamples = 100;
	PanelLamps *p = (PanelLamps*)arg;
	Panel cur;
	u32 on[7][18];
	float intensity[7][18];
	memset(intensity, 0, sizeof(intensity));
	for(;;) {
		memset(on, 0, sizeof(on));
		for(int i = 0; i < nsamples; i++) {
			cur = *p->p;
			countRow(on[0], cur.lights0);
			countRow(on[1], cur.lights1);
			countRow(on[2], cur.lights2);
			countRow(on[3], cur.lights3);
			countRow(on[4], cur.lights4);
			countRow(on[5], cur.lights5);
			countRow(on[6], cur.lights6);
		}

		for(int i = 0; i < 7; i++)
		for(int j = 0; j < 18; j++) {
			float t = (float)on[i][j]/nsamples;
			float diff = t-intensity[i][j];
			if(diff >= 0.0f)
				intensity[i][j] += diff*0.012f;
			else
				intensity[i][j] *= 0.995f;
			p->lamps[i][j] = intensity[i][j]*31 + 0.5f;
		}
	}
}

volatile int doexit;
void*
panelthread(void *arg)
{
	pthread_t th;
	PanelLamps panel;

	memset(&panel, 0, sizeof(panel));
	panel.p = (Panel*)arg;
	pthread_create(&th, nil, lampthread, &panel);

	while(!doexit) {
		setLights(&panel);
		readSwitches(panel.p);
	}
	setAddr(15);
	inRow();
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
	opengpio();
	signal(SIGINT, interrupt);

	for(int i = 0; i < nelem(ADDR); i++)
		addrmsk |= 1<<ADDR[i];
	for(int i = 0; i < nelem(COLUMNS); i++)
		colmsk |= 1<<COLUMNS[i];

	gpio->clr0 = ~0;
	for(int i = 0; i < nelem(ADDR); i++)
		OUT_GPIO(ADDR[i]);
	inRow();

	// only pi4
	gpio->pup_pdn_cntrl_reg0 = 0;
	gpio->pup_pdn_cntrl_reg1 = 0;
	for(int i = 0; i < nelem(COLUMNS); i++)
		(&gpio->pup_pdn_cntrl_reg0)[COLUMNS[i]/16] |= 1 << 2*(COLUMNS[i]%16);

	// not pi4
	gpio->pud = 2;	// pull up
	int p = 0;
	for(int i = 0; i < nelem(COLUMNS); i++)
		p |= 1<<COLUMNS[i];
	usleep(1);
	gpio->pudclk0 = p;
	usleep(1);
	gpio->pud = 0;
	gpio->pudclk0 = 0;
}
