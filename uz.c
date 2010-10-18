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
#include <string.h>

#include "upng.h"

#define FIRST_LENGTH_CODE_INDEX 257
#define LAST_LENGTH_CODE_INDEX 285
#define MAX_BIT_LENGTH 15 /* maximum bit length used in any huffman tree */
#define NUM_DEFLATE_CODE_SYMBOLS 288	/*256 literals, the end code, some length codes, and 2 unused codes */
#define NUM_CODE_LENGTH_CODES 19	/*the code length codes. 0-15: code lengths, 16: copy previous 3-6 times, 17: 3-10 zeros, 18: 11-138 zeros */
#define NUM_DISTANCE_SYMBOLS 32	/*the distance codes have their own symbols, 30 used, 2 unused */

#define DEFLATE_CODE_BUFFER_SIZE (NUM_DEFLATE_CODE_SYMBOLS * 4)
#define DISTANCE_BUFFER_SIZE (NUM_DISTANCE_SYMBOLS * 4)
#define CODE_LENGTH_BUFFER_SIZE (NUM_DISTANCE_SYMBOLS * 4)

#define SET_ERROR(upng,code) do { (upng)->error = (code); (upng)->error_line = __LINE__; } while (0)

typedef struct huffman_tree {
	unsigned* tree2d;
	unsigned* tree1d;
	unsigned* lengths; /* lengths of codes in 1d tree */
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

/* the buffer must be numcodes*4 in size! */
static void huffman_tree_init(huffman_tree* tree, unsigned* buffer, unsigned numcodes, unsigned maxbitlen)
{
	tree->tree1d = buffer;	/* first fourth of buffer */
	tree->lengths = buffer + numcodes; /* second fourth of buffer */
	tree->tree2d = buffer + numcodes * 2; /* second half of buffer */

	tree->numcodes = numcodes;
	tree->maxbitlen = maxbitlen;
}

/*given the code lengths (as stored in the PNG file), generate the tree as defined by Deflate. maxbitlen is the maximum bits that a code in the tree can have. return value is error.*/
static void huffman_tree_create_lengths(upng_t* upng, huffman_tree* tree, const unsigned *bitlen)
{
	unsigned blcount[MAX_BIT_LENGTH];
	unsigned nextcode[MAX_BIT_LENGTH];
	unsigned bits, n, i;
	unsigned nodefilled = 0;	/*up to which node it is filled */
	unsigned treepos = 0;	/*position in the tree (1 of the numcodes columns) */

	/* initialize local vectors */
	memset(blcount, 0, sizeof(blcount));
	memset(nextcode, 0, sizeof(nextcode));

	/* copy bitlen into tree */
	memcpy(tree->lengths, bitlen, tree->numcodes * sizeof(unsigned));

	/*step 1: count number of instances of each code length */
	for (bits = 0; bits < tree->numcodes; bits++) {
		blcount[tree->lengths[bits]]++;
	}

	/*step 2: generate the nextcode values */
	for (bits = 1; bits <= tree->maxbitlen; bits++) {
		nextcode[bits] = (nextcode[bits - 1] + blcount[bits - 1]) << 1;
	}

	/*step 3: generate all the codes */
	for (n = 0; n < tree->numcodes; n++) {
		if (tree->lengths[n] != 0) {
			tree->tree1d[n] = nextcode[tree->lengths[n]]++;
		}
	}

	/*convert tree1d[] to tree2d[][]. In the 2D array, a value of 32767 means uninited, a value >= numcodes is an address to another bit, a value < numcodes is a code. The 2 rows are the 2 possible bit values (0 or 1), there are as many columns as codes - 1
	   a good huffmann tree has N * 2 - 1 nodes, of which N - 1 are internal nodes. Here, the internal nodes are stored (what their 0 and 1 option point to). There is only memory for such good tree currently, if there are more nodes (due to too long length codes), error 55 will happen */
	for (n = 0; n < tree->numcodes * 2; n++) {
		tree->tree2d[n] = 32767;	/*32767 here means the tree2d isn't filled there yet */
	}

	for (n = 0; n < tree->numcodes; n++) {	/*the codes */
		for (i = 0; i < tree->lengths[n]; i++) {	/*the bits for this code */
			unsigned char bit = (unsigned char)((tree->tree1d[n] >> (tree->lengths[n] - i - 1)) & 1);
			/* check if oversubscribed */
			if (treepos > tree->numcodes - 2) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				return;
			}

			if (tree->tree2d[2 * treepos + bit] == 32767) {	/*not yet filled in */
				if (i + 1 == tree->lengths[n]) {	/*last bit */
					tree->tree2d[2 * treepos + bit] = n;	/*put the current code in it */
					treepos = 0;
				} else {	/*put address of the next step in here, first that address has to be found of course (it's just nodefilled + 1)... */
					nodefilled++;
					tree->tree2d[2 * treepos + bit] = nodefilled + tree->numcodes;	/*addresses encoded with numcodes added to it */
					treepos = nodefilled;
				}
			} else {
				treepos = tree->tree2d[2 * treepos + bit] - tree->numcodes;
			}
		}
	}

	for (n = 0; n < tree->numcodes * 2; n++) {
		if (tree->tree2d[n] == 32767) {
			tree->tree2d[n] = 0;	/*remove possible remaining 32767's */
		}
	}
}

