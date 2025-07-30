#include "common.h"
#include "whirlwind.h"
//#include "panel_b18.h"
#include "args.h"

#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <signal.h>

typedef struct Panel Panel;
void updateswitches(Whirlwind *ww, Panel *panel);
void updatelights(Whirlwind *ww, Panel *panel);
void lightsoff(Panel *panel);
Panel *getpanel(void);

/*
    logic and schematics:
	R-127_Whirlwind_I_Computer_Block_Diagrams_Volume_1_Sep47.pdf
	R-127_Whirlwind_I_Computer_Block_Diagrams_Volume_2_Sep47.pdf
	R-177_A_Method_of_Test_Checking_an_Electronic_Digital_Computer_Mar1950.pdf
	R-221_Whirlwind_I_Operational_Logic_May54.pdf
*/

static void nullIO(Whirlwind *ww, ExUnit *eu) {}
ExUnit nullUnit = { 0, nullIO, nullIO, nullIO, nullIO };


// IOS decoded:
// from E-466-1_Operation_of_the_In-Out_Element_Feb54.pdf
#define C01	(1<<0)		// all output units
#define C02	(1<<1)		// paper tape automatic
#define C03	(1<<2)		// all scopes
#define C04	(1<<3)		// magnetic drum read
#define C05	(1<<4)		// all readers, mt unix, cmaera, buffer drum
				// switch field, aux drum change group, or output coder
#define C06	(1<<5)		// paper tape automatic
#define C07	(1<<6)		// mt-delayed print out
#define C08	(1<<7)		// indicator lights
#define C09	(1<<8)		// vector display
#define C10	(1<<9)		// number display
#define C11	(1<<10)		// point and vector display
#define C12	(1<<11)		// vector and number display
#define MT01	(1<<12)		// mt read or rerecord
#define MT02	(1<<13)		// mt record
#define MT03	(1<<14)		// mt stop after record
#define MD04	(1<<15)		// aux drum change group
#define MT16	(1<<16)		// mt
#define LG01	(1<<17)		// point display
#define GA01	(1<<18)		// output coder

// one pulse sets carry, one pulse gated by carry
#define IODC(us) ~(us-2)

#define SI_ (0<<11) +
#define rs_ (1<<11) +
#define BI_ (2<<11) +
#define RD_ (3<<11) +
#define BO_ (4<<11) +
#define RC_ (5<<11) +
#define sd_ (6<<11) +
#define cf_ (7<<11) +
#define TS_ (8<<11) +
#define TD_ (9<<11) +
#define TA_ (10<<11) +
#define CK_ (11<<11) +
#define AB_ (12<<11) +
#define EX_ (13<<11) +
#define CP_ (14<<11) +
#define SP_ (15<<11) +
#define CA_ (16<<11) +
#define CS_ (17<<11) +
#define AD_ (18<<11) +
#define SU_ (19<<11) +
#define CM_ (20<<11) +
#define SA_ (21<<11) +
#define AO_ (22<<11) +
#define DM_ (23<<11) +
#define MR_ (24<<11) +
#define MH_ (25<<11) +
#define DV_ (26<<11) +
#define SL_ (27<<11) +
#define SR_ (28<<11) +
#define SF_ (29<<11) +
#define CL_ (30<<11) +
#define md_ (31<<11) +

#define SI 0x00000001
//#define rs 0x00000002
#define BI 0x00000004
#define RD 0x00000008
#define BO 0x00000010
#define RC 0x00000020
//#define sd 0x00000040
//#define cf 0x00000080
#define TS 0x00000100
#define TD 0x00000200
#define TA 0x00000400
#define CK 0x00000800
#define AB 0x00001000
#define EX 0x00002000
#define CP 0x00004000
#define SP 0x00008000
#define CA 0x00010000
#define CS 0x00020000
#define AD 0x00040000
#define SU 0x00080000
#define CM 0x00100000
#define SA 0x00200000
#define AO 0x00400000
#define DM 0x00800000
#define MR 0x01000000
#define MH 0x02000000
#define DV 0x04000000
#define SL 0x08000000
#define SR 0x10000000
#define SF 0x20000000
#define CL 0x40000000
//#define md 0x80000000
#define PT 0xFFFFFFFF	// not really, probably

#include "cpo54.inc"
const int cpo116_cm_rd_out_tp6 = 0xFFFF0000;
const int cpo117_cm_rd_out_tp6 = 0xFF00FF00;
const int cpo118_cm_rd_out_tp6 = 0xF0F0F0F0;
const int cpo119_cm_rd_out_tp6 = 0xCCCCCCCC;
const int cpo120_cm_rd_out_tp6 = 0xAAAAAAAA;

#define MS_SELECT !!(ww->bus & 0003740)

