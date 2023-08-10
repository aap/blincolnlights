#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "args.h"
#include "common.h"

typedef uint64_t u64;
typedef uint32_t u32, Word;
typedef uint16_t u16, Addr;
typedef uint8_t u8;
#define WORDMASK 0777777
#define ADDRMASK 07777
#define MAXMEM (4*1024)
#define nil NULL

typedef struct PDP1 PDP1;

struct PDP1
{
	Word ac;
	Word io;
	Word mb;
	Word ma;
	Word pc;
	Word ir;
	Word core[MAXMEM];

	Word ta;
	Word tw;

	int start_sw;
	int stop_sw;
	int continue_sw;
	int examine_sw;
	int deposit_sw;

	int run, run_enable;
	int cyc;
	int df1, df2;
	int bc1, bc2;
	int ov1, ov2;
	int rim;
	int sbm;
	int ioc;
	int ihs;
	int ios;
	int ioh;

	int ss;
	int pf;

	int single_cyc_sw;
	int single_inst_sw;

	int cychack;	// for cycle entry past TP0
	int ncycle;
	u64 prevtime, time;	// measure time
	u64 prevcyctm, cyctm;	// cycle time
	int debt;

	// peripherals
	int pcp;
	int tcp;

	// display
	int dcp;
	int dbx, dby;
	// simulation
	int dpy_fd;
	int dpy_state;

	// reader
	int rcp;
	int rb;
	int rc;
	int rby;
	int rcl;
	int rbs;
	// simulation
	int r_fd;
};


#define IR pdp->ir
#define PC pdp->pc
#define MA pdp->ma
#define MB pdp->mb
#define AC pdp->ac
#define IO pdp->io

// 0
#define IR_AND (IR == 001)
#define IR_IOR (IR == 002)
#define IR_XOR (IR == 003)
#define IR_XCT (IR == 004)
// 5
// 6
#define IR_CALJDA (IR == 007)
#define IR_LAC (IR == 010)
#define IR_LIO (IR == 011)
#define IR_DAC (IR == 012)
#define IR_DAP (IR == 013)
#define IR_DIP (IR == 014)
#define IR_DIO (IR == 015)
#define IR_DZM (IR == 016)
// 17
#define IR_ADD (IR == 020)
#define IR_SUB (IR == 021)
#define IR_IDX (IR == 022)
#define IR_ISP (IR == 023)
#define IR_SAD (IR == 024)
#define IR_SAS (IR == 025)
#define IR_MUS (IR == 026)
#define IR_DIS (IR == 027)
#define IR_JMP (IR == 030)
#define IR_JSP (IR == 031)
#define IR_SKIP (IR == 032)
#define IR_SHRO (IR == 033)
#define IR_LAW (IR == 034)
#define IR_IOT (IR == 035)
// 36
#define IR_OPR (IR == 037)
#define IR_INCORR (IR==0 || IR==5 || IR==6 || IR==017 || IR==036)

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

static void iot(PDP1 *pdp, int pulse);
static void handleio(PDP1 *pdp);

static struct timespec starttime;
void
inittime(void)
{
	clock_gettime(CLOCK_MONOTONIC, &starttime);
}
u64
gettime(void)
{
	struct timespec tm;
	u64 t;
	clock_gettime(CLOCK_MONOTONIC, &tm);
	tm.tv_sec -= starttime.tv_sec;
	t = tm.tv_nsec;
	t += (u64)tm.tv_sec * 1000 * 1000 * 1000;
	return t;
}
void
nsleep(u64 ns)
{
	struct timespec tm;
	tm.tv_sec = ns / (1000 * 1000 * 1000);
	tm.tv_nsec = ns % (1000 * 1000 * 1000);
	nanosleep(&tm, nil);
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
	pdp->sbm = 0;
}

