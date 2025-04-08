#include "common.h"
#include "pdp1.h"
#include "args.h"

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/socket.h>

#include <signal.h>

typedef struct Panel Panel;
void updateswitches(PDP1 *pdp, Panel *panel);
void updatelights(PDP1 *pdp, Panel *panel);
void lightsoff(Panel *panel);
void lightson(Panel *panel);
Panel *getpanel(void);

#define Edge(sw) (pdp->sw && !prev_##sw)

void initmusic(void);
void stopmusic(void);
void svc_music(PDP1 *pdp);
int domusic = 0;

void
emu(PDP1 *pdp, Panel *panel)
{
	pdp->panel = panel;

	pwrclr(pdp);
if(domusic) initmusic();

	bool prev_start_sw;
	bool prev_stop_sw;
	bool prev_continue_sw;
	bool prev_examine_sw;
	bool prev_deposit_sw;
	bool prev_readin_sw;
	updateswitches(pdp, panel);

	inittime();
	pdp->simtime = gettime();
	pdp->dpy_last = pdp->simtime;
	for(;;) {
		prev_start_sw = pdp->start_sw;
		prev_stop_sw = pdp->stop_sw;
		prev_continue_sw = pdp->continue_sw;
		prev_examine_sw = pdp->examine_sw;
		prev_deposit_sw = pdp->deposit_sw;
		prev_readin_sw = pdp->readin_sw;
		updateswitches(pdp, panel);

		if(pdp->power_sw) {
			if(Edge(start_sw) || Edge(continue_sw) ||
			   Edge(examine_sw) || Edge(deposit_sw)) {
				spec(pdp);
				cycle(pdp);
			}
			if(Edge(stop_sw)) pdp->run_enable = 0;
			if(Edge(readin_sw)) start_readin(pdp);

			if(pdp->rim_cycle) readin1(pdp);
			if(pdp->rim_return && --pdp->rim_return == 0 &&
			   pdp->rim) {
				// restart after reader is done
				if(IR == 0 && !pdp->stop_sw)
					readin2(pdp);
				else if(IR_DIO) {
					cycle(pdp);
					pdp->rim_cycle = 1;
				}
			}

			if(pdp->run)	// not really correct
svc_music(pdp),
				cycle(pdp);
			else
stopmusic(),
				updatelights(pdp, panel);
			throttle(pdp);
			handleio(pdp);
			pdp->simtime += 5000;
		} else {
stopmusic();
			pwrclr(pdp);

if(pdp->start_sw && pdp->readin_sw) {
	lightson(panel);
	sleep(1);
	exit(100);
}
			lightsoff(panel);

			pdp->simtime = gettime();
		}
		agedisplay(pdp);
		cli(pdp);
	}
}

void
handlenetcmd(int fd, void *arg)
{
	PDP1 *pdp = (PDP1*)arg;
	char line[1024];
	int n;
	while(n = read(fd, line, sizeof(line)), n > 0) {
//		printf("got %d bytes\n", n);
		line[n] = 0;
//		printf("<%s>\n", line);
		char *r = handlecmd(pdp, line);
//printf("reply: <%s>\n", r);
		n = strlen(r);
		r[n] = '\n';
		r[n+1] = '\0';
		write(fd, r, strlen(r));
	}
//	printf("closing\n");
	close(fd);
}

void
handledpy(int fd, void *arg)
{
	PDP1 *pdp = (PDP1*)arg;
	if(pdp->dpy_fd >= 0) {
		close(fd);
	} else {
		pdp->dpy_fd = fd;
		nodelay(pdp->dpy_fd);
	}
}

void
handleptr(int fd, void *arg)
{
	PDP1 *pdp = (PDP1*)arg;
	close(pdp->r_fd);
	pdp->r_fd = fd;
	nodelay(pdp->r_fd);
}

void*
netthread(void *arg)
{
	struct PortHandler ports[] = {
		{ 1234, handlenetcmd },
		{ 3400, handledpy },
		{ 8101, handleptr },
	};
	serveN(ports, nelem(ports), arg);
}

char *argv0;
void
usage(void)
{
	fprintf(stderr, "usage: %s [-h host] [-p port]\n", argv0);
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
					mem[a++] = w;
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
			if(a != i){
				a = i;
				fprintf(f, "%06o:\n", a);
			}
			fprintf(f, "%06o\n", mem[a++]);
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
inthandler(int sig)
{
	exit(0);
}


int
main(int argc, char *argv[])
{
	PDP1 pdp1, *pdp = &pdp1;
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

	panel = getpanel();
	if(panel == nil) {
		fprintf(stderr, "can't find operator panel\n");
		return 1;
	}

	memp = pdp->core;
	memsz = MAXMEM;

	atexit(exitcleanup);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, inthandler);

	memset(pdp, 0, sizeof(*pdp));
	readmem("coremem", memp, memsz);

	startpolling();

	pdp->dpy_fd = -1;
//	pdp->dpy_fd = dial(host, port);
//	if(pdp->dpy_fd < 0)
//		printf("can't open display\n");
//	nodelay(pdp->dpy_fd);

	pthread_create(&th, NULL, netthread, pdp);

//	const char *tape = "maindec/maindec1_20.rim";
//	const char *tape = "tapes/circle.rim";
//	const char *tape = "tapes/munch.rim";
//	const char *tape = "tapes/minskytron.rim";
//	const char *tape = "tapes/spacewar2B_5.rim";
//	const char *tape = "tapes/ddt.rim";
	const char *tape = "tapes/dpys5.rim";
	pdp->muldiv_sw = 1;

	pdp->r_fd = open(tape, O_RDONLY);

	pdp->p_fd = open("punch.out", O_CREAT|O_WRONLY|O_TRUNC, 0644);

	pdp->typ_fd.id = -1;
	int fd[2];
	socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
	pdp->typ_fd.fd = fd[0];
//	pdp->typ_fd.fd = open("/tmp/typ", O_RDWR);
//	if(pdp->typ_fd.fd < 0)
//		printf("can't open /tmp/typ\n");
	waitfd(&pdp->typ_fd);
	typtelnet(fd[1]);

	emu(pdp, panel);
	return 0;	// can't happen
}