/*get the tree of a deflated block with fixed tree, as specified in the deflate specification*/
static void generate_fixed_tree(upng_t* upng, huffman_tree* tree)
{
	unsigned bitlen[NUM_DEFLATE_CODE_SYMBOLS];
	unsigned i;

	/*288 possible codes: 0-255=literals, 256=endcode, 257-285=lengthcodes, 286-287=unused */
	for (i = 0; i <= 143; i++)
		bitlen[i] = 8;
	for (i = 144; i <= 255; i++)
		bitlen[i] = 9;
	for (i = 256; i <= 279; i++)
		bitlen[i] = 7;
	for (i = 280; i <= 287; i++)
		bitlen[i] = 8;

	huffman_tree_create_lengths(upng, tree, bitlen);
}

static void generate_distance_tree(upng_t* upng, huffman_tree* tree)
{
	unsigned bitlen[NUM_DISTANCE_SYMBOLS];
	unsigned i;

	/*there are 32 distance codes, but 30-31 are unused */
	for (i = 0; i < NUM_DISTANCE_SYMBOLS; i++) {
		bitlen[i] = 5;
	}

	huffman_tree_create_lengths(upng, tree, bitlen);
}

/*Decodes a symbol from the tree
if decoded is true, then result contains the symbol, otherwise it contains something unspecified (because the symbol isn't fully decoded yet)
bit is the bit that was just read from the stream
you have to decode a full symbol (let the decode function return true) before you can try to decode another one, otherwise the state isn't reset
return value is error.*/
static void huffman_tree_decode(upng_t* upng, const huffman_tree* tree, unsigned *decoded, unsigned *result, unsigned *treepos, unsigned char bit)
{
	/* error: it appeared outside the codetree */
	if ((*treepos) >= tree->numcodes) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return;
	}

	(*result) = tree->tree2d[2 * (*treepos) + bit];
	(*decoded) = ((*result) < tree->numcodes);

	if (*decoded) {
		(*treepos) = 0;
	} else {
		(*treepos) = (*result) - tree->numcodes;
	}
}

static unsigned huffman_decode_symbol(upng_t *upng, const unsigned char *in, unsigned long *bp, const huffman_tree* codetree, unsigned long inlength)
{
	unsigned treepos = 0, decoded, ct;
	for (;;) {
		unsigned char bit;

		/* error: end of input memory reached without endcode */
		if (((*bp) & 0x07) == 0 && ((*bp) >> 3) > inlength) {
			SET_ERROR(upng, UPNG_EMALFORMED);
			return 0;
		}

		bit = read_bit(bp, in);
		huffman_tree_decode(upng, codetree, &decoded, &ct, &treepos, bit);
		if (upng->error != UPNG_EOK) {
			return 0;
		}

		if (decoded) {
			return ct;
		}
	}
}

/*get the tree of a deflated block with fixed tree, as specified in the deflate specification*/
static void get_tree_inflate_fixed(upng_t* upng, huffman_tree* tree, huffman_tree* treeD)
{
	/*error checking not done, this is fixed stuff, it works, it doesn't depend on the image */
	generate_fixed_tree(upng, tree);
	generate_distance_tree(upng, treeD);
}

