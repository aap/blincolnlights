INC=-I..
LIBS=-lpthread -lm
CFLAGS=-O3 -g -Wall

PINCTRL := $(wildcard ../pinctrl/*.o)

panel_pidp1: panel_pidp1.c ../common.c
	cc $(CFLAGS) -o $@ $^ $(INC) $(LIBS) $(PINCTRL)

run: panel_pidp1
	./panel_pidp1
