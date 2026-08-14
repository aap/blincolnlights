#!/usr/bin/env python3
"""Conformance tests for the PDP-1 debug protocol (DEBUG_PROTOCOL_SPEC.md v1).

The executable form of the spec.  Runs against any implementation:

    python3 pdp1dbg_test.py [--host localhost] [--port 1044] [-v]

Exit 0 = all passed.  Tests that the mock cannot cover (they need the real
panel segment) are reported as SKIP with the reason; those are the ones that
must be run against the emulator by hand.
"""

import argparse
import socket
import sys
import time

# countdown demo, as in the pdp1-ai-debug skill.  entry 0o4.
DEMO = {
    0o04: 0o700005,   # law 5
    0o05: 0o240015,   # dac cnt
    0o06: 0o200015,   # loop, lac cnt
    0o07: 0o640100,   # sza
    0o10: 0o600012,   # jmp dec
    0o11: 0o760400,   # hlt
    0o12: 0o420016,   # dec, sub one
    0o13: 0o240015,   # dac cnt
    0o14: 0o600006,   # jmp loop
    0o15: 0o000000,   # cnt
    0o16: 0o000001,   # one
}
# a jsp subroutine, for `next` and `back`.  entry 0o20.
SUB = {
    0o20: 0o620024,   # jsp 24
    0o21: 0o760400,   # hlt
    0o24: 0o260026,   # dap 26
    0o25: 0o600026,   # jmp 26
    0o26: 0o600000,   # jmp .  (patched by the dap to jmp 21)
}


class Fail(Exception):
    pass


class Skip(Exception):
    pass


class Client:
    def __init__(self, host, port, verbose=False):
        self.s = socket.create_connection((host, port), timeout=10)
        self.f = self.s.makefile("rwb")
        self.events = []
        self.verbose = verbose

    def send(self, line):
        if self.verbose:
            print("   > %s" % line)
        self.f.write((line + "\n").encode())
        self.f.flush()

    def readline(self):
        l = self.f.readline()
        if not l:
            raise Fail("connection closed")
        l = l.decode().rstrip("\r\n")
        if self.verbose:
            print("   < %s" % l)
        return l

    def cmd(self, line):
        """Send a command, return (ok, payload, datalines).  Skips events."""
        self.send(line)
        data = []
        while True:
            l = self.readline()
            if l.startswith("!"):
                self.events.append(l)
            elif l.startswith(":"):
                data.append(l[1:].strip())
            elif l[:1] in ("+", "-"):
                return l[0] == "+", l[1:].strip(), data
            else:
                raise Fail("untyped line from server: %r" % l)

    def must(self, line):
        ok, payload, data = self.cmd(line)
        if not ok:
            raise Fail("%r failed: %s" % (line, payload))
        return payload, data

    def mustfail(self, line, token):
        ok, payload, _ = self.cmd(line)
        if ok:
            raise Fail("%r should have failed with %s, got: + %s"
                       % (line, token, payload))
        if not payload.startswith(token):
            raise Fail("%r: expected %s, got: %s" % (line, token, payload))
        return payload

    def kv(self, line="s"):
        payload, _ = self.must(line)
        out = {}
        for f in payload.split():
            k, _, v = f.partition("=")
            out[k] = v
        return out

    def waitevent(self, prefix, timeout=5):
        end = time.time() + timeout
        while time.time() < end:
            for e in self.events:
                if e.startswith(prefix):
                    return e
            self.s.settimeout(max(0.05, end - time.time()))
            try:
                self.events.append(self.readline())
            except (socket.timeout, TimeoutError):
                pass
            finally:
                self.s.settimeout(10)
        raise Fail("no %s event within %ds" % (prefix, timeout))

    def load(self, prog):
        for a, w in sorted(prog.items()):
            self.must("d %o %o" % (a, w))

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


# --- tests -------------------------------------------------------------

TESTS = []


def test(fn):
    TESTS.append(fn)
    return fn


def setup(c):
    """Stopped machine with the demo loaded and PC at the entry."""
    c.cmd("stop")
    c.must("ub *")
    c.must("uwp *")
    c.load(DEMO)
    c.must("w pc 4")
    c.must("w ac 0")


