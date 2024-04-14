#include "common.h"
#include "panel_b18.h"
#include "tx0.h"

void
updateswitches(TX0 *tx0, Panel *panel)
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
		if(down & KEY_LOAD1) TAC = sw0;
		if(down & KEY_LOAD2) TBR = sw0;
	}

        tx0->sw_power = !!(sw1 & SW_POWER);
	tx0->sw_sup_alarm = 0;
	tx0->sw_sup_chime = 0;
	// no idea what these are
	tx0->sw_auto_restart = 0;
	tx0->sw_auto_readin = 0;
	tx0->sw_auto_test = 0;
	tx0->sw_stop_c0 = !!(sw1 & SW_SSTEP);
	tx0->sw_stop_c1 = !!(sw1 & SW_SINST);;
	tx0->sw_step = !!(sw1 & SW_SPARE1);
	tx0->sw_repeat = !!(sw1 & SW_REPEAT);
	tx0->sw_cm_sel = 0;
	tx0->sw_type_in = 0;
	tx0->btn_test = !!(down & KEY_START);
	tx0->btn_readin = !!(down & KEY_READIN);
	tx0->btn_stop = !!(down & KEY_STOP);
	tx0->btn_restart = !!(down & KEY_CONT);
}

void
updatelights(TX0 *tx0, Panel *panel)
{
	int indicators = 0;
	if(panel->sw1 & KEY_SPARE) {
		indicators |= 0;
		indicators |= tx0->c<<8;
		indicators |= tx0->r<<7;
		indicators |= tx0->t<<6;
	} else {
		indicators |= tx0->ss<<9;
		indicators |= tx0->ios<<8;
		indicators |= tx0->pbs<<7;
		indicators |= 0;
	}
	indicators |= (0x8>>panel->sel1) << 10;
	indicators |= (0x8>>panel->sel2) << 14;
	indicators |= panel->sw1 & 0x3F;
	switch(panel->sel1) {
	case 0: panel->lights0 = AC; break;
	case 1: panel->lights0 = MAR | IR<<16; break;
	case 2: panel->lights0 = 0;
	case 3: panel->lights0 = TAC; break;
	}
	switch(panel->sel2) {
	case 0: panel->lights1 = LR; break;
	case 1: panel->lights1 = PC; break;
	case 2: panel->lights1 = MBR; break;
	case 3: panel->lights1 = TBR; break;
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
