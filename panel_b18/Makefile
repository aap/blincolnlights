INC=-I..
LIBS=-lpthread -lm
CFLAGS=-O3 -g -Wall

panel_b18: panel_b18.c ../common.c ../gpio.c
	cc $(CFLAGS) -o $@ $^ $(INC) $(LIBS)

run: panel_b18
	./panel_b18