@test
def t_hello(c):
    """hello announces proto=1 and the address space"""
    k = c.kv("hello")
    if k.get("proto") != "1":
        raise Fail("proto=%s, want 1" % k.get("proto"))
    if "maxmem" not in k or "machine" not in k:
        raise Fail("hello lacks machine/maxmem: %s" % k)


@test
def t_unknown_command(c):
    """unknown commands are ?cmd, not silence"""
    c.mustfail("frobnicate", "?cmd")


@test
def t_framing_and_data_lines(c):
    """e emits ': <addr> <words...>', at most 8 words per line"""
    setup(c)
    payload, data = c.must("e 4 11")
    if payload != "11":
        raise Fail("e returned %r, want the decimal count '11'" % payload)
    if len(data) != 2:
        raise Fail("9 words should be 2 data lines (8+1), got %d" % len(data))
    first = data[0].split()
    if first[0] != "000004" or len(first) != 9:
        raise Fail("bad data line: %r" % data[0])
    if first[1] != "700005":
        raise Fail("words are not 6-digit octal: %r" % first[1])


@test
def t_examine_deposit_roundtrip(c):
    """d then e round-trips a word, without disturbing the machine"""
    setup(c)
    before = c.kv()
    c.must("d 1000 123456")
    _, data = c.must("e 1000")
    if data[0].split()[1] != "123456":
        raise Fail("deposit/examine mismatch: %r" % data)
    after = c.kv()
    for k in ("pc", "ac", "io", "ir", "ov"):
        if before[k] != after[k]:
            raise Fail("e/d disturbed %s: %s -> %s" % (k, before[k], after[k]))


@test
def t_address_range(c):
    """addresses outside core are ?addr"""
    c.mustfail("e 200000", "?addr")
    c.mustfail("d 200000 1", "?addr")


@test
def t_octal_only(c):
    """words are octal; 8 and 9 are not digits"""
    c.mustfail("d 100 99", "?arg")


@test
def t_registers(c):
    """r reads, w writes, w pc needs no HLT/START/EXAMINE dance"""
    setup(c)
    c.must("w ac 777777")
    if c.kv("r ac")["ac"] != "777777":
        raise Fail("w ac did not take")
    c.must("w pc 12")
    if c.kv()["pc"] != "000012":
        raise Fail("w pc did not take")
    c.mustfail("w nosuchreg 1", "?reg")


@test
def t_switches_are_not_registers(c):
    """ta/tw/ss are switches: readable via r, not writable via w"""
    setup(c)
    c.must("r ta")
    c.mustfail("w ta 100", "?reg")


@test
def t_status_keys(c):
    """s carries the mandatory key set; s full adds more"""
    setup(c)
    k = c.kv()
    for want in ("run", "cyc", "df1", "pc", "ac", "io", "ma", "mb", "ir",
                 "ov", "pf", "ss", "at", "stop"):
        if want not in k:
            raise Fail("status lacks %s: %s" % (want, k))
    if k["at"] != "700005":
        raise Fail("at= should be core[pc]=700005, got %s" % k["at"])
    full = c.kv("s full")
    for want in ("run_enable", "df2", "bc", "hsc", "rim", "sbm", "exd", "ta", "tw"):
        if want not in full:
            raise Fail("s full lacks %s" % want)


@test
def t_step(c):
    """step executes exactly one instruction and reports stop=step"""
    setup(c)
    k = c.kv("step")
    if k["pc"] != "000005" or k["ac"] != "000005":
        raise Fail("after 'law 5': pc=%s ac=%s" % (k["pc"], k["ac"]))
    if k["stop"] != "step" or k["run"] != "0":
        raise Fail("stop=%s run=%s, want step/0" % (k["stop"], k["run"]))
    k = c.kv("step 3")
    if k["pc"] != "000010":
        raise Fail("3 more steps should reach 000010, got %s" % k["pc"])


@test
def t_trace(c):
    """trace emits one line per instruction, pc = the executed address"""
    setup(c)
    payload, data = c.must("trace 3")
    if len(data) != 3:
        raise Fail("trace 3 gave %d lines" % len(data))
    f = dict(x.split("=", 1) for x in data[0].split())
    if f["pc"] != "000004" or f["inst"] != "700005":
        raise Fail("first trace line: %r" % data[0])
    if f["ac"] != "000005":
        raise Fail("trace should show the post-instruction ac: %r" % data[0])
    if "stop=step" not in payload:
        raise Fail("trace should end stopped by budget: %s" % payload)
    _, data = c.must("trace 2 changed")
    if any("io=" in l for l in data):
        raise Fail("'changed' should omit unchanged registers: %r" % data)


