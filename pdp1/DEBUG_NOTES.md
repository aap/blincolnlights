# PDP-1 debug interface — survey notes

Notes from reading `blincolnlights/pdp1` and `rx-0` before adding a
network debug interface to the emulator (drivable by the rx-0 DDT
frontend, and by scripts/agents).

## 1. What's where

### Emulator (`blincolnlights/pdp1`)

| file | what |
| ---- | ---- |
| `main.c` | entry point, arg parsing, net thread, **the main loop** `emu()` |
| `pdp1.c` | the CPU at TP (time pulse) level, IOTs/devices, display, `cli`/`handlecmd` |
| `pdp1.h` | `struct PDP1` — the complete machine state, one flat struct |
| `panel1.c` | PiDP-1 panel glue: `updateswitches` / `updatelights` / `getpanel` |
| `panelb18.c` | same for the Blincolnlights 18 panel |
| `typtelnet.c` | telnet server + FIO-DEC/ASCII translation for the typewriter |
| `audio.c` | AC-bit-driven audio |
| `../common.c` | sockets (`dial`, `serve1`, `serveN`), mmap segments, time, `split` |
| `../pollfd.c` | poll thread + `FD`/`waitfd`/`closefd`; how fds get watched without blocking |

No disassembler exists anywhere in this repo (`macro_disasm` is a core
dump, not code). That stays on the client side, where rx-0 already has one.

### The main loop — `main.c:26` `emu()`

Per iteration, in order:

1. snapshot the momentary keys (`prev_*_sw`)
2. `updateswitches(pdp, panel)` — panel mmap → `pdp` (`main.c:54`)
3. edges: START/CONT/EXAM/DEP → `spec()` + `cycle()`; STOP → `run_enable=0`;
   READIN → `start_readin()` (`main.c:57-63`)
4. RIM loader state machine (`main.c:65-75`)
5. `if(run) cycle(); else updatelights();`
6. `throttle()` → `handleio()` → `simtime += 5000` (5 µs per iteration)
7. `agedisplay()` ×2, then `cli()` (stdin, polled every 10000 iterations)

Everything the CPU and the devices do happens on this one thread, with no
locking anywhere. That's the single most important fact for the design below.

### Panel plumbing

- `struct Panel` in `panel_pidp1.h`, shared by mmap of `/tmp/pdp1_panel`
  (`panel1.c:129`). Driver processes: `panel_pidp1` (real hardware),
  `vpanel_pdp1` (SDL).
- `panel1.c:6 updateswitches` copies `sw0..sw3` → `ta/tw/ss/power/sstep/
  sinst/`keys`/spacewar` **every iteration**. This is where the override
  hooks in.
- Edge detection lives in `emu()`, not in the panel driver — so a
  synthesized key press only has to be visible for one iteration.
- `KEY_READER` / `KEY_READER_UP` (`panel_pidp1.h:17`) are produced by
  `vpanel_pdp1/main.c:265` but no `updateswitches` consumes them.

### Ports (`main.c:174`, `serveN`)

1040 cli · 1041 typewriter telnet · 1042 ptr · 1043 ptp · 3400/3401 display.

### Existing cli — `pdp1.c:2005` `cli()`, `pdp1.c:2026` `handlecmd()`

`r`/`p`/`l`/`d`/`sbs`/`pen`/`muldiv`/`audio`/`help`. Same handler serves
stdin and port 1040. `pdp_periph` uses it connect-command-read-close, one
connection per command (`pdp_periph/main.c:123 emucmd`).

### rx-0 side

- `common/fe.c` — the DDT frontend: raw terminal, symbol table, expression
  assembler, typeout modes, open/close/deposit logic. Machine independent.
- `common/fe.h:51-79` — **the entire backend contract**. That list is the
  spec for the network protocol.
- `pdp1/mach.h` — `REG_*` enum, `ADDRMASK`/`WORDMASK`/`MAXMEM`.
- `pdp1/mach.c` — a *second, older* PDP-1 emulator (no IOTs, no devices)
  plus `disasm()` plus a command channel to a sim thread
  (`mach.c:671 handlemsg`). The CPU half is what a net client replaces;
  `disasm` and `syms.inc` stay.
- `pdp1/syms.inc` — opcode symbols, all flagged `SYM_HALFKILL` so they parse
  on input but are never used for typeout; plus `pc/ac/io/ss/pf` as register
  names for the `reg◊/` opener.
- Declared but never implemented for pdp1: `cpu_readin`, `cpu_exec`,
  `cpu_ioreset`, `cpu_printflags` (fe.c never calls them). `fe.h` also
  declares `devtab`, `coloncmd`, `docmd`, `fe_svc`, `initcrt`, `initnetmem`,
  `helpstr` — remnants of a fuller frontend, nothing defines them.

## 2. Things to get right in the debug interface

**a) Two classes of operation — don't conflate them.**