/* get the tree of a deflated block with dynamic tree, the tree itself is also Huffman compressed with a known tree*/
static void get_tree_inflate_dynamic(upng_t* upng, huffman_tree* codetree, huffman_tree* codetreeD, huffman_tree* codelengthcodetree, const unsigned char *in, unsigned long *bp, unsigned long inlength)
{
	unsigned codelengthcode[NUM_CODE_LENGTH_CODES];
	unsigned bitlen[NUM_DEFLATE_CODE_SYMBOLS];
	unsigned bitlenD[NUM_DISTANCE_SYMBOLS];
	unsigned n, HLIT, HDIST, HCLEN, i;

	/*make sure that length values that aren't filled in will be 0, or a wrong tree will be generated */
	/*C-code note: use no "return" between ctor and dtor of an uivector! */
	if ((*bp) >> 3 >= inlength - 2) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return;
	}

	/* clear bitlen arrays */
	memset(bitlen, 0, sizeof(bitlen));
	memset(bitlenD, 0, sizeof(bitlenD));

	/*the bit pointer is or will go past the memory */
	HLIT = read_bits(bp, in, 5) + 257;	/*number of literal/length codes + 257. Unlike the spec, the value 257 is added to it here already */
	HDIST = read_bits(bp, in, 5) + 1;	/*number of distance codes. Unlike the spec, the value 1 is added to it here already */
	HCLEN = read_bits(bp, in, 4) + 4;	/*number of code length codes. Unlike the spec, the value 4 is added to it here already */

	for (i = 0; i < NUM_CODE_LENGTH_CODES; i++) {
		if (i < HCLEN) {
			codelengthcode[CLCL[i]] = read_bits(bp, in, 3);
		} else {
			codelengthcode[CLCL[i]] = 0;	/*if not, it must stay 0 */
		}
	}

	huffman_tree_create_lengths(upng, codelengthcodetree, codelengthcode);

	/* bail now if we encountered an error earlier */
	if (upng->error != UPNG_EOK) {
		return;
	}

	/*now we can use this tree to read the lengths for the tree that this function will return */
	i = 0;
	while (i < HLIT + HDIST) {	/*i is the current symbol we're reading in the part that contains the code lengths of lit/len codes and dist codes */
		unsigned code = huffman_decode_symbol(upng, in, bp, codelengthcodetree, inlength);
		if (upng->error != UPNG_EOK) {
			break;
		}

		if (code <= 15) {	/*a length code */
			if (i < HLIT) {
				bitlen[i] = code;
			} else {
				bitlenD[i - HLIT] = code;
			}
			i++;
		} else if (code == 16) {	/*repeat previous */
			unsigned replength = 3;	/*read in the 2 bits that indicate repeat length (3-6) */
			unsigned value;	/*set value to the previous code */

			if ((*bp) >> 3 >= inlength) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				break;
			}
			/*error, bit pointer jumps past memory */
			replength += read_bits(bp, in, 2);

			if ((i - 1) < HLIT) {
				value = bitlen[i - 1];
			} else {
				value = bitlenD[i - HLIT - 1];
			}

			/*repeat this value in the next lengths */
			for (n = 0; n < replength; n++) {
				/* i is larger than the amount of codes */
				if (i >= HLIT + HDIST) {
					SET_ERROR(upng, UPNG_EMALFORMED);
					break;
				}

				if (i < HLIT) {
					bitlen[i] = value;
				} else {
					bitlenD[i - HLIT] = value;
				}
				i++;
			}
		} else if (code == 17) {	/*repeat "0" 3-10 times */
			unsigned replength = 3;	/*read in the bits that indicate repeat length */
			if ((*bp) >> 3 >= inlength) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				break;
			}

			/*error, bit pointer jumps past memory */
			replength += read_bits(bp, in, 3);

			/*repeat this value in the next lengths */
			for (n = 0; n < replength; n++) {
				/* error: i is larger than the amount of codes */
				if (i >= HLIT + HDIST) {
					SET_ERROR(upng, UPNG_EMALFORMED);
					break;
				}

				if (i < HLIT) {
					bitlen[i] = 0;
				} else {
					bitlenD[i - HLIT] = 0;
				}
				i++;
			}
		} else if (code == 18) {	/*repeat "0" 11-138 times */
			unsigned replength = 11;	/*read in the bits that indicate repeat length */
			/* error, bit pointer jumps past memory */
			if ((*bp) >> 3 >= inlength) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				break;
			}

			replength += read_bits(bp, in, 7);

			/*repeat this value in the next lengths */
			for (n = 0; n < replength; n++) {
				/* i is larger than the amount of codes */
				if (i >= HLIT + HDIST) {
					SET_ERROR(upng, UPNG_EMALFORMED);
					break;
				}
				if (i < HLIT)
					bitlen[i] = 0;
				else
					bitlenD[i - HLIT] = 0;
				i++;
			}
		} else {
			/* somehow an unexisting code appeared. This can never happen. */
			SET_ERROR(upng, UPNG_EMALFORMED);
			break;
		}
	}

	if (upng->error == UPNG_EOK && bitlen[256] == 0) {
		SET_ERROR(upng, UPNG_EMALFORMED);
	}

	/*the length of the end code 256 must be larger than 0 */
	/*now we've finally got HLIT and HDIST, so generate the code trees, and the function is done */
	if (upng->error == UPNG_EOK) {
		huffman_tree_create_lengths(upng, codetree, bitlen);
	}
	if (upng->error == UPNG_EOK) {
		huffman_tree_create_lengths(upng, codetreeD, bitlenD);
	}
}

