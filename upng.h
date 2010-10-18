/*
uPNG -- derived from uPNG version 20100808

Copyright (c) 2005-2010 Lode Vandevenne
Copyright (c) 2010 Sean Middleditch

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

		1. The origin of this software must not be misrepresented; you must not
		claim that you wrote the original software. If you use this software
		in a product, an acknowledgment in the product documentation would be
		appreciated but is not required.

		2. Altered source versions must be plainly marked as such, and must not be
		misrepresented as being the original software.

		3. This notice may not be removed or altered from any source
		distribution.
*/

#if !defined(UPNG_H)
#define UPNG_H

typedef enum upng_error {
	UPNG_EOK			= 0,
	UPNG_ENOMEM			= 1,
	UPNG_ENOTFOUND		= 2,
	UPNG_ENOTPNG		= 3,
	UPNG_EMALFORMED		= 4,
	UPNG_EUNSUPPORTED	= 5,
	UPNG_EBADCHECKSUM	= 6
} upng_error;

typedef enum upng_format {
	UPNG_BADFORMAT,

	UPNG_RGB_888,

	UPNG_RGBA_8888,

	UPNG_G_1,
	UPNG_G_2,
	UPNG_G_4,
	UPNG_G_8,

	UPNG_GA_1,
	UPNG_GA_2,
	UPNG_GA_4,
	UPNG_GA_8
} upng_format;

typedef struct upng_t upng_t;

upng_t*	upng_new			(void);
void		upng_free			(upng_t* decoder);

upng_error	upng_decode			(upng_t* decoder, const unsigned char* in, unsigned long insize);
upng_error	upng_decode_file	(upng_t* decoder, const char* filename);

upng_error	upng_inspect		(upng_t* decoder, const unsigned char* in, unsigned long size);
upng_error	upng_inspect_file	(upng_t* decoder, const char* filename);

upng_error	upng_get_error		(const upng_t* upng);
unsigned	upng_get_error_line	(const upng_t* upng);

unsigned	upng_get_width		(const upng_t* upng);
unsigned	upng_get_height		(const upng_t* upng);
unsigned	upng_get_bpp		(const upng_t* upng);
unsigned	upng_get_format		(const upng_t* upng);

const unsigned char*	upng_get_buffer		(const upng_t* upng);
unsigned				upng_get_size		(const upng_t* upng);

/* internal structures and data types */

typedef enum upng_color {
	UPNG_GREY		= 0,
	UPNG_RGB		= 2,
	UPNG_GREY_ALPHA	= 4,
	UPNG_RGBA		= 6
} upng_color;

struct upng_t {
	unsigned		width;
	unsigned		height;

	upng_color		color_type;
	unsigned		color_depth;
	upng_format		format;

	unsigned char*	buffer;
	unsigned long	size;

	upng_error		error;
	unsigned		error_line;
};

#endif /*defined(UPNG_H)*/