@test
def t_halt(c):
    """running into hlt (760400) reports reason=halt"""
    setup(c)
    k = c.kv("run 100")
    if k["stop"] != "halt":
        raise Fail("stop=%s, want halt" % k["stop"])
    # PC is incremented before the instruction executes, so after the hlt
    # at 0o11 it points at 0o12 -- what the panel lights show.
    if k["pc"] != "000012":
        raise Fail("should halt past the hlt, pc=%s want 000012" % k["pc"])


@test
def t_illegal(c):
    """an undefined opcode reports reason=illegal, not a silent stop"""
    setup(c)
    c.must("d 4 0")            # opcode 0 is NOT hlt
    c.must("w pc 4")
    k = c.kv("run 10")
    if k["stop"] != "illegal":
        raise Fail("stop=%s, want illegal" % k["stop"])


@test
def t_run_budget(c):
    """a bounded run always terminates: the crash-loop anti-footgun"""
    setup(c)
    c.must("d 4 600004")       # jmp . — infinite loop
    c.must("w pc 4")
    k = c.kv("run 50")
    if k["stop"] != "step":
        raise Fail("stop=%s, want step (budget exhausted)" % k["stop"])
    if k["run"] != "0":
        raise Fail("machine still running after budget")


@test
def t_run_condition(c):
    """run <n> if <cond> stops the instant the condition holds"""
    setup(c)
    k = c.kv("run 100 if M[15]=000004")
    if k["stop"] != "cond":
        raise Fail("stop=%s, want cond" % k["stop"])
    _, data = c.must("e 15")
    if data[0].split()[1] != "000004":
        raise Fail("stopped with cnt=%s, want 000004" % data[0])
    setup(c)
    k = c.kv("run 100 if M[15]!=000000")
    if k["stop"] != "cond":
        raise Fail("!= form: stop=%s, want cond" % k["stop"])


@test
def t_breakpoint(c):
    """breakpoints stop AT the address and leave core untouched"""
    setup(c)
    before, _ = c.must("e 12")
    c.must("b 12")
    k = c.kv("run 100")
    if k["stop"] != "break":
        raise Fail("stop=%s, want break" % k["stop"])
    if k["pc"] != "000012":
        raise Fail("pc=%s, want 000012 (stop before the fetch)" % k["pc"])
    after, _ = c.must("e 12")
    if before != after:
        raise Fail("breakpoint modified core: %s -> %s" % (before, after))
    if c.kv()["at"] != "420016":
        raise Fail("instruction at the breakpoint was clobbered")
    payload, data = c.must("b")
    if payload != "1" or "000012" not in data[0]:
        raise Fail("b listing: %r %r" % (payload, data))
    c.must("ub 12")
    if c.must("b")[0] != "0":
        raise Fail("ub did not remove the breakpoint")


@test
def t_breakpoint_no_ac_clobber(c):
    """a breakpoint on a jsp entry must not destroy the return address

    The deposited-HLT convention zeroes AC here, so `dap ret` fixes the
    return to 0 and the subroutine crashes into the constant pool."""
    setup(c)
    c.load(SUB)
    c.must("d 26 600000")
    c.must("w pc 20")
    c.must("b 24")
    k = c.kv("run 100")
    if k["stop"] != "break" or k["pc"] != "000024":
        raise Fail("did not break at the subroutine entry: %s" % k)
    if k["ac"] != "000021":
        raise Fail("AC should still hold the return address 000021, got %s" % k["ac"])
    c.must("ub *")
    k = c.kv("run 100")
    if k["stop"] != "halt" or k["pc"] != "000022":
        raise Fail("subroutine did not return cleanly: %s" % k)


@test
def t_conditional_breakpoint(c):
    """b <addr> if <cond> only fires when the condition holds"""
    setup(c)
    c.must("b 12 if M[15]=000002")
    k = c.kv("run 200")
    if k["stop"] != "break":
        raise Fail("stop=%s, want break" % k["stop"])
    _, data = c.must("e 15")
    if data[0].split()[1] != "000002":
        raise Fail("fired with cnt=%s, want 000002" % data[0].split()[1])
    c.must("ub *")


