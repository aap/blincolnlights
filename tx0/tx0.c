#include "common.h"
#include "tx0.h"
#include <unistd.h>
#include <fcntl.h>

#define B0 0400000
#define B1 0200000
#define B2 0100000
#define B3 0040000
#define B4 0020000
#define B5 0010000
#define B6 0004000
#define B7 0002000
#define B8 0001000
#define B9 0000400
#define B10 0000200
#define B11 0000100
#define B12 0000040
#define B13 0000020
#define B14 0000010
#define B15 0000004
#define B16 0000002
#define B17 0000001

#define US(us) ((us)*1000 - 1)
#define RDLY US(4000)		// 250/s
#define PDLY US(15873)		// 63/s just a guess
#define TDLY US(25000)		// just a guess

void
pwrclr(TX0 *tx0)
{
	IR = rand() & 03;
	PC = rand() & ADDRMASK;
	MAR = rand() & ADDRMASK;
	MBR = rand() & WORDMASK;
	AC = rand() & WORDMASK;
	LR = rand() & WORDMASK;

	tx0->c = 0;
	tx0->r = 0;
	tx0->t = 0;
	tx0->par = 0;
	tx0->ss = 0;
	tx0->pbs = 0;
	tx0->ios = 0;
	tx0->ch = 0;
	tx0->aff = 0;
	tx0->lp = 0;
	tx0->mrffu = 0;
	tx0->mrffv = 0;
	tx0->miff = 0;

	tx0->dpy_time = NEVER;

	tx0->petr12 = 0;
	tx0->petr3 = 0;
	tx0->petr4 = 0;
	tx0->r_time = NEVER;

	tx0->fl_time = NEVER;
	tx0->tyi_wait = 0;
}

static void
petr(TX0 *tx0, int p12)
{
	tx0->petr12 = p12;
	tx0->r_time = tx0->simtime + RDLY;
}

static void
cyclestandby(TX0 *tx0)
{
	// TP1

	// TP2

	// TP3
	// I think PETR4 is PETR3 synchronized
	if(tx0->petr3)
		tx0->petr4 = 1;

	// TP4
	if(tx0->petr4)
		AC >>= 1;

	// TP5

	// TP6

	// TP7

	// TP8
	if(tx0->petr4) {
		tx0->petr3 = 0;
		tx0->petr4 = 0;
		if(tx0->petr_holes & 001) AC |= 0400000;
		if(tx0->petr_holes & 002) AC |= 0040000;
		if(tx0->petr_holes & 004) AC |= 0004000;
		if(tx0->petr_holes & 010) AC |= 0000400;
		if(tx0->petr_holes & 020) AC |= 0000040;
		if(tx0->petr_holes & 040) AC |= 0000004;
		if(tx0->petr12 == 3) {
			tx0->ios = 1;
			tx0->r_time = NEVER;
		}
		tx0->petr12 = (tx0->petr12+1) & 3;
	} else {
		if(tx0->ios)
			tx0->ss = tx0->pbs;
		else if(!tx0->pbs)
			tx0->ss = 0;
	}
}

static void
halt(TX0 *tx0)
{
	tx0->pbs = 0;
	tx0->ch = 1;
}

static void
readmem(TX0 *tx0)
{
	// TODO: TSS, LR
	MBR |= tx0->core[MAR];
}

static void
correctpar(TX0 *tx0)
{
	// we don't care about that right now
}

static void
checkpar(TX0 *tx0)
{
	// we don't care about that right now
}

