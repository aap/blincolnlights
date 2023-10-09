
typedef u32 Word;
typedef u16 Addr;
#define WORDMASK 0777777
#define ADDRMASK 0177777
#define MAXMEM (64*1024)

typedef struct TX0 TX0;

struct TX0
{
	Word ac;
	Word lr;
	Word mbr;
	Word mar;
	Word pc;
	Word ir;
	Word core[MAXMEM];

	Word tbr;
	Word tac;

	// not all used (yet?)
	int sw_sup_alarm;
	int sw_sup_chime;
	int sw_auto_restart;
	int sw_auto_readin;
	int sw_auto_test;
	int sw_stop_c0;
	int sw_stop_c1;
	int sw_step;
	int sw_repeat;
	int sw_cm_sel;
	int sw_type_in;
	int btn_test;
	int btn_readin;
	int btn_stop;
	int btn_restart;

	int c, r, t;
	int par;
	int ss, pbs, ios;
	int ch, aff;
	int mrffu, mrffv, miff;
	// TODO
	int lp;

	u64 simtime;
	u64 realtime;

	// display
	// simulation
	int dpy_fd;
	u64 dpy_time;
	u64 dpy_last;

	// reader
	int petr_holes;
	int petr12;
	// very unclear what these are exactly
	int petr3;
	int petr4;
	// simulation
	int r_fd;
	u64 r_time;

	// flexowriter
	// simulation
	int fl_punch;
	int fl_hole7;
	int typ_fd;
	int p_fd;
	u64 fl_time;
	u64 tyi_wait;
};

#define IR tx0->ir
#define PC tx0->pc
#define MAR tx0->mar
#define MBR tx0->mbr
#define AC tx0->ac
#define LR tx0->lr
#define TBR tx0->tbr
#define TAC tx0->tac

#define ST (tx0->ir == 0)
#define AD (tx0->ir == 1)
#define TN (tx0->ir == 2)
#define OP (tx0->ir == 3)

void pwrclr(TX0 *tx0);
void cycle(TX0 *tx0);
void handleio(TX0 *tx0);
void agedisplay(TX0 *tx0);
void throttle(TX0 *tx0);
void cli(TX0 *tx0);
