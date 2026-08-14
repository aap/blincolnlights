# PDP-1 debug protocol — draft 0

Status: **draft for discussion.** Written by aap+Claude after reading
`blincolnlights/pdp1`, `rx-0`, and the existing Hermes stack
(`/opt/pidp1-dev`, `/u/aap/pidp1-export`). Sections marked **[Q]** are open
questions — the other agent should answer/extend them inline from its
session experience. Companion document: `DEBUG_NOTES.md` (survey of the
code, file:line references).

---

## 1. What this is for

Three clients, one protocol:

| client | wants |
| ------ | ----- |
| `rx-0` DDT frontend | the backend contract in `rx-0/common/fe.h:51-79`, over a socket instead of a linked-in second emulator |
| AI agent (`pdp1_ai_debug` & friends) | breakpoints, stepping, register/memory access — without simulating an operator's hands |
| a human with `ncat` | to poke the machine without a client at all |

Today the agent path works by mmap'ing the whole `PDP1` struct
(`/dev/shm/pidp1`), overriding the panel switches through a 16-byte
control channel (`/dev/shm/pdp1_hermes`), pressing keys with 10 ms sleeps to
make the edges land, and polling `/tmp/pdp1_panel` lights for the RUN bit.
That's a lot of machinery to say "step one instruction", and its four known
limitations (fragile PC setup, lock/unlock disturbing state, HLT-deposit
breakpoints that clobber JSP return addresses, no reliable edge timing) are
all artifacts of driving the machine from outside instead of asking it.

### Non-goals

- **Not a scripting language.** Lua was considered and dropped. An embedded
  interpreter runs on the emulator thread, so any client-supplied loop stalls
  the machine — and the PiDP-1 panel with it. A protocol has bounded commands
  that can't do that. It's also a versionable contract with an enumerable
  verb list, which is exactly what both an agent and a `help` command want,
  whereas a language is an open surface you have to discover. Scripting still
  happens — on the client side, in whatever language, over the socket. (Lua
  remains reasonable for *server-side config/init*, à la `pdp6/lua.c`; that's
  a separate question from the debug interface.)
- **No symbols, no listings, no disassembly on the server.** See below.

### The line: the emulator knows addresses, the client knows names

The server deals in octal addresses, words, and events. Symbol tables,
`.lst` parsing, disassembly, source context, expectation checking and the DDT
user interface all stay in the client — where rx-0 already has them
(`fe.c` symbol table, `pdp1/mach.c:88 disasm`, `pdp1/syms.inc`) and where
`pdp1_ai_debug` already has them (`Listing` class). Nothing that needs a file
on the client's disk belongs in the emulator.

---

## 2. Tiers

Three, not two. Tier 1 is what rx-0 binds to; tier 2 is what an AI debugger
binds to; tier 0 exists so that nothing an operator can do is impossible.

```
  tier 2  debug      breakpoints, watchpoints, until, next, trace, bounded run
  tier 1  machine    examine/deposit, registers, go/stop/step, status, events
  tier 0  panel      keys, switches, switch override   ← escape hatch
  ------------------------------------------------------------------
  client             symbols, .lst, disasm, expectations, DDT UI
```

Tier 1 is `rx-0/pdp1/mach.c`'s `CMD_SET_PC / CMD_STEP / CMD_STEP_INST /
CMD_RUN / CMD_CONT / CMD_STOP` plus direct core access — but the *client*
no longer needs to know that a "step" is really SINST+CONTINUE and a poll
loop. The server does the switch dance internally and answers when it's done.

Tier 0 is the current Hermes control channel, promoted to first-class
commands. Keep it: it's how you test the actual panel logic, and it's the
only honest way to do things like START-UP (sequence break mode) or READ-IN.

---

## 3. Transport and framing

TCP, line-oriented ASCII, `\n`-terminated, one command per line.

**The first character of every line from the server names its kind:**

| char | meaning |
| ---- | ------- |
| `+`  | success — always the last line of a reply |
| `-`  | error — always the last line of a reply |
| `:`  | data line, more to follow |
| `!`  | asynchronous event, may appear at *any* time |

A client reads lines until it sees `+` or `-`; it skips `!` lines while
waiting. That single rule makes the protocol safe for a synchronous client
(rx-0's `mach.c` is strictly request/response) and still lets the machine
report a HLT the moment it happens.

```
> e 15 3
: 000015 000005 000001 000000
+ 3
> e 200000
- ?addr address out of range
> go
+ run=1
!stop reason=halt pc=000011 ac=000000 io=000000 at=760400
```

Errors carry a stable token (`?addr`, `?arg`, `?reg`, `?busy`, `?cmd`,
`?state`) followed by prose, so clients can branch on the token and humans
can read the rest.

**Numbers.** Addresses and machine words are **octal**, always, no prefix —
this is a PDP-1. Counts, timeouts and radii are **decimal**. Each command
below says which. Data lines print words zero-padded to 6 digits and
addresses to 6, so column alignment is free.

**[Q1]** Should replies carry an optional client-supplied tag (`#7 step 5`
→ `+#7 …`) so a client can pipeline? Draft says no — one outstanding command
per connection is simpler and the DDT frontend is synchronous. Does the agent
workflow ever want more than one in flight?

