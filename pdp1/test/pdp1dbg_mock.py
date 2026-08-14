#!/usr/bin/env python3
"""Reference server for the PDP-1 debug protocol (DEBUG_PROTOCOL_SPEC.md v1).

This is a MOCK, not an emulator. It implements the protocol over a small
instruction-level PDP-1 so that the conformance suite is runnable and clients
(rx-0's backend, agent tooling) can be developed before the emulator work
lands. It is not cycle- or TP-accurate, has no devices, and mul/div are
no-ops. Where the mock and the emulator disagree, the emulator is right.

    python3 pdp1dbg_mock.py [--port 1044]
"""

import argparse
import socketserver
import threading
import time

MAXMEM = 1 << 16
WORDMASK = 0o777777
ADDRMASK = 0o7777
B0, B5 = 0o400000, 0o010000
HLT = 0o760400

# --- machine -----------------------------------------------------------

def decflg(n):
    return {1: 0o40, 2: 0o20, 3: 0o10, 4: 0o04, 5: 0o02, 6: 0o01, 7: 0o77}.get(n & 7, 0)


def add18(a, b):
    s = a + b
    if s > WORDMASK:
        s = (s + 1) & WORDMASK
    if s == WORDMASK:
        s = 0
    return s


class Stop(Exception):
    def __init__(self, reason):
        self.reason = reason


