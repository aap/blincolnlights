#include "common.h"
#include "panel_b18.h"
#include "whirlwind.h"

void
updateswitches(Whirlwind *ww, Panel *panel)
{
	int sw0 = panel->sw0;
	int sw1 = panel->sw1;
	int down = sw1 & ~panel->psw1;
	panel->psw1 = panel->sw1;

	if(down) {
		if(down & KEY_SEL1)
			panel->sel1 = (panel->sel1+1 + 2*!!(sw1&KEY_MOD)) % 4;
		if(down & KEY_SEL2)
			panel->sel2 = (panel->sel2+1 + 2*!!(sw1&KEY_MOD)) % 4;
		if(down & KEY_LOAD1) {
			// ??
		}
		if(down & KEY_LOAD2) {
			// ??
		}
	}

	ww->pc_reset_sw = sw0 & 03777;
	ww->btn_readin = !!(down & KEY_READIN);
	ww->btn_start_over = (down & KEY_START) && !(sw1 & KEY_MOD);
	ww->btn_start_at40 = (down & KEY_START) && (sw1 & KEY_MOD);
	ww->btn_examine = !!(down & KEY_EXAM);
	ww->btn_stop = !!(sw1 & KEY_STOP);
	ww->btn_start_clock	= (down & KEY_CONT) && (sw1 & KEY_MOD);
	ww->btn_clock_pulse	= (down & KEY_CONT) && !(sw1 & KEY_MOD) && (sw1 & SW_SSTEP);
	ww->btn_order_by_order	= (down & KEY_CONT) && !(sw1 & KEY_MOD) && !(sw1 & SW_SSTEP) && (sw1 & SW_SINST);
	ww->btn_restart		= (down & KEY_CONT) && !(sw1 & KEY_MOD) && !(sw1 & SW_SSTEP) && !(sw1 & SW_SINST);

	ww->power_sw = !!(sw1 & SW_POWER);
	ww->stop_si1 = !!(sw1 & SW_SPARE1);
}

void
updatelights(Whirlwind *ww, Panel *panel)
{
	int indicators = 0;
	if(panel->sw1 & KEY_SPARE) {
		indicators |= ww->stop_clock<<9;
		indicators |= 0<<8;
		indicators |= 0<<7;
		indicators |= 0<<6;
	} else {
		indicators |= ww->automatic<<9;
		indicators |= 0<<8;
		indicators |= 0<<7;
		indicators |= ww->tpd<<6;
	}
	indicators |= (0x8>>panel->sel1) << 10;
	indicators |= (0x8>>panel->sel2) << 14;
	indicators |= panel->sw1 & 0x3F;
	switch(panel->sel1) {
	case 0: panel->lights0 = ww->sam<<17 | ww->ov<<16 |
		((panel->sw1 & KEY_SPARE)?ww->ac_cry:ww->ac); break;
	case 1: panel->lights0 = ww->ssc<<17 | ww->mar; break;
	case 2: panel->lights0 = ww->cs<<13 | ww->src<<12 | ww->sc<<6 | ww->tss; break;
	case 3: panel->lights0 = ww->cr; break;
	}
	switch(panel->sel2) {
	case 0: panel->lights1 = ww->br; break;
	case 1: panel->lights1 = ww->pc; break;
	case 2: panel->lights1 = ww->par; break;
	case 3: panel->lights1 = ww->ar; break;
	}
	panel->lights2 = indicators;
}

void
lightsoff(Panel *panel)
{
	panel->lights0 = 0;
	panel->lights1 = 0;
	panel->lights2 = 0;
}

Panel*
getpanel(void)
{
	return attachseg("/tmp/b18_panel", sizeof(Panel));
}
