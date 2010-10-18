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

#define FIRST_LENGTH_CODE_INDEX 257
#define LAST_LENGTH_CODE_INDEX 285
#define NUM_DEFLATE_CODE_SYMBOLS 288	/*256 literals, the end code, some length codes, and 2 unused codes */
#define NUM_CODE_LENGTH_CODES 19	/*the code length codes. 0-15: code lengths, 16: copy previous 3-6 times, 17: 3-10 zeros, 18: 11-138 zeros */
#define NUM_DISTANCE_SYMBOLS 32	/*the distance codes have their own symbols, 30 used, 2 unused */

#define SET_ERROR(info,code) do { (info)->error = (code); (info)->error_line = __LINE__; } while (0)

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

typedef struct huffman_tree {
	uivector tree2d;
	uivector tree1d;
	uivector lengths;	/*the lengths of the codes of the 1d-tree */
	unsigned maxbitlen;	/*maximum number of bits a single code can get */
	unsigned numcodes;	/*number of symbols in the alphabet = number of codes */
} huffman_tree;

static const unsigned LENGTHBASE[29]	/*the base lengths represented by codes 257-285 */
    = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
	67, 83, 99, 115, 131, 163, 195, 227, 258
};

static const unsigned LENGTHEXTRA[29]	/*the extra bits used by codes 257-285 (added to base length) */
    = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
	5, 5, 5, 5, 0
};

static const unsigned DISTANCEBASE[30]	/*the base backwards distances (the bits of distance codes appear after length codes and use their own huffman tree) */
    = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
	513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289,
	16385, 24577
};

static const unsigned DISTANCEEXTRA[30]	/*the extra bits of backwards distances (added to base) */
    = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10,
	10, 11, 11, 12, 12, 13, 13
};

