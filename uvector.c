/*
uPNG -- derived from LodePNG version 20100808

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

#include <stdlib.h>

#include "upng.h"

typedef struct ucvector {
	unsigned char *data;
	unsigned long size;	/*used size */
	unsigned long allocsize;	/*allocated size */
} ucvector;

typedef struct uivector {
	unsigned *data;
	unsigned long size;	/*size in number of unsigned longs */
	unsigned long allocsize;	/*allocated size in bytes */
} uivector;

void uivector_cleanup(uivector* p)
{
	p->size = p->allocsize = 0;
	free(p->data);
	p->data = NULL;
}

upng_error uivector_resize(uivector* p, unsigned long size)
{				/*returns 1 if success, 0 if failure ==> nothing done */
	if (size* sizeof(unsigned) > p->allocsize) {
		unsigned long newsize = size* sizeof(unsigned)* 2;
		void *data = realloc(p->data, newsize);
		if (data) {
			p->allocsize = newsize;
			p->data = (unsigned *)data;
			p->size = size;
		} else
			return UPNG_ENOMEM;
	} else
		p->size = size;
	return UPNG_EOK;
}

upng_error uivector_resizev(uivector* p, unsigned long size, unsigned value)
{				/*resize and give all new elements the value */
	unsigned long oldsize = p->size, i;
	if (uivector_resize(p, size) != UPNG_EOK)
		return UPNG_ENOMEM;
	for (i = oldsize; i < size; i++)
		p->data[i] = value;
	return UPNG_EOK;
}

void uivector_init(uivector* p)
{
	p->data = NULL;
	p->size = p->allocsize = 0;
}

void ucvector_cleanup(void *p)
{
	((ucvector *) p)->size = ((ucvector *) p)->allocsize = 0;
	free(((ucvector *) p)->data);
	((ucvector *) p)->data = NULL;
}

upng_error ucvector_resize(ucvector* p, unsigned long size)
{				/*returns 1 if success, 0 if failure ==> nothing done */
	if (size* sizeof(unsigned char) > p->allocsize) {
		unsigned long newsize = size* sizeof(unsigned char)* 2;
		void *data = realloc(p->data, newsize);
		if (data) {
			p->allocsize = newsize;
			p->data = (unsigned char *)data;
			p->size = size;
		} else
			return UPNG_ENOMEM;	/*error: not enough memory */
	} else
		p->size = size;
	return UPNG_EOK;
}

upng_error ucvector_resizev(ucvector* p, unsigned long size, unsigned char value)
{				/*resize and give all new elements the value */
	unsigned long oldsize = p->size, i;
	if (ucvector_resize(p, size) != UPNG_EOK)
		return UPNG_ENOMEM;
	for (i = oldsize; i < size; i++)
		p->data[i] = value;
	return UPNG_EOK;
}

void ucvector_init(ucvector* p)
{
	p->data = NULL;
	p->size = p->allocsize = 0;
}

/*you can both convert from vector to buffer&size and vica versa*/
void ucvector_init_buffer(ucvector* p, unsigned char *buffer, unsigned long size)
{
	p->data = buffer;
	p->allocsize = p->size = size;
}
