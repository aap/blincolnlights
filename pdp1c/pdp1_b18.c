#include "common.h"
#include "panel_b18.h"
#include "pdp1.h"
#include "args.h"

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

void
emu(PDP1 *pdp, Panel *panel)
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

	pwrclr(pdp);

	inittime();
	pdp->simtime = gettime();
	pdp->dpy_last = pdp->simtime;
	for(;;) {
		psw1 = sw1;
		sw0 = panel->sw0;
		sw1 = panel->sw1;
		down = sw1 & ~psw1;

		if(down) {
			if(down & KEY_SEL1)
				sel1 = (sel1+1 + 2*!!(sw1&KEY_MOD)) % 4;
			if(down & KEY_SEL2)
				sel2 = (sel2+1 + 2*!!(sw1&KEY_MOD)) % 4;
			if(down & KEY_LOAD1) {
				if(sw1 & KEY_MOD)
					pdp->ss = sw0 & 077;
				else
					pdp->ta = sw0 & ADDRMASK;
			}
			if(down & KEY_LOAD2) pdp->tw = sw0;
		}

		if(sw1 & SW_POWER) {
			indicators = 0;
			if(sw1 & KEY_SPARE) {
				indicators |= pdp->ioc<<9;
				indicators |= pdp->ihs<<8;
				indicators |= pdp->ios<<7;
				indicators |= pdp->ioh<<6;
			} else {
				indicators |= pdp->run<<9;
				indicators |= pdp->cyc<<8;
				indicators |= pdp->df1<<7;
				indicators |= pdp->rim<<6;
			}
			indicators |= (0x8>>sel1) << 10;
			indicators |= (0x8>>sel2) << 14;
			indicators |= sw1 & 0x3F;
			switch(sel1) {
			case 0: panel->lights0 = AC; break;
			case 1: panel->lights0 = MA | IR<<12; break;
			case 2: panel->lights0 = pdp->pf<<6 | pdp->ss; break;
			case 3: panel->lights0 = pdp->ta; break;
			}
			switch(sel2) {
			case 0: panel->lights1 = IO; break;
			case 1: panel->lights1 = PC; break;
			case 2: panel->lights1 = MB; break;
			case 3: panel->lights1 = pdp->tw; break;
			}
			panel->lights2 = indicators;

			if(down & (KEY_START | KEY_CONT | KEY_EXAM | KEY_DEP)) {
				pdp->start_sw = !!(down & KEY_START);
				pdp->continue_sw = !!(down & KEY_CONT);
				pdp->examine_sw = !!(down & KEY_EXAM);
				pdp->deposit_sw = !!(down & KEY_DEP);
				spec(pdp);
				cycle(pdp);
			}
			if(down & KEY_STOP) pdp->run_enable = 0;
			if(down & KEY_READIN) start_readin(pdp);
			if(pdp->rim_cycle) readin1(pdp);
			if(pdp->rim_return && --pdp->rim_return == 0 &&
			   pdp->rim) {
				// restart after reader is done
				if(IR == 0 && !(sw1 & KEY_STOP))
					readin2(pdp);
				else if(IR_DIO) {
					cycle(pdp);
					pdp->rim_cycle = 1;
				}
			}
			pdp->single_cyc_sw = !!(sw1 & SW_SSTEP);
			pdp->single_inst_sw = !!(sw1 & SW_SINST);

			if(pdp->run)	// not really correct
				cycle(pdp);
			throttle(pdp);
			handleio(pdp);
		} else {
			pwrclr(pdp);

			panel->lights0 = 0;
			panel->lights1 = 0;
			panel->lights2 = 0;

			pdp->simtime = gettime();
		}
		agedisplay(pdp);
		cli(pdp);
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

	initGPIO();
	memset(&panel, 0, sizeof(panel));
	memset(pdp, 0, sizeof(*pdp));

	pdp->dpy_fd = dial(host, port);
	if(pdp->dpy_fd < 0)
		printf("can't open display\n");
	nodelay(pdp->dpy_fd);

	pthread_create(&th, NULL, panelthread, &panel);

//	pdp->tw = 0777777;
	pdp->tw = 0677721;	// minskytron
	pdp->ss = 060;

//	const char *tape = "../pdp1/maindec/maindec1_20.rim";
//	const char *tape = "../pdp1/tapes/circle.rim";
//	const char *tape = "../pdp1/tapes/munch.rim";
	const char *tape = "../pdp1/tapes/minskytron.rim";
//	const char *tape = "../pdp1/tapes/spacewar2B_5.rim";
//	const char *tape = "../pdp1/tapes/ddt.rim";

	pdp->r_fd = open(tape, O_RDONLY);
//	readrim(pdp);

	pdp->p_fd = open("punch.out", O_CREAT|O_WRONLY|O_TRUNC);

	pdp->typ_fd = open("/tmp/typ", O_RDWR);
	if(pdp->typ_fd < 0)
		printf("can't open /tmp/typ\n");

	emu(pdp, &panel);
	return 0;	// can't happen
}