/*inflate a block with dynamic of fixed Huffman tree*/
static void inflate_huffman(upng_t* upng, unsigned char* out, unsigned long outsize, const unsigned char *in, unsigned long *bp, unsigned long *pos, unsigned long inlength, unsigned btype)
{
	unsigned codetree_buffer[DEFLATE_CODE_BUFFER_SIZE];
	unsigned codetreeD_buffer[DISTANCE_BUFFER_SIZE];
	unsigned done = 0;

	huffman_tree codetree;	/*287, the code tree for Huffman codes */
	huffman_tree codetreeD;	/*31, the code tree for distance codes */

	huffman_tree_init(&codetree, codetree_buffer, NUM_DEFLATE_CODE_SYMBOLS, 15);
	huffman_tree_init(&codetreeD, codetreeD_buffer, NUM_DISTANCE_SYMBOLS, 15);

	if (btype == 1) {
		get_tree_inflate_fixed(upng, &codetree, &codetreeD);
	} else if (btype == 2) {
		unsigned codelengthcodetree_buffer[CODE_LENGTH_BUFFER_SIZE];
		huffman_tree codelengthcodetree;	/*18, the code tree for code length codes */

		huffman_tree_init(&codelengthcodetree, codelengthcodetree_buffer, NUM_CODE_LENGTH_CODES, 7);
		get_tree_inflate_dynamic(upng, &codetree, &codetreeD, &codelengthcodetree, in, bp, inlength);
	}

	while (done == 0 && upng->error == UPNG_EOK) {
		unsigned code = huffman_decode_symbol(upng, in, bp, &codetree, inlength);
		if (upng->error != UPNG_EOK) {
			return;
		}

		if (code == 256) {
			/* end code */
			done = 1;
		} else if (code <= 255) {
			/* literal symbol */
			if ((*pos) >= outsize) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				return;
			}

			/* store output */
			out[(*pos)++] = (unsigned char)(code);
		} else if (code >= FIRST_LENGTH_CODE_INDEX && code <= LAST_LENGTH_CODE_INDEX) {	/*length code */
			/* part 1: get length base */
			unsigned long length = LENGTHBASE[code - FIRST_LENGTH_CODE_INDEX];
			unsigned codeD, distance, numextrabitsD;
			unsigned long start, forward, backward, numextrabits;

			/* part 2: get extra bits and add the value of that to length */
			numextrabits = LENGTHEXTRA[code - FIRST_LENGTH_CODE_INDEX];

			/* error, bit pointer will jump past memory */
			if (((*bp) >> 3) >= inlength) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				return;
			}
			length += read_bits(bp, in, numextrabits);

			/*part 3: get distance code */
			codeD = huffman_decode_symbol(upng, in, bp, &codetreeD, inlength);
			if (upng->error != UPNG_EOK) {
				return;
			}

			/* invalid distance code (30-31 are never used) */
			if (codeD > 29) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				return;
			}

			distance = DISTANCEBASE[codeD];

			/*part 4: get extra bits from distance */
			numextrabitsD = DISTANCEEXTRA[codeD];

			/* error, bit pointer will jump past memory */
			if (((*bp) >> 3) >= inlength) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				return;
			}

			distance += read_bits(bp, in, numextrabitsD);

			/*part 5: fill in all the out[n] values based on the length and dist */
			start = (*pos);
			backward = start - distance;

			if ((*pos) + length >= outsize) {
				SET_ERROR(upng, UPNG_EMALFORMED);
				return;
			}

			for (forward = 0; forward < length; forward++) {
				out[(*pos)++] = out[backward];
				backward++;

				if (backward >= start) {
					backward = start - distance;
				}
			}
		}
	}
}

