#include "common.h"
#include "pdp1.h"
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
#define RDLY US(2500)		// 400/s
#define PDLY US(15873)		// 63/s

static void iot_pulse(PDP1 *pdp, int pulse, int dev, int nac);
static void iot(PDP1 *pdp, int pulse);

void
pwrclr(PDP1 *pdp)
{
	IR = rand() & 077;
	PC = rand() & 07777;
	MA = rand() & 07777;
	MB = rand() & 0777777;
	AC = rand() & 0777777;
	IO = rand() & 0777777;

	pdp->rc = 0;
	pdp->rby = 0;
	pdp->rcl = 0;
	pdp->r_time = NEVER;
	pdp->rim_return = 0;
	pdp->rim_cycle = 0;

	pdp->punon = 0;
	pdp->p_time = NEVER;

	pdp->tbs = 0;
	pdp->tbb = 0;
	pdp->tyo = 0;
	pdp->typ_time = NEVER;
	pdp->tyi_wait = 0;

	pdp->dpy_time = NEVER;
}

static void
sc(PDP1 *pdp)
{
	pdp->df1 = 0;
	pdp->df2 = 0;
	pdp->ov1 = 0;
	pdp->ov2 = 0;
	pdp->ihs = 0;
	pdp->ios = 0;
	pdp->ioh = 0;
	pdp->ir = 0;
	pdp->pc = 0;
	pdp->ioc = 1;
	pdp->sbm = pdp->sbm_start_sw;

	pdp->tyo = 0;
}

void
spec(PDP1 *pdp)
{
	// PB
	pdp->run = 0;
	if(pdp->start_sw || pdp->deposit_sw || pdp->examine_sw)
		pdp->rim = 0;

	// SP1
	pdp->run_enable = 1;
	MA = 0;
	if(pdp->start_sw)
		pdp->cyc = 0;
	if(pdp->deposit_sw)
		pdp->ac = 0;
	if(pdp->start_sw || pdp->deposit_sw || pdp->examine_sw)
		sc(pdp);

	// SP2
	if(pdp->start_sw || pdp->deposit_sw || pdp->examine_sw)
		PC |= pdp->ta;
	if(pdp->deposit_sw || pdp->examine_sw || pdp->rim)
		pdp->cyc = 1;
	if(pdp->examine_sw)
		pdp->ir |= 020>>1;
	if(pdp->deposit_sw) {
		pdp->ir |= 024>>1;
		AC |= pdp->tw;
	}

	// SP3
	if(pdp->deposit_sw || pdp->examine_sw) {
		MA |= PC;
		pdp->cychack = 1;
	}

	// SP4
	if(pdp->start_sw || pdp->continue_sw)
		pdp->run = 1;

	pdp->start_sw = 0;
	pdp->sbm_start_sw = 0;
	pdp->stop_sw = 0;
	pdp->continue_sw = 0;
	pdp->examine_sw = 0;
	pdp->deposit_sw = 0;
}

void
start_readin(PDP1 *pdp)
{
	// PB
	pdp->run = 0;
	pdp->rim = 1;
	pdp->sbm = 0;

	pdp->rim_cycle = 1;
}
void
readin1(PDP1 *pdp)
{
	pdp->rim_cycle = 0;

	// SP1
	pdp->run_enable = 1;
	MA = 0;
	if(pdp->rim) {	// guaranteed
		MB = 0;
		sc(pdp);
		iot_pulse(pdp, 1, 2, 0);
	}
}
void
readin2(PDP1 *pdp)
{
	// SP2
	pdp->cyc = 1;
	MB |= IO;
	// epc = eta

	// SP3
	IR |= MB>>13;
	MA |= MB & ADDRMASK;
	// extend: exd = 1

	// SP4
	if(IR_DIO) iot_pulse(pdp, 1, 2, 0);
	if(IR_JMP) {
		pdp->run = 1;
		pdp->cyc = 0;
		pdp->rim = 0;
		PC |= MB & ADDRMASK;
		pdp->cychack = 1;	// actually TP0 should work too
	}
}

