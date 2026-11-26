/*
 * wire_test.c - tests of the Xpra wire format code (hw/vnc/xpra_wire.c)
 * that need no X server and no xpra: known answers taken from xpra's own
 * rencodeplus and from python-lz4, round trips, and damaged input.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: 2026 D. Kevin Cameron
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xpra_wire.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, what) \
    do { \
	checks++; \
	if (!(cond)) { \
	    failures++; \
	    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); \
	} \
    } while (0)

static unsigned int seed = 12345;

static unsigned int
rnd(void)
{
    seed = seed * 1103515245U + 12345U;
    return (seed >> 8) & 0xffffff;
}

static size_t
unhex(const char *hex, unsigned char *out)
{
    size_t n = 0;
    unsigned int v;

    while (hex[0] && hex[1]) {
	sscanf(hex, "%2x", &v);
	out[n++] = (unsigned char)v;
	hex += 2;
    }
    return n;
}

static int
sameHex(const XwBuf *b, const char *hex)
{
    unsigned char want[512];
    size_t n = unhex(hex, want);

    return !b->failed && b->len == n && memcmp(b->data, want, n) == 0;
}

/* what xpra's rencodeplus.dumps() gives for these values */
static void
testEncode(void)
{
    static const struct { long long v; const char *hex; } ints[] = {
	{ 0, "00" }, { 1, "01" }, { 43, "2b" }, { 44, "3e2c" },
	{ 127, "3e7f" }, { 128, "3f0080" }, { -1, "46" }, { -32, "65" },
	{ -33, "3edf" }, { 32767, "3f7fff" }, { 32768, "4000008000" },
	{ 2147483647LL, "407fffffff" },
	{ 2147483648LL, "410000000080000000" },
	{ 9223372036854775807LL, "417fffffffffffffff" },
	{ -9223372036854775807LL - 1, "418000000000000000" },
    };
    XwBuf b;
    char longstr[65];
    size_t i;

    xwBufInit(&b);
    for (i = 0; i < sizeof(ints) / sizeof(ints[0]); i++) {
	xwBufReset(&b);
	xwPutInt(&b, ints[i].v);
	CHECK(sameHex(&b, ints[i].hex), ints[i].hex);
    }
    xwBufReset(&b);
    xwPutBool(&b, 1);
    xwPutBool(&b, 0);
    CHECK(sameHex(&b, "4344"), "booleans");
    xwBufReset(&b);
    xwPutStr(&b, "");
    xwPutStr(&b, "a");
    xwPutStr(&b, "\xc3\xa9");
    CHECK(sameHex(&b, "80816182c3a9"), "short strings");
    memset(longstr, 'x', 64);
    longstr[64] = '\0';
    xwBufReset(&b);
    xwPutStr(&b, longstr);
    CHECK(b.len == 67 && memcmp(b.data, "64:xxx", 6) == 0, "64 byte string");
    xwBufReset(&b);
    xwPutStr(&b, longstr + 1);
    CHECK(b.len == 64 && b.data[0] == 0xbf, "63 byte string");
    xwBufReset(&b);
    xwPutBytes(&b, "", 0);
    xwPutBytes(&b, "ab", 2);
    CHECK(sameHex(&b, "302f322f6162"), "bytes");
    xwBufFree(&b);
}

