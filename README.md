# Blincolnlights

The simulators are meant to be used with the
[Blincolnlights 18](https://hackaday.io/project/191985-blincolnlights-18)
panel.
The TX-0 and PDP-1 simulators also support the upcoming PiDP-1 panel.

Currently there are Whirlwind, TX-0 and PDP-1 emulators.

# Building

To build the system simulators
just execute `make` in the `whirlwind`, `tx0` or `pdp1` directory.

To build the Flexowriter simulator,
`make mkptyfl` in the `tools` directory.

To build the FIO-DEC Model B simulator,
`make mkptyfio` in the `tools` directory.

# Whirlwind

Simulated peripherals are only a display.

## Run
Run `./whirlwind` in the `whirlwind` directory.

# TX-0

Simulated peripherals are the display, paper tape reader, and Flexowriter (typewriter and punch).
The configuration is the original with 64kw memory and no new instructions yet.
In the future it would be nice to expand it.

## Run
Run `mkptyfl /tmp/fl` to create a pty,
then `./tx0` in the `tx0` directory to run the TX-0 simulator (`./tx0_pidp1`) for the PiDP-1 panel).

# PDP-1

Simulated peripherals are the display, paper tape reader and punch, and Typewriter.
Not all extensions are implemented yet, only a very basic PDP-1/C.

## Run
Run `mkptyfio /tmp/typ` to create a pty,
then `./pdp1` in the `pdp1` directory to run the PDP-1 simulator (`./pidp1`) for the PiDP-1 panel).

# Display

To get a display, take the `p7sim` program from
[p7sim](https://github.com/aap/p7sim), start it,
and run any emulator with arguments `-h host` and `-p port`
indicating where to connect to (default: localhost 3400)
