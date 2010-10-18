#include <stdio.h>
#include <malloc.h>

#include "upng.h"

#define HI(w) (((w) >> 8) & 0xFF)
#define LO(w) ((w) & 0xFF)

int main(int argc, char** argv) {
	FILE* fh;
	upng_error error;
	upng_info* info;
	unsigned width, height;
	unsigned x, y;

	if (argc <= 2) {
		return 0;
	}

	info = upng_new();
	error = upng_decode_file(info, argv[1]);
	if (error != UPNG_EOK) {
		printf("error: %u %u\n", upng_get_error(info), upng_get_error_line(info));
		return 0;
	}

	width = upng_get_width(info);
	height = upng_get_height(info);

	printf("size:	%ux%ux%u (%u)\n", width, height, upng_get_bpp(info), upng_get_size(info));
	printf("format:	%u\n", upng_get_format(info));

	fh = fopen(argv[2], "wb");
	fprintf(fh, "%c%c%c", 0, 0, 2);
	fprintf(fh, "%c%c%c%c%c", 0, 0, 0, 0, 0);
	fprintf(fh, "%c%c%c%c%c%c%c%c%c%c", 0, 0, 0, 0, LO(width), HI(width), LO(height), HI(height), 32, 8);

	for (y = 0; y != height; ++y) {
		for (x = 0; x != width; ++x) {
			putc(upng_get_buffer(info)[(height - y) * width * 4 + x * 4 + 0], fh);
			putc(upng_get_buffer(info)[(height - y) * width * 4 + x * 4 + 1], fh);
			putc(upng_get_buffer(info)[(height - y) * width * 4 + x * 4 + 2], fh);
			putc(upng_get_buffer(info)[(height - y) * width * 4 + x * 4 + 3], fh);
		}
	}

	fclose(fh);

	upng_free(info);
	return 0;
}