static int
decflg(int n)
{
	switch(n&7) {
	case 1: return 040;
	case 2: return 020;
	case 3: return 010;
	case 4: return 004;
	case 5: return 002;
	case 6: return 001;
	case 7: return 077;
	}
	return 0;
}

static void
shro(PDP1 *pdp)
{
	int ac = AC;
	int io = IO;
	switch((MB>>9) & 017) {
	case 001:	// RAL
		ac = (AC&~B0)<<1 | (AC&B0)>>17;
		break;
	case 002:	// RIL
		io = (IO&~B0)<<1 | (IO&B0)>>17;
		break;
	case 003:	// RCL
		ac = (AC&~B0)<<1 | (IO&B0)>>17;
		io = (IO&~B0)<<1 | (AC&B0)>>17;
		break;
	case 005:	// SAL
		ac = (AC&B0) | (AC&~(B0|B1))<<1 | (AC&B0)>>17;
		break;
	case 006:	// SIL
		io = (IO&B0) | (IO&~(B0|B1))<<1 | (IO&B0)>>17;
		break;
	case 007:	// SCL
		ac = (AC&B0) | (AC&~(B0|B1))<<1 | (IO&B0)>>17;
		io = (IO&~B0)<<1 | (AC&B0)>>17;
		break;
	case 011:	// RAR
		ac = (AC&B17)<<17 | AC>>1;
		break;
	case 012:	// RIR
		io = (IO&B17)<<17 | IO>>1;
		break;
	case 013:	// RCR
		ac = (IO&B17)<<17 | AC>>1;
		io = (AC&B17)<<17 | IO>>1;
		break;
	case 015:	// SAR
		ac = (AC&B0) | AC>>1;
		break;
	case 016:	// SIR
		io = (IO&B0) | IO>>1;
		break;
	case 017:	// SCR
		ac = (AC&B0) | AC>>1;
		io = (AC&B17)<<17 | IO>>1;
		break;
	}
	AC = ac;
	IO = io;
}