static void
cycle0(TX0 *tx0)
{
	// TP1
	tx0->aff = 0;
	MBR = 0;
	if(!tx0->t) {
		tx0->mrffv = 1;
		tx0->mrffu = 1;
	}

	// TP2
	if(!tx0->t)
		tx0->mrffv = 0;
	else if(tx0->r)
		MBR |= AC;
	else
		MBR |= TBR;

	// TP3
	if(!tx0->t)
		readmem(tx0);
	else
		correctpar(tx0);
	if(tx0->mrffu)
		tx0->core[MAR] = 0;

	// TP4
	if(!tx0->t)
		tx0->miff = 1;
	PC = 0;

	// TP5
	if(tx0->miff)
		tx0->core[MAR] |= MBR;
	IR = MBR>>16;
	tx0->mrffu = 0;

	// TP6
	PC = (MAR+1) & ADDRMASK;
	if(tx0->r) {
		if(AD)
			halt(tx0);
		if(AD || OP)
			IR ^= 3;
	}
	if(tx0->sw_stop_c0)
		halt(tx0);
	checkpar(tx0);

	// TP7
	MAR = MBR & ADDRMASK;
	tx0->miff = 0;

	// TP8
	if(!tx0->aff && OP) {
		if(MAR & 0100000)
			AC &= 0000777;
		if(MAR & 0040000)
			AC &= 0777000;
		if((MAR & 0030000) == 0020000) {
			tx0->ios = 0;	
			tx0->ss = 0;	
		}
		switch((MAR>>9) & 7) {
		case 0:	break;
		case 1:	// read line
			petr(tx0, 3);
			break;
		case 2:
			tx0->dpy_time = tx0->simtime + US(50);
			break;
		case 3:	// read 3 lines
			petr(tx0, 1);
			break;
		case 4:	// start print
			// no idea about timing
			tx0->fl_punch = 0;
			tx0->fl_time = tx0->simtime + TDLY;
			break;
		case 5:	// spare
			break;
		case 6:	// start punch
			tx0->fl_punch = 1;
			tx0->fl_hole7 = 0;
			// no idea about timing
			tx0->fl_time = tx0->simtime + PDLY;
			break;
		case 7:	// start punch
			tx0->fl_punch = 1;
			tx0->fl_hole7 = 1;
			// no idea about timing
			tx0->fl_time = tx0->simtime + PDLY;
			break;
		}
	}
	if(tx0->t) {
		if(ST) {
			AC = 0;
			if(tx0->r) {
				tx0->ios = 0;	
				tx0->ss = 0;	
				petr(tx0, 1);
			}
		}
	} else {
		if(!(AC & 0400000))
			tx0->c = 1;
	}
	if(TN) {
		tx0->t = 0;
		tx0->r = 0;
	} else
		tx0->c = 1;
	if(!tx0->pbs)
		tx0->ss = 0;
}

static void
cycle1(TX0 *tx0)
{
	// TP1
	tx0->aff = 0;
	MBR = 0;
	if(tx0->t && !tx0->r && ST)
		AC |= TAC;
	if(ST || AD) {
		tx0->mrffv = 1;
		tx0->mrffu = 1;
	}
	if(OP) {
		if((MAR & 0104) == 0004)
			AC |= TAC;
		if((MAR & 0104) == 0100)
// set or jam?
			AC = (tx0->lp<<16) | AC&0177777;
	}

	// TP2
	if(ST || AD)
		tx0->mrffv = 0;
	if(ST)
		MBR |= AC;
	if(OP) {
		if((MAR & 3) == 3)
			MBR |= TBR;
		if((MAR & 3) == 1)
			MBR |= AC;
		if(MAR & 040)
			AC ^= 0777777;
	}

	// TP3
	if(AD)
		readmem(tx0);
	else
		correctpar(tx0);
	if(tx0->mrffu)
		tx0->core[MAR] = 0;
	if(OP) {
// TODO: can we swap?
		if((MAR & 3) == 2)
			MBR |= LR;
		if((MAR & 0300) == 0200)
			LR = MBR;
	}

	// TP4
	if(ST | AD)
		tx0->miff = 1;
	if(AD || OP && MAR & 020)
		AC ^= MBR;
	if(OP && MAR & 0400) {
		int in = MAR&0200 ? AC&1 : (AC>>17)&1;
		AC = AC>>1 | in<<17;
	}
	if(tx0->t)
		PC = 0;

	// TP5
	if(tx0->miff)
		tx0->core[MAR] |= MBR;
	tx0->mrffu = 0;

	// TP6
	if(tx0->t) {
		PC = (MAR+1) & ADDRMASK;
		if(!tx0->r && !tx0->sw_repeat)
			halt(tx0);
	}
	if(tx0->sw_stop_c1)
		halt(tx0);
	if(OP && (MAR & 030000) == 030000)
		halt(tx0);
	checkpar(tx0);

	// TP7
	if(AD || OP && MAR & 010) {
		AC += (~AC & MBR)<<1;
		if(AC & 01000000) AC += 1;
		AC &= WORDMASK;
	}
	if(!tx0->aff && (!tx0->t || tx0->sw_step))
		MAR = PC;
	tx0->miff = 0;

	// TP8
	if(!tx0->t)
		tx0->c = 0;
	else if(tx0->r){
		AC = 0;
		tx0->ios = 0;	
		tx0->ss = 0;	
		tx0->c = 0;
		petr(tx0, 1);
	}
	if(OP && tx0->sw_repeat)
		tx0->c = 0;
	if(!tx0->pbs)
		tx0->ss = 0;
}

