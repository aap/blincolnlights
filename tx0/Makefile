INC=-I..
LIBS=-lpthread -lm

all: tx0 tx0_pidp1

tx0: main.c tx0.c panelb18.c ../common.c
	cc -O3 -o tx0 $^ $(INC) $(LIBS)

tx0_pidp1: main.c tx0.c panel1.c ../common.c
	cc -O3 -o tx0_pidp1 $^ $(INC) $(LIBS)
