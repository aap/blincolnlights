/* The debug service.  See DEBUG_PROTOCOL_SPEC.md for what, and
 * DEBUG_IMPLEMENTATION.md for why it is shaped like this.
 *
 * Everything in here runs on the emulator thread.  Nothing in here may
 * block it: not a slow client, not a full socket buffer, not an
 * unbounded run. */

#include "common.h"
#include "netsvc.h"
#include "pdp1.h"
#include "dbg.h"

#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>

enum {
	MAXBP = 64,
	MAXWP = 16,
	RING = 64,		/* call ring */
	MAXCONN = 8,
	MAXEXAM = 4096,		/* words per e, lines per trace */
	MAXRUN = 10*1000*1000,	/* an "unbounded" run is still bounded */
	MAXARG = 64,
};

/* events */
enum { EV_NONE, EV_STOP, EV_ALL };

/* pending command */
enum { PEND_NONE, PEND_RUN, PEND_WAIT };

/* stop reasons, spec §6 */
enum {
	S_NONE, S_HALT, S_ILLEGAL, S_BREAK, S_WATCH, S_COND,
	S_STEP, S_CYCLE, S_STOP, S_MANUAL, S_POWER, S_READIN
};
static const char *reasons[] = {
	"none", "halt", "illegal", "break", "watch", "cond",
	"step", "cycle", "stop", "manual", "power", "readin"
};

/* momentary keys, one pass of emu() each */
enum {
	K_START = 1, K_STARTUP = 2, K_STOP = 4, K_CONT = 8,
	K_EXAM = 16, K_DEP = 32, K_READIN = 64, K_FEED = 128,
};

typedef struct Cond Cond;
typedef struct Bp Bp;
typedef struct Wp Wp;
typedef struct Call Call;
typedef struct DbgConn DbgConn;
typedef struct Dbg Dbg;

struct Cond
{
	int kind;		/* 0 none, 1 register, 2 memory */
	int reg;
	Addr addr;
	int ne;
	Word val;
};

struct Bp
{
	int used;
	Addr addr;
	Cond cond;
};

struct Wp
{
	int used;
	Addr addr;
	int rw;			/* 1 read, 2 write, 3 both */
};

struct Call
{
	Addr to, from, ret;
};

struct DbgConn
{
	NetConn *nc;
	int events;
	int claimed;

	int pending;
	long budget;		/* -1 = unbounded */
	int cycles;		/* budget counts memory cycles, not instructions */
	Cond cond;
	Addr tempbp;
	int hastemp;
	int trace, tracechanged;
	Word prevac, previo, prevma, prevmb;
	int prevov;
	u64 deadline;		/* PEND_WAIT, 0 = forever */
	int answered;		/* got a + for this stop, so no !stop too */
};

struct Dbg
{
	PDP1 *pdp;
	NetSvc svc;

	Bp bps[MAXBP];
	Wp wps[MAXWP];
	Call ring[RING];
	int nring, ringhead;

	/* boundary bookkeeping */
	int haveprev;
	Addr prevpc;
	Word previnst;

	/* stop bookkeeping */
	int running;		/* sticky: the machine was asked to run */
	int reason;
	int selfstop;		/* the stop was ours, reason is already set */
	int nwait;

	/* watchpoint bookkeeping */
	Addr lastreadaddr;
	Word lastreadval;
	int havelastread;
	int wppend;
	Addr wpaddr, wppc;
	Word wpold, wpnew;
	int wprw;

	/* switch override */
	int armed;
	int keys;		/* asserted for exactly one pass */
	int haveta, havetw, havess, havepower;
	int havesstep, havesinst, haveextend;
	Word ta, tw, ss;
	int power, sstep, sinst, extend;
	int onceta;		/* one-pass ta, for go <addr> / readin <addr> */
	Word oncetaval;
	int panelpower;		/* what the real panel says, before we meddle */
	int ppower, pkeys;	/* for !panel */
	int preader;		/* reader key, for the physical unlock edge */

	DbgConn *claimer;
};

int dbg_anywp;
int testmode;
static Dbg dbgstate;
static Dbg *dbg;

/* ---------------------------------------------------------------- text */

static int
ceq(const char *a, const char *b)
{
	for(; *a && *b; a++, b++)
		if(tolower(*a) != tolower(*b))
			return 0;
	return *a == *b;
}

static int
tokenize(char *line, char **argv, int max)
{
	int argc = 0;
	while(*line && argc < max) {
		while(*line == ' ' || *line == '\t')
			line++;
		if(*line == '\0')
			break;
		argv[argc++] = line;
		while(*line && *line != ' ' && *line != '\t')
			line++;
		if(*line)
			*line++ = '\0';
	}
	return argc;
}

/* octal, always, no prefix */
static int
octal(const char *s, Word *out)
{
	Word v = 0;
	if(*s == '\0')
		return 0;
	for(; *s; s++) {
		if(*s < '0' || *s > '7')
			return 0;
		v = v*8 + (*s - '0');
		if(v > 07777777)
			return 0;
	}
	*out = v;
	return 1;
}

/* counts, timeouts and radii are decimal */
static int
decimal(const char *s, long *out)
{
	long v = 0;
	if(*s == '\0')
		return 0;
	for(; *s; s++) {
		if(*s < '0' || *s > '9')
			return 0;
		v = v*10 + (*s - '0');
		if(v > 1000*1000*1000)
			return 0;
	}
	*out = v;
	return 1;
}

/* --------------------------------------------------------------- replies */

/* every server line is typed by its first character: + ok, - error,
 * : data, ! event */
static int
frame(char *buf, int sz, char type, const char *fmt, va_list ap)
{
	int n;

	n = 0;
	if(type) {
		buf[n++] = type;
		buf[n++] = ' ';
	}
	n += vsnprintf(buf+n, sz-n-1, fmt, ap);
	if(n < 0 || n > sz-2)
		n = sz-2;
	buf[n++] = '\n';
	return n;
}

static void
reply(NetConn *nc, char type, const char *fmt, ...)
{
	char buf[NETLINE+64];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = frame(buf, sizeof(buf), type, fmt, ap);
	va_end(ap);
	netsvc_write(nc, buf, n);
}

#define ok(c, ...) reply((c)->nc, '+', __VA_ARGS__)
#define err(c, ...) reply((c)->nc, '-', __VA_ARGS__)
#define dat(c, ...) reply((c)->nc, ':', __VA_ARGS__)

/* fmt already starts with !stop / !bp / !wp / !panel */
static void
event(int minlevel, const char *fmt, ...)
{
	char buf[NETLINE+64];
	NetConn *nc;
	DbgConn *dc;
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = frame(buf, sizeof(buf), 0, fmt, ap);
	va_end(ap);

	for(nc = dbg->svc.conns; nc; nc = nc->next) {
		dc = (DbgConn*)nc->aux;
		if(dc && dc->events >= minlevel)
			netsvc_write(nc, buf, n);
	}
}

/* ------------------------------------------------------------- registers */

enum {
	R_PC, R_AC, R_IO, R_MB, R_MA, R_IR, R_OV, R_PF, R_EPC, R_EMA,
	R_TA, R_TW, R_SS, R_ETA,
	R_RUN, R_RUNEN, R_CYC, R_DF1, R_DF2, R_BC, R_HSC, R_RIM, R_SBM,
	R_EXD, R_IOH, R_IOC, R_IOS,
	NREG
};

