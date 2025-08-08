#include "common.h"
#include "pdp1.h"
#include <unistd.h>
#include <fcntl.h>

// PDP-1D but probably also on some C's?
#define LAILIA

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

#define RD_CHAN 1
#define PUN_CHAN 6
#define TTI_CHAN 7
#define TTO_CHAN 8

static void iot_pulse(PDP1 *pdp, int pulse, int dev, int nac);
static void iot(PDP1 *pdp, int pulse);

// TP length	ns
//	0 	200	200
//	1 	300	500
//	2 	550	1050
//	3 	300	1350
//	4 	200	1550
//	5 	250	1800
//	6 	250	2050
//	6a 	400	2450
//	7 	200	2650
//	8 	200	2850
//	9 	1200	4050
//	9a 	750	4800
//	10 	200	5000

enum {
	TP0_end = 200,
	TP1_end = 500,
	TP2_end = 1050,
	TP3_end = 1350,
	TP4_end = 1550,
	TP5_end = 1800,
	TP6_end = 2050,
	TP6a_end = 2450,
	TP7_end = 2650,
	TP8_end = 2850,
	TP9_end = 4050,
	TP9a_end = 4800,
	TP10_end = 5000,
	TP_unreachable = TP10_end
};
#define TP(n) if(pdp->timernd < TP##n##_end) { updatelights(pdp, pdp->panel); pdp->timernd = TP_unreachable; }

static void
readmem(PDP1 *pdp)
{
	MB |= pdp->core[(pdp->ema|MA)%MAXMEM];
	pdp->core[(pdp->ema|MA)%MAXMEM] = 0;
}

static void
writemem(PDP1 *pdp)
{
	pdp->core[(pdp->ema|MA)%MAXMEM] = MB;
}

static void mop2379(PDP1 *pdp) {
	pdp->i = pdp->w;
	pdp->w = pdp->rs;
	pdp->rs = pdp->r;
	pdp->r = !pdp->w;	// actually !pdp->rs but already clobbered
}
static void inhibit(PDP1 *pdp) { pdp->i = 1; }
static void memclr(PDP1 *pdp) {
	pdp->r = 0;
	pdp->rs = 0;
	pdp->w = 0;
	pdp->i = 0;
}

void
pwrclr(PDP1 *pdp)
{
	IR = rand() & 077;
	PC = rand() & 07777;
	MA = rand() & 07777;
	MB = rand() & 0777777;
	AC = rand() & 0777777;
	IO = rand() & 0777777;

	pdp->cyc = rand() & 1;
	pdp->df1 = rand() & 1;
	pdp->df2 = rand() & 1;
	pdp->bc = rand() & 3;
	pdp->ov1 = rand() & 1;
	pdp->ov2 = rand() & 1;
	pdp->rim = rand() & 1;
	pdp->sbm = rand() & 1;
	pdp->ioc = rand() & 1;
	pdp->ihs = rand() & 1;
	pdp->ios = rand() & 1;
	pdp->ioh = rand() & 1;
	pdp->pf = rand() & 077;
	memclr(pdp);

	if(pdp->sbs16) {
		pdp->b4 = rand() & 0177777;
		pdp->b3 = rand() & 0177777;
		pdp->b2 = rand() & 0177777;
		pdp->b1 = rand() & 0177777;
	} else {
		pdp->b4 = rand() & 1;
		pdp->b3 = rand() & 1;
		pdp->b2 = rand() & 1;
		pdp->b1 = rand() & 1;
	}
	pdp->req = 0;

	pdp->emc = rand() & 1;
	pdp->exd = rand() & 1;
	pdp->ema = rand() & EXTMASK;
	pdp->epc = rand() & EXTMASK;

	pdp->rc = 0;
	pdp->rby = 0;
	pdp->rcl = 0;
	pdp->r_time = NEVER;
	pdp->rim_return = 0;
	pdp->rim_cycle = 0;

	pdp->punon = 0;
	pdp->p_time = NEVER;
	pdp->feed_time = 0;

	pdp->tbs = 0;
	pdp->tbb = 0;
	pdp->tyo = 0;
	pdp->typ_time = NEVER;
	pdp->tyi_wait = 0;

	pdp->dpy_defl_time = NEVER;
	pdp->dpy_time = NEVER;
}

static void
mul_shift(PDP1 *pdp)
{
	int ac = AC>>1;
	IO = (AC&B17)<<17 | IO>>1;
	AC = ac;
}
static void
div_shift(PDP1 *pdp)
{
	int ac = (AC&~B0)<<1 | (IO&B0)>>17;
	IO = (IO&~B0)<<1 | (~AC&B0)>>17;
	AC = ac;
}

