#include "common.h"
#include "pdp5.h"
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>


enum {
	B0 = 04000,
	B1 = 02000,
	B2 = 01000,
	B3 = 00400,
	B4 = 00200,
	B5 = 00100,
	B6 = 00040,
	B7 = 00020,
	B8 = 00010,
	B9 = 00004,
	B10 = 00002,
	B11 = 00001
};

#define KEY_EX_DP (pdp->key_examine || pdp->key_deposit)
#define KEY_ST_EX_DP (pdp->key_start || KEY_EX_DP)
#define KEY_MANUAL (pdp->key_continue || pdp->key_load_address || KEY_ST_EX_DP)
#define RUN_STOP (pdp->key_stop ||\
	KEY_EX_DP ||\
	pdp->sw_single_step ||\
	pdp->sw_single_instruction && pdp->c.state == MAJ_P)
#define IR_AND ((pdp->c.ir>>1) == 0)
#define IR_TAD ((pdp->c.ir>>1) == 1)
#define IR_ISZ ((pdp->c.ir>>1) == 2)
#define IR_DCA ((pdp->c.ir>>1) == 3)
#define IR_JMS ((pdp->c.ir>>1) == 4)
#define IR_JMP ((pdp->c.ir>>1) == 5)
#define IR_IOT ((pdp->c.ir>>1) == 6)
#define IR_OPR ((pdp->c.ir>>1) == 7)
#define IR_OP1 (pdp->c.ir == 016)
#define IR_OP2 (pdp->c.ir == 017)
#define IR_I (pdp->c.ir & 1)
#define ZP (!(pdp->c.mb & 0200))
#define IR_2CYCLE_INST ((pdp->c.ir&010) == 0 || IR_JMS)

#define MB (pdp->c.mb)
#define AC (pdp->c.ac)
#define MA (MA_(pdp))
#define NMB (pdp->n.mb)
#define NAC (pdp->n.ac)
#define NMA (pdp->n.ma)
#define ST (pdp->c.state)
#define NST (pdp->n.state)

#define CLEAR_AC (NAC &= 010000)
#define INDEX_AC (NAC = (pdp->n.ac+1) & 017777)
#define CLEAR_L (NAC &= 07777)
#define CLEAR_MA NMA = 0
#define CLEAR_MA_0_4 NMA &= 00177
#define COUNT_MA NMA = (pdp->c.ma+1) & WD
#define MA_JM_MB_5_11 NMA = pdp->n.ma&07600 | MB&00177
#define MA_JM_MB_0_4 NMA = pdp->n.ma&00177 | MB&07600
#define MB_JM_MA_0_4 NMB = pdp->n.mb&00177 | MA&07600

char *statenames[] = { "P", "F", "D", "E1", "E2", "B" };

int dotrace = 0;
void
trace(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if(dotrace){
		fprintf(stdout, "  ");
		vfprintf(stdout, fmt, ap);
	}
	va_end(ap);
}


void
tracestate(PDP5 *pdp)
{
	trace("\tL/%o AC/%04o MB/%04o MA/%04o.%d IR/%o PC/%04o RUN/%d\n",
		(AC>>12)&1, AC&07777, MB,
		MA, pdp->disable_ma, pdp->c.ir, pdp->core[0],
		pdp->run);
}

void
set_mb(PDP5 *pdp, Word mb)
{
	NMB = mb & WD;
	if(ST == MAJ_F && !pdp->int_ack)
		pdp->n.ir |= (NMB & ~MB)>>8;
	if(ST == MAJ_E1 && IR_ISZ)
		pdp->skip ^= !!(~NMB & MB & B0);
}

void
count_mb(PDP5 *pdp)
{
	set_mb(pdp, MB+1+pdp->skip);
}

void
rar(PDP5 *pdp)
{
	NAC >>= 1;
	NAC |= (AC<<12) & 010000;
}

void
ral(PDP5 *pdp)
{
	NAC = (NAC<<1) & 017777;
	NAC |= (AC>>12) & 1;
}

void nulliot(PDP5*, int, void*) { }

static void
iop(PDP5 *pdp, int p)
{
	trace("IOP%d\n", p);
	int d = MB>>3 & 077;
	pdp->devtab[d].iot(pdp, p, pdp->devtab[d].dev);
}

