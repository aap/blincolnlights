INC=-I..
LIBS=-lpthread

all: tx0 tx0_pidp1

tx0: tx0.c tx0_b18.c ../common.c ../gpio.c ../panel_b18.c
	cc -O3 -o tx0 $^ $(INC) $(LIBS)

tx0_pidp1: tx0.c tx0_pidp1.c ../common.c ../gpio.c ../panel_pidp1.c
	cc -O3 -o tx0_pidp1 $^ $(INC) $(LIBS)
