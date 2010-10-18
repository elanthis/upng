#include <SDL/SDL.h>
#include <stdio.h>
#include <malloc.h>

#include "upng.h"

int main(int argc, char** argv) {
	SDL_Surface* display;
	SDL_Surface* surface;
	SDL_Event event;
	upng_error_type error;
	upng_decoder decoder;

	if (argc <= 1) {
		return 0;
	}

	upng_decoder_init(&decoder);
	error = upng_decode_file(&decoder, argv[1]);
	if (error != UPNG_EOK) {
		printf("error: %u %u\n", decoder.error, decoder.error_line);
		return 0;
	}

	printf("type: %d,%d\n", decoder.color_type, decoder.color_depth);
	printf("size: %u,%u\n", decoder.width, decoder.height);

	SDL_Init(SDL_INIT_VIDEO);
	display = SDL_SetVideoMode(decoder.width, decoder.height, 0, 0);

	surface = SDL_CreateRGBSurfaceFrom(decoder.img_buffer, decoder.width, decoder.height, 32, decoder.width, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);

	/*
	{
		int i, j;
		for (j = 0; j != decoder.height ; ++j) {
			for (i = 0; i != decoder.width; ++i) {
				printf("%02x", decoder.img_buffer[j * decoder.width * 4 + i * 4]);
				printf("%02x", decoder.img_buffer[j * decoder.width * 4 + i * 4 + 1]);
				printf("%02x", decoder.img_buffer[j * decoder.width * 4 + i * 4 + 2]);
				printf("%02x ", decoder.img_buffer[j * decoder.width * 4 + i * 4 + 3]);
			}
			printf("\n");
		}
	}
	*/

	while (SDL_WaitEvent(&event)) {
		if (event.type == SDL_QUIT) {
			break;
		}

		SDL_BlitSurface(surface, 0, display, 0);
		SDL_UpdateRect(display, 0, 0, decoder.width, decoder.height);
	}

	SDL_Quit();
	upng_decoder_cleanup(&decoder);
	return 0;
}
