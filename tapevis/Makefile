all: tapevis

tapevis: tapevis.c ../common.c
	cc -o $@ $^ -I.. `sdl2-config --cflags --libs`
