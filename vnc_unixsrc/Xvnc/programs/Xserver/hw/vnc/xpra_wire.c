/*
 * xpra_wire.c - the Xpra wire format: packet framing, the "rencodeplus"
 * value encoding and LZ4 block compression.  See xpra_wire.h.
 *
 * Everything a client sends goes through xwParseHeader(), xwLz4Decompress()
 * and xwDecode() before anything else looks at it, inside the X server.
 * They take any bytes at all: every length is checked against what is left
 * of the input and of the output, nesting and tree size are bounded.
 */

/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: 2026 D. Kevin Cameron
 *
 * This is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  It is distributed WITHOUT ANY WARRANTY; see
 * the file LICENCE.TXT.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "xpra_wire.h"

/****************************************************************************/
/* buffers */

void
xwBufInit(XwBuf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->failed = 0;
}

void
xwBufFree(XwBuf *b)
{
    free(b->data);
    xwBufInit(b);
}

void
xwBufReset(XwBuf *b)
{
    b->len = 0;
    b->failed = 0;
}

int
xwBufReserve(XwBuf *b, size_t extra)
{
    size_t need, cap;
    unsigned char *p;

    if (b->failed)
	return 0;
    if (extra > (size_t)-1 - b->len)
	goto fail;
    need = b->len + extra;
    if (need <= b->cap)
	return 1;
    cap = b->cap ? b->cap : 256;
    while (cap < need) {
	if (cap > (size_t)-1 / 2) {
	    cap = need;
	    break;
	}
	cap *= 2;
    }
    p = (unsigned char *)realloc(b->data, cap);
    if (!p)
	goto fail;
    b->data = p;
    b->cap = cap;
    return 1;

 fail:
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->failed = 1;
    return 0;
}

