# Implementing the debug interface — a plan

How to get `DEBUG_PROTOCOL_SPEC.md` into the emulator without turning it into
a different program. Read the spec first for *what*; this is *where* and *in
what order*.

The shape: a simh-style console on one port, spoken by humans, agents and
frontends alike; connections fanned out so nobody needs a multiplexer; and a
switch override that sits politely on top of the real panel instead of
fighting it.

---

## Status

Phases 0–4 are **done**; phase 5 is client work in the other two repos and
has not been started. `pdp1/test/pdp1dbg_test.py` reports 30 passed, 0
failed, 1 skipped against `pdp1 -t`, and the same against the mock.

New: `netsvc.c`/`netsvc.h`, `pdp1/dbg.c`/`dbg.h`. Touched: `main.c` (the two
hooks, `startnet`, `-t`, `-l`), `pdp1.c` (`atfetch`, the two watchpoint
lines, `cmdfailed`/`cmdunknown`, display fan-out), `pdp1.h` (`void *dbg` at
the very end, `DispCon`), both panel files (one line each), `typtelnet.c`
(fan-out), `common.c` (`netlocalonly`).

Four things came out differently from the plan below, all noted in
`DEBUG_PROTOCOL_SPEC.md` where they matter:

1. **`-t` had to make `pwrclr` deterministic**, not just force POWER on. A
   randomised power-on leaves `cyc`/`bc`/`sbm` set, CONTINUE resumes into
   the middle of an instruction that never started, and the machine stops on
   `IR_INCORR` having executed nothing.
2. **`w pc` clears `cyc`/`df1`/`df2`/`bc`/`hsc`.** Setting PC means "the next
   instruction is here", which only means anything at a fetch boundary. This
   is the piece that actually replaces the EXAMINE/START dance.
3. **PC after a `hlt` points past it**, as the panel lights do. The v1 mock
   rewound it and two conformance tests encoded that; the emulator wins
   (spec §10), so the mock and the tests were fixed.
4. **`panel off` refuses but does not mirror** into the panel segment, per §4
   below rather than the v1 spec's MUST.

Still open, and deliberately not decided unilaterally: `-l` (bind loopback
only) exists and defaults to **off**, so remote panels and frontends keep
working. §9 argues it should default on.

---

## 1. The claim: three hooks, three new files, nine edited lines

Everything the protocol needs comes from three places in the existing code:

| hook | where | what it gives |
| ---- | ----- | ------------- |
| **fetch boundary** | `emu()`, before `cycle()` | breakpoints, step/run budgets, `trace`, the call ring, stop classification |
| **service** | `emu()`, next to `cli()` | reading commands, replying, events |
| **switch override** | end of `updateswitches()` | tier 0, `panel on/off` |

Plus two lines inside `readmem`/`writemem` for watchpoints. That is the whole
intrusion into existing files:

```c
        for(;;) {
                prev_start_sw = pdp->start_sw;
                ...
                updateswitches(pdp, panel);     /* + dbgoverride(pdp) at its end */

                if(pdp->power_sw) {
                        ...
                        if(pdp->run) {
                                if(doaudio) svc_audio(pdp); else stopaudio();
-                               cycle(pdp);
+                               if(atfetch(pdp) && dbgfetch(pdp))
+                                       dbgstop(pdp);   /* run = run_enable = 0 */
+                               else
+                                       cycle(pdp);
                        } else {
                        ...
                }
                agedisplay(pdp, 0);
                agedisplay(pdp, 1);
                cli(pdp);
+               dbgsvc(pdp);
        }
```

New files: `../netsvc.c` + `netsvc.h` (connection fan-out, reusable by the
other emulators), `pdp1/dbg.c` + `dbg.h` (the command language and debug
state). Nothing else moves.

---

## 2. The fetch-boundary hook does almost everything

This is the key structural idea, and it is why tier 2 needs no surgery on the
CPU model at all.

`atfetch(pdp)` — new, in `pdp1.c`, because only `pdp1.c` should know the
state machine — answers *"would the next `cycle()` call begin a new
instruction?"*. Candidate predicate, **to be confirmed against the state
machine before relying on it**:

