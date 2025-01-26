#include "common.h"
#include "panel_b18.h"
#include "pdp1.h"

void
updateswitches(PDP1 *pdp, Panel *panel)
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
			if(sw1 & KEY_MOD)
				pdp->ss = sw0 & 077;
			else
				pdp->ta = sw0 & ADDRMASK;
		}
		if(down & KEY_LOAD2) pdp->tw = sw0;
	}

	pdp->power_sw = !!(sw1 & SW_POWER);
	pdp->single_cyc_sw = !!(sw1 & SW_SSTEP);
	pdp->single_inst_sw = !!(sw1 & SW_SINST);

	pdp->start_sw = !!(sw1 & KEY_START) && !(sw1 & KEY_MOD);
	pdp->sbm_start_sw = !!(sw1 & KEY_START) && (sw1 & KEY_MOD);
	pdp->stop_sw = !!(sw1 & KEY_STOP);
	pdp->continue_sw = !!(sw1 & KEY_CONT);
	pdp->examine_sw = !!(sw1 & KEY_EXAM);
	pdp->deposit_sw = !!(sw1 & KEY_DEP);
	pdp->readin_sw = !!(sw1 & KEY_READIN);
}

void
updatelights(PDP1 *pdp, Panel *panel)
{
	int indicators = 0;
	if(panel->sw1 & KEY_SPARE) {
		indicators |= pdp->ioc<<9;
		indicators |= pdp->ihs<<8;
		indicators |= pdp->ios<<7;
		indicators |= pdp->ioh<<6;
	} else {
		indicators |= pdp->run<<9;
		indicators |= pdp->cyc<<8;
		indicators |= pdp->df1<<7;
		indicators |= pdp->rim<<6;
	}
	indicators |= (0x8>>panel->sel1) << 10;
	indicators |= (0x8>>panel->sel2) << 14;
	indicators |= panel->sw1 & 0x3F;
	switch(panel->sel1) {
	case 0: panel->lights0 = AC; break;
	case 1: panel->lights0 = MA | IR<<12; break;
	case 2: panel->lights0 = pdp->pf<<6 | pdp->ss; break;
	case 3: panel->lights0 = pdp->ta; break;
	}
	switch(panel->sel2) {
	case 0: panel->lights1 = IO; break;
	case 1: panel->lights1 = PC; break;
	case 2: panel->lights1 = MB; break;
	case 3: panel->lights1 = pdp->tw; break;
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

void
lightson(Panel *panel)
{
	panel->lights0 = 0777777;
	panel->lights1 = 0777777;
	panel->lights2 = 0777777;
}

Panel*
getpanel(void)
{
	return attachseg("/tmp/b18_panel", sizeof(Panel));
}