static void
cycle0(PDP1 *pdp)
{
	int hack = pdp->cychack;
	pdp->cychack = 0;
	switch(hack) {
	default:
	// TP0
	if(IR_SHRO && (MB & B12)) shro(pdp);
	MA |= PC;

	case 1:
	// TP1
	if(IR_SHRO && (MB & B11)) shro(pdp);
	// emc = 0

	// TP2
	if(IR_SHRO && (MB & B10)) shro(pdp);
	PC = (PC+1) & ADDRMASK;
	if(IR_IOT) pdp->ioc = !pdp->ioh && !pdp->ihs;
	pdp->ihs = 0;

	// TP3
	if(IR_SHRO && (MB & B9)) shro(pdp);
	MB = 0;

	case 4:
	// TP4
	MB |= pdp->core[MA];
	pdp->core[MA] = 0;
	IR = 0;

	// TP5
	IR |= MB>>13;

	// TP6
	if((MB & B5) && !IR_SHRO && !IR_SKIP &&
	                !IR_LAW && !IR_OPR && !IR_IOT && !IR_CALJDA)
		pdp->df1 = 1;

	// TP6a
	if(IR_IOT && !(MB & B5) && pdp->ioh) {
		pdp->ioc = 1;
		pdp->ihs = 1;
		pdp->ioh = 0;
	}

	// TP7
	if(IR_SHRO && (MB & B17)) shro(pdp);
	if(IR_JSP && !pdp->df1 || IR_LAW || IR_OPR && (MB & B10)) AC = 0;
	if(IR_OPR && (MB & B6)) IO = 0;
	if(IR_IOT) {
		if((MB & B5) && !pdp->ioh && !pdp->ihs) pdp->ioh = 1;
		if(pdp->ioc) iot(pdp, 0);
	}

	// TP8
	if(!pdp->df1) {
		if(IR_JSP) AC |= PC, PC = 0;
		if(IR_JMP) PC = 0;
	}
	if(IR_SKIP) {
		int skip = 0;
		if((MB & B7) && !(IO&B0)) skip = 1;
		if((MB & B8) && !pdp->ov1) skip = 1;
		if((MB & B9) && (AC&B0)) skip = 1;
		if((MB & B10) && !(AC&B0)) skip = 1;
		if((MB & B11) && AC==0) skip = 1;
		if((MB&070) && !(pdp->ss&decflg(MB>>3))) skip = 1;
		if((MB&007) && !(pdp->pf&decflg(MB))) skip = 1;
		if(MB & B5) skip = !skip;
		if(skip) PC = (PC+1) & ADDRMASK;
	}
	if(IR_SHRO && (MB & B16)) shro(pdp);
	if(IR_LAW) AC |= MB & 0007777;
	if(IR_OPR) {
		if(MB & B7) AC |= pdp->tw;
		if(MB & B11) AC |= PC;
		if(MB & B14) pdp->pf |= decflg(MB);
		else pdp->pf &= ~decflg(MB);
	}

	// TP9
	pdp->core[MA] = MB;	// approximate
	if(!pdp->df1 && (IR_JMP || IR_JSP)) PC |= MB & ADDRMASK;
	if(IR_SKIP && (MB & B8)) pdp->ov1 = 0;
	if(IR_SHRO && (MB & B15)) shro(pdp);
	if(IR_OPR && (MB & B8) || IR_LAW && (MB & B5)) AC ^= WORDMASK;
	if(IR_IOT && !pdp->ihs && pdp->ios) pdp->ioh = 0;
	if(IR_OPR && (MB & B9) ||
	   IR_INCORR ||
	   pdp->single_cyc_sw ||
	   pdp->single_inst_sw && !pdp->df1 && pdp->ir >= 030 ||
	   !pdp->run_enable)
		pdp->run = 0;

	// TP9A
	if(IR_SHRO && (MB & B14)) shro(pdp);
	if(IR_IOT && pdp->ioh) PC = (PC-1) & ADDRMASK, pdp->df1 = pdp->df2 = 0;

	// TP10
	if(pdp->run) MA = 0;
	if(pdp->df1 || pdp->ir < 030) pdp->cyc = 1;
	if(IR_SHRO && (MB & B13)) shro(pdp);
	if(IR_IOT) {
		if(pdp->ihs) pdp->ioh = 1;
		else if(!pdp->ioh) pdp->ios = 0;
		if(pdp->ioc) iot(pdp, 1);
	}
	}
}

static void
defer(PDP1 *pdp)
{
	// TP0
	MA |= MB & ADDRMASK;

	// TP3
	MB = 0;

	// TP4
	MB |= pdp->core[MA];
	pdp->core[MA] = 0;

	if(MB & B5) {
		// TP6
		pdp->df2 = 1;
	} else {
		// TP7
		if(IR_JSP) AC = 0;

		// TP8
		if(IR_JSP) AC |= PC, PC = 0;
		if(IR_JMP) PC = 0;

		// TP9
		if(IR_JSP || IR_JMP) PC |= MB & ADDRMASK;
	}
	pdp->core[MA] = MB;	// approximate
	if(IR_INCORR ||
	   pdp->single_cyc_sw ||
	   pdp->single_inst_sw && !pdp->df2 && pdp->ir >= 030 ||
	   !pdp->run_enable)
		pdp->run = 0;

	// TP10
	if(pdp->run) MA = 0;
	if(!pdp->df2) {
		pdp->df1 = 0;
		if(pdp->ir >= 030) pdp->cyc = 0;
	}
	pdp->df2 = 0;
}

