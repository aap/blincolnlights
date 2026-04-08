all: pdp_periph 
#pdp_periphES

SRC=main.c lua.c display.c papertape.c typewriter.c ../common.c liblua.o
INC=-I.. -I../lua
LIBS=-lSDL2_ttf

pdp_periph: $(SRC) glad/glad.o
	cc -g -O3 $(INC) -o $@ -g $^ -lm -ldl -lpthread `sdl2-config --cflags --libs` $(LIBS)
pdp_periphES: $(SRC) glad/glad.o
	cc -g -O3 $(INC) -o $@ -g -DGLES $^ -lm -ldl -lpthread `sdl2-config --cflags --libs` $(LIBS)
