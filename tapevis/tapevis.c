#include "common.h"
#include "args.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <poll.h>

#define nil NULL
char *argv0;

SDL_Window *window;
#define HEIGHT 130
#define WIDTH 1000
#define GAP 20


unsigned char *ptrbuf;
int ptrbuflen;
int *ptpbuf;
int ptpbuflen;

int ptpfd = -1;

int ptrpos;


// sorta temporary
void
drawcircle(SDL_Renderer *renderer, int cx, int cy, int r)
{
	for(int y = -r; y < r; y++)
	for(int x = -r; x < r; x++)
		if(x*x + y*y < r*r)
			SDL_RenderDrawPoint(renderer, cx+x, cy+y);
}

void
draw(SDL_Renderer *renderer)
{
	SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
	SDL_RenderClear(renderer);

	int width, height, gap;
	SDL_GetWindowSize(window, &width, &height);
	gap = height/50;
	height = (height-gap)/2;

	SDL_Rect rect;
	rect.x = 0;
	rect.y = 0;
	rect.w = width;
	rect.h = height;

	int space = height/10;
	if(space < 4) space = 4;
	// heuristics
	int r = space/2 - 1;
	int fr = r/2 + 1;
	if(fr == r) fr--;

	int i = ptrpos - 10;
	int c;

	int punchpos = width-5*space;
	int readpos = 10*space;

	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
	SDL_RenderDrawLine(renderer, readpos, 0, readpos, height+gap);
	SDL_RenderDrawLine(renderer, punchpos, height, punchpos, 2*height+gap);

	// reader
	int yoff = 0;
	if(ptrbuflen > 0) {
		SDL_SetRenderDrawColor(renderer, 255, 255, 176, 255);
		rect.x = i < 0 ? -i*space : 0;
		rect.w = (ptrbuflen-i)*space - rect.x;
		if(rect.w > width)
			rect.w = width;
		rect.y = yoff;
		SDL_RenderFillRect(renderer, &rect);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		for(int x = space/2; x < width+2*r; x += space, i++) {
// we have a bit of a race here...hopefully ok for now
			if(i >= 0 && i < ptrbuflen)
				c = ptrbuf[i];
			else
				continue;
			if(c & 1) drawcircle(renderer, x, yoff + 1*space, r);
			if(c & 2) drawcircle(renderer, x, yoff + 2*space, r);
			if(c & 4) drawcircle(renderer, x, yoff + 3*space, r);
			drawcircle(renderer, x, yoff + 4*space, fr);
			if(c & 010) drawcircle(renderer, x, yoff + 5*space, r);
			if(c & 020) drawcircle(renderer, x, yoff + 6*space, r);
			if(c & 040) drawcircle(renderer, x, yoff + 7*space, r);
			if(c & 0100) drawcircle(renderer, x, yoff + 8*space, r);
			if(c & 0200) drawcircle(renderer, x, yoff + 9*space, r);
		}
	}
	yoff += height + gap;

	// punch
	if(ptpfd >= 0) {
		SDL_SetRenderDrawColor(renderer, 255, 255, 176, 255);
		for(i = 0; i < ptpbuflen; i++)
			if(ptpbuf[i] < 0)
				break;
		rect.x = punchpos - i*space;
		if(rect.x < 0) rect.x = 0;
		rect.w = width;
		rect.y = yoff;
		SDL_RenderFillRect(renderer, &rect);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		i = 0;
		for(int x = punchpos; x > -2*r; x -= space) {
			if(i < ptpbuflen)
				c = ptpbuf[i++];
			else
				c = 0;
			if(c < 0)
				continue;
			if(c & 1) drawcircle(renderer, x, yoff + 1*space, r);
			if(c & 2) drawcircle(renderer, x, yoff + 2*space, r);
			if(c & 4) drawcircle(renderer, x, yoff + 3*space, r);
			drawcircle(renderer, x, yoff + 4*space, fr);
			if(c & 010) drawcircle(renderer, x, yoff + 5*space, r);
			if(c & 020) drawcircle(renderer, x, yoff + 6*space, r);
			if(c & 040) drawcircle(renderer, x, yoff + 7*space, r);
			if(c & 0100) drawcircle(renderer, x, yoff + 8*space, r);
			if(c & 0200) drawcircle(renderer, x, yoff + 9*space, r);
		}
	}
}


