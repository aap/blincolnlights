#include "common.h"
#include "panel_pidp1.h"
#include "pdp1.h"
#include "args.h"

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

void
emu(PDP1 *pdp, Panel *panel)
{
	int sw0, sw1, sw2;
	int psw2;
	int down;
	int l5, l6;

	sw0 = 0;
	sw1 = 0;
	sw2 = 0;

	pwrclr(pdp);

	inittime();
	pdp->simtime = gettime();
	pdp->dpy_last = pdp->simtime;
	for(;;) {
		psw2 = sw2;
		sw0 = panel->sw0;
		sw1 = panel->sw1;
		sw2 = panel->sw2;
		down = sw2 & ~psw2;

		if(sw0 & SW_POWER) {
			// TODO: SW_EXTEND
			// TODO: MA EXT
			pdp->ta = sw0 & 07777;
			pdp->tw = sw1;
			pdp->ss = (sw2>>10) & 077;
			pdp->single_cyc_sw = !!(sw2 & SW_SSTEP);
			pdp->single_inst_sw = !!(sw2 & SW_SINST);

			l5 = 0;
			if(pdp->run) l5 |= L5_RUN;
			if(pdp->cyc) l5 |= L5_CYC;
			if(pdp->df1) l5 |= L5_DF1;
//			if(pdp->hsc) l5 |= L5_HSC;
			if(pdp->bc1) l5 |= L5_BC1;
			if(pdp->bc2) l5 |= L5_BC2;
			if(pdp->ov1) l5 |= L5_OV1;
			if(pdp->rim) l5 |= L5_RIM;
			if(pdp->sbm) l5 |= L5_SBM;
//			if(pdp->ext) l5 |= L5_EXT;
			if(pdp->ioh) l5 |= L5_IOH;
			if(pdp->ioc) l5 |= L5_IOC;
			if(pdp->ios) l5 |= L5_IOS;
			l5 |= L5_PWR;
			if(pdp->single_cyc_sw) l5 |= L5_SSTEP;
			if(pdp->single_inst_sw) l5 |= L5_SINST;

			panel->lights0 = PC;
			panel->lights1 = MA;
			panel->lights2 = MB;
			panel->lights3 = AC;
			panel->lights4 = IO;
			panel->lights5 = l5;
			panel->lights6 = pdp->ir<<13 | pdp->ss<<6 | pdp->pf;


			if(down & (KEY_START | KEY_START_UP | KEY_CONT | KEY_EXAM | KEY_DEP)) {
				pdp->sbm_start_sw = !!(down & KEY_START_UP);
				pdp->start_sw = !!(down & (KEY_START|KEY_START_UP));
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
				if(IR == 0 && !(sw2 & KEY_STOP))
					readin2(pdp);
				else if(IR_DIO) {
					cycle(pdp);
					pdp->rim_cycle = 1;
				}
			}

			if(pdp->run)	// not really correct
				cycle(pdp);
			throttle(pdp);
			handleio(pdp);
			pdp->simtime += 5000;
		} else {
			pwrclr(pdp);

			panel->lights0 = 0;
			panel->lights1 = 0;
			panel->lights2 = 0;
			panel->lights3 = 0;
			panel->lights4 = 0;
			panel->lights5 = 0;
			panel->lights6 = 0;

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

//	const char *tape = "maindec/maindec1_20.rim";
//	const char *tape = "tapes/circle.rim";
//	const char *tape = "tapes/munch.rim";
	const char *tape = "tapes/minskytron.rim";
//	const char *tape = "tapes/spacewar2B_5.rim";
//	const char *tape = "tapes/ddt.rim";

	pdp->r_fd = open(tape, O_RDONLY);

	pdp->p_fd = open("punch.out", O_CREAT|O_WRONLY|O_TRUNC, 0644);

	pdp->typ_fd = open("/tmp/typ", O_RDWR);
	if(pdp->typ_fd < 0)
		printf("can't open /tmp/typ\n");

	emu(pdp, &panel);
	return 0;	// can't happen
}
