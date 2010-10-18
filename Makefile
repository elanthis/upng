all: test view

test: test.c upng.c upng.h
	$(CC) -o test test.c upng.c -Wall -pedantic -g -O0

view: view.c upng.c upng.h
	$(CC) -o view view.c upng.c -Wall -pedantic -g -O0 -lSDL -lGL