static const struct {
	const char *name;
	int writable;
	int width;		/* octal digits */
} reginfo[NREG] = {
	[R_PC] =    { "pc",		1, 6 },
	[R_AC] =    { "ac",		1, 6 },
	[R_IO] =    { "io",		1, 6 },
	[R_MB] =    { "mb",		1, 6 },
	[R_MA] =    { "ma",		1, 6 },
	[R_IR] =    { "ir",		1, 2 },
	[R_OV] =    { "ov",		1, 1 },
	[R_PF] =    { "pf",		1, 2 },
	[R_EPC] =   { "epc",		1, 2 },
	[R_EMA] =   { "ema",		1, 2 },
	[R_TA] =    { "ta",		0, 6 },
	[R_TW] =    { "tw",		0, 6 },
	[R_SS] =    { "ss",		0, 2 },
	[R_ETA] =   { "eta",		0, 2 },
	[R_RUN] =   { "run",		0, 1 },
	[R_RUNEN] = { "run_enable",	0, 1 },
	[R_CYC] =   { "cyc",		0, 1 },
	[R_DF1] =   { "df1",		0, 1 },
	[R_DF2] =   { "df2",		0, 1 },
	[R_BC] =    { "bc",		0, 1 },
	[R_HSC] =   { "hsc",		0, 1 },
	[R_RIM] =   { "rim",		0, 1 },
	[R_SBM] =   { "sbm",		0, 1 },
	[R_EXD] =   { "exd",		0, 1 },
	[R_IOH] =   { "ioh",		0, 1 },
	[R_IOC] =   { "ioc",		0, 1 },
	[R_IOS] =   { "ios",		0, 1 },
};

static int
findreg(const char *name)
{
	int i;
	for(i = 0; i < NREG; i++)
		if(ceq(reginfo[i].name, name))
			return i;
	return -1;
}

static Addr
curaddr(PDP1 *pdp)
{
	return (pdp->epc | PC) & (MAXMEM-1);
}

static Word
regget(PDP1 *pdp, int r)
{
	switch(r) {
	case R_PC: return curaddr(pdp);
	case R_AC: return AC;
	case R_IO: return IO;
	case R_MB: return MB;
	case R_MA: return (pdp->ema | MA) & (MAXMEM-1);
	case R_IR: return IR;
	case R_OV: return pdp->ov1;
	case R_PF: return pdp->pf;
	case R_EPC: return pdp->epc >> 12;
	case R_EMA: return pdp->ema >> 12;
	case R_TA: return (pdp->eta | pdp->ta) & (MAXMEM-1);
	case R_TW: return pdp->tw;
	case R_SS: return pdp->ss;
	case R_ETA: return pdp->eta >> 12;
	case R_RUN: return pdp->run;
	case R_RUNEN: return pdp->run_enable;
	case R_CYC: return pdp->cyc;
	case R_DF1: return pdp->df1;
	case R_DF2: return pdp->df2;
	case R_BC: return pdp->bc;
	case R_HSC: return pdp->hsc;
	case R_RIM: return pdp->rim;
	case R_SBM: return pdp->sbm;
	case R_EXD: return pdp->exd;
	case R_IOH: return pdp->ioh;
	case R_IOC: return pdp->ioc;
	case R_IOS: return pdp->ios;
	}
	return 0;
}

static void
regset(PDP1 *pdp, int r, Word v)
{
	switch(r) {
	case R_PC: PC = v & ADDRMASK; pdp->epc = v & EXTMASK; break;
	case R_AC: AC = v & WORDMASK; break;
	case R_IO: IO = v & WORDMASK; break;
	case R_MB: MB = v & WORDMASK; break;
	case R_MA: MA = v & ADDRMASK; pdp->ema = v & EXTMASK; break;
	case R_IR: IR = v & 037; break;
	case R_OV: pdp->ov1 = !!v; break;
	case R_PF: pdp->pf = v & 077; break;
	case R_EPC: pdp->epc = (v << 12) & EXTMASK; break;
	case R_EMA: pdp->ema = (v << 12) & EXTMASK; break;
	}
}

static int
regprint(PDP1 *pdp, int r, char *buf, int sz)
{
	return snprintf(buf, sz, "%s=%0*o", reginfo[r].name,
		reginfo[r].width, regget(pdp, r));
}

static const int shortstatus[] = {
	R_RUN, R_CYC, R_DF1, R_PC, R_AC, R_IO, R_MA, R_MB, R_IR, R_OV,
	R_PF, R_SS
};
static const int fullstatus[] = {
	R_RUNEN, R_DF2, R_BC, R_HSC, R_RIM, R_SBM, R_EXD, R_IOH, R_IOC,
	R_IOS, R_EPC, R_EMA, R_ETA, R_TA, R_TW
};

static char*
statusline(PDP1 *pdp, char *buf, int sz, int full)
{
	int i, n;

	n = 0;
	for(i = 0; i < nelem(shortstatus); i++) {
		n += regprint(pdp, shortstatus[i], buf+n, sz-n);
		n += snprintf(buf+n, sz-n, " ");
	}
	n += snprintf(buf+n, sz-n, "at=%06o stop=%s",
		pdp->core[curaddr(pdp)], reasons[dbg->reason]);
	if(full)
		for(i = 0; i < nelem(fullstatus); i++) {
			n += snprintf(buf+n, sz-n, " ");
			n += regprint(pdp, fullstatus[i], buf+n, sz-n);
		}
	return buf;
}

/* ------------------------------------------------------------ conditions */

/* <cond> := <reg> ("="|"!=") <octal> | "M[" <addr> "]" ("="|"!=") <octal>
 * Deliberately not an expression language. */
static int
parsecond(char **argv, int argc, Cond *c)
{
	char buf[256], *s, *p;
	int n, i;

	n = 0;
	for(i = 0; i < argc; i++) {
		int l = strlen(argv[i]);
		if(n + l >= sizeof(buf))
			return 0;
		memcpy(buf+n, argv[i], l);
		n += l;
	}
	buf[n] = '\0';
	if(n == 0)
		return 0;

	memset(c, 0, sizeof(*c));
	s = buf;
	if(s[0] == 'M' || s[0] == 'm') {
		if(s[1] != '[')
			return 0;
		p = strchr(s, ']');
		if(p == nil)
			return 0;
		*p = '\0';
		if(!octal(s+2, &c->val) || c->val >= MAXMEM)
			return 0;
		c->kind = 2;
		c->addr = c->val;
		s = p+1;
	} else {
		p = s;
		while(*p && *p != '=' && *p != '!')
			p++;
		if(*p == '\0')
			return 0;
		i = *p;
		*p = '\0';
		c->reg = findreg(s);
		*p = i;
		if(c->reg < 0)
			return 0;
		c->kind = 1;
		s = p;
	}

	if(s[0] == '!' && s[1] == '=') {
		c->ne = 1;
		s += 2;
	} else if(s[0] == '=')
		s += 1;
	else
		return 0;
	return octal(s, &c->val);
}

static int
condtrue(PDP1 *pdp, Cond *c)
{
	Word v;
	if(c->kind == 0)
		return 0;
	v = c->kind == 2 ? pdp->core[c->addr] : regget(pdp, c->reg);
	return c->ne ? v != c->val : v == c->val;
}

static int
condprint(Cond *c, char *buf, int sz)
{
	if(c->kind == 0)
		return 0;
	if(c->kind == 2)
		return snprintf(buf, sz, " if M[%06o]%s%06o",
			c->addr, c->ne ? "!=" : "=", c->val);
	return snprintf(buf, sz, " if %s%s%06o",
		reginfo[c->reg].name, c->ne ? "!=" : "=", c->val);
}

/* ----------------------------------------------------------- watchpoints */

static void
recalcwp(void)
{
	int i;
	dbg_anywp = 0;
	for(i = 0; i < MAXWP; i++)
		if(dbg->wps[i].used)
			dbg_anywp = 1;
}

