#include <stdio.h>
#include <malloc.h>

#include "upng.h"

int main(int argc, char** argv) {
	FILE* fh;
	upng_error error;
	upng_info* info;
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

	printf("size:	%ux%u\n", upng_get_width(info), upng_get_height(info));
	printf("bpp:	%u\n", upng_get_bpp(info));
	printf("format:	%u\n", upng_get_format(info));

	fh = fopen(argv[2], "wb");
	fprintf(fh, "%c%c%c", 0, 0, 2);
	fprintf(fh, "%c%c%c%c%c", 0, 0, 0, 0, 0);
	fprintf(fh, "%c%c%c%c%c%c%c%c%c%c", 0, 0, 0, 0, upng_get_width(info), 0, upng_get_height(info), 0, 32, 8);

	for (y = 0; y != upng_get_height(info); ++y) {
		for (x = 0; x != upng_get_width(info); ++x) {
			putc(upng_get_buffer(info)[(upng_get_height(info) - y) * upng_get_width(info) * 4 + x * 4 + 0], fh);
			putc(upng_get_buffer(info)[(upng_get_height(info) - y) * upng_get_width(info) * 4 + x * 4 + 1], fh);
			putc(upng_get_buffer(info)[(upng_get_height(info) - y) * upng_get_width(info) * 4 + x * 4 + 2], fh);
			putc(upng_get_buffer(info)[(upng_get_height(info) - y) * upng_get_width(info) * 4 + x * 4 + 3], fh);
		}
	}

	fclose(fh);

	upng_free(info);
	return 0;
}
