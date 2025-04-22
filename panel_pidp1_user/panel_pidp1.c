#include "common.h"
#include "pinctrl/gpiolib.h"
#include "panel_pidp1.h"
#include <math.h>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <pthread.h>


// kernel device layout
typedef struct PDP1_Panel PDP1_Panel;
struct PDP1_Panel {
	uint8_t lights[10][18];
	int sw[4];
};

// resolution ~50-150ns
void
xsleep(u64 dt)
{
	u64 t1 = gettime();
	u64 t2 = gettime();
	while(t1+dt > t2)
		t2 = gettime();
}

void
countRow(u32 *on, u32 bits)
{
	for(int i = 0; i < 18; i++)
		on[i] += (bits>>i) & 1;
}

//float fall = 0.995f;
float fall = 0.990f;
//float rise = 0.012f;
//float rise = 0.040f;
float rise = 0.030f;
float power = 1.0f;

float map(float x, float x1, float x2, float y1, float y2) {
	float t = (x-x1)/(x2-x1)*(y2-y1) + y1;
	return t < y1 ? y1 : t > y2 ? y2 : t;
}

volatile int doupdate = 1;
volatile int waitmode = 1;

// not ideal yet at all, but better than nothing
void
lampthread(Panel *p, PDP1_Panel *pp)
{
//	const int nsamples = 10000;	// for xsleep(3000)
//	const int nsamples = 500;
	int nsamples = waitmode ? 500 : 10000;
	Panel cur;
	u32 on[10][18];
	float intensity[10][18];
	u64 now, prev;
	float dt;

	memset(intensity, 0, sizeof(intensity));
	now = gettime();
	for(;;) {
		while(!doupdate) {
			usleep(10000);
			prev = gettime();
		}
		memset(on, 0, sizeof(on));
		for(int i = 0; i < nsamples; i++) {
			cur = *p;
			countRow(on[0], cur.lights0);
			countRow(on[1], cur.lights1);
			countRow(on[2], cur.lights2);
			countRow(on[3], cur.lights3);
			countRow(on[4], cur.lights4);
			countRow(on[5], cur.lights5);
			countRow(on[6], cur.lights6);
			countRow(on[7], cur.lights7);
			countRow(on[8], cur.lights8);
			countRow(on[9], cur.lights9);
			if(waitmode)
				nsleep(3500);
			else
				xsleep(3000);

			memcpy(&p->sw0, pp->sw, 4*sizeof(int));	
		}

		prev = now;
		now = gettime();
		dt = (now - prev)/(1000.0f * 1000.0f);
//printf("%f\n", dt);
		for(int i = 0; i < 10; i++)
		for(int j = 0; j < 18; j++) {
			float targ = (float)on[i][j]/nsamples;
			if(targ >= intensity[i][j]) {
				float t = powf(1.0f-rise, dt);
				intensity[i][j] = intensity[i][j]*t + targ*(1-t);
			} else {
				float t = powf(fall, dt);
				intensity[i][j] = intensity[i][j]*t + targ*(1-t);
			}

			float l = intensity[i][j];
			int in = map(powf(l,power), 0.1f,1.0f, 0.0f,31.5f);
			pp->lights[i][j] = in;
		}
	}
}

volatile int mode = 1;

void*
foothread(void *arg)
{
	int c;
	while(c = getchar(), c != EOF) {
		if(c == ',') mode = !mode;
		if(c == '.') waitmode = !waitmode;
		if(c == '/') doupdate = !doupdate;
		if(c == '\n') continue;
		printf("%s %s %s\n",
			mode ? "aap   " : "pidp11",
			waitmode ? "sleep" : "wait ",
			doupdate ? "  update" : "noupdate");
	}
	return NULL;
}

int
main(int argc, char *argv[])
{
	Panel *p;

	p = createseg("/tmp/pdp1_panel", sizeof(Panel));
	if(p == nil)
		return 1;

	int fd = open("/dev/pidp1_panel", O_RDWR);
	if(fd < 0)
		return 1;
	void *pp = mmap(NULL, sizeof(Panel), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if(pp == MAP_FAILED) {
		printf("couldn't mmap\n");
		return 1;
	}


	pthread_t th;
	pthread_create(&th, nil, foothread, nil);
	lampthread(p, pp);

	return 0;
}
