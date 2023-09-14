#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h> 
#include <netinet/tcp.h>
#include <netdb.h>

#include "common.h"

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


static struct timespec starttime;
void
inittime(void)
{
	clock_gettime(CLOCK_MONOTONIC, &starttime);
}
u64
gettime(void)
{
	struct timespec tm;
	u64 t;
	clock_gettime(CLOCK_MONOTONIC, &tm);
	tm.tv_sec -= starttime.tv_sec;
	t = tm.tv_nsec;
	t += (u64)tm.tv_sec * 1000 * 1000 * 1000;
	return t;
}
void
nsleep(u64 ns)
{
	struct timespec tm;
	tm.tv_sec = ns / (1000 * 1000 * 1000);
	tm.tv_nsec = ns % (1000 * 1000 * 1000);
	nanosleep(&tm, nil);
}
