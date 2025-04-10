#include "common.h"
#include "tx0.h"
#include "args.h"

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <signal.h>

typedef struct Panel Panel;
void updateswitches(TX0 *tx0, Panel *panel);
void updatelights(TX0 *tx0, Panel *panel);
void lightsoff(Panel *panel);
Panel *getpanel(void);

void
emu(TX0 *tx0, Panel *panel)
{
	pwrclr(tx0);

	// UT3 expects certain constants
	tx0->core[07773] = 1;
	tx0->core[07776] = 0630000;

	inittime();
	tx0->simtime = gettime();
	tx0->dpy_last = tx0->simtime;
	for(;;) {
		updateswitches(tx0, panel);

		if(tx0->sw_power) {
			updatelights(tx0, panel);

			if(tx0->btn_test) {
				tx0->c = 0;
				tx0->r = 0;
				tx0->t = 1;
				tx0->pbs = 1;
				tx0->ios = 1;
			}
			if(tx0->btn_readin) {
				tx0->c = 1;
				tx0->r = 1;
				tx0->t = 1;
				tx0->pbs = 1;
				tx0->ios = 1;
				tx0->ir = 2;	// trn
			}
			if(tx0->btn_stop) {
				tx0->pbs = 0;
			}
			if(tx0->btn_restart) {
				tx0->pbs = 1;
				tx0->ios = 1;
			}

			cycle(tx0);
			throttle(tx0);
			handleio(tx0);
			tx0->simtime += 6000;
		} else {
			pwrclr(tx0);

			lightsoff(panel);

			tx0->simtime = gettime();
		}
		agedisplay(tx0);
		cli(tx0);
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
sighandler(int sig)
{
	exit(0);
}

int
main(int argc, char *argv[])
{
	TX0 txzero, *tx0 = &txzero;
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
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	memset(tx0, 0, sizeof(*tx0));

	tx0->dpy_fd = dial(host, port);
	if(tx0->dpy_fd < 0)
		printf("can't open display\n");
	nodelay(tx0->dpy_fd);

	tx0->r_fd = open("t/out.rim", O_RDONLY);
	tx0->p_fd = open("punch.out", O_CREAT|O_WRONLY|O_TRUNC, 0644);

	tx0->typ_fd = open("/tmp/fl", O_RDWR);
	if(tx0->typ_fd < 0)
		printf("can't open /tmp/fl\n");

	emu(tx0, panel);
	return 0;	// can't happen
}