void
xwBufAppend(XwBuf *b, const void *p, size_t n)
{
    if (n == 0 || !xwBufReserve(b, n))
	return;
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

void
xwBufConsume(XwBuf *b, size_t n)
{
    if (n >= b->len) {
	b->len = 0;
	return;
    }
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
}

static void
putByte(XwBuf *b, int c)
{
    unsigned char ch = (unsigned char)c;

    xwBufAppend(b, &ch, 1);
}

/****************************************************************************/
/* packet header */

int
xwParseHeader(const unsigned char *p, XwHeader *h)
{
    if (p[0] != 'P')
	return -1;
    h->flags = p[1];
    h->level = p[2];
    h->index = p[3];
    h->size = ((unsigned long)p[4] << 24) | ((unsigned long)p[5] << 16) |
	((unsigned long)p[6] << 8) | (unsigned long)p[7];
    if (h->index >= XW_MAX_CHUNKS)
	return -1;
    return 0;
}

void
xwPutHeader(unsigned char *p, int flags, int level, int index,
	    unsigned long size)
{
    p[0] = 'P';
    p[1] = (unsigned char)flags;
    p[2] = (unsigned char)level;
    p[3] = (unsigned char)index;
    p[4] = (unsigned char)(size >> 24);
    p[5] = (unsigned char)(size >> 16);
    p[6] = (unsigned char)(size >> 8);
    p[7] = (unsigned char)size;
}

/****************************************************************************/
/*
 * rencodeplus.  One type byte, then:
 *
 *    0..43      that integer
 *    44         a double, 8 bytes big endian
 *    '0'..'9'   decimal length, then ':' and a string or '/' and bytes
 *    59         a list, items until 127
 *    60         a dictionary, key and value pairs until 127
 *    61         an integer in decimal, until 127
 *    62..65     an integer of 1, 2, 4, 8 bytes, big endian
 *    66         a float, 4 bytes big endian
 *    67 68 69   true, false, none
 *    70..101    the integers -1 to -32
 *    102..126   a dictionary of 0 to 24 pairs
 *    128..191   a string of 0 to 63 bytes
 *    192..255   a list of 0 to 63 items
 */

#define CHR_FLOAT64	44
#define CHR_LIST	59
#define CHR_DICT	60
#define CHR_INT		61
#define CHR_INT1	62
#define CHR_INT2	63
#define CHR_INT4	64
#define CHR_INT8	65
#define CHR_FLOAT32	66
#define CHR_TRUE	67
#define CHR_FALSE	68
#define CHR_NONE	69
#define CHR_TERM	127

#define INT_POS_FIXED_COUNT	44
#define INT_NEG_FIXED_START	70
#define INT_NEG_FIXED_COUNT	32
#define DICT_FIXED_START	102
#define DICT_FIXED_COUNT	25
#define STR_FIXED_START		128
#define STR_FIXED_COUNT		64
#define LIST_FIXED_START	192

void
xwPutInt(XwBuf *b, long long v)
{
    unsigned char t[9];
    int n, i;

    if (v >= 0 && v < INT_POS_FIXED_COUNT) {
	putByte(b, (int)v);
	return;
    }
    if (v < 0 && v >= -INT_NEG_FIXED_COUNT) {
	putByte(b, INT_NEG_FIXED_START + (int)(-1 - v));
	return;
    }
    if (v >= -128 && v <= 127) {
	t[0] = CHR_INT1;
	n = 1;
    } else if (v >= -32768 && v <= 32767) {
	t[0] = CHR_INT2;
	n = 2;
    } else if (v >= -2147483647LL - 1 && v <= 2147483647LL) {
	t[0] = CHR_INT4;
	n = 4;
    } else {
	t[0] = CHR_INT8;
	n = 8;
    }
    for (i = 0; i < n; i++)
	t[1 + i] = (unsigned char)((unsigned long long)v >> (8 * (n - 1 - i)));
    xwBufAppend(b, t, (size_t)n + 1);
}

void
xwPutBool(XwBuf *b, int v)
{
    putByte(b, v ? CHR_TRUE : CHR_FALSE);
}

void
xwPutStrN(XwBuf *b, const char *s, size_t n)
{
    char len[24];

    if (n < STR_FIXED_COUNT) {
	putByte(b, STR_FIXED_START + (int)n);
    } else {
	sprintf(len, "%lu:", (unsigned long)n);
	xwBufAppend(b, len, strlen(len));
    }
    xwBufAppend(b, s, n);
}

void
xwPutStr(XwBuf *b, const char *s)
{
    if (!s)
	s = "";
    xwPutStrN(b, s, strlen(s));
}

void
xwPutBytes(XwBuf *b, const void *p, size_t n)
{
    char len[24];

    sprintf(len, "%lu/", (unsigned long)n);
    xwBufAppend(b, len, strlen(len));
    xwBufAppend(b, p, n);
}

void
xwListOpen(XwBuf *b)
{
    putByte(b, CHR_LIST);
}

void
xwDictOpen(XwBuf *b)
{
    putByte(b, CHR_DICT);
}

void
xwClose(XwBuf *b)
{
    putByte(b, CHR_TERM);
}

void
xwKeyInt(XwBuf *b, const char *key, long long v)
{
    xwPutStr(b, key);
    xwPutInt(b, v);
}

void
xwKeyBool(XwBuf *b, const char *key, int v)
{
    xwPutStr(b, key);
    xwPutBool(b, v);
}

void
xwKeyStr(XwBuf *b, const char *key, const char *s)
{
    xwPutStr(b, key);
    xwPutStr(b, s);
}

void
xwKeyPair(XwBuf *b, const char *key, long long v1, long long v2)
{
    xwPutStr(b, key);
    xwListOpen(b);
    xwPutInt(b, v1);
    xwPutInt(b, v2);
    xwClose(b);
}

/****************************************************************************/
/* decoding */

#define ARENA_CHUNK	512		/* values per block */
#define ARENA_MAX_NODES	400000		/* a tree bigger than this is refused */
#define DECODE_MAX_DEPTH 48

typedef struct _XwBlock {
    struct _XwBlock *next;
    XwValue v[ARENA_CHUNK];
} XwBlock;

struct _XwArena {
    XwBlock *blocks;		/* all blocks, the one in use first */
    XwBlock *spare;		/* blocks kept by xwArenaReset */
    int used;			/* values used in blocks */
    long total;
};

XwArena *
xwArenaNew(void)
{
    XwArena *a = (XwArena *)malloc(sizeof(XwArena));

    if (a) {
	a->blocks = NULL;
	a->spare = NULL;
	a->used = ARENA_CHUNK;
	a->total = 0;
    }
    return a;
}

void
xwArenaReset(XwArena *a)
{
    XwBlock *blk, *next;

    /* keep one block for the next packet, give the others back */
    for (blk = a->blocks; blk; blk = next) {
	next = blk->next;
	if (!a->spare) {
	    blk->next = NULL;
	    a->spare = blk;
	} else {
	    free(blk);
	}
    }
    a->blocks = NULL;
    a->used = ARENA_CHUNK;
    a->total = 0;
}

void
xwArenaFree(XwArena *a)
{
    if (!a)
	return;
    xwArenaReset(a);
    free(a->spare);
    free(a);
}

static XwValue *
newValue(XwArena *a, int type)
{
    XwValue *v;

    if (a->used >= ARENA_CHUNK) {
	XwBlock *blk;

	if (a->total >= ARENA_MAX_NODES)
	    return NULL;
	if (a->spare) {
	    blk = a->spare;
	    a->spare = NULL;
	} else {
	    blk = (XwBlock *)malloc(sizeof(XwBlock));
	    if (!blk)
		return NULL;
	}
	blk->next = a->blocks;
	a->blocks = blk;
	a->used = 0;
    }
    v = &a->blocks->v[a->used++];
    a->total++;
    v->type = type;
    v->i = 0;
    v->f = 0.0;
    v->s = NULL;
    v->n = 0;
    v->first = NULL;
    v->next = NULL;
    return v;
}

static long long
readBE(const unsigned char *p, int n)
{
    unsigned long long u = 0;
    int i;

    for (i = 0; i < n; i++)
	u = (u << 8) | p[i];
    /* sign extend */
    if (n < 8 && (u & (1ULL << (8 * n - 1))))
	u |= ~0ULL << (8 * n);
    return (long long)u;
}

static double
readFloat(const unsigned char *p, int n)
{
    static const int one = 1;
    int little = *(const char *)&one;
    unsigned char t[8];
    int i;

    for (i = 0; i < n; i++)
	t[i] = little ? p[n - 1 - i] : p[i];
    if (n == 4) {
	float f;

	memcpy(&f, t, 4);
	return (double)f;
    } else {
	double d;

	memcpy(&d, t, 8);
	return d;
    }
}

static XwValue *decodeValue(XwArena *a, const unsigned char **pp,
			    const unsigned char *end, int depth);

/*
 * The children of a list or dictionary: "count" of them if count >= 0,
 * otherwise as many as there are before the terminator.
 */

static int
decodeChildren(XwArena *a, XwValue *parent, long count,
	       const unsigned char **pp, const unsigned char *end, int depth)
{
    XwValue *tail = NULL, *child;
    long n = 0;

    for (;;) {
	if (count >= 0) {
	    if (n == count)
		break;
	} else {
	    if (*pp >= end)
		return 0;
	    if (**pp == CHR_TERM) {
		(*pp)++;
		break;
	    }
	}
	child = decodeValue(a, pp, end, depth + 1);
	if (!child)
	    return 0;
	if (tail)
	    tail->next = child;
	else
	    parent->first = child;
	tail = child;
	n++;
    }
    if (parent->type == XW_DICT) {
	if (n & 1)
	    return 0;
	n /= 2;
    }
    parent->n = (size_t)n;
    return 1;
}

static XwValue *
decodeValue(XwArena *a, const unsigned char **pp, const unsigned char *end,
	    int depth)
{
    const unsigned char *p = *pp;
    XwValue *v;
    int t;

    if (depth > DECODE_MAX_DEPTH || p >= end)
	return NULL;
    t = *p++;

    if (t < INT_POS_FIXED_COUNT) {
	if (!(v = newValue(a, XW_INT)))
	    return NULL;
	v->i = t;
    } else if (t >= '0' && t <= '9') {
	size_t len = (size_t)(t - '0');
	int digits = 1;

	while (p < end && *p >= '0' && *p <= '9') {
	    if (++digits > 10)
		return NULL;
	    len = len * 10 + (size_t)(*p++ - '0');
	}
	if (p >= end || (*p != ':' && *p != '/'))
	    return NULL;
	if (!(v = newValue(a, *p == ':' ? XW_STR : XW_BYTES)))
	    return NULL;
	p++;
	if (len > (size_t)(end - p))
	    return NULL;
	v->s = p;
	v->n = len;
	p += len;
    } else if (t >= STR_FIXED_START && t < STR_FIXED_START + STR_FIXED_COUNT) {
	size_t len = (size_t)(t - STR_FIXED_START);

	if (len > (size_t)(end - p))
	    return NULL;
	if (!(v = newValue(a, XW_STR)))
	    return NULL;
	v->s = p;
	v->n = len;
	p += len;
    } else if (t >= LIST_FIXED_START) {
	if (!(v = newValue(a, XW_LIST)))
	    return NULL;
	if (!decodeChildren(a, v, (long)(t - LIST_FIXED_START), &p, end, depth))
	    return NULL;
    } else if (t >= DICT_FIXED_START && t < DICT_FIXED_START + DICT_FIXED_COUNT) {
	if (!(v = newValue(a, XW_DICT)))
	    return NULL;
	if (!decodeChildren(a, v, 2L * (t - DICT_FIXED_START), &p, end, depth))
	    return NULL;
    } else if (t >= INT_NEG_FIXED_START &&
	       t < INT_NEG_FIXED_START + INT_NEG_FIXED_COUNT) {
	if (!(v = newValue(a, XW_INT)))
	    return NULL;
	v->i = -1 - (t - INT_NEG_FIXED_START);
    } else {
	switch (t) {
	case CHR_LIST:
	case CHR_DICT:
	    if (!(v = newValue(a, t == CHR_LIST ? XW_LIST : XW_DICT)))
		return NULL;
	    if (!decodeChildren(a, v, -1L, &p, end, depth))
		return NULL;
	    break;
	case CHR_INT1:
	case CHR_INT2:
	case CHR_INT4:
	case CHR_INT8:
	{
	    int n = 1 << (t - CHR_INT1);

	    if (n > end - p)
		return NULL;
	    if (!(v = newValue(a, XW_INT)))
		return NULL;
	    v->i = readBE(p, n);
	    p += n;
	    break;
	}
	case CHR_INT:
	{
	    /* decimal digits: too many of them saturate */
	    unsigned long long u = 0;
	    int neg = 0, sat = 0, digits = 0;

	    if (p < end && *p == '-') {
		neg = 1;
		p++;
	    }
	    while (p < end && *p >= '0' && *p <= '9') {
		if (u > (0x7fffffffffffffffULL - 9) / 10)
		    sat = 1;
		else
		    u = u * 10 + (unsigned long long)(*p - '0');
		p++;
		if (++digits > 4096)
		    return NULL;
	    }
	    if (p >= end || *p != CHR_TERM || digits == 0)
		return NULL;
	    p++;
	    if (!(v = newValue(a, XW_INT)))
		return NULL;
	    if (sat)
		u = 0x7fffffffffffffffULL;
	    v->i = neg ? -(long long)u : (long long)u;
	    break;
	}
	case CHR_FLOAT32:
	case CHR_FLOAT64:
	{
	    int n = t == CHR_FLOAT32 ? 4 : 8;

	    if (n > end - p)
		return NULL;
	    if (!(v = newValue(a, XW_FLOAT)))
		return NULL;
	    v->f = readFloat(p, n);
	    p += n;
	    break;
	}
	case CHR_TRUE:
	case CHR_FALSE:
	    if (!(v = newValue(a, XW_BOOL)))
		return NULL;
	    v->i = t == CHR_TRUE;
	    break;
	case CHR_NONE:
	    if (!(v = newValue(a, XW_NONE)))
		return NULL;
	    break;
	default:
	    return NULL;
	}
    }
    *pp = p;
    return v;
}

XwValue *
xwDecode(XwArena *a, const unsigned char *data, size_t len)
{
    const unsigned char *p = data;
    XwValue *v;

    if (!a)
	return NULL;
    v = decodeValue(a, &p, data + len, 0);
    if (!v || p != data + len)
	return NULL;
    return v;
}

XwValue *
xwItem(const XwValue *list, int index)
{
    XwValue *v;

    if (!list || list->type != XW_LIST || index < 0)
	return NULL;
    for (v = list->first; v && index > 0; v = v->next)
	index--;
    return v;
}

XwValue *
xwGet(const XwValue *dict, const char *key)
{
    XwValue *k;
    size_t n = strlen(key);

    if (!dict || dict->type != XW_DICT)
	return NULL;
    for (k = dict->first; k && k->next; k = k->next->next) {
	if ((k->type == XW_STR || k->type == XW_BYTES) && k->n == n &&
	    memcmp(k->s, key, n) == 0)
	    return k->next;
    }
    return NULL;
}

int
xwIsStr(const XwValue *v, const char *s)
{
    size_t n = strlen(s);

    return v && (v->type == XW_STR || v->type == XW_BYTES) && v->n == n &&
	memcmp(v->s, s, n) == 0;
}

long long
xwInt(const XwValue *v, long long dflt)
{
    if (!v)
	return dflt;
    if (v->type == XW_INT || v->type == XW_BOOL)
	return v->i;
    if (v->type == XW_FLOAT) {
	if (v->f >= -9.0e18 && v->f <= 9.0e18)
	    return (long long)v->f;
    }
    return dflt;
}

int
xwBool(const XwValue *v, int dflt)
{
    if (v && (v->type == XW_INT || v->type == XW_BOOL))
	return v->i != 0;
    return dflt;
}

char *
xwStr(const XwValue *v, char *buf, size_t buflen)
{
    size_t n = 0;

    if (buflen == 0)
	return buf;
    if (v && (v->type == XW_STR || v->type == XW_BYTES)) {
	n = v->n < buflen - 1 ? v->n : buflen - 1;
	memcpy(buf, v->s, n);
    }
    buf[n] = '\0';
    return buf;
}

int
xwListHas(const XwValue *list, const char *s)
{
    XwValue *v;

    if (!list || list->type != XW_LIST)
	return 0;
    for (v = list->first; v; v = v->next) {
	if (xwIsStr(v, s))
	    return 1;
    }
    return 0;
}

/****************************************************************************/
/*
 * LZ4 block format.  A block is a series of sequences: a token (literal
 * length in the high four bits, match length less 4 in the low four, 15
 * meaning "more length bytes follow"), the literals, a two byte little
 * endian match offset, then the rest of the match length.  The last
 * sequence stops after its literals.
 */

#define LZ4_HASH_LOG	13
#define LZ4_MINMATCH	4
#define LZ4_MAX_OFFSET	65535
#define LZ4_LASTLITERALS 5
#define LZ4_MFLIMIT	12
#define LZ4_MAX_INPUT	0x7E000000

static unsigned int
read32(const unsigned char *p)
{
    unsigned int v;

    memcpy(&v, p, 4);
    return v;
}

size_t
xwLz4Bound(size_t n)
{
    return 4 + n + n / 255 + 16;
}

static unsigned char *
lz4PutLength(unsigned char *op, size_t len)
{
    while (len >= 255) {
	*op++ = 255;
	len -= 255;
    }
    *op++ = (unsigned char)len;
    return op;
}

size_t
xwLz4Compress(const unsigned char *src, size_t n, unsigned char *dst,
	      size_t dstcap)
{
    unsigned int table[1 << LZ4_HASH_LOG];
    const unsigned char *ip = src, *anchor = src, *iend = src + n;
    unsigned char *op = dst, *token;
    size_t litlen;

    if (n > LZ4_MAX_INPUT || dstcap < xwLz4Bound(n))
	return 0;

    *op++ = (unsigned char)n;
    *op++ = (unsigned char)(n >> 8);
    *op++ = (unsigned char)(n >> 16);
    *op++ = (unsigned char)(n >> 24);

    if (n > LZ4_MFLIMIT) {
	const unsigned char *mflimit = iend - LZ4_MFLIMIT;
	const unsigned char *matchlimit = iend - LZ4_LASTLITERALS;

	memset(table, 0, sizeof(table));
	while (ip < mflimit) {
	    unsigned int h = (read32(ip) * 2654435761U) >> (32 - LZ4_HASH_LOG);
	    const unsigned char *ref = src + table[h];
	    size_t mlen, off;

	    table[h] = (unsigned int)(ip - src);
	    if (ref >= ip || (size_t)(ip - ref) > LZ4_MAX_OFFSET ||
		read32(ref) != read32(ip)) {
		/* step faster the longer nothing has matched */
		ip += 1 + ((ip - anchor) >> 6);
		continue;
	    }
	    while (ip > anchor && ref > src && ip[-1] == ref[-1]) {
		ip--;
		ref--;
	    }
	    mlen = LZ4_MINMATCH;
	    while (ip + mlen < matchlimit && ip[mlen] == ref[mlen])
		mlen++;

	    litlen = (size_t)(ip - anchor);
	    token = op++;
	    if (litlen >= 15) {
		*token = 15 << 4;
		op = lz4PutLength(op, litlen - 15);
	    } else {
		*token = (unsigned char)(litlen << 4);
	    }
	    memcpy(op, anchor, litlen);
	    op += litlen;
	    off = (size_t)(ip - ref);
	    *op++ = (unsigned char)off;
	    *op++ = (unsigned char)(off >> 8);
	    if (mlen - LZ4_MINMATCH >= 15) {
		*token |= 15;
		op = lz4PutLength(op, mlen - LZ4_MINMATCH - 15);
	    } else {
		*token |= (unsigned char)(mlen - LZ4_MINMATCH);
	    }
	    ip += mlen;
	    anchor = ip;
	}
    }

    litlen = (size_t)(iend - anchor);
    token = op++;
    if (litlen >= 15) {
	*token = 15 << 4;
	op = lz4PutLength(op, litlen - 15);
    } else {
	*token = (unsigned char)(litlen << 4);
    }
    memcpy(op, anchor, litlen);
    op += litlen;
    return (size_t)(op - dst);
}

long
xwLz4Size(const unsigned char *src, size_t n)
{
    unsigned long v;

    if (n < 4)
	return -1;
    v = (unsigned long)src[0] | ((unsigned long)src[1] << 8) |
	((unsigned long)src[2] << 16) | ((unsigned long)src[3] << 24);
    if (v > LZ4_MAX_INPUT)
	return -1;
    return (long)v;
}

long
xwLz4Decompress(const unsigned char *src, size_t n, unsigned char *dst,
		size_t dstcap)
{
    const unsigned char *ip, *iend = src + n;
    unsigned char *op = dst, *oend;
    long size = xwLz4Size(src, n);

    if (size < 0 || (size_t)size > dstcap)
	return -1;
    ip = src + 4;
    oend = dst + size;

    for (;;) {
	unsigned int token;
	size_t len, off;
	int b;

	if (ip >= iend)
	    return -1;
	token = *ip++;

	len = token >> 4;
	if (len == 15) {
	    do {
		if (ip >= iend)
		    return -1;
		b = *ip++;
		len += (size_t)b;
		if (len > LZ4_MAX_INPUT)
		    return -1;
	    } while (b == 255);
	}
	if (len > (size_t)(iend - ip) || len > (size_t)(oend - op))
	    return -1;
	memcpy(op, ip, len);
	op += len;
	ip += len;
	if (ip == iend)
	    break;			/* the last sequence has no match */

	if (iend - ip < 2)
	    return -1;
	off = (size_t)ip[0] | ((size_t)ip[1] << 8);
	ip += 2;
	if (off == 0 || off > (size_t)(op - dst))
	    return -1;

	len = token & 15;
	if (len == 15) {
	    do {
		if (ip >= iend)
		    return -1;
		b = *ip++;
		len += (size_t)b;
		if (len > LZ4_MAX_INPUT)
		    return -1;
	    } while (b == 255);
	}
	len += LZ4_MINMATCH;
	if (len > (size_t)(oend - op))
	    return -1;
	if (off >= len) {
	    memcpy(op, op - off, len);
	    op += len;
	} else {
	    /* the match runs into what it is writing: byte by byte */
	    const unsigned char *ref = op - off;

	    while (len--)
		*op++ = *ref++;
	}
    }
    return op == oend ? size : -1;
}