static void
cycle1(PDP1 *pdp)
{
	int hack = pdp->cychack;
	pdp->cychack = 0;
	switch(hack) {
	default:
	// TP0
	if(IR_CALJDA && !(MB & B5))
		MA |= 0100;
	else
		MA |= MB & ADDRMASK;
	// EMA stuff
	if(IR_DIS) {
		int ac = (AC&~B0)<<1 | (IO&B0)>>17;
		IO = (IO&~B0)<<1 | (~AC&B0)>>17;
		AC = ac;
	}

	case 1:
	// TP1
	// emc = 0

	// TP2
	if(IR_DIS & !(IO & B17)) {
		if(AC == 0777777) AC = 1;
		else AC++;
	}

	// TP3
	MB = 0;
	if(IR_XCT) {
		pdp->cyc = 0;
		pdp->cychack = 4;
		return;
	}

	// TP4
	MB |= pdp->core[MA];
	pdp->core[MA] = 0;
	if(IR_SUB || IR_DIS && (IO & B17)) AC ^= WORDMASK;
	if(IR_LIO) IO = 0;

	// TP5
	if(IR_AND) AC &= MB;
	if(IR_IOR) AC |= MB;
	if(IR_IDX || IR_ISP || IR_LAC) AC = MB;
	if(IR_DIO || IR_DZM) MB = 0;
	if(IR_LIO) IO |= MB;
	if((IR_ADD || IR_SUB) && (AC&B0) == (MB&B0)) pdp->ov2 = 1;
	if(IR_XOR || IR_ADD || IR_SUB || IR_SAD || IR_SAS ||
	   IR_DIS || IR_MUS && (IO & B17))
		AC ^= MB;

	// TP6
	if(IR_ADD || IR_SUB || IR_DIS || IR_MUS && (IO & B17)) {
		AC += (~AC & MB)<<1;
		if(AC & 01000000) AC++;
		AC &= WORDMASK;
	}
	if(IR_IDX || IR_ISP) {
		if(AC == 0777776) AC = 0;
		else if(AC == 0777777) AC = 1;
		else AC++;
	}

	// TP7
	if(IR_DAC || IR_IDX || IR_ISP) MB = AC;
	if(IR_CALJDA) MB = AC, AC = 0;
	if(IR_DAP) MB = MB&0770000 | AC&0007777;
	if(IR_DIP) MB = MB&0007777 | AC&0770000;
	if(IR_DIO) MB = IO;

	// TP8
	if(IR_MUS) {
		int ac = AC>>1;
		IO = (AC&B17)<<17 | IO>>1;
		AC = ac;
	}
	if(IR_CALJDA) AC |= PC, PC = 0;
	if(IR_SAS && AC==0 || IR_SAD && AC!=0 ||
	   IR_ISP && !(AC & B0))
		PC = (PC+1) & ADDRMASK;

	// TP9
	pdp->core[MA] = MB;	// approximate
	if(IR_CALJDA) PC |= MA;
	if(IR_SUB) AC ^= WORDMASK;
	if(IR_SAD || IR_SAS) AC ^= MB;
	if((IR_ADD || IR_SUB) && (AC&B0) == (MB&B0)) pdp->ov2 = 0;
	if(IR_INCORR ||
	   pdp->single_cyc_sw ||
	   pdp->single_inst_sw ||
	   !pdp->run_enable)
		pdp->run = 0;

	// TP9A
	if((IR_ADD || IR_DIS) && AC == 0777777) AC = 0;
	if(IR_CALJDA) PC = (PC+1) & ADDRMASK;

	// TP10
	if(pdp->ov2) pdp->ov1 = 1;
	pdp->ov2 = 0;
	pdp->cyc = 0;
	if(pdp->run) MA = 0;
	}
}

void
cycle(PDP1 *pdp)
{
	// a cycle takes 5μs
	if(!pdp->cyc) cycle0(pdp);
	else if(pdp->df1) defer(pdp);
	else cycle1(pdp);
}

void
throttle(PDP1 *pdp)
{
	do
		pdp->realtime = gettime();
	while(pdp->realtime < pdp->simtime);
}