void handleio(Whirlwind *ww);
static void ioc_reset_si(Whirlwind *ww);
static void ioc_reset_rc(Whirlwind *ww);
static void ioc_reset_rd(Whirlwind *ww);

static void
agescope(Whirlwind *ww)
{
	Scope *s = &ww->scope;
	int cmd = 510<<23;
	assert(s->last <= ww->simtime);
	u64 dt = (ww->simtime - s->last)/1000;
	while(dt >= 510) {
		s->last += 510*1000;
		write(s->fd, &cmd, sizeof(cmd));
		dt = (ww->simtime - s->last)/1000;
	}
}

static void
intensify(Whirlwind *ww)
{
	Scope *s = &ww->scope;
	if(s->fd >= 0) {
		agescope(ww);
		u32 cmd = 0;
		int x = ww->hdefl;
		int y = ww->vdefl;
		int dt = (ww->simtime - s->last)/1000;
		// Convert [-1.0,+1.0] to [0,1023]
		if(x & 02000) x++;
		if(y & 02000) y++;
		x = ((x+02000)&03777)>>1;
		y = ((y+02000)&03777)>>1;
		cmd = x | (y<<10) | (7<<20) | (dt<<23);
		s->last = ww->simtime;
		if(write(s->fd, &cmd, 4) < 4)
			s->fd = -1;
	}
}

static void
ios_decode(Whirlwind *ww)
{
	ww->ios_dec = 0;
	ww->active_unit = &nullUnit;
	switch((ww->ios>>6) & 7) {
	case 0:
		ww->active_unit = &ww->misc;
		break;
	case 1: break;	// magnetiv tape
	case 2: break;	// paper tape
	case 3: break;	// intervention register
	case 4: break;	// mite buffer storage
	case 5: break;	// indicator light
	case 6:		// display
		ww->ios_dec = C01|C03;
		switch((ww->ios>>9) & 3) {
		case 0: ww->ios_dec |= LG01|C11; break;	// points
		case 1: ww->ios_dec |= C09|C11|C12; break;	// vectors
		case 2:
		case 3: ww->ios_dec |= C10|C12; break;	// symbols
		}
		ww->active_unit = &ww->scope.eu;
		break;
	case 7: break;	// magnetic drum
	}
}

static void
ss_clear(Whirlwind *ww)
{
	ww->ssc = 0;
	ww->tss = 0;
	ww->mar = 0;
}
static void
ss_rd_in(Whirlwind *ww)
{
	ww->ssc |= MS_SELECT;
	ww->tss |= ww->bus & 037;
	ww->mar |= ww->bus & 03777;
	if(MS_SELECT)
		ww->tss = 0;
	if(ww->tss >= 2 && ww->tss <= 6)
		ww->sel_ffs = &ww->ffs[ww->tss-2];
	else
		ww->sel_ffs = nil;
}

static void
ms_start(Whirlwind *ww, int strobe)
{
	// MS write is supposed to clear SS,
	// which should probably also clear mar,
	// but then where is the address stored?
	// use mar_buf here as a hack
	ww->mar_buf = ww->mar;
	ww->ms_on = 1;
	ww->ms_state = 1;
	ww->ms_strobe = strobe;
}

static Word
stor_rd_out(Whirlwind *ww)
{
	Word b = ww->par;
	b |= ww->tg[ww->tss];
	if(ww->sel_ffs)
		b |= *ww->sel_ffs;
	return b;
}

static void
cr_alarm(Whirlwind *ww)
{
	printf("CR alarm\n");

	ww->automatic = 0;
}
static void
ar_alarm(Whirlwind *ww)
{
	printf("arithmetic alarm\n");

	ww->automatic = 0;
}
static void
div_alarm(Whirlwind *ww)
{
	printf("division alarm\n");

	ww->automatic = 0;
}



static void
ac_carry_clear(Whirlwind *ww)
{
	ww->ac_cry = 0;
	ww->sam = 0;
	ww->ov = 0;
}
static void
add(Whirlwind *ww, Word ar)
{
	if(ww->ac & ar & 0100000) {
		// left digit carry
		ww->ov ^= 1;
		ww->sam = 0;
	}
	ww->ac_cry |= (ww->ac & ar)<<1;
	ww->ac ^= ar;
}
static void
carry(Whirlwind *ww, int cry)
{
	int sum = ww->ac + cry + ww->ov;
	if(sum & 0200000)
		sum = (sum+1) & 0177777;
	if(~ww->ac & sum & 0100000) {
		// left digit carry
		ww->ov ^= 1;
		ww->sam = 0;
	}
	ww->ac = sum;
}

static void
arith_clear(Whirlwind *ww)
{
	ww->div_ff = 0;
	ww->div_pulse = 0;
	ww->sr_ff = 0;
	ww->sl_ff = 0;
	ww->mul_ff = 0;
	ww->po_ff = 0;
	ww->po_pulse = 0;
}

