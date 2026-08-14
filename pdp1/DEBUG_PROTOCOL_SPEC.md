# PDP-1 debug protocol — specification, version 1

Normative spec for the debug service in the blincolnlights PDP-1 emulator.
Short by design: this is the document you implement from.

- Rationale, tiers, alternatives considered: `DEBUG_PROTOCOL.md`
- Code survey with file:line references: `DEBUG_NOTES.md`
- Live-session review that shaped this: `DEBUG_PROTOCOL_REVIEW.md`
- Conformance tests + reference mock: `test/`

Keywords MUST / SHOULD / MAY are used in the usual sense.

---

## 0. Prerequisites — all discharged

Seven items. The first five blocked implementation; the last two were
decisions that had to be made before code was written, because they affect
other people's tools. **All seven are done** — see the notes on each.

**P1. Connections must not run on the accept loop.** `serveN`
(`common.c:167-184`) calls the handler inline, so one long-lived connection
stops the process accepting anything else. Accept on the net thread, hand the
fd to `pollfd.c` (`waitfd`), read and reply from the emulator thread — the
same shape as `typ_fd`. *Done: `netsvc.c`.*

**P2. All `pdp` mutation moves to the emulator thread.** Today `handlecmd`
runs on the net thread and mutates core (`readrim` zeroes 64K words) while
`cycle()` runs. Fix 1040 with the same mechanism as P1 rather than building a
second service on the broken pattern. *Done: 1040 is a `netsvc` line
service and `dbgsvc()` runs it from `emu()`.*

**P3. Export an instruction-boundary predicate.** `INST_DONE` (`pdp1.c:87`)
is a macro private to `pdp1.c`; breakpoints, step budgets, `trace` and
watchpoint reporting all need it. *Done, but not `INST_DONE`:* `pdp1.c`
exports `atfetch(PDP1*)`, "would the next `cycle()` begin a new
instruction?", which is the gap *between* instructions rather than the end
of one. `instdone()` is exported too, unused so far.

**P4. Classify stops.** Detect the `run` 1→0 transition and why: HLT
(`IR_OPR && MB&B9`), `IR_INCORR`, `run_enable` cleared, manual single-step
switch. All four already converge on one place (`STOP`, `pdp1.c:96`); record
which term fired. *Done, in `dbg.c:classify()`, on the observed 1→0
transition rather than inside `STOP`: once `run` is 0 nothing moves, so
`IR`/`MB` still describe the instruction that stopped the machine.*

**P5. Switch override in `updateswitches`.** Merge after the panel read, in
one shared helper called by both `panel1.c:6` and `panelb18.c:6`. Momentary
keys must stay asserted for exactly one pass of `emu()`, because `Edge()`
compares against the value snapshotted at the top of the loop. *Done:
`dbgoverride()`, called from the last line of both panel files, and
`dbgsvc()` releases the keys at the bottom of the same pass.*

**P6. Decide the fate of the shm patches** (`pidp1-export/patches/0001`,
`0002`). §9 of this spec assumes `/dev/shm/pidp1` exists and is read-only for
clients. If those patches are not upstreamed, say so — the bulk-read path in
`[A2]` then does not exist in a stock build, and `e` becomes the only way to
read core. *Decided: not upstreamed.* `hello` therefore does **not**
advertise `shm`, and `e` is the only way to read core in a stock build.

**P7. Do not disturb the `PDP1` struct layout.** Patch 0001 `memcpy`s `PDP1`
into shared memory and the Python tools hardcode byte offsets (`ac` 16,
`io` 20, `mb` 24, `ma` 28, `pc` 32, `ir` 36, `core` 40, `ta` 262184,
`tw` 262188). Any new field inserted before `core[]` silently breaks every
one of them. Therefore: **debug state lives in its own struct**, reached by a
pointer appended at the *end* of `PDP1`, never inline. Better still, generate
the offsets into a header the tools read. *Done: `void *dbg` is the last
field of `PDP1`, with a comment saying why. Generating the offsets is still
worth doing.*

Two more that are not blockers but make everything easier:

- **Headless test mode.** *Done:* `pdp1 -t` doesn't load or dump `coremem`,
  forces POWER on, mounts no tapes, and additionally makes `pwrclr` **not**
  randomise the flip-flops. That last part turned out to be required, not a
  nicety: a randomised power-on leaves `cyc`/`bc`/`sbm` set, so CONTINUE
  resumes into the middle of an instruction that never started and the
  machine stops on `IR_INCORR` before executing anything.