static void
iot_pulse(PDP1 *pdp, int pulse, int dev, int nac)
{
	switch(dev) {
	case 000:
		break;

	case 001:	// rpa
	case 002:	// rpb
		if(pulse) {
			pdp->rcp = nac;
			if(dev == 00001) {
				pdp->rby = 0;
				pdp->rc = 3;
				pdp->rcl ^= 1;
			} else {
				pdp->rby = 1;
				pdp->rc = 1;
				pdp->rcl = 1;
			}
			pdp->r_time = pdp->simtime + RDLY;
			pdp->rb = 0;
		}
		break;

	case 003:	// tyo
		if(!pulse) {
			if(!pdp->tyo)
				pdp->tb = 0;
		} else {
			pdp->tcp = nac;
			if(!pdp->tyo) {
				pdp->tyo = 1;
				pdp->tb |= IO & 077;
				pdp->typ_time = pdp->simtime + US(25000);
			}
		}
		break;

	case 004:	// tyi
		if(!pulse) {
			IO = 0;
		} else {
			pdp->tbs = 0;
			pdp->io |= pdp->tb;
		}
		break;

	case 005:	// ppa
	case 006:	// ppb
		if(!pulse) {
			pdp->pb = 0;
			pdp->punon = 1;
			pdp->p_time = pdp->simtime + PDLY;
		} else {
			pdp->pcp = nac;
			if(dev == 00005)
				pdp->pb |= IO & 0377;
			else
				pdp->pb |= 0200 | (IO >> 12)&077;
		}
		break;

	case 007:	// dpy
		if(!pulse) {
			pdp->dbx = 0;
			pdp->dby = 0;
		} else {
			pdp->dcp = nac;
			pdp->dbx |= AC>>8;
			pdp->dby |= IO>>8;
			pdp->dpy_time = pdp->simtime + US(50);
		}
		break;

	case 030:	// rrb
		if(pulse) {
			IO |= pdp->rb;
			pdp->rbs = 0;
		}
		break;

	case 033:	// cks
		if(pulse) {
			// TODO: LP
			IO |= pdp->rbs<<16;
			IO |= !pdp->tyo<<15;
			IO |= pdp->tbs<<14;
			IO |= !pdp->punon<<13;
			// ..
			IO |= pdp->sbm<<11;
		}
		break;

	case 054:	// lsm
		if(!pulse) {
			pdp->sbm = 0;
		}
		break;
	case 055:	// esm
		if(!pulse) {
			pdp->sbm = 1;
		}
		break;
	case 056:	// csb		ESB in schematics?!?
		if(!pulse) {
			// TODO
		}
		break;

	default:
		printf("unknown IOT %06o\n", MB);
		break;
	}
}

static void
iot(PDP1 *pdp, int pulse)
{
	int nac = (MB & (B5|B6)) == B5 || (MB & (B5|B6)) == B6;
	int dev = MB & 077;
	if(!pulse && (dev&070)==030) IO = 0;
	iot_pulse(pdp, pulse, dev, nac);
}