static void
add_to_sc(Whirlwind *ww)
{
	if(ww->sc & 040) {
		ww->stop_clock = 0;
		arith_clear(ww);
	}
	ww->sc = (ww->sc+1) & 077;
}

//#define TRACE(tp) trace(ww, tp)
#define TRACE(...)

static void
trace(Whirlwind *ww, const char *tp)
{
	printf("%s  ", tp);
	printf("PC/%04o AC/%06o BR/%06o AR/%06o PAR/%06o SC/%02o SRC/%d\n",
		ww->pc, ww->ac, ww->br, ww->ar, ww->par, ww->sc, ww->src);
}

static void
tp1(Whirlwind *ww)
{
	TRACE("TP1");

	if(cpo40_par_rd_out_tp1 & ww->cs_dec)
		ww->bus |= ww->par;
	if(cpo60_ar_rd_out_tp1 & ww->cs_dec)
		ww->bus |= ww->ar;
	if(cpo43_ac_rd_out_tp1 & ww->cs_dec)
		ww->bus |= ww->ac;

	if(SI & ww->cs_dec) {
		ww->vdefl |= ww->bus >> 5;
		// TODO: drum
		// TODO: when does this happen?
		if(0)
			ww->ior |= ww->bus;
		ioc_reset_si(ww);
		ww->active_unit->start(ww, ww->active_unit);
	}
	if(RC & ww->cs_dec) {
		ww->hdefl |= ww->bus >> 5;
		ww->par |= ww->bus;	// and parity count
		if(ww->active_unit != &ww->scope.eu)
			ww->ior |= ww->bus;
	}
	if(cpo08_pc_rd_in_tp1 & ww->cs_dec)
		ww->pc |= ww->bus & 03777;
	if(cpo71_stor_par_rd_in_tp1 & ww->cs_dec) {
		if(ww->sel_ffs) *ww->sel_ffs |= ww->bus;
		ww->par |= ww->bus;
	}
	if(cpo67_stor_par_rd_in_rt11_tp1 & ww->cs_dec) {
		if(ww->sel_ffs) *ww->sel_ffs |= ww->bus & 03777;
		ww->par |= ww->bus & 03777;
	}

	if(cpo19_ss_clear_tp1 & ww->cs_dec)
		ss_clear(ww);
	if(cpo42_ac_src_clear_tp1 & ww->cs_dec) {
		ww->ac = 0;
		ww->src = 0;
	}
	if(cpo27_check_magnitude_tp1 & ww->cs_dec) {
		if(ww->ar & 0100000)
			ww->ar ^= 0177777;
		ww->sign = 0;
	}
	if(cpo22_ar_sign_check_tp1 & ww->cs_dec) {
		if(ww->ar & 0100000) {
			ww->ar ^= 0177777;
			ww->sign ^= 1;
		}
		ww->div_err = 1;
	}
	if(cpo47_transfer_check_tp1 & ww->cs_dec) {
		// unclear what CPO47 is.
		// it used to be CR CHECK at TP8_dly under PT
		// but then became TRFR CHECK at TP1 not under PT
		// so when does CR CHECK happen?
		if(ww->cr) cr_alarm(ww);
	}
	if(cpo82_ms_write_ss_clear_tp1 & ww->cs_dec) {
		if(ww->ssc)
			ms_start(ww, 0);
		ss_clear(ww);
	}

	if(cpo42x_ac_clear_tp1_dly & ww->cs_dec)
		ww->ac = 0;
}

static void
tp2(Whirlwind *ww)
{
	TRACE("TP2");

	if(RC & ww->cs_dec) {
		// TODO: Sense BC#3
	}
	if(cpo12_pc_rd_out_tp2 & ww->cs_dec)
		ww->bus |= ww->pc;
	if(cpo17_ss_rd_in_tp25 & ww->cs_dec)
		ss_rd_in(ww);
	if(cpo09_cr_clear_pc_rd_to_cb_tp2 & ww->cs_dec) {
		ww->cr = 0;
		// -- dly
		ww->ckbus |= ww->pc;
		ww->cr ^= ww->ckbus;
	}

	if(cpo48_stop_clock_tp2 & ww->cs_dec)
		ww->stop_clock = 1;
	if(cpo78_add_tp2 & ww->cs_dec)
		add(ww, ww->ar);
	if(cpo80_subtract_tp2 & ww->cs_dec)
		add(ww, ~ww->ar);
	if(cpo35_multiply_tp2 & ww->cs_dec) {
		ww->mul_ff = 1;
		ww->sc = 022;
	}
	if(cpo36_divide_tp2 & ww->cs_dec) {
		ww->div_ff = 1;
		ww->div_pulse = 0;
		ww->sc = 020;
	}
	if(cpo33_shift_left_tp2 & ww->cs_dec)
		if(!(ww->sc & 040))
			ww->sl_ff = 1;
	if(cpo31_shift_right_tp2 & ww->cs_dec)
		if(!(ww->sc & 040))
			ww->sr_ff = 1;
	if(cpo04_add_to_sc_tp27 & ww->cs_dec)
		add_to_sc(ww);
}

