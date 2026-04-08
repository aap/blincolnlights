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
readwrite(int telfd, int ttyfd)
{
	int n;
	struct pollfd pfd[2];
	char c;

	pfd[0].fd = ttyfd;
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
			if(n = read(ttyfd, &c, 1), n <= 0)
				return;
			else {
				c &= 0177;
				write(telfd, &c, 1);
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
				c |= 0200;	// needed?
				write(ttyfd, &c, 1);
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

static int ttyport;
static int ttyfd;

void*
telthread(void *arg)
{
	for(;;) {
		int telfd = serve1(ttyport);
		cmd(telfd, WILL, XMITBIN);
		cmd(telfd, DO, XMITBIN);
		cmd(telfd, WILL, ECHO_);
		cmd(telfd, DO, SUPRGA);
		cmd(telfd, WILL, SUPRGA);
		cmd(telfd, WONT, LINEEDIT);
		cmd(telfd, DONT, LINEEDIT);
//		write(telfd, "[2J[H", 7);
		readwrite(telfd, ttyfd);
		close(telfd);
	}
}

void
ttytelnet(int port, int fd)
{
	pthread_t th;
	ttyport = port;
	ttyfd = fd;
	pthread_create(&th, NULL, telthread, NULL);
}