static void inflate_nocmp(upng_t* upng, unsigned char* out, unsigned long outsize, const unsigned char *in, unsigned long *bp, unsigned long *pos, unsigned long inlength)
{
	unsigned long p;
	unsigned len, nlen, n;

	/* go to first boundary of byte */
	while (((*bp) & 0x7) != 0) {
		(*bp)++;
	}
	p = (*bp) / 8;		/*byte position */

	/* read len (2 bytes) and nlen (2 bytes) */
	if (p >= inlength - 4) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return;
	}

	len = in[p] + 256 * in[p + 1];
	p += 2;
	nlen = in[p] + 256 * in[p + 1];
	p += 2;

	/* check if 16-bit nlen is really the one's complement of len */
	if (len + nlen != 65535) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return;
	}

	if ((*pos) + len >= outsize) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return;
	}

	/* read the literal data: len bytes are now stored in the out buffer */
	if (p + len > inlength) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return;
	}

	for (n = 0; n < len; n++) {
		out[(*pos)++] = in[p++];
	}

	(*bp) = p * 8;
}

/*inflate the deflated data (cfr. deflate spec); return value is the error*/
static upng_error uz_inflate_data(upng_t* upng, unsigned char* out, unsigned long outsize, const unsigned char *in, unsigned long insize, unsigned long inpos)
{
	unsigned long bp = 0;	/*bit pointer in the "in" data, current byte is bp >> 3, current bit is bp & 0x7 (from lsb to msb of the byte) */
	unsigned long pos = 0;	/*byte position in the out buffer */

	unsigned done = 0;
	upng_error error;

	while (done == 0) {
		unsigned btype;

		/* ensure next bit doesn't point past the end of the buffer */
		if ((bp >> 3) >= insize) {
			SET_ERROR(upng, UPNG_EMALFORMED);
			return upng->error;
		}

		/* read block control bits */
		done = read_bit(&bp, &in[inpos]);
		btype = read_bit(&bp, &in[inpos]) | (read_bit(&bp, &in[inpos]) << 1);

		/* process control type appropriateyly */
		if (btype == 3) {
			SET_ERROR(upng, UPNG_EMALFORMED);
			return upng->error;
		} else if (btype == 0) {
			inflate_nocmp(upng, out, outsize, &in[inpos], &bp, &pos, insize);	/*no compression */
		} else {
			inflate_huffman(upng, out, outsize, &in[inpos], &bp, &pos, insize, btype);	/*compression, btype 01 or 10 */
		}

		/* stop if an error has occured */
		if (upng->error != UPNG_EOK) {
			return upng->error;
			return error;
		}
	}

	return upng->error;
}

upng_error uz_inflate(upng_t* upng, unsigned char *out, unsigned long outsize, const unsigned char *in, unsigned long insize)
{
	/* we require two bytes for the zlib data header */
	if (insize < 2) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return upng->error;
	}

	/* 256 * in[0] + in[1] must be a multiple of 31, the FCHECK value is supposed to be made that way */
	if ((in[0] * 256 + in[1]) % 31 != 0) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return upng->error;
	}

	/*error: only compression method 8: inflate with sliding window of 32k is supported by the PNG spec */
	if ((in[0] & 15) != 8 || ((in[0] >> 4) & 15) > 7) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return upng->error;
	}

	/* the specification of PNG says about the zlib stream: "The additional flags shall not specify a preset dictionary." */
	if (((in[1] >> 5) & 1) != 0) {
		SET_ERROR(upng, UPNG_EMALFORMED);
		return upng->error;
	}

	/* create output buffer */
	uz_inflate_data(upng, out, outsize, in, insize, 2);

	return upng->error;
}
