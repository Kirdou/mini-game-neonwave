all:
	gcc main.c -o cyborg_game $(shell sdl2-config --cflags --libs) -lSDL2_image -lSDL2_ttf

run: all
	./cyborg_game

clean:
	rm -f cyborg_game