void
dbg_readmem(PDP1 *pdp, Addr a, Word val)
{
	int i;

	dbg->lastreadaddr = a;
	dbg->lastreadval = val;
	dbg->havelastread = 1;
	if(dbg->wppend)
		return;
	for(i = 0; i < MAXWP; i++)
		if(dbg->wps[i].used && dbg->wps[i].addr == a &&
		   (dbg->wps[i].rw & 1)) {
			dbg->wppend = 1;
			dbg->wpaddr = a;
			dbg->wpold = val;
			dbg->wpnew = val;
			dbg->wprw = 1;
			dbg->wppc = dbg->haveprev ? dbg->prevpc : curaddr(pdp);
			return;
		}
}

void
dbg_writemem(PDP1 *pdp, Addr a, Word val)
{
	int i;

	/* core is read-restore: every reference writes the word back.
	 * Only a *changed* word is a write as far as a watchpoint cares. */
	if(dbg->wppend)
		return;
	if(!dbg->havelastread || dbg->lastreadaddr != a ||
	   dbg->lastreadval == val)
		return;
	for(i = 0; i < MAXWP; i++)
		if(dbg->wps[i].used && dbg->wps[i].addr == a &&
		   (dbg->wps[i].rw & 2)) {
			dbg->wppend = 1;
			dbg->wpaddr = a;
			dbg->wpold = dbg->lastreadval;
			dbg->wpnew = val;
			dbg->wprw = 2;
			dbg->wppc = dbg->haveprev ? dbg->prevpc : curaddr(pdp);
			return;
		}
}

/* ------------------------------------------------------------- call ring */

static int
iscall(Word w)
{
	int op = (w >> 13) & 037;
	return op == 031 || op == 007;		/* jsp, cal/jda */
}

static void
pushcall(Addr to, Addr from)
{
	Call *c;
	dbg->ringhead = (dbg->ringhead + RING - 1) % RING;
	c = &dbg->ring[dbg->ringhead];
	c->to = to;
	c->from = from;
	c->ret = (from + 1) & ADDRMASK;
	if(dbg->nring < RING)
		dbg->nring++;
}

/* ----------------------------------------------------- the fetch boundary */

static void
clearpending(DbgConn *dc)
{
	if(dc->pending == PEND_WAIT && dbg->nwait > 0)
		dbg->nwait--;
	dc->pending = PEND_NONE;
	dc->budget = 0;
	dc->cycles = 0;
	dc->trace = 0;
	dc->cond.kind = 0;
	dc->hastemp = 0;
}

static void
traceline(PDP1 *pdp, DbgConn *dc)
{
	char buf[256];
	int n = 0;

	n += snprintf(buf+n, sizeof(buf)-n, "pc=%06o inst=%06o",
		dbg->prevpc, dbg->previnst);
	if(!dc->tracechanged || AC != dc->prevac)
		n += snprintf(buf+n, sizeof(buf)-n, " ac=%06o", AC);
	if(!dc->tracechanged || IO != dc->previo)
		n += snprintf(buf+n, sizeof(buf)-n, " io=%06o", IO);
	if(!dc->tracechanged || regget(pdp, R_MA) != dc->prevma)
		n += snprintf(buf+n, sizeof(buf)-n, " ma=%06o",
			regget(pdp, R_MA));
	if(!dc->tracechanged || MB != dc->prevmb)
		n += snprintf(buf+n, sizeof(buf)-n, " mb=%06o", MB);
	if(!dc->tracechanged || pdp->ov1 != dc->prevov)
		n += snprintf(buf+n, sizeof(buf)-n, " ov=%d", pdp->ov1);
	dat(dc, "%s", buf);

	dc->prevac = AC;
	dc->previo = IO;
	dc->prevma = regget(pdp, R_MA);
	dc->prevmb = MB;
	dc->prevov = pdp->ov1;
}

/* everything a stop can be decided by, at the one place where nothing is
 * in flight and PC is the address about to be fetched */
static int
boundary(PDP1 *pdp)
{
	NetConn *nc;
	DbgConn *dc;
	Addr pc;
	int i, stop;

	pc = curaddr(pdp);
	stop = S_NONE;

	/* a watchpoint fired mid-instruction; the instruction has finished */
	if(dbg->wppend) {
		dbg->wppend = 0;
		event(EV_ALL, "!wp addr=%06o old=%06o new=%06o pc=%06o",
			dbg->wpaddr, dbg->wpold, dbg->wpnew, dbg->wppc);
		dbg->reason = S_WATCH;
		return 1;
	}

	/* the instruction recorded at the previous boundary has retired */
	if(dbg->haveprev && iscall(dbg->previnst))
		pushcall(pc, dbg->prevpc);

	for(nc = dbg->svc.conns; nc; nc = nc->next) {
		dc = (DbgConn*)nc->aux;
		if(dc == nil || dc->pending != PEND_RUN)
			continue;
		if(dc->trace && dbg->haveprev) {
			traceline(pdp, dc);
			dc->trace--;
		}
		if(dc->hastemp && dc->tempbp == pc)
			stop = S_BREAK;
		if(dc->cond.kind && condtrue(pdp, &dc->cond))
			stop = S_COND;
		if(dc->budget > 0 && !dc->cycles && --dc->budget == 0)
			if(stop == S_NONE)
				stop = S_STEP;
	}

	for(i = 0; i < MAXBP; i++)
		if(dbg->bps[i].used && dbg->bps[i].addr == pc &&
		   (dbg->bps[i].cond.kind == 0 ||
		    condtrue(pdp, &dbg->bps[i].cond))) {
			stop = S_BREAK;
			break;
		}

	dbg->prevpc = pc;
	dbg->previnst = pdp->core[pc];
	dbg->haveprev = 1;

	if(stop == S_NONE)
		return 0;
	if(stop == S_BREAK)
		event(EV_ALL, "!bp addr=%06o", pc);
	dbg->reason = stop;
	return 1;
}

static int
cyclebudget(void)
{
	NetConn *nc;
	DbgConn *dc;
	int stop = 0;

	for(nc = dbg->svc.conns; nc; nc = nc->next) {
		dc = (DbgConn*)nc->aux;
		if(dc == nil || dc->pending != PEND_RUN || !dc->cycles)
			continue;
		if(dc->budget > 0 && --dc->budget == 0)
			stop = 1;
	}
	if(stop)
		dbg->reason = S_CYCLE;
	return stop;
}

int
dbgfetch(PDP1 *pdp)
{
	dbg->running = 1;
	if(atfetch(pdp) && boundary(pdp))
		return 1;
	return cyclebudget();
}

void
dbgstop(PDP1 *pdp)
{
	/* at a boundary nothing is in flight, so PC/cyc/df1/bc are already a
	 * valid resume state.  This is deliberately not the STOP-key path,
	 * which lets the current instruction finish. */
	pdp->run = 0;
	pdp->run_enable = 0;
	dbg->selfstop = 1;
}

/* ----------------------------------------------------------- stop reports */

static int
classify(PDP1 *pdp)
{
	if(!pdp->power_sw)
		return S_POWER;
	if(IR_OPR && (MB & 0000400))		/* B9: real HLT, 760400 */
		return S_HALT;
	if(IR_INCORR)				/* an undefined opcode */
		return S_ILLEGAL;
	if(pdp->single_cyc_sw || pdp->single_inst_sw)
		return S_MANUAL;
	if(!pdp->run_enable)
		return S_STOP;
	return S_MANUAL;
}

