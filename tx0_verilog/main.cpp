#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <pigpio.h>
#include <verilated.h>

#include "args.h"
#include "common.h"
#include "panel_b18.h"
#include "Vtx0panel.h"


// hooked up to a flexowriter-style pty
// see mkptyfl
struct Flexowriter
{
	int in, out;
	int dly;	// hack
};
void
putfl(Flexowriter *fl, int c) 
{
	char cc;
	cc = c & 077;
	write(fl->out, &cc, 1);
}
int
getfl(Flexowriter *fl)  
{
	char c;
	read(fl->in, &c, 1); 
	return c;
}


void
readmem(FILE *f, Vtx0panel *dev)
{
	char buf[100], *s;
	int a, w, par;
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
					par = ~0;
					for(i = 0; i < 18; i++)
						par ^= w<<(i+1);
					dev->tx0panel__DOT__tx0__DOT__core[a++] = (w & 0777777) | (par&01000000);
				}
			}else
				s++;
		}
	}
}
void
readmemf(const char *path, Vtx0panel *dev)
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
	fprintf(stderr, "usage: %s [-h host] [-p port]\n", argv0);
	exit(1);
}

int
main(int argc, char *argv[])
{
	Panel panel;
	Flexowriter fl;
	pthread_t th;
	const char *host;
	int port;

	host = "localhost";
	port = 3400;
	ARGBEGIN {
	case 'h':
		host = EARGF(usage());
		break;
	case 'p':
		port = atoi(EARGF(usage()));
		break;
	default:
		usage();
	} ARGEND;

	initGPIO();

	memset(&panel, 0, sizeof(panel));


	memset(&fl, 0, sizeof(fl));
	fl.in = open("/tmp/fl", O_RDWR);
	if(fl.in < 0)
		printf("can't open /tmp/fl\n");
	fl.out = fl.in;

	int disp = dial(host, port);
	if(disp < 0)
		printf("can't open display\n");
	nodelay(disp);


	Vtx0panel *dev = new Vtx0panel;

	readmemf("ut3.mem", dev);
	readmemf("circle.mem", dev);

	pthread_create(&th, NULL, panelthread, &panel);

	int fl_to_lr = 0;
	int fl_complete = 0;
	int display_complete = 0;
	int disbuf[64];
	int ndis = 0;

	dev->clk = 0;
	dev->reset = 1;
	for(int i = 0; i < 20; i++) {
		dev->clk ^= 1;
		dev->eval();
	}
	dev->tx0panel__DOT__ac = rand() & 0777777;
	dev->tx0panel__DOT__lr = rand() & 0777777;
	dev->tx0panel__DOT__mbr = rand() & 0777777;
	dev->tx0panel__DOT__mar = rand() & 0177777;
	dev->tx0panel__DOT__pc = rand() & 0177777;
	dev->tx0panel__DOT__ir = rand() & 3;
	dev->flexo_complete = 0;
	dev->flexo_to_lr = 0;

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
			dev->tx0panel__DOT__ac = rand() & 0777777;
			dev->tx0panel__DOT__lr = rand() & 0777777;
			dev->tx0panel__DOT__mbr = rand() & 0777777;
			dev->tx0panel__DOT__mar = rand() & 0177777;
			dev->tx0panel__DOT__pc = rand() & 0177777;
			dev->tx0panel__DOT__ir = rand() & 3;
			dev->reset = 1;
			panel.lights0 = 0;
			panel.lights1 = 0;
			panel.lights2 = 0;
		}
		dev->eval();


		/* flexo read */
		if(fl.dly == 0) {
			if(hasinput(fl.in)) {
				int c = getfl(&fl);
				fl_to_lr = 20;
				dev->flexo_in = c&077;
			}
			fl.dly = 10000;
		} else
			fl.dly--;
		if(fl_to_lr) fl_to_lr--;
		dev->flexo_to_lr = fl_to_lr != 0;


		/* flexo print */
		if(dev->flexo_start_print)
			fl_complete = 100;
		if(fl_complete == 50)
			putfl(&fl, dev->flexo_out);
		if(fl_complete) fl_complete--;
		dev->flexo_complete = fl_complete != 0 && fl_complete < 50;


		/* display */
		if(dev->display_start) {
			if(disp >= 0) {
				int x = dev->display_x;
				int y = dev->display_y;
				if(x & 0400) x++;
				if(y & 0400) y++;
				x = ((x+0400)&0777)<<1;
				y = ((y+0400)&0777)<<1;
				int cmd = x | (y<<10) | (7<<20);

				// batch up a few points for the display
				disbuf[ndis++] = cmd;
				if(ndis == nelem(disbuf)) {
					write(disp, disbuf, sizeof(disbuf));
					ndis = 0;
				}
			}
			display_complete = 10;
		}
		if(display_complete) display_complete--;
		dev->display_complete = display_complete != 0;
	}

	delete dev;
	return 0;
}