- **Fix `handlenetcmd`'s buffer overruns** (`main.c:116-124`). *Done by
  deletion:* `netsvc` reads and frames lines, and `handlecmd` is now reached
  only through `dbg.c`.

---

## 1. Transport and framing

TCP, line-oriented US-ASCII, `\n`-terminated. `\r` before `\n` MUST be
accepted and ignored. One command per line.

**Server lines are typed by their first character:**

| char | meaning |
| ---- | ------- |
| `+`  | success — always the final line of a reply |
| `-`  | error — always the final line of a reply |
| `:`  | data line, more follow |
| `!`  | asynchronous event, may appear at any time |

A client reads lines until `+` or `-`, skipping `!`. There is at most **one
outstanding command per connection**; a client MUST NOT send a second command
before the first has been answered. No tags, no pipelining.

Lines longer than 1024 bytes are answered `- ?arg line too long`; the server
MUST discard input through the next `\n` so the connection stays in sync.

## 2. Lexical rules

- Commands and names are case-insensitive; lowercase is canonical.
- Arguments are separated by runs of spaces and/or tabs.
- **Addresses and machine words are octal, always, no prefix.** Counts,
  timeouts, and light-pen radii are **decimal**. Each command below says which.
- Words are printed zero-padded to 6 octal digits, addresses to 6.
- Unknown trailing arguments are an error (`?arg`), never ignored.

## 3. Machine model

Address space is `0..0177777` (`MAXMEM`, 64K words); addresses outside it are
`?addr`. Words are 18 bits.

**Registers.** Readable with `r`, writable with `w` where marked:

| name | bits | w | note |
| ---- | ---- | - | ---- |
| `pc` | 16 | yes | |
| `ac` `io` `mb` | 18 | yes | |
| `ma` | 16 | yes | |
| `ir` | 5 | yes | opcode>>1 |
| `ov` | 1 | yes | OV1 |
| `pf` | 6 | yes | program flags |
| `epc` `ema` | 4 | yes | memory extension |
| `ta` `tw` `ss` `eta` | | **no** | switches — use `sw` |
| `run` `run_enable` `cyc` `df1` `df2` `bc` `hsc` `rim` `sbm` `exd` `ioh` `ioc` `ios` | 1 | no | status |

**Sense switches and program flags are numbered from the left**, as DEC
numbered them: flag or switch *N* is bit `0o40>>(N-1)`, so switch 1 is `040`
and switch 6 is `001` — *not* `1<<N`. `ss=40` means sense switch 1 is up,
and that is what `szs 1` (`640010`) tests. This catches everyone once; see
`decflg()` in `pdp1.c`.

Switch values are deliberately *not* writable through `w`: while the override
is disarmed the panel overwrites them on the next pass of `emu()`, which
would make `w ta` silently do nothing. Use `sw`, which requires `panel on`.

**Status line.** A fixed key order, emitted by `s`, by every command that
completes a stop, and by `!stop`:

```
run=0 cyc=0 df1=0 pc=000012 ac=420016 io=000000 ma=000015 mb=000001 ir=21 ov=0 pf=00 ss=00 at=420016 stop=break
```

`at` is `core[pc]` — the word about to be executed. `stop` is the reason for
the most recent stop (§7), or `none` if the machine has not stopped yet.
`s full` appends `run_enable df2 bc hsc rim sbm exd ioh ioc ios epc ema eta
ta tw`. Clients MUST tolerate unknown keys and MUST NOT depend on key order.

**Conditions.** A fixed, deliberately non-extensible grammar — not an
expression language:

```
<cond> := <reg> ("=" | "!=") <octal>
        | "M[" <addr> "]" ("=" | "!=") <octal>
```

Conditions are evaluated at instruction boundaries only. `run 1000 if
M[cnt]!=5` is the invariant-breaking form: it stops the instant `cnt` stops
being 5.

---

## 4. Connection model

- Listens on **port 1040**, up to **8** concurrent connections. A ninth is
  accepted, answered `- ?busy too many connections`, and closed.
- **This is the same port as the old cli**, not a separate 1044: the debug
  language is a superset of port 1040's, so a debug session never needs a
  second socket and the port count goes down rather than up. Every verb
  `pdp_periph`, the web UI and the agent skills already send (`r <file>`,
  `p <file>`, `l <file>`, `muldiv`, `audio`, `sbs`, `pen`) still works
  verbatim; replies are now framed with a leading `+`/`-`, which those
  clients tolerate (`pdp_periph` greps for `" on\n"`).