static void
testDecode(void)
{
    XwArena *a = xwArenaNew();
    unsigned char raw[512];
    char buf[32];
    XwValue *v, *d;
    size_t n;

    /* ["draw", 7, 0, 0, 484, 316, "rgb24", b"\0\1\2", 12, 1936,
       {"rgb_format": "BGRX", "lz4": 1, "window-size": (484, 316)}] */
    n = unhex("cb84647261770700003f01e43f013c857267623234332f0001020c3f0790"
	      "698a7267625f666f726d61748442475258836c7a34018b77696e646f772d"
	      "73697a65c23f01e43f013c", raw);
    v = xwDecode(a, raw, n);
    CHECK(v && v->type == XW_LIST && v->n == 11, "draw packet is a list of 11");
    CHECK(xwIsStr(xwItem(v, 0), "draw"), "packet type");
    CHECK(xwInt(xwItem(v, 1), -1) == 7, "wid");
    CHECK(xwInt(xwItem(v, 4), -1) == 484 && xwInt(xwItem(v, 5), -1) == 316, "size");
    CHECK(xwItem(v, 7) && xwItem(v, 7)->type == XW_BYTES && xwItem(v, 7)->n == 3 &&
	  memcmp(xwItem(v, 7)->s, "\0\1\2", 3) == 0, "pixel bytes");
    CHECK(xwInt(xwItem(v, 9), -1) == 1936, "rowstride");
    d = xwItem(v, 10);
    CHECK(d && d->type == XW_DICT && d->n == 3, "options dictionary");
    CHECK(strcmp(xwStr(xwGet(d, "rgb_format"), buf, sizeof(buf)), "BGRX") == 0, "rgb_format");
    CHECK(xwInt(xwGet(d, "lz4"), 0) == 1, "lz4 option");
    CHECK(xwInt(xwItem(xwGet(d, "window-size"), 1), 0) == 316, "nested list");
    CHECK(xwGet(d, "missing") == NULL && xwItem(v, 11) == NULL, "absent items");
    CHECK(strcmp(xwStr(NULL, buf, sizeof(buf)), "") == 0, "string of nothing");
    CHECK(strcmp(xwStr(xwItem(v, 6), buf, 4), "rgb") == 0, "string truncated to the buffer");

    /* {1: 'b', b'k': [True]} */
    xwArenaReset(a);
    n = unhex("68018162312f6bc143", raw);
    v = xwDecode(a, raw, n);
    CHECK(v && v->type == XW_DICT && v->n == 2, "dictionary with mixed keys");
    CHECK(xwBool(xwItem(xwGet(v, "k"), 0), 0) == 1, "bytes key, list value");

    /* 1.5, None, 2**64 (saturates), -33, terminated list and dictionary */
    xwArenaReset(a);
    n = unhex("3b2c3ff8000000000000453d31383434363734343037333730393535"
	      "31363136" "7f3edf3c816101" "7f7f", raw);
    v = xwDecode(a, raw, n);
    CHECK(v && v->type == XW_LIST && v->n == 5, "terminated list");
    CHECK(xwItem(v, 0) && xwItem(v, 0)->type == XW_FLOAT && xwItem(v, 0)->f == 1.5, "double");
    CHECK(xwItem(v, 1) && xwItem(v, 1)->type == XW_NONE, "none");
    CHECK(xwInt(xwItem(v, 2), 0) == 9223372036854775807LL, "big integer saturates");
    CHECK(xwInt(xwItem(v, 3), 0) == -33, "negative byte");
    CHECK(xwInt(xwGet(xwItem(v, 4), "a"), 0) == 1, "terminated dictionary");

    /* not well formed */
    xwArenaReset(a);
    CHECK(xwDecode(a, raw, 0) == NULL, "empty input");
    n = unhex("c30181", raw);
    CHECK(xwDecode(a, raw, n) == NULL, "list cut short");
    n = unhex("0101", raw);
    CHECK(xwDecode(a, raw, n) == NULL, "trailing bytes");
    n = unhex("393939393939393939393a61", raw);
    CHECK(xwDecode(a, raw, n) == NULL, "string longer than the input");
    n = unhex("67816101" "00", raw);
    CHECK(xwDecode(a, raw, n - 2) == NULL, "dictionary with a key and no value");
    n = unhex("7f", raw);
    CHECK(xwDecode(a, raw, n) == NULL, "terminator alone");
    {
	/* deeper than the decoder goes */
	unsigned char deep[200];

	memset(deep, 0xc1, sizeof(deep));
	deep[sizeof(deep) - 1] = 0;
	CHECK(xwDecode(a, deep, sizeof(deep)) == NULL, "nesting limit");
    }
    xwArenaFree(a);
}

/* encode, decode, compare */
static void
testRoundTrip(void)
{
    XwArena *a = xwArenaNew();
    XwBuf b;
    XwValue *v, *d;
    char buf[64];
    int i;

    xwBufInit(&b);
    xwListOpen(&b);
    xwPutStr(&b, "hello");
    xwDictOpen(&b);
    xwKeyStr(&b, "version", "6.5");
    xwKeyBool(&b, "rencodeplus", 1);
    xwKeyInt(&b, "big", 123456789012345LL);
    xwKeyPair(&b, "desktop_size", 1280, 800);
    xwPutStr(&b, "encoders");
    xwListOpen(&b);
    for (i = 0; i < 100; i++)
	xwPutInt(&b, i * 37 - 1000);
    xwClose(&b);
    xwClose(&b);
    xwClose(&b);
    CHECK(!b.failed, "encode");
    v = xwDecode(a, b.data, b.len);
    CHECK(v && v->n == 2 && xwIsStr(xwItem(v, 0), "hello"), "round trip: packet");
    d = xwItem(v, 1);
    CHECK(strcmp(xwStr(xwGet(d, "version"), buf, sizeof(buf)), "6.5") == 0, "round trip: string");
    CHECK(xwBool(xwGet(d, "rencodeplus"), 0), "round trip: boolean");
    CHECK(xwInt(xwGet(d, "big"), 0) == 123456789012345LL, "round trip: 64 bits");
    CHECK(xwInt(xwItem(xwGet(d, "desktop_size"), 1), 0) == 800, "round trip: pair");
    CHECK(xwGet(d, "encoders") && xwGet(d, "encoders")->n == 100, "round trip: 100 items");
    for (i = 0; i < 100; i++) {
	if (xwInt(xwItem(xwGet(d, "encoders"), i), 0) != i * 37 - 1000) {
	    CHECK(0, "round trip: integer values");
	    break;
	}
    }
    xwBufFree(&b);
    xwArenaFree(a);
}