static void
tp6(PDP5 *pdp)
{
	trace("TP6 %s\n\n", statenames[ST]);

	if(RUN_STOP) pdp->run = 0;

	switch(ST){
	case MAJ_P:
		NST = MAJ_F;
		break;
	case MAJ_F:
		if(IR_OPR)
			NST = pdp->brk_req ? MAJ_B : MAJ_P;
		else if(IR_IOT){
			if(MB & B9) iop(pdp, 4);
			NST = pdp->brk_req ? MAJ_B : MAJ_P;
		}else{
			if(IR_I)
				NST = MAJ_D;
			else if(IR_2CYCLE_INST)
				NST = MAJ_E1;
			else
				NST = MAJ_P;
		}
		break;
	case MAJ_D:
		if(IR_2CYCLE_INST) NST = MAJ_E1;
		else NST = MAJ_P;
		break;
	case MAJ_E1:
		if(IR_TAD){
			NAC += (~AC & MB)<<1;
			NAC &= 017777;
		}
		if(IR_JMS || KEY_EX_DP) NST = MAJ_E2;
		else NST = pdp->brk_req ? MAJ_B : MAJ_P;
		break;
	case MAJ_E2:
		pdp->int_ack = 0;
		NST = MAJ_P;
		break;
	case MAJ_B:
		NST = pdp->brk_req ? MAJ_B : MAJ_P;
		break;
	}
}

static void
tp5(PDP5 *pdp)
{
	trace("TP5 %s\n", statenames[ST]);

	switch(ST){
	case MAJ_P:
		pdp->n.ir = 0;
		if(pdp->interrupts && pdp->int_delay){
			pdp->int_ack = 1;
			pdp->int_enable = 0;
			pdp->int_delay = 0;
		}
		break;
	case MAJ_F:
		if(IR_OP1){
			if(MB & B6) NAC ^= 07777;
			if(MB & B7) NAC ^= 010000;
			if(MB & B8) rar(pdp);
			if(MB & B9) ral(pdp);
		}else if(IR_OP2){
			if(MB & B8) pdp->skip ^= 1;
			if(MB & B9) NAC |= pdp->c.sr;
			if(MB & B10) pdp->run = 0;
		}else if(IR_IOT){
			if(MB & B10) iop(pdp, 2);
		}
		break;
	case MAJ_B:
		pdp->n.ir = 0;
		break;
	}
}

static void
tp4(PDP5 *pdp)
{
	trace("TP4 %s\n", statenames[ST]);

	switch(ST){
	case MAJ_P:
		if(!IR_JMP) count_mb(pdp);
		break;
	case MAJ_F:
		if(IR_OP1){
			if(MB & B4) CLEAR_AC;
			if(MB & B5) CLEAR_L;
		}else if(IR_OP2){
			if(MB & B4) CLEAR_AC;
			if(MB & B5 && AC & B0 ||
			   MB & B6 && (AC & 07777) == 0 ||
			   MB & B7 && AC & 010000)
				pdp->skip ^= 1;
		}else if(IR_IOT){
			if(MB & B11) iop(pdp, 1);
		}
		break;
	case MAJ_D:
		if((MA & 07770) == 00010) count_mb(pdp);
		break;
	case MAJ_E1:
		if(IR_AND) NAC &= MB | 010000;
		if(IR_TAD) NAC ^= MB;
		if(IR_ISZ) count_mb(pdp);
		break;
	case MAJ_E2:
		if(!pdp->int_ack) count_mb(pdp);
		break;
	}
}

static void
tp3(PDP5 *pdp)
{
	trace("TP3 %s\n", statenames[ST]);

	pdp->brk_req = 0;
	switch(ST){
	case MAJ_F:
		if(pdp->int_ack)
			pdp->n.ir |= 010;	// JMS
		if(pdp->int_enable)
			pdp->int_delay = 1;
		break;
	case MAJ_E1:
		if(IR_DCA){
			NMB = AC & WD;
			CLEAR_AC;
		}
		break;
	}
}

