test: test.c upng.c uz.c uvector.c upng.h
	$(CC) -o test test.c upng.c uz.c uvector.c -Wall -pedantic -g -O0 -lSDL
