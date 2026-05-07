# Makefile for Cyberpunk City Game
# Path: /home/vboxuser/Downloads/Integration_de_projet/ahmed_task

CC = gcc
TARGET = game
SRC = main.c
CFLAGS = -Wall -Wextra -O2
SDL_CFLAGS = $(shell sdl2-config --cflags)
SDL_LIBS = $(shell sdl2-config --libs)
LIBS = -lSDL2_image -lm
ALL_CFLAGS = $(CFLAGS) $(SDL_CFLAGS)
ALL_LIBS = $(SDL_LIBS) $(LIBS)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(ALL_CFLAGS) $< -o $@ $(ALL_LIBS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

debug: CFLAGS += -g -DDEBUG
debug: clean $(TARGET)

install-deps:
	sudo apt-get update && sudo apt-get install -y libsdl2-dev libsdl2-image-dev

help:
	@echo "Targets: all, clean, run, debug, install-deps, help"

.PHONY: all clean run debug install-deps help