```c
int atfetch(PDP1 *pdp) {
        return !pdp->cyc && !pdp->bc && !pdp->hsc && pdp->cychack == 0;
}
```

Note this is *not* `INST_DONE` (`pdp1.c:87`), which asks whether the current
instruction is finishing. We want the gap between instructions: break cycles
and high-speed-channel cycles must not count as instructions, and we must
never stop in the middle of a sequence break.

At that point PC is the address about to be fetched and the machine is in a
clean inter-instruction state. So `dbgfetch(pdp)` can, with no other hooks:

- **Breakpoints** — compare PC against the table. Stop *before* the fetch, so
  PC still points at the breakpoint address. Nothing is written to core, so
  nothing needs restoring, and a breakpoint on a `jsp` entry no longer eats
  the return address.
- **Budgets** — every boundary crossed is one instruction retired. `step n`,
  `run n`, `trace n`, `until addr n` all reduce to counting boundaries.
- **Trace** — at boundary *N* record PC and `core[PC]`; at boundary *N+1* the
  post-instruction registers are visible. Trace lines are emitted one
  boundary late, which costs nothing.
- **Call ring** — if `core[PC]` is `jsp`/`jda`/`cal`, remember it; at the next
  boundary record `(target, from, ret)` using the actual new PC. Ring of 64,
  surfaced by `back`.
- **Conditions** — `run n if <cond>` is evaluated here, which is exactly the
  "at instruction boundaries only" rule in the spec.

**Stopping.** At a boundary nothing is in flight, so `dbgstop()` sets
`run = run_enable = 0` directly and PC/cyc/df1/bc are already a valid resume
state — `cont` picks up correctly. This is deliberately *not* the STOP-key
path (`run_enable = 0` alone), which lets the current instruction finish;
that is right for a STOP key and wrong for a breakpoint.

**Do not implement `step` with the SINST switch.** Reusing
`single_inst_sw` means pulsing CONTINUE, several loop iterations per step,
and the residue problem the current tooling has (a client that dies
mid-command leaves SINST set). Counting boundaries needs none of that, and
the panel's own SINGLE INST switch keeps working independently — it just
produces `stop=manual`.

**Watchpoints** are the exception: they need the two lines in `readmem` /
`writemem` (`pdp1.c:99-110`), guarded by a global "any watchpoints set" flag
so the fast path stays one predictable branch. They record a pending stop and
let the instruction finish; the stop lands at the next boundary.

**Stop classification** (spec §6 reasons) also lives here: on a `run` 1→0
transition that we did not cause, ask why — `IR_INCORR` (→ `illegal`), OPR
with B9 (→ `halt`), `run_enable` cleared by the STOP key (→ `stop`),
`single_cyc_sw`/`single_inst_sw` (→ `manual`).

---

## 3. Fan-out: `netsvc`

The reusable half. Goal: **one accept path, all I/O on the emulator thread,
N clients per stream.** This is what deletes `pdp1_mux`.

```c
struct NetConn {
        FD fd;                  /* pollfd.c handle */
        NetSvc *svc;
        NetConn *next;
        char in[1024]; int nin;         /* line assembly, SVC_LINE only */
        char out[OUTBUF]; int nout;     /* never block the emulator */
        void *aux;
};

struct NetSvc {
        int port, maxconn, mode;        /* SVC_LINE | SVC_RAW */
        void (*accepted)(NetConn*);
        void (*line)(NetConn*, char*);          /* SVC_LINE */
        void (*data)(NetConn*, u8*, int);       /* SVC_RAW */
        void (*closed)(NetConn*);
        NetConn *conns;
};

void netsvc_add(NetSvc*);
void netsvc_start(void);        /* accept thread */
void netsvc_poll(void);         /* emulator thread: adopt, read, dispatch, flush */
void netsvc_write(NetConn*, const void*, int);
void netsvc_broadcast(NetSvc*, const void*, int);
```

The accept thread does nothing but `accept()` and push the fd onto a queue.
`netsvc_poll()`, called from the emulator loop, drains the queue, builds
`NetConn`s, registers them with `pollfd.c`, reads what's ready and dispatches.
That is prerequisites P1 and P2 discharged in one mechanism, and it fixes the
existing bug where a long-lived connection on 1040 stops the process
accepting displays and tapes.

