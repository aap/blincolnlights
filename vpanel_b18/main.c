#include <stdio.h>
#include "common.h"

#define nil NULL

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

static SDL_Window *window;
static SDL_Renderer *renderer;

#include "../art/panelart.inc"
#include "../art/b18art.inc"

typedef struct Grid Grid;
struct Grid
{
	float xoff, yoff;
	float xscl, yscl;
};

typedef struct Element Element;
struct Element
{
	SDL_Texture **tex;
	Grid *grid;
	float x, y;
	int state, active;
	SDL_Rect r;
};

SDL_Texture*
loadtex(unsigned char *data, int sz)
{
	SDL_RWops *f;
	SDL_Texture *tex;

	f = SDL_RWFromConstMem(data, sz);
	if(f == nil)
		panic("Couldn't load resource");
	tex = IMG_LoadTexture_RW(renderer, f, 0);
	if(tex == nil)
		panic("Couldn't load image");
	SDL_RWclose(f);
	return tex;
}

SDL_Texture *paneltex;
SDL_Texture *lamptex[2];
SDL_Texture *switchtex[2];
SDL_Texture *hswitchtex[2];
SDL_Texture *btntex[2];

Grid grid1;

#include "elements.inc"

void
drawgrid(SDL_Texture *tex, Grid *g)
{
	int w, h;
	float x, y;
	SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
	SDL_QueryTexture(tex, nil, nil, &w, &h);
	for(x = g->xoff; x < w; x += g->xscl)
		SDL_RenderDrawLine(renderer, x, 0, x, h);
	for(y = g->yoff; y < h; y += g->yscl)
		SDL_RenderDrawLine(renderer, 0, y, w, y);
}

void
putongrid(Element *e)
{
	int w, h;
	SDL_QueryTexture(e->tex[0], nil, nil, &w, &h);
	e->r.x = e->grid->xoff - w/2 + e->grid->xscl*e->x;
	e->r.y = e->grid->yoff - h/2 + e->grid->yscl*e->y;
	e->r.w = w;
	e->r.h = h;
}

void
drawelement(Element *e)
{
	SDL_RenderCopy(renderer, e->tex[e->state], nil, &e->r);
}

void
draw(void)
{
	SDL_RenderCopy(renderer, paneltex, nil, nil);

	//drawgrid(paneltex, &grid1);

	for(unsigned i = 0; i < nelem(lights); i++)
		drawelement(&lights[i]);
	for(unsigned i = 0; i < nelem(switches); i++)
		drawelement(&switches[i]);
	for(unsigned i = 0; i < nelem(btns); i++)
		drawelement(&btns[i]);
	SDL_RenderPresent(renderer);
}

void
init(void)
{
	paneltex = loadtex(b18_panel_png, b18_panel_png_len);
	lamptex[0] = loadtex(lamp_off_png, lamp_off_png_len);
	lamptex[1] = loadtex(lamp_on_png, lamp_on_png_len);
	switchtex[0] = loadtex(switch_d_png, switch_d_png_len);
	switchtex[1] = loadtex(switch_u_png, switch_u_png_len);
	hswitchtex[0] = loadtex(switch_r_png, switch_r_png_len);
	hswitchtex[1] = loadtex(switch_l_png, switch_l_png_len);
	btntex[0] = loadtex(key_n_png, key_n_png_len);
	btntex[1] = loadtex(key_d_png, key_d_png_len);


	float scl = 1000.0/430.0;

	grid1.xscl = 13.0 * scl;
	grid1.yscl = 13.0 * scl;
	grid1.xoff = 0.0;
	grid1.yoff = 0.0;

	for(unsigned i = 0; i < nelem(lights); i++)
		putongrid(&lights[i]);
	for(unsigned i = 0; i < nelem(switches); i++)
		putongrid(&switches[i]);
	for(unsigned i = 0; i < nelem(btns); i++)
		putongrid(&btns[i]);
}

int
ismouseover(Element *e, int x, int y)
{
	return x >= e->r.x && x < e->r.x+e->r.w &&
		y >= e->r.y && y < e->r.y+e->r.h;
}

