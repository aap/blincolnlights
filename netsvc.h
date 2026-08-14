#ifndef NETSVC_H
#define NETSVC_H
/* Connection fan-out for the emulators.
 *
 * One accept path, all I/O on the emulator thread, N clients per stream.
 * The accept thread does nothing but accept() and queue the fd; the
 * emulator thread adopts it, registers it with pollfd.c, and does all
 * reading, dispatching and writing.  Nothing here may block the emulator:
 * sockets are non-blocking and a short write keeps the remainder in the
 * connection's out buffer.
 */

typedef struct NetConn NetConn;
typedef struct NetSvc NetSvc;

enum {
	SVC_LINE = 0,	/* callbacks get whole \n-terminated lines */
	SVC_RAW,	/* callbacks get bytes as they arrive */

	NETLINE = 1024,	/* longest line we accept, see DEBUG_PROTOCOL_SPEC §1 */
	NETOUT = 64*1024,	/* default output buffer */
};

struct NetConn
{
	FD fd;
	NetSvc *svc;
	NetConn *next;
	void *aux;		/* the service's per-connection state */

	/* line assembly, SVC_LINE only */
	char in[NETLINE+1];
	int nin;
	int toolong;		/* discarding the rest of an over-long line */

	char *out;
	int nout, outsz;
	int dead;
};

struct NetSvc
{
	int port;
	int maxconn;
	int mode;		/* SVC_LINE or SVC_RAW */
	int outsz;		/* per-connection output buffer, 0 = NETOUT */

	/* all called on the emulator thread */
	void (*accepted)(NetConn *c);
	void (*line)(NetConn *c, char *line);	/* nil = the line was too long */
	void (*data)(NetConn *c, u8 *buf, int n);
	void (*closed)(NetConn *c);
	/* if set, the service takes the raw fd and no NetConn is made */
	void (*adopt)(NetSvc *svc, int fd);

	/* private */
	NetConn *conns;
	int nconn;
	NetSvc *nextsvc;
};

void netsvc_add(NetSvc *svc);
void netsvc_start(void);
void netsvc_poll(void);
void netsvc_write(NetConn *c, const void *buf, int n);
void netsvc_print(NetConn *c, const char *fmt, ...);
void netsvc_broadcast(NetSvc *svc, const void *buf, int n);
void netsvc_close(NetConn *c);
NetConn *netsvc_adopt(NetSvc *svc, int fd);
int netsvc_nconn(NetSvc *svc);

#endif
