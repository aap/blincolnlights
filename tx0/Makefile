INC=-I..
LIBS=-lpthread -lm

all: tx0_pdp1 tx0_b18

tx0_pdp1: main.c tx0.c panel1.c ../common.c
	cc -O3 -o $@ $^ $(INC) $(LIBS)

tx0_b18: main.c tx0.c panelb18.c ../common.c
	cc -O3 -o $@ $^ $(INC) $(LIBS)