static void
tp3(Whirlwind *ww)
{
	TRACE("TP3");

	if(RC & ww->cs_dec) {
		ioc_reset_rc(ww);
		// init record
		ww->active_unit->record(ww, ww->active_unit);
	}
	if(cpo18_ss_rd_out_cr_rd_in_tp3 & ww->cs_dec) {
		ww->bus |= ww->ssc ? ww->mar : ww->tss;
		ww->cr ^= ww->bus;
	}

	if(cpo46_carry_tp3 & ww->cs_dec) {
		carry(ww, ww->ac_cry);
		ww->ac_cry = 0;
	}
	if(cpo52_roundoff_via_src_tp3 & ww->cs_dec)
		if((ww->br & 0100000) && !ww->src) carry(ww, 1);
	if(cpo51_ac_clear_tp3 & ww->cs_dec)
		ww->ac = 0;
	if(cpo81_ms_rd_par_clear_tp3 & ww->cs_dec) {
		ww->par = 0;
		if(ww->ssc)
			ms_start(ww, 1);
	}
}

static void
tp4(Whirlwind *ww)
{
	TRACE("TP4");

	if(cpo56_transfer_check_tp47 & ww->cs_dec)
		if(ww->cr) cr_alarm(ww);
	if(cpo76_stor_rd_out_par_rd_in_tp4 & ww->cs_dec) {
		ww->bus |= stor_rd_out(ww);
		ww->par |= ww->bus;
	}
	if(cpo68_cs_clear_tp4 & ww->cs_dec)
		ww->cs = 0;
	if(cpo20_ss_clear_tp4 & ww->cs_dec)
		ss_clear(ww);
	if(cpo74_stor_rd_to_cb_tp4 & ww->cs_dec) {
		ww->ckbus |= stor_rd_out(ww);
		ww->cr ^= ww->ckbus;
	}
	if(cpo32_arithmetic_check_tp4 & ww->cs_dec)
		if(ww->ov) ar_alarm(ww);
	if(cpo34_special_add_tp4 & ww->cs_dec)
		if(ww->ov) {
			ww->ov = 0;
			ww->sam = 1;
			if(ww->ac & 0100000) {
				ww->ov ^= 1;
				ww->sam = 0;
			}
			ww->ac ^= 0100000;
		}
	if(cpo15_ac_carry_clear_tp4 & ww->cs_dec)
		ac_carry_clear(ww);
	if(cpo44_br_clear_via_src_tp4 & ww->cs_dec)
		if(!ww->src) ww->br = 0;
	if(cpo03_sc_src_preset_tp4 & ww->cs_dec) {
		ww->sc = 037;
		ww->src = 0;
	}

	if(cpo24_product_sign_tp4_dly & ww->cs_dec) {
		if(ww->sign) {
			ww->ac ^= 0177777;
			ww->sign ^= 1;
		}
		ww->div_err = 0;
	}
}

static void
tp5(Whirlwind *ww)
{
	TRACE("TP5");

	if(cpo28_par_rd_out_tp5 & ww->cs_dec)
		ww->bus |= ww->par;
	if(cpo66_cs_rd_in_tp5 & ww->cs_dec)
		ww->cs |= ww->bus>>11;
	if(cpo17_ss_rd_in_tp25 & ww->cs_dec)
		ss_rd_in(ww);
	if(cpo02_sc_src_rd_in_tp5 & ww->cs_dec) {
		ww->sc ^= ww->bus&037;
		ww->src |= (ww->bus>>9)&1;
	}
}

