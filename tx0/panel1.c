#include "common.h"
#include "panel_pidp1.h"
#include "tx0.h"

#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

void
updateswitches(TX0 *tx0, Panel *panel)
{
	int sw0 = panel->sw0;
	int sw1 = panel->sw1;
	int sw2 = panel->sw2;
	int down = sw2 & ~panel->psw2;
	panel->psw2 = panel->sw2;

	// HACK: we don't have 18 bits on the PDP-1 panel
	// use lower sense switches for IR
	tx0->tbr = (sw0 & ADDRMASK) | (((sw2>>10)>>4) & 3)<<16;
	tx0->tac = sw1;

        tx0->sw_power = !!(sw0 & SW_POWER);
	tx0->sw_sup_alarm = 0;
	tx0->sw_sup_chime = 0;
	// no idea what these are
	tx0->sw_auto_restart = 0;
	tx0->sw_auto_readin = 0;
	tx0->sw_auto_test = 0;
	tx0->sw_stop_c0 = !!(sw2 & SW_SSTEP);
	tx0->sw_stop_c1 = !!(sw2 & SW_SINST);;
	tx0->sw_step = !!(sw2 & 02000);
	tx0->sw_repeat = !!(sw2 & 04000);
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
	int l5;
	l5 = 0;
	if(tx0->ss) l5 |= L5_RUN;
	if(tx0->c) l5 |= L5_CYC;
//	if(tx0->t) l5 |= L5_DF1;
	if(tx0->t) l5 |= L5_HSC;	// no test FF
	if(tx0->lp&1) l5 |= L5_BC1;	// no lp FFs
	if(tx0->lp&2) l5 |= L5_BC2;
	if(tx0->aff) l5 |= L5_OV1;	// no alarm FF
	if(tx0->r) l5 |= L5_RIM;
//	if(tx0->sbm) l5 |= L5_SBM;
//	if(tx0->ext) l5 |= L5_EXT;
	if(!tx0->ios) l5 |= L5_IOH;
	if(tx0->pbs) l5 |= L5_IOC;	// no pbs FF
//	if(tx0->ios) l5 |= L5_IOS;
	l5 |= L5_PWR;
	if(tx0->sw_stop_c0) l5 |= L5_SSTEP;
	if(tx0->sw_stop_c1) l5 |= L5_SINST;

	panel->lights0 = PC;
	panel->lights1 = MAR;
	panel->lights2 = MBR;
	panel->lights3 = AC;
	panel->lights4 = LR;
	panel->lights5 = l5;
	panel->lights6 = (tx0->ir<<3)<<13 | ((panel->sw2>>10)&077)<<6 |
		tx0->petr12<<2 | tx0->petr3<<1 | tx0->petr4;
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
