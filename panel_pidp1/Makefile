INC=-I..
LIBS=-lpthread -lm
CFLAGS=-O3 -g -Wall

panel_pidp1: panel_pidp1.c ../common.c ../gpio.c
	cc $(CFLAGS) -o $@ $^ $(INC) $(LIBS)

run: panel_pidp1
	./panel_pidp1
