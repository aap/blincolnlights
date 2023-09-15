INC=-I..
LIBS=-lpigpio -lpthread

all: pdp1 pidp1

pdp1: pdp1.c pdp1_b18.c ../common.c ../panel_b18.c
	cc -O3 -o pdp1 $^ $(INC) $(LIBS)

pidp1: pdp1.c pdp1_pidp1.c ../common.c ../panel_pidp1.c
	cc -O3 -o pidp1 $^ $(INC) $(LIBS)
