INC=-I../lua -I..
LIBS=-lpthread -lm -lSDL2
#CFLAGS=-Wall -Wextra -g -O3
#CFLAGS=-g -O3 -mcpu=cortex-a53 -mtune=cortex-a53 $(INC)
CFLAGS=-g -Wall -Wno-parentheses
OBJ=main.o pdp6.o netmem.o tty.o pt.o dis340.o dc.o ut.o ge.o audio.o ttytelnet.o common.o pollfd.o lua.o

all: pdp6 pdp6_10

common.o: ../common.c
	cc -c $(CFLAGS) -o $@ $^
pollfd.o: ../pollfd.c
	cc -c $(CFLAGS) -o $@ $^

pdp6: $(OBJ) panel6.o liblua.o
	cc $(CFLAGS) -o $@ $^ $(LIBS)

pdp6_10: $(OBJ) panel10.o liblua.o
	cc $(CFLAGS) -o $@ $^ $(LIBS)

run: pdp6
	./pdp6
