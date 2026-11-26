/*
 * xpra_wire.h - the Xpra wire format: packet framing, the "rencodeplus"
 * value encoding and LZ4 block compression.
 *
 * Nothing in here knows about the X server: the same code is linked into
 * Xvnc and into the stand-alone tests (tests/wire_test.c).
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

#ifndef XPRA_WIRE_H
#define XPRA_WIRE_H

#include <stddef.h>

/*
 * A growable byte buffer.  A failed allocation empties the buffer and sets
 * "failed"; every later append is then a no-op, so a caller can build a
 * whole packet and test the flag once.
 */

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
    int failed;
} XwBuf;

extern void xwBufInit(XwBuf *b);
extern void xwBufFree(XwBuf *b);
extern void xwBufReset(XwBuf *b);
extern int xwBufReserve(XwBuf *b, size_t extra);
extern void xwBufAppend(XwBuf *b, const void *p, size_t n);
extern void xwBufConsume(XwBuf *b, size_t n);

/*
 * Packet header: 'P', protocol flags, compression, chunk index, payload
 * size (32 bits, big endian).  A packet whose index is not zero is a raw
 * chunk: it replaces the item at that position in the packet that follows.
 */

#define XW_HEADER_SIZE		8

#define XW_FLAG_RENCODE		0x01	/* the old rencode: not spoken here */
#define XW_FLAG_CIPHER		0x02
#define XW_FLAG_YAML		0x04
#define XW_FLAG_FLUSH		0x08
#define XW_FLAG_RENCODEPLUS	0x10

#define XW_LEVEL_LZ4		0x10
#define XW_LEVEL_BROTLI		0x40

#define XW_MAX_CHUNKS		16	/* chunk index is below this */

typedef struct {
    int flags;
    int level;
    int index;
    unsigned long size;
} XwHeader;

extern int xwParseHeader(const unsigned char *p, XwHeader *h);
extern void xwPutHeader(unsigned char *p, int flags, int level, int index,
			unsigned long size);

/*
 * Encoding.  Lists and dictionaries are always written in their terminated
 * form, so nothing has to be counted in advance: open, put the items (for a
 * dictionary: key, value, key, value...), close.
 */

extern void xwPutInt(XwBuf *b, long long v);
extern void xwPutBool(XwBuf *b, int v);
extern void xwPutStr(XwBuf *b, const char *s);
extern void xwPutStrN(XwBuf *b, const char *s, size_t n);
extern void xwPutBytes(XwBuf *b, const void *p, size_t n);
extern void xwListOpen(XwBuf *b);
extern void xwDictOpen(XwBuf *b);
extern void xwClose(XwBuf *b);

/* dictionary entries with a string key */
extern void xwKeyInt(XwBuf *b, const char *key, long long v);
extern void xwKeyBool(XwBuf *b, const char *key, int v);
extern void xwKeyStr(XwBuf *b, const char *key, const char *s);
extern void xwKeyPair(XwBuf *b, const char *key, long long v1, long long v2);

/*
 * Decoding gives a tree.  Strings and byte strings point into the packet,
 * they are not copied and not terminated.  The children of a list are its
 * items in order; the children of a dictionary alternate key and value.
 */

#define XW_NONE		0
#define XW_BOOL		1
#define XW_INT		2
#define XW_FLOAT	3
#define XW_STR		4
#define XW_BYTES	5
#define XW_LIST		6
#define XW_DICT		7

typedef struct _XwValue {
    int type;
    long long i;			/* XW_INT, XW_BOOL */
    double f;				/* XW_FLOAT */
    const unsigned char *s;		/* XW_STR, XW_BYTES */
    size_t n;				/* length, or number of children */
    struct _XwValue *first;		/* XW_LIST, XW_DICT */
    struct _XwValue *next;		/* next sibling */
} XwValue;

typedef struct _XwArena XwArena;

extern XwArena *xwArenaNew(void);
extern void xwArenaReset(XwArena *a);
extern void xwArenaFree(XwArena *a);

/* returns the value, or NULL if the data is not one well-formed value */
extern XwValue *xwDecode(XwArena *a, const unsigned char *data, size_t len);

extern XwValue *xwItem(const XwValue *list, int index);
extern XwValue *xwGet(const XwValue *dict, const char *key);
extern int xwIsStr(const XwValue *v, const char *s);
extern long long xwInt(const XwValue *v, long long dflt);
extern int xwBool(const XwValue *v, int dflt);
/* copies a string or byte string into buf, always terminated */
extern char *xwStr(const XwValue *v, char *buf, size_t buflen);
/* does a list of strings have this one? */
extern int xwListHas(const XwValue *list, const char *s);

/*
 * LZ4 block compression as xpra uses it: the uncompressed length as four
 * bytes, little endian, then one LZ4 block.
 */

extern size_t xwLz4Bound(size_t n);
/* returns the compressed length, 0 if dst is too small */
extern size_t xwLz4Compress(const unsigned char *src, size_t n,
			    unsigned char *dst, size_t dstcap);
/* the length the data claims to decompress to, -1 if there is no prefix */
extern long xwLz4Size(const unsigned char *src, size_t n);
/* returns the decompressed length, -1 if the data is not valid */
extern long xwLz4Decompress(const unsigned char *src, size_t n,
			    unsigned char *dst, size_t dstcap);

#endif /* XPRA_WIRE_H */