*Non-destructive*: examine/deposit core, read/write registers. These must
touch `pdp->core[]` / `pdp->ac` etc. directly. Going through the panel is
destructive: `spec()` (`pdp1.c:393`) calls `sc()` (`pdp1.c:362`) on
START/EXAM/DEP, which clears PC, IR, OV, IOC/IOS/IOH, sequence break state,
and *then* does `PC |= ta`. A DDT `100/` implemented with the EXAMINE key
would smash the machine you're debugging.

*Panel operations*: start/continue/stop/single step/readin. These should go
through the switch path so lights, RIM mode, sequence break, and the
single-instruction logic all behave authentically.

rx-0's `mach.c` already splits it exactly this way (direct `core[]` for
examine/deposit, `spec()` for run/step) — good precedent to copy.

**b) Threading.** `handlecmd` currently runs on the net thread and mutates
emulator state — `r_fd`/`p_fd`, and via `readrim` (`pdp1.c:1977`) it zeroes
all 64K of core — while the emulator thread is inside `cycle()`. It works by
luck and short connections. Don't extend that pattern. Either
(i) queue commands on the net thread and execute them in `emu()` next to
`cli()`, or (ii) hand the accepted fd to the existing `pollfd` mechanism and
read it from the emulator thread, exactly like `typ_fd`. (ii) fits the
codebase best; only `accept()` stays on the net thread.

**c) `serveN` blocks.** `common.c:149-188` calls `ports[i].handle(confd,…)`
from inside the accept loop, so a handler that loops — `handlenetcmd`,
`main.c:111` — stops the process accepting *anything else*. Today this is
masked because pdp_periph opens a connection per command. A DDT session is
long-lived, so while it's attached you'd be unable to connect a display or a
tape. This must be fixed together with the debug port; approach (b-ii) fixes
both at once.

**d) Reply framing.** Port 1040 has none: `handlenetcmd` appends a `\n`
(`main.c:120-125`) and the client does one `read()`. A stateful client needs
exactly one reply per command with an unambiguous end — e.g. a `+`/`-`
status prefix and a lone `.` line to end multi-line output.

**e) Step semantics.** rx-0's `cpu_nextinst` pulses CONTINUE with
`single_inst_sw` and then busy-polls until `run==0`. Over a socket that's a
lot of round trips. Let the server do it: `step` replies only once the
machine has stopped, with the new state attached. Same for cycle-stepping
with `single_cyc_sw`. The real single-instruction logic already exists —
`MANUAL_RUN` / `INST_DONE` (`pdp1.c:87-96`); use it rather than a new path.

**f) One-shot status.** `fe.c:492` polls `isrunning()`/`isstopped()` every
1 ms while `started`, then makes four more backend calls to print the stop
banner. A single `status` reply carrying run/run_enable/PC/AC/IO/MA/MB/IR/
OV/flags **and** `core[PC]` collapses that to one round trip. Worth also
pushing an unsolicited line when the machine stops on its own (HLT) so an
agent needn't poll — but keep polling working, since the DDT frontend is
synchronous.

**g) Addressing.** `pdp1.h:9` `MAXMEM` is 64K with the type 15 memory
extension (`exd/emc/ema/epc`); `readmem`/`writemem` (`pdp1.c:99-110`) use
`(ema|MA)%MAXMEM`. rx-0's `pdp1/mach.h` still says 4K / `ADDRMASK 07777`.
The protocol should take a plain 16-bit address; if we want to debug
extended memory the client's `Addr`/`ADDRMASK` need widening (`tx0/mach.h`
already runs fe.c at 64K, so the frontend copes).

**h) Registers to expose.** PC AC IO MA MB IR TA TW SS PF, plus EPC/EMA/ETA,
plus the status bits run/run_enable/cyc/df1/df2/bc/hsc/ov1/rim/sbm/exd/
ioh/ioc/ios. `REG_*` in `rx-0/pdp1/mach.h` needs extending and the names
adding to `syms.inc` with flags=1 so they don't pollute typeout.

**i) Breakpoints** don't exist in the frontend at all (no `$B`). The
emulator side is easy if wanted: check PC against a small table in `emu()`
when the next cycle would be a fetch (`!cyc && !bc && !hsc`) and clear
`run_enable` the way the STOP key does. The frontend needs new commands.

**j) Text, not binary.** The backend contract is a handful of operations, and
a line-based protocol means a human with `nc` — or an agent — can drive the
machine with no client at all. Bulk transfer can still be `dump start count`
emitting octal words. `pdp6/lua.c:249-262` already registers essentially this
API (`e`, `d`, `setpc`, `readmemory`, `ptrmount`, `ptrunmount`, `ptpmount`,
`ptpunmount`, `discon`) — reusing those names keeps the two machines
consistent.

## 3. Override panel (later, but so it's written down)

- Put it at the end of `updateswitches` (`panel1.c:6`): read the real panel
  first, then merge the override.
- Overridable: `ta`, `tw`, `ss`, `single_cyc_sw`, `single_inst_sw`, power,
  and the momentary keys.