static void
carry(PDP1 *pdp)
{
	AC += (~AC & MB)<<1;
	if(AC & 01000000) AC++;
	AC &= WORDMASK;
}
static void
inc_ac(PDP1 *pdp)
{
	if(AC == 0777776) AC = 0;
	else if(AC == 0777777) AC = 1;
	else AC++;
}
static void
pc_to_ac(PDP1 *pdp)
{
	AC |= pdp->ov1<<17 | pdp->exd<<16 | pdp->epc | PC;
}

static void
clr_ma(PDP1 *pdp)
{
	MA = 0;
	pdp->ema = 0;
}
static void
mb_to_ma(PDP1 *pdp)
{
	MA |= MB & ADDRMASK;
	if(pdp->emc)
		pdp->ema |= MB & EXTMASK;
	else
		pdp->ema |= pdp->epc;
}
static void
pc_to_ma(PDP1 *pdp)
{
	MA |= PC;
	pdp->ema |= pdp->epc;
}


static void
clr_pc(PDP1 *pdp)
{
	PC = 0;
	pdp->epc = 0;
}
static void
mb_to_pc(PDP1 *pdp)
{
	PC |= MB & ADDRMASK;
	if(pdp->emc)
		pdp->epc |= MB & EXTMASK;
	else
		pdp->epc |= pdp->ema;
}
static void
ma_to_pc(PDP1 *pdp)
{
	PC |= MA;
	pdp->epc |= pdp->ema;
}
static void
pc_inc(PDP1 *pdp)
{
	PC = (PC+1) & ADDRMASK;
}
static void
inst_cancel(PDP1 *pdp)
{
	PC = (PC-1) & ADDRMASK;
	pdp->df1 = 0;
	pdp->df2 = 0;
}

static void
sbs_calc_req(PDP1 *pdp)
{
	pdp->req = (pdp->b3 & ~pdp->b3+1) & (pdp->b4 & ~pdp->b4+1)-1;
}
static void
clr_sbs(PDP1 *pdp)
{
	pdp->b4 = 0;
	pdp->b3 = 0;
	if(pdp->sbs16)
		pdp->b2 = 0;
	sbs_calc_req(pdp);
}
static void
hold_break(PDP1 *pdp)
{
	pdp->b4 |= pdp->req;
	if(!pdp->sbs16)
		pdp->b2 = 0;
	sbs_calc_req(pdp);
}
static void
sbs_sync(PDP1 *pdp)
{
	pdp->b3 |= pdp->b2;
	sbs_calc_req(pdp);
}
static void
sbs_reset_sync(PDP1 *pdp)
{
	if(pdp->sbs16)
		pdp->b2 &= ~pdp->b3;
}

static void
sc(PDP1 *pdp)
{
	pdp->df1 = 0;
	pdp->df2 = 0;
	pdp->bc = 0;
	pdp->ov1 = 0;
	pdp->ov2 = 0;
	pdp->ihs = 0;
	pdp->ioc = 1;
	pdp->ios = 0;
	pdp->ioh = 0;
	pdp->ir = 0;
	clr_pc(pdp);
	pdp->sbm = pdp->sbm_start_sw;
	clr_sbs(pdp);
	pdp->b2 = 0;

#ifdef LAILIA
	pdp->lai = 0;
	pdp->lia = 0;
#endif

	pdp->emc = 0;
	pdp->exd = 0;

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
	clr_ma(pdp);
	if(pdp->start_sw)
		pdp->cyc = 0;
	if(pdp->deposit_sw)
		pdp->ac = 0;
	if(pdp->start_sw || pdp->deposit_sw || pdp->examine_sw)
		sc(pdp);

	// SP2
	if(pdp->start_sw || pdp->deposit_sw || pdp->examine_sw) {
		PC |= pdp->ta;
		pdp->epc |= pdp->eta;
	}
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
		pc_to_ma(pdp);
		pdp->cychack = 1;
	}
	if(pdp->start_sw && pdp->extend_sw)
		pdp->exd = 1;

	// SP4
	if(pdp->start_sw || pdp->continue_sw)
		pdp->run = 1;
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
	clr_ma(pdp);
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
	mb_to_ma(pdp);
	if(pdp->extend_sw)
		pdp->exd = 1;

	// SP4
	if(IR_DIO) iot_pulse(pdp, 1, 2, 0);
	if(IR_JMP) {
		pdp->run = 1;
		pdp->cyc = 0;
		pdp->rim = 0;
		mb_to_pc(pdp);
		pdp->cychack = 1;	// actually TP0 should work too
	}
}

static void
clrmd(PDP1 *pdp)
{
	pdp->scr = 0;
	pdp->smb = 0;
	pdp->srm = 0;
}

