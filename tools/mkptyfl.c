/* Creates a pty and connects it to the process tty.
 * As an optional argument it takes a filename and symlinks /dev/pts/N. */ 

/* Pretend to be a Flexowriter of sorts */

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


enum FlCode
{
	FlColor = -2,
	FlStop = -3,
	FlUCase = -4,
	FlLCase = -5,
	FlNull = -6,
};

// TODO: not all characters map to ascii

static int fl2ascii[] = {
	-1, -1, 'e', '8', -1, '|', 'a', '3',
	' ', '=', 's', '4', 'i', '+', 'u', '2',
	FlColor, '.', 'd', '5', 'r', '1', 'j', '7',
	'n', ',', 'f', '6', 'c', '-', 'k', -1,
	't', -1, 'z', '\b', 'l', '\t', 'w', -1,
	'h', '\n', 'y', -1, 'p', -1, 'q', -1,
	'o', FlStop, 'b', -1, 'g', -1, '9', -1,
	'm', FlUCase, 'x', -1, 'v', FlLCase, '0', FlNull,

	-1, -1, 'E', '8', -1, '_', 'A', '3',
	' ', ':', 'S', '4', 'I', '/', 'U', '2',
	FlColor, ')', 'D', '5', 'R', '1', 'J', '7',
	'N', '(', 'F', '6', 'C', '-', 'K', -1,
	'T', -1, 'Z', '\b', 'L', '\t', 'W', -1,
	'H', '\n', 'Y', -1, 'P', -1, 'Q', -1,
	'O', FlStop, 'B', -1, 'G', -1, '9', -1,
	'M', FlUCase, 'X', -1, 'V', FlLCase, '0', FlNull,
};

// 100 LC
// 200 UC
static int ascii2fl[] = {
	-1, -1, -1, -1, -1, -1, -1, -1,
	077, 045, -1, -1, -1, 051, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,

	010, -1, -1, -1, -1, -1, -1, -1,
	0231, 0221, -1, 0115, 0131, 0135, 0121, 0215,
	0176, 0125, 0117, 0107, 0113, 0123, 0133, 0127,
	0103, 0166, -1, -1, -1, 0111, -1, -1,

	-1, 0206, 0262, 0234, 0222, 0202, 0232, 0264,
	0250, 0214, 0226, 0236, 0244, 0270, 0230, 0260,
	0254, 0256, 0224, 0212, 0240, 0216, 0274, 0246,
	0272, 0252, 0242, -1, -1, -1, -1, 0205,

	-1, 0106, 0162, 0134, 0122, 0102, 0132, 0164,
	0150, 0114, 0126, 0136, 0144, 0170, 0130, 0160,
	0154, 0156, 0124, 0112, 0140, 0116, 0174, 0146,
	0172, 0152, 0142, -1, 0105, -1, -1, 077,
};

int color;
int ucase;

void
putfl(int c, int fd)
{
	char s[10];

	c = ucase*0100 + (c&0177);
	c = fl2ascii[c];
	switch(c){
	case -1: return;
	case FlColor:
		color = !color;
		if(color == 0){
			sprintf(s, "\e[39;49m");
			write(fd, s, 8);
		}else{
			sprintf(s, "\e[31m");
			write(fd, s, 5);
		}
		break;
	case FlStop:
		break;
	case FlUCase:
		ucase = 1;
		break;
	case FlLCase:
		ucase = 0;
		break;
	case FlNull:
		break;
	default:
		if(c == '\n'){
			s[0] = '\r';
			s[1] = '\n';
			write(fd, s, 2);
		}else{
			s[0] = c;
			write(fd, s, 1);
		}
	}
}

void
putascii(int c, int fd, int localfd)
{
	char s[2];
	int n;
	c = ascii2fl[c];
	if(c < 0)
		return;
	n = 0;
	if(c & 0300){
		if(c & 0100 && ucase)
			s[n++] = 075;
		else if(c & 0200 && !ucase)
			s[n++] = 071;
	}
	s[n++] = c & 077;
	write(fd, s, n);

// local echo
int i;
for(i = 0; i < n; i++)
	putfl(s[i], localfd);
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
				c &= 077;
				putfl(c, ttyout);
				SLEEP;
			}
		}
		/* read from tty, write to pty */
		if(pfd[1].revents & POLLIN){
			if(n = read(ttyin, &c, 1), n <= 0)
				return;
			else{
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

	raw(0);

	write(1, "[2J[H", 7);
	readwrite(0, 1, fd, fd);
	close(fd);

	printf("\e[39;49m");
	reset(0);

	if(ptylink)
		unlink(ptylink);

	return 0;
}