static void
tp6(Whirlwind *ww)
{
	TRACE("TP6");

	// IO, no CPO numbers known
	if(SI & ww->cs_dec) {
		if((ww->ios_dec & C01) && ww->interlock) {
			ww->interlock = 0;
			ww->stop_clock = 1;
		}
	}
	if((RD|RC|BI|BO) & ww->cs_dec) {
		if(ww->interlock) {
			ww->interlock = 0;
			ww->stop_clock = 1;
		}
	}

	if(cpo116_cm_rd_out_tp6 & ww->cs_dec)
		ww->bus |= 0100000;
	if(cpo117_cm_rd_out_tp6 & ww->cs_dec)
		ww->bus |= 0040000;
	if(cpo118_cm_rd_out_tp6 & ww->cs_dec)
		ww->bus |= 0020000;
	if(cpo119_cm_rd_out_tp6 & ww->cs_dec)
		ww->bus |= 0010000;
	if(cpo120_cm_rd_out_tp6 & ww->cs_dec)
		ww->bus |= 0004000;
	if(cpo49_ss_rd_out_cr_rd_in_tp6 & ww->cs_dec) {
		ww->bus |= ww->ssc ? ww->mar : ww->tss;
		ww->cr ^= ww->bus;
	}
	if(cpo70_ar_clear_tp6 & ww->cs_dec)
		ww->ar = 0;
	if(cpo41_ac_src_clear_tp6 & ww->cs_dec)
		ww->ac = ww->src = 0;
	if(cpo55_ac_carry_clear_tp6 & ww->cs_dec)
		ac_carry_clear(ww);
	if(cpo53_br_clear_tp6 & ww->cs_dec)
		ww->br = 0;
	if(cpo23_ac_sign_check_tp6 & ww->cs_dec) {
		if(ww->ac & 0100000) {
			ww->ac ^= 0177777;
			ww->sign ^= 1;
		}
	}
	if(cpo84_ms_rd_par_clear_tp6 & ww->cs_dec) {
		ww->par = 0;
		if(ww->ssc)
			ms_start(ww, 1);
	}
}

static void
tp7(Whirlwind *ww)
{
	TRACE("TP7");

	if(cpo56_transfer_check_tp47 & ww->cs_dec)
		if(ww->cr) cr_alarm(ww);

	// IO, no CPO numbers known
	if(SI & ww->cs_dec) {
		ww->ios = 0;
		ww->ckbus |= ww->par & 03777;
		ww->cr ^= ww->ckbus;
		// stop units
        	ww->misc.stop(ww, &ww->misc);
        	ww->scope.eu.stop(ww, &ww->scope.eu);

		// IOS delay start
		ww->interlock = 0;
ww->stop_clock = 1;
		ww->iocc = 017;
		ww->iodc = 077777;
		// IOS delay
		ww->iodc_start = 1;
		ww->iodc &= IODC(15);
	}
	if(cpo50_stop_clock_tp7 & ww->cs_dec)
		ww->stop_clock = 1;
	if(cpo64_stor_rd_out_tp7 & ww->cs_dec)
		ww->bus |= stor_rd_out(ww);
	if(cpo79_ar_rd_in_tp7 & ww->cs_dec)
		ww->ar |= ww->bus;
	if(cpo14_add_to_pc_tp7 & ww->cs_dec) {
		ww->pc++;
		if(ww->pc == 04000) {
			// TODO: pc end carry
			ww->pc = 0;
		}
	}
	if(cpo21_compare_tp7 & ww->cs_dec)
		if(ww->ac & 0100000) ww->cs |= 1;
	if(cpo65_stor_rd_to_cb_tp7 & ww->cs_dec) {
		ww->ckbus |= stor_rd_out(ww);
		ww->cr ^= ww->ckbus;
	}
	if(cpo26_special_carry_tp7 & ww->cs_dec) {
		if(ww->sam) {
			ww->sam = 0;
			ww->ac ^= 0177776;
		}
		if(ww->ov) {
			ww->ov = 0;
			carry(ww, 1);
		}
	}
	if(cpo59_ac_rd_to_br_tp7 & ww->cs_dec)
		ww->br |= ww->ac;
	if(cpo2xx_br_rd_to_ac_tp7 & ww->cs_dec)
		ww->ac |= ww->br;
	if(cpo04_add_to_sc_tp27 & ww->cs_dec)
		add_to_sc(ww);
	if(cpo25_point_off_tp7 & ww->cs_dec) {
		ww->po_ff = 1;
		// TODO: would make sense to clear po_pulse here
		ww->sc = 0;
	}

	if(cpo05_add_tp7_dly & ww->cs_dec)
		add(ww, ww->ar);
	if((SI|RC|RD) & ww->cs_dec)
		ww->ior = 0;
	if(SI & ww->cs_dec) {
		ww->bus = 0;
		ww->bus |= ww->par;
		ww->ios |= ww->bus & 03777;
		ios_decode(ww);
	}
}