void
handleio(PDP1 *pdp)
{
	/* Reader */
	if(pdp->rcl && pdp->r_time < pdp->simtime && pdp->r_fd >= 0) {
		u8 c;
		pdp->r_time = pdp->simtime + RDLY;
		if(read(pdp->r_fd, &c, 1) <= 0) {
			close(pdp->r_fd);
			pdp->r_fd = -1;
			return;
		}
		if(pdp->rc && (!pdp->rby || c&0200)) {
			// STROBE PETR
			pdp->rcl = 0;
			pdp->rb |= c & (pdp->rby ? 077 : 0377);
			// SHIFT RB
			if(pdp->rc != 3) {
				pdp->rb = (pdp->rb<<6) & WORDMASK;
				pdp->rcl = 1;
			}
			// CLR IO
			if(pdp->rc == 3 && (pdp->rcp || pdp->rim)) IO = 0;
			// -----
			// +1 RC
			if(pdp->rc == 3) {
				// READER RETURN
				if(pdp->rcp) pdp->ios = 1;
				else pdp->rbs = 1;
				if(pdp->rcp || pdp->rim) {
					IO |= pdp->rb;
					pdp->rbs = 0;
					if(pdp->rim) pdp->rim_return = 2;
				}
			}
			pdp->rc = (pdp->rc+1) & 3;
		}
	}

	/* Punch */
	if(pdp->punon && pdp->p_time < pdp->simtime) {
		pdp->p_time = NEVER;
		if(pdp->p_fd >= 0) {
			char c = pdp->pb;
			write(pdp->p_fd, &c, 1);
		}
		if(pdp->pcp) pdp->ios = 1;
	}

	/* Typewriter */
	if(pdp->typ_time < pdp->simtime) {
		// wrong timing
		pdp->typ_time = NEVER;
		if((pdp->tb&076) == 034)
			pdp->tbb = pdp->tb & 1;
		else if(pdp->typ_fd >= 0) {
			char c = (pdp->tbb<<6) | pdp->tb;
			write(pdp->typ_fd, &c, 1);
		}
		// this is really much more complicated
		// and overlaps with the type-in logic
		pdp->tyo = 0;
		if(pdp->tcp) pdp->ios = 1;
	}
	if(pdp->tyi_wait < pdp->simtime && hasinput(pdp->typ_fd)) {
		char c;
		if(read(pdp->typ_fd, &c, 1) <= 0) {
			close(pdp->typ_fd);
			pdp->typ_fd = -1;
			return;
		}
		pdp->tb = 0;
		// STROBE TYPE
		pdp->tb |= c & 077;
		//
		pdp->tbs = 1;
		// TYPE SYNC
		pdp->pf |= 040;

		// PDP-1 has to keep up, so avoid clobbering TB
		// not sure what a good timeout here is
		pdp->tyi_wait = pdp->simtime + 25000000;
	}

	/* Display */
	if(pdp->dpy_time < pdp->simtime) {
		pdp->dpy_time = NEVER;
		if(pdp->dpy_fd >= 0) {
			agedisplay(pdp);
			int x = pdp->dbx;
			int y = pdp->dby;
			int dt = (pdp->simtime - pdp->dpy_last)/1000;
			if(x & 01000) x++;
			if(y & 01000) y++;
			x = (x+01000)&01777;
			y = (y+01000)&01777;
			int cmd = x | (y<<10) | (4<<20) | (dt<<23);
			pdp->dpy_last = pdp->simtime;
			write(pdp->dpy_fd, &cmd, sizeof(cmd));
		}
		if(pdp->dcp) pdp->ios = 1;
	}
}

void
agedisplay(PDP1 *pdp)
{
	int cmd = 511<<23;
	assert(pdp->dpy_last < pdp->simtime);
	u64 dt = (pdp->simtime - pdp->dpy_last)/1000;
	while(dt >= 511) { 
		pdp->dpy_last += 511*1000;
		write(pdp->dpy_fd, &cmd, sizeof(cmd));
		dt = (pdp->simtime - pdp->dpy_last)/1000;
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
		} while((c&0200) == 0);
		w = w<<6 | (c&077);
	}
	return w;
}

void
readrim(PDP1 *pdp, int fd)
{
	int inst, wd;

	if(fd < 0) {
		fprintf(stderr, "no tape\n");
		return;
	}

	// clear memory just to be safe
	for(wd = 0; wd < MAXMEM; wd++)
		pdp->core[wd] = 0;
	for(;;) {
		inst = getwrd(fd);
		if((inst&0760000) == 0320000) {
			wd = getwrd(fd);
			pdp->core[inst&07777] = wd;
		} else if((inst&0760000) == 0600000) {
			printf("start: %04o\n", inst&07777);
			return;
		} else {
			printf("rim botch: %06o\n", inst);
			return;
		}
	}
}

void
cli(PDP1 *pdp)
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
				close(pdp->r_fd);
				pdp->r_fd = -1;
				if(args[1]) {
					pdp->r_fd = open(args[1], O_RDONLY);
					if(pdp->r_fd < 0)
						printf("couldn't open %s\n", args[1]);
				}
			}
			// punch
			else if(strcmp(args[0], "p") == 0) {
				close(pdp->p_fd);
				pdp->p_fd = -1;
				if(args[1]) {
					pdp->p_fd = open(args[1], O_CREAT|O_WRONLY|O_TRUNC);
					if(pdp->p_fd < 0)
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
						readrim(pdp, fd);
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
