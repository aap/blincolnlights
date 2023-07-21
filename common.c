#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <pigpio.h>
#include <verilated.h>

#include <sys/socket.h>
#include <netinet/in.h> 
#include <netinet/tcp.h>
#include <netdb.h>

#include "common.h"

int LED_ROWS[] = {2, 3, 4};
int SWITCH_ROWS[] = {17, 27};
int COLUMNS[] = {26, 19, 13, 6, 5, 11,
	14, 15, 18, 23, 24, 25, 8, 7, 12, 16, 20, 21};

void
disablePin(int pin)
{
	gpioSetMode(pin, PI_INPUT);
	gpioSetPullUpDown(pin, PI_PUD_OFF);
}

void
enablePin(int pin)
{
	gpioSetMode(pin, PI_INPUT);
	gpioSetPullUpDown(pin, PI_PUD_UP);
}

void
outputPin(int pin, int val)
{
	gpioSetMode(pin, PI_OUTPUT);
	gpioWrite(pin, val);
}

void
delay(int ms)
{
	usleep(ms * 1000);
}



void
disableRows(void)
{
	int i;
	for(i = 0; i < nelem(LED_ROWS); i++)
		disablePin(LED_ROWS[i]);
	for(i = 0; i < nelem(SWITCH_ROWS); i++)
		disablePin(SWITCH_ROWS[i]);
}

void
enableRows(void)
{
	int i;
	for(i = 0; i < nelem(LED_ROWS); i++)
		outputPin(LED_ROWS[i], 1);
	for(i = 0; i < nelem(SWITCH_ROWS); i++)
		outputPin(SWITCH_ROWS[i], 1);
}

void
disableColumns(void)
{
	int i;
	for(i = 0; i < nelem(COLUMNS); i++)
		disablePin(COLUMNS[i]);
}

void
enableColumns(void)
{
	int i;
	for(i = 0; i < nelem(COLUMNS); i++)
		enablePin(COLUMNS[i]);
}



void
setLights(Panel *p)
{
	int i;

	outputPin(LED_ROWS[0], 1);
	for(i = 0; i < 18; i++)
		outputPin(COLUMNS[i], (~p->lights0>>i)&1);
	delay(2);
	outputPin(LED_ROWS[0], 0);

	outputPin(LED_ROWS[1], 1);
	for(i = 0; i < 18; i++)
		outputPin(COLUMNS[i], (~p->lights1>>i)&1);
	delay(2);
	outputPin(LED_ROWS[1], 0);

	outputPin(LED_ROWS[2], 1);
	for(i = 0; i < 18; i++)
		outputPin(COLUMNS[i], (~p->lights2>>i)&1);
	delay(2);
	outputPin(LED_ROWS[2], 0);
}

void
readSwitches(Panel *p)
{
	int i, sw;

	enableColumns();

	outputPin(SWITCH_ROWS[0], 0);
	delay(2);
	sw = 0;
	for(i = 0; i < 18; i++)
		sw |= !gpioRead(COLUMNS[i]) << i;
	disablePin(SWITCH_ROWS[0]);
	p->sw0 = sw;

	outputPin(SWITCH_ROWS[1], 0);
	delay(2);
	sw = 0;
	for(i = 0; i < 18; i++)
		sw |= !gpioRead(COLUMNS[i]) << i;
	disablePin(SWITCH_ROWS[1]);
	p->sw1 = sw;
}

int doexit;

void*
panelthread(void *arg)
{
	Panel *p = (Panel*)arg;
	while(!doexit) {
		setLights(p);
		readSwitches(p);
	}

	gpioTerminate();
	exit(0);
}

void
interrupt(int sig)
{
	doexit = 1;
}

void
initGPIO(void)
{
        if(gpioInitialise() < 0)
                exit(1);
	signal(SIGINT, interrupt);
	disableRows();
	disableColumns();
}

void
panic(const char *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
        exit(1);
}
int
hasinput(int fd)
{
	fd_set fds;
	struct timeval timeout;

	if(fd < 0) return 0;

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	return select(fd+1, &fds, NULL, NULL, &timeout) > 0;
}
int
dial(const char *host, int port)
{
	char portstr[32];
	int sockfd;
	struct addrinfo *result, *rp, hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	snprintf(portstr, 32, "%d", port);
	if(getaddrinfo(host, portstr, &hints, &result)){
		perror("error: getaddrinfo");
		return -1;
	}

	for(rp = result; rp; rp = rp->ai_next){
		sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(sockfd < 0)
			continue;
		if(connect(sockfd, rp->ai_addr, rp->ai_addrlen) >= 0)
			goto win;
		close(sockfd);
	}
	freeaddrinfo(result);
	perror("error");
	return -1;

win:
	freeaddrinfo(result);
	return sockfd;
}
void
nodelay(int fd)
{
	int flag = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}