static void
tp8(Whirlwind *ww)
{
	TRACE("TP8");

	if(SI & ww->cs_dec) {
		ww->bus |= ww->ios;
		ww->cr ^= ww->bus;
		ww->vdefl = 0;
		// TODO: drum
	}
	if(RC & ww->cs_dec)
		ww->hdefl = 0;

	if(cpo69_ar_rd_out_tp8 & ww->cs_dec)
		ww->bus |= ww->ar;
	if(cpo54_ac_rd_out_tp8 & ww->cs_dec)
		ww->bus |= ww->ac;
	if(cpo01_sc_rd_out_tp8 & ww->cs_dec)
		ww->bus |= ww->sc;
	if(cpo10_pc_rd_out_tp8 & ww->cs_dec)
		ww->bus |= ww->pc;
	if(RC & ww->cs_dec)
		if(ww->active_unit == &ww->scope.eu) {
			ww->bus |= stor_rd_out(ww);
			ww->ior |= ww->bus;
		}

	if(cpo73_ar_rd_in_tp8 & ww->cs_dec)
		ww->ar |= ww->bus;
	if(cpo58_cr_rd_in_tp8 & ww->cs_dec)
		ww->cr ^= ww->bus;

	if(cpo61_stor_clear_tp8 & ww->cs_dec)
		if(ww->sel_ffs) *ww->sel_ffs = 0;
	if(cpo75_stor_clear_rt11_tp8 & ww->cs_dec)
		if(ww->sel_ffs) *ww->sel_ffs &= 0174000;
	if(cpo88_par_clear_tp8 & ww->cs_dec)
		ww->par = 0;
	if(cpo89x_par_clear_rt11_tp8 & ww->cs_dec)
		ww->par &= 0174000;
	if(cpo46x_carry_tp8 & ww->cs_dec) {
		carry(ww, ww->ac_cry);
		ww->ac_cry = 0;
	}
	if(cpo16_end_around_carry_tp8 & ww->cs_dec)
		carry(ww, 1);
	if(cpo23x_ac_sign_check_tp8 & ww->cs_dec) {
		if(ww->ac & 0100000) {
			ww->ac ^= 0177777;
			ww->sign ^= 1;
		}
	}

	if(cpo06_pc_clear_tp8_dly & ww->cs_dec)
		ww->pc = 0;
	if(RC & ww->cs_dec)
		ww->par = 0;
}

static void
tpd_tick(Whirlwind *ww)
{
	int t = ww->tpd;
	ww->tpd = (ww->tpd+1) & 7;
	ww->cs_dec = 1 << ww->cs;
	ww->bus = ww->ckbus = 0;
	switch(t) {
	case 0: tp1(ww); break;
	case 1: tp2(ww); break;
	case 2: tp3(ww); break;
	case 3: tp4(ww); break;
	case 4: tp5(ww); break;
	case 5: tp6(ww); break;
	case 6: tp7(ww); break;
	case 7: tp8(ww); break;
	}
}

static void
ms_tick(Whirlwind *ww)
{
	if(ww->ms_state == 0) return;
	switch(ww->ms_state++) {
	case 1: break;
	case 2:
		if(ww->ms_strobe)
			ww->par |= ww->ms[ww->mar_buf];
		ww->ms[ww->mar_buf] = 0;
		break;
	case 3: break;
	case 4: break;
	case 5:
		ww->ms[ww->mar_buf] |= ww->par;
		break;
	case 6: break;
	case 7: break;
	case 8:
		ww->ms_on = 0;
		ww->ms_state = 0;
		break;
	}
}

static void
io_cycle(Whirlwind *ww, int odd)
{
	if(ww->ios_dec & LG01) {
		// point intensification delay
		ww->iodc &= IODC(35);
		if(!odd)
			;	// TODO: sense light gun
	}
	if(ww->ios_dec & C03)
		ww->iodc_start = 1;
	if(ww->ios_dec & C11)
		if(odd)		// hack, because this happens twice
			intensify(ww);
}

static void
ioc_reset_si(Whirlwind *ww)
{
}

static void
ioc_reset_rcrd(Whirlwind *ww)
{
	ww->interlock = 1;
	// TODO: clear alarm
	if(ww->ios_dec & C03)
		ww->iodc_start = 1;
}

static void
ioc_reset_rc(Whirlwind *ww)
{
	if(ww->ios_dec & LG01) {
		ww->ior = 0;
		ww->iocc &= ~2;
	}
	if(ww->ios_dec & C03)
		// scope deflect delay
		ww->iodc &= IODC(100);
	ioc_reset_rcrd(ww);
}

static void
ioc_reset_rd(Whirlwind *ww)
{
	ioc_reset_rcrd(ww);
}

static void
io_tick(Whirlwind *ww)
{
	if(ww->iodc & 0100000) {
		ww->iodc = 077777;
		int iocc = ww->iocc;
		ww->iocc++;
		if(ww->iocc & 020) {
			ww->iocc = 017;
			if(!ww->interlock)
				ww->stop_clock = 0;
			ww->interlock = 0;
// TODO: alarm control, sync completion
		} else
			io_cycle(ww, iocc&1);
	}
	if(ww->iodc_start) {
		ww->iodc++;
		if(ww->iodc & 0100000)
			ww->iodc_start = 0;
	}
}

static void
sr_step(Whirlwind *ww)
{
	add_to_sc(ww);
	ww->br = (ww->ac&1)<<15 | ww->br>>1;
	Word sr = ww->ac^ww->ac_cry;
	ww->ac = (ww->ac&~sr)&0100000 | sr>>1;
	ww->ac_cry &= ~sr;
}

