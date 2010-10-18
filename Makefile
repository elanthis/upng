test: test.c upng.c upng.h
	$(CC) -o test test.c upng.c -Wall -pedantic -g -O0 -lSDL