static void
spec(PDP1 *pdp)
{
	// PB
	pdp->run = 0;
	if(pdp->start_sw || pdp->deposit_sw || pdp->examine_sw)
		pdp->rim = 0;

	// SP1
	pdp->run_enable = 1;
	pdp->ma = 0;
	if(pdp->start_sw)
		pdp->cyc = 0;
	if(pdp->deposit_sw)
		pdp->ac = 0;
	if(pdp->start_sw || pdp->deposit_sw || pdp->examine_sw)
		sc(pdp);

	// SP2
	if(pdp->start_sw || pdp->deposit_sw || pdp->examine_sw)
		pdp->pc |= pdp->ta;
	if(pdp->deposit_sw || pdp->examine_sw || pdp->rim)
		pdp->cyc = 1;
	if(pdp->examine_sw)
		pdp->ir |= 020>>1;
	if(pdp->deposit_sw) {
		pdp->ir |= 024>>1;
		pdp->ac |= pdp->tw;
	}

	// SP3
	if(pdp->deposit_sw || pdp->examine_sw) {
		pdp->ma |= pdp->pc;
		pdp->cychack = 1;
	}

	// SP4
	if(pdp->start_sw || pdp->continue_sw)
		pdp->run = 1;

	pdp->start_sw = 0;
	pdp->stop_sw = 0;
	pdp->continue_sw = 0;
	pdp->examine_sw = 0;
	pdp->deposit_sw = 0;
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
	switch(pdp->cychack) {
	default:
	// TP0
	if(IR_SHRO && (MB & B12)) shro(pdp);
	MA |= PC;

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

	pdp->cychack = 0;
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
	switch(pdp->cychack) {
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
	static int ncycle = 0;

	// a cycle takes 5μs
	if(!pdp->cyc) cycle0(pdp);
	else if(pdp->df1) defer(pdp);
	else cycle1(pdp);

	// This isn't quite ideal yet
	ncycle++;
	if(ncycle == 100) {
		const int cyc = 4993;	// bit of slack
		pdp->prevcyctm = pdp->cyctm;
		do pdp->cyctm = gettime();
		while(pdp->cyctm < pdp->prevcyctm + cyc*ncycle-pdp->debt);
		if(pdp->cyctm-pdp->prevcyctm < cyc*ncycle)
			pdp->debt = cyc*ncycle - (pdp->cyctm-pdp->prevcyctm);
		else
			pdp->debt = 0;
		ncycle = 0;
	}
}

void
measuretime(PDP1 *pdp)
{
	static int ncycle = 0;
	ncycle++;
	if(ncycle == 100000) {
		pdp->prevtime = pdp->time;
		pdp->time = gettime();
		float cyctime = (double)(pdp->time-pdp->prevtime) / ncycle;
		printf("%f\n", cyctime);
		ncycle = 0;
	}
}

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

	inittime();
	pdp->prevtime = pdp->time = gettime();
	pdp->prevcyctm = pdp->cyctm = gettime();
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
			pdp->single_cyc_sw = !!(sw1 & SW_SSTEP);
			pdp->single_inst_sw = !!(sw1 & SW_SINST);

			if(pdp->run) {	// not really correct
				cycle(pdp);
				handleio(pdp);
				measuretime(pdp);
			}
		} else {
			IR = rand() & 077;
			PC = rand() & 07777;
			MA = rand() & 07777;
			MB = rand() & 0777777;
			AC = rand() & 0777777;
			IO = rand() & 0777777;

			panel->lights0 = 0;
			panel->lights1 = 0;
			panel->lights2 = 0;
		}
	}
}

static void
iot(PDP1 *pdp, int pulse)
{
	int nac = (MB & (B5|B6)) == B5 || (MB & (B5|B6)) == B6;
	int dev = MB & 03777;
	switch(dev) {
	case 00000:
		break;

	case 00001:	// rpa
	case 00002:	// rpb
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
			pdp->rb = 0;
		}
		break;

	case 00007:	// dpy
		if(!pulse) {
			pdp->dbx = 0;
			pdp->dby = 0;
		} else {
			pdp->dcp = nac;
			pdp->dbx |= AC>>8;
			pdp->dby |= IO>>8;
			pdp->dpy_state = 10;
		}
		break;

	case 00030:	// rrb
		if(pulse) {
			IO |= pdp->rb;
			pdp->rbs = 0;
		}
		break;

	case 00033:	// cks
		if(pulse) {
			// TODO: LP
			IO |= pdp->rbs<<16;
			// TODO: ~TYO
			// TODO: TBS
			// TODO: ~PUN
			// ..
			IO |= pdp->sbm<<11;
		}
		break;

	default:
		printf("unknown IOT %06o\n", MB);
		break;
	}
}

static void
handleio(PDP1 *pdp)
{
	if(pdp->rcl && pdp->r_fd >= 0) {
		u8 c;
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
				}
			}
			pdp->rc = (pdp->rc+1) & 3;
		}
	}

	if(pdp->dpy_state) {
		pdp->dpy_state--;
		if(pdp->dpy_state == 0) {
			if(pdp->dpy_fd >= 0) {
				int x = pdp->dbx;
				int y = pdp->dby;
				if(x & 01000) x++;
				if(y & 01000) y++;
				x = ((x+01000)&01777);
				y = ((y+01000)&01777);
//				int cmd = x | (y<<10) | (7<<20);
				int cmd = x | (y<<10) | (4<<20);
				write(pdp->dpy_fd, &cmd, sizeof(cmd));
			}
			if(pdp->dcp) pdp->ios = 1;
		}
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
readrim2(PDP1 *pdp)
{
	int inst, wd;

	if(pdp->r_fd < 0) {
		fprintf(stderr, "no tape\n");
		return;
	}

	for(;;) {
		inst = getwrd(pdp->r_fd);
		if((inst&0760000) == 0320000) {
			wd = getwrd(pdp->r_fd);
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

	pdp->tw = 0777777;
	pdp->ss = 060;

//	const char *tape = "../pdp1/maindec/maindec1_20.rim";
//	const char *tape = "../pdp1/tapes/circle.rim";
//	const char *tape = "../pdp1/tapes/minskytron.rim";
	const char *tape = "../pdp1/tapes/spacewar2B_5.rim";

	pdp->r_fd = open(tape, O_RDONLY);
	readrim2(pdp);

	emu(pdp, &panel);
	return 0;	// can't happen
}
