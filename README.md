# Blincolnlights

This repository contains code to interface
with the physical
[Blincolnlights 18](https://hackaday.io/project/191985-blincolnlights-18),
the PiDP-10 and PiDP-1 panels.

There are also virtual SDL-based panels for the blincolnlights 18,
PDP-1 and Whirlwind.

In addition, there are software emulators of the
Whirlwind, TX-0, PDP-1 and PDP-5, which make use of these panels.
A PDP-6 emulator that works with the PiDP-10 panel
can be found [here](https://github.com/aap/pdp6/tree/master/newemu).

The panel drivers and the emulator communicate by mmap'ing
a file that represents the physical panel.
That way a physical panel is easily swappable for a virtual one
or another user interface.

Because of the the PiDP-1 project
the PDP-1 emulator is by far the most polished.
Making all of these work nicely with pretty and comfy interfaces
is the aspiration.

# RPI configuration

I noticed issues when i had i2c enabled in `/boot/config.txt`.
The pigpio library knows how to deal with that somehow but
my own code for some reason does not work and GPIO 2 and 3 are always high.
Until I manage to fix this, please make sure you don't have i2c on.

# Building

To build everything run `make' in the root directory.
The virtual panels need SDL2.

# Panel drivers

For the Blincolnlights 18 panel, start `panel_b18/panel_b18` before
starting any emulator.
The virtual version of this is `vpanel_b18/panel_b18`.

For the PiDP-1 panel, start `panel_pidp1/panel_pidp1` before
starting any emulator.
The virtual version of this is `vpanel_pdp1/panel_pdp1`.

For the PiDP-10 panel, start `panel_pidp10/panel_pidp10` before
starting any emulator.

The Whirlwind panel is purely virtual at this point.
Start it with `vpanel_whirlwind/panel_whirlwind`.

# Peripherals

The way periphals like paper tape devices or a typewriter are attached
has evolved a bit over time, especially while making the PiDP-1.
Originally the devices would be started first, waiting for the emulator to connect,
which was very convenient for development.
In a PiDP-setting where the emulator is constantly running it makes more sense
to have the peripherals connect to the emulator instead.

Therefore there is a bit of chaos in this regard right now,
ideally both ways will be supported.

# Whirlwind

Emulated peripherals are only a display.

The emulator is compatible with the Whirlwind and the Blincolnlights panels.

## Run
Start a compatible panel driver,
then run `./whirlwind` or `./whirlwind_b18` resp. in the `whirlwind` directory.

# TX-0

Emulated peripherals are the display, paper tape reader, and Flexowriter (typewriter and punch).
The configuration is the original with 64kw memory and no new instructions yet.
In the future it would be nice to expand it.

The emulator is compatible with the PDP-1 and the Blincolnlights panels.

## Run
Run `mkptyfl /tmp/fl` to create a pty.
Start a compatible panel driver,
then run `./tx0_pdp1` or `./tx0_b18` resp. in the `tx0` directory.

# PDP-1

Emulated peripherals are the paper tape reader and punch, typewriter,
and the type 30 display, including type 33 symbol generator.
The emulated system is a PDP-1/C with various options.

The emulator is compatible with the PDP-1 and the Blincolnlights panels.

## Run
Start a compatible panel driver,
then run `./pdp1` or `./pdp1_b18` resp. in the `pdp1` directory.

It is listening for connections on the following ports:

| Port    |                            |
| ------- | -------------------------- |
| 1040    | cli command                |
| 1041    | typewriter over telnet     |
| 1042    | external paper tape reader |
| 1043    | external paper tape punch  |
| 3400    | display                    |
| 3401    | second display             |

Use [pdp1_periph](https://github.com/obsolescence/pidp1/tree/main/src/pdp1_periph)
or the [web interface](https://github.com/obsolescence/pidp1/tree/main/web_pdp1)
to connect.

# PDP-5

This one has not been tested much yet.
The only emulated peripheral is a teletype.

The emulator is compatible with the PDP-1 panel.
Press start up for load address; the major state is shown in the program flags.

TODO: virtual PDP-5 panel. should also be mostly compatible with a PiDP-8.

## Run
Start a compatible panel driver,
then run `./pdp5` in the `pdp5` directory.

It is listening for a telnet connection on port 1550.

# Display

To get a display, take the `p7sim` program from
[p7sim](https://github.com/aap/p7sim), start it,
and run any emulator with arguments `-h host` and `-p port`
indicating where to connect to (default: localhost 3400).

The PDP-1 emulator also accepts incoming connections from `p7sim`
and [pdp1_periph](https://github.com/obsolescence/pidp1/tree/main/src/pdp1_periph)