static void
reportstop(PDP1 *pdp)
{
	char st[512];
	NetConn *nc;
	DbgConn *dc;

	if(!dbg->selfstop)
		dbg->reason = classify(pdp);
	dbg->selfstop = 0;
	dbg->running = 0;
	dbg->wppend = 0;

	statusline(pdp, st, sizeof(st), 0);

	for(nc = dbg->svc.conns; nc; nc = nc->next) {
		dc = (DbgConn*)nc->aux;
		if(dc == nil)
			continue;
		dc->answered = 0;
		if(dc->pending != PEND_NONE) {
			ok(dc, "%s", st);
			clearpending(dc);
			dc->answered = 1;
		}
	}

	/* a connection whose own + already carried the stop does not also
	 * get the event (spec §6) */
	for(nc = dbg->svc.conns; nc; nc = nc->next) {
		dc = (DbgConn*)nc->aux;
		if(dc == nil || dc->events < EV_STOP || dc->answered)
			continue;
		netsvc_print(nc, "!stop reason=%s %s\n", reasons[dbg->reason], st);
	}
}

/* --------------------------------------------------------- panel override */

static void
paneledge(PDP1 *pdp)
{
	static const struct { int mask; const char *name; } keys[] = {
		{ K_START, "start" }, { K_STOP, "stop" }, { K_CONT, "cont" },
		{ K_EXAM, "exam" }, { K_DEP, "dep" }, { K_READIN, "readin" },
	};
	int now, i;

	now = 0;
	if(pdp->start_sw) now |= K_START;
	if(pdp->stop_sw) now |= K_STOP;
	if(pdp->continue_sw) now |= K_CONT;
	if(pdp->examine_sw) now |= K_EXAM;
	if(pdp->deposit_sw) now |= K_DEP;
	if(pdp->readin_sw) now |= K_READIN;

	for(i = 0; i < nelem(keys); i++)
		if((now & keys[i].mask) && !(dbg->pkeys & keys[i].mask))
			event(EV_ALL, "!panel key=%s", keys[i].name);
	dbg->pkeys = now;

	if(pdp->power_sw != dbg->ppower) {
		dbg->ppower = pdp->power_sw;
		event(EV_ALL, "!panel power=%d", dbg->ppower);
	}
}

void
dbgoverride(PDP1 *pdp)
{
	if(dbg == nil)
		return;

	/* what the real panel says, before we meddle with it.  panel off
	 * needs this to refuse to strand the machine with POWER off. */
	dbg->panelpower = pdp->power_sw;

	/* human activity on the main panel, reported whether or not the
	 * override is armed, and sampled before our own keys go on */
	paneledge(pdp);

	if(testmode)
		pdp->power_sw = 1;

	if(dbg->armed) {
		if(dbg->haveta) { pdp->ta = dbg->ta & ADDRMASK; pdp->eta = dbg->ta & EXTMASK; }
		if(dbg->havetw) pdp->tw = dbg->tw;
		if(dbg->havess) pdp->ss = dbg->ss;
		if(dbg->havepower) pdp->power_sw = dbg->power;
		if(dbg->havesstep) pdp->single_cyc_sw = dbg->sstep;
		if(dbg->havesinst) pdp->single_inst_sw = dbg->sinst;
		if(dbg->haveextend) pdp->extend_sw = dbg->extend;
	}

	/* go <addr> and readin <addr> need TA for exactly the pass in which
	 * they press the key, armed or not */
	if(dbg->onceta) {
		pdp->ta = dbg->oncetaval & ADDRMASK;
		pdp->eta = dbg->oncetaval & EXTMASK;
	}

	if(dbg->keys & (K_START|K_STARTUP)) pdp->start_sw = 1;
	if(dbg->keys & K_STARTUP) pdp->sbm_start_sw = 1;
	if(dbg->keys & K_STOP) pdp->stop_sw = 1;
	if(dbg->keys & K_CONT) pdp->continue_sw = 1;
	if(dbg->keys & K_EXAM) pdp->examine_sw = 1;
	if(dbg->keys & K_DEP) pdp->deposit_sw = 1;
	if(dbg->keys & K_READIN) pdp->readin_sw = 1;
	if(dbg->keys & K_FEED) pdp->tape_feed = 1;

	if(pdp->run)
		dbg->running = 1;
}

/* Nonzero while the override is holding a switch the running program can
 * read for itself: TW (lat) and SS (szs).  A machine whose sense switches
 * disagree with the ones under the operator's hands is baffling, so the
 * panel lights every sense switch lamp while this is true.  The other
 * overrides (TA, SSTEP, SINST, EXTEND, POWER) show up in the lights they
 * drive already, so they do not raise the warning. */
int
dbgswoverride(void)
{
	if(dbg == nil || !dbg->armed)
		return 0;
	return dbg->havetw || dbg->havess;
}

/* Give the panel back: drop the override and everything it was holding.
 * This is 'panel off force', and it is what the reader key does. */
static int
unlockpanel(void)
{
	int had;

	had = dbg->armed || dbg->haveta || dbg->havetw || dbg->havess ||
		dbg->havepower || dbg->havesstep || dbg->havesinst ||
		dbg->haveextend;
	dbg->armed = 0;
	dbg->haveta = dbg->havetw = dbg->havess = dbg->havepower = 0;
	dbg->havesstep = dbg->havesinst = dbg->haveextend = 0;
	return had;
}

/* The tape reader key has no machine function, so it is the physical way
 * out: either position releases the override, whoever armed it and whether
 * or not they are still connected.  It is deliberately not on the network
 * — a client that could unlock could also 'panel off force'.
 *
 * Edge-triggered, so a client may re-arm even while the key is held; this
 * is an escape hatch, not a lockout.  Note that it releases POWER too, so
 * if the physical POWER switch is off the machine powers down: the panel
 * is in charge again and that is what the panel says. */
void
dbgreaderkey(PDP1 *pdp, int down)
{
	if(dbg == nil)
		return;
	if(down && !dbg->preader) {
		event(EV_ALL, "!panel key=reader");
		if(unlockpanel())
			event(EV_ALL, "!panel override=off by=reader");
	}
	dbg->preader = down;
}

/* ---------------------------------------------------------- the commands */

static int
running(PDP1 *pdp)
{
	return pdp->run;
}

static int
denied(DbgConn *dc, int write)
{
	if(!write)
		return 0;
	if(dbg->claimer && dbg->claimer != dc) {
		err(dc, "?busy another connection holds the claim");
		return 1;
	}
	return 0;
}

static void
startrun(DbgConn *dc, long budget, int cycles)
{
	dc->pending = PEND_RUN;
	dc->budget = budget;
	dc->cycles = cycles;
	dbg->running = 1;
}

static void
snapshot(PDP1 *pdp, DbgConn *dc)
{
	/* the boundary bookkeeping starts from where we are now: the first
	 * fetch after CONTINUE happens before dbgfetch ever sees it */
	dbg->prevpc = curaddr(pdp);
	dbg->previnst = pdp->core[dbg->prevpc];
	dbg->haveprev = 1;
	dc->prevac = AC;
	dc->previo = IO;
	dc->prevma = regget(pdp, R_MA);
	dc->prevmb = MB;
	dc->prevov = pdp->ov1;
}