static void
testHeader(void)
{
    unsigned char h[XW_HEADER_SIZE];
    XwHeader hdr;

    xwPutHeader(h, XW_FLAG_RENCODEPLUS | XW_FLAG_FLUSH, XW_LEVEL_LZ4 | 1, 0, 3515);
    CHECK(memcmp(h, "P\x18\x11\x00\x00\x00\x0d\xbb", 8) == 0, "header bytes");
    CHECK(xwParseHeader(h, &hdr) == 0 && hdr.flags == 0x18 && hdr.level == 0x11 &&
	  hdr.index == 0 && hdr.size == 3515, "header parsed");
    h[0] = 'G';
    CHECK(xwParseHeader(h, &hdr) < 0, "not a packet");
    h[0] = 'P';
    h[3] = 16;
    CHECK(xwParseHeader(h, &hdr) < 0, "chunk index out of range");
}

static void
lz4RoundTrip(const unsigned char *data, size_t n, const char *what)
{
    size_t cap = xwLz4Bound(n);
    unsigned char *c = (unsigned char *)malloc(cap);
    unsigned char *d = (unsigned char *)malloc(n + 1);
    size_t clen;
    long dlen;

    clen = xwLz4Compress(data, n, c, cap);
    CHECK(clen > 0 && clen <= cap, what);
    CHECK(xwLz4Size(c, clen) == (long)n, what);
    dlen = xwLz4Decompress(c, clen, d, n);
    CHECK(dlen == (long)n && memcmp(d, data, n) == 0, what);
    /* a block one byte short, and an output one byte short */
    if (clen > 5)
	CHECK(xwLz4Decompress(c, clen - 1, d, n) < 0, "truncated block");
    if (n > 0)
	CHECK(xwLz4Decompress(c, clen, d, n - 1) < 0, "output too small");
    free(c);
    free(d);
}

static void
testLz4(void)
{
    static const char text[] =
	"abcabcabcabcabcabcabcabcabcabcabcabc hello hello hello hello, "
	"the quick brown fox";
    unsigned char raw[256], out[512];
    unsigned char *big;
    size_t n, i, len;
    long r;

    /* blocks made by python-lz4 */
    n = unhex("510000003f61626303000e6e2068656c6c6f0600f0062c2074686520"
	      "717569636b2062726f776e20666f78", raw);
    r = xwLz4Decompress(raw, n, out, sizeof(out));
    CHECK(r == 81 && memcmp(out, text, 81) == 0, "python-lz4 block");
    n = unhex("2c0100001f000100ff14500000000000", raw);
    r = xwLz4Decompress(raw, n, out, sizeof(out));
    CHECK(r == 300, "python-lz4 block of zeros");
    for (i = 0; r == 300 && i < 300; i++) {
	if (out[i]) {
	    CHECK(0, "zeros");
	    break;
	}
    }
    CHECK(xwLz4Decompress(raw, n, out, 299) < 0, "block larger than the buffer");
    CHECK(xwLz4Decompress(raw, 3, out, sizeof(out)) < 0, "no length prefix");
    /* an offset pointing before the start of the output */
    n = unhex("100000001061" "0500" "00", raw);
    CHECK(xwLz4Decompress(raw, n, out, sizeof(out)) < 0, "offset before the output");
    n = unhex("100000001061" "0000" "00", raw);
    CHECK(xwLz4Decompress(raw, n, out, sizeof(out)) < 0, "zero offset");

    lz4RoundTrip((const unsigned char *)"", 0, "empty");
    lz4RoundTrip((const unsigned char *)text, 1, "one byte");
    lz4RoundTrip((const unsigned char *)text, 12, "12 bytes");
    lz4RoundTrip((const unsigned char *)text, 13, "13 bytes");
    lz4RoundTrip((const unsigned char *)text, sizeof(text) - 1, "text");

    big = (unsigned char *)malloc(1 << 20);
    for (len = 1; len <= (1 << 20); len = len * 3 + 7) {
	if (len > (1 << 20))
	    len = 1 << 20;
	/* runs, like window contents */
	for (i = 0; i < len; i++)
	    big[i] = (unsigned char)((i / 97) * 13);
	lz4RoundTrip(big, len, "runs");
	/* noise: does not compress */
	for (i = 0; i < len; i++)
	    big[i] = (unsigned char)rnd();
	lz4RoundTrip(big, len, "noise");
	/* pixels: four byte units, a few colours, long matches far apart */
	for (i = 0; i + 4 <= len; i += 4) {
	    unsigned int c = ((i / 4) % 517) < 400 ? 0xffffffU : (rnd() & 3) * 0x404040U;

	    memcpy(big + i, &c, 4);
	}
	lz4RoundTrip(big, len, "pixels");
	if (len == (1 << 20))
	    break;
    }
    free(big);
}

