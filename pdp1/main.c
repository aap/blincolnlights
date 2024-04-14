#include "common.h"
#include "pdp1.h"
#include "args.h"

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <signal.h>

typedef struct Panel Panel;
void updateswitches(PDP1 *pdp, Panel *panel);
void updatelights(PDP1 *pdp, Panel *panel);
void lightsoff(Panel *panel);
Panel *getpanel(void);

#define Edge(sw) (pdp->sw && !prev_##sw)

void
emu(PDP1 *pdp, Panel *panel)
{
	pdp->panel = panel;

	pwrclr(pdp);

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
				cycle(pdp);
			else
				updatelights(pdp, panel);
			throttle(pdp);
			handleio(pdp);
			pdp->simtime += 5000;
		} else {
			pwrclr(pdp);

			lightsoff(panel);

			pdp->simtime = gettime();
		}
		agedisplay(pdp);
		cli(pdp);
	}
}

void*
netcmd(void *arg)
{
	PDP1 *pdp = (PDP1*)arg;
	int fd;
	char line[1024];
	int n;
	for(;;) {
		fd = serve1(1234);
//		printf("connect %d\n", fd);
		while(n = read(fd, line, sizeof(line)), n > 0) {
//			printf("got %d bytes\n", n);
			line[n] = 0;
//			printf("<%s>\n", line);
			char *r = handlecmd(pdp, line);
			write(fd, r, strlen(r));
		}
//		printf("closing\n");
		close(fd);
	}
}

char *argv0;
void
usage(void)
{
	fprintf(stderr, "usage: %s [-h host] [-p port]\n", argv0);
	exit(1);
}

static Panel *panel;
void
exitcleanup(void)
{
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

	atexit(exitcleanup);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, inthandler);

	memset(pdp, 0, sizeof(*pdp));

	pdp->dpy_fd = dial(host, port);
	if(pdp->dpy_fd < 0)
		printf("can't open display\n");
	nodelay(pdp->dpy_fd);

	pthread_create(&th, NULL, netcmd, pdp);

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

	pdp->typ_fd = open("/tmp/typ", O_RDWR);
	if(pdp->typ_fd < 0)
		printf("can't open /tmp/typ\n");

	emu(pdp, panel);
	return 0;	// can't happen
}
