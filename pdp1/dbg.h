/* Debug service — DEBUG_PROTOCOL_SPEC.md v1.
 *
 * Three hooks into the emulator, and that is all:
 *
 *	dbgfetch()	in emu(), before cycle().  Breakpoints, step/run
 *			budgets, trace, the call ring, stop classification.
 *	dbgsvc()	in emu(), next to cli().  Commands, replies, events.
 *	dbgoverride()	at the end of updateswitches(), in every panel file.
 *			Tier 0: the switch override.
 *
 * plus two lines in readmem/writemem for watchpoints, behind dbg_anywp.
 */

void dbginit(PDP1 *pdp, int port);
int dbgfetch(PDP1 *pdp);	/* nonzero: stop here, do not cycle */
void dbgstop(PDP1 *pdp);
void dbgsvc(PDP1 *pdp);
void dbgoverride(PDP1 *pdp);

/* watchpoints.  dbg_anywp is zero unless a watchpoint is set, so the
 * memory fast path stays one predictable branch. */
extern int dbg_anywp;
void dbg_readmem(PDP1 *pdp, Addr a, Word val);
void dbg_writemem(PDP1 *pdp, Addr a, Word val);

/* headless test mode (-t): POWER forced on, no coremem load/dump */
extern int testmode;
