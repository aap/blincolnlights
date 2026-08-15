#include "common.h"
#include "netsvc.h"
#include "pdp1.h"
#include "dbg.h"
#include "args.h"

#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/socket.h>

#include <signal.h>

typedef struct Panel Panel;
void updateswitches(PDP1 *pdp, Panel *panel);
void updatelights(PDP1 *pdp, Panel *panel);
void lightsoff(Panel *panel);
void lightson(Panel *panel);
Panel *getpanel(void);

#define Edge(sw) (pdp->sw && !prev_##sw)

int doaudio;

void
emu(PDP1 *pdp, Panel *panel)
{
	pdp->panel = panel;

	pwrclr(pdp);
	initaudio();

	bool prev_start_sw;
	bool prev_stop_sw;
	bool prev_continue_sw;
	bool prev_examine_sw;
	bool prev_deposit_sw;
	bool prev_readin_sw;
	updateswitches(pdp, panel);

	inittime();
	pdp->simtime = gettime();
	pdp->dpy[0].last = pdp->simtime;
	pdp->dpy[1].last = pdp->simtime;
	pdp->dpy[0].ncmds = 0;
	pdp->dpy[1].ncmds = 0;
	for(;;) {
		prev_start_sw = pdp->start_sw;
		prev_stop_sw = pdp->stop_sw;
		prev_continue_sw = pdp->continue_sw;
		prev_examine_sw = pdp->examine_sw;
		prev_deposit_sw = pdp->deposit_sw;
		prev_readin_sw = pdp->readin_sw;
		updateswitches(pdp, panel);

		if(pdp->power_sw) {
			if(Edge(start_sw) || Edge(continue_sw) ||
			   Edge(examine_sw) || Edge(deposit_sw)) {
				spec(pdp);
				cycle(pdp);
			}
			if(Edge(stop_sw)) pdp->run_enable = 0;
			if(Edge(readin_sw)) start_readin(pdp);

			if(pdp->rim_cycle) readin1(pdp);
			if(pdp->rim_return && --pdp->rim_return == 0 &&
			   pdp->rim) {
				// restart after reader is done
				if(IR == 0 && !pdp->stop_sw)
					readin2(pdp);
				else if(IR_DIO) {
					cycle(pdp);
					pdp->rim_cycle = 1;
				}
			}

			if(pdp->run) {	// not really correct
				if(doaudio)
					svc_audio(pdp);
				else
					stopaudio();
				if(dbgfetch(pdp))
					dbgstop(pdp);
				else
					cycle(pdp);
			 } else {
				stopaudio();
				updatelights(pdp, panel);
			}
			throttle(pdp);
			handleio(pdp);
			pdp->simtime += 5000;
		} else {
			stopaudio();
			pwrclr(pdp);

			/* magic key combo used for shutdown */
			if(pdp->start_sw && pdp->readin_sw) {
				lightson(panel);
				sleep(1);
				exit(100);
			}
			lightsoff(panel);

			pdp->simtime = gettime();
		}
		agedisplay(pdp, 0);
		agedisplay(pdp, 1);
		cli(pdp);
		dbgsvc(pdp);
	}
}

static PDP1 *thepdp;

/* Displays fan out: a new client joins the picture instead of stealing it.
 * This used to close the display the second client connected to and then
 * leak the new fd without installing it. */
static void
dpyaccepted(NetConn *c)
{
	DispCon *d = &thepdp->dpy[c->svc->port&1];

	if(c->svc->nconn == 1) {
		d->last = thepdp->simtime;
		d->agetime = 50*1000;
		d->ncmds = 0;
	}
}

/* the light pen reports back in 4-byte commands */
static void
dpydata(NetConn *c, u8 *buf, int n)
{
	int i;

	if((c->svc->port&1) != 0)
		return;		/* only the main display has a pen */
	for(i = 0; i+4 <= n; i += 4)
		dpypen(thepdp, buf[i] | buf[i+1]<<8 | buf[i+2]<<16 | buf[i+3]<<24);
}

/* the reader and punch take the raw fd: they want a blocking read */
static void
adoptptr(NetSvc *svc, int fd)
{
	close(thepdp->r_fd);
	thepdp->r_fd = fd;
}

static void
adoptptp(NetSvc *svc, int fd)
{
	close(thepdp->p_fd);
	thepdp->p_fd = fd;
}

static NetSvc ptrsvc = { .port = 1042, .maxconn = 1, .mode = SVC_RAW, .adopt = adoptptr };
static NetSvc ptpsvc = { .port = 1043, .maxconn = 1, .mode = SVC_RAW, .adopt = adoptptp };
static NetSvc dpysvc0 = { .port = 3400, .maxconn = 4, .mode = SVC_RAW,
	.accepted = dpyaccepted, .data = dpydata };
