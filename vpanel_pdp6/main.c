#include <stdio.h>
#include "common.h"

#define nil NULL

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

static SDL_Window *window;
static SDL_Renderer *renderer;

#include "../art/panelart.inc"
#include "../art/pdp6art.inc"

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

Element *ir_l, *mi_l;
Element *pc_l, *ma_l;
Element *pih_l, *pir_l, *pio_l;
Element *misc_l;
Element *data_sw, *ma_sw;
Element *misc_sw;

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

Grid grid1, grid2, grid3, grid4;

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

//	drawgrid(paneltex, &grid4);

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
	paneltex = loadtex(pdp6_op_panel_png, pdp6_op_panel_png_len);
	lamptex[0] = loadtex(lamp_off_png, lamp_off_png_len);
	lamptex[1] = loadtex(lamp_on_png, lamp_on_png_len);
	switchtex[0] = loadtex(switch_d_png, switch_d_png_len);
	switchtex[1] = loadtex(switch_u_png, switch_u_png_len);
	hswitchtex[0] = loadtex(switch_r_png, switch_r_png_len);
	hswitchtex[1] = loadtex(switch_l_png, switch_l_png_len);
	keytex[0] = loadtex(key_n_png, key_n_png_len);
	keytex[1] = loadtex(key_d_png, key_d_png_len);
	keytex[2] = loadtex(key_u_png, key_u_png_len);


	float scl = 800.0/500.0;	// random, leftover form pdp1

	grid1.xscl = 9.72 * scl;
	grid1.yscl = 11.4  * scl;
	grid1.xoff = 49.0 * scl + grid1.xscl/2;
	grid1.yoff = 17.0 * scl + grid1.yscl/2;

	grid2 = grid1;
	grid2.xoff += grid1.xscl*39 + grid1.xscl/2;

	grid3 = grid1;
	grid3.xoff += grid1.xscl*61 - 0.6*scl;

	grid4 = grid2;
	grid4.xoff += -grid4.xscl/2;
	grid4.xscl = 15.68 * scl;
	grid4.xoff += grid4.xscl/2;
	grid4.yoff += grid4.yscl*6.55;

	for(unsigned i = 0; i < nelem(lights); i++)
		putongrid(&lights[i]);
	for(unsigned i = 0; i < nelem(switches); i++)
		putongrid(&switches[i]);
	for(unsigned i = 0; i < nelem(keys); i++)
		putongrid(&keys[i]);

	Element *e = lights;
	ir_l = e; e += 18;
	mi_l = e; e += 36;
	pc_l = e; e += 18;
	ma_l = e; e += 18;
	pih_l = e; e += 7;
	pir_l = e; e += 7;
	pio_l = e; e += 7;
	misc_l = e;

	e = switches;
	data_sw = e; e += 36;
	ma_sw = e; e += 18;
	misc_sw = e;
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



#include "panel_pidp6.h"

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
	panel = createseg("/tmp/pdp6_panel", sizeof(Panel));
	if(panel == nil)
		exit(1);

	setnlights(panel->sw0, data_sw+18, 18, 0400000);
	setnlights(panel->sw1, data_sw, 18, 0400000);
	setnlights(panel->sw2, ma_sw, 18, 0400000);
	misc_sw[0].state = !!(panel->sw3 & SW_REPEAT);
	misc_sw[1].state = !!(panel->sw3 & SW_ADDR_STOP);
	misc_sw[2].state = !!(panel->sw3 & SW_POWER);
	misc_sw[3].state = !!(panel->sw3 & SW_MEM_DISABLE);
}

void
updatepanel(void)
{
	int sw;

	panel->sw0 = getnswitches(data_sw+18, 0400000, 18, 1);
	panel->sw1 = getnswitches(data_sw, 0400000, 18, 1);
	panel->sw2 = getnswitches(ma_sw, 0400000, 18, 1);
	sw = 0;
	if(keys[0].state == 1) sw |= KEY_START;
	if(keys[0].state == 2) sw |= KEY_READIN;
	if(keys[1].state == 1) sw |= KEY_INST_CONT;
	if(keys[1].state == 2) sw |= KEY_MEM_CONT;
	if(keys[2].state == 1) sw |= KEY_INST_STOP;
	if(keys[2].state == 2) sw |= KEY_MEM_STOP;
	if(keys[3].state == 1) sw |= KEY_IO_RESET;
	if(keys[3].state == 2) sw |= KEY_EXEC;
	if(keys[4].state == 1) sw |= KEY_DEP;
	if(keys[4].state == 2) sw |= KEY_DEP_NXT;
	if(keys[5].state == 1) sw |= KEY_EX;
	if(keys[5].state == 2) sw |= KEY_EX_NXT;
	if(misc_sw[0].state == 1) sw |= SW_REPEAT;
	if(misc_sw[1].state == 1) sw |= SW_ADDR_STOP;
	if(misc_sw[2].state == 1) sw |= SW_POWER;
	if(misc_sw[3].state == 1) sw |= SW_MEM_DISABLE;
	panel->sw3 = sw;

	sw = 0;
	if(keys[6].state == 1) sw |= KEY_MOTOR_OFF;
	if(keys[6].state == 2) sw |= KEY_MOTOR_ON;
	if(keys[7].state == 1) sw |= KEY_PTP_FEED;
	if(keys[7].state == 2) sw |= KEY_PTR_FEED;
	panel->sw4 = sw;

	setnlights(panel->lights0, mi_l+18, 18, 0400000);
	setnlights(panel->lights1, mi_l, 18, 0400000);
	setnlights(panel->lights2, ma_l, 18, 0400000);
	setnlights(panel->lights3, ir_l, 18, 0400000);
	setnlights(panel->lights4, pc_l, 18, 0400000);

	misc_l[0].state = !!(panel->lights5 & L5_RUN);
	misc_l[1].state = !!(panel->lights5 & L5_MC_STOP);
	misc_l[2].state = !!(panel->lights5 & L5_PI_ON);
	misc_l[3].state = !!(panel->lights5 & L5_REPEAT);
	misc_l[4].state = !!(panel->lights5 & L5_ADDR_STOP);
	misc_l[5].state = !!(panel->lights5 & L5_POWER);
	misc_l[6].state = !!(panel->lights5 & L5_MEM_DISABLE);
	setnlights(panel->lights5, pio_l, 7, 0100);

	setnlights(panel->lights6, pir_l, 7, 0100);
	setnlights(panel->lights6, pih_l, 7, 020000);
}

int
main()
{
	SDL_Event ev;

	SDL_Init(SDL_INIT_EVERYTHING);
	IMG_Init(IMG_INIT_PNG);

	if(SDL_CreateWindowAndRenderer(1399, 200, 0, &window, &renderer) < 0)
		panic("error: SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
	SDL_SetWindowTitle(window, "PDP-6 console");

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
