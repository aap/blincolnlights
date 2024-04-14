INC=-I..
LIBS=-lpthread -lm

all: pdp1 pidp1

pdp1: main.c panelb18.c pdp1.c ../common.c
	cc -g -O3 -o pdp1 $^ $(INC) $(LIBS)

pidp1: main.c panel1.c pdp1.c ../common.c
	cc -g -O3 -o pidp1 $^ $(INC) $(LIBS)