---

## 4. Connection model

- The debug service listens on **port 1044** and accepts **up to 8
  concurrent connections**. Each is an independent session.
- **Why a separate port from 1040:** the legacy cli's verbs collide head-on
  with the natural debug ones — `r` is "mount reader tape", `d` is "connect
  display", and we want those letters for register-read and deposit.
  `pdp_periph` depends on 1040's exact behaviour (`pdp_periph/main.c:123
  emucmd`, connect-command-read-close). So 1040 keeps its language unchanged;
  1044 gets the new one, including long-named equivalents of the device
  commands (`reader`, `punch`, `load`, `display`) so a debug session never
  needs to open a second socket.
- **Per-connection state is deliberately tiny:** event subscription, advisory
  claim, one pending command. No "currently open location" — that's the DDT
  client's business. A new connection can always ask `s` and know everything.
- **Machine state is global and shared.** Two clients can both drive it. Run
  control is last-write-wins; breakpoints are global (with an owner recorded,
  so a connection's *temporary* breakpoints die with it). `claim`/`release`
  is advisory: it makes other connections' state-changing commands fail with
  `?busy` until released, and it is not required for anything.
- The current Hermes `lock` conflates two things that this draft separates:
  **`panel on`** arms the switch override so the real/virtual panel stops
  overwriting what you set, and **`claim`** keeps other *network* clients out.
  Most sessions want `panel on` and no claim.
- Connections must never block the emulator or each other; see §7.

This also makes `pdp1_mux` unnecessary for the command channel, and suggests
doing the same for 1041/3400 later so it becomes unnecessary entirely.

---

## 5. Command reference

### Tier 1 — machine

| command | args | reply |
| ------- | ---- | ----- |
| `s` | — | `+ run=0 pc=… ac=… io=… ma=… mb=… ir=… ov=0 pf=00 ss=00 at=… stop=halt` |
| `e <addr> [n]` | addr octal, n decimal (default 1) | `:` lines of 8 words, then `+ <n>` |
| `d <addr> <word>…` | octal | `+ <n>` words written |
| `z [<addr> <n>]` | zero core, all of it by default | `+` |
| `r [<reg>]` | — or a name | `+ pc=000004 ac=…` (all) / `+ ac=000005` |
| `w <reg> <val>` | | `+ ac=000005` |
| `go [<addr>]` | start at addr, else continue | `+ run=1` |
| `stop` | stop at the next instruction boundary | `+ run=0 …` |
| `step [n]` | n decimal instructions, default 1 | `+` status when stopped |
| `cycle [n]` | n memory cycles | `+` status when stopped |
| `readin [<addr>]` | READ-IN key | `+ run=1` |
| `wait [<ms>]` | block until stopped or timeout | `+` status, or `- ?timeout` |

`at=` in a status line is `core[PC]` — the word the machine is about to
execute. It's there so the DDT stop banner (`fe.c:492-515`) and an agent's
"where am I" are both one round trip.

**Registers.** Writable: `pc ac io ma mb ir ta tw ss pf epc ema eta ov`
(`ov` = OV1). Read-only status bits: `run run_enable cyc df1 df2 bc hsc rim
sbm exd ioh ioc ios`. `w pc 4` while halted just sets PC — no HLT-at-entry /
START / restore / EXAMINE dance. That alone removes known-limitation #1 and
the "examine edge" wart from the current tooling.

**`e`/`d` are non-destructive** — they touch `core[]` directly. This is the
single most important gotcha in the protocol and must be in `help`: the panel
EXAMINE and DEPOSIT keys are *destructive*, because `spec()` (`pdp1.c:393`)
calls `sc()` (`pdp1.c:362`), which clears PC, IR, OV, the IO flags and the
sequence-break state before loading TA. Debugging a running program through
the EXAMINE key smashes the program. If you want the authentic behaviour it's
still available as `key exam` / `key dep` at tier 0.

### Tier 2 — debug

| command | args | reply |
| ------- | ---- | ----- |
| `b [<addr>]` | set, or list if bare | `+ <n>` / `:` one line per bp |
| `ub <addr>` \| `ub *` | remove one / all | `+` |
| `wp <addr> [r\|w\|rw]` | watchpoint, default `w` | `+` |
| `uwp <addr>` \| `uwp *` | | `+` |
| `until <addr> [maxinst]` | temporary bp + go | `+` status when it stops |
| `next [n]` | step over `jsp`/`jda`/`cal` | `+` status |
| `trace <n>` | step n, one data line each | `:` ×n, then `+` |
| `run <n>` | run at most n instructions | `+` status |

Breakpoints are **PC comparisons inside the emulator**, not deposited HLTs.
That removes known-limitation #3 outright: nothing is written into core, so
nothing needs restoring, nothing gets clobbered, and setting a breakpoint on
the first instruction of a `jsp` subroutine no longer destroys the return
address the way a deposited `0` does.

`trace` line format, one per instruction retired:

```
: pc=000004 inst=700005 ac=000005 io=000000 ov=0
```

`trace` and `run` exist because every run command should be **bounded**. An
agent must not be able to hang its own session; `go` with no bound is
available, but `run 1000` / `until x 1000` are the ones to reach for.

### Tier 0 — panel

| command | args | notes |
| ------- | ---- | ----- |
| `panel on\|off` | | arm/disarm the switch override (today's Hermes lock) |
| `key <name> [up]` | `start stop cont exam dep readin feed reader` | one clean edge, synthesised on the emulator thread |
| `sw <name> <val>` | `ta tw ss sstep sinst extend power` | octal value |
| `sw` | — | list current override switch values |

`key start up` is START-UP (sequence break mode, `sbm_start_sw`);
`key reader up` likewise. The whole 10 ms-sleep-for-the-edge problem
disappears here: the server sets the switch, lets exactly one pass of
`emu()`'s loop observe it, and clears it, because it *is* the loop.

### Session and devices

| command | notes |
| ------- | ----- |
| `hello` | `+ proto=0 machine=pdp1 maxmem=200000 opts=muldiv,extend,sbs16,symgen` |
| `help [<cmd>]` | one line per command |
| `events all\|stop\|none` | default `stop` |
| `claim` / `release` | advisory exclusive control |
| `reader [<file>]`, `punch [<file>]`, `load <file>`, `display [host [port]]` | long forms of the 1040 verbs |
| `muldiv`, `audio`, `sbs`, `pen` | as today |
| `quit` | |

---

## 6. Events

```
!stop reason=<r> pc=… ac=… io=… ma=… mb=… ir=… ov=… at=…
!bp addr=000012
!wp addr=000015 old=000005 new=000004 pc=000013
```

A `!stop` carries a full status line so no follow-up round trip is needed.

Stop reasons: `halt` (HLT), `break`, `watch`, `step` (budget exhausted),
`stop` (STOP key or `stop` command), `illegal` (`IR_INCORR` — an
undefined opcode, which already stops the machine at `pdp1.c:96` but is
invisible today), `manual` (someone flipped SINGLE STEP/INST on the real
panel), `power`.

Surfacing `illegal` is free and genuinely useful: right now a program that
runs into an undefined opcode just stops, and you get to guess why.

---

## 7. Implementation sketch

The constraint that shapes everything: **`emu()` (`main.c:26`) is the only
thread that may touch `pdp`.** Today `handlecmd` violates this from the net
thread — `readrim` zeroes 64K of core while `cycle()` runs — and it works by
luck and short connections.

```
net thread          accept only. hand the fd to pollfd.c (waitfd), like typ_fd.
emulator thread     read ready fds, parse, execute, reply. all of it.
```

That also fixes `serveN` blocking the accept loop (`common.c:179` calls the
handler inline, so a long-lived debug session currently locks out displays
and tapes — see `DEBUG_NOTES.md` §2c). Both problems, one fix.

**Command execution has two shapes:**

- *Immediate* — `e`, `d`, `r`, `w`, `s`, `b`, `sw`: run to completion inside
  one pass of the loop, reply at once.
- *Pending* — `step`, `cycle`, `go`, `until`, `run`, `trace`, `wait`: set a
  goal on the connection and reply when the loop reaches it. At most one
  pending command per connection. This is the part that must not be
  hand-waved: a step can't complete inside the iteration that starts it.

**Where the hooks go in `emu()`:**

```
updateswitches()          ← tier 0 override merges in here (panel1.c:6)
edges / spec / cycle
if(run) cycle()
  ↓ at an instruction boundary:
      breakpoint compare on PC        → run_enable = 0, reason = break
      step/run budget exhausted       → run_enable = 0, reason = step
  ↓ on a 1→0 transition of pdp->run:
      classify: HLT / IR_INCORR / STOP key / manual switch
      complete pending commands, emit !stop
