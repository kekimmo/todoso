
all:
	gcc main.c --std=c99 -lm `pkg-config --cflags --libs gl glu libpng sdl` -g -o todoso

