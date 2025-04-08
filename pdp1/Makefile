INC=-I..
LIBS=-lpthread -lm -lSDL2

all: pdp1_b18 pdp1

pdp1_b18: main.c panelb18.c pdp1.c typtelnet.c audio.c ../common.c ../pollfd.c
	cc -g -O3 -o $@ $^ $(INC) $(LIBS)

pdp1: main.c panel1.c pdp1.c typtelnet.c audio.c ../common.c ../pollfd.c
	cc -g -O3 -o $@ $^ $(INC) $(LIBS)
