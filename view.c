#include <SDL/SDL.h>
#include <GL/gl.h>
#include <stdio.h>
#include <malloc.h>

#include "upng.h"

int main(int argc, char** argv) {
	SDL_Event event;
	upng_error error;
	upng_t* upng;
	GLuint texture;

	if (argc <= 1) {
		return 0;
	}

	upng = upng_new();
	error = upng_decode_file(upng, argv[1]);
	if (error != UPNG_EOK) {
		printf("error: %u %u\n", upng_get_error(upng), upng_get_error_line(upng));
		return 0;
	}

	SDL_Init(SDL_INIT_VIDEO);
	SDL_SetVideoMode(upng_get_width(upng), upng_get_height(upng), 0, SDL_OPENGL|SDL_DOUBLEBUF);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glClearColor(0.f, 0.f, 0.f, 0.f);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 1, 0, 1, 0, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	switch (upng_get_format(upng)) {
	case UPNG_RGB_888:
		glTexImage2D(GL_TEXTURE_2D, 0, 3, upng_get_width(upng), upng_get_height(upng), 0, GL_RGB, GL_UNSIGNED_BYTE, upng_get_buffer(upng));
		break;
	case UPNG_RGBA_8888:
		glTexImage2D(GL_TEXTURE_2D, 0, 4, upng_get_width(upng), upng_get_height(upng), 0, GL_RGBA, GL_UNSIGNED_BYTE, upng_get_buffer(upng));
		break;
	default:
		return 1;
	}

	upng_free(upng);

	while (SDL_WaitEvent(&event)) {
		if (event.type == SDL_QUIT) {
			break;
		}

		glClear(GL_COLOR_BUFFER_BIT);

		glBegin(GL_QUADS);
			glTexCoord2f(0.f, 1.f);
			glVertex2f(0.f, 0.f);

			glTexCoord2f(0.f, 0.f);
			glVertex2f(0.f, 1.f);

			glTexCoord2f(1.f, 0.f);
			glVertex2f(1.f, 1.f);

			glTexCoord2f(1.f, 1.f);
			glVertex2f(1.f, 0.f);
		glEnd();

		SDL_GL_SwapBuffers();
	}

	glDeleteTextures(1, &texture);
	SDL_Quit();
	return 0;
}