static void
sl_step(Whirlwind *ww)
{
	add_to_sc(ww);
	if(ww->cs_dec & CL) {
		int t = ww->ac>>15;
		ww->ac = ww->ac<<1 | (ww->br>>15)&1;
		ww->br = ww->br<<1 | t&1;
	} else {
		ww->ac = (ww->ac<<1)&077777 | (ww->br>>15)&1;
		ww->br = ww->br<<1;
	}
}

static void
mul_step(Whirlwind *ww)
{
	if(ww->br & 1) {
		ww->br ^= 1;
		add(ww, ww->ar);
	} else {
		sr_step(ww);
	}
}

static void
divide_step(Whirlwind *ww)
{
	if(!ww->div_pulse) {
		carry(ww, ww->ac_cry);
		ww->ac_cry = 0;
	} else {
		add_to_sc(ww);
		// divide shift left
		if(ww->ac & 0100000) {
			ww->div_err = 0;
			ww->ov = 0;
			ww->ac = ww->ac<<1 | 1;
			ww->br <<= 1;

			add(ww, ww->ar);
		} else {
			if(ww->div_err)
				div_alarm(ww);
			ww->ov = 0;
			ww->ac = ww->ac<<1;
			ww->br <<= 1;

			ww->br |= 1;
			add(ww, ~ww->ar);
		}
	}
	ww->div_pulse ^= 1;
}

static void
po_step(Whirlwind *ww)
{
	if(!ww->po_pulse) {
		if(ww->ac & 040000) {
			ww->stop_clock = 0;
			arith_clear(ww);
		}
	} else {
		add_to_sc(ww);
		ww->ac = (ww->ac<<1)&077777 | (ww->br>>15)&1;
		ww->br = ww->br<<1;
	}
	ww->po_pulse ^= 1;
}

// 2mc	0.5μs
static void
ae_hf(Whirlwind *ww)
{
	if(ww->div_ff) divide_step(ww);
}

// 1mc	1.0μs
static void
ae_lf(Whirlwind *ww)
{
	if(ww->sr_ff) sr_step(ww);
	if(ww->sl_ff) sl_step(ww);
	if(ww->mul_ff) mul_step(ww);
	if(ww->po_ff) po_step(ww);
}

// not quite sure how these things are reset
static void
ioclr(Whirlwind *ww)
{
	ww->interlock = 0;
	ww->ios_dec = 0;
	ww->active_unit = &nullUnit;
}

static void
pwrclr(Whirlwind *ww)
{
	ww->ms_on = 0;
	ww->stop_clock = 0;
	ww->automatic = 0;
	ioclr(ww);
	arith_clear(ww);

	ww->tpd = rand() & 7;
	ww->pc = rand() & 03777;
	ww->cs = rand() & 037;
	ww->tss = rand() & 037;
	ww->ffs[0] = rand() & 0177777;
	ww->ffs[1] = rand() & 0177777;
	ww->ffs[2] = rand() & 0177777;
	ww->ffs[3] = rand() & 0177777;
	ww->ffs[4] = rand() & 0177777;
	ww->ar = rand() & 0177777;
	ww->ac = rand() & 0177777;
	ww->ac_cry = rand() & 0177777;
	ww->br = rand() & 0177777;
	ww->src = rand() & 1;
	ww->sc = rand() & 077;
	ww->sam = rand() & 1;
	ww->ov = rand() & 1;
	ww->sign = rand() & 1;
	ww->div_err = rand() & 1;
	ww->cr = rand() & 0177777;
	ww->par = rand() & 0177777;
	ww->mar = rand() & 03777;
}

static void
start_over(Whirlwind *ww, Word pc)
{
	ww->tpd = 0;
	ww->cs = 013;	// ck
	ww->tss = 0;	// ? no schematic
	ac_carry_clear(ww);
	ww->sign = 0;
	ww->div_err = 0;
	ww->pc = pc;
	ww->cr = 0;	// not sure about this
	arith_clear(ww);

	ww->ms_on = 0;
	ww->stop_clock = 0;
	ww->automatic = 1;
}

void
throttle(Whirlwind *ww)
{
	while(ww->realtime < ww->simtime)
		ww->realtime = gettime();
}

