all: pdp_periph pdp_periphES

SRC=main.c lua.c display.c papertape.c typewriter.c dectape.c ../common.c liblua.o textures.o
INC=-I.. -I../lua
LIBS=-lSDL2_ttf

pdp_periph: $(SRC) glad/glad.o
	cc -g -O3 $(INC) -o $@ -g $^ -lm -ldl -lpthread `sdl2-config --cflags --libs` $(LIBS)
pdp_periphES: $(SRC) glad/glad.o
	cc -g -O3 $(INC) -o $@ -g -DGLES $^ -lm -ldl -lpthread `sdl2-config --cflags --libs` $(LIBS)

textures.o: textures.c art/dectape_cap_circle.h art/dectape_reel.h
	cc -c -o $@ textures.c

art/dectape_cap_circle.h: art/dectape_cap_circle.png
	convert $< -depth 8 RGBA:- > art/dectape_cap_circle.raw
	xxd -n dectape_cap_circle -i art/dectape_cap_circle.raw > $@

art/dectape_reel.h: art/dectape_reel.png
	convert $< -depth 8 RGBA:- > art/dectape_reel.raw
	xxd -n dectape_reel -i art/dectape_reel.raw > $@
