#include "common.h"
#include "pdp5.h"
#include "args.h"

#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/socket.h>

#include <signal.h>

typedef struct Panel Panel;
void updateswitches(PDP5 *pdp, Panel *panel);
void updatelights(PDP5 *pdp, Panel *panel);
void lightsoff(Panel *panel);
void lightson(Panel *panel);
Panel *getpanel(void);


void
emu(PDP5 *pdp, Panel *panel)
{
	pdp->panel = panel;

	pwrclr(pdp);
	updateswitches(pdp, panel);

	inittime();
	pdp->simtime = gettime();
	for(;;) {
		updateswitches(pdp, panel);

		if(pdp->sw_power) {
			tick(pdp);
			updatelights(pdp, panel);
			throttle(pdp);
			handleio(pdp);
			pdp->simtime += 1000;
		} else {
			pwrclr(pdp);
			lightsoff(panel);

			pdp->simtime = gettime();
		}
	}
}

char *argv0;
void
usage(void)
{
	fprintf(stderr, "usage: %s\n", argv0);
	exit(1);
}

void
readmem(const char *file, Word *mem, Word size)
{
	FILE *f;
	char buf[100], *s;
	Word a;
	Word w;
	if(f = fopen(file, "r"), f == nil)
		return;
	a = 0;
	while(s = fgets(buf, 100, f)){
		while(*s){
			if(*s == ';')
				break;
			else if('0' <= *s && *s <= '7'){
				w = strtol(s, &s, 8);
				if(*s == ':'){
					a = w;
					s++;
				}else if(a < size)
					mem[a++] = w & 07777;
				else
					fprintf(stderr, "Address out of range: %o\n", a++);
			}else
				s++;
		}
	}
	fclose(f);
}

void
dumpmem(const char *file, Word *mem, Word size)
{
	FILE *f;
	Word i, a;

	if(f = fopen("coremem", "w"), f == nil)
		return;

	a = 0;
	for(i = 0; i < size; i++)
		if(mem[i] != 0){
//			if(a != i){
			if(1){
				a = i;
				fprintf(f, "%06o:\n", a);
			}
			fprintf(f, "%06o\n", mem[a++]);
		}


	fclose(f);
}

void
loadrim(const char *path, Word *mem, Word size)
{
	FILE *f;
	int c;
	Word w, a;

	if(f = fopen(path, "rb"), f == nil) {
		fprintf(stderr, "couldn't open file %s\n", path);
		return;
	}

	a = 0;

	for(;;){
		while(c = getc(f), c != EOF && c & 0200);   // skip leaders
		w = c<<6 | getc(f);     // get word
		if(w & 010000)
			a = w & WD;
		else if (a < size) {
			mem[a] = w & WD;
//printf("%o/ %o\n", a, w);
			if(a == 0)
				break;
			a = 0;
		}
	}

	fclose(f);
}


// a bit ugly...
static Panel *panel;
static Word *memp;
static int memsz;
void
exitcleanup(void)
{
	dumpmem("coremem", memp, memsz);
	lightsoff(panel);
}

void
sighandler(int sig)
{
	exit(0);
}

void
blinky(PDP5 *pdp)
{
	/* mike hill's tiny blinky */
	int a = 07772;
	pdp->core[a++] = 07012;
	pdp->core[a++] = 02771;
	pdp->core[a++] = 05373;
	pdp->core[a++] = 07020;
	pdp->core[a++] = 04371;
}

int
main(int argc, char *argv[])
{
	PDP5 pdp5, *pdp = &pdp5;
	TTY tty;
	pthread_t th;

	argv0 = argv[0];

	panel = getpanel();
	if(panel == nil) {
		fprintf(stderr, "can't find operator panel\n");
		return 1;
	}

	memp = pdp->core;
	memsz = MAXMEM;

	srand(time(nil));
	atexit(exitcleanup);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	memset(pdp, 0, sizeof(*pdp));
	pdpinit(pdp);
	attach_tty(pdp, &tty);

	readmem("coremem", memp, memsz);

	if(argc > 1) 
		loadrim(argv[1], memp, memsz);
	else {
		blinky(pdp);
		printf("start blinky at 7772\n");
	}

	startpolling();

	int fd[2];
	socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
	tty.fd.fd = fd[0];
        waitfd(&tty.fd);
	ttytelnet(1550, fd[1]);

	emu(pdp, panel);
	return 0;	// can't happen
}
