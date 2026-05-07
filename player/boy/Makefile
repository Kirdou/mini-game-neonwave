all:
	gcc main.c -o biker_game $(shell sdl2-config --cflags --libs) -lSDL2_image -lSDL2_ttf

run: all
	./biker_game

clean:
	rm -f biker_game