class Machine:
    REGS_RW = ("pc", "ac", "io", "mb", "ma", "ir", "ov", "pf", "epc", "ema")
    REGS_RO = ("run", "run_enable", "cyc", "df1", "df2", "bc", "hsc", "rim",
               "sbm", "exd", "ioh", "ioc", "ios", "ta", "tw", "ss", "eta")

    def __init__(self):
        self.lock = threading.RLock()
        self.cv = threading.Condition(self.lock)
        self.core = [0] * MAXMEM
        for r in self.REGS_RW + self.REGS_RO:
            setattr(self, r, 0)
        self.ioc = 1
        self.stop = "none"
        self.bp = {}          # addr -> (cond|None, owner|None, temporary)
        self.wp = {}          # addr -> mode
        self.calls = []       # ring of (to, frm, ret)
        self.panel = False    # override armed
        self.claim = None
        self.budget = -1
        self.cond = None
        self.hit = None       # pending !bp / !wp description

    # --- core access, watchpoint aware
    def rd(self, a):
        a %= MAXMEM
        if "r" in self.wp.get(a, ""):
            self.hit = ("wp", a, self.core[a], self.core[a], self.pc)
        return self.core[a]

    def wr(self, a, w):
        a %= MAXMEM
        w &= WORDMASK
        if "w" in self.wp.get(a, ""):
            self.hit = ("wp", a, self.core[a], w, self.pc)
        self.core[a] = w

    # --- one instruction
    def step_one(self):
        self.hit = None
        pc = self.pc
        mb = self.rd(pc)
        self.ma = pc
        self.mb = mb
        self.pc = (pc + 1) & ADDRMASK | (pc & ~ADDRMASK)
        ir = mb >> 13
        self.ir = ir
        y = mb & ADDRMASK
        ind = mb & B5

        # indirect addressing (not for skip/shift/law/opr/iot/cal-jda)
        if ind and ir not in (0o32, 0o33, 0o34, 0o35, 0o37, 0o07):
            for _ in range(8):
                w = self.rd(y)
                y = w & ADDRMASK
                if not w & B5:
                    break

        if ir in (0, 0o05, 0o06, 0o17, 0o36):
            raise Stop("illegal")

        ac, io = self.ac, self.io
        if ir == 0o01:   self.ac = ac & self.rd(y)
        elif ir == 0o02: self.ac = ac | self.rd(y)
        elif ir == 0o03: self.ac = ac ^ self.rd(y)
        elif ir == 0o04:                                   # xct
            self.pc = y
            return self.step_one()
        elif ir == 0o07:                                   # cal / jda
            t = y if ind else 0o100
            self.wr(t, ac)
            self.ac = self.pc
            self.calls.insert(0, (t + 1, pc, self.pc))
            del self.calls[64:]
            self.pc = (t + 1) & ADDRMASK
        elif ir == 0o10: self.ac = self.rd(y)
        elif ir == 0o11: self.io = self.rd(y)
        elif ir == 0o12: self.wr(y, ac)
        elif ir == 0o13: self.wr(y, self.rd(y) & 0o770000 | ac & ADDRMASK)
        elif ir == 0o14: self.wr(y, self.rd(y) & ADDRMASK | ac & 0o770000)
        elif ir == 0o15: self.wr(y, io)
        elif ir == 0o16: self.wr(y, 0)
        elif ir == 0o20: self.ac = add18(ac, self.rd(y))
        elif ir == 0o21: self.ac = add18(ac, self.rd(y) ^ WORDMASK)
        elif ir in (0o22, 0o23):                           # idx / isp
            v = add18(self.rd(y), 1)
            self.ac = v
            self.wr(y, v)
            if ir == 0o23 and not v & B0:
                self.pc = (self.pc + 1) & ADDRMASK
        elif ir == 0o24:                                   # sad
            if ac != self.rd(y):
                self.pc = (self.pc + 1) & ADDRMASK
        elif ir == 0o25:                                   # sas
            if ac == self.rd(y):
                self.pc = (self.pc + 1) & ADDRMASK
        elif ir in (0o26, 0o27):
            pass                                           # mus/dis: not modelled
        elif ir == 0o30: self.pc = y
        elif ir == 0o31:                                   # jsp
            self.ac = self.pc
            self.calls.insert(0, (y, pc, self.pc))
            del self.calls[64:]
            self.pc = y
        elif ir == 0o32:                                   # skip
            s = False
            if mb & 0o002000 and not io & B0: s = True
            if mb & 0o001000 and not self.ov: s = True
            if mb & 0o000400 and ac & B0: s = True
            if mb & 0o000200 and not ac & B0: s = True
            if mb & 0o000100 and ac == 0: s = True
            if mb & 0o070 and not self.ss & decflg(mb >> 3): s = True
            if mb & 0o007 and not self.pf & decflg(mb): s = True
            if mb & B5: s = not s
            if mb & 0o001000: self.ov = 0
            if s:
                self.pc = (self.pc + 1) & ADDRMASK
        elif ir == 0o33: self.shro(mb)
        elif ir == 0o34:                                   # law
            self.ac = (y ^ WORDMASK) if ind else y
        elif ir == 0o35: pass                              # iot: no devices
        elif ir == 0o37:                                   # opr
            if mb & 0o000200: self.ac = 0                  # cla
            if mb & 0o004000: self.io = 0                  # cli
            if mb & 0o002000: self.ac |= self.tw           # lat
            if mb & 0o000100: self.ac |= self.pc           # lap
            if mb & 0o007:
                if mb & 0o000010:
                    self.pf |= decflg(mb)
                else:
                    self.pf &= ~decflg(mb)
            if mb & 0o001000: self.ac ^= WORDMASK          # cma
            if mb & 0o000400: raise Stop("halt")           # hlt
        self.ac &= WORDMASK
        self.io &= WORDMASK
        if self.hit:
            raise Stop("watch")

    def shro(self, mb):
        ac, io, n = self.ac, self.io, mb & 0o777
        k = bin(n).count("1")
        op = (mb >> 9) & 0o17
        for _ in range(k):
            if op == 0o01:   ac = (ac << 1 | ac >> 17) & WORDMASK
            elif op == 0o02: io = (io << 1 | io >> 17) & WORDMASK
            elif op == 0o03:
                ac, io = (ac << 1 | io >> 17) & WORDMASK, (io << 1 | ac >> 17) & WORDMASK
            elif op == 0o05: ac = (ac & B0) | (ac << 1 & 0o377777)
            elif op == 0o06: io = (io & B0) | (io << 1 & 0o377777)
            elif op == 0o11: ac = (ac & 1) << 17 | ac >> 1
            elif op == 0o12: io = (io & 1) << 17 | io >> 1
            elif op == 0o13:
                ac, io = (io & 1) << 17 | ac >> 1, (ac & 1) << 17 | io >> 1
            elif op == 0o15: ac = (ac & B0) | ac >> 1
            elif op == 0o16: io = (io & B0) | io >> 1
            elif op == 0o17:
                ac, io = (ac & B0) | ac >> 1, (ac & 1) << 17 | io >> 1
        self.ac, self.io = ac & WORDMASK, io & WORDMASK

    # --- conditions
    def evalcond(self, c):
        kind, key, neg, val = c
        cur = self.core[key % MAXMEM] if kind == "m" else getattr(self, key)
        return (cur != val) if neg else (cur == val)

    # --- the run engine.  Returns a stop reason.
    def execute(self, budget=-1, cond=None, tracecb=None):
        """Run instructions until something stops us.  Called without the
        lock held; grabs it in slices so `stop` from another connection and
        the free-run thread can interleave."""
        n = 0
        while True:
            with self.lock:
                if not self.run:
                    return self.stop
                for _ in range(200):
                    if not self.run:
                        return self.stop
                    if self.bpcheck():
                        return self.halt("break")
                    prev = (self.ac, self.io, self.ma, self.mb, self.ov)
                    pc = self.pc
                    try:
                        self.step_one()
                    except Stop as s:
                        # PC was incremented before the instruction
                        # executed (TP2), so after a hlt it points at the
                        # word *after* it, exactly as the panel lights and
                        # the emulator show it.
                        return self.halt(s.reason)
                    if tracecb:
                        tracecb(pc, prev)
                    n += 1
                    if cond and self.evalcond(cond):
                        return self.halt("cond")
                    if budget >= 0 and n >= budget:
                        return self.halt("step")
            time.sleep(0)

    def bpcheck(self):
        e = self.bp.get(self.pc)
        if e is None:
            return False
        cond = e[0]
        if cond and not self.evalcond(cond):
            return False
        self.hit = ("bp", self.pc)
        return True

    def halt(self, reason):
        self.run = 0
        self.run_enable = 0
        self.stop = reason
        with self.cv:
            self.cv.notify_all()
        return reason

    # --- reporting
    def status(self, full=False):
        k = [("run", self.run), ("cyc", self.cyc), ("df1", self.df1),
             ("pc", o(self.pc)), ("ac", o(self.ac)), ("io", o(self.io)),
             ("ma", o(self.ma)), ("mb", o(self.mb)), ("ir", "%02o" % self.ir),
             ("ov", self.ov), ("pf", "%02o" % self.pf), ("ss", "%02o" % self.ss),
             ("at", o(self.core[self.pc % MAXMEM])), ("stop", self.stop)]
        if full:
            k += [(r, o(getattr(self, r))) for r in
                  ("run_enable", "df2", "bc", "hsc", "rim", "sbm", "exd",
                   "ioh", "ioc", "ios", "epc", "ema", "eta", "ta", "tw")]
        return " ".join("%s=%s" % (a, b) for a, b in k)