- Momentary keys must stay asserted for exactly one pass of `emu()`, since
  `Edge()` compares against the value captured at the top of the loop.
  Cleanest: the debug side sets `ovr.keys`, and `emu()` clears them after the
  `updateswitches` that consumed them.
- Decide precedence explicitly. The first bug you'll hit is the physical TA
  switches overwriting whatever DDT just set, one iteration later. Probably:
  OR the momentary keys, *replace* ta/tw/ss while the override is armed.
- `panelb18.c` needs the same treatment — worth factoring the merge into one
  shared function both `updateswitches` implementations call.

*Landed as `dbgoverride()`, called from the end of both `updateswitches`
implementations, exactly as sketched.* Two things this list did not
anticipate, both of which only show up once somebody stands at the panel
while somebody else drives:

- **The override has to be visible.** TW and SS are readable by the running
  program (`lat`, `szs`), so an invisible override makes the machine
  inexplicable to the operator. All six program flag lamps light while
  either is held. The lamps are borrowed, not `pdp->pf` — the machine's real
  flags are untouched.
- **The panel has to be able to take itself back.** The tape reader key
  drives nothing on the PiDP-1, so either position of it drops the override
  and everything it holds. That is the real answer to "decide precedence
  explicitly": the override wins while it is armed, and the human wins
  whenever they want to. It cannot be a network command — anything
  reachable from 1040 is no escape hatch, since `panel off force` is already
  there.

## 4. Low hanging fruit

1. **`main.c:116-124` `handlenetcmd` overruns two buffers.** `line[n] = 0`
   with `n` up to `sizeof(line)` is a 1-byte stack overflow on a full 1024-byte
   command; `r[n]='\n'; r[n+1]='\0'` writes up to 2 bytes past the end of
   `handlecmd`'s static `resp[1024]`. Bound both.
2. **`serveN` handles connections inline** (`common.c:179`) — see 2(c).
3. **`handlecmd`'s static `resp[1024]` is shared** between the stdin cli
   (emulator thread) and the net thread, and mutates `pdp` from the net
   thread. Interleaved replies and racy state changes.
4. **Help text bugs** (`pdp1.c:2136-2137`): the `muldiv` and `audio` lines
   are missing `\n` so they run together; `sbs` isn't documented at all;
   `pen [1-6]` is wrong — `pdp1.c:2119-2121` clamps to 3..16.
5. **`-h`/`-p` are parsed and ignored** (`main.c:272-286`; the compiler says
   so). Either wire them to an outgoing display connection like the `d`
   command does, or drop them from `usage()` and `README.md:135-141`.
6. **Hardcoded startup config** (`main.c:322-327`): tape
   `tapes/dpys5.rim`, `muldiv_sw = 1`, punch file `punch.out`. Command-line
   options would help scripted runs a lot.
7. **`dumpmem` ignores its `file` argument** (`main.c:226-247`, always writes
   `./coremem`), and `if(1)` at `main.c:238` — the intended `if(a != i)` is
   commented out — emits an address line before *every* word, doubling the
   file size.
8. **`readmem("coremem")` at startup** (`main.c:304`) silently carries state
   across runs, which is surprising while debugging. At least a flag to skip it.
   *(`-t` now skips both the load and the dump.)*
8a. **`/tmp/pdp1_panel` carries across runs too**, and it is the same class of
   surprise. The segment outlives the emulator, so whatever the switches were
   when it last exited — or whatever a test wrote into them — is what the next
   run reads. This bites the `panel off` POWER refusal in particular: the
   refusal only fires when the segment genuinely says POWER off, so a segment
   left with POWER=1 by earlier testing makes `panel off` *correctly* not
   refuse, which looks exactly like the check not working. Zero the segment
   (or check `sw power`) before concluding anything about that path.
9. **Stray debug printf** at `pdp1.c:1911` ("char missed") — unconditional,
   in the typewriter path, and unindented. Put it behind a debug flag.
10. Trivia: unused `p` at `pdp1.c:2013`; multi-line-comment warning at
    `typtelnet.c:84`. Otherwise the build is warning-clean apart from
    deliberate `&&`-in-`||` style.
11. **`KEY_READER`/`KEY_READER_UP` go nowhere** — see §1.
12. **Stale copies.** `pdp1_lua/` is a full copy of the emulator (`pdp1.c`
    byte-identical, `main.c` older) with an unfinished 25-line `lua.c`;
    `pdp1/pdp1.c.dev` likewise. Worth resolving before the hackathon adds a
    third copy — `pdp6/lua.c` is the version of that idea that actually works.
13. **`maindec/` is a free regression suite.** The MAINDEC diagnostics plus
    `maindec/run` are already here; with a debug port and scriptable tape
    mounting, "load, start, run, check the halt address" becomes an automated
    test. Probably the highest-value item after the debug port itself.
14. **Document the 1040 command language** in `README.md` — it lists the
    ports but not what to say to them.