static void
tp1(PDP5 *pdp)
{
	trace("TP1 %s\n", statenames[ST]);

	if(KEY_EX_DP)
		COUNT_MA;
	if(IR_OP1 && MB & B11)
		INDEX_AC;
	if(IR_OP1 && MB & B10){
		if(MB & B8) rar(pdp);
		if(MB & B9) ral(pdp);
	}
	switch(ST){
	case MAJ_P:
		if(IR_JMP && !IR_I && !ZP) MB_JM_MA_0_4;
		if(IR_JMS) NMB = MA;
		break;
	case MAJ_F:
		MA_JM_MB_0_4;
		MA_JM_MB_5_11;
		pdp->skip = 0;
		break;
	case MAJ_D:
		MA_JM_MB_5_11;
		if(ZP)
			CLEAR_MA_0_4;
		break;
	case MAJ_E1:
		if(pdp->int_ack)
			CLEAR_MA;
		else
			MA_JM_MB_5_11;
		if(IR_I)
			MA_JM_MB_0_4;	// INT ACK not possible
		else if(ZP)
			CLEAR_MA_0_4;
		break;
	case MAJ_E2:
		if(pdp->int_ack)
			COUNT_MA;
		break;
	}
	pdp->disable_ma = ST == MAJ_P ||
		ST == MAJ_E1 && IR_JMS;
	if(!(ST == MAJ_P && (IR_JMP | IR_JMS)) &&
	   ST != MAJ_E2)
		NMB = 0;
	if(ST == MAJ_P && !IR_JMS && !IR_I && ZP)
		NMB &= 0177;
}

/* special pulses */

static void
sp3(PDP5 *pdp)
{
	trace("SP3\n");
	if(pdp->key_continue || KEY_ST_EX_DP)
		pdp->run = 1;
	if(KEY_ST_EX_DP){
		pdp->int_delay = 0;
		pdp->int_enable = 0;
	}
	if(pdp->key_start){
		NMB = MA;
		pdp->disable_ma = 1;
		CLEAR_L;
	}
	if(pdp->key_deposit)
		NAC |= pdp->c.sr;

	pdp->c = pdp->n;
}

static void
sp2(PDP5 *pdp)
{
	trace("SP2\n");
	if(pdp->key_load_address)
		NMA |= pdp->c.sr;
	if(KEY_ST_EX_DP){
		CLEAR_AC;
		pdp->n.ir |= 2;
	}
	if(pdp->key_start)
		pdp->n.ir |= 010;	// JMP with 2 above
	if(pdp->key_deposit)
		pdp->n.ir |= 4;	// DCA with 2 above
	if(pdp->key_start)
		NST = MAJ_P;
	if(KEY_EX_DP)
		NST = MAJ_E1;

	pdp->c = pdp->n;
	sp3(pdp);
}

static void
sp1(PDP5 *pdp)
{
	trace("SP1\n");
	if(pdp->key_load_address)
		NMA = 0;
	if(KEY_ST_EX_DP){
		pdp->int_ack = 0;
		pdp->n.ir = 0;
		NMB = 0;
	}
	if(KEY_EX_DP || pdp->key_load_address)
		pdp->disable_ma = 0;
	if(pdp->key_continue)
		tp1(pdp);

	pdp->c = pdp->n;
	sp2(pdp);
}

static void
sp0(PDP5 *pdp)
{
	trace("SP0\n");
	pdp->run = 0;
	pdp->io_hlt = 0;

	pdp->simtime += 10000;
	sp1(pdp);
}

void
pwrclr(PDP5 *pdp)
{
	trace("PWR CLR\n");
	pdp->run = 0;
	pdp->io_hlt = 0;

	// randomize flip flops
	pdp->c.state = rand() % 6;
	pdp->disable_ma = 0;	// not actually done
	pdp->tg = 0;
	pdp->n.ac = rand() & 017777;
	pdp->n.mb = rand() & 07777;
	pdp->n.ma = rand() & 07777;
	pdp->n.ir = rand() & 7;
	pdp->c = pdp->n;
}

static void
pdp_iot(PDP5 *pdp, int p, void *d)
{
	switch(p) {
	case 1:
		pdp->int_enable = 1;
		break;

	case 2:
		pdp->int_enable = 0;
		pdp->int_delay = 0;
		break;
	}
}

void
setintr(PDP5 *pdp, int dev, int i)
{
	if(pdp->devtab[dev].intr != i){
		pdp->devtab[dev].intr = i;
		pdp->interrupts = 0;
		for(dev = 0; dev < 0100; dev++)
			pdp->interrupts |= pdp->devtab[dev].intr;
	}
}