@test
def t_until(c):
    """until sets a temporary breakpoint and removes it again"""
    setup(c)
    k = c.kv("until 12")
    if k["pc"] != "000012" or k["stop"] != "break":
        raise Fail("until: %s" % k)
    if c.must("b")[0] != "0":
        raise Fail("until left its temporary breakpoint behind")


@test
def t_next_steps_over(c):
    """next runs a jsp subroutine to completion"""
    setup(c)
    c.load(SUB)
    c.must("d 26 600000")
    c.must("w pc 20")
    k = c.kv("next")
    if k["pc"] != "000021":
        raise Fail("next over jsp landed at %s, want 000021" % k["pc"])


@test
def t_back(c):
    """back reports the call ring"""
    setup(c)
    c.load(SUB)
    c.must("d 26 600000")
    c.must("w pc 20")
    c.must("step")
    payload, data = c.must("back")
    if payload == "0":
        raise Fail("back is empty after a jsp")
    f = dict(x.split("=", 1) for x in data[0].split()[1:])
    if f.get("to") != "000024" or f.get("ret") != "000021":
        raise Fail("call ring entry: %r" % data[0])


@test
def t_watchpoint(c):
    """a write watchpoint stops the machine and reports old/new"""
    setup(c)
    c.must("wp 15 w")
    c.must("events all")
    k = c.kv("run 100")
    if k["stop"] != "watch":
        raise Fail("stop=%s, want watch" % k["stop"])
    e = [x for x in c.events if x.startswith("!wp")]
    if not e:
        raise Fail("no !wp event")
    f = dict(x.split("=", 1) for x in e[0].split()[1:])
    if f.get("addr") != "000015" or f.get("new") != "000005":
        raise Fail("!wp fields: %s" % e[0])
    c.must("uwp *")
    c.must("events stop")


@test
def t_deposit_while_running(c):
    """d refuses while the machine runs; poke is the explicit escape hatch"""
    setup(c)
    c.must("d 4 600004")       # jmp . — keeps running
    c.must("w pc 4")
    c.must("go")
    try:
        c.mustfail("d 1000 1", "?state")
        c.mustfail("w ac 1", "?state")
        c.must("poke 1000 1")
    finally:
        c.must("stop")


@test
def t_go_and_wait(c):
    """go returns at once; wait blocks until the stop"""
    setup(c)
    payload, _ = c.must("go")
    if "run=1" not in payload:
        raise Fail("go should reply run=1, got %r" % payload)
    k = c.kv("wait 5000")
    if k["stop"] != "halt":
        raise Fail("wait returned stop=%s, want halt" % k["stop"])
    k = c.kv("wait 100")
    if k["stop"] != "halt":
        raise Fail("wait on a stopped machine should return immediately")


@test
def t_wait_timeout(c):
    """wait on a running machine times out rather than hanging"""
    setup(c)
    c.must("d 4 600004")
    c.must("w pc 4")
    c.must("go")
    try:
        c.mustfail("wait 200", "?timeout")
    finally:
        c.must("stop")


@test
def t_stop_event(c):
    """!stop is delivered, carries a full status, and honours 'events none'"""
    setup(c)
    c.must("events stop")
    other = Client(c.host, c.port)
    try:
        other.must("events stop")
        c.must("go")
        e = other.waitevent("!stop")
        f = dict(x.split("=", 1) for x in e.split()[1:])
        if f.get("reason") != "halt":
            raise Fail("!stop reason=%s, want halt" % f.get("reason"))
        for want in ("pc", "ac", "at"):
            if want not in f:
                raise Fail("!stop lacks %s: %s" % (want, e))
        other.must("events none")
        other.events = []
        setup(c)
        c.must("go")
        c.kv("wait 5000")
        time.sleep(0.2)
        other.send("s")
        while True:
            l = other.readline()
            if l.startswith("+"):
                break
            if l.startswith("!"):
                raise Fail("event delivered despite 'events none': %s" % l)
    finally:
        other.close()


@test
def t_long_line_resync(c):
    """an over-long line errors but does not desync the connection"""
    c.send("d 100 " + "1" * 2000)
    while True:
        l = c.readline()
        if l[:1] in ("+", "-"):
            break
    if not l.startswith("-"):
        raise Fail("over-long line should be an error")
    k = c.kv("hello")
    if k.get("proto") != "1":
        raise Fail("connection desynced after an over-long line")


