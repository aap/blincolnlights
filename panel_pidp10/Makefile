INC=-I..
LIBS=-lpthread -lm
CFLAGS=-O3 -g -Wall

panel_pidp10: panel_pidp10.c ../common.c ../gpio.c
	cc $(CFLAGS) -o $@ $^ $(INC) $(LIBS)

run: panel_pidp10
	./panel_pidp10