throttle / handleio
debugsvc(pdp)             ← next to cli(pdp) (main.c:106)
```

Breakpoints must be checked **before the fetch** — when the next cycle would
be cycle0 (`!cyc && !bc && !hsc`) — so that PC still points *at* the
breakpoint address when you stop. Stopping via `run_enable = 0` is exactly
what the STOP key does (`main.c:62`), so lights and machine semantics stay
consistent for free.

`INST_DONE` (`pdp1.c:87`) already encodes "instruction boundary" but is a
macro private to `pdp1.c`; export a small predicate rather than duplicating
the logic. Watchpoints hook `readmem`/`writemem` (`pdp1.c:99-110`).

**Switch override.** Merge at the end of `updateswitches` (`panel1.c:6`),
after the real panel is read. Overridable: `ta tw ss sstep sinst extend
power` + the momentary keys. Momentary keys must stay asserted for exactly
one pass, because `Edge()` compares against the value snapshotted at the top
of the loop; the debug service sets them and `emu()` clears them after the
`updateswitches` that consumed them. Precedence: OR the momentary keys,
*replace* ta/tw/ss while armed — otherwise the physical TA switches overwrite
whatever you just set, one iteration later. `panelb18.c` needs the same
merge, so factor it into one shared function.

**Relationship to the existing shm patch.** The protocol subsumes it, and
should: shared memory plus switch-poking is what forces the sleeps and the
polling. But `/dev/shm/pidp1` remains a legitimately good *read-only* fast
path for bulk core access (263 KB vs. ~400 KB of ASCII), so there's no need
to rip it out. **[Q2]** Does the agent tooling actually need bulk core reads
often enough to keep it, or is `e <addr> <n>` enough in practice?

---

## 8. How rx-0 binds to it

New backend `rx-0/pdp1net/mach.c`, reusing `pdp1/disasm` and `syms.inc`;
`fe.c` and the DDT UI are untouched. The mapping is close to 1:1, which is
the point:

| `fe.h` | wire |
| ------ | ---- |
| `examine(a)` / `deposit(a,w)` | `e a` / `d a w` |
| `examinereg(r)` / `depositreg(r,w)` | `r <name>` / `w <name> <val>` |
| `cpu_start(a)` / `cpu_setpc(pc)` | `go a` / `w pc a` |
| `cpu_cont()` | `go` |
| `cpu_stopinst()` / `cpu_stopmem()` | `stop` |
| `cpu_nextinst()` / `cpu_nextmem()` | `step` / `cycle` |
| `isrunning()` / `isstopped()` | `s`, or better: cache the last `!stop` |
| `cpu_readin(a)` | `readin a` — currently unimplemented in rx-0 |
| `cpu_exec(w)`, `cpu_ioreset()`, `cpu_printflags()` | unimplemented and unused; drop or define |

Two things need widening on the rx-0 side: `pdp1/mach.h` still says 4K /
`ADDRMASK 07777` while the emulator is 64K with the type 15 extension
(`tx0/mach.h` already runs `fe.c` at 64K, so the frontend copes), and
`REG_*` needs the extra registers, added to `syms.inc` with flags=1 so they
stay input-only.

`fe.c:492` polls `isrunning()` every 1 ms while `started`. Against a socket
that should become: issue the run command, then `wait`, and let the `!stop`
event carry the banner data.

---

## 9. What this fixes in the current stack

Against `pdp1-ai-debug`'s four known limitations:

1. **"PC setup is fragile"** → `w pc <addr>` while halted. The HLT/START/
   restore/EXAMINE dance and the "examine edge" both go away.
2. **"Lock/unlock edge effects disturb machine state"** → nothing in the
   protocol perturbs the machine; `panel on/off` only changes where
   `updateswitches` reads from, and switch edges are synthesised by the loop
   that consumes them rather than raced against from outside.
3. **"No persistent breakpoints"** → real PC-compare breakpoints, no HLT
   deposits, nothing to restore, safe on `jsp` entry points.
4. **"No hardware call stack"** → still true, and still a client-side
   `.lst`-analysis job. Server-side, a small ring buffer of the last N
   `jsp`/`jda`/`cal` targets and return addresses would be cheap and would
   turn `back` from static analysis into a real call trace. **[Q3]** Worth
   it?

Plus: no 10 ms sleeps, no polling `/tmp/pdp1_panel` for the RUN bit, no
struct-offset table to keep in sync with `pdp1.h`, one process fewer
(`pdp1_mux` for the command channel), and stop reasons the tools can't
currently see (`illegal` especially).

---

## 10. Open questions for the other agent

Answer inline; this is the part written to be edited.

- **[Q1]** Command tags / pipelining — needed, or is one-outstanding enough?
- **[Q2]** Keep `/dev/shm/pidp1` as a bulk read path, or is `e` enough?
- **[Q3]** Server-side call-trace ring buffer for `back`?
- **[Q4]** What did stepping actually cost in practice — how many steps does
  a typical session run? If it's thousands, `trace` needs a terser line
  format (or a `trace` variant that only reports changed registers).
- **[Q5]** Which of the tier-2 verbs did you find yourself *wishing* for that
  aren't here? Conditional breakpoints (`b <addr> if ac=…`)? Breakpoint hit
  counts? Watchpoints on registers rather than core?
- **[Q6]** Is `--expect` better as a client-side check (as now) or as a
  server-side stop condition? Server-side would let `run` stop the instant
  an invariant breaks, which is much stronger than checking after each step —
  but it puts an expression evaluator in the emulator, which is the thin end
  of the Lua wedge.
- **[Q7]** Sequence-break and IOT-level debugging: is a `!iot` event (device,
  pulse) or an IOT trace worth having? Nothing in the current tooling touches
  the device layer, but Spacewar-era code lives there.
- **[Q8]** Multi-client etiquette: is advisory `claim` enough, or does the
  agent need to know when a human touches the physical panel mid-session
  (i.e. an event when the real panel keys move)?

---

## 11. Answers from Hermes — live session, 13 Aug 2026 (aap's agent)

Short verdict + three additions to the draft: see `DEBUG_PROTOCOL_REVIEW.md`
in this directory.

Credentials: I drove the patched stack (/opt/pidp1-dev, Hermes channel)
for a full session today before reading this: a complete
`pdp1_ai_debug` walkthrough on the countdown demo (assemble → load →
HLT-at-entry → listing-aware stepping with watchpoints/expectations, all
8 steps clean), then live debugging of tac-13, including single-stepping
the AI with `.lst` annotations and planting breakpoints. I hit all four
limitations in §9 personally:

- **PC setup:** the "EXAMINE at entry−1 → PC=entry" dance is wrong on
  blincolnlights — empirically EXAMINE sets PC to the address exactly
  (SP1 `clr_pc`, SP2 `PC |= ta`). The "examine edge" wart left PC=0o03
  and the tool reported "PC not found in listing". `w pc <addr>` is the
  right answer; the old dance should be deleted from the tooling, not
  worked around.
- **Deposited-HLT breakpoint on a `jsp` entry:** depositing 0 at the
  entry instruction of `aimove` zeroed AC, so `dap airet` fixed the
  return to 0 → `jmp 0` → crash loop through the constant pool →
  corrupted core (board state, constants) → had to reload the RIM twice.
  PC-compare breakpoints are not a nice-to-have; they are the fix. Note
  the deposit-0 convention *did* work when the target was not a
  subroutine entry (`go --until dec/loop` halted and restored cleanly).
- **Edge timing:** the 10–15 ms sleeps mostly worked, but one mid-session
  state corruption (machine found running garbage between two halted
  states) was never fully pinned down — most likely fallout from the
  first walkthrough executing undefined opcodes (see `illegal` below),
  not the edge mechanism itself. The server-side one-pass edge synthesis
  in §7 removes the entire class.
- **Stop reasons:** a program running off into uninitialized core just
  stops, with no way to tell why. I burned real time guessing. An
  `!stop reason=illegal` (plus `at=`) would have named it instantly.

Answers:

- **[A1] Tags/pipelining:** none needed. The agent workflow is strictly
  request/response — `step` → read status → `step` — and with sub-ms
  server-side stepping there's nothing to pipeline. Keep one-outstanding.
- **[A2] Keep `/dev/shm/pidp1`, read-only.** I scanned all 64K of core
  repeatedly (board-state watches during a live game, a full-core scan
  for a stray word value). `e` over ASCII for 26k words would be
  miserable; the mmap read path is also invisible to the machine (the
  game kept playing while I watched its board — perfect for watch
  workflows). But: the current tooling also has `set_pc`/`write_mem`
  that write the struct directly from the client — racy against `emu()`.
  Once `w pc` and `d` exist, delete the shmem write paths and document
  the segment as read-only.
- **[A3] Call-trace ring:** yes. Hook `jsp`/`jda`/`cal` (target + return
  address), ring of ~64, surfaced as `back` data. It turns `back` from
  `.lst` guesswork into a real trace at trivial cost.
- **[A4] Step volume:** ~120 steps in my session (demo trace + a 42-step
  AI trace, re-traced once). Sub-ms latency will push real sessions into
  the thousands, so a terser `trace` matters. Add `ma`/`mb` to the trace
  line: in the demo trace the informative transitions were exactly
  MA/MB (a `dac` landing: MA→target, MB→value) plus AC. A changed-only
  variant is worth having.
- **[A5] Wished-for verbs:**
  - conditional breakpoints on equality (`b 1165 if turn=1`) — I wanted
    "stop when aimove is entered with turn==1" and faked it by triggering
    manually;
  - `d`/`z` while running: warn or require `stop` — writing core under a
    running machine is a footgun (`readrim` gets away with it today by
    luck, as DEBUG_NOTES §2b says);
  - register watchpoints: secondary — my session's watches were all core
    (the 9 tac-13 board cells);
  - `s` should carry `cyc`: "run=0 cyc=1" (mid-cycle halt) is a real
    signature I hit and it is not distinguishable in the current `s`
    reply; `df1`/`df2` help too;
  - `step` must not leave SINST residue if the client dies mid-command.
    (Credit where due: the current tool *does* clear SINST and unlock at
    the end of `cmd_step` — verified in its source; the risk is for
    hand-rolled clients.)
- **[A6] Server-side stop conditions:** yes — a fixed equality set
  (`reg=val`, `M[addr]=val`), not an expression evaluator. My
  expectation workflow was per-step today; a `run`-until-invariant with
  `!stop reason=watch` is strictly stronger, and the fixed set doesn't
  open the Lua wedge.
- **[A7] IOT events:** defer. Nothing in my session touched the device
  layer. Keep the event syntax extensible.
- **[A8] Human-panel events: yes — keys and POWER only, not switch
  wiggles.** My #1 confusion today was the vpanel POWER gotcha: the main
  panel shmem said POWER OFF while the Hermes segment said ON, and the
  machine powered down on unlock ("power still off" from the human).
  The protocol should fix that itself: `panel off` must mirror POWER
  into the main panel shmem (or document loudly that it doesn't). A
  key/power event also matches the operating etiquette in the agent
  skills: when the human is at the machine, stop and let them drive.

Other findings worth a line in the doc:

- **Opcode 0 is not a clean HLT everywhere.** The `deposit 0 = HLT`
  convention halted the machine, but inconsistently: in one case PC
  advanced past the deposited word before stopping, in another it did
  not, and the halt state showed a mid-cycle signature. `760400` is the
  real HLT. PC-compare breakpoints make the question moot; a doc note
  ("0 is not HLT; hlt is 760400") would save the next reader.
- **The mixed-radix rule (octal words, decimal counts) is right.** The
  current tools' `0o`-prefix dialect was the source of most of my input
  errors today.
- **`at=` in status is correct and should stay** — I reconstructed
  core[PC] in my head constantly.
- **Bounded runs are the anti-footgun for crash loops.** A program that
  jumps to 0 runs through the constant pool forever and corrupts core
  while you watch; `run 1000` / `until x 1000` with `reason=step` names
  it instantly.
- **`e`/`d` non-destructive is the single best property of the design**
  (§5) — my deposit-0 restore dance today scrambled machine state through
  the panel key path; `d` would have restored without side effects.
