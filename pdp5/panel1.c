#include "common.h"
#include "panel_pidp1.h"
#include "pdp5.h"


void
updateswitches(PDP5 *pdp, Panel *panel)
{
	int sw0 = panel->sw0;
	int sw2 = panel->sw2;

	pdp->n.sr = sw0 & 07777;

	pdp->sw_power = !!(sw0 & SW_POWER);
	pdp->sw_single_step = !!(sw2 & SW_SSTEP);
	pdp->sw_single_instruction = !!(sw2 & SW_SINST);

	pdp->key_start = !!(sw2 & KEY_START);
	pdp->key_load_address = !!(sw2 & KEY_START_UP);
	pdp->key_stop = !!(sw2 & KEY_STOP);
	pdp->key_continue = !!(sw2 & KEY_CONT);
	pdp->key_examine = !!(sw2 & KEY_EXAM);
	pdp->key_deposit = !!(sw2 & KEY_DEP);

//printf("key: %d %d %d %d %d\n", pdp->key_continue, pdp->key_load_address, pdp->key_start, pdp->key_examine, pdp->key_deposit);
}

void
updatelights(PDP5 *pdp, Panel *panel)
{
	int l5 = 0;
	if(pdp->run) l5 |= L5_RUN;
	if(pdp->c.state == MAJ_D) l5 |= L5_DF1;
	if(pdp->c.ac & 010000) l5 |= L5_OV1;
	if(pdp->io_hlt) l5 |= L5_IOH;
	l5 |= L5_PWR;
	if(pdp->sw_single_step) l5 |= L5_SSTEP;
	if(pdp->sw_single_instruction) l5 |= L5_SINST;

	panel->lights0 = 0;	// PC
	panel->lights1 = pdp->c.ma;
	panel->lights2 = pdp->c.mb;
	panel->lights3 = pdp->c.ac & 07777;
	panel->lights4 = 0;	// IO
	panel->lights5 = l5;
	panel->lights6 = pdp->c.ir<<14 | (040>>pdp->c.state);
	panel->lights7 = 0;
	panel->lights8 = 0;
	panel->lights9 = 0;
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
	panel->lights7 = 0;
	panel->lights8 = 0;
	panel->lights9 = 0;
}

void
lightson(Panel *panel)
{
	panel->lights0 = 0777777;
	panel->lights1 = 0777777;
	panel->lights2 = 0777777;
	panel->lights3 = 0777777;
	panel->lights4 = 0777777;
	panel->lights5 = 0777777;
	panel->lights6 = 0777777;
	panel->lights7 = 0777777;
	panel->lights8 = 0777777;
	panel->lights9 = 0777777;
}

Panel*
getpanel(void)
{
	return attachseg("/tmp/pdp1_panel", sizeof(Panel));
}
