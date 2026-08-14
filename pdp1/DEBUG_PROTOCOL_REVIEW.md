# DEBUG_PROTOCOL — review from Hermes (aap's agent), 13 Aug 2026

Review of `DEBUG_PROTOCOL.md` (draft 0) after a full live session driving
the patched stack (/opt/pidp1-dev, Hermes control channel). Written for
Claude Opus to pick up alongside the draft.

Detailed answers to the draft's open questions **[Q1]-[Q8]** are in
**section 11 of `DEBUG_PROTOCOL.md`** — this file is the short verdict,
plus three things the draft should add.

## Verdict

Strong design. The diagnosis of the current stack's four limitations
(§9) is confirmed 1:1 by session experience — all four were hit in
practice within one afternoon:

1. **PC setup** — the "EXAMINE at entry−1 → PC=entry" dance is wrong on
   blincolnlights: EXAMINE sets PC to the examined address *exactly*
   (SP1 `clr_pc`, SP2 `PC |= ta`). `w pc <addr>` while halted is the
   right answer; delete the old dance from the tooling.
2. **Deposited-HLT breakpoints** — depositing 0 at the *first*
   instruction of a `jsp` subroutine zeroes AC, so `dap airet` fixes the
   return to 0 → `jmp 0` → crash loop through the constant pool →
   corrupted core (had to reload the RIM twice). PC-compare breakpoints
   are the fix, not a nice-to-have. (The deposit-0 convention *did* work
   when the target was not a subroutine entry.)
3. **Lock/unlock disturbance** — the vpanel POWER gotcha is real and
   survives any protocol that doesn't address it: main panel shmem said
   POWER OFF while the Hermes segment said ON; machine powered down on
   unlock. See "Add 1" below.
4. **Edge timing** — 10–15 ms sleeps mostly worked; one mid-session
   garbage run was never fully pinned down (likely undefined-opcode
   fallout). Server-side one-pass edge synthesis removes the class.

Also confirmed as right: `e`/`d` non-destructive (a deposit-0 restore
dance through the panel key path scrambled state; `d` would not have),
`at=` in status, stop reasons incl. `illegal`, bounded runs as the
crash-loop anti-footgun, mixed-radix rule (octal words / decimal counts),
single-threaded `emu()` constraint, and the `serveN`-blocks-accept
problem (DEBUG_NOTES §2c) that the emulator-thread-reads design fixes.

## Three things the draft should add

### 1. `panel off` must mirror POWER into the main panel shmem
The vpanel power-down-on-unlock gotcha (main `/tmp/pdp1_panel` sw0
POWER bit vs. the override segment) survives the protocol as drafted: a
client that powers on via the override and then disarms (`panel off`)
hands control back to a vpanel whose POWER still says OFF, and the
machine dies. Either mirror POWER (and ideally the other toggle
switches) into the main panel shmem on disarm, or document loudly that
the operator must set POWER on the physical/virtual panel. This was the
#1 confusion of the live session ("power still off").

### 2. Document that opcode 0 is not a clean HLT
The `deposit 0 = HLT` convention halted the machine but inconsistently:
in one case PC advanced past the deposited word before stopping, in
another it did not (mid-cycle signature). Real HLT is `760400`. A one-
line note ("0 is not HLT; hlt is 760400") in `help` or the doc would
save the next reader. PC-compare breakpoints make it moot for the
protocol itself.

### 3. State that shmem becomes read-only for clients
`/dev/shm/pidp1` is worth keeping as a bulk read path (64K core scans
were constant in the session; ASCII `e` for 26k words would be
miserable), but the current tooling's `set_pc`/`write_mem` shmem *write*
paths are racy against `emu()`. Once `w pc` and `d` exist, delete the
client write paths and document the segment as read-only — writes only
through the protocol, on the emulator thread.

## Highest-value pieces to build first (from session experience)

1. PC-compare breakpoints (`b`/`until`) — removes the clobbering class.
2. `w pc` / `d` / `s` with `at=` — one round trip for "where am I".
3. `run`/`until` with bounds — the crash-loop anti-footgun.
4. `!stop` with `reason=illegal` — names the unexplained stop.
5. Key/POWER human-panel events — matches the operating etiquette
   ("when the human is at the machine, stop and let them drive").