static void
multiply(PDP1 *pdp)
{
	int lastlong;
	pdp->simtime += 150;
	goto start;
	do {
		if(lastlong = (IO & B17)) {
			// MDP-3
			AC ^= MB;
			pdp->simtime += 200;

			// MDP-4
			carry(pdp);
			pdp->simtime += 500;
		}

		// MDP-5
		pdp->scr = (pdp->scr+1) & 037;
		mul_shift(pdp);
		pdp->simtime += 150;

	start:
		// MDP-6
		// if(scr==021) MD_RESTART
		// but we run synchronously
		;
	} while(pdp->scr != 022);
	// last cycle is asynchronous
	pdp->simtime -= lastlong*(200 + 500) + 150;

	// MDP-11
	if(pdp->srm != pdp->smb && !(AC == 0 && IO == 0)) {
		AC ^= WORDMASK;
		IO ^= WORDMASK;
	}
}

static void
divide(PDP1 *pdp)
{
	int done;
	pdp->simtime += 150;
	goto start;
	do {
		// MDP-1
		if(!(AC & B0))
			MB ^= WORDMASK;
		div_shift(pdp);
		pdp->simtime += 150;

		if(!(IO & B17)) {
			// MDP-2
			inc_ac(pdp);
			pdp->simtime += 500;
		}

	start:
		// MDP-3
		AC ^= MB;
		pdp->simtime += 200;

		// MDP-4
		if(AC == 0777777)
			AC ^= WORDMASK;
		else
			carry(pdp);
		// delayed 0.05us
		if(MB & B0)
			MB ^= WORDMASK;
		pdp->simtime += 500;

		// MDP-5
		done = pdp->scr==022 || pdp->scr==0 && !(AC&B0);
		pdp->scr = (pdp->scr+1) & 037;
	} while(!done);

	// MDP-7
	AC ^= MB;
	if(pdp->scr & 2) pc_inc(pdp);
	pdp->simtime += 150;

	// MDP-8
	if(AC == 0777777)
		AC ^= WORDMASK;
	else
		carry(pdp);
	pdp->simtime += 500;

	// MDP-9
	if(pdp->scr & 020)
		AC >>= 1;
	// MD_RESTART, but we run synchronously
	// so don't add to simtime from now on

	// MDP-10
	if(!(pdp->scr & 020) && pdp->srm ||
	   (pdp->scr & 020) && IO != 0 && pdp->srm != pdp->smb)
		IO ^= WORDMASK;
	if(AC != 0 && pdp->srm)
		AC ^= WORDMASK;
	MB = 0;

	// swap AC and IO
	if(pdp->scr & 020) {
		// MDP-12
		MB |= IO;

		// MDP-13
		int t = MB; MB = AC; AC = t;
		IO = 0;

		// MDP-14
		IO |= MB;
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

#define CY0_INST_DONE (!pdp->df1 && pdp->ir >= 030)
#define CY0_MIDBRK_PERMIT (pdp->ir < 030)
#define DF_INST_DONE (!pdp->df2 && pdp->ir >= 030)
#define DF_MIDBRK_PERMIT (pdp->ir < 030 || (IR_JMP || IR_JSP) && pdp->df2)

static void
cycle0(PDP1 *pdp)
{
	int hack = pdp->cychack;
	pdp->cychack = 0;
	switch(hack) {
	default:
	// TP0
	if(IR_SHRO && (MB & B12)) shro(pdp);
#ifdef LAILIA
	if(pdp->lai) MB |= IO;
#endif
	pc_to_ma(pdp);
	TP(0)

	case 1:
	// TP1
	if(IR_SHRO && (MB & B11)) shro(pdp);
#ifdef LAILIA
	if(pdp->lai && pdp->lia) {
		int t = MB; MB = AC; AC = t;
		IO = 0;
	} else {
		if(pdp->lia) {
			MB = AC;
			IO = 0;
		}
		if(pdp->lai) AC = MB;
	}
#endif
	pdp->emc = 0;
	TP(1)

	// TP2
	mop2379(pdp);
	if(IR_SHRO && (MB & B10)) shro(pdp);
	pc_inc(pdp);
	if(IR_IOT) pdp->ioc = !pdp->ioh && !pdp->ihs;
	pdp->ihs = 0;
#ifdef LAILIA
	if(pdp->lia) IO |= MB;
#endif
	TP(2)

	// TP3
	mop2379(pdp);
	if(IR_SHRO && (MB & B9)) shro(pdp);
	MB = 0;
	TP(3)

	case 4:
	// TP4
	sbs_sync(pdp);
	readmem(pdp);
	IR = 0;
	TP(4)

	// TP5
	IR |= MB>>13;
	pdp->lai = 0;
	pdp->lia = 0;
	TP(5)

	// TP6
	if((MB & B5) && !IR_SHRO && !IR_SKIP &&
	                !IR_LAW && !IR_OPR && !IR_IOT && !IR_CALJDA)
		pdp->df1 = 1;
	TP(6)

	// TP6a
	if(IR_IOT && !(MB & B5) && pdp->ioh) {
		pdp->ioc = 1;
		pdp->ihs = 1;
		pdp->ioh = 0;
	}
	TP(6a)

	// TP7
	mop2379(pdp);
	if(IR_SHRO && (MB & B17)) shro(pdp);
	if(IR_JSP && !pdp->df1 || IR_LAW || IR_OPR && (MB & B10)) AC = 0;
	if(IR_OPR && (MB & B6)) IO = 0;
	if(IR_IOT) {
		if((MB & B5) && !pdp->ioh && !pdp->ihs) pdp->ioh = 1;
		if(pdp->ioc) iot(pdp, 0);
	}
	TP(7)

	// TP8
	inhibit(pdp);
	if(!pdp->df1) {
		if(IR_JSP) pc_to_ac(pdp), clr_pc(pdp);
		if(IR_JMP) clr_pc(pdp);
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
		if(skip) pc_inc(pdp);
	}
	if(IR_SHRO && (MB & B16)) shro(pdp);
	if(IR_LAW) AC |= MB & 0007777;
	if(IR_OPR) {
		if(MB & B7) AC |= pdp->tw;
		if(MB & B11) pc_to_ac(pdp);
#ifdef LAILIA
		if(MB & B12) pdp->lai = 1;
		if(MB & B13) pdp->lia = 1;
#endif
		if(MB & B14) pdp->pf |= decflg(MB);
		else pdp->pf &= ~decflg(MB);
	}
	TP(8)

	// TP9
	mop2379(pdp);
	writemem(pdp);		// approximate
	if(!pdp->df1 && (IR_JMP || IR_JSP)) mb_to_pc(pdp);
	if(IR_SKIP && (MB & B8)) pdp->ov1 = 0;
	if(IR_SHRO && (MB & B15)) shro(pdp);
	if(IR_OPR && (MB & B8) || IR_LAW && (MB & B5)) AC ^= WORDMASK;
	if(IR_IOT && !pdp->ihs && pdp->ios) pdp->ioh = 0;
	if(IR_OPR && (MB & B9) ||
	   IR_INCORR ||
	   pdp->single_cyc_sw ||
	   pdp->single_inst_sw && CY0_INST_DONE ||
	   !pdp->run_enable)
		pdp->run = 0;
	clrmd(pdp);
	TP(9)

	// TP9A
	if(IR_SHRO && (MB & B14)) shro(pdp);
	if(IR_IOT && pdp->ioh) inst_cancel(pdp);
	TP(9a)

	// TP10
	sbs_reset_sync(pdp);
	memclr(pdp);
	if(pdp->run) clr_ma(pdp);
	if(pdp->df1 || pdp->ir < 030) pdp->cyc = 1;
	if(pdp->sbm && pdp->req) {
		if(CY0_INST_DONE || CY0_MIDBRK_PERMIT) {
			pdp->cyc = 1;
			pdp->bc |= 1;
			if(CY0_MIDBRK_PERMIT) inst_cancel(pdp);
assert(pdp->cyc && pdp->bc==1 && !pdp->df1 && !pdp->df2);
		}
	}
	if(IR_SHRO && (MB & B13)) shro(pdp);
	if(IR_IOT) {
		if(pdp->ihs) pdp->ioh = 1;
		else if(!pdp->ioh) pdp->ios = 0;
		if(pdp->ioc) iot(pdp, 1);
	}
	if(MB & B0) pdp->smb = 1;
#ifdef LAILIA
	if(pdp->lai) MB = 0;
#endif
	TP(10)
	}
}

static void
defer(PDP1 *pdp)
{
	int sbs_restore = 0;

	// TP0
	mb_to_ma(pdp);
	TP(0)

	// TP1
	pdp->emc = 0;
	TP(1)

	// TP2
	mop2379(pdp);
	if(pdp->sbm && IR_JMP && pdp->epc == 0) {
		if(pdp->sbs16) {
			if((MB & 07703) == 1) {
				pdp->b4 &= ~(1<<((MB&074)>>2));
				pdp->exd = 1;
				sbs_restore = 1;
			}
		} else {
			if((MB & 07777) == 1) {
				pdp->b3 = 0;
				pdp->b4 = 0;
				pdp->exd = 1;
				sbs_restore = 1;
			}
		}
		sbs_calc_req(pdp);
	}
	TP(2)

	// TP3
	mop2379(pdp);
	MB = 0;
	TP(3)

	// TP4
	sbs_sync(pdp);
	readmem(pdp);
	TP(4)

	// TP5
	if(pdp->exd) pdp->emc = 1;
	TP(5)

	if(MB & B5 && !pdp->exd) {
		// TP6
		pdp->df2 = 1;
		TP(6)
		TP(6a)
		mop2379(pdp);
		TP(7)
		inhibit(pdp);
		TP(8)
	} else {
		TP(6)
		TP(6a)

		// TP7
		if(IR_JSP) AC = 0;
		mop2379(pdp);
		TP(7)

		// TP8
		inhibit(pdp);
		if(IR_JSP) pc_to_ac(pdp), clr_pc(pdp);
		if(IR_JMP) clr_pc(pdp);
		TP(8)

		// TP9
		if(IR_JSP || IR_JMP) mb_to_pc(pdp);
		clrmd(pdp);
	}
	// TP9
	mop2379(pdp);
	writemem(pdp);		// approximate
	if(IR_INCORR ||
	   pdp->single_cyc_sw ||
	   pdp->single_inst_sw && DF_INST_DONE ||
	   !pdp->run_enable)
		pdp->run = 0;
	TP(9)

	// 3.5us after TP2, shortly before TP9A
	if(sbs_restore) {
		pdp->ov1 = !!(MB & B0);
		pdp->exd = !!(MB & B1);
	}
	TP(9a)

	// TP10
	sbs_reset_sync(pdp);
	memclr(pdp);
	if(pdp->run) clr_ma(pdp);
	if(!pdp->df2) {
		pdp->df1 = 0;
		if(pdp->ir >= 030) pdp->cyc = 0;
	}
	if(pdp->sbm && pdp->req) {
		if(DF_INST_DONE || DF_MIDBRK_PERMIT) {
			pdp->cyc = 1;
			pdp->bc |= 1;
			if(DF_MIDBRK_PERMIT) inst_cancel(pdp);
assert(pdp->cyc && pdp->bc==1 && !pdp->df1 && !pdp->df2);
		}
	}
	pdp->df2 = 0;
	if(MB & B0) pdp->smb = 1;
	TP(10)
}

static void
cycle1(PDP1 *pdp)
{
	int hack = pdp->cychack;
	pdp->cychack = 0;
	switch(hack) {
	default:
	// TP0
	if(IR_CALJDA && !(MB & B5)) {
		MA |= 0100;
		if(!pdp->exd)
			pdp->ema |= pdp->epc;
	} else
		mb_to_ma(pdp);
	// EMA stuff
	if(IR_DIS)
		div_shift(pdp);
	TP(0)

	case 1:
	// TP1
	pdp->emc = 0;
	TP(1)

	// TP2
	mop2379(pdp);
	if(IR_DIS && !(IO & B17)) {
		if(AC == 0777777) AC = 1;
		else AC++;
	}
	if(IR_MUL) {
		MB = AC;
		IO = 0;
	}
	TP(2)

	// TP3
	mop2379(pdp);
	if(IR_MUL)
		IO |= MB;
	MB = 0;
	if(IR_XCT) {
		pdp->cyc = 0;
		pdp->cychack = 4;
		cycle0(pdp);
		return;
	}
	TP(3)

	// TP4
	sbs_sync(pdp);
	readmem(pdp);
	if(IR_SUB || IR_DIS && (IO & B17)) AC ^= WORDMASK;
	if(IR_LIO) IO = 0;
	TP(4)

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
	TP(5)

	// TP6
	if(IR_ADD || IR_SUB || IR_DIS || IR_MUS && (IO & B17))
		carry(pdp);
	if(IR_IDX || IR_ISP)
		inc_ac(pdp);
	TP(6)
	TP(6a)

	// TP7
	mop2379(pdp);
	if(IR_DAC || IR_IDX || IR_ISP) MB = AC;
	if(IR_CALJDA) MB = AC, AC = 0;
	if(IR_DAP) MB = MB&0770000 | AC&0007777;
	if(IR_DIP) MB = MB&0007777 | AC&0770000;
	if(IR_DIO) MB = IO;
	TP(7)

	// TP8
	inhibit(pdp);
	if(IR_MUS)
		mul_shift(pdp);
	if(IR_CALJDA) pc_to_ac(pdp), clr_pc(pdp);
	if(IR_SAS && AC==0 || IR_SAD && AC!=0 ||
	   IR_ISP && !(AC & B0))
		pc_inc(pdp);
	TP(8)

	// TP9
	mop2379(pdp);
	writemem(pdp);		// approximate
	if(IR_CALJDA) ma_to_pc(pdp);
	if(IR_SUB || IR_DIS && (IO & B17)) AC ^= WORDMASK;
	if(IR_SAD || IR_SAS) AC ^= MB;
	if((IR_ADD || IR_SUB) && (AC&B0) == (MB&B0)) pdp->ov2 = 0;
	if(IR_INCORR ||
	   pdp->single_cyc_sw ||
	   pdp->single_inst_sw ||
	   !pdp->run_enable)
		pdp->run = 0;
	clrmd(pdp);
	TP(9)

	// TP9A
	if((IR_ADD || IR_DIS) && AC == 0777777) AC = 0;
	if(IR_CALJDA) pc_inc(pdp);
	TP(9a)

	// TP10
	sbs_reset_sync(pdp);
	memclr(pdp);
	if(pdp->ov2) pdp->ov1 = 1;
	pdp->ov2 = 0;
	pdp->cyc = 0;
	if(pdp->run) clr_ma(pdp);
	if(MB & B0) pdp->smb = 1;
	if(IR_MUL) {
		if(MB & B0)
			MB ^= WORDMASK;
		if(IO & B0) {
			IO ^= WORDMASK;
			pdp->srm = 1;
		}
		pdp->scr |= 1;
		AC = 0;
		multiply(pdp);
		// without delay to TP0
		pdp->simtime -= 200;
	}
	if(IR_DIV) {
		if(!(MB & B0))
			MB ^= WORDMASK;
		if(AC & B0) {
			AC ^= WORDMASK;
			IO ^= WORDMASK;
			pdp->srm = 1;
		}
		divide(pdp);
		// without delay to TP0
		pdp->simtime -= 200;
	}
	TP(10)
	}
}

static void
brkcycle(PDP1 *pdp)
{
	// TP0
	if(IR_SHRO && (MB & B12)) shro(pdp);
	if(pdp->bc == 1 && pdp->sbs16) {
		int be = 0;
		for(int r = pdp->req; !(r&1); r >>= 1)
			be++;
		MA |= be<<2;
	}
	if(pdp->bc == 2 || pdp->bc == 3) pc_to_ma(pdp);
	TP(0)

	// TP1
	if(IR_SHRO && (MB & B11)) shro(pdp);
	pdp->emc = 0;
	TP(1)

	// TP2
	mop2379(pdp);
	if(IR_SHRO && (MB & B10)) shro(pdp);
	if(IR_IOT) pdp->ioc = !pdp->ioh && !pdp->ihs;
	pdp->ihs = 0;
	TP(2)

	// TP3
	mop2379(pdp);
	if(IR_SHRO && (MB & B9)) shro(pdp);
	MB = 0;
	TP(3)

	// TP4
	if(pdp->bc == 1) hold_break(pdp);
	sbs_sync(pdp);
	readmem(pdp);
	if(pdp->bc == 1) IR = 0;
	TP(4)

	// TP5
	if(pdp->bc == 3) MB = 0;
	TP(5)

	// TP6
	TP(6)
	TP(6a)

	// TP7
	mop2379(pdp);
	if(pdp->bc == 1) MB = AC, AC = 0;
	if(pdp->bc == 2) MB = AC;
	if(pdp->bc == 3) MB |= IO;
	TP(7)

	// TP8
	inhibit(pdp);
	if(pdp->bc == 1) pc_to_ac(pdp), clr_pc(pdp);
	TP(8)

	// TP9
	mop2379(pdp);
	writemem(pdp);		// approximate
	if(pdp->bc == 1) ma_to_pc(pdp);
	if(pdp->single_cyc_sw ||
	   !pdp->run_enable)
		pdp->run = 0;
	clrmd(pdp);
	TP(9)

	// TP9A
	pc_inc(pdp);
	TP(9a)

	// TP10
	sbs_reset_sync(pdp);
	memclr(pdp);
	if(pdp->run) clr_ma(pdp);
	if(MB & B0) pdp->smb = 1;
	if(pdp->bc == 3) pdp->cyc = 0;
	pdp->bc = (pdp->bc+1) & 3;
	TP(10)
}

void
cycle(PDP1 *pdp)
{
// TODO: these can be wrong after power on
// how do we handle that?
//	assert(pdp->cyc || pdp->bc==0);
//	assert(!pdp->df1 || pdp->bc==0);

	pdp->timernd = rand() % TP_unreachable;
	// a cycle takes 5μs
	if(pdp->bc) brkcycle(pdp);
	else if(!pdp->cyc) cycle0(pdp);
	else if(pdp->df1) defer(pdp);
	else cycle1(pdp);
}

void
throttle(PDP1 *pdp)
{
	while(pdp->realtime < pdp->simtime) {
		usleep(1000);
		pdp->realtime = gettime();
	}
}
}

// pulse=0: TP7
// pulse=1: TP10
static void
iot_pulse(PDP1 *pdp, int pulse, int dev, int nac)
{
	int ch;
	ch = (MB>>6)&077;
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
			pdp->dint = 0;
		} else {
			pdp->dcp = nac;
			pdp->dbx |= AC>>8;
			pdp->dby |= IO>>8;
			pdp->dint |= (MB>>6)&7;
			pdp->dpy_defl_time = pdp->simtime + US(35);
			pdp->dpy_time = pdp->dpy_defl_time + US(15);
		}
		break;

	case 011:	// spacewar controllers
		// simple but stupid version for now
		if(pulse) {
			// LRTF
			IO |= pdp->spcwar1<<14 | pdp->spcwar2;
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

	case 050:	// dsc
		if(!pulse) {
			if(pdp->sbs16 && ch < 16)
				pdp->b1 &= ~(1<<ch);
		}
		break;
	case 051:	// asc
		if(!pulse) {
			if(pdp->sbs16 && ch < 16)
				pdp->b1 |= (1<<ch);
		}
		break;
	case 052:	// isb
		if(!pulse) {
			if(pdp->sbs16 && ch < 16)
				pdp->b2 |= (1<<ch);
		}
		break;
	case 053:	// cac
		if(!pulse) {
			if(pdp->sbs16)
				pdp->b1 = 0;
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
	case 056:	// cbs
		if(!pulse) {
			clr_sbs(pdp);
		}
		break;

	case 074:	// lem/eem
		if(pulse) {
			pdp->exd = !!(MB & B6);
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

static void
req(PDP1 *pdp, int chan)
{
	if(pdp->sbs16)
		pdp->b2 |= pdp->b1 & (1<<chan);
	else
		pdp->b2 = 1;
}

void
flushdpy(DispCon *d)
{
	int sz = d->ncmds*sizeof(d->cmdbuf[0]);
	int n = write(d->fd, d->cmdbuf, sz);
	d->ncmds = 0;
	if(n < sz) {
		close(d->fd);
		d->fd = -1;
	}
}

void
dpycmd(PDP1 *pdp, int i, u32 cmd)
{
	DispCon *d = &pdp->dpy[i];
	d->cmdbuf[d->ncmds++] = cmd;
	if(d->ncmds == nelem(d->cmdbuf))
		flushdpy(d);
}

void
agedisplay(PDP1 *pdp, int i)
{
	DispCon *d = &pdp->dpy[i];
	if(d->fd < 0)
		return;
	int ival = d->agetime;
	int cmd = ival<<23;
	assert(d->last <= pdp->simtime);
	u64 dt = (pdp->simtime - d->last)/1000;
	if(dt >= ival) {
		dpycmd(pdp, i, 511<<23);
		// TODO? theoretically dt could be huge,
		// but if it is you have other problems
		dpycmd(pdp, i, dt);
		d->last = pdp->simtime;
		flushdpy(d);

		// increase interval during fade out
		// to reduce number of age-commands
		if(d->agetime < 1000*1000)
			d->agetime += d->agetime/6;
	}
}

void
display(PDP1 *pdp, int i)
{
	// need to make sure dt field doesn't overflow cmd
	pdp->dpy[i].agetime = 510;
	agedisplay(pdp, i);
	// reset age interval for every point shown
	pdp->dpy[i].agetime = 50*1000;
	if(pdp->dpy[i].fd < 0)
		return;
	int x = pdp->dbx;
	int y = pdp->dby;
	int dt = (pdp->simtime - pdp->dpy[i].last)/1000;
	if(x & 01000) x++;
	if(y & 01000) y++;
	x = (x+01000)&01777;
	y = (y+01000)&01777;
	int cmd = x | (y<<10) | (dt<<23);
	int in = (pdp->dint+4)&7;
	int twoscreens = pdp->dpy[0].fd >= 0 && pdp->dpy[1].fd >= 0;
	if(twoscreens) {
		// probably wrong
		cmd |= (in&3)<<20;
		if(!!(pdp->dint&4) != i)
			return;
	} else {
		cmd |= in<<20;
	}

	pdp->dpy[i].last = pdp->simtime;
	dpycmd(pdp, i, cmd);
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
		// write back in case this is over a socket
		// and we need to synchronize
		write(pdp->r_fd, &c, 1);
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
				// not sure about this, but seems annoying
				if(!pdp->rim)
					req(pdp, RD_CHAN);
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
		req(pdp, PUN_CHAN);
	} else if(pdp->tape_feed && pdp->feed_time < pdp->simtime) {
		pdp->feed_time = pdp->simtime + PDLY;
		if(pdp->p_fd >= 0) {
			char c = 0;
			write(pdp->p_fd, &c, 1);
		}
	}

	/* Typewriter */
	if(pdp->typ_time < pdp->simtime) {
		// wrong timing
		pdp->typ_time = NEVER;
		if((pdp->tb&076) == 034)
			pdp->tbb = pdp->tb & 1;
		else if(pdp->typ_fd.fd >= 0) {
			char c = (pdp->tbb<<6) | pdp->tb;
			write(pdp->typ_fd.fd, &c, 1);
		}
		// this is really much more complicated
		// and overlaps with the type-in logic
		pdp->tyo = 0;
		if(pdp->tcp) pdp->ios = 1;
		req(pdp, TTO_CHAN);
	}
	if(pdp->tyi_wait < pdp->simtime && pdp->typ_fd.ready) {
		char c;
		if(read(pdp->typ_fd.fd, &c, 1) <= 0) {
			closefd(&pdp->typ_fd);
			pdp->typ_fd.fd = -1;
			return;
		}
		waitfd(&pdp->typ_fd);
if(pdp->pf & 040) printf("	char missed <%o>\n", pdp->tb);
		pdp->tb = 0;
		// STROBE TYPE
		pdp->tb |= c & 077;
		//
		pdp->tbs = 1;
		// TYPE SYNC
		pdp->pf |= 040;
		req(pdp, TTI_CHAN);

		// PDP-1 has to keep up, so avoid clobbering TB
		// not sure what a good timeout here is
		pdp->tyi_wait = pdp->simtime + 25000000;
	}

	/* Display */
	if(pdp->dpy_defl_time < pdp->simtime) {
		pdp->dpy_defl_time = NEVER;
		display(pdp, 0);
		display(pdp, 1);
	}
	if(pdp->dpy_time < pdp->simtime) {
		pdp->dpy_time = NEVER;
		if(pdp->dcp) pdp->ios = 1;
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

		char *resp = handlecmd(pdp, line);
		printf("%s\n", resp);
	}
}

char*
handlecmd(PDP1 *pdp, char *line)
{
	int n;
	static char resp[1024];
	char *p;

	if(p = strchr(line, '\r'), p) *p = '\0';
	if(p = strchr(line, '\n'), p) *p = '\0';

	char **args = split(line, &n);

	strcpy(resp, "ok");
	if(n > 0) {
		// reader
		if(strcmp(args[0], "r") == 0) {
			close(pdp->r_fd);
			pdp->r_fd = -1;
			if(args[1]) {
				pdp->r_fd = open(args[1], O_RDONLY);
				if(pdp->r_fd < 0)
					sprintf(resp, "couldn't open %s", args[1]);
			}
		}
		// punch
		else if(strcmp(args[0], "p") == 0) {
			close(pdp->p_fd);
			pdp->p_fd = -1;
			if(args[1]) {
				pdp->p_fd = open(args[1], O_CREAT|O_WRONLY|O_TRUNC, 0644);
				if(pdp->p_fd < 0)
					sprintf(resp, "couldn't open %s", args[1]);
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
					sprintf(resp, "couldn't open %s", rimfile);
				} else {
					readrim(pdp, fd);
					close(fd);
				}
			} else
				sprintf(resp, "no filename");
		}
		// display
		else if(strcmp(args[0], "d") == 0) {
			static const char *host = "localhost";
			static int port = 3400;
			if(args[1])
				host = args[1];
			if(args[2])
				port = atoi(args[2]);

			if(pdp->dpy[0].fd >= 0)
				close(pdp->dpy[0].fd);
			pdp->dpy[0].last = pdp->simtime;
			pdp->dpy[0].fd = dial(host, port);
			if(pdp->dpy[0].fd < 0)
				strcpy(resp, "can't open display");
			else
				nodelay(pdp->dpy[0].fd);
		}
		// help
		else if(strcmp(args[0], "?") == 0 ||
			strcmp(args[0], "help") == 0) {
			p = resp;
			p += sprintf(p, "r                     unmount tape from reader\n");
			p += sprintf(p, "r filename            mount tape in reader\n");
			p += sprintf(p, "p                     unmount tape from punch\n");
			p += sprintf(p, "p filename            mount tape in punch\n");
			p += sprintf(p, "l filename            load memory from RIM-file\n");
			p += sprintf(p, "d [host] [port]       connect to display program\n");
			p += sprintf(p, "muldiv [on/off]       set/toggle type 10 mul-div option");
			p += sprintf(p, "audio [on/off]        set/toggle audio output");
		}
		else if(strcmp(args[0], "muldiv") == 0) {
			if(args[1]) {
				if(strcmp(args[1], "on") == 0 ||
				   strcmp(args[1], "1") == 0)
					pdp->muldiv_sw = 1;
				else if(strcmp(args[1], "off") == 0 ||
				   strcmp(args[1], "0") == 0)
					pdp->muldiv_sw = 0;
			} else
				pdp->muldiv_sw = !pdp->muldiv_sw;
			sprintf(resp, "mul-div now %s", pdp->muldiv_sw ? "on" : "off");
		}
		else if(strcmp(args[0], "audio") == 0) {
			if(args[1]) {
				if(strcmp(args[1], "on") == 0 ||
				   strcmp(args[1], "1") == 0)
					doaudio = 1;
				else if(strcmp(args[1], "off") == 0 ||
				   strcmp(args[1], "0") == 0)
					doaudio = 0;
			} else
				doaudio = !doaudio;
			sprintf(resp, "audio now %s", doaudio ? "on" : "off");
		}
	}

	free(args[0]);
	free(args);

	return resp;
}
