#include <stdbool.h>

typedef u16 Addr, Word;
#define WORDMASK 0177777
#define ADDRMASK 03777
#define MAXMEM (2*1024)

typedef struct Whirlwind Whirlwind;
typedef struct ExUnit ExUnit;
typedef struct Scope Scope;

struct ExUnit
{
	int active;     // reading or recording
	void (*start)(Whirlwind *ww, ExUnit *eu);
	void (*stop)(Whirlwind *ww, ExUnit *eu);
	void (*record)(Whirlwind *ww, ExUnit *eu);
	void (*read)(Whirlwind *ww, ExUnit *eu);
};
struct Scope
{
	ExUnit eu;
	int fd;
	u64 last;
};

struct Whirlwind
{
	bool power_sw;

	// control - 100
	int ms_on;
	int stop_clock;
	int automatic;
	int stop_tp5;		// no idea how this is really done
	int stop_si1;
	int tpd;
	Word pc;
	Word pc_reset_sw;
	int cs, cs_dec;
	int ssc;
	// test storage - 200
	Word tss;
	Word tg[16];
	Word ffs[5], *sel_ffs;
	Word ffs_reset_sw[5];
	// arithmetic - 300
	Word ar;
	Word ac;
	Word ac_cry;
	Word br;
	int src;
	int sc;
	int sam;
	int ov;
	int sign;
	int div_err;
	// arithmetic control
	int div_ff, div_pulse;
	int sr_ff;
	int sl_ff;
	int mul_ff;
	int po_ff, po_pulse;
	// IO - 400
	Word ior;
	Word ios;
	int ios_dec;
	int iocc;
	int iodc;
	int iodc_start;
	int interlock;
	// external units - 500
	int vdefl;
	int hdefl;
	ExUnit misc;
	Scope scope;
	ExUnit *active_unit;
	// check - 600
	Word cr;
	// magnetic storage - 800
	Word par;
	Word mar;
	Word mar_buf;	// no idea how this works
	Word ms[MAXMEM];
	int ms_strobe;
	int ms_state;	// simulate delay

	Word bus, ckbus;

// alarms
//	arithmetic
//	divide error
//	cr
//	program		(reader)
//	parity
//	inactivity	(500ms no completion)

	// push buttons
	bool btn_readin;
	bool btn_start_over;
	bool btn_start_at40;
	bool btn_stop;	// we treat it has a switch here
	bool btn_start_clock;
	bool btn_restart;
	bool btn_order_by_order;
	bool btn_clock_pulse;	// no idea if such a thing existed
	bool btn_examine;

	// simulation
	u64 simtime;
	u64 realtime;
};