const char *host = "localhost";

// don't call when still mounted!
void
mountptr(const char *file)
{
	FILE *f = fopen(file, "rb");
	if(f == nil) {
		fprintf(stderr, "couldn't open file %s\n", file);
		return;
	}
	fseek(f, 0, 2);
	ptrbuflen = ftell(f);
	fseek(f, 0, 0);
	ptrbuf = malloc(ptrbuflen);
	fread(ptrbuf, 1, ptrbuflen, f);
	fclose(f);

	ptrpos = 0;
	// find beginning
	while(ptrpos < ptrbuflen && ptrbuf[ptrpos] == 0)
		ptrpos++;
	if(ptrpos > 20) ptrpos -= 10;
}

void
unmountptr(void)
{
	// i'm lazy...try to mitigate race somewhat
	static char dummy[100], *p;
	ptrbuflen = 0;
	p = ptrbuf;
	ptrbuf = dummy;
	if(p != dummy)
		free(p);
}

void
ptrsend(int fd)
{
	char c;
	c = ptrbuf[ptrpos];
	write(fd, &c, 1);
}

void
disconnectptr(struct pollfd *pfd)
{
	close(pfd->fd);
	pfd->fd = -1;
	pfd->events = 0;
}

void
connectptr(struct pollfd *pfd)
{
	pfd->fd = dial(host, 1042);
	if(pfd->fd < 0) {
		pfd->events = 0;
		unmountptr();
	} else {
		pfd->events = POLLIN;
		ptrsend(pfd->fd);
	}
}




void
initptp(void)
{
	ptpfd = open("/tmp/ptpunch", O_CREAT|O_WRONLY|O_TRUNC, 0644);
	if(ptpfd < 0) {
		fprintf(stderr, "can't open /tmp/ptpunch\n");
		exit(1);
	}
	for(int i = 0; i < ptpbuflen; i++)
		ptpbuf[i] = -1;
}

void
fileptp(const char *file)
{
	int fd;
	char c;

	fd = open(file, O_CREAT|O_WRONLY|O_TRUNC, 0644);
	if(fd < 0) {
		fprintf(stderr, "couldn't open file %s\n", file);
		return;
	}
	close(ptpfd);
	ptpfd = open("/tmp/ptpunch", O_RDONLY);
	while(read(ptpfd, &c, 1) > 0)
		write(fd, &c, 1);
	close(fd);
	close(ptpfd);
	initptp();
}

void
disconnectptp(struct pollfd *pfd)
{
	close(pfd->fd);
	pfd->fd = -1;
	pfd->events = 0;
}

void
connectptp(struct pollfd *pfd)
{
	pfd->fd = dial(host, 1043);
	if(pfd->fd < 0) {
		pfd->events = 0;
		initptp();
	} else {
		pfd->events = POLLIN;
	}
}

struct pollfd pfds[5];

void
cli(char *line, int n)
{
	char *p;

	if(p = strchr(line, '\r'), p) *p = '\0';
	if(p = strchr(line, '\n'), p) *p = '\0';

	char **args = split(line, &n);
	if(n > 0) {
		if(strcmp(args[0], "r") == 0) {
			disconnectptr(&pfds[1]);
			unmountptr();
			if(args[1]) {
				mountptr(args[1]);
				if(ptrbuflen > 0)
					connectptr(&pfds[1]);
			}
		}
		else if(strcmp(args[0], "p") == 0) {
			disconnectptp(&pfds[2]);
			if(args[1])
				fileptp(args[1]);
			else
				initptp();
			connectptp(&pfds[2]);
		}
	}
}

