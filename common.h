#define nelem(array) (sizeof(array)/sizeof(array[0]))

void panic(const char *fmt, ...);
int hasinput(int fd);
int dial(const char *host, int port);
void nodelay(int fd);

void initGPIO(void);

typedef struct Panel Panel;
struct Panel
{
	int sw0;
	int sw1;
	int lights0;
	int lights1;
	int lights2;
};
void *panelthread(void *arg);
