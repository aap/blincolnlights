/* Creates a pty and connects it to the process tty.
 * As an optional argument it takes a filename and symlinks /dev/pts/N. */ 

/* Pretend to be an IBM Model b of sorts */

#define _XOPEN_SOURCE 600	/* for ptys */
#define _DEFAULT_SOURCE		/* for cfmakeraw */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <netinet/tcp.h>
#include <netdb.h>

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

struct termios tiosaved;

int
raw(int fd)
{
	struct termios tio;
	if(tcgetattr(fd, &tio) < 0) return -1;
	tiosaved = tio;
	cfmakeraw(&tio);
	if(tcsetattr(fd, TCSAFLUSH, &tio) < 0) return -1;
	return 0;
}

int
reset(int fd)
{
	if(tcsetattr(0, TCSAFLUSH, &tiosaved) < 0) return -1;
	return 0;
}

#define BAUD 30

struct timespec slp = { 0, 1000*1000*1000 / BAUD };
struct timespec hupslp = { 0, 100*1000*1000 };

//#define SLEEP nanosleep(&slp, NULL)
#define SLEEP


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

int color;
int ucase;

void
putfio(int c, int fd)
{
	const char *s;
	int col;

	col = !!(c&0100);
	c = ucase*0100 + (c&077);

	s = fio2uni[c];
	if(s == XXX) return;
	if(s == Lcs) { ucase = 0; return; }
	if(s == Ucs) { ucase = 1; return; }

	if(color != col) {
		color = col;
		if(color == 0)
			write(fd, "\e[39;49m", 8);
		else
			write(fd, "\e[31m", 5);
	}
	write(fd, s, strlen(s));
}

void
putascii(int c, int fd, int localfd)
{
	char s[2];
	int n;
	c = ascii2fio[c];
	if(c < 0)
		return;
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

int
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

void
readwrite(int ttyin, int ttyout, int ptyin, int ptyout)
{
	int n;
	struct pollfd pfd[2];
	char c;

	pfd[0].fd = ptyin;
	pfd[0].events = POLLIN;
	pfd[1].fd = ttyin;
	pfd[1].events = POLLIN;
	while(pfd[0].fd != -1){
		n = poll(pfd, 2, -1);
		if(n < 0){
			perror("error poll");
			return;
		}
		if(n == 0)
			return;
		if(pfd[0].revents & POLLHUP)
			nanosleep(&hupslp, NULL);
		/* read from pty, write to tty */
		if(pfd[0].revents & POLLIN){
			if(n = read(ptyin, &c, 1), n <= 0)
				return;
			else{
				c &= 0177;
				putfio(c, ttyout);
				SLEEP;
			}
		}
		/* read from tty, write to pty */
		if(pfd[1].revents & POLLIN){
			if(n = read(ttyin, &c, 1), n <= 0)
				return;
			else{
int ic = c & 0377;
if(ic == IAC)
c = readiac(ttyin);
if(c < 0) continue;
				c &= 0177;	// needed?
				if(c == 035)
					return;
				putascii(c, ptyout, ttyout);
				SLEEP;
			}
		}
	}
}

int
serve1(int port)
{
	int sockfd, confd;
	socklen_t len;
	struct sockaddr_in server, client;
	int x;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		perror("error: socket");
		return -1;
	}

	x = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&x, sizeof x);

	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);
	if(bind(sockfd, (struct sockaddr*)&server, sizeof(server)) < 0){
		perror("error: bind");
		return -1;
	}
	listen(sockfd, 5);
	len = sizeof(client);
	confd = accept(sockfd, (struct sockaddr*)&client, &len);
	close(sockfd);
	if(confd >= 0)
		return confd;
	perror("error: accept");
	return -1;
}

void
cmd(int fd, int a, int b)
{
	char ca = a;
	char cb = b;
	char iac = IAC;
	write(fd, &iac, 1);
	write(fd, &ca, 1);
	if(b >= 0) write(fd, &cb, 1);
}

int
main(int argc, char *argv[])
{
	int fd;
	int slv;
	const char *ptylink;

	ptylink = NULL;
	if(argc > 1)
		ptylink = argv[1];

	fd = posix_openpt(O_RDWR);
	if(fd < 0)
		return 1;
	if(grantpt(fd) < 0)
		return 2;
	if(unlockpt(fd) < 0)
		return 3;

	if(ptylink){
		unlink(ptylink);
		if(symlink(ptsname(fd), ptylink) < 0)
			fprintf(stderr, "Error: %s\n", strerror(errno));
	}

	printf("%s\n", ptsname(fd));

	slv = open(ptsname(fd), O_RDWR);
	if(slv < 0)
		return 4;
	raw(slv);
	close(slv);

//	raw(0);

	for(;;) {
printf("waiting for connection\n");
		int telfd = serve1(3401);
		cmd(telfd, WILL, XMITBIN);
		cmd(telfd, DO, XMITBIN);
		cmd(telfd, WILL, ECHO_);
		//	cmd(telfd, DO, ECHO_);
		cmd(telfd, DO, SUPRGA);
		cmd(telfd, WILL, SUPRGA);
		cmd(telfd, WONT, LINEEDIT);
		cmd(telfd, DONT, LINEEDIT);
		//	write(1, "[2J[H", 7);
		//	readwrite(0, 1, fd, fd);
		write(telfd, "[2J[H", 7);
		readwrite(telfd, telfd, fd, fd);
		close(telfd);
	}
	close(fd);

//	printf("\e[39;49m");
//	reset(0);

	if(ptylink)
		unlink(ptylink);

	return 0;
}
