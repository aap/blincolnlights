#include <stdio.h>
#include "common.h"

#define nil NULL

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

static SDL_Window *window;
static SDL_Renderer *renderer;

#include "../art/panelart.inc"
#include "../art/pdp1art.inc"

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

Element *pc_l, *ma_l;
Element *mb_l, *ac_l, *io_l;
Element *ff_l, *misc_l, *ss_l, *pf_l, *ir_l;
Element *ta_sw, *tw_sw, *ext_sw;
Element *misc_sw, *ss_sw;

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
SDL_Texture *keytex[3];

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

//	drawgrid(paneltex, &grid2);

	for(unsigned i = 0; i < nelem(lights); i++)
		drawelement(&lights[i]);
	for(unsigned i = 0; i < nelem(switches); i++)
		drawelement(&switches[i]);
	for(unsigned i = 0; i < nelem(keys); i++)
		drawelement(&keys[i]);
	SDL_RenderPresent(renderer);
}

void
init(void)
{
	paneltex = loadtex(pdp1_panel_png, pdp1_panel_png_len);
	lamptex[0] = loadtex(lamp_off_png, lamp_off_png_len);
	lamptex[1] = loadtex(lamp_on_png, lamp_on_png_len);
	switchtex[0] = loadtex(switch_d_png, switch_d_png_len);
	switchtex[1] = loadtex(switch_u_png, switch_u_png_len);
	hswitchtex[0] = loadtex(switch_r_png, switch_r_png_len);
	hswitchtex[1] = loadtex(switch_l_png, switch_l_png_len);
	keytex[0] = loadtex(key_n_png, key_n_png_len);
	keytex[1] = loadtex(key_d_png, key_d_png_len);
	keytex[2] = loadtex(key_u_png, key_u_png_len);


	float scl = 800.0/500.0;	// mm to px

	grid1.xscl = 12.95 * scl;
	grid1.yscl = 12.9  * scl;
	grid1.xoff =  4.5  * scl + grid1.xscl/2;
	grid1.yoff =  2.56 * scl;

	grid2 = grid1;
	grid2.xoff =  1.05 * scl + grid2.xscl/2;

	for(unsigned i = 0; i < nelem(lights); i++)
		putongrid(&lights[i]);
	for(unsigned i = 0; i < nelem(switches); i++)
		putongrid(&switches[i]);
	for(unsigned i = 0; i < nelem(keys); i++)
		putongrid(&keys[i]);

	Element *e = lights;
	pc_l = e; e += 16;
	ma_l = e; e += 16;
	mb_l = e; e += 18;
	ac_l = e; e += 18;
	io_l = e; e += 18;
	ff_l = e; e += 13;
	misc_l = e; e += 3;
	ss_l = e; e += 6;
	pf_l = e; e += 6;
	ir_l = e; e += 5;

	e = switches;
	ext_sw = e; e++;
	ta_sw = e; e += 16;
	tw_sw = e; e += 18;
	misc_sw = e; e += 3;
	ss_sw = e; e += 6;
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

	for(unsigned i = 0; i < nelem(keys); i++) {
		e = &keys[i];
		if(buttonstate == 0 || !ismouseover(e, x, y))
			e->state = 0;
		else if(buttonstate & 1)
			e->state = 1;
		else if(buttonstate & 4)
			e->state = 2;
	}
}



#include "panel_pidp1.h"

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
	panel = createseg("/tmp/pdp1_panel", sizeof(Panel));
	if(panel == nil)
		exit(1);

	setnlights(panel->sw0, ta_sw, 16, 0100000);
	setnlights(panel->sw1, tw_sw, 18, 0400000);
	setnlights(panel->sw2, ss_sw, 6, 0100000);
	ext_sw->state = !!(panel->sw0 & SW_EXTEND);
	misc_sw[0].state = !!(panel->sw0 & SW_POWER);
	misc_sw[1].state = !!(panel->sw2 & SW_SSTEP);
	misc_sw[2].state = !!(panel->sw2 & SW_SINST);
}

void
updatepanel(void)
{
	int sw;

	sw = getnswitches(ta_sw, 0100000, 16, 1);
	if(ext_sw->state) sw |= SW_EXTEND;
	if(misc_sw[0].state) sw |= SW_POWER;
	panel->sw0 = sw;

	panel->sw1 = getnswitches(tw_sw, 0400000, 18, 1);

	sw = getnswitches(ss_sw, 0100000, 6, 1);
	if(misc_sw[1].state) sw |= SW_SSTEP;
	if(misc_sw[2].state) sw |= SW_SINST;
	if(keys[0].state == 1) sw |= KEY_START;
	if(keys[0].state == 2) sw |= KEY_START_UP;
	if(keys[1].state == 1) sw |= KEY_STOP;
	if(keys[2].state == 1) sw |= KEY_CONT;
	if(keys[3].state == 1) sw |= KEY_EXAM;
	if(keys[4].state == 2) sw |= KEY_DEP;
	if(keys[5].state == 1) sw |= KEY_READIN;
	if(keys[6].state == 1) sw |= KEY_READER;
	if(keys[6].state == 2) sw |= KEY_READER_UP;
	if(keys[7].state == 1) sw |= KEY_FEED;
	panel->sw2 = sw;

	panel->sw3 = 0;	// no spacewar controllers for now

	setnlights(panel->lights0, pc_l, 16, 0100000);
	setnlights(panel->lights1, ma_l, 16, 0100000);
	setnlights(panel->lights2, mb_l, 18, 0400000);
	setnlights(panel->lights3, ac_l, 18, 0400000);
	setnlights(panel->lights4, io_l, 18, 0400000);
	setnlights(panel->lights5, ff_l, 13, L5_RUN);
	setnlights(panel->lights5, misc_l, 3, L5_PWR);
	setnlights(panel->lights6, ir_l, 5, 0400000);
	setnlights(panel->lights6, ss_l, 6, 0004000);
	setnlights(panel->lights6, pf_l, 6, 0000040);
}



int
main()
{
	SDL_Event ev;

	SDL_Init(SDL_INIT_EVERYTHING);
	IMG_Init(IMG_INIT_PNG);

	if(SDL_CreateWindowAndRenderer(800, 448, 0, &window, &renderer) < 0)
		panic("error: SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
	SDL_SetWindowTitle(window, "PDP-1 console");

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
