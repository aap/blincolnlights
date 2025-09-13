#include "common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <poll.h>
#include <pthread.h>

enum {
	SE = 240,
	NOP = 241,
	BRK = 243,
	IP = 244,
	AO = 245,
	AYT = 246,
	EC = 247,
	EL = 248,
	GA = 249,
	SB = 250,
	WILL = 251,
	WONT = 252,
	DO = 253,
	DONT = 254,
	IAC = 255,

	XMITBIN = 0,
	ECHO_ = 1,
	SUPRGA = 3,
	LINEEDIT = 34,
};

#define BAUD 30

// TODO: not all characters map to ascii

#define XXX ((const char*)1)
#define Lcs ((const char*)2)
#define Ucs ((const char*)3)
// shouldn't be sent, so we ignore
// just for documentation
#define Red XXX
#define Blk XXX

static const char *fio2uni[] = {
	" ", "1", "2", "3", "4", "5", "6", "7", "8", "9", XXX, XXX, XXX, XXX, XXX, XXX,
	"0", "/", "s", "t", "u", "v", "w", "x", "y", "z", XXX, ",", Blk, Red, "\t", XXX,
	"·", "j", "k", "l", "m", "n", "o", "p", "q", "r", XXX, XXX, "-", ")", "‾", "(",
	XXX, "a", "b", "c", "d", "e", "f", "g", "h", "i", Lcs, ".", Ucs, "\b", XXX, "\r\n",

	" ", "\"", "'", "~", "⊃", "∨", "∧", "<", ">", "↑", XXX, XXX, XXX, XXX, XXX, XXX,
	"→", "?", "S", "T", "U", "V", "W", "X", "Y", "Z", XXX, "=", Blk, Red, "\t", XXX,
	"_", "J", "K", "L", "M", "N", "O", "P", "Q", "R", XXX, XXX, "+", "]", "|", "[",
	XXX, "A", "B", "C", "D", "E", "F", "G", "H", "I", Lcs, "×", Ucs, "\b", XXX, "\r\n",
};

// 100 LC
// 200 UC
static int ascii2fio[] = {
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0075, 0036,   -1,   -1,   -1, 0077,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,

	0000, 0205, 0201, 0204,   -1,   -1, 0206, 0202,
	0157, 0155, 0273, 0254, 0133, 0154, 0173, 0121,
	0120, 0101, 0102, 0103, 0104, 0105, 0106, 0107,
	0110, 0111,   -1,   -1, 0207, 0233, 0210, 0221,

	0140, 0261, 0262, 0263, 0264, 0265, 0266, 0267,
	0270, 0271, 0241, 0242, 0243, 0244, 0245, 0246,
	0247, 0250, 0251, 0222, 0223, 0224, 0225, 0226,
	0227, 0230, 0231, 0257, 0220, 0255, 0211, 0240,

	0156, 0161, 0162, 0163, 0164, 0165, 0166, 0167,
	0170, 0171, 0141, 0142, 0143, 0144, 0145, 0146,
	0147, 0150, 0151, 0122, 0123, 0124, 0125, 0126,
	0127, 0130, 0131,   -1, 0256,   -1, 0203,   -1,

// missing	replacement
// 204	⊃	#
// 205	∨	!
// 206	∧	&
// 220	→	\
// 273	×	*
// 140	·	@
// 156	‾	`
};

static int color;
static int ucase;

static void
putfio(int c, int fd)
{
	const char *s;
	int col;

	col = !!(c&0100);
	c = ucase*0100 + (c&077);

	if(color != col) {
		color = col;
		if(color == 0)
			write(fd, "\e[39;49m", 8);
		else
			write(fd, "\e[31m", 5);
	}
	s = fio2uni[c];
// TODO: synchronize ucase?
	if(s == Lcs) { ucase = 0; return; }
	if(s == Ucs) { ucase = 1; return; }
	if(s != XXX) write(fd, s, strlen(s));
}

static void
getfio(int c, int fd, int localfd)
{
	char s[2];
	int n;

	n = 0;
	if(c & 0300){
		if(c & 0100 && ucase)
			s[n++] = 072;
		else if(c & 0200 && !ucase)
			s[n++] = 074;
	}
	s[n++] = c & 077;
	write(fd, s, n);

	// local echo
	int i;
	for(i = 0; i < n; i++)
		putfio(color<<6 | s[i], localfd);
}


static void
getascii(int c, int fd, int localfd)
{
	// simulate common combinations
	// didn't actually use to work so well, but maybe fixed now?
	if(c == ';') {
		getfio(0140, fd, localfd);
		getfio(033, fd, localfd);
	} else if(c == ':') {
		getfio(0140, fd, localfd);
		getfio(073, fd, localfd);
	} else {
		c = ascii2fio[c];
		if(c < 0)
			return;

		getfio(c, fd, localfd);
	}
}

static int
readiac(int fd)
{
	int c;
	char cc;
	read(fd, &cc, 1);
	c = cc & 0377;
	switch(c) {
	case NOP: break;
	case WILL:
		  read(fd, &cc, 1);
		  break;
	case WONT:
		  read(fd, &cc, 1);
		  break;
	case DO:
		  read(fd, &cc, 1);
		  break;
	case DONT:
		  read(fd, &cc, 1);
		  break;
	case IAC:
		  return c;
	default:
		  printf("unknown telnet command %d\n", c);
	}
	return -1;
}

static void
readwrite(int telfd, int typfd)
{
	int n;
	struct pollfd pfd[2];
	char c;

	pfd[0].fd = typfd;
	pfd[0].events = POLLIN;
	pfd[1].fd = telfd;
	pfd[1].events = POLLIN;
	while(pfd[0].fd != -1) {
		n = poll(pfd, 2, -1);
		if(n < 0){
			perror("error poll");
			return;
		}
		if(n == 0)
			return;
		/* take from pdp, send to telnet */
		if(pfd[0].revents & POLLIN) {
			if(n = read(typfd, &c, 1), n <= 0)
				return;
			else {
				c &= 0177;
				putfio(c, telfd);
			}
		}
		/* receive over telnet, send to pdp */
		if(pfd[1].revents & POLLIN) {
			if(n = read(telfd, &c, 1), n <= 0)
				return;
			else {
				if((c&0377) == IAC)
					c = readiac(telfd);
				if(c < 0)
					continue;
				c &= 0177;	// needed?
				getascii(c, typfd, telfd);
			}
		}
	}
}

static void
cmd(int fd, int a, int b)
{
	char ca = a;
	char cb = b;
	char iac = IAC;
	write(fd, &iac, 1);
	write(fd, &ca, 1);
	if(b >= 0) write(fd, &cb, 1);
}

static int typport;
static int typfd;

void*
telthread(void *arg)
{
	for(;;) {
		int telfd = serve1(typport);
		cmd(telfd, WILL, XMITBIN);
		cmd(telfd, DO, XMITBIN);
		cmd(telfd, WILL, ECHO_);
		cmd(telfd, DO, SUPRGA);
		cmd(telfd, WILL, SUPRGA);
		cmd(telfd, WONT, LINEEDIT);
		cmd(telfd, DONT, LINEEDIT);
//		write(telfd, "[2J[H", 7);
		if(color) {
			color = 0;
			putfio(0160, telfd);
		}
		readwrite(telfd, typfd);
		close(telfd);
	}
}

void
typtelnet(int port, int fd)
{
	pthread_t th;
	typport = port;
	typfd = fd;
	pthread_create(&th, NULL, telthread, NULL);
}
