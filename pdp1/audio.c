#include "common.h"
#include "pdp1.h"

#include <SDL2/SDL.h>

static SDL_AudioDeviceID dev;
static int nsamples;
static u64 nexttime;

#define SAMPLE_TIME (1000000000/48000)
//#define SAMPLE_TIME (1000000000/44100)

void
initaudio(void)
{
	SDL_AudioSpec spec;

	SDL_Init(SDL_INIT_AUDIO);

	memset(&spec, 0, sizeof(spec));
	spec.freq = 48000;
//	spec.freq = 44100;
	spec.format = AUDIO_U8;
	spec.channels = 1;
	spec.samples = 1024;	// whatever this is
	spec.callback = nil;
	dev = SDL_OpenAudioDevice(nil, 0, &spec, nil, 0);
}

void
stopaudio(void)
{
	if(dev == 0)
		return;

	SDL_PauseAudioDevice(dev, 1);
	SDL_ClearQueuedAudio(dev);
	nsamples = 0;
	nexttime = 0;
}

void
svc_audio(PDP1 *pdp)
{
	u8 s;

	if(dev == 0 || nexttime >= pdp->simtime)
		return;

	if(nexttime == 0)
		nexttime = pdp->simtime + SAMPLE_TIME;
	else
		nexttime += SAMPLE_TIME;

	// queue up a few samples
	if(nsamples < 10) {
		nsamples++;
		// then start playing
		if(nsamples == 10)
			SDL_PauseAudioDevice(dev, 0);
	}

	s = 0;
	for(int i = 2; i < 6; i++)
		if(pdp->pf & (1<<i))
//			s += 63;
			s += 40;
	SDL_QueueAudio(dev, &s, 1);
}