def o(v):
    return "%06o" % (v & WORDMASK)


# --- protocol ----------------------------------------------------------

KEYS = ("start", "stop", "cont", "exam", "dep", "readin", "feed", "reader")
SWITCHES = ("ta", "tw", "ss", "sstep", "sinst", "extend", "power")


class Err(Exception):
    def __init__(self, token, msg):
        self.token, self.msg = token, msg


def octal(s, lim=WORDMASK, what="value"):
    try:
        v = int(s, 8)
    except ValueError:
        raise Err("?arg", "not an octal number: %s" % s)
    if not 0 <= v <= lim:
        raise Err("?addr" if lim == MAXMEM - 1 else "?arg", "%s out of range" % what)
    return v


def dec(s, lo=0, hi=1 << 30):
    try:
        v = int(s, 10)
    except ValueError:
        raise Err("?arg", "not a decimal number: %s" % s)
    if not lo <= v <= hi:
        raise Err("?limit", "count out of range")
    return v


def parsecond(args):
    """<reg>=<octal> | M[<addr>]=<octal>, also with !="""
    s = " ".join(args)
    neg = "!=" in s
    if "=" not in s:
        raise Err("?arg", "malformed condition")
    lhs, rhs = s.split("!=" if neg else "=", 1)
    lhs, rhs = lhs.strip().lower(), rhs.strip()
    val = octal(rhs)
    if lhs.startswith("m[") and lhs.endswith("]"):
        return ("m", octal(lhs[2:-1], MAXMEM - 1, "address"), neg, val)
    if lhs in Machine.REGS_RW or lhs in Machine.REGS_RO:
        return ("r", lhs, neg, val)
    raise Err("?reg", "unknown register: %s" % lhs)


