#include "common.h"
#include "panel_b18.h"
#include "tx0.h"
#include "args.h"

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

void
emu(TX0 *tx0, Panel *panel)
{
	int sw0, sw1;
	int psw1;
	int down;
	int sel1, sel2;
	int indicators;

	sw0 = 0;
	sw1 = 0;
	sel1 = 0;
	sel2 = 0;

	pwrclr(tx0);

	// UT3 expects certain constants
	tx0->core[07773] = 1;
	tx0->core[07776] = 0630000;

	inittime();
	tx0->simtime = gettime();
	tx0->dpy_last = tx0->simtime;
	for(;;) {
		psw1 = sw1;
		sw0 = panel->sw0;
		sw1 = panel->sw1;
		down = sw1 & ~psw1;

		if(sw1 & SW_POWER) {
			if(down) {
				if(down & KEY_SEL1)
					sel1 = (sel1+1 + 2*!!(sw1&KEY_MOD)) % 4;
				if(down & KEY_SEL2)
					sel2 = (sel2+1 + 2*!!(sw1&KEY_MOD)) % 4;
				if(down & KEY_LOAD1) TAC = sw0;
				if(down & KEY_LOAD2) TBR = sw0;
			}
			tx0->sw_sup_alarm = 0;
			tx0->sw_sup_chime = 0;
			// no idea what these are
			tx0->sw_auto_restart = 0;
			tx0->sw_auto_readin = 0;
			tx0->sw_auto_test = 0;
			tx0->sw_stop_c0 = !!(sw1 & SW_SSTEP);
			tx0->sw_stop_c1 = !!(sw1 & SW_SINST);;
			tx0->sw_step = !!(sw1 & SW_SPARE1);
			tx0->sw_repeat = !!(sw1 & SW_REPEAT);
			tx0->sw_cm_sel = 0;
			tx0->sw_type_in = 0;
			tx0->btn_test = !!(down & KEY_START);
			tx0->btn_readin = !!(down & KEY_READIN);
			tx0->btn_stop = !!(down & KEY_STOP);
			tx0->btn_restart = !!(down & KEY_CONT);

			indicators = 0;
			if(sw1 & KEY_SPARE) {
				indicators |= 0;
				indicators |= tx0->c<<8;
				indicators |= tx0->r<<7;
				indicators |= tx0->t<<6;
			} else {
				indicators |= tx0->ss<<9;
				indicators |= tx0->ios<<8;
				indicators |= tx0->pbs<<7;
				indicators |= 0;
			}
			indicators |= (0x8>>sel1) << 10;
			indicators |= (0x8>>sel2) << 14;
			indicators |= sw1 & 0x3F;
			switch(sel1) {
			case 0: panel->lights0 = AC; break;
			case 1: panel->lights0 = MAR | IR<<16; break;
			case 2: panel->lights0 = 0;
			case 3: panel->lights0 = TAC; break;
			}
			switch(sel2) {
			case 0: panel->lights1 = LR; break;
			case 1: panel->lights1 = PC; break;
			case 2: panel->lights1 = MBR; break;
			case 3: panel->lights1 = TBR; break;
			}
			panel->lights2 = indicators;

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

			panel->lights0 = 0;
			panel->lights1 = 0;
			panel->lights2 = 0;

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

int
main(int argc, char *argv[])
{
	Panel panel;
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

	initGPIO();
	memset(&panel, 0, sizeof(panel));
	memset(tx0, 0, sizeof(*tx0));

	tx0->dpy_fd = dial(host, port);
	if(tx0->dpy_fd < 0)
		printf("can't open display\n");
	nodelay(tx0->dpy_fd);

	pthread_create(&th, NULL, panelthread, &panel);

	tx0->r_fd = open("t/out.rim", O_RDONLY);
	tx0->p_fd = open("punch.out", O_CREAT|O_WRONLY|O_TRUNC, 0644);

	tx0->typ_fd = open("/tmp/fl", O_RDWR);
	if(tx0->typ_fd < 0)
		printf("can't open /tmp/fl\n");

	emu(tx0, &panel);
	return 0;	// can't happen
}