- Three verbs had to be arbitrated, because the old cli and this spec both
  wanted them:
  - **`d`** is *deposit*. Connecting a display is `dpy` (or `display`);
    nothing shipped used `d` for that.
  - **`r`** is still the *reader*, because `pdp_periph` depends on it.
    Registers are **`reg`**; as a convenience `r <name>` also reads
    registers when the argument names one, so `r ac` works as written
    everywhere in this document. Bare `r` unmounts the tape; bare `reg`
    lists every register.
  - **`p`** is still the *punch*; `poke` is spelled out in full.
- **Per-connection state:** event subscription, advisory claim, at most one
  pending command. Nothing else. No "currently open location" — that belongs
  to the DDT client.
- **Machine state is global.** Two clients may both drive it; run control is
  last-write-wins. Any stop completes the pending run commands of *every*
  connection.
- **Breakpoints and watchpoints are global**, but record their owner:
  temporary ones (from `until`, `next`) are removed when their connection
  closes. Explicit `b`/`wp` entries persist.
- `claim` is advisory: while one connection holds it, state-changing commands
  from others fail `?busy`. Nothing requires it.
- **On disconnect** the server MUST cancel that connection's pending command,
  drop its temporary breakpoints, and clear any override bits it owns —
  in particular SINST/SSTEP must never be left set by a client that died
  mid-step.

---

## 5. Commands

### Session

| command | reply |
| ------- | ----- |
| `hello` | `+ proto=1 machine=pdp1 maxmem=200000 opts=muldiv,extend,sbs16,symgen` |
| `help [<cmd>]` | `:` one line per command, then `+` |
| `events none\|stop\|all` | `+ events=stop` — default `stop` |
| `claim` / `release` | `+ claim=1` / `+ claim=0` |
| `quit` | `+ bye`, then close |

### Memory

| command | args | reply |
| ------- | ---- | ----- |
| `e <addr> [n]` | addr octal, n decimal (default 1, max 4096) | `:` lines, then `+ <n>` |
| `d <addr> <word>…` | octal | `+ <n>` |
| `poke <addr> <word>…` | octal | `+ <n>` |
| `z [<addr> <n>]` | whole core if bare | `+ <n>` |

`e` data lines carry the start address then up to 8 words:

```
: 000015 000005 000001 000000
```

`d` and `z` are **refused with `?state` while the machine is running.**
Writing core under a running program is a footgun; `poke` is the explicit
escape hatch and never refuses. Neither disturbs any other machine state —
this is the property that makes debugging a live program possible at all, and
it is why these are not the panel DEPOSIT key.

### Registers

| command | reply |
| ------- | ----- |
| `reg [<reg>…]`, `r <reg>…` | `+ pc=000004 ac=000005 …` (all registers if bare `reg`) |
| `w <reg> <val>` | `+ ac=000005` |

`w` is refused with `?state` while running. `w pc <addr>` while halted is the
supported way to set PC; there is no HLT-at-entry / START / restore / EXAMINE
dance.

`w pc` additionally clears `cyc`/`df1`/`df2`/`bc`/`hsc`, because "the next
instruction is at this address" is only meaningful at a fetch boundary — a
machine halted mid-cycle would otherwise resume into the middle of the
instruction it was already executing. This is the part that replaces the
EXAMINE/START dance, and it is why nothing else about the machine has to be
disturbed.

### Run control

| command | args | reply |
| ------- | ---- | ----- |
| `s [full]` | | `+ <status>` |
| `go [<addr>]` | octal; START at addr, else CONTINUE | `+ run=1` |
| `stop` | | `+ <status>` |
| `step [n]` | n decimal, default 1 | `+ <status>` when stopped |
| `cycle [n]` | n decimal memory cycles | `+ <status>` |
| `next [n]` | step, but over `jsp`/`jda`/`cal` | `+ <status>` |
| `run <n> [if <cond>]` | n decimal, max instructions | `+ <status>` |
| `until <addr> [n]` | temporary breakpoint + go | `+ <status>` |
| `trace <n> [changed]` | n decimal | `:` ×n, then `+ <status>` |
| `wait [<ms>]` | ms decimal, default forever | `+ <status>` or `- ?timeout` |
| `readin [<addr>]` | READ-IN key | `+ run=1` |

`go` and `readin` return immediately with `run=1`; the stop arrives as
`!stop`. Everything else that runs the machine is a *pending* command: the
reply is withheld until the machine stops, and carries the status.

`wait` returns immediately if the machine is already stopped.

Every bounded form (`step`, `run`, `until`, `trace`, `next`) MUST stop when
its budget is exhausted, reporting `stop=step`. A program that jumps into the
constant pool must not be able to hang a session.