class Conn(socketserver.StreamRequestHandler):
    def setup(self):
        super().setup()
        self.events = "stop"
        self.wlock = threading.Lock()
        self.pending = False
        self.srv.add(self)

    def finish(self):
        self.srv.remove(self)
        m = self.srv.m
        with m.lock:
            for a, e in list(m.bp.items()):
                if e[1] is self and e[2]:
                    del m.bp[a]
            if m.claim is self:
                m.claim = None
        super().finish()

    # --- output helpers
    def send(self, line):
        with self.wlock:
            try:
                self.wfile.write((line + "\n").encode())
                self.wfile.flush()
            except OSError:
                pass

    def event(self, line):
        if self.events == "all" or (self.events == "stop" and line.startswith("!stop")):
            self.send(line)

    def handle(self):
        m = self.srv.m
        while True:
            line = self.rfile.readline(4096)
            if not line:
                return
            if len(line) > 1024 and not line.endswith(b"\n"):
                while True:                       # resync on the next newline
                    c = self.rfile.readline(4096)
                    if not c or c.endswith(b"\n"):
                        break
                self.send("- ?arg line too long")
                continue
            args = line.decode("ascii", "replace").strip().split()
            if not args:
                continue
            cmd, args = args[0].lower(), args[1:]
            fn = getattr(self, "c_" + cmd, None)
            try:
                if fn is None:
                    raise Err("?cmd", "unknown command: %s" % cmd)
                if cmd not in ("s", "r", "e", "hello", "help", "events",
                               "claim", "release", "quit", "back", "b", "wait"):
                    if m.claim is not None and m.claim is not self:
                        raise Err("?busy", "another connection holds the claim")
                r = fn(args)
            except Err as ex:
                self.send("- %s %s" % (ex.token, ex.msg))
                continue
            self.send("+ " + r if r else "+")
            if cmd == "quit":
                return

    # --- session
    def c_hello(self, a):
        return ("proto=1 machine=pdp1 maxmem=%o opts=muldiv,extend,sbs16,symgen"
                % MAXMEM)

    def c_help(self, a):
        for n in sorted(x[2:] for x in dir(self) if x.startswith("c_")):
            self.send(": " + n)
        self.send(": note: opcode 0 is not HLT; hlt is 760400")
        return ""

    def c_events(self, a):
        if a:
            if a[0] not in ("none", "stop", "all"):
                raise Err("?arg", "events none|stop|all")
            self.events = a[0]
        return "events=" + self.events

    def c_claim(self, a):
        m = self.srv.m
        with m.lock:
            if m.claim not in (None, self):
                raise Err("?busy", "another connection holds the claim")
            m.claim = self
        return "claim=1"

    def c_release(self, a):
        m = self.srv.m
        with m.lock:
            if m.claim is self:
                m.claim = None
        return "claim=0"

    def c_quit(self, a):
        return "bye"

    # --- memory
    def c_e(self, a):
        if not a:
            raise Err("?arg", "e <addr> [n]")
        m = self.srv.m
        ad = octal(a[0], MAXMEM - 1, "address")
        n = dec(a[1], 1, 4096) if len(a) > 1 else 1
        if len(a) > 2:
            raise Err("?arg", "too many arguments")
        if ad + n > MAXMEM:
            raise Err("?addr", "address out of range")
        with m.lock:
            for i in range(0, n, 8):
                w = m.core[ad + i:ad + min(i + 8, n)]
                self.send(": %06o %s" % (ad + i, " ".join(o(x) for x in w)))
        return str(n)

    def deposit(self, a, force):
        if len(a) < 2:
            raise Err("?arg", "d <addr> <word>...")
        m = self.srv.m
        ad = octal(a[0], MAXMEM - 1, "address")
        ws = [octal(x) for x in a[1:]]
        if ad + len(ws) > MAXMEM:
            raise Err("?addr", "address out of range")
        with m.lock:
            if m.run and not force:
                raise Err("?state", "machine is running; stop first or use poke")
            m.core[ad:ad + len(ws)] = ws
        return str(len(ws))

    def c_d(self, a):
        return self.deposit(a, False)

    def c_poke(self, a):
        return self.deposit(a, True)

    def c_z(self, a):
        m = self.srv.m
        ad = octal(a[0], MAXMEM - 1, "address") if a else 0
        n = dec(a[1]) if len(a) > 1 else MAXMEM - ad
        with m.lock:
            if m.run:
                raise Err("?state", "machine is running; stop first")
            n = min(n, MAXMEM - ad)
            m.core[ad:ad + n] = [0] * n
        return str(n)

    # --- registers
    def c_r(self, a):
        m = self.srv.m
        names = [x.lower() for x in a] or list(Machine.REGS_RW) + ["ta", "tw", "ss"]
        with m.lock:
            out = []
            for n in names:
                if n not in Machine.REGS_RW and n not in Machine.REGS_RO:
                    raise Err("?reg", "unknown register: %s" % n)
                out.append("%s=%s" % (n, o(getattr(m, n))))
        return " ".join(out)

    def c_w(self, a):
        if len(a) != 2:
            raise Err("?arg", "w <reg> <val>")
        m = self.srv.m
        n, v = a[0].lower(), octal(a[1])
        if n in ("ta", "tw", "ss", "eta"):
            raise Err("?reg", "%s is a switch; use sw (needs panel on)" % n)
        if n not in Machine.REGS_RW:
            raise Err("?reg", "unknown or read-only register: %s" % n)
        with m.lock:
            if m.run:
                raise Err("?state", "machine is running; stop first")
            setattr(m, n, v & (ADDRMASK if n in ("pc", "ma") else WORDMASK))
        return "%s=%s" % (n, o(getattr(m, n)))

    # --- run control
    def c_s(self, a):
        if a and a[0] != "full":
            raise Err("?arg", "s [full]")
        with self.srv.m.lock:
            return self.srv.m.status(bool(a))

    def start(self, addr=None):
        m = self.srv.m
        with m.lock:
            if addr is not None:
                m.pc = addr
            m.run = m.run_enable = 1
            m.stop = "none"

    def c_go(self, a):
        ad = octal(a[0], MAXMEM - 1, "address") if a else None
        self.start(ad)
        self.srv.freerun()
        return "run=1"

    def c_readin(self, a):
        return self.c_go(a)

    def c_stop(self, a):
        m = self.srv.m
        with m.lock:
            if m.run:
                m.run = 0
                m.stop = "stop"
                with m.cv:
                    m.cv.notify_all()
            return m.status()

    def bounded(self, budget=-1, cond=None, temp=None, tracecb=None):
        m = self.srv.m
        if temp is not None:
            with m.lock:
                m.bp.setdefault(temp, (None, self, True))
        self.start()
        try:
            m.execute(budget, cond, tracecb)
        finally:
            if temp is not None:
                with m.lock:
                    e = m.bp.get(temp)
                    if e and e[2] and e[1] is self:
                        del m.bp[temp]
        with m.lock:
            self.srv.notify(m, self)
            return m.status()

    def c_step(self, a):
        return self.bounded(dec(a[0], 1) if a else 1)

    def c_cycle(self, a):
        return self.c_step(a)          # mock has no sub-instruction cycles

    def c_run(self, a):
        if not a:
            raise Err("?arg", "run <n> [if <cond>]")
        n = dec(a[0], 0)
        cond = None
        if len(a) > 1:
            if a[1].lower() != "if":
                raise Err("?arg", "run <n> [if <cond>]")
            cond = parsecond(a[2:])
        return self.bounded(n, cond)

    def c_until(self, a):
        if not a:
            raise Err("?arg", "until <addr> [n]")
        ad = octal(a[0], MAXMEM - 1, "address")
        n = dec(a[1], 1) if len(a) > 1 else -1
        return self.bounded(n, None, ad)

    def c_next(self, a):
        m = self.srv.m
        n = dec(a[0], 1) if a else 1
        for _ in range(n):
            with m.lock:
                w = m.core[m.pc % MAXMEM]
                ir, ret = w >> 13, (m.pc + 1) & ADDRMASK
            r = self.bounded(-1, None, ret) if ir in (0o31, 0o07) else self.bounded(1)
            with m.lock:
                if m.stop not in ("step", "break"):
                    break
        return r

    def c_trace(self, a):
        if not a:
            raise Err("?arg", "trace <n> [changed]")
        n = dec(a[0], 1, 100000)
        changed = len(a) > 1 and a[1].lower() == "changed"
        m = self.srv.m
        lines = []

        def cb(pc, prev):
            cur = (m.ac, m.io, m.ma, m.mb, m.ov)
            f = [("pc", o(pc)), ("inst", o(m.core[pc % MAXMEM]))]
            for i, k in enumerate(("ac", "io", "ma", "mb", "ov")):
                if not changed or cur[i] != prev[i]:
                    f.append((k, o(cur[i]) if k != "ov" else str(cur[i])))
            lines.append(": " + " ".join("%s=%s" % x for x in f))

        st = self.bounded(n, None, None, cb)
        for l in lines:
            self.send(l)
        return st

    def c_wait(self, a):
        m = self.srv.m
        ms = dec(a[0], 0) if a else None
        with m.cv:
            if m.run:
                m.cv.wait(ms / 1000.0 if ms is not None else None)
            if m.run:
                raise Err("?timeout", "still running")
            return m.status()

    # --- debug
    def c_b(self, a):
        m = self.srv.m
        with m.lock:
            if not a:
                for ad, e in sorted(m.bp.items()):
                    self.send(": %06o%s" % (ad, " if ..." if e[0] else ""))
                return str(len(m.bp))
            ad = octal(a[0], MAXMEM - 1, "address")
            cond = None
            if len(a) > 1:
                if a[1].lower() != "if":
                    raise Err("?arg", "b <addr> [if <cond>]")
                cond = parsecond(a[2:])
            m.bp[ad] = (cond, self, False)
            return str(len(m.bp))

    def c_ub(self, a):
        m = self.srv.m
        with m.lock:
            if a and a[0] == "*":
                n = len(m.bp)
                m.bp.clear()
                return str(n)
            if not a:
                raise Err("?arg", "ub <addr>|*")
            ad = octal(a[0], MAXMEM - 1, "address")
            return str(1 if m.bp.pop(ad, None) is not None else 0)

    def c_wp(self, a):
        if not a:
            raise Err("?arg", "wp <addr> [r|w|rw]")
        m = self.srv.m
        ad = octal(a[0], MAXMEM - 1, "address")
        mode = a[1].lower() if len(a) > 1 else "w"
        if mode not in ("r", "w", "rw"):
            raise Err("?arg", "mode must be r, w or rw")
        with m.lock:
            m.wp[ad] = mode
        return ""

    def c_uwp(self, a):
        m = self.srv.m
        with m.lock:
            if a and a[0] == "*":
                n = len(m.wp)
                m.wp.clear()
                return str(n)
            if not a:
                raise Err("?arg", "uwp <addr>|*")
            ad = octal(a[0], MAXMEM - 1, "address")
            return str(1 if m.wp.pop(ad, None) is not None else 0)

    def c_back(self, a):
        m = self.srv.m
        n = dec(a[0], 1) if a else 16
        with m.lock:
            c = m.calls[:n]
            for i, (to, frm, ret) in enumerate(c):
                self.send(": %d to=%06o from=%06o ret=%06o" % (i, to, frm, ret))
            return str(len(c))

    # --- panel
    def c_panel(self, a):
        m = self.srv.m
        if not a:
            return "panel=%d" % m.panel
        if a[0] == "on":
            m.panel = True
        elif a[0] == "off":
            m.panel = False       # NB: mock does not model the POWER mirror
        else:
            raise Err("?arg", "panel [on|off [force]]")
        return "panel=%d" % m.panel

    def c_sw(self, a):
        m = self.srv.m
        if not a:
            return " ".join("%s=%s" % (s, o(getattr(m, s, 0))) for s in SWITCHES)
        n = a[0].lower()
        if n not in SWITCHES:
            raise Err("?arg", "unknown switch: %s" % n)
        if len(a) > 1:
            if not m.panel:
                raise Err("?state", "switch override not armed; panel on first")
            setattr(m, n, octal(a[1]))
        return "%s=%s" % (n, o(getattr(m, n, 0)))

    def c_key(self, a):
        if not a or a[0].lower() not in KEYS:
            raise Err("?arg", "key <%s> [up]" % "|".join(KEYS))
        m = self.srv.m
        if not m.panel:
            raise Err("?state", "switch override not armed; panel on first")
        k = a[0].lower()
        if k == "start":
            self.start(m.ta)
            self.srv.freerun()
        elif k == "stop":
            self.c_stop([])
        elif k == "cont":
            self.start()
            self.srv.freerun()
        return ""

    # --- devices (stubs; the mock has no peripherals)
    def c_load(self, a):
        if not a:
            raise Err("?arg", "load <file>")
        m = self.srv.m
        try:
            data = open(a[0], "rb").read()
        except OSError as e:
            raise Err("?file", str(e))
        # RIM: triples of punched frames -> 18-bit words
        frames = [b & 0o77 for b in data if b & 0o200]
        words = [frames[i] << 12 | frames[i + 1] << 6 | frames[i + 2]
                 for i in range(0, len(frames) - 2, 3)]
        start, i = None, 0
        with m.lock:
            while i + 1 < len(words):
                if words[i] & 0o760000 == 0o320000:
                    m.core[words[i] & ADDRMASK] = words[i + 1]
                    i += 2
                elif words[i] & 0o760000 == 0o600000:
                    start = words[i] & ADDRMASK
                    break
                else:
                    raise Err("?file", "rim botch at word %d" % i)
        return "loaded" + (" start=%06o" % start if start is not None else "")

    def c_reader(self, a): return ""
    def c_punch(self, a): return ""
    def c_display(self, a): return ""
    def c_muldiv(self, a): return "muldiv on"
    def c_audio(self, a): return "audio off"
    def c_sbs(self, a): return "sbs 16"
    def c_pen(self, a): return "pen 5"


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, addr):
        self.m = Machine()
        self.conns = []
        self.clock = threading.Lock()
        self.runner = None
        handler = type("H", (Conn,), {"srv": self})
        super().__init__(addr, handler)

    def add(self, c):
        with self.clock:
            self.conns.append(c)

    def remove(self, c):
        with self.clock:
            if c in self.conns:
                self.conns.remove(c)

    def notify(self, m, origin=None):
        """Emit !bp/!wp then !stop.  The connection whose pending command is
        being answered does not also get !stop (spec §6)."""
        pre = []
        if m.hit and m.hit[0] == "bp":
            pre.append("!bp addr=%06o" % m.hit[1])
        elif m.hit and m.hit[0] == "wp":
            pre.append("!wp addr=%06o old=%s new=%s pc=%s"
                       % (m.hit[1], o(m.hit[2]), o(m.hit[3]), o(m.hit[4])))
        m.hit = None
        with self.clock:
            for c in list(self.conns):
                for p in pre:
                    c.event(p)
                if c is not origin:
                    c.event("!stop reason=%s %s" % (m.stop, m.status()))

    def freerun(self):
        """Background execution for `go` (unbounded)."""
        m = self.m

        def th():
            m.execute(-1, None, None)
            with m.lock:
                self.notify(m)

        if self.runner is None or not self.runner.is_alive():
            self.runner = threading.Thread(target=th, daemon=True)
            self.runner.start()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=1044)
    p.add_argument("--host", default="127.0.0.1")
    a = p.parse_args()
    s = Server((a.host, a.port))
    print("pdp1dbg mock listening on %s:%d" % (a.host, a.port), flush=True)
    s.serve_forever()


if __name__ == "__main__":
    main()
