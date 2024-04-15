INC=-I..
LIBS=-lpthread

all: whirlwind whirlwind_b18

whirlwind: whirlwind.c panelww.c ../common.c
	cc -O3 -o $@ $^ $(INC) $(LIBS)

whirlwind_b18: whirlwind.c panelb18.c ../common.c
	cc -O3 -o $@ $^ $(INC) $(LIBS)