`trace` emits one line per instruction retired. `pc` is the address of the
instruction *executed*; `inst` is the word fetched from it:

```
: pc=000005 inst=240015 ac=000005 io=000000 ma=000015 mb=000005 ov=0
```

`trace <n> changed` omits keys whose value did not change, always keeping
`pc` and `inst`.

### Debug

| command | args | reply |
| ------- | ---- | ----- |
| `b [<addr> [if <cond>]]` | | set, or list if bare |
| `ub <addr>` \| `ub *` | | `+ <n>` removed |
| `wp <addr> [r\|w\|rw]` | default `w` | `+` |
| `uwp <addr>` \| `uwp *` | | `+ <n>` |
| `back [n]` | n decimal, default 16 | `:` ×n, then `+ <n>` |

Breakpoints are **PC comparisons inside the emulator**. Nothing is written
into core, so nothing needs restoring, and a breakpoint on the first
instruction of a `jsp` subroutine no longer destroys the return address the
way a deposited `0` does. They are tested *before the fetch* — when the next
cycle would be cycle0 (`!cyc && !bc && !hsc`) — so PC still points **at** the
breakpoint address when the machine stops.

Watchpoints fire on core access from `readmem`/`writemem` (`pdp1.c:99-110`)
and stop the machine at the end of the current instruction, not mid-cycle.
PDP-1 core is read-restore, so *every* memory reference — the instruction
fetch included — writes its word back. A write watchpoint therefore fires
only when the word written differs from the word just read, which is exactly
"the program changed this location". `dzm` over an already-zero word is
consequently invisible.

`b` listing and `back` (a ring of the last 64 `jsp`/`jda`/`cal` transfers,
index 0 = most recent):

```
: 000012
: 001165 if ac=000001
+ 2
: 0 to=001165 from=001451 ret=001452
+ 1
```

### Panel (tier 0)

| command | args | notes |
| ------- | ---- | ----- |
| `panel [on\|off [force]]` | | arm/disarm the switch override |
| `key <name> [up]` | `start stop cont exam dep readin feed reader` | one clean edge |
| `sw [<name> [<val>]]` | `ta tw ss sstep sinst extend power` | octal; requires `panel on` |

`key start up` is START-UP (sequence break mode); `key reader up` likewise.
The server synthesises the edge on the emulator thread and holds it for
exactly one pass of `emu()` — no sleeps, no missed edges, no races.

**`panel off` MUST refuse with `?state` if disarming would immediately power
the machine down** — i.e. the override has POWER on, the machine is powered,
and the panel segment has POWER off. `panel off force` overrides the refusal.
This is the single biggest operational trap in the current stack: a client
powers on through the override, disarms, and hands control back to a panel
that still says OFF.

The v1 draft also required mirroring the override's toggles back into the
panel segment. That is **not** implemented and is no longer required: it
cannot work for the physical PiDP-1 (real switches win on the next scan) and
it would put panel bit layouts into `dbg.c`, which the override deliberately
avoids by working on decoded fields. The refusal makes the situation visible,
which was the point. A vpanel-only mirror remains a legitimate local nicety
for `panel1.c`.

### Devices

Long forms of the port-1040 verbs, identical semantics, and the short forms
keep working: `reader`/`r [<file>]`, `punch`/`p [<file>]`,
`load`/`l <file>`, `display`/`dpy [<host> [<port>]]`, `muldiv [on|off]`,
`audio [on|off]`, `sbs [1|16]`, `pen [<n>]`. `pen` and `sbs` take decimal,
as they do today. These are the only verbs that are lenient about their
arguments, because existing clients send things like `muldiv ?`.

---

## 6. Events

Sent to connections per their `events` setting: `none`, `stop` (default:
`!stop` only), `all`.

```
!stop reason=halt run=0 cyc=0 df1=0 pc=000011 ac=000000 … at=760400 stop=halt
!bp addr=000012
!wp addr=000015 old=000005 new=000004 pc=000013
!panel key=start
!panel power=0
```

`!stop` carries a full status line so no follow-up round trip is needed.
`!bp`/`!wp` precede the `!stop` they caused.

A connection that receives a `+` status line for its own pending command
MUST NOT also receive `!stop` for that same stop — the reply already carries
it. Other connections do get the event. Consequently a client that uses `go`
(which returns immediately) with `events none` has no notification at all and
must poll `s` or block in `wait`.

`!panel` reports human activity on the *main* panel segment — key edges and
POWER changes only, never toggle-switch wiggles — and is emitted whether or
not the override is armed. It exists so an agent can notice that a human has
walked up to the machine and yield to them.