**Sockets are `O_NONBLOCK`, and writes must never stall the machine.** A
short write keeps the remainder in `out`; on overflow, raw services (display)
drop — frames are disposable — and line services (debug) drop the
*connection*. Do not add POLLOUT handling to `pollfd.c` for this; the volumes
don't justify it.

Per-stream limits: debug 8, display 4 each, typewriter 4, reader/punch 1 (a
tape with two clients is meaningless).

**Display** (`3400`/`3401`): `flushdpy()` becomes `netsvc_broadcast()`. Keep
two logical `DispCon`s with their own `cmdbuf`/aging; only the write end
fans out. This also fixes `connectdpy` (`main.c:131-143`), which today closes
the *existing* display when a second client connects and then leaks the new
fd without installing it — so the second attempt disconnects the picture and
connects nothing.

**Typewriter** (`1041`): the whole change fits inside `typtelnet.c` and
touches the emulator not at all. `readwrite()` is a 1:1 poll today; make it
poll a small array — fan output out to every client, merge input from all of
them. The FIO-DEC/telnet translation stays exactly where it is. Doing it this
way means typewriter fan-out can ship before `netsvc` exists.

Once display and typewriter fan out, `pdp1_mux` has nothing left to do and
ports 1050/1051/3500/3501 disappear.

---

## 4. The switch override, panel-agnostic

Override at the **decoded** level, not the raw switch words. The Hermes patch
replaces `sw0/sw1/sw2` before decoding, which means the debug code has to
know the PiDP-1 bit layout. Instead, let `updateswitches` finish its job and
then overwrite the fields it produced:

```c
/* last line of updateswitches(), in panel1.c AND panelb18.c */
dbgoverride(pdp);
```

`dbg.c` then deals only in `pdp->ta`, `pdp->tw`, `pdp->ss`, `pdp->power_sw`,
`pdp->start_sw`… — no bit masks, no panel knowledge, and both panels are
supported by the same code. One line per panel file.

**Momentary keys.** `dbgoverride` asserts a key for exactly one pass;
`dbgsvc` at the bottom of the loop clears it. Since `Edge()` compares against
the value snapshotted at the top of `emu()`, that produces exactly one clean
edge. No sleeps, no races, no missed edges — the loop synthesising the edge
is the loop consuming it.