static const char *helptext[] = {
	"hello                      protocol greeting",
	"help [cmd]                 this",
	"events none|stop|all       event subscription (default stop)",
	"claim / release            advisory exclusion",
	"quit                       close the connection",
	"e|examine <addr> [n]       examine core (n decimal, or addr-addr)",
	"d|deposit <addr> <w>...    deposit, refused while running",
	"poke <addr> <w>...         deposit, never refused",
	"z [<addr> <n>]             zero core",
	"reg|r [<reg>...]           read registers ('r <file>' is the reader)",
	"w <reg> <val>              write a register, refused while running",
	"s [full]                   status line",
	"go [<addr>]                START at addr, or CONTINUE; returns at once",
	"stop                       STOP key",
	"step [n]                   n instructions",
	"cycle [n]                  n memory cycles",
	"next [n]                   step over jsp/jda/cal",
	"run <n> [if <cond>]        at most n instructions",
	"until <addr> [n]           temporary breakpoint, then go",
	"trace <n> [changed]        one line per instruction retired",
	"wait [<ms>]                block until the machine stops",
	"readin [<addr>]            READ-IN key",
	"b [<addr> [if <cond>]]     breakpoint, or list",
	"ub <addr> | ub *           remove breakpoints",
	"wp <addr> [r|w|rw]         watchpoint (default w)",
	"uwp <addr> | uwp *         remove watchpoints",
	"back [n]                   the call ring, 0 = most recent",
	"panel [on|off [force]]     arm/disarm the switch override",
	"key <name> [up]            start stop cont exam dep readin feed",
	"sw [<name> [<val>]]        ta tw ss sstep sinst extend power",
	"reader|r [<file>]          mount/unmount paper tape",
	"punch|p [<file>]           mount/unmount punch",
	"load|l <file>              read a RIM tape into core",
	"dpy|display [host [port]]  connect to a display program",
	"muldiv|audio [on|off]      options",
	"sbs [1|16]  pen [<n>]      options (decimal)",
	"pen click <x> <y> [<ms>]   click the light pen (decimal, y up)",
	"NB: opcode 0 is not HLT.  Real HLT is 760400; 0 stops as ?illegal.",
	"NB: sense switch/flag N is bit 040>>(N-1).  Switch 1 is 40, not 1.",
	"NB: overriding tw or ss lights every sense switch lamp, and the tape",
	"    reader key releases the override from the panel itself.",
};

