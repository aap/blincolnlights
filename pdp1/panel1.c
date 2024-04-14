#include "common.h"
#include "panel_pidp1.h"
#include "pdp1.h"

void
updateswitches(PDP1 *pdp, Panel *panel)
{
	int sw0 = panel->sw0;
	int sw1 = panel->sw1;
	int sw2 = panel->sw2;

	pdp->extend_sw = !!(sw0 & SW_EXTEND);
	pdp->eta = sw0 & EXTMASK;
	pdp->ta = sw0 & ADDRMASK;
	pdp->tw = sw1;
	pdp->ss = (sw2>>10) & 077;

	pdp->power_sw = !!(sw0 & SW_POWER);
	pdp->single_cyc_sw = !!(sw2 & SW_SSTEP);
	pdp->single_inst_sw = !!(sw2 & SW_SINST);

	pdp->start_sw = !!(sw2 & (KEY_START|KEY_START_UP));
	pdp->sbm_start_sw = !!(sw2 & KEY_START_UP);
	pdp->stop_sw = !!(sw2 & KEY_STOP);
	pdp->continue_sw = !!(sw2 & KEY_CONT);
	pdp->examine_sw = !!(sw2 & KEY_EXAM);
	pdp->deposit_sw = !!(sw2 & KEY_DEP);
	pdp->readin_sw = !!(sw2 & KEY_READIN);
}

void
updatelights(PDP1 *pdp, Panel *panel)
{
	int l5;
	l5 = 0;
	if(pdp->run) l5 |= L5_RUN;
	if(pdp->cyc) l5 |= L5_CYC;
	if(pdp->df1) l5 |= L5_DF1;
//	if(pdp->hsc) l5 |= L5_HSC;
	if(pdp->bc&1) l5 |= L5_BC1;
	if(pdp->bc&2) l5 |= L5_BC2;
	if(pdp->ov1) l5 |= L5_OV1;
	if(pdp->rim) l5 |= L5_RIM;
	if(pdp->sbm) l5 |= L5_SBM;
	if(pdp->exd) l5 |= L5_EXD;
	if(pdp->ioh) l5 |= L5_IOH;
	if(pdp->ioc) l5 |= L5_IOC;
	if(pdp->ios) l5 |= L5_IOS;
	l5 |= L5_PWR;
	if(pdp->single_cyc_sw) l5 |= L5_SSTEP;
	if(pdp->single_inst_sw) l5 |= L5_SINST;

	panel->lights0 = pdp->epc | PC;
	panel->lights1 = pdp->ema | MA;
	panel->lights2 = MB;
	panel->lights3 = AC;
	panel->lights4 = IO;
	panel->lights5 = l5;
	panel->lights6 = pdp->ir<<13 | pdp->ss<<6 | pdp->pf;
}

void
lightsoff(Panel *panel)
{
	panel->lights0 = 0;
	panel->lights1 = 0;
	panel->lights2 = 0;
	panel->lights3 = 0;
	panel->lights4 = 0;
	panel->lights5 = 0;
	panel->lights6 = 0;
}

Panel*
getpanel(void)
{
	return attachseg("/tmp/pdp1_panel", sizeof(Panel));
}
