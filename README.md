# Blincolnlights

This repository contains code to interface
with the [Blincolnlights 18](https://hackaday.io/project/191985-blincolnlights-18),
the PiDP-10 and the upcoming PiDP-1 panels.

In addition, there are software emulators of the
Whirlwind, TX-0 and PDP-1, which make use of these panels.
A PDP-6 emulator that works with the PiDP-10 panel
can be found [here](https://github.com/aap/pdp6/tree/master/newemu).

TODO: Eventually this repository will probably only have the
panel drivers, and emulators elsewhere.

The panel drivers and the emulator communicate by mmap'ing
a file that represents the physical panel.
That way a physical panel is easily swappable for a virtual one
or another user interface.

# RPI configuration

I noticed issues when i had i2c enabled in `/boot/config.txt`.
The pigpio library knows how to deal with that somehow but
my own code for some reason does not work and GPIO 2 and 3 are always high.
Until I manage to fix this, please make sure you don't have i2c on.

# Building

To build everything run `make' in the root directory.

# Panel drivers

For the Blincolnlights 18 panel, start `panel_b18/panel_b18` before
starting any emulator.

For the PiDP-1 panel, start `panel_pidp1/panel_pidp1` before
starting any emulator.

For the PiDP-10 panel, start `panel_pidp10/panel_pidp10` before
starting any emulator.

# Whirlwind

Emulated peripherals are only a display.

## Run
Start the blincolnlights 18 panel driver,
then run `./whirlwind` in the `whirlwind` directory.

# TX-0

Emulated peripherals are the display, paper tape reader, and Flexowriter (typewriter and punch).
The configuration is the original with 64kw memory and no new instructions yet.
In the future it would be nice to expand it.

## Run
Run `mkptyfl /tmp/fl` to create a pty.
Start either the blincolnlights or PiDP-1 panel driver, 
then `./tx0` in the `tx0` directory to run the TX-0 emulator (`./tx0_pidp1` for the PiDP-1 panel).

# PDP-1

Emulated peripherals are the display, paper tape reader and punch, and Typewriter.
The emulated system is a PDP-1/C with various options.

## Run
Run `mkptyfio /tmp/typ` to create a pty.
Start either the blincolnlights or PiDP-1 panel driver, 
then `./pdp1` in the `pdp1` directory to run the PDP-1 emulator (`./pidp1` for the PiDP-1 panel).

# Display

To get a display, take the `p7sim` program from
[p7sim](https://github.com/aap/p7sim), start it,
and run any emulator with arguments `-h host` and `-p port`
indicating where to connect to (default: localhost 3400)
