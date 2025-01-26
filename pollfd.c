#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include "common.h"

struct FDmsg
{
	int msg;
	FD *fd;
};

static FD *fds[100];
static struct pollfd pfds[100];
static int nfds;
static int pollpipe[2] = { -1, -1 };

static void
removeslot(int i)
{
	pfds[i].revents = 0;
	pfds[i].events = 0;
	pfds[i].fd = -1;
	fds[i]->fd = -1;
	fds[i]->id = -1;
	fds[i]->ready = 0;
	fds[i] = nil;
}

static void
pollfds(void)
{
	struct FDmsg msg;
	FD *fd;
	int i, n;

	pipe(pollpipe);
	pfds[0].fd = pollpipe[0];
	pfds[0].events = POLLIN;
	nfds = 1;
	for(;;) {
		n = poll(pfds, nfds, -1);
		if(n < 0) {
			perror("error poll");
			return;
		}

		/* someone wants us to watch their fd or has closed it */
		if(pfds[0].revents & POLLIN) {
			read(pfds[0].fd, &msg, sizeof(msg));
			fd = msg.fd;
			switch(msg.msg) {
			// wait
			case 1:
				if(fd->id >= 0) {
					/* already in list */
					assert(fds[fd->id] == fd);
//printf("polling fd %d in slot %d\n", fd->fd, fd->id);
				} else {
					/* add to list */
					for(i = 1; i < nfds; i++)
						if(fds[i] == nil)
							break;
					assert(i < nelem(pfds));
					if(i == nfds) nfds++;
					fd->id = i;
					fds[fd->id] = fd;
//printf("adding fd %d in slot %d\n", fd->fd, fd->id);
				}
				pfds[fd->id].fd = fd->fd;
				pfds[fd->id].events = POLLIN;
				pfds[fd->id].revents = 0;
				fd->ready = 0;
				break;

			// close
			case 2:
				/* fd was closed */
//printf("received close for fd %d slot %d\n", pfds[fd->id].fd, fd->id);
				assert(fd->id >= 0);
				close(pfds[fd->id].fd);
				removeslot(fd->id);
				break;
			}
		}

		for(i = 1; i < nfds; i++) {
			/* fd was closed on other side */
			if(pfds[i].revents & POLLHUP) {
//printf("fd %d hung up\n", pfds[i].fd);
				close(fds[i]->fd);
				removeslot(i);
			}
			if(pfds[i].revents & POLLIN) {
//printf("fd %d became ready\n", pfds[i].fd);
				/* ignore this fd for now */
				pfds[i].fd = -1;
				pfds[i].events = 0;
				pfds[i].revents = 0;
				fds[i]->ready = 1;
			}
			if(pfds[i].revents)
				printf("more events on fd %d: %d\n", pfds[i].fd, pfds[i].revents);
		}
	}
}

static void *pollthread(void *arg) { pollfds(); return nil; }

void
startpolling(void)
{
	pthread_t th;
	pthread_create(&th, nil, pollthread, nil);

	// wait for thread to start so waitfd can do its thing
	while(((volatile int*)pollpipe)[0] < 0) usleep(1000);
}

void
waitfd(FD *fd)
{
	struct FDmsg msg;
	if(fd->fd < 0)
		return;
	fd->ready = 0;
	msg.msg = 1;
	msg.fd = fd;
	write(pollpipe[1], &msg, sizeof(msg));
}

void
closefd(FD *fd)
{
	struct FDmsg msg;
	printf("closing fd %d\n", fd->fd);
	int i = fd->id;
	msg.msg = 2;
	msg.fd = fd;
	write(pollpipe[1], &msg, sizeof(msg));
	// wait for thread to notice
	while(((volatile FD**)fds)[i] != nil) usleep(1);
}