void
emu(Whirlwind *ww, Panel *panel)
{
	int stop_tp5 = 0;

	pwrclr(ww);

	inittime();
	ww->simtime = gettime();
	ww->scope.last = ww->simtime;
	for(;;) {
		updateswitches(ww, panel);

		if(ww->power_sw) {
			updatelights(ww, panel);

// M-1734_WWI_Control_Switches_and_Pushbuttons_for_Normal_Operation_of_the_Computer_Dec1952.pdf
// PB:
// 	ERASE ES
// 	READ IN		stop, reset, start at read-in (21,025)
//	CLEAR ALARM
//	START OVER	clears stuff, start at PC SW
//	START AT 40
//	STOP / CHANGE TO P.B.
//	START CLOCK
//	RESTART		opposite of STOP
//	ORDER BY ORDER	RESTART, stop at TP5
//	EXAMINE		START OVER, stop at TP5
//
//	RESET ALL FFS
//	SELECTIVE FFS RESET & RESTART
//	FFS RESET BY TP3
//	FFS RESET BY PCEC
//	FFS RESET BY rs
// TS
//	STOP ON si-1
//	STOP ON SEL PULSE	a bit complicated

			if(ww->btn_start_over) start_over(ww, ww->pc_reset_sw);
			if(ww->btn_start_at40) start_over(ww, 040);
			if(ww->btn_readin) start_over(ww, 21);
			if(ww->btn_examine) {
				start_over(ww, ww->pc_reset_sw);
				ww->stop_tp5 = 1;
			}
			// not a pulse here - so can single step clock pulses
			if(ww->btn_stop) ww->automatic = 0;
			if(ww->btn_restart) ww->automatic = 1;
			if(ww->btn_order_by_order) {
				ww->automatic = 1;
				ww->stop_tp5 = 1;
			}
			if(ww->btn_start_clock) ww->stop_clock = 0;

			// 1μs interval
			ms_tick(ww);
			io_tick(ww);
			if(ww->automatic) {
				if(!ww->stop_clock && !ww->ms_on) {
					tpd_tick(ww);
					if(ww->tpd == 5 && ww->stop_tp5) {
						ww->stop_tp5 = 0;
						ww->automatic = 0;
					}
				}
				ae_hf(ww);
				ae_hf(ww);
				ae_lf(ww);
			} else if(ww->btn_clock_pulse) {
				if(ww->stop_clock) {
					ae_hf(ww);
					ae_lf(ww);
				} else {
					ae_hf(ww);
					ae_lf(ww);
					tpd_tick(ww);
				}
			}
			// TODO: io clock
			ww->simtime += 1000;

			throttle(ww);
		} else {
			pwrclr(ww);

			lightsoff(panel);

			ww->simtime = gettime();
		}
		if(ww->scope.fd >= 0)
			agescope(ww);
	}
}

static void
start_misc(Whirlwind *ww, ExUnit *eu)
{
	switch(ww->ios & 037) {
	case 0:
		ww->automatic = 0;
		break;
	case 1:
		if(ww->stop_si1)
			ww->automatic = 0;
		break;
	}
}

void
handleio(Whirlwind *ww)
{
}

static void
readmem(Whirlwind *ww, FILE *f)
{
	char buf[100], *s;
	Word a, w;

	a = 0;
	while(s = fgets(buf, 100, f)) {
		while(*s) {
			if(*s == ';')
				break;
			else if('0' <= *s && *s <= '7') {
				w = strtol(s, &s, 8);
				if(*s == ':') {
					a = w;
					s++;
				} else if(a < 04000)
					ww->ms[a++] = w;
			} else
				s++;
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
	Whirlwind ww1, *ww = &ww1;
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

	memset(ww, 0, sizeof(*ww));
	ww->misc = nullUnit;
	ww->misc.start = start_misc;
	ww->scope.eu = nullUnit;

	ww->scope.fd = dial(host, port);
	if(ww->scope.fd < 0)
		printf("can't open display\n");
	nodelay(ww->scope.fd);

	FILE *mf;
	mf = fopen("out.mem", "r");
	if(mf) {
		readmem(ww, mf);
		fclose(mf);
	}

	ww->tg[0] = 0;
	ww->tg[1] = 1;
	ww->tg[2] = 00000;	// FF
	ww->tg[3] = 00000;	// FF
	ww->tg[4] = 00000;	// FF
	ww->tg[5] = 00000;	// FF
	ww->tg[6] = 00000;	// FF
	ww->tg[7] = SA_ 4;
	ww->tg[8] = TS_ 4;
	ww->tg[9] = AO_ 6;
	ww->tg[10] = AO_ 3;
	ww->tg[11] = CP_ 18;
	ww->tg[12] = TS_ 3;
	ww->tg[13] = RD_ 13;
	ww->tg[14] = CP_ 12;
	ww->tg[15] = TS_ 6;
	ww->tg[16] = EX_ 4;
	ww->tg[17] = TS_ 5;
	ww->tg[18] = RD_ 18;
	ww->tg[19] = TS_ 2;
	ww->tg[20] = SP_ 6;
	ww->tg[21] = SI_ 139;	// paper tape entry
	ww->tg[22] = CA_ 0;
	ww->tg[23] = SP_ 12;
	ww->tg[24] = SI_ 66;	// magtape entry
	ww->tg[25] = RD_ 25;
	ww->tg[26] = CP_ 12;
	ww->tg[27] = SP_ 24;

	emu(ww, panel);
	return 0;	// can't happen
}