static void
docmd(PDP1 *pdp, DbgConn *dc, char *line)
{
	char *argv[MAXARG], buf[512];
	char raw[NETLINE+1];
	int argc, i, n, r;
	long l;
	Word a, v;

	strncpy(raw, line, sizeof(raw)-1);
	raw[sizeof(raw)-1] = '\0';
	argc = tokenize(line, argv, MAXARG);
	if(argc == 0) {
		ok(dc, "ok");
		return;
	}

	/* --- session --- */
	if(ceq(argv[0], "hello")) {
		ok(dc, "proto=1 machine=pdp1 maxmem=%o opts=muldiv,extend,sbs16,symgen",
			MAXMEM);
		return;
	}
	if(ceq(argv[0], "help") || ceq(argv[0], "?")) {
		for(i = 0; i < nelem(helptext); i++)
			if(argc < 2 || strncmp(helptext[i], argv[1], strlen(argv[1])) == 0)
				dat(dc, "%s", helptext[i]);
		ok(dc, "ok");
		return;
	}
	if(ceq(argv[0], "events")) {
		if(argc == 2) {
			if(ceq(argv[1], "none")) dc->events = EV_NONE;
			else if(ceq(argv[1], "stop")) dc->events = EV_STOP;
			else if(ceq(argv[1], "all")) dc->events = EV_ALL;
			else { err(dc, "?arg none|stop|all"); return; }
		} else if(argc != 1) {
			err(dc, "?arg events [none|stop|all]");
			return;
		}
		ok(dc, "events=%s", dc->events == EV_NONE ? "none" :
			dc->events == EV_STOP ? "stop" : "all");
		return;
	}
	if(ceq(argv[0], "claim")) {
		if(dbg->claimer && dbg->claimer != dc) {
			err(dc, "?busy another connection holds the claim");
			return;
		}
		dbg->claimer = dc;
		dc->claimed = 1;
		ok(dc, "claim=1");
		return;
	}
	if(ceq(argv[0], "release")) {
		if(dbg->claimer == dc)
			dbg->claimer = nil;
		dc->claimed = 0;
		ok(dc, "claim=0");
		return;
	}
	if(ceq(argv[0], "quit") || ceq(argv[0], "bye")) {
		ok(dc, "bye");
		netsvc_close(dc->nc);
		return;
	}

	/* --- memory --- */
	if(ceq(argv[0], "e") || ceq(argv[0], "examine") || ceq(argv[0], "ex")) {
		char *dash;
		long cnt = 1;
		if(argc < 2 || argc > 3) { err(dc, "?arg e <addr> [n]"); return; }
		dash = strchr(argv[1]+1, '-');
		if(dash) {
			Word b;
			*dash = '\0';
			if(!octal(argv[1], &a) || !octal(dash+1, &b)) {
				err(dc, "?arg octal address range"); return; }
			if(a >= MAXMEM || b >= MAXMEM) { err(dc, "?addr out of core"); return; }
			if(b < a) { err(dc, "?arg empty range"); return; }
			cnt = b - a + 1;
			if(argc == 3) { err(dc, "?arg e <addr>-<addr>"); return; }
		} else {
			if(!octal(argv[1], &a)) { err(dc, "?arg octal address"); return; }
			if(a >= MAXMEM) { err(dc, "?addr out of core"); return; }
			if(argc == 3 && !decimal(argv[2], &cnt)) {
				err(dc, "?arg decimal count"); return; }
		}
		if(cnt < 1 || cnt > MAXEXAM) { err(dc, "?limit at most %d words", MAXEXAM); return; }
		for(i = 0; i < cnt; ) {
			n = snprintf(buf, sizeof(buf), "%06o", (a+i) & (MAXMEM-1));
			for(r = 0; r < 8 && i < cnt; r++, i++)
				n += snprintf(buf+n, sizeof(buf)-n, " %06o",
					pdp->core[(a+i) & (MAXMEM-1)]);
			dat(dc, "%s", buf);
		}
		ok(dc, "%ld", cnt);
		return;
	}
	if(ceq(argv[0], "d") || ceq(argv[0], "deposit") || ceq(argv[0], "dep") ||
	   ceq(argv[0], "poke")) {
		int force = ceq(argv[0], "poke");
		if(denied(dc, 1)) return;
		if(argc < 3) { err(dc, "?arg %s <addr> <word>...", argv[0]); return; }
		if(!octal(argv[1], &a)) { err(dc, "?arg octal address"); return; }
		if(a >= MAXMEM) { err(dc, "?addr out of core"); return; }
		if(a + argc-2 > MAXMEM) { err(dc, "?addr runs off the end of core"); return; }
		for(i = 2; i < argc; i++)
			if(!octal(argv[i], &v) || v > WORDMASK) {
				err(dc, "?arg octal word: %s", argv[i]); return; }
		if(!force && running(pdp)) {
			err(dc, "?state machine is running; use poke");
			return;
		}
		for(i = 2; i < argc; i++) {
			octal(argv[i], &v);
			pdp->core[(a + i-2) & (MAXMEM-1)] = v;
		}
		ok(dc, "%d", argc-2);
		return;
	}
	if(ceq(argv[0], "z")) {
		long cnt;
		if(denied(dc, 1)) return;
		if(running(pdp)) { err(dc, "?state machine is running"); return; }
		if(argc == 1) {
			memset(pdp->core, 0, sizeof(pdp->core));
			ok(dc, "%d", MAXMEM);
			return;
		}
		if(argc != 3) { err(dc, "?arg z [<addr> <n>]"); return; }
		if(!octal(argv[1], &a)) { err(dc, "?arg octal address"); return; }
		if(a >= MAXMEM) { err(dc, "?addr out of core"); return; }
		if(!decimal(argv[2], &cnt)) { err(dc, "?arg decimal count"); return; }
		if(a + cnt > MAXMEM) { err(dc, "?addr runs off the end of core"); return; }
		for(i = 0; i < cnt; i++)
			pdp->core[a+i] = 0;
		ok(dc, "%ld", cnt);
		return;
	}

	/* --- registers.  'r' is the reader unless its argument names a
	 * register: port 1040's r <file> has to keep working verbatim. --- */
	if(ceq(argv[0], "reg") ||
	   (ceq(argv[0], "r") && argc > 1 && findreg(argv[1]) >= 0)) {
		n = 0;
		if(argc == 1) {
			for(i = 0; i < NREG; i++) {
				if(i)
					n += snprintf(buf+n, sizeof(buf)-n, " ");
				n += regprint(pdp, i, buf+n, sizeof(buf)-n);
			}
		} else
			for(i = 1; i < argc; i++) {
				r = findreg(argv[i]);
				if(r < 0) { err(dc, "?reg %s", argv[i]); return; }
				if(i > 1)
					n += snprintf(buf+n, sizeof(buf)-n, " ");
				n += regprint(pdp, r, buf+n, sizeof(buf)-n);
			}
		ok(dc, "%s", buf);
		return;
	}
	if(ceq(argv[0], "w")) {
		if(denied(dc, 1)) return;
		if(argc != 3) { err(dc, "?arg w <reg> <val>"); return; }
		r = findreg(argv[1]);
		if(r < 0) { err(dc, "?reg %s", argv[1]); return; }
		if(!reginfo[r].writable) {
			err(dc, "?reg %s is not writable%s", argv[1],
				r >= R_TA && r <= R_ETA ? " (a switch: use sw)" : "");
			return;
		}
		if(!octal(argv[2], &v)) { err(dc, "?arg octal value"); return; }
		if(r == R_PC && v >= MAXMEM) { err(dc, "?addr out of core"); return; }
		if(running(pdp)) { err(dc, "?state machine is running"); return; }
		regset(pdp, r, v);
		if(r == R_PC) {
			/* setting PC means "the next instruction is at this
			 * address", which is only meaningful at a fetch
			 * boundary.  A machine halted mid-cycle would
			 * otherwise resume into the middle of the old
			 * instruction.  This is what makes 'w pc' the whole
			 * of the EXAMINE/START/restore dance. */
			pdp->cyc = pdp->df1 = pdp->df2 = 0;
			pdp->bc = pdp->hsc = 0;
			pdp->cychack = 0;
			/* and the in-out transfer with it, exactly as sc()
			 * does for START.  ioc is the command enable, and it
			 * is only recomputed at TP2 of an IOT that follows
			 * another IOT -- so a machine that has never been
			 * started still has ioc=0, the first IOT gets no
			 * device pulse, and an in-out wait then waits for a
			 * completion nobody ever asked for.  It hangs on the
			 * instruction forever.  Left over from the same
			 * mid-cycle problem as the flip-flops above. */
			pdp->ioc = 1;
			pdp->ioh = pdp->ios = pdp->ihs = 0;
		}
		regprint(pdp, r, buf, sizeof(buf));
		ok(dc, "%s", buf);
		return;
	}

	/* --- run control --- */
	if(ceq(argv[0], "s") || ceq(argv[0], "show") || ceq(argv[0], "status")) {
		int full = argc > 1 && ceq(argv[1], "full");
		if(argc > 2 || (argc == 2 && !full)) { err(dc, "?arg s [full]"); return; }
		ok(dc, "%s", statusline(pdp, buf, sizeof(buf), full));
		return;
	}
	if(ceq(argv[0], "go") || ceq(argv[0], "cont") || ceq(argv[0], "c")) {
		if(denied(dc, 1)) return;
		if(argc > 2) { err(dc, "?arg go [<addr>]"); return; }
		if(argc == 2) {
			if(!octal(argv[1], &a)) { err(dc, "?arg octal address"); return; }
			if(a >= MAXMEM) { err(dc, "?addr out of core"); return; }
			dbg->onceta = 1;
			dbg->oncetaval = a;
			dbg->keys |= K_START;
		} else
			dbg->keys |= K_CONT;
		snapshot(pdp, dc);
		dbg->running = 1;
		dbg->reason = S_NONE;
		ok(dc, "run=1");
		return;
	}
	if(ceq(argv[0], "stop")) {
		if(denied(dc, 1)) return;
		if(!running(pdp)) { ok(dc, "%s", statusline(pdp, buf, sizeof(buf), 0)); return; }
		dbg->keys |= K_STOP;
		startrun(dc, -1, 0);
		return;
	}
	if(ceq(argv[0], "readin")) {
		if(denied(dc, 1)) return;
		if(argc > 2) { err(dc, "?arg readin [<addr>]"); return; }
		if(argc == 2) {
			if(!octal(argv[1], &a)) { err(dc, "?arg octal address"); return; }
			if(a >= MAXMEM) { err(dc, "?addr out of core"); return; }
			dbg->onceta = 1;
			dbg->oncetaval = a;
		}
		dbg->keys |= K_READIN;
		snapshot(pdp, dc);
		dbg->running = 1;
		dbg->reason = S_NONE;
		ok(dc, "run=1");
		return;
	}
	if(ceq(argv[0], "step") || ceq(argv[0], "s1") || ceq(argv[0], "cycle") ||
	   ceq(argv[0], "next") || ceq(argv[0], "run") || ceq(argv[0], "until") ||
	   ceq(argv[0], "trace")) {
		int cycles = ceq(argv[0], "cycle");
		long budget = 1;
		int first = 1;

		if(denied(dc, 1)) return;
		if(dc->pending != PEND_NONE) { err(dc, "?busy a command is already pending"); return; }

		memset(&dc->cond, 0, sizeof(dc->cond));
		dc->hastemp = 0;
		dc->trace = 0;
		dc->tracechanged = 0;

		if(ceq(argv[0], "until")) {
			if(argc < 2) { err(dc, "?arg until <addr> [n]"); return; }
			if(!octal(argv[1], &a)) { err(dc, "?arg octal address"); return; }
			if(a >= MAXMEM) { err(dc, "?addr out of core"); return; }
			dc->tempbp = a;
			dc->hastemp = 1;
			budget = MAXRUN;
			first = 2;
			if(argc > 3) { err(dc, "?arg until <addr> [n]"); return; }
			if(argc == 3) {
				if(!decimal(argv[2], &budget)) { err(dc, "?arg decimal count"); return; }
				first = 3;
			}
		} else if(ceq(argv[0], "run")) {
			if(argc < 2) { err(dc, "?arg run <n> [if <cond>]"); return; }
			if(!decimal(argv[1], &budget)) { err(dc, "?arg decimal count"); return; }
			first = 2;
		} else {
			if(argc > 1 && !ceq(argv[1], "changed")) {
				if(!decimal(argv[1], &budget)) { err(dc, "?arg decimal count"); return; }
				first = 2;
			} else
				first = 1;
		}
		if(budget < 1 || budget > MAXRUN) { err(dc, "?limit count out of range"); return; }

		if(ceq(argv[0], "trace")) {
			if(budget > MAXEXAM) { err(dc, "?limit at most %d lines", MAXEXAM); return; }
			dc->trace = budget;
			if(first < argc && ceq(argv[first], "changed")) {
				dc->tracechanged = 1;
				first++;
			}
		}
		if(ceq(argv[0], "next")) {
			Word inst = pdp->core[curaddr(pdp)];
			if(iscall(inst)) {
				dc->tempbp = (curaddr(pdp) + 1) & (MAXMEM-1);
				dc->hastemp = 1;
				budget = MAXRUN;
			}
		}
		if(first < argc && ceq(argv[first], "if")) {
			if(!parsecond(argv+first+1, argc-first-1, &dc->cond)) {
				err(dc, "?arg condition: <reg>|M[<addr>] =|!= <octal>");
				return;
			}
			first = argc;
		}
		if(first < argc) { err(dc, "?arg unknown argument: %s", argv[first]); return; }

		snapshot(pdp, dc);
		dbg->reason = S_NONE;
		if(!running(pdp))
			dbg->keys |= K_CONT;
		startrun(dc, budget, cycles);
		return;
	}
	if(ceq(argv[0], "wait")) {
		long ms = 0;
		if(argc > 2) { err(dc, "?arg wait [<ms>]"); return; }
		if(argc == 2 && !decimal(argv[1], &ms)) { err(dc, "?arg decimal ms"); return; }
		if(dc->pending != PEND_NONE) { err(dc, "?busy a command is already pending"); return; }
		if(!running(pdp)) {
			ok(dc, "%s", statusline(pdp, buf, sizeof(buf), 0));
			return;
		}
		dc->pending = PEND_WAIT;
		dc->budget = 0;
		dc->deadline = ms ? gettime() + (u64)ms*1000*1000 : 0;
		dbg->nwait++;
		return;
	}

	/* --- breakpoints and watchpoints --- */
	if(ceq(argv[0], "b") || ceq(argv[0], "break")) {
		if(argc == 1) {
			n = 0;
			for(i = 0; i < MAXBP; i++)
				if(dbg->bps[i].used) {
					r = snprintf(buf, sizeof(buf), "%06o", dbg->bps[i].addr);
					condprint(&dbg->bps[i].cond, buf+r, sizeof(buf)-r);
					dat(dc, "%s", buf);
					n++;
				}
			ok(dc, "%d", n);
			return;
		}
		if(denied(dc, 1)) return;
		if(!octal(argv[1], &a)) { err(dc, "?arg octal address"); return; }
		if(a >= MAXMEM) { err(dc, "?addr out of core"); return; }
		for(i = 0; i < MAXBP; i++)
			if(dbg->bps[i].used && dbg->bps[i].addr == a)
				break;
		if(i == MAXBP)
			for(i = 0; i < MAXBP; i++)
				if(!dbg->bps[i].used)
					break;
		if(i == MAXBP) { err(dc, "?limit at most %d breakpoints", MAXBP); return; }
		memset(&dbg->bps[i], 0, sizeof(dbg->bps[i]));
		dbg->bps[i].used = 1;
		dbg->bps[i].addr = a;
		if(argc > 2) {
			if(!ceq(argv[2], "if") ||
			   !parsecond(argv+3, argc-3, &dbg->bps[i].cond)) {
				dbg->bps[i].used = 0;
				err(dc, "?arg b <addr> [if <cond>]");
				return;
			}
		}
		n = 0;
		for(i = 0; i < MAXBP; i++)
			if(dbg->bps[i].used)
				n++;
		ok(dc, "%d", n);
		return;
	}
	if(ceq(argv[0], "ub") || ceq(argv[0], "nobreak")) {
		if(denied(dc, 1)) return;
		if(argc != 2) { err(dc, "?arg ub <addr> | ub *"); return; }
		n = 0;
		if(strcmp(argv[1], "*") == 0) {
			for(i = 0; i < MAXBP; i++)
				if(dbg->bps[i].used) {
					dbg->bps[i].used = 0;
					n++;
				}
		} else {
			if(!octal(argv[1], &a)) { err(dc, "?arg octal address"); return; }
			for(i = 0; i < MAXBP; i++)
				if(dbg->bps[i].used && dbg->bps[i].addr == a) {
					dbg->bps[i].used = 0;
					n++;
				}
		}
		ok(dc, "%d", n);
		return;
	}
	if(ceq(argv[0], "wp")) {
		int rw = 2;
		if(argc == 1) {
			n = 0;
			for(i = 0; i < MAXWP; i++)
				if(dbg->wps[i].used) {
					dat(dc, "%06o %s", dbg->wps[i].addr,
						dbg->wps[i].rw == 1 ? "r" :
						dbg->wps[i].rw == 2 ? "w" : "rw");
					n++;
				}
			ok(dc, "%d", n);
			return;
		}
		if(denied(dc, 1)) return;
		if(argc > 3) { err(dc, "?arg wp <addr> [r|w|rw]"); return; }
		if(!octal(argv[1], &a)) { err(dc, "?arg octal address"); return; }
		if(a >= MAXMEM) { err(dc, "?addr out of core"); return; }
		if(argc == 3) {
			if(ceq(argv[2], "r")) rw = 1;
			else if(ceq(argv[2], "w")) rw = 2;
			else if(ceq(argv[2], "rw")) rw = 3;
			else { err(dc, "?arg r|w|rw"); return; }
		}
		for(i = 0; i < MAXWP; i++)
			if(dbg->wps[i].used && dbg->wps[i].addr == a)
				break;
		if(i == MAXWP)
			for(i = 0; i < MAXWP; i++)
				if(!dbg->wps[i].used)
					break;
		if(i == MAXWP) { err(dc, "?limit at most %d watchpoints", MAXWP); return; }
		dbg->wps[i].used = 1;
		dbg->wps[i].addr = a;
		dbg->wps[i].rw = rw;
		recalcwp();
		n = 0;
		for(i = 0; i < MAXWP; i++)
			if(dbg->wps[i].used)
				n++;
		ok(dc, "%d", n);
		return;
	}
	if(ceq(argv[0], "uwp")) {
		if(denied(dc, 1)) return;
		if(argc != 2) { err(dc, "?arg uwp <addr> | uwp *"); return; }
		n = 0;
		if(strcmp(argv[1], "*") == 0) {
			for(i = 0; i < MAXWP; i++)
				if(dbg->wps[i].used) {
					dbg->wps[i].used = 0;
					n++;
				}
		} else {
			if(!octal(argv[1], &a)) { err(dc, "?arg octal address"); return; }
			for(i = 0; i < MAXWP; i++)
				if(dbg->wps[i].used && dbg->wps[i].addr == a) {
					dbg->wps[i].used = 0;
					n++;
				}
		}
		recalcwp();
		ok(dc, "%d", n);
		return;
	}
	if(ceq(argv[0], "back")) {
		long cnt = 16;
		if(argc > 2) { err(dc, "?arg back [n]"); return; }
		if(argc == 2 && !decimal(argv[1], &cnt)) { err(dc, "?arg decimal count"); return; }
		if(cnt > dbg->nring)
			cnt = dbg->nring;
		for(i = 0; i < cnt; i++) {
			Call *c = &dbg->ring[(dbg->ringhead + i) % RING];
			dat(dc, "%d to=%06o from=%06o ret=%06o",
				i, c->to, c->from, c->ret);
		}
		ok(dc, "%ld", cnt);
		return;
	}

	/* --- panel, tier 0 --- */
	if(ceq(argv[0], "panel")) {
		if(argc == 1) { ok(dc, "panel=%s", dbg->armed ? "on" : "off"); return; }
		if(argc > 3) { err(dc, "?arg panel [on|off [force]]"); return; }
		if(ceq(argv[1], "on")) {
			if(denied(dc, 1)) return;
			dbg->armed = 1;
			ok(dc, "panel=on");
			return;
		}
		if(!ceq(argv[1], "off")) { err(dc, "?arg panel [on|off [force]]"); return; }
		if(denied(dc, 1)) return;
		if(argc == 3 && !ceq(argv[2], "force")) {
			err(dc, "?arg panel off [force]");
			return;
		}
		if(argc < 3 && dbg->havepower && dbg->power &&
		   pdp->power_sw && !dbg->panelpower && !testmode) {
			err(dc, "?state the panel says POWER off; disarming would "
				"power the machine down.  'panel off force' if you mean it");
			return;
		}
		unlockpanel();
		ok(dc, "panel=off");
		return;
	}
	if(ceq(argv[0], "key")) {
		static const struct { const char *name; int key; } keys[] = {
			{ "start", K_START }, { "stop", K_STOP },
			{ "cont", K_CONT }, { "continue", K_CONT },
			{ "exam", K_EXAM }, { "examine", K_EXAM },
			{ "dep", K_DEP }, { "deposit", K_DEP },
			{ "readin", K_READIN }, { "reader", K_READIN },
			{ "feed", K_FEED },
		};
		if(denied(dc, 1)) return;
		if(argc < 2 || argc > 3) { err(dc, "?arg key <name> [up]"); return; }
		if(argc == 3 && !ceq(argv[2], "up")) { err(dc, "?arg key <name> [up]"); return; }
		for(i = 0; i < nelem(keys); i++)
			if(ceq(keys[i].name, argv[1])) {
				int k = keys[i].key;
				if(argc == 3 && k == K_START)
					k = K_STARTUP;
				dbg->keys |= k;
				ok(dc, "key=%s", keys[i].name);
				return;
			}
		err(dc, "?arg unknown key: %s", argv[1]);
		return;
	}
	if(ceq(argv[0], "sw")) {
		static const char *names[] = {
			"ta", "tw", "ss", "sstep", "sinst", "extend", "power"
		};
		int w;
		if(argc == 1) {
			ok(dc, "ta=%06o tw=%06o ss=%02o sstep=%d sinst=%d extend=%d power=%d",
				(pdp->eta|pdp->ta) & (MAXMEM-1), pdp->tw, pdp->ss,
				pdp->single_cyc_sw, pdp->single_inst_sw,
				pdp->extend_sw, pdp->power_sw);
			return;
		}
		if(argc > 3) { err(dc, "?arg sw [<name> [<val>]]"); return; }
		for(w = 0; w < nelem(names); w++)
			if(ceq(names[w], argv[1]))
				break;
		if(w == nelem(names)) { err(dc, "?arg unknown switch: %s", argv[1]); return; }
		if(argc == 2) {
			switch(w) {
			case 0: ok(dc, "ta=%06o", (pdp->eta|pdp->ta) & (MAXMEM-1)); return;
			case 1: ok(dc, "tw=%06o", pdp->tw); return;
			case 2: ok(dc, "ss=%02o", pdp->ss); return;
			case 3: ok(dc, "sstep=%d", pdp->single_cyc_sw); return;
			case 4: ok(dc, "sinst=%d", pdp->single_inst_sw); return;
			case 5: ok(dc, "extend=%d", pdp->extend_sw); return;
			case 6: ok(dc, "power=%d", pdp->power_sw); return;
			}
		}
		if(denied(dc, 1)) return;
		if(!dbg->armed) { err(dc, "?state the override is not armed; panel on"); return; }
		if(!octal(argv[2], &v)) { err(dc, "?arg octal value"); return; }
		switch(w) {
		case 0:
			if(v >= MAXMEM) { err(dc, "?addr out of core"); return; }
			dbg->ta = v; dbg->haveta = 1;
			ok(dc, "ta=%06o", v);
			return;
		case 1:
			if(v > WORDMASK) { err(dc, "?arg 18-bit word"); return; }
			dbg->tw = v; dbg->havetw = 1;
			ok(dc, "tw=%06o", v);
			return;
		case 2:
			if(v > 077) { err(dc, "?arg 6 sense switches"); return; }
			dbg->ss = v; dbg->havess = 1;
			ok(dc, "ss=%02o", v);
			return;
		case 3: dbg->sstep = !!v; dbg->havesstep = 1; ok(dc, "sstep=%d", !!v); return;
		case 4: dbg->sinst = !!v; dbg->havesinst = 1; ok(dc, "sinst=%d", !!v); return;
		case 5: dbg->extend = !!v; dbg->haveextend = 1; ok(dc, "extend=%d", !!v); return;
		case 6: dbg->power = !!v; dbg->havepower = 1; ok(dc, "power=%d", !!v); return;
		}
		return;
	}

	/* --- devices: port 1040's own language, unchanged --- */
	if(denied(dc, 1)) return;
	{
		char *p = handlecmd(pdp, raw, 1);
		if(cmdunknown)
			err(dc, "?cmd unknown command: %s (try help)", argv[0]);
		else if(cmdarg)
			err(dc, "?arg %s", p);
		else if(cmdfailed)
			err(dc, "?file %s", p);
		else
			ok(dc, "%s", p);
	}
}