static NetSvc dpysvc1 = { .port = 3401, .maxconn = 4, .mode = SVC_RAW,
	.accepted = dpyaccepted, .data = dpydata };

void
startnet(PDP1 *pdp)
{
	thepdp = pdp;
	dbginit(pdp, 1040);	/* cli + debug, see DEBUG_PROTOCOL_SPEC.md */
	// 1041 is typewriter
	netsvc_add(&ptrsvc);
	netsvc_add(&ptpsvc);
	// even/odd for display 1 and 2
	netsvc_add(&dpysvc0);
	netsvc_add(&dpysvc1);
	pdp->dpy[0].svc = &dpysvc0;
	pdp->dpy[1].svc = &dpysvc1;
	netsvc_start();
}

char *argv0;
void
usage(void)
{
	fprintf(stderr, "usage: %s [-lt] [-D tapedir] [-h host] [-p port]\n", argv0);
	exit(1);
}

void
readmem(const char *file, Word *mem, Word size)
{
	FILE *f;
	char buf[100], *s;
	Word a;
	Word w;
	if(f = fopen(file, "r"), f == nil)
		return;
	a = 0;
	while(s = fgets(buf, 100, f)){
		while(*s){
			if(*s == ';')
				break;
			else if('0' <= *s && *s <= '7'){
				w = strtol(s, &s, 8);
				if(*s == ':' || *s == '/'){
					a = w;
					s++;
				}else if(a < size)
					mem[a++] = w;
				else
					fprintf(stderr, "Address out of range: %o\n", a++);
			}else
				s++;
		}
	}
	fclose(f);
}

void
dumpmem(const char *file, Word *mem, Word size)
{
	FILE *f;
	Word i, a;

	if(f = fopen("coremem", "w"), f == nil)
		return;

	a = 0;
	for(i = 0; i < size; i++)
		if(mem[i] != 0){
//			if(a != i){
			if(1){
				a = i;
				fprintf(f, "%06o:\n", a);
			}
			fprintf(f, "%06o\n", mem[a++]);
		}


	fclose(f);
}

// a bit ugly...
static Panel *panel;
static Word *memp;
static int memsz;
void
exitcleanup(void)
{
	if(!testmode)
		dumpmem("coremem", memp, memsz);
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
	PDP1 pdp1, *pdp = &pdp1;
	const char *host;
	int port;

	host = "localhost";
	port = 3400;
	ARGBEGIN {
	case 'l':
		/* listen on loopback only.  the ports carry a command
		 * language that can open and truncate files. */
		netlocalonly = 1;
		break;
	case 'D':
		/* where tapes named over the network are allowed to be.
		 * default is the working directory. */
		tapedir = EARGF(usage());
		break;
	case 't':
		/* headless: no coremem load/dump, POWER forced on, no tapes.
		 * for the conformance suite and anything else unattended */
		testmode = 1;
		break;
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

	memp = pdp->core;
	memsz = MAXMEM;

	srand(time(nil));
	atexit(exitcleanup);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	memset(pdp, 0, sizeof(*pdp));
	if(!testmode)
		readmem("coremem", memp, memsz);

	startpolling();

	pdp->penr = 5;

	startnet(pdp);

	if(!testmode) {
//	const char *tape = "maindec/maindec1_20.rim";
//	const char *tape = "tapes/circle.rim";
//	const char *tape = "tapes/munch.rim";
//	const char *tape = "tapes/minskytron.rim";
//	const char *tape = "tapes/spacewar2B_5.rim";
//	const char *tape = "tapes/ddt.rim";
	const char *tape = "tapes/dpys5.rim";
	pdp->muldiv_sw = 1;

	pdp->r_fd = open(tape, O_RDONLY);
	pdp->p_fd = open("punch.out", O_CREAT|O_WRONLY|O_TRUNC, 0644);
	} else {
		pdp->r_fd = -1;
		pdp->p_fd = -1;
		pdp->muldiv_sw = 1;
	}

	pdp->typ_fd.id = -1;
	int fd[2];
	socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
	pdp->typ_fd.fd = fd[0];
//	pdp->typ_fd.fd = open("/tmp/typ", O_RDWR);
//	if(pdp->typ_fd.fd < 0)
//		printf("can't open /tmp/typ\n");
	waitfd(&pdp->typ_fd);
	typtelnet(1041, fd[1]);

	emu(pdp, panel);
	return 0;	// can't happen
}
