#include "common.h"
#include "whirlwind.h"
#include "panel_whirlwind.h"

void
updateswitches(Whirlwind *ww, Panel *panel)
{
	int sw0 = panel->sw[0];
	int sw6 = panel->sw[6];
	int down = sw6 & ~panel->psw6;
	panel->psw6 = panel->sw[6];

	// TODO: FFS RESET SEL
	ww->pc_reset_sw = panel->sw[0] & 03777;
	ww->ffs_reset_sw[0] = panel->sw[1];
	ww->ffs_reset_sw[1] = panel->sw[2];
	ww->ffs_reset_sw[2] = panel->sw[3];
	ww->ffs_reset_sw[3] = panel->sw[4];
	ww->ffs_reset_sw[4] = panel->sw[5];

	ww->btn_readin = !!(down & 0100000);
	// TODO: clear alarm
	ww->btn_start_over = !!(down & 0020000);
	ww->btn_start_at40 = !!(down & 0010000);
	ww->btn_stop = !!(down & 0004000);
	ww->btn_start_clock = !!(down & 0002000);
	ww->btn_restart = !!(down & 0001000);
	ww->btn_order_by_order = !!(down & 0000400);
	ww->btn_examine = !!(down & 0000200);
	// TODO: erase MS
	// TODO: reset FFS
	// TODO: reset all FFS
	// TODO: reset FFS at TP5
	// TODO: reset FFS at PCEC
	ww->stop_si1 = !!(sw6 & 2);

	// not on this panel
	ww->btn_clock_pulse = 0;
	ww->power_sw = 1;
}

void
updatelights(Whirlwind *ww, Panel *panel)
{
	int l;

	// TODO: alarms
	panel->lights[0] = ww->pc;
	panel->lights[1] = ww->br;
	panel->lights[2] = ww->ac_cry;
	panel->lights[3] = ww->ac;
	panel->lights[4] = ww->ar;
	panel->lights[5] = ww->cr;
	panel->lights[6] = ww->par;
	panel->lights[7] = ww->ior;

	// TODO: div error? or alarm?

	l = ww->tss;
	l |= ww->automatic << 5;
	l |= ww->stop_clock << 6;
	l |= ww->ms_on << 7;
	l |= ww->tpd << 8;
	l |= ww->cs << 11;
	panel->lights[8] = l;
	l = ww->sc;
	l |= ww->sr_ff << 6;
	l |= ww->sl_ff << 7;
	l |= ww->mul_ff << 8;
	l |= ww->div_pulse << 9;
	l |= ww->div_ff << 10;
	l |= ww->po_pulse << 11;
	l |= ww->po_ff << 12;
	l |= ww->sign << 14;
	l |= ww->src << 15;
	panel->lights[9] = l;
	l = ww->mar;
	l = ww->ssc << 11;
	l = ww->ov << 14;
	l = ww->sam << 15;
	panel->lights[01] = l;
	panel->lights[11] = ww->ffs[0];
	panel->lights[12] = ww->ffs[1];
	panel->lights[13] = ww->ffs[2];
	panel->lights[14] = ww->ffs[3];
	panel->lights[15] = ww->ffs[4];
}

void
lightsoff(Panel *panel)
{
	for(int i = 0; i < 16; i++)
		panel->lights[i] = 0;
}

Panel*
getpanel(void)
{
	return attachseg("/tmp/whirlwind_panel", sizeof(Panel));
}
