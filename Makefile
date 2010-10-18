test: test.c upng.c uz.c upng.h
	$(CC) -o test test.c upng.c uz.c -Wall -pedantic -g -O0 -lSDL
