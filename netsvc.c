#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include "common.h"
#include "netsvc.h"

enum { ACCEPTQ = 32 };

static NetSvc *svcs;
static struct {
	int fd;
	NetSvc *svc;
} acceptq[ACCEPTQ];
static volatile int nacceptq;
static pthread_mutex_t acceptlock = PTHREAD_MUTEX_INITIALIZER;

void
netsvc_add(NetSvc *svc)
{
	NetSvc **p;
	if(svc->outsz == 0)
		svc->outsz = NETOUT;
	if(svc->maxconn == 0)
		svc->maxconn = 1;
	svc->conns = nil;
	svc->nconn = 0;
	svc->nextsvc = nil;
	for(p = &svcs; *p; p = &(*p)->nextsvc)
		;
	*p = svc;
}

/* accept thread */
static void
gotconn(int fd, void *arg, int port)
{
	NetSvc *svc;

	for(svc = svcs; svc; svc = svc->nextsvc)
		if(svc->port == port)
			break;
	pthread_mutex_lock(&acceptlock);
	if(svc == nil || nacceptq >= ACCEPTQ)
		close(fd);
	else {
		acceptq[nacceptq].fd = fd;
		acceptq[nacceptq].svc = svc;
		nacceptq++;
	}
	pthread_mutex_unlock(&acceptlock);
}

static void*
acceptthread(void *arg)
{
	struct PortHandler ports[32];
	NetSvc *svc;
	int n;

	n = 0;
	for(svc = svcs; svc && n < nelem(ports); svc = svc->nextsvc) {
		ports[n].port = svc->port;
		ports[n].handle = gotconn;
		n++;
	}
	serveN(ports, n, nil);
	return nil;
}

void
netsvc_start(void)
{
	pthread_t th;
	pthread_create(&th, nil, acceptthread, nil);
}

void
netsvc_close(NetConn *c)
{
	c->dead = 1;
}

/* emulator thread from here on */

static NetConn*
adopt(NetSvc *svc, int fd)
{
	NetConn *c;

	nodelay(fd);

	/* a service that takes the raw fd gets it exactly as accepted:
	 * the paper tape reader, for one, wants a blocking read */
	if(svc->adopt) {
		svc->adopt(svc, fd);
		return nil;
	}
	fcntl(fd, F_SETFL, O_NONBLOCK);

	c = (NetConn*)malloc(sizeof(NetConn));
	if(c == nil) {
		close(fd);
		return nil;
	}
	memset(c, 0, sizeof(NetConn));
	c->outsz = svc->outsz;
	c->out = (char*)malloc(c->outsz);
	if(c->out == nil) {
		free(c);
		close(fd);
		return nil;
	}
	c->svc = svc;
	c->fd.fd = fd;
	c->fd.id = -1;
	c->next = svc->conns;
	svc->conns = c;
	svc->nconn++;
	waitfd(&c->fd);

	if(svc->accepted)
		svc->accepted(c);
	/* the service may have said no; if it didn't and we are over the
	 * limit, say no for it */
	if(!c->dead && svc->nconn > svc->maxconn)
		netsvc_close(c);
	return c;
}

/* for connections the emulator dials out itself */
NetConn*
netsvc_adopt(NetSvc *svc, int fd)
{
	return adopt(svc, fd);
}

static void
drainaccept(void)
{
	struct { int fd; NetSvc *svc; } q[ACCEPTQ];
	int i, n;

	if(nacceptq == 0)
		return;
	pthread_mutex_lock(&acceptlock);
	n = nacceptq;
	memcpy(q, acceptq, n*sizeof(q[0]));
	nacceptq = 0;
	pthread_mutex_unlock(&acceptlock);

	for(i = 0; i < n; i++)
		adopt(q[i].svc, q[i].fd);
}

