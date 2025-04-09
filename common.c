#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <netinet/tcp.h>
#include <netdb.h>

#include <poll.h>

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
socketlisten(int port)
{
	int x;
	struct sockaddr_in server;
	int fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd < 0) {
		perror("error: socket");
		return -1;
	}

	x = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&x, sizeof x);

	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);
	if(bind(fd, (struct sockaddr*)&server, sizeof(server)) < 0) {
		close(fd);
		perror("error: bind");
		return -1;
	}
	listen(fd, 1);
	return fd;
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

int
serve1(int port)
{
	int sockfd, confd;
	socklen_t len;
	struct sockaddr_in client;

	sockfd = socketlisten(port);
	if(sockfd < 0)
		return -1;

	len = sizeof(client);
	confd = accept(sockfd, (struct sockaddr*)&client, &len);
	close(sockfd);
	if(confd >= 0)
		return confd;
	perror("error: accept");
	return -1;
}

// listen on multiple ports
void
serveN(struct PortHandler *ports, int nports, void *arg)
{
	int i;
	int confd;
	struct pollfd pfds[100];
	struct sockaddr_in client;
	socklen_t len;

	// create and listen on sockets for the given ports
	for(i = 0; i < nports; i++) {
		pfds[i].revents = 0;
		pfds[i].events = 0;
		pfds[i].fd = socketlisten(ports[i].port);
		if(pfds[i].fd >= 0)
			pfds[i].events = POLLIN;
	}

	// now poll to accept connections and hand off to handle function
	for(;;) {
		int ret = poll(pfds, nports, -1);
		if(ret < 0)
			break;
		if(ret == 0)
			continue;

		for(i = 0; i < nports; i++) {
			if(pfds[i].revents & POLLIN) {
				len = sizeof(client);
				confd = accept(pfds[i].fd, (struct sockaddr*)&client, &len);
				if(confd >= 0)
					ports[i].handle(confd, arg);
			} else if(pfds[i].revents) {
				printf("weird stuff on port %d\n", ports[i].port);
			}
		}
	}

	for(i = 0; i < nports; i++)
		close(pfds[i].fd);
}

void
nodelay(int fd)
{
	int flag = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}


void*
createseg(const char *name, size_t sz)
{
	int fd;
	void *p;

	mode_t mask = umask(0);
	fd = open(name, O_RDWR|O_CREAT, 0666);
	umask(mask);
	// if we try to open a /tmp file owned by another user
	// with O_CREAT, the above will fail (even for root).
	// so try again without O_CREAT
	if(fd == -1)
		fd = open(name, O_RDWR);
	if(fd == -1) {
		fprintf(stderr, "couldn't open file %s\n", name);
		return nil;
	}
	ftruncate(fd, sz);
	p = mmap(nil, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if(p == MAP_FAILED) {
		fprintf(stderr, "couldn't mmap file\n");
		return nil;
	}

	return p;
}
void*
attachseg(const char *name, size_t sz)
{
	int fd;
	void *p;

	fd = open(name, O_RDWR);
	if(fd == -1) {
		fprintf(stderr, "couldn't open file %s\n", name);
		return nil;
	}
	p = mmap(NULL, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if(p == MAP_FAILED) {
		fprintf(stderr, "couldn't mmap file\n");
		return nil;
	}

	return p;
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



static int
isdelim(char c)
{
	return c == '\0' || strchr(" \t\n;'\"", c) != nil;
}

/* have to free argv[0] and argv to clean up */
char**
split(char *line, int *pargc)
{
	int argc, n;
	char **argv, *lp, delim;

	n = strlen(line)+1;
	// just allocate enough
	lp = (char*)malloc(2*n);
	argv = (char**)malloc(sizeof(char*)*n);
	argc = 0;
	for(; *line; line++) {
		while(isspace(*line)) line++;
		if(*line == '\0')
			break;
		argv[argc++] = lp;
		if(*line == '"' || *line == '\'') {
			delim = *line++;
			while(*line && *line != delim) {
				if(*line == '\\')
					line++;
				*lp++ = *line++;
			}
		} else {
			while(!isdelim(*line)) {
				if(*line == '\\')
					line++;
				*lp++ = *line++;
			}
		}
		*lp++ = '\0';
	}
	if(pargc) *pargc = argc;
	argv[argc++] = nil;

	return argv;
}