/* -------------------------------------------------------------- plumbing */

static void
dbgaccepted(NetConn *nc)
{
	DbgConn *dc;

	dc = (DbgConn*)malloc(sizeof(DbgConn));
	if(dc == nil) {
		netsvc_close(nc);
		return;
	}
	memset(dc, 0, sizeof(DbgConn));
	dc->nc = nc;
	dc->events = EV_STOP;
	nc->aux = dc;

	if(dbg->svc.nconn > dbg->svc.maxconn) {
		reply(nc, '-', "?busy too many connections");
		netsvc_close(nc);
	}
}

static void
dbgclosed(NetConn *nc)
{
	DbgConn *dc = (DbgConn*)nc->aux;

	if(dc == nil)
		return;
	/* cancel the pending command, drop temporary breakpoints and give up
	 * the claim.  The switch override is machine-wide, not per connection,
	 * so it deliberately survives: a client that dies holding SINST leaves
	 * it held, and the way out is 'panel off force' or the reader key. */
	clearpending(dc);
	if(dbg->claimer == dc)
		dbg->claimer = nil;
	nc->aux = nil;
	free(dc);
}

static void
dbgline(NetConn *nc, char *line)
{
	DbgConn *dc = (DbgConn*)nc->aux;

	if(dc == nil)
		return;
	if(line == nil) {
		err(dc, "?arg line too long");
		return;
	}
	if(dc->pending != PEND_NONE) {
		/* at most one outstanding command per connection */
		err(dc, "?busy a command is already pending");
		return;
	}
	docmd(dbg->pdp, dc, line);
}

