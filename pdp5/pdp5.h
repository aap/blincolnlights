typedef unsigned char uchar;
typedef u8 bool;
typedef u16 Word;

#define MAXMEM 010000

enum {
	WD = 07777,

	MAJ_P = 0,
	MAJ_F,
	MAJ_D,
	MAJ_E1,
	MAJ_E2,
	MAJ_B,
};

typedef struct PDP5 PDP5;
typedef struct Panel Panel;
typedef void Pulse(PDP5 *proc);

typedef uint64_t u64;

struct PDP5
{
	Panel *panel;

	bool run;
	bool io_hlt;

	struct {
		Word ac;	// this includes link
		Word mb;
		Word ma;
		Word ir;
		Word sr;

		int state;
		int key_manual;
	} c, n;		// current and next state

	bool disable_ma;
	bool skip;
	bool int_ack;
	bool int_delay;
	bool int_enable;

	bool brk_req;
	int tg;
	int interrupts;

	bool key_continue;
	bool key_load_address;
	bool key_start;
	bool key_examine;
	bool key_deposit;
	bool key_stop;
	bool sw_single_step;
	bool sw_single_instruction;
	bool sw_power;

	u64 simtime;
	u64 realtime;

	Word core[MAXMEM];
	struct {
		void *dev;
		void (*iot)(PDP5*, int, void*);
		void (*cyc)(PDP5*, void*);
		int intr;
	} devtab[0100];
};
#define MA_(p) ((p)->disable_ma ? 0 : (p)->c.ma)
void pdpinit(PDP5 *pdp);
void pwrclr(PDP5 *proc);
void tick(PDP5 *proc);
void handleio(PDP5 *pdp);
void throttle(PDP5 *pdp);

typedef struct TTY TTY;
struct TTY
{
	FD fd;

	uchar luo, lui;
	int oflag, iflag;
};
void attach_tty(PDP5 *pdp, TTY *tty);
void ttytelnet(int port, int fd);