void
pdpinit(PDP5 *pdp)
{
	int i;
	pdp->interrupts = 0;
	for(i = 0; i < 0100; i++){
		pdp->devtab[i].dev = nil;
		pdp->devtab[i].iot = nulliot;
		pdp->devtab[i].cyc = nil;
		pdp->devtab[i].intr = 0;
	}
	pdp->devtab[0].iot = pdp_iot;
}

enum {
	TG3 = 1,
	TG2 = 2,	// read
	TG1 = 4,	// inhibit
	TG0 = 8		// write
};

void
tick(PDP5 *pdp)
{
	int tg = pdp->tg;
	int ptg = tg;

	// read core
	if(tg & TG2){
		if(pdp->c.state == MAJ_E1 && IR_DCA ||
		   pdp->c.state == MAJ_E2 ||
		   pdp->c.state == MAJ_P && (IR_JMP || IR_JMS) ||
		   pdp->c.state == MAJ_B && 0)	// TODO
			;	// don't strobe
		else
			set_mb(pdp, MB | pdp->core[MA]);
		pdp->core[MA] = 0;
	}
	// write core
	if(tg & TG0) pdp->core[MA] |= MB;
	pdp->c = pdp->n;

	// timing
	if(pdp->run || tg & (TG0 | TG1 | TG2)) {
		if(tg&TG3) {
			// TG3 falling
			if(!(tg&TG1)) {
				if(tg&TG2) tg ^= TG1;
				tg ^= TG2;
			}
		} else {
			// TG3 rising
			if(tg&TG0) tg &= TG3;
			else if(tg&TG1) tg ^= TG0;
		}
		tg ^= TG3;
	}
	pdp->tg = tg;

	int e = tg ^ ptg;
	int re = tg & e;
	int fe = ~tg & e;

	if(fe&TG1 && (pdp->run || pdp->io_hlt || KEY_EX_DP)) tp1(pdp);
	if(re&TG3 && ptg&TG2) tp3(pdp);
	if(fe&TG2) tp4(pdp);
	if(re&TG0) tp5(pdp);
	if(fe&TG3 && ptg&TG0) tp6(pdp);
	pdp->c = pdp->n;

	pdp->n.key_manual = KEY_MANUAL;
	if(pdp->n.key_manual && !pdp->c.key_manual)
		sp0(pdp);
}

void
throttle(PDP5 *pdp)
{
	while(pdp->realtime < pdp->simtime) {
		usleep(1000);
		pdp->realtime = gettime();
	}       
}               


static void
tto_iot(PDP5 *pdp, int p, void *d)
{
	TTY *tty = d;

	switch(p) {
	case 1:
		if(tty->oflag) pdp->skip = 1;
		break;

	case 2:
		tty->oflag = 0;
		setintr(pdp, 4, 0);
		break;

	case 4:
		tty->luo |= pdp->c.ac & 0377;
		write(tty->fd.fd, &tty->luo, 1);
		tty->luo = 0;
		tty->oflag = 1;
		setintr(pdp, 4, 1);
		break;
	}
}

static void
tti_iot(PDP5 *pdp, int p, void *d)
{
	TTY *tty = d;

	switch(p) {
	case 1:
		if(tty->iflag) pdp->skip = 1;
		break;

	case 2:
		tty->oflag = 0;
		tty->iflag = 0;
		pdp->n.ac = 0;
		setintr(pdp, 3, 0);
		break;

	case 4:
		pdp->n.ac |= tty->lui;
		break;
	}
}

void
tti_cycle(PDP5 *pdp, void *d)
{
	TTY *tty = d;

	if(!tty->iflag)
		if(tty->fd.ready){
			read(tty->fd.fd, &tty->lui, 1);
			tty->iflag = 1;
			setintr(pdp, 3, tty->iflag);
			waitfd(&tty->fd);
		}
}

void
attach_tty(PDP5 *pdp, TTY *tty)
{
	tty->fd.id = -1;
	tty->fd.fd = 1;
	tty->luo = 0;
	tty->oflag = 0;
	pdp->devtab[03].dev = tty;
	pdp->devtab[03].iot = tti_iot;
	pdp->devtab[03].cyc = tti_cycle;
	pdp->devtab[04].dev = tty;
	pdp->devtab[04].iot = tto_iot;
}

void
handleio(PDP5 *pdp)
{
	int dev;
	for(dev = 0; dev < 0100; dev++)
		if(pdp->devtab[dev].cyc)
			pdp->devtab[dev].cyc(pdp, pdp->devtab[dev].dev);
}
