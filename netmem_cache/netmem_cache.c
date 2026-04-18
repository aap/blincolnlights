// lol
//#include "/u/aap/src/pdp6/emu/util.c"
#include "util.c"

#include <stdlib.h>
#include <poll.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

enum
{
	WRRQ = 1,
	RDRQ,
	ACK,
	ERR,
	RDNRQ,
	WRNRQ,
};

static u8*
putu16(u8 *p, u16 n)
{
	*p++ = n>>8;
	*p++ = n;
	return p;
}

static u8*
getu16(u8 *p, u16 *wp)
{
	u16 w;
	w = p[0];
	w <<= 8; w |= p[1];
	*wp = w;
	return p+2;
}

static u8*
putword(u8 *p, u64 w)
{
	*p++ = w;
	*p++ = w>>8;
	*p++ = w>>16;
	*p++ = w>>24;
	*p++ = w>>32;
	return p;
}

static u8*
getword(u8 *p, u64 *wp)
{
	u64 w;
	w = p[4];
	w <<= 8; w |= p[3];
	w <<= 8; w |= p[2];
	w <<= 8; w |= p[1];
	w <<= 8; w |= p[0];
	*wp = w & 0777777777777;
	return p+5;
}

static u8*
putaddr(u8 *p, u32 w)
{
	*p++ = w;
	*p++ = w>>8;
	*p++ = w>>16;
	return p;
}

static u8*
getaddr(u8 *p, u32 *wp)
{
	u32 w;
	w = p[2];
	w <<= 8; w |= p[1];
	w <<= 8; w |= p[0];
	*wp = w & 0777777;
	return p+3;
}

u64 cache[512];
int cachedpage;
int dirty;

int memfd;

void
getpage(int pg)
{
	static u8 buf[6 + 512*5];
	u8 *p;
	u16 len, i, n;
	u32 a;
	u64 d;

	a = pg*512;
	n = 512;

	len = 6;
	p = putu16(buf, len);
	*p++ = RDNRQ;
	p = putaddr(p, a);
	p = putu16(p, n);
	write(memfd, buf, len+2);

	readn(memfd, buf, 2);
	getu16(buf, &len);
	readn(memfd, buf, len);
	if(buf[0] != ACK) {
		fprintf(stderr, "no ack\n");
		exit(1);
	}
	p = &buf[1];
	for(i = 0; i < n; i++) {
		p = getword(p, &d);
		cache[i] = d;
	}

	cachedpage = pg;
	dirty = 0;
}

void
writepage(int pg)
{
	static u8 buf[6 + 512*5];
	u8 *p;
	u16 len, i, n;
	u32 a;
	u64 d;

	a = pg*512;
	n = 512;

	len = 6 + n*5;
	p = putu16(buf, len);
	*p++ = WRNRQ;
	p = putaddr(p, a);
	p = putu16(p, n);
	for(i = 0; i < n; i++)
		p = putword(p, cache[i]);
	write(memfd, buf, len+2);

	readn(memfd, buf, 2);
	getu16(buf, &len);
	readn(memfd, buf, len);
	if(buf[0] != ACK) {
		fprintf(stderr, "no ack\n");
		exit(1);
	}

	cachedpage = -1;
	dirty = 0;
}

#define log(...)

void
netmem(int fd)
{
	u8 buf[9], *p;
	u16 len;
	u32 a;
	u64 d;
	int page;
	struct pollfd fds[2];

	cachedpage = -1;
	dirty = 0;

	fds[0].fd = fd;
	fds[0].events = POLLIN;
	fds[1].fd = memfd;
	fds[1].events = POLLIN;
	for(;;) {
		poll(fds, 2, cachedpage != -1 ? 30 : -1);
		// this should not happen unless other side closes
		if(fds[1].revents) {
			printf("closing connection\n");
			return;
		}
		if((fds[0].revents & POLLIN) == 0) {
			log("timeout, invalidate cache\n");
			if(dirty)
				writepage(cachedpage);
			cachedpage = -1;
			dirty = 0;
			continue;
		}

		if(readn(fd, buf, 2)) {
			printf("netmem fd closed\n");
			return;
		}
		getu16(buf, &len);
		if(len > sizeof(buf)) {
			printf("netmem botch, closing\n");
			close(fd);
			return;
		}

		memset(buf, 0, sizeof(buf));
		readn(fd, buf, len);

		p = getaddr(&buf[1], &a);

		switch(buf[0]) {
		case WRRQ:
			p = getword(p, &d);
//			printf("write %06o %012llo\n", a, d);
			page = a/512;
			a &= 0777;
			if(page != cachedpage) {
				log("need page %o (wr) %d\n", page, dirty);
				if(dirty)
					writepage(cachedpage);
				getpage(page);
			}
			cache[a] = d;
			dirty = 1;
			len = 1;
			p = putu16(buf, len);
			*p++ = ACK;
			write(fd, buf, len+2);
			break;

		case RDRQ:
			page = a/512;
//			printf("read %06o ", a);
			a &= 0777;
			if(page != cachedpage) {
				log("need page %o (rd) %d\n", page, dirty);
				if(dirty)
					writepage(cachedpage);
				getpage(page);
			}
			d = cache[a];
//			printf("%012llo\n", d);
			len = 6;
			p = putu16(buf, len);
			*p++ = ACK;
			p = putword(p, d);
			write(fd, buf, len+2);
			break;

		default:
			printf("unknown msg %d\n", buf[0]);
		}
	}
}

void
handlecon(int confd, void *arg)
{
	memfd = confd;
printf("connected to pdp-6 %d\n", memfd);
	int fd = dial("maya", 10006);
	if(fd < 0)
		goto discon;
printf("connected fd %d\n", fd);

	netmem(fd);

	close(fd);
	fd = -1;
discon:
	close(memfd);
	memfd = -1;
}

int
main()
{
	serve(20006, handlecon, NULL);

	return 0;
}
