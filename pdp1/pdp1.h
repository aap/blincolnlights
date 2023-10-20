
typedef u32 Word;
typedef u16 Addr;
#define WORDMASK 0777777
#define ADDRMASK 07777
#define EXTMASK 0170000
//#define MAXMEM (4*1024)
#define MAXMEM (64*1024)

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
	int sbm_start_sw;
	int stop_sw;
	int continue_sw;
	int examine_sw;
	int deposit_sw;

	int run, run_enable;
	int cyc;
	int df1, df2;
	int bc;
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

	// seq break
	int sbs16;	// 16 channel, type 20
	// one bit per channel if type 20
	u16 req;	// highest prio channel
	u16 b1;		// on (only type 20)
	u16 b2;		// req
	u16 b3;		// req synchronized
	u16 b4;		// break held

	// type 10, multiply-divide
	int muldiv_sw;
	int scr;
	int smb, srm;

	// type 15, memory extension
	int extend_sw;
	int emc;
	int exd;
	Word eta;
	Word ema;
	Word epc;

	int cychack;	// for cycle entry past TP0
	u64 simtime;
	u64 realtime;

	// display
	int dcp;
	int dbx, dby;
	// simulation
	int dpy_fd;
	u64 dpy_time;
	u64 dpy_last;

	// reader
	int rcp;
	int rb;
	int rc;
	int rby;
	int rcl;
	int rbs;
	// simulation
	int r_fd;
	u64 r_time;
	int rim_return;
	int rim_cycle;		// hack to trigger read-in SP1

	// punch
	int pcp;
	int pb;
	int punon;
	// simulation
	int p_time;
	int p_fd;

	// typewriter
	int tcp;
	int tb;
	int tbs;
	int tbb;
	int tyo;
	// simulation
	int typ_fd;
	u64 typ_time;
	u64 tyi_wait;
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
#define IR_MUS (!pdp->muldiv_sw && (IR == 026))
#define IR_DIS (!pdp->muldiv_sw && (IR == 027))
#define IR_MUL (pdp->muldiv_sw && (IR == 026))
#define IR_DIV (pdp->muldiv_sw && (IR == 027))
#define IR_JMP (IR == 030)
#define IR_JSP (IR == 031)
#define IR_SKIP (IR == 032)
#define IR_SHRO (IR == 033)
#define IR_LAW (IR == 034)
#define IR_IOT (IR == 035)
// 36
#define IR_OPR (IR == 037)
#define IR_INCORR (IR==0 || IR==5 || IR==6 || IR==017 || IR==036)


void pwrclr(PDP1 *pdp);
void spec(PDP1 *pdp);
void cycle(PDP1 *pdp);
void start_readin(PDP1 *pdp);
void readin1(PDP1 *pdp);
void readin2(PDP1 *pdp);
void handleio(PDP1 *pdp);
void agedisplay(PDP1 *pdp);
void throttle(PDP1 *pdp);
void cli(PDP1 *pdp);