void
cycle(TX0 *tx0)
{
	// a cycle takes 6μs
	if(!tx0->ss) cyclestandby(tx0);
	else if(!tx0->c) cycle0(tx0);
	else cycle1(tx0);
}

void
throttle(TX0 *tx0)
{
	while(tx0->realtime < tx0->simtime)
		tx0->realtime = gettime();
}

void
handleio(TX0 *tx0)
{
	/* Reader */
	if(tx0->petr12 && tx0->r_time < tx0->simtime && tx0->r_fd >= 0) {
		u8 c;
		tx0->r_time = tx0->simtime + RDLY;
		if(read(tx0->r_fd, &c, 1) <= 0) {
			close(tx0->r_fd);
			tx0->r_fd = -1;
			return;
		}
		tx0->petr_holes = c;
		// probably only read if 7th hole is punched
		tx0->petr3 = !!(c & 0100);
	}

	/* Flexowriter */
	if(tx0->fl_time < tx0->simtime) {
		tx0->fl_time = NEVER;
		if(tx0->fl_punch) {
			char c = 0200;
			if(AC & 0100000) c |= 001;
			if(AC & 0010000) c |= 002;
			if(AC & 0001000) c |= 004;
			if(AC & 0000100) c |= 010;
			if(AC & 0000010) c |= 020;
			if(AC & 0000001) c |= 040;
			if(tx0->fl_hole7)
				c |= 0100;
			if(tx0->p_fd >= 0)
				write(tx0->p_fd, &c, 1);
		} else {
			char c = 0;
			if(AC & 0100000) c |= 040;
			if(AC & 0010000) c |= 020;
			if(AC & 0001000) c |= 010;
			if(AC & 0000100) c |= 004;
			if(AC & 0000010) c |= 002;
			if(AC & 0000001) c |= 001;
			if(tx0->typ_fd >= 0)
				write(tx0->typ_fd, &c, 1);
		}
		tx0->ios = 1;
	}
	if(tx0->tyi_wait < tx0->simtime && hasinput(tx0->typ_fd)) {
		char c;
		if(read(tx0->typ_fd, &c, 1) <= 0) {
			close(tx0->typ_fd);
			tx0->typ_fd = -1;
			return;
		}
		// not entirely sure how char enters LR
		// but i think whole LR is cleared
		tx0->lr = 0400000 | (c&077)<<11;
		// TX-0 has to keep up, so avoid clobbering LR
		// not sure what a good timeout here is
		tx0->tyi_wait = tx0->simtime + US(25000);
	}

	/* Display */
	if(tx0->dpy_time < tx0->simtime) {
		tx0->dpy_time = NEVER;
		if(tx0->dpy_fd >= 0) {
			agedisplay(tx0);
			int x = AC >> 9;
			int y = AC & 0777;
			int dt = (tx0->simtime - tx0->dpy_last)/1000;
			if(x & 0400) x++;
			if(y & 0400) y++;
			x = ((x+0400)&0777)<<1;
			y = ((y+0400)&0777)<<1;
			int cmd = x | (y<<10) | (7<<20) | (dt<<23);
			tx0->dpy_last = tx0->simtime;
			write(tx0->dpy_fd, &cmd, sizeof(cmd));
		}
		tx0->ios = 1;
	}
}