**PC after a stop.** `pc` is always the machine's real PC, the one on the
panel lights. PC is incremented at TP2, *before* the instruction executes, so
after a `hlt` at 000011 the status reads `pc=000012`. Only a breakpoint stops
with PC still pointing *at* the instruction, because breakpoints are tested
before the fetch. (The v1 mock rewound PC on a halt to make it point at the
`hlt`; that was wrong and has been fixed — a client that trusted it would
have told the user to `w pc` back onto an instruction that had already run.)

**Stop reasons:** `halt` (real HLT), `illegal` (`IR_INCORR`, an undefined
opcode — previously invisible and the single most valuable new reason),
`break`, `watch`, `cond`, `step` (budget exhausted), `cycle`, `stop` (STOP
key or `stop`), `manual` (a human flipped SINGLE STEP/INST), `power`,
`readin`.

## 7. Errors

`- ?<token> <prose>`. Tokens: `?cmd` unknown command, `?arg` bad or missing
argument, `?addr` address out of range, `?reg` unknown or unwritable
register, `?state` not allowed in the current machine state, `?busy` another
connection holds the claim / too many connections, `?limit` count exceeds a
server limit, `?timeout` `wait` expired, `?file` device file could not be
opened.

## 8. Notes for implementers

- Two command shapes: *immediate* (`e d r w s b sw panel …`) complete inside
  one pass of `emu()`; *pending* (`step cycle run until next trace wait`) set
  a goal and are answered when the loop reaches it. A step cannot complete in
  the iteration that starts it — this is the part not to hand-wave.
- Stop via `run_enable = 0`, exactly as the STOP key does (`main.c:62`), so
  lights and machine semantics stay consistent for free.
- Service the debug connections next to `cli(pdp)` (`main.c:106`).
- **Opcode 0 is not HLT.** Real HLT is `760400`. Depositing `0` halts via the
  `IR_INCORR` path with an inconsistent PC and a mid-cycle signature. Worth a
  line in `help`, since the convention is widespread in existing tooling.
- Reading core through `/dev/shm/pidp1` stays supported and is invisible to
  the machine, which makes it ideal for watching a program while it runs.
  **Client writes to that segment are not supported** — they race `emu()`.
  All writes go through `d`/`poke`/`w`.

## 9. Worked example

```
$ telnet localhost 1040
> hello
+ proto=1 machine=pdp1 maxmem=200000 opts=muldiv,extend,sbs16,symgen
> load /opt/pidp1-dev/tapes/demo.rim
+ loaded start=000004
> w pc 4
+ pc=000004
> s
+ run=0 cyc=0 df1=0 pc=000004 ac=000000 … at=700005 stop=none
> trace 3
: pc=000004 inst=700005 ac=000005 io=000000 ma=000004 mb=700005 ov=0
: pc=000005 inst=240015 ac=000005 io=000000 ma=000015 mb=000005 ov=0
: pc=000006 inst=200015 ac=000005 io=000000 ma=000015 mb=000005 ov=0
+ run=0 … pc=000007 at=640100 stop=step
> b 12
+ 1
> go
+ run=1
!bp addr=000012
!stop reason=break run=0 … pc=000012 at=420016 stop=break
> e 15 1
: 000015 000005
+ 1
> run 1000 if M[15]!=5
+ run=0 … pc=000014 at=600006 stop=cond
> ub *
+ 1
```

## 10. Conformance tests

`test/pdp1dbg_test.py` is the executable form of this spec: stdlib-only,
`--host`/`--port`, works against any implementation. It is the definition of
"done" for the emulator side. **30 pass, 0 fail, 1 skip** against both the
emulator and the mock as of this writing; the skip is `panel_power_mirror`,
which needs a real panel driver and was checked by hand instead (`panel off`
refuses, `panel off force` powers the machine down).

`test/pdp1dbg_mock.py` is a reference server — the protocol over a small
instruction-level PDP-1 (not a TP-level emulator; a stand-in so the suite is
runnable and the rx-0 client can be built before the emulator work lands).
Where mock and emulator disagree, the emulator wins and the mock is wrong.

```
make -C pdp1 && pdp1/pdp1 -t &
python3 pdp1/test/pdp1dbg_test.py --port 1040

python3 pdp1/test/pdp1dbg_mock.py --port 1044 &     # the reference server
python3 pdp1/test/pdp1dbg_test.py --port 1044
```

`-t` is what makes this unattended: no `coremem` load or dump, POWER forced
on, no tapes mounted, and a deterministic power-on state.
