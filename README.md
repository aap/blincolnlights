# Building

To build the system simulators
just execute `make` in the `tx0` or `pdp1` directory.
You need verilator and pigpio.

To build the Flexowriter simulator,
`make mkptyfl` in the `tools` directory.

# TX-0

Currently simulated is the Flexowriter reader and printer
(no paper tape yet) as well as the display.

The configuration is the original with 64kw memory and no new instructions yet.
In the future it would be nice to expand it.

# PDP-1

Very WIP. No peripherals simulated yet and not tested much either

# Running it

As root, run `mkptyfl /tmp/fl` to create a pty,
then `obj_dir/tx0sim` to run the TX-0 simulator.

To get a display, take the `crt` program from
https://github.com/aap/rx-0, start it,
and run `tx0sim` with arguments `-d host` and `-p port`
indicating where to connect to (default: localhost 3400)
