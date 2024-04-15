#include <stdio.h>
#include "common.h"

#define nil NULL

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

static SDL_Window *window;
static SDL_Renderer *renderer;

#include "../art/panelart.inc"
#include "../art/wwart.inc"

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
SDL_Texture *btntex[2];

Grid grid1, grid2;

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

	//drawgrid(paneltex, &grid2);

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
	paneltex = loadtex(ww_panel_png, ww_panel_png_len);
	lamptex[0] = loadtex(lamp_off_png, lamp_off_png_len);
	lamptex[1] = loadtex(lamp_on_png, lamp_on_png_len);
	switchtex[0] = loadtex(switch_d_png, switch_d_png_len);
	switchtex[1] = loadtex(switch_u_png, switch_u_png_len);
	btntex[0] = loadtex(key_n_png, key_n_png_len);
	btntex[1] = loadtex(key_d_png, key_d_png_len);


	float scl = 1280.0/121.743;

	grid1.xscl = 3.528 * scl;
	grid1.yscl = 1.411 * scl;
	grid1.xoff = 3.977 * scl;
	grid1.yoff = 5.256 * scl;

	grid2 = grid1;
	grid2.xoff = 64.849 * scl;

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

int buttonstate;

void
mouse(int x, int y)
{
	Element *e;

	for(unsigned i = 0; i < nelem(switches); i++) {
		e = &switches[i];
		if(buttonstate == 0 || !ismouseover(e, x, y)) {
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

	for(unsigned i = 0; i < nelem(btns); i++) {
		e = &btns[i];
		if(buttonstate == 0 || !ismouseover(e, x, y))
			e->state = 0;
		else if(buttonstate & 1)
			e->state = 1;
	}
}



#include "panel_whirlwind.h"

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
	panel = createseg("/tmp/whirlwind_panel", sizeof(Panel));
	if(panel == nil)
		exit(1);

	setnlights(panel->sw[0], switches+0*16, 16, 0100000);
	setnlights(panel->sw[1], switches+1*16, 16, 0100000);
	setnlights(panel->sw[2], switches+2*16, 16, 0100000);
	setnlights(panel->sw[3], switches+3*16, 16, 0100000);
	setnlights(panel->sw[4], switches+4*16, 16, 0100000);
	setnlights(panel->sw[5], switches+5*16, 16, 0100000);
	setnlights(panel->sw[6], switches+6*16, 4, 010);
}

void
updatepanel(void)
{
	int sw;

	panel->sw[0] = getnswitches(switches+0*16, 0100000, 16, 1);
	panel->sw[1] = getnswitches(switches+1*16, 0100000, 16, 1);
	panel->sw[2] = getnswitches(switches+2*16, 0100000, 16, 1);
	panel->sw[3] = getnswitches(switches+3*16, 0100000, 16, 1);
	panel->sw[4] = getnswitches(switches+4*16, 0100000, 16, 1);
	panel->sw[5] = getnswitches(switches+5*16, 0100000, 16, 1);
	sw = getnswitches(switches+6*16, 010, 4, 1);
	sw |= getnswitches(btns, 0100000, 12, 1);
	panel->sw[6] = sw;

	for(int i = 0; i < 16; i++)
		setnlights(panel->lights[i], lights+i*16, 16, 0100000);
}



int
main()
{
	SDL_Event ev;

	SDL_Init(SDL_INIT_EVERYTHING);
	IMG_Init(IMG_INIT_PNG);

	if(SDL_CreateWindowAndRenderer(1280, 935, 0, &window, &renderer) < 0)
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
			case SDL_QUIT:
				exit(0);
			}

		updatepanel();
		draw();

		SDL_Delay(30);
	}

	return 0;
}
