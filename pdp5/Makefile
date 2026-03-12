INC=-I..
LIBS=-lpthread

all: pdp5

pdp5: main.c panel1.c pdp5.c ttytelnet.c ../common.c ../pollfd.c
	cc -g -O3 -o $@ $^ $(INC) $(LIBS)