void
dbginit(PDP1 *pdp, int port)
{
	dbg = &dbgstate;
	memset(dbg, 0, sizeof(*dbg));
	dbg->pdp = pdp;
	dbg->reason = S_NONE;
	pdp->dbg = dbg;

	dbg->svc.port = port;
	dbg->svc.maxconn = MAXCONN;
	dbg->svc.mode = SVC_LINE;
	dbg->svc.accepted = dbgaccepted;
	dbg->svc.line = dbgline;
	dbg->svc.closed = dbgclosed;
	netsvc_add(&dbg->svc);
}

void
dbgsvc(PDP1 *pdp)
{
	NetConn *nc;
	DbgConn *dc;
	u64 now;

	/* release the keys asserted for the pass that just ran: Edge()
	 * compared them against the top-of-loop snapshot, so that was
	 * exactly one clean edge */
	dbg->keys = 0;
	dbg->onceta = 0;

	if(dbg->running && !pdp->run)
		reportstop(pdp);

	if(dbg->nwait) {
		now = gettime();
		for(nc = dbg->svc.conns; nc; nc = nc->next) {
			dc = (DbgConn*)nc->aux;
			if(dc == nil || dc->pending != PEND_WAIT)
				continue;
			if(dc->deadline && now >= dc->deadline) {
				clearpending(dc);
				err(dc, "?timeout the machine is still running");
			}
		}
	}

	netsvc_poll();
}