void*
tapethread(void *arg)
{
	char c;
	memset(pfds, 0, sizeof(pfds));

	pfds[0].fd = 0;
	pfds[0].events = POLLIN;
	pfds[1].fd = -1;
	pfds[2].fd = -1;
	pfds[3].fd = socketlisten(1050);
	pfds[3].events = POLLIN;
	pfds[4].fd = -1;


	if(ptrbuflen > 0)
		connectptr(&pfds[1]);
	connectptp(&pfds[2]);

	int n;
	char line[1024];
	for(;;) {
		int ret = poll(pfds, nelem(pfds), -1);
		if(ret < 0)
			exit(0);
		if(ret == 0)
			continue;

		// accept cli connection
		if(pfds[3].revents & POLLIN) {
			struct sockaddr_in client;
			socklen_t len = sizeof(client);
			pfds[4].fd = accept(pfds[3].fd, (struct sockaddr*)&client, &len);
			if(pfds[4].fd >= 0)
				pfds[4].events = POLLIN;
		}

		// handle network cli
		else if(pfds[4].revents & POLLIN) {
                        n = read(pfds[4].fd, line, sizeof(line));
			if(n <= 0) {
				close(pfds[4].fd);
				pfds[4].fd = -1;
				pfds[4].events = 0;
			}
			if(n < sizeof(line))
				line[n] = '\0';
			cli(line, n);
		}

		// handle cli
		else if(pfds[0].revents & POLLIN) {
			n = read(pfds[0].fd, line, sizeof(line));
			if(n <= 0) {
				close(pfds[0].fd);
				pfds[0].fd = -1;
				pfds[0].events = 0;
				continue;
			}
			if(n < sizeof(line))
				line[n] = '\0';
			cli(line, n);
		}

		// handle reader
		else if(pfds[1].revents & POLLIN) {
			if(read(pfds[1].fd, &c, 1) <= 0) {
				disconnectptr(&pfds[1]);
				unmountptr();
			} else {
				ptrpos++;
				ptrsend(pfds[1].fd);
			}
		}

		// handle punch
		else if(pfds[2].revents & POLLIN) {
			if(read(pfds[2].fd, &c, 1) <= 0) {
				disconnectptp(&pfds[2]);
				initptp();
				connectptp(&pfds[2]);
				continue;
			}
			memcpy(ptpbuf+1, ptpbuf, (ptpbuflen-1)*sizeof(*ptpbuf));
			ptpbuf[0] = c&0377;
			write(ptpfd, &c, 1);
		}
	}
}

void
usage(void)
{
	fprintf(stderr, "usage: %s [-h host] [ptrfile]\n", argv0);
	exit(0);
}

int
main(int argc, char *argv[])
{
	ARGBEGIN{
	case 'h':
		host = EARGF(usage());
		break;
	}ARGEND;

	if(SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
		return 1;
	}

	window = SDL_CreateWindow("Paper tape",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		WIDTH, 2*HEIGHT + GAP,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if(window == nil) {
		fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
		return 1;
	}

	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if(renderer == nil) {
		fprintf(stderr, "Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
		return 1;
	}

	ptpbuflen = 2000;
	ptpbuf = malloc(ptpbuflen*sizeof(int));
	if(argc > 0)
		mountptr(argv[0]);
	initptp();

	pthread_t th;
        pthread_create(&th, nil, tapethread, nil);

	int quit = 0;
	SDL_Event e;

	while(!quit) {
		while(SDL_PollEvent(&e) != 0) {
			switch(e.type) {
			case SDL_QUIT:
				quit = 1;
				break;
			}
		}

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);

		draw(renderer);

		SDL_RenderPresent(renderer);
		SDL_Delay(30);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
