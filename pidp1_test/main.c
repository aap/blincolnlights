#include "common.h"
#include "panel_pidp1.h"
#include <pthread.h>

Panel*
getpanel(void)
{
	return attachseg("/tmp/pdp1_panel", sizeof(Panel));
}

void*
swthread(void *arg)
{
	volatile Panel *p = (volatile Panel*)arg;
	for(;;) {
		printf("\r%06o %06o %06o %06o",
			p->sw0,
			p->sw1,
			p->sw2,
			p->sw3);
		fflush(stdout);
		nsleep(10000000);
	}
	return nil;
}

int
main()
{
	Panel *p;
	int *lp;
	int i;
	pthread_t th;

	p = getpanel();
	if(p == nil)
		return 1;
	p->lights0 = 0;
	p->lights1 = 0;
	p->lights2 = 0;
	p->lights3 = 0;
	p->lights4 = 0;
	p->lights5 = 0;
	p->lights6 = 0;
	p->lights7 = 0;
	p->lights8 = 0;
	p->lights9 = 0;
	pthread_create(&th, nil, swthread, p);

	lp = &p->lights0;
	*lp = 1;
	i = 0;
	for(;;) {
		lp[i] <<= 1;
		if(lp[i] & 01000000) {
			lp[i] = 0;
			i = (i+1)%10;
			lp[i] = 1;
		}
		nsleep(100000000);
	}
	return 0;
}
