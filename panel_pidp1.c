#include "common.h"
#include "panel_pidp1.h"

#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>


typedef struct GPIO GPIO;
struct GPIO
{
	u32 fsel0;	// 00
	u32 fsel1;
	u32 fsel2;
	u32 fsel3;
	u32 fsel4;	// 10
	u32 fsel5;
	u32 res__18;
	u32 set0;
	u32 set1;	// 20
	u32 res__24;
	u32 clr0;
	u32 clr1;
	u32 res__30;	// 30
	u32 lev0;
	u32 lev1;
	u32 res__3c;
	u32 eds0;	// 40
	u32 eds1;
	u32 res__48;
	u32 ren0;
	u32 ren1;	// 50
	u32 res__54;
	u32 fen0;
	u32 fen1;
	u32 res__60;	// 60
	u32 hen0;
	u32 hen1;
	u32 res__6c;
	u32 len0;	// 70
	u32 len1;
	u32 res__78;
	u32 aren0;
	u32 aren1;	// 80
	u32 res__84;
	u32 afen0;
	u32 afen1;
	u32 res__90;	// 90

	// not PI 4
	u32 pud;
	u32 pudclk0;
	u32 pudclk1;
	u32 res__a0;	// a0
	u32 space[16];

	// PI 4 only
	u32 pup_pdn_cntrl_reg0;	// e4
	u32 pup_pdn_cntrl_reg1;
	u32 pup_pdn_cntrl_reg2;
	u32 pup_pdn_cntrl_reg3;
};
volatile GPIO *gpio;

// 10 pin settings per FSEL register
#define INP_GPIO(pin) (&gpio->fsel0)[(pin)/10] &= ~(7<<(((pin)%10)*3))
#define OUT_GPIO(pin) (&gpio->fsel0)[(pin)/10] |=  (1<<(((pin)%10)*3))

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


int ADDR[] = {4, 17, 27, 22};
int COLUMNS[] = {26, 19, 13, 6, 5, 11, 9, 10,
	18, 23, 24, 25, 8, 7, 12, 16, 20, 21};
u32 addrmsk, colmsk;

#define SET_GPIO(pin, val) if(val) gpio->set0 = 1<<pin; else gpio->clr0 = 1<<pin;

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

int doexit;
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