@test
def t_claim(c):
    """claim is advisory and excludes other connections"""
    setup(c)
    other = Client(c.host, c.port)
    try:
        c.must("claim")
        other.mustfail("d 100 1", "?busy")
        other.must("s")             # reads stay allowed
        c.must("release")
        other.must("d 100 1")
    finally:
        other.close()


@test
def t_zero_core(c):
    """z clears core"""
    setup(c)
    c.must("d 1000 123456")
    c.must("z 1000 1")
    _, data = c.must("e 1000")
    if data[0].split()[1] != "000000":
        raise Fail("z did not clear: %r" % data)


@test
def t_switch_override_gate(c):
    """sw requires the override to be armed"""
    setup(c)
    c.must("panel off")
    c.mustfail("sw ta 100", "?state")
    c.must("panel on")
    c.must("sw ta 100")
    if c.kv("r ta")["ta"] != "000100":
        raise Fail("sw ta did not take")
    c.must("panel off")


@test
def t_switch_tw_reaches_the_program(c):
    """sw tw reaches the running program: lat loads the test word switches"""
    setup(c)
    c.must("panel on")
    try:
        c.must("sw tw 123456")
        c.load({0o4: 0o762000,          # lat -- ac |= tw
                0o5: 0o760400})         # hlt
        c.must("w pc 4")
        c.must("w ac 0")
        k = c.kv("run 10")
        if k["stop"] != "halt":
            raise Fail("stop=%s, want halt" % k["stop"])
        if k["ac"] != "123456":
            raise Fail("lat loaded ac=%s, want the test word 123456" % k["ac"])
    finally:
        c.must("panel off")


@test
def t_switch_ss_reaches_the_program(c):
    """sw ss reaches the program, in DEC's reversed flag numbering

    Sense switch N is bit 0o40>>(N-1), NOT 1<<N: switch 1 is 0o40.  Anyone
    writing a sense-switch test gets this wrong once."""
    prog = {
        0o04: 0o640010,   # szs 1 -- skip if sense switch 1 is zero
        0o05: 0o600010,   # jmp 10   (ss1 was up)
        0o06: 0o700001,   # law 1    (ss1 was down)
        0o07: 0o760400,   # hlt
        0o10: 0o700002,   # law 2
        0o11: 0o760400,   # hlt
    }
    setup(c)
    c.must("panel on")
    try:
        for sw, want in (("40", "000002"), ("00", "000001")):
            c.load(prog)
            c.must("sw ss " + sw)
            c.must("w pc 4")
            c.must("w ac 0")
            k = c.kv("run 20")
            if k["stop"] != "halt":
                raise Fail("ss=%s: stop=%s, want halt" % (sw, k["stop"]))
            if k["ss"] != sw.zfill(2):
                raise Fail("ss=%s did not reach the status line: %s" % (sw, k["ss"]))
            if k["ac"] != want:
                raise Fail("ss=%s: szs 1 gave ac=%s, want %s "
                           "(flag N is 0o40>>(N-1), not 1<<N)"
                           % (sw, k["ac"], want))
    finally:
        c.must("panel off")


@test
def t_panel_power_mirror(c):
    """panel off must not strand the machine with POWER off (spec §5)

    Needs the real /tmp/pdp1_panel segment and a panel driver; not
    modelled by the mock.  Run by hand against the emulator:
      panel on / sw power 200000 / go / panel off   -> must not power down,
      and must refuse with ?state if the panel segment still says off."""
    raise Skip("needs the real panel segment and a panel driver")


# --- runner ------------------------------------------------------------

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=1044)
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("-k", help="only run tests whose name contains this")
    a = p.parse_args()

    npass = nfail = nskip = 0
    for fn in TESTS:
        name = fn.__name__[2:]
        if a.k and a.k not in name:
            continue
        c = Client(a.host, a.port, a.verbose)
        c.host, c.port = a.host, a.port
        try:
            fn(c)
            npass += 1
            print("ok   %-28s %s" % (name, (fn.__doc__ or "").split("\n")[0]))
        except Skip as e:
            nskip += 1
            print("SKIP %-28s %s" % (name, e))
        except (Fail, OSError) as e:
            nfail += 1
            print("FAIL %-28s %s" % (name, e))
        finally:
            c.close()

    print("\n%d passed, %d failed, %d skipped" % (npass, nfail, nskip))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