int ctrldown;
int buttonstate;
int mx, my;

void
updateswitches(void)
{
	Element *e;

	for(unsigned i = 0; i < nelem(switches); i++) {
		e = &switches[i];
		if(buttonstate == 0 || !ismouseover(e, mx, my)) {
			e->active = 0;
		}else if(!e->active) {
			e->active = 1;
			if(buttonstate & 1)
				e->state = !e->state;
			else if(buttonstate & 2)
				e->state = 1;
			else if(buttonstate & 4)
				e->state = 0;
		}
	}

	// buttons can be held down
	for(unsigned i = 0; i < nelem(btns); i++) {
		e = &btns[i];
		if(buttonstate == 0 || !ismouseover(e, mx, my)) {
			if(!e->active)
				e->state = 0;
		} else if((buttonstate & 5) == 5) {
			e->active = !e->active;
		} else if(buttonstate & 1)
			e->state = 1;
	}

	if(ctrldown)
		btns[7].state = 1;
}

void
mouse(int x, int y)
{
	mx = x;
	my = y;
	updateswitches();
}



#include "panel_b18.h"

Panel *panel;

void    
setnlights(int b, Element *l, int n, int bit)
{
	int i;
	for(i = 0; i < n; i++, bit >>= 1)
		l[i].state = !!(b & bit);
}

int
getnswitches(Element *sw, int bit, int n, int state)
{               
	int b, i;
	b = 0;
	for(i = 0; i < n; i++, bit >>= 1)
		if(sw[i].state == state)
			b |= bit;
	return b;
}

void
initpanel(void)
{
	panel = createseg("/tmp/b18_panel", sizeof(Panel));
	if(panel == nil)
		exit(1);

	setnlights(panel->sw0, switches, 18, 0400000);
	setnlights(panel->sw1, switches+18, 6, 040);
}

void
updatepanel(void)
{
	int sw;

	panel->sw0 = getnswitches(switches, 0400000, 18, 1);
	sw = getnswitches(switches+18, 040, 6, 1);
	sw |= getnswitches(btns, 0400000, 12, 1);
	panel->sw1 = sw;

	setnlights(panel->lights0, lights+0*18, 18, 0400000);
	setnlights(panel->lights1, lights+1*18, 18, 0400000);
	setnlights(panel->lights2, lights+2*18, 18, 0400000);
}



int
main()
{
	SDL_Event ev;

	SDL_Init(SDL_INIT_EVERYTHING);
	IMG_Init(IMG_INIT_PNG);

	if(SDL_CreateWindowAndRenderer(1000, 302, 0, &window, &renderer) < 0)
		panic("error: SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
	SDL_SetWindowTitle(window, "Whirlwind control");

	init();

	initpanel();

	for(;;) {
		while(SDL_PollEvent(&ev))
			switch(ev.type) {
			case SDL_MOUSEMOTION:
				mouse(ev.motion.x, ev.motion.y);
				break;
			case SDL_MOUSEBUTTONDOWN:
				buttonstate |= 1<<(ev.button.button-1);
				mouse(ev.button.x, ev.button.y);
				break;
			case SDL_MOUSEBUTTONUP:
				buttonstate &= ~(1<<(ev.button.button-1));
				mouse(ev.button.x, ev.button.y);
				break;

			case SDL_KEYDOWN:
				switch(ev.key.keysym.scancode) {
				case SDL_SCANCODE_LCTRL:
				case SDL_SCANCODE_RCTRL:
				case SDL_SCANCODE_CAPSLOCK:
					ctrldown = 1;
					updateswitches();
					break;
				}
				break;
			case SDL_KEYUP:
				switch(ev.key.keysym.scancode) {
				case SDL_SCANCODE_LCTRL:
				case SDL_SCANCODE_RCTRL:
				case SDL_SCANCODE_CAPSLOCK:
					ctrldown = 0;
					updateswitches();
					break;
				}
				break;

			case SDL_QUIT:
				exit(0);
			}

		updatepanel();
		draw();

		SDL_Delay(30);
	}

	return 0;
}
