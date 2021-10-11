#include <stdio.h>
#include <malloc.h>

#include "upng.h"

int main(int argc, char** argv) {
	upng_t* upng;
	unsigned width, height, depth;
	unsigned x, y, d;

	if (argc <= 1) {
		printf("usage:\n    ./png2rgb fruits.png");
		return 0;
	}

	upng = upng_new_from_file(argv[1]);
	if (upng_get_error(upng) != UPNG_EOK) {
		printf("[upng_new_from_file] error: %u %u\n", upng_get_error(upng), upng_get_error_line(upng));
		return 0;
	}

	if (upng_decode(upng) != UPNG_EOK) {
		printf("[upng_decode] error: %u %u\n", upng_get_error(upng), upng_get_error_line(upng));
		return 0;
	}

	width = upng_get_width(upng);
	height = upng_get_height(upng);
	depth = upng_get_bpp(upng) / 8;

	printf("size:	%ux%ux%u (%u)\n", width, height, upng_get_bpp(upng), upng_get_size(upng));
	printf("format:	%u\n", upng_get_format(upng));

	if (upng_get_format(upng) == UPNG_RGB8 || upng_get_format(upng) == UPNG_RGBA8) {

		for (y = 0; y < height; ++y) {
			for (x = 0; x < width; ++x) {
				printf("( ");
				for (d = 0; d < depth; ++d) {
					printf("%d ", upng_get_buffer(upng)[y * width * depth + x * depth + d]);
				}
				printf(") ");
			}
		}
		printf("\n\n-\n\n");
	}

	upng_free(upng);
	return 0;
}