static const unsigned CLCL[NUM_CODE_LENGTH_CODES]	/*the order in which "code length alphabet code lengths" are stored, out of this the huffman tree of the dynamic huffman tree lengths is generated */
= { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

static void uivector_cleanup(uivector* p)
{
	p->size = p->allocsize = 0;
	free(p->data);
	p->data = NULL;
}

static upng_error uivector_resize(uivector* p, unsigned long size)
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

static upng_error uivector_resizev(uivector* p, unsigned long size, unsigned value)
{				/*resize and give all new elements the value */
	unsigned long oldsize = p->size, i;
	if (uivector_resize(p, size) != UPNG_EOK)
		return UPNG_ENOMEM;
	for (i = oldsize; i < size; i++)
		p->data[i] = value;
	return UPNG_EOK;
}

static void uivector_init(uivector* p)
{
	p->data = NULL;
	p->size = p->allocsize = 0;
}

static upng_error ucvector_resize(ucvector* p, unsigned long size)
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

/*you can both convert from vector to buffer&size and vica versa*/
static void ucvector_init_buffer(ucvector* p, unsigned char *buffer, unsigned long size)
{
	p->data = buffer;
	p->allocsize = p->size = size;
}

static unsigned char read_bit(unsigned long *bitpointer, const unsigned char *bitstream)
{
	unsigned char result = (unsigned char)((bitstream[(*bitpointer) >> 3] >> ((*bitpointer) & 0x7)) & 1);
	(*bitpointer)++;
	return result;
}

static unsigned read_bits(unsigned long *bitpointer, const unsigned char *bitstream, unsigned long nbits)
{
	unsigned result = 0, i;
	for (i = 0; i < nbits; i++)
		result += ((unsigned)read_bit(bitpointer, bitstream)) << i;
	return result;
}

static void huffman_tree_init(huffman_tree* tree)
{
	uivector_init(&tree->tree2d);
	uivector_init(&tree->tree1d);
	uivector_init(&tree->lengths);
}

static void huffman_tree_cleanup(huffman_tree* tree)
{
	uivector_cleanup(&tree->tree2d);
	uivector_cleanup(&tree->tree1d);
	uivector_cleanup(&tree->lengths);
}

/*the tree representation used by the info. return value is error*/
static unsigned huffman_tree_create(huffman_tree* tree)
{
	unsigned nodefilled = 0;	/*up to which node it is filled */
	unsigned treepos = 0;	/*position in the tree (1 of the numcodes columns) */
	unsigned n, i;

	if (uivector_resize(&tree->tree2d, tree->numcodes * 2) != UPNG_EOK)
		return 9901;	/*if failed return not enough memory error */
	/*convert tree1d[] to tree2d[][]. In the 2D array, a value of 32767 means uninited, a value >= numcodes is an address to another bit, a value < numcodes is a code. The 2 rows are the 2 possible bit values (0 or 1), there are as many columns as codes - 1
	   a good huffmann tree has N * 2 - 1 nodes, of which N - 1 are internal nodes. Here, the internal nodes are stored (what their 0 and 1 option point to). There is only memory for such good tree currently, if there are more nodes (due to too long length codes), error 55 will happen */
	for (n = 0; n < tree->numcodes * 2; n++)
		tree->tree2d.data[n] = 32767;	/*32767 here means the tree2d isn't filled there yet */

	for (n = 0; n < tree->numcodes; n++)	/*the codes */
		for (i = 0; i < tree->lengths.data[n]; i++) {	/*the bits for this code */
			unsigned char bit = (unsigned char)((tree->tree1d.data[n] >> (tree->lengths.data[n] - i - 1)) & 1);
			if (treepos > tree->numcodes - 2)
				return 55;	/*error 55: oversubscribed; see description in header */
			if (tree->tree2d.data[2 * treepos + bit] == 32767) {	/*not yet filled in */
				if (i + 1 == tree->lengths.data[n]) {	/*last bit */
					tree->tree2d.data[2 * treepos + bit] = n;	/*put the current code in it */
					treepos = 0;
				} else {	/*put address of the next step in here, first that address has to be found of course (it's just nodefilled + 1)... */

					nodefilled++;
					tree->tree2d.data[2 * treepos + bit] = nodefilled + tree->numcodes;	/*addresses encoded with numcodes added to it */
					treepos = nodefilled;
				}
			} else
				treepos = tree->tree2d.data[2 * treepos + bit] - tree->numcodes;
		}
	for (n = 0; n < tree->numcodes * 2; n++)
		if (tree->tree2d.data[n] == 32767)
			tree->tree2d.data[n] = 0;	/*remove possible remaining 32767's */

	return 0;
}

static unsigned huffman_tree_create_lengths2(huffman_tree* tree)
{				/*given that numcodes, lengths and maxbitlen are already filled in correctly. return value is error. */
	uivector blcount;
	uivector nextcode;
	unsigned bits, n, error = 0;

	uivector_init(&blcount);
	uivector_init(&nextcode);
	if (uivector_resize(&tree->tree1d, tree->numcodes) != UPNG_EOK
	    || uivector_resizev(&blcount, tree->maxbitlen + 1, 0) != UPNG_EOK
	    || uivector_resizev(&nextcode, tree->maxbitlen + 1, 0) != UPNG_EOK)
		error = 9902;

	if (!error) {
		/*step 1: count number of instances of each code length */
		for (bits = 0; bits < tree->numcodes; bits++)
			blcount.data[tree->lengths.data[bits]]++;
		/*step 2: generate the nextcode values */
		for (bits = 1; bits <= tree->maxbitlen; bits++)
			nextcode.data[bits] = (nextcode.data[bits - 1] + blcount.data[bits - 1]) << 1;
		/*step 3: generate all the codes */
		for (n = 0; n < tree->numcodes; n++)
			if (tree->lengths.data[n] != 0)
				tree->tree1d.data[n] = nextcode.data[tree->lengths.data[n]]++;
	}

	uivector_cleanup(&blcount);
	uivector_cleanup(&nextcode);

	if (!error)
		return huffman_tree_create(tree);
	else
		return error;
}

/*given the code lengths (as stored in the PNG file), generate the tree as defined by Deflate. maxbitlen is the maximum bits that a code in the tree can have. return value is error.*/
static unsigned huffman_tree_create_lengths(huffman_tree* tree, const unsigned *bitlen, unsigned long numcodes, unsigned maxbitlen)
{
	unsigned i;
	if (uivector_resize(&tree->lengths, numcodes) != UPNG_EOK)
		return 9903;
	for (i = 0; i < numcodes; i++)
		tree->lengths.data[i] = bitlen[i];
	tree->numcodes = (unsigned)numcodes;	/*number of symbols */
	tree->maxbitlen = maxbitlen;
	return huffman_tree_create_lengths2(tree);
}

/*get the tree of a deflated block with fixed tree, as specified in the deflate specification*/
static unsigned generate_fixed_tree(huffman_tree* tree)
{
	unsigned i, error = 0;
	uivector bitlen;
	uivector_init(&bitlen);
	if (uivector_resize(&bitlen, NUM_DEFLATE_CODE_SYMBOLS) != UPNG_EOK)
		error = 9909;

	if (!error) {
		/*288 possible codes: 0-255=literals, 256=endcode, 257-285=lengthcodes, 286-287=unused */
		for (i = 0; i <= 143; i++)
			bitlen.data[i] = 8;
		for (i = 144; i <= 255; i++)
			bitlen.data[i] = 9;
		for (i = 256; i <= 279; i++)
			bitlen.data[i] = 7;
		for (i = 280; i <= 287; i++)
			bitlen.data[i] = 8;

		error = huffman_tree_create_lengths(tree, bitlen.data, NUM_DEFLATE_CODE_SYMBOLS, 15);
	}

	uivector_cleanup(&bitlen);
	return error;
}

static unsigned generate_distance_tree(huffman_tree* tree)
{
	unsigned i, error = 0;
	uivector bitlen;
	uivector_init(&bitlen);
	if (uivector_resize(&bitlen, NUM_DISTANCE_SYMBOLS) != UPNG_EOK)
		error = 9910;

	/*there are 32 distance codes, but 30-31 are unused */
	if (!error) {
		for (i = 0; i < NUM_DISTANCE_SYMBOLS; i++)
			bitlen.data[i] = 5;
		error = huffman_tree_create_lengths(tree, bitlen.data, NUM_DISTANCE_SYMBOLS, 15);
	}
	uivector_cleanup(&bitlen);
	return error;
}

/*Decodes a symbol from the tree
if decoded is true, then result contains the symbol, otherwise it contains something unspecified (because the symbol isn't fully decoded yet)
bit is the bit that was just read from the stream
you have to decode a full symbol (let the decode function return true) before you can try to decode another one, otherwise the state isn't reset
return value is error.*/
static unsigned huffman_tree_decode(const huffman_tree* tree, unsigned *decoded, unsigned *result, unsigned *treepos, unsigned char bit)
{
	if ((*treepos) >= tree->numcodes)
		return 11;	/*error: it appeared outside the codetree */

	(*result) = tree->tree2d.data[2 * (*treepos) + bit];
	(*decoded) = ((*result) < tree->numcodes);

	if (*decoded)
		(*treepos) = 0;
	else
		(*treepos) = (*result) - tree->numcodes;

	return 0;
}

static unsigned huffman_decode_symbol(unsigned int *error, const unsigned char *in, unsigned long *bp, const huffman_tree* codetree, unsigned long inlength)
{
	unsigned treepos = 0, decoded, ct;
	for (;;) {
		unsigned char bit;
		if (((*bp) & 0x07) == 0 && ((*bp) >> 3) > inlength) {
			*error = 10;
			return 0;
		}		/*error: end of input memory reached without endcode */
		bit = read_bit(bp, in);
		*error = huffman_tree_decode(codetree, &decoded, &ct, &treepos, bit);
		if (*error)
			return 0;	/*stop, an error happened */
		if (decoded)
			return ct;
	}
}

/*get the tree of a deflated block with fixed tree, as specified in the deflate specification*/
static void get_tree_inflate_fixed(huffman_tree* tree, huffman_tree* treeD)
{
	/*error checking not done, this is fixed stuff, it works, it doesn't depend on the image */
	generate_fixed_tree(tree);
	generate_distance_tree(treeD);
}

/*get the tree of a deflated block with dynamic tree, the tree itself is also Huffman compressed with a known tree*/
static unsigned get_tree_inflate_dynamic(huffman_tree* codetree, huffman_tree* codetreeD, huffman_tree* codelengthcodetree, const unsigned char *in, unsigned long *bp, unsigned long inlength)
{
	/*make sure that length values that aren't filled in will be 0, or a wrong tree will be generated */
	/*C-code note: use no "return" between ctor and dtor of an uivector! */
	unsigned error = 0;
	unsigned n, HLIT, HDIST, HCLEN, i;
	uivector bitlen;
	uivector bitlenD;
	uivector codelengthcode;

	if ((*bp) >> 3 >= inlength - 2) {
		return 49;
	}
	/*the bit pointer is or will go past the memory */
	HLIT = read_bits(bp, in, 5) + 257;	/*number of literal/length codes + 257. Unlike the spec, the value 257 is added to it here already */
	HDIST = read_bits(bp, in, 5) + 1;	/*number of distance codes. Unlike the spec, the value 1 is added to it here already */
	HCLEN = read_bits(bp, in, 4) + 4;	/*number of code length codes. Unlike the spec, the value 4 is added to it here already */

	/*read the code length codes out of 3 * (amount of code length codes) bits */
	uivector_init(&codelengthcode);
	if (uivector_resize(&codelengthcode, NUM_CODE_LENGTH_CODES) != UPNG_EOK)
		error = 9911;

	if (!error) {
		for (i = 0; i < NUM_CODE_LENGTH_CODES; i++) {
			if (i < HCLEN)
				codelengthcode.data[CLCL[i]] = read_bits(bp, in, 3);
			else
				codelengthcode.data[CLCL[i]] = 0;	/*if not, it must stay 0 */
		}

		error = huffman_tree_create_lengths(codelengthcodetree, codelengthcode.data, codelengthcode.size, 7);
	}

	uivector_cleanup(&codelengthcode);
	if (error)
		return error;

	/*now we can use this tree to read the lengths for the tree that this function will return */
	uivector_init(&bitlen);
	uivector_resizev(&bitlen, NUM_DEFLATE_CODE_SYMBOLS, 0);
	uivector_init(&bitlenD);
	uivector_resizev(&bitlenD, NUM_DISTANCE_SYMBOLS, 0);
	i = 0;
	if (!bitlen.data || !bitlenD.data)
		error = 9912;
	else
		while (i < HLIT + HDIST) {	/*i is the current symbol we're reading in the part that contains the code lengths of lit/len codes and dist codes */
			unsigned code = huffman_decode_symbol(&error, in, bp,
							      codelengthcodetree, inlength);
			if (error)
				break;

			if (code <= 15) {	/*a length code */
				if (i < HLIT)
					bitlen.data[i] = code;
				else
					bitlenD.data[i - HLIT] = code;
				i++;
			} else if (code == 16) {	/*repeat previous */
				unsigned replength = 3;	/*read in the 2 bits that indicate repeat length (3-6) */
				unsigned value;	/*set value to the previous code */

				if ((*bp) >> 3 >= inlength) {
					error = 50;
					break;
				}
				/*error, bit pointer jumps past memory */
				replength += read_bits(bp, in, 2);

				if ((i - 1) < HLIT)
					value = bitlen.data[i - 1];
				else
					value = bitlenD.data[i - HLIT - 1];
				/*repeat this value in the next lengths */
				for (n = 0; n < replength; n++) {
					if (i >= HLIT + HDIST) {
						error = 13;
						break;
					}	/*error: i is larger than the amount of codes */
					if (i < HLIT)
						bitlen.data[i] = value;
					else
						bitlenD.data[i - HLIT] = value;
					i++;
				}
			} else if (code == 17) {	/*repeat "0" 3-10 times */
				unsigned replength = 3;	/*read in the bits that indicate repeat length */
				if ((*bp) >> 3 >= inlength) {
					error = 50;
					break;
				}
				/*error, bit pointer jumps past memory */
				replength += read_bits(bp, in, 3);

				/*repeat this value in the next lengths */
				for (n = 0; n < replength; n++) {
					if (i >= HLIT + HDIST) {
						error = 14;
						break;
					}	/*error: i is larger than the amount of codes */
					if (i < HLIT)
						bitlen.data[i] = 0;
					else
						bitlenD.data[i - HLIT] = 0;
					i++;
				}
			} else if (code == 18) {	/*repeat "0" 11-138 times */
				unsigned replength = 11;	/*read in the bits that indicate repeat length */
				if ((*bp) >> 3 >= inlength) {
					error = 50;
					break;
				}	/*error, bit pointer jumps past memory */
				replength += read_bits(bp, in, 7);

				/*repeat this value in the next lengths */
				for (n = 0; n < replength; n++) {
					if (i >= HLIT + HDIST) {
						error = 15;
						break;
					}	/*error: i is larger than the amount of codes */
					if (i < HLIT)
						bitlen.data[i] = 0;
					else
						bitlenD.data[i - HLIT] = 0;
					i++;
				}
			} else {
				error = 16;
				break;
			}	/*error: somehow an unexisting code appeared. This can never happen. */
		}

	if (!error && bitlen.data[256] == 0) {
		error = 64;
	}

	/*the length of the end code 256 must be larger than 0 */
	/*now we've finally got HLIT and HDIST, so generate the code trees, and the function is done */
	if (!error)
		error = huffman_tree_create_lengths(codetree, &bitlen.data[0], bitlen.size, 15);
	if (!error)
		error = huffman_tree_create_lengths(codetreeD, &bitlenD.data[0], bitlenD.size, 15);

	uivector_cleanup(&bitlen);
	uivector_cleanup(&bitlenD);

	return error;
}

/*inflate a block with dynamic of fixed Huffman tree*/
static unsigned inflate_huffman(ucvector* out, const unsigned char *in, unsigned long *bp, unsigned long *pos, unsigned long inlength, unsigned btype)
{
	unsigned endreached = 0, error = 0;
	huffman_tree codetree;	/*287, the code tree for Huffman codes */
	huffman_tree codetreeD;	/*31, the code tree for distance codes */

	huffman_tree_init(&codetree);
	huffman_tree_init(&codetreeD);

	if (btype == 1)
		get_tree_inflate_fixed(&codetree, &codetreeD);
	else if (btype == 2) {
		huffman_tree codelengthcodetree;	/*18, the code tree for code length codes */
		huffman_tree_init(&codelengthcodetree);
		error = get_tree_inflate_dynamic(&codetree, &codetreeD, &codelengthcodetree, in, bp, inlength);
		huffman_tree_cleanup(&codelengthcodetree);
	}

	while (!endreached && !error) {
		unsigned code = huffman_decode_symbol(&error, in, bp, &codetree, inlength);
		if (error)
			break;	/*some error happened in the above function */
		if (code == 256)
			endreached = 1;	/*end code */
		else if (code <= 255) {	/*literal symbol */
			if ((*pos) >= out->size)
				ucvector_resize(out, ((*pos) + 1) * 2);	/*reserve more room at once */
			if ((*pos) >= out->size) {
				error = 9913;
				break;
			}	/*not enough memory */
			out->data[(*pos)] = (unsigned char)(code);
			(*pos)++;
		} else if (code >= FIRST_LENGTH_CODE_INDEX && code <= LAST_LENGTH_CODE_INDEX) {	/*length code */
			/*part 1: get length base */
			unsigned long length = LENGTHBASE[code - FIRST_LENGTH_CODE_INDEX];
			unsigned codeD, distance, numextrabitsD;
			unsigned long start, forward, backward, numextrabits;

			/*part 2: get extra bits and add the value of that to length */
			numextrabits = LENGTHEXTRA[code - FIRST_LENGTH_CODE_INDEX];
			if (((*bp) >> 3) >= inlength) {
				error = 51;
				break;
			}	/*error, bit pointer will jump past memory */
			length += read_bits(bp, in, numextrabits);

			/*part 3: get distance code */
			codeD = huffman_decode_symbol(&error, in, bp, &codetreeD, inlength);
			if (error)
				break;
			if (codeD > 29) {
				error = 18;
				break;
			}	/*error: invalid distance code (30-31 are never used) */
			distance = DISTANCEBASE[codeD];

			/*part 4: get extra bits from distance */
			numextrabitsD = DISTANCEEXTRA[codeD];
			if (((*bp) >> 3) >= inlength) {
				error = 51;
				break;
			}	/*error, bit pointer will jump past memory */
			distance += read_bits(bp, in, numextrabitsD);

			/*part 5: fill in all the out[n] values based on the length and dist */
			start = (*pos);
			backward = start - distance;
			if ((*pos) + length >= out->size)
				ucvector_resize(out, ((*pos) + length) * 2);	/*reserve more room at once */
			if ((*pos) + length >= out->size) {
				error = 9914;
				break;
			}
			/*not enough memory */
			for (forward = 0; forward < length; forward++) {
				out->data[(*pos)] = out->data[backward];
				(*pos)++;
				backward++;
				if (backward >= start)
					backward = start - distance;
			}
		}
	}

	huffman_tree_cleanup(&codetree);
	huffman_tree_cleanup(&codetreeD);

	return error;
}

static unsigned inflate_nocmp(ucvector* out, const unsigned char *in, unsigned long *bp, unsigned long *pos, unsigned long inlength)
{
	/*go to first boundary of byte */
	unsigned long p;
	unsigned LEN, NLEN, n, error = 0;
	while (((*bp) & 0x7) != 0)
		(*bp)++;
	p = (*bp) / 8;		/*byte position */

	/*read LEN (2 bytes) and NLEN (2 bytes) */
	if (p >= inlength - 4)
		return 52;	/*error, bit pointer will jump past memory */
	LEN = in[p] + 256 * in[p + 1];
	p += 2;
	NLEN = in[p] + 256 * in[p + 1];
	p += 2;

	/*check if 16-bit NLEN is really the one's complement of LEN */
	if (LEN + NLEN != 65535)
		return 21;	/*error: NLEN is not one's complement of LEN */

	if ((*pos) + LEN >= out->size) {
		if (ucvector_resize(out, (*pos) + LEN) != UPNG_EOK)
			return 9915;
	}

	/*read the literal data: LEN bytes are now stored in the out buffer */
	if (p + LEN > inlength)
		return 23;	/*error: reading outside of in buffer */
	for (n = 0; n < LEN; n++)
		out->data[(*pos)++] = in[p++];

	(*bp) = p * 8;

	return error;
}

/*inflate the deflated data (cfr. deflate spec); return value is the error*/
static upng_error uz_inflate_data(upng_info* info, ucvector* out, const unsigned char *in, unsigned long insize, unsigned long inpos)
{
	unsigned long bp = 0;	/*bit pointer in the "in" data, current byte is bp >> 3, current bit is bp & 0x7 (from lsb to msb of the byte) */
	unsigned long pos = 0;	/*byte position in the out buffer */

	unsigned done = 0;
	upng_error error;

	while (done == 0) {
		unsigned btype;

		/* ensure next bit doesn't point past the end of the buffer */
		if ((bp >> 3) >= insize) {
			SET_ERROR(info, UPNG_EMALFORMED);
			return info->error;
		}

		/* read block control bits */
		done = read_bit(&bp, &in[inpos]);
		btype = read_bit(&bp, &in[inpos]) | (read_bit(&bp, &in[inpos]) << 1);

		/* process control type appropriateyly */
		if (btype == 3) {
			SET_ERROR(info, UPNG_EMALFORMED);
			return info->error;
		} else if (btype == 0) {
			error = inflate_nocmp(out, &in[inpos], &bp, &pos, insize);	/*no compression */
		} else {
			error = inflate_huffman(out, &in[inpos], &bp, &pos, insize, btype);	/*compression, btype 01 or 10 */
		}

		/* stop if an error has occured */
		if (info->error != UPNG_EOK) {
			return info->error;
			return error;
		}
	}

	/* resize output buffer accordingly */
	error = ucvector_resize(out, pos);
	if (error != UPNG_EOK) {
		SET_ERROR(info, error);
	}

	return info->error;
}

upng_error uz_inflate(upng_info* info, unsigned char **out, unsigned long *outsize, const unsigned char *in, unsigned long insize)
{
	ucvector outv;

	/* we require two bytes for the zlib data header */
	if (insize < 2) {
		SET_ERROR(info, UPNG_EMALFORMED);
		return info->error;
	}

	/* 256 * in[0] + in[1] must be a multiple of 31, the FCHECK value is supposed to be made that way */
	if ((in[0] * 256 + in[1]) % 31 != 0) {
		SET_ERROR(info, UPNG_EMALFORMED);
		return info->error;
	}

	/*error: only compression method 8: inflate with sliding window of 32k is supported by the PNG spec */
	if ((in[0] & 15) != 8 || ((in[0] >> 4) & 15) > 7) {
		SET_ERROR(info, UPNG_EMALFORMED);
		return info->error;
	}

	/* the specification of PNG says about the zlib stream: "The additional flags shall not specify a preset dictionary." */
	if (((in[1] >> 5) & 1) != 0) {
		SET_ERROR(info, UPNG_EMALFORMED);
		return info->error;
	}

	/* create output buffer */
	ucvector_init_buffer(&outv, *out, *outsize);
	uz_inflate_data(info, &outv, in, insize, 2);

	/* store output */
	*out = outv.data;
	*outsize = outv.size;

	return info->error;
}
