# Blincolnlights

The simulators are meant to be used with the
[Blincolnlights 18](https://hackaday.io/project/191985-blincolnlights-18)
panel.

Currently there is a TX-0 and two PDP-1 simulators.

# Building

To build the system simulators
just execute `make` in the `tx0`, `pdp1` or `pdp1c` directory.
You need verilator and pigpio.

To build the Flexowriter simulator,
`make mkptyfl` in the `tools` directory.

To build the FIO-DEC Model B simulator,
`make mkptyfio` in the `tools` directory.

# TX-0

Currently simulated is the Flexowriter reader and printer
(no paper tape yet) as well as the display.

The configuration is the original with 64kw memory and no new instructions yet.
In the future it would be nice to expand it.

# PDP-1

There are two simulations.
The `pdp1` directory has a verilator simulation that is rather slow,
the `pdp1c` directory has a C simulation that is fast but less complete.
The verilator simulation has simulation for
the display, reader, punch and typewriter.
The C simulation only has the display and reader so far.

# Running it

## TX-0
As root, run `mkptyfl /tmp/fl` to create a pty,
then `obj_dir/tx0sim` to run the TX-0 simulator.

## PDP-1
As root, run `mkptyfio /tmp/ty` to create a pty,
then `obj_dir/pdp1sim` in the `pdp1` directory
or `./pdp1` in the `pdp1c` directory to run the PDP-1 simulator.

To get a display, take the `crt` program from
https://github.com/aap/rx-0, start it,
and run `tx0sim` with arguments `-h host` and `-p port`
indicating where to connect to (default: localhost 3400)
