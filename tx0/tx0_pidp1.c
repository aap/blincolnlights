#include "common.h"
#include "panel_pidp1.h"
#include "tx0.h"
#include "args.h"

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

void
emu(TX0 *tx0, Panel *panel)
{
	int sw0, sw1, sw2;
	int psw2;
	int down;
	int l5, l6;

	sw0 = 0;
	sw1 = 0;
	sw2 = 0;

	pwrclr(tx0);

	// UT3 expects certain constants
	tx0->core[07773] = 1;
	tx0->core[07776] = 0630000;

	inittime();
	tx0->simtime = gettime();
	tx0->dpy_last = tx0->simtime;
	for(;;) {
		psw2 = sw2;
		sw0 = panel->sw0;
		sw1 = panel->sw1;
		sw2 = panel->sw2;
		down = sw2 & ~psw2;

		if(sw0 & SW_POWER) {
			// HACK: we don't have 18 bits on the PDP-1 panel
			// use lower sense switches for IR
			tx0->tbr = (sw0 & ADDRMASK) | (((sw2>>10)>>4) & 3)<<16;
			tx0->tac = sw1;

			tx0->sw_sup_alarm = 0;
			tx0->sw_sup_chime = 0;
			// no idea what these are
			tx0->sw_auto_restart = 0;
			tx0->sw_auto_readin = 0;
			tx0->sw_auto_test = 0;
			tx0->sw_stop_c0 = !!(sw2 & SW_SSTEP);
			tx0->sw_stop_c1 = !!(sw2 & SW_SINST);;
			tx0->sw_step = !!(sw2 & 02000);
			tx0->sw_repeat = !!(sw2 & 04000);
			tx0->sw_cm_sel = 0;
			tx0->sw_type_in = 0;
			tx0->btn_test = !!(down & KEY_START);
			tx0->btn_readin = !!(down & KEY_READIN);
			tx0->btn_stop = !!(down & KEY_STOP);
			tx0->btn_restart = !!(down & KEY_CONT);

			l5 = 0;
			if(tx0->ss) l5 |= L5_RUN;
			if(tx0->c) l5 |= L5_CYC;
//			if(tx0->t) l5 |= L5_DF1;
			if(tx0->t) l5 |= L5_HSC;	// no test FF
			if(tx0->lp&1) l5 |= L5_BC1;	// no lp FFs
			if(tx0->lp&2) l5 |= L5_BC2;
			if(tx0->aff) l5 |= L5_OV1;	// no alarm FF
			if(tx0->r) l5 |= L5_RIM;
//			if(tx0->sbm) l5 |= L5_SBM;
//			if(tx0->ext) l5 |= L5_EXT;
			if(!tx0->ios) l5 |= L5_IOH;
			if(tx0->pbs) l5 |= L5_IOC;	// no pbs FF
//			if(tx0->ios) l5 |= L5_IOS;
			l5 |= L5_PWR;
			if(tx0->sw_stop_c0) l5 |= L5_SSTEP;
			if(tx0->sw_stop_c1) l5 |= L5_SINST;

			panel->lights0 = PC;
			panel->lights1 = MAR;
			panel->lights2 = MBR;
			panel->lights3 = AC;
			panel->lights4 = LR;
			panel->lights5 = l5;
			panel->lights6 = (tx0->ir<<3)<<13 | ((sw2>>10)&077)<<6 | tx0->petr12<<2 | tx0->petr3<<1 | tx0->petr4;

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
			panel->lights3 = 0;
			panel->lights4 = 0;
			panel->lights5 = 0;
			panel->lights6 = 0;

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
