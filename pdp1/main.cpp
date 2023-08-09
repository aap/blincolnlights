#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <pigpio.h>
#include <verilated.h>
#include <termios.h>


#include "args.h"
#include "common.h"
#include "Vpdp1panel.h"

// hooked up to a PDP-1-style pty
// see mkptytyp
struct Typewriter
{
	int in, out;
	int dly;        // hack
};
void
puttyp(Typewriter *t, int c)
{
	char cc;
	cc = c & 0177;
	write(t->out, &cc, 1);
}
int
gettyp(Typewriter *t)
{
	char c;
	read(t->in, &c, 1);
	return c;
}


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
				} else if(a < 010000) {
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
	Typewriter ty;
	pthread_t th;
	const char *disphost;
	int port;
	FILE *ptr;

	disphost = "localhost";
	port = 3400;
	ARGBEGIN {
	case 'd':
		disphost = EARGF(usage());
		break;
	case 'p':
		port = atoi(EARGF(usage()));
		break;
	default:
		usage();
	} ARGEND;

	initGPIO();

	memset(&panel, 0, sizeof(panel));


	memset(&ty, 0, sizeof(ty));
	ty.in = open("/tmp/ty", O_RDWR);
	if(ty.in < 0)
		printf("can't open /tmp/ty\n");
	ty.out = ty.in;

	int disp = dial(disphost, port);
	if(disp < 0)
		printf("can't open display\n");
	nodelay(disp);


	int rdly = 10;
//	ptr = fopen("tapes/ddt.rim", "rb");
//	ptr = fopen("paper_tapes/minskytron.rim", "rb");
	ptr = fopen("paper_tapes/spacewar2B.rim", "rb");
//	ptr = fopen("paper_tapes/snowflake.rim", "rb");
//	ptr = fopen("maindec/maindec1.rim", "rb");
//	ptr = fopen("maindec/maindec1_20.rim", "rb");
//	ptr = fopen("tapes/test1.rim", "rb");


	Vpdp1panel *dev = new Vpdp1panel;

//	readmemf("out.mem", dev);

	pthread_create(&th, NULL, panelthread, &panel);

	int disbuf[64];
	int ndis = 0;

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
	dev->key = 0;

int cmddly = 0;
int cmd = -1; //open("/dev/rfcomm0", O_RDWR);

	static struct timespec start, now, diff;
	clock_gettime(CLOCK_REALTIME, &start);
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

		if(dev->clk) {
			if(cmddly == 0) {
				if(hasinput(cmd)) {
					char line[1024], *p;
					int i, n;
					n = read(cmd, line, sizeof(line));
					if(n <= 0) {
						close(cmd);
						cmd = -1;
						printf("disconnect\n");
					}
					if(n > 0 && n < sizeof(line)) {
						line[n] = '\0';
						if(p = strchr(line, '\r'), p) *p = '\0';
						if(p = strchr(line, '\n'), p) *p = '\0';

						printf("cmd: <%s>\n", line);
						write(cmd, "received\n", 9);
					}
				}
				if(cmd == -1) {
					cmd = open("/dev/rfcomm0", O_RDWR);
					if(cmd >= 0) {
						struct termios tio;
						tcgetattr(cmd, &tio);
						cfmakeraw(&tio);
						tcsetattr(cmd, TCSAFLUSH, &tio);

						printf("connect!\n");
					}
				}
				cmddly = 10000;
			}
			cmddly--;

			/* typewriter read */
			if(ty.dly == 0) {
				if(dev->key)
					dev->key = 0;
				else if(hasinput(ty.in)) {
					int c = gettyp(&ty);
					dev->key = c | 0100;
				}
				ty.dly = 10000;
			} else
				ty.dly--;

			/* typewriter print */
			if(dev->typeout)
				puttyp(&ty, (dev->tbb<<6) | dev->tb);

			/* read */
			dev->hole &= 0377;
			if(dev->rcl) {
				if(rdly == 0) {
					int c;
					c = getc(ptr);
					if(c < 0)
						rdly = 1000;
					else {
						dev->hole = 0400 | c&0377;
printf("hole %o\n", c&0377);
						rdly = 10;
					}
				} else
					rdly--;
			} else
				rdly = 10;

			/* punch */
			if(dev->punch)
				printf("punch %o\n", dev->pb);

			/* display */
			if(dev->dpy_intensify)
			if(disp >= 0) {
				int x = dev->dbx;
				int y = dev->dby;
				if(x & 01000) x++;
				if(y & 01000) y++;
				x = ((x+01000)&01777);
				y = ((y+01000)&01777);
				int cmd = x | (y<<10) | (7<<20);

				// batch up a few points for the display
				disbuf[ndis++] = cmd;
				if(ndis == nelem(disbuf)) {
					write(disp, disbuf, sizeof(disbuf));
					ndis = 0;
				}
			}

if(dev->ncycle == 10000) {
	clock_gettime(CLOCK_REALTIME, &now);
	diff.tv_sec = now.tv_sec - start.tv_sec;
	diff.tv_nsec = now.tv_nsec - start.tv_nsec;
	if(diff.tv_nsec < 0){
		diff.tv_nsec += 1000000000;
		diff.tv_sec -= 1;
	}
	float cyctime = (double)diff.tv_nsec / dev->ncycle;
//printf("%f\n", cyctime);
	start = now;
	dev->ncycle = 0;
}

		}
	}

	delete dev;
	return 0;
}