static void
flushconn(NetConn *c)
{
	int n;

	while(c->nout > 0 && c->fd.fd >= 0) {
		n = write(c->fd.fd, c->out, c->nout);
		if(n < 0) {
			if(errno == EINTR)
				continue;
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			netsvc_close(c);
			return;
		}
		if(n == 0)
			return;
		c->nout -= n;
		if(c->nout > 0)
			memmove(c->out, c->out+n, c->nout);
	}
}

void
netsvc_write(NetConn *c, const void *buf, int n)
{
	if(c->dead || n <= 0)
		return;
	if(c->nout + n > c->outsz) {
		/* try to make room before giving up */
		flushconn(c);
		if(c->dead)
			return;
		if(c->nout + n > c->outsz) {
			if(c->svc->mode == SVC_RAW)
				return;		/* frames are disposable */
			netsvc_close(c);	/* a line client that can't keep up */
			return;
		}
	}
	memcpy(c->out + c->nout, buf, n);
	c->nout += n;
}

void
netsvc_print(NetConn *c, const char *fmt, ...)
{
	char buf[NETLINE+64];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if(n < 0)
		return;
	if(n >= sizeof(buf))
		n = sizeof(buf)-1;
	netsvc_write(c, buf, n);
}

void
netsvc_broadcast(NetSvc *svc, const void *buf, int n)
{
	NetConn *c;
	for(c = svc->conns; c; c = c->next)
		netsvc_write(c, buf, n);
}

static void
dispatchline(NetConn *c)
{
	if(c->svc->line == nil)
		return;
	if(c->toolong) {
		c->toolong = 0;
		c->nin = 0;
		c->svc->line(c, nil);
		return;
	}
	if(c->nin > 0 && c->in[c->nin-1] == '\r')
		c->nin--;
	c->in[c->nin] = '\0';
	c->nin = 0;
	c->svc->line(c, c->in);
}

static void
readconn(NetConn *c)
{
	char buf[4096];
	int i, n;

	n = read(c->fd.fd, buf, sizeof(buf));
	if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
		waitfd(&c->fd);
		return;
	}
	if(n <= 0) {
		netsvc_close(c);
		return;
	}

	if(c->svc->mode == SVC_RAW) {
		if(c->svc->data)
			c->svc->data(c, (u8*)buf, n);
	} else {
		for(i = 0; i < n; i++) {
			if(buf[i] == '\n') {
				dispatchline(c);
				if(c->dead)
					return;
			} else if(c->toolong)
				;	/* discard through the next \n */
			else if(c->nin >= NETLINE)
				c->toolong = 1;
			else
				c->in[c->nin++] = buf[i];
		}
	}
	if(!c->dead)
		waitfd(&c->fd);
}

static void
reap(NetSvc *svc)
{
	NetConn **p, *c;

	for(p = &svc->conns; c = *p, c; ) {
		if(!c->dead) {
			p = &c->next;
			continue;
		}
		if(svc->closed)
			svc->closed(c);
		if(c->fd.fd >= 0 && c->fd.id >= 0)
			closefd(&c->fd);
		else if(c->fd.fd >= 0)
			close(c->fd.fd);
		*p = c->next;
		svc->nconn--;
		free(c->out);
		free(c);
	}
}

void
netsvc_poll(void)
{
	NetSvc *svc;
	NetConn *c, *next;
	int dead;

	drainaccept();

	dead = 0;
	for(svc = svcs; svc; svc = svc->nextsvc) {
		for(c = svc->conns; c; c = next) {
			next = c->next;
			/* the poll thread reaps fds that hung up */
			if(c->fd.fd < 0)
				netsvc_close(c);
			else if(c->fd.ready)
				readconn(c);
			/* dead connections too: a rejection has to get out */
			flushconn(c);
			dead |= c->dead;
		}
	}

	if(dead)
		for(svc = svcs; svc; svc = svc->nextsvc)
			reap(svc);
}

int
netsvc_nconn(NetSvc *svc)
{
	return svc ? svc->nconn : 0;
}