void
agedisplay(TX0 *tx0)
{
	int cmd = 510<<23;
	assert(tx0->dpy_last <= tx0->simtime);
	u64 dt = (tx0->simtime - tx0->dpy_last)/1000;
	while(dt >= 510) {
		tx0->dpy_last += 510*1000;
		write(tx0->dpy_fd, &cmd, sizeof(cmd));
		dt = (tx0->simtime - tx0->dpy_last)/1000;
	}
}

int
getwrd(int fd)
{
	u8 c;
	int w, n;

	w = 0;
	n = 3;
	while(n--) {
		do {
			if(read(fd, &c, 1) <= 0)
				return -1;
		} while((c&0300) != 0300);
		w >>= 1;
		if(c & 001) w |= 0400000;
		if(c & 002) w |= 0040000;
		if(c & 004) w |= 0004000;
		if(c & 010) w |= 0000400;
		if(c & 020) w |= 0000040;
		if(c & 040) w |= 0000004;
	}
	return w;
}

void
readrim(TX0 *tx0, int fd)
{
	int inst, wd;

	if(fd < 0) {
		fprintf(stderr, "no tape\n");
		return;
	}

	// clear memory just to be safe
	for(wd = 0; wd < MAXMEM; wd++)
		tx0->core[wd] = 0;
	for(;;) {
		inst = getwrd(fd);
		if((inst&0600000) == 0000000 || (inst&0600000) == 0600000) {
			wd = getwrd(fd);
			tx0->core[inst&ADDRMASK] = wd;
		} else if((inst&0600000) == 0200000 || (inst&0600000) == 0400000) {
			printf("start: %04o\n", inst&ADDRMASK);
			return;
		} else {
			printf("rim botch: %06o\n", inst);
			return;
		}
	}
}

void
cli(TX0 *tx0)
{
	static int timer = 0;
	if(timer++ != 10000) return;
	timer = 0;

	if(!hasinput(0)) return;

	char line[1024], *p;
	int n;

	n = read(0, line, sizeof(line));
	if(n > 0 && n < sizeof(line)) {
		line[n] = '\0';
		if(p = strchr(line, '\r'), p) *p = '\0';
		if(p = strchr(line, '\n'), p) *p = '\0';

		char **args = split(line, &n);

		if(n > 0) {
			// reader
			if(strcmp(args[0], "r") == 0) {
				close(tx0->r_fd);
				tx0->r_fd = -1;
				if(args[1]) {
					tx0->r_fd = open(args[1], O_RDONLY);
					if(tx0->r_fd < 0)
						printf("couldn't open %s\n", args[1]);
				}
			}
			// punch
			else if(strcmp(args[0], "p") == 0) {
				close(tx0->p_fd);
				tx0->p_fd = -1;
				if(args[1]) {
					tx0->p_fd = open(args[1], O_CREAT|O_WRONLY|O_TRUNC, 0644);
					if(tx0->p_fd < 0)
						printf("couldn't open %s\n", args[1]);
				}
			}
			// load
			else if(strcmp(args[0], "l") == 0) {
				static char *rimfile = nil;
				int fd;
				if(args[1]) {
					free(rimfile);
					rimfile = strdup(args[1]);
				}
				if(rimfile) {
					fd = open(rimfile, O_RDONLY);
					if(fd < 0) {
						printf("couldn't open %s\n", rimfile);
					} else {
						readrim(tx0, fd);
						close(fd);
					}
				} else
					printf("no filename\n");
			}
			// help
			else if(strcmp(args[0], "?") == 0 ||
			        strcmp(args[0], "help") == 0) {
				printf("r                     unmount tape from reader\n");
				printf("r filename            mount tape in reader\n");
				printf("p                     unmount tape from punch\n");
				printf("p filename            mount tape in punch\n");
				printf("l filename            load memory from RIM-file\n");
			}
		}

		free(args[0]);
		free(args);
	}
}
