all: png2tga glview

png2tga: png2tga.c upng.c upng.h
	$(CC) -o png2tga png2tga.c upng.c -Wall -pedantic -g -O0

glview: glview.c upng.c upng.h
	$(CC) -o glview glview.c upng.c -Wall -pedantic -g -O0 -lSDL -lGL