**The POWER trap, solved better than by mirroring.** Because the override
runs *after* the panel is read, `dbg.c` sees both the real panel's values and
its own. So `panel off` can simply look: is the real panel's POWER off while
the machine is powered and running? Then refuse with `?state` and say so.
No write-back into the panel segment, no panel-specific code, and the
failure the Hermes session spent an afternoon on becomes an error message.
`panel off force` for when you mean it. (`panel1.c` may *additionally* mirror
the toggles into the segment so the vpanel UI catches up — that's a local
nicety, not a protocol requirement, and it can't work for real switches.)

---

## 5. Pending commands

Immediate commands (`e d r w s b sw panel` …) run to completion inside one
`dbgsvc()` call. Pending ones (`step cycle run until next trace wait`) cannot:
a step doesn't finish in the iteration that starts it. Each connection
carries at most one:

```c
struct DbgConn {
        int events;             /* none | stop | all */
        int pending;            /* PEND_NONE | PEND_RUN | PEND_WAIT */
        int budget;             /* instructions remaining, -1 = unbounded */
        Cond cond;              /* optional stop condition */
        Addr tempbp; int hastemp;
        int trace, tracechanged;
        Word prevac, previo, prevma, prevmb; int prevov;  /* for 'changed' */
};
```

When a stop happens, walk the connection list: complete every pending run
command with its `+` status, then emit `!stop` to everyone *else* (spec §6 —
a connection's own `+` already carries it). On disconnect: cancel the pending
command, drop temporary breakpoints, clear any override bits that connection
owned. That last one is why SINST residue can't happen here.

---

## 6. simh vocabulary

The spec's verbs are already close; add aliases so simh muscle memory works
and nobody has to look anything up:

| simh | ours |
| ---- | ---- |
| `examine` / `ex` | `e` |
| `deposit` / `dep` | `d` |
| `break` / `nobreak` | `b` / `ub` |
| `step` / `cont` / `go` / `run` | same |
| `attach` / `detach` | `reader` / `punch` with and without a file |
| `show` / `set` | `s` / `sw`, and the option verbs |

Also worth taking from simh: **address ranges** in `e`, so `e 100-110` works
alongside `e 100 9`. Cheap, and it's what fingers expect.

On the name clash you flagged: panel operations are never top-level verbs —
they exist only as `key exam`, `key dep`, `sw`, `panel`. So a bare `d` can
only ever mean core-write, and the ambiguity is syntactic zero even though
the English is overloaded. If that still grates, `peek`/`poke` for core
access leaves *examine* and *deposit* meaning nothing but the keys.

---

## 7. Order of work

Each phase is independently useful; nothing after phase 0 is required for
the phase before it to be worth having.

**Phase 0 — hygiene, no new features.** `handlenetcmd`'s two buffer overruns
(`main.c:116-124`); the `connectdpy` bug; a `-t` headless mode (no
`coremem` load/dump, POWER forced on) so tests can run unattended.

**Phase 1 — `netsvc`, and move 1040 onto it.** Behaviour unchanged, but
commands now execute on the emulator thread and long-lived connections stop
blocking the accept loop. Everything else depends on this.

**Phase 2 — fan-out.** Display via `netsvc_broadcast`; typewriter inside
`typtelnet.c`. **Delete `pdp1_mux`.** This phase alone removes a process and
four port numbers, and it's independent of the debug work — do it whenever.

**Phase 3 — the debug core.** `atfetch` + `dbgfetch` + `dbgsvc`;
`e d poke z r w s go stop step cycle run until trace wait`, breakpoints,
events. On port 1040 as new verbs (rename display `d` → `dpy`, registers as
`reg`, so `r` stays the reader — nothing shipped uses `d` for display). At
the end of this phase the rx-0 backend and `pdp1_ai_debug` can both be
ported, and the conformance suite should go green.

**Phase 4 — panel and the rest.** `dbgoverride`, `panel`/`key`/`sw`, the
POWER refusal; then watchpoints, call ring, conditions, `next`.

**Phase 5 — clients.** `rx-0/pdp1net/mach.c`; the debug channel in
`web_pdp1/pdpsrv.go` (a persistent dial plus a message type — the `.lst` is
already in the browser, so source-level stepping is a UI problem).

---

## 8. Things not to break

- **`PDP1`'s layout.** Patch 0001 `memcpy`s the struct into `/dev/shm/pidp1`
  and the Python tools hardcode byte offsets (`core` at 40, `ta` at 262184).
  Debug state goes in its own `Dbg`, reached by a `void *dbg` **appended at
  the end** of `PDP1`. Never insert a field before `core[]`. External readers
  will see a pointer into the emulator's heap; it is not for them to follow.
- **Port 1040's existing language.** `pdp_periph` sends `r <file>`,
  `p <file>`, `muldiv`, `audio`; the web UI forwards arbitrary command lines;
  the agent skills use `l <file>`. Those five must keep working verbatim.
- **The physical panel stays authoritative** unless explicitly overridden,
  and the override must be visible — keep the Hermes patch's idea of lighting
  the sense-switch lamps while armed.
- **Nothing in the debug path may block the emulator.** Not a slow client,
  not a full socket buffer, not an unbounded `run`.

## 9. Deferred, on purpose

IOT/device-level events (`!iot`) — nothing needs them yet, keep the event
syntax extensible. Register watchpoints — the real watches are all core.
Symbols, listings, disassembly — client side, permanently. A second
`DispCon` refactor to N logical displays — two is enough.

One thing worth deciding early because it's cheap now and awkward later:
`socketlisten` binds `INADDR_ANY`, and `punch <file>` opens
`O_CREAT|O_WRONLY|O_TRUNC`. On a machine that ships to hundreds of users,
with a web frontend forwarding arbitrary command lines, a `-l` flag to bind
loopback-only (default on, opt out for remote panels) is one line and closes
the whole question.