/*
 * Damaged input must be refused or decoded, never crash: built with
 * -fsanitize=address,undefined this is the test of the bounds checks.
 */
static void
testDamaged(void)
{
    XwArena *a = xwArenaNew();
    unsigned char good[512], bad[512], out[4096];
    size_t n, cn;
    int i, j;
    unsigned char c[512];

    n = unhex("cb84647261770700003f01e43f013c857267623234332f0001020c3f0790"
	      "698a7267625f666f726d61748442475258836c7a34018b77696e646f772d"
	      "73697a65c23f01e43f013c", good);
    for (i = 0; i < 200000; i++) {
	size_t len = n;

	memcpy(bad, good, n);
	for (j = (int)(rnd() % 4) + 1; j > 0; j--)
	    bad[rnd() % n] = (unsigned char)rnd();
	if (rnd() % 4 == 0)
	    len = rnd() % (n + 1);
	xwArenaReset(a);
	(void)xwDecode(a, bad, len);
    }
    for (i = 0; i < 20000; i++) {
	size_t len = rnd() % sizeof(bad);

	for (j = 0; j < (int)len; j++)
	    bad[j] = (unsigned char)rnd();
	xwArenaReset(a);
	(void)xwDecode(a, bad, len);
    }
    checks++;

    for (j = 0; j < 400; j++)
	good[j] = (unsigned char)((j / 7) & 3);
    cn = xwLz4Compress(good, 400, c, sizeof(c));
    CHECK(cn > 0, "compress for the damage test");
    for (i = 0; i < 200000; i++) {
	size_t len = cn;

	memcpy(bad, c, cn);
	for (j = (int)(rnd() % 3) + 1; j > 0; j--)
	    bad[rnd() % cn] = (unsigned char)rnd();
	if (rnd() % 4 == 0)
	    len = rnd() % (cn + 1);
	(void)xwLz4Decompress(bad, len, out, sizeof(out));
	(void)xwLz4Decompress(bad, len, out, 400);
	(void)xwLz4Decompress(bad, len, out, 100);
    }
    checks++;
    xwArenaFree(a);
}

/*
 * "wire_test lz4c" compresses standard input to standard output and
 * "wire_test lz4d" decompresses it, for checking against another LZ4.
 */
static int
filter(int compress)
{
    XwBuf in;
    unsigned char tmp[65536], *out;
    size_t n, cap;
    long r;

    xwBufInit(&in);
    while ((n = fread(tmp, 1, sizeof(tmp), stdin)) > 0)
	xwBufAppend(&in, tmp, n);
    if (in.failed)
	return 2;
    if (compress) {
	cap = xwLz4Bound(in.len);
	out = (unsigned char *)malloc(cap);
	n = xwLz4Compress(in.data ? in.data : tmp, in.len, out, cap);
	if (n == 0)
	    return 2;
    } else {
	r = xwLz4Size(in.data ? in.data : tmp, in.len);
	if (r < 0)
	    return 2;
	out = (unsigned char *)malloc((size_t)r + 1);
	r = xwLz4Decompress(in.data, in.len, out, (size_t)r);
	if (r < 0)
	    return 2;
	n = (size_t)r;
    }
    fwrite(out, 1, n, stdout);
    free(out);
    xwBufFree(&in);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "lz4c") == 0)
	return filter(1);
    if (argc > 1 && strcmp(argv[1], "lz4d") == 0)
	return filter(0);
    testEncode();
    testDecode();
    testRoundTrip();
    testHeader();
    testLz4();
    testDamaged();
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
