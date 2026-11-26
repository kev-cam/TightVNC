/*
 * xpra.c - sharing this server's windows, one by one, with Xpra clients.
 *
 * A VNC viewer gets the whole desktop.  An Xpra client ("xpra attach")
 * gets every top level window as a window of its own, on the user's
 * desktop.  With -xpra this server speaks the Xpra protocol next to RFB,
 * from inside the X server: the windows are found by walking the window
 * tree, their contents come from the framebuffer through the hooks RFB
 * updates already use (draw.c), and input goes in the way RFB input does
 * (kbdptr.c).
 *
 * This server has no Composite extension: there is one framebuffer and a
 * window's pixels exist only where it is visible.  Only the visible part
 * of a window is sent; what another window covers is sent when the
 * application repaints it after being uncovered, which giving a window the
 * focus (or clicking in it) brings about by raising it.
 *
 * So that it does not come to that, when no window manager is running the
 * windows are kept apart: where a window is on this screen is of no
 * consequence to a client that places it on its own desktop, and a window
 * that would lie over another, or off the screen, is moved to a free part
 * of it (xpraPlace).  Give the server a screen large enough for the
 * windows side by side (-geometry).  Under a window manager the windows
 * are left where they are.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "scrnintstr.h"
#include "windowstr.h"
#include "regionstr.h"
#include "propertyst.h"
#include "inputstr.h"
#include "dixstruct.h"
#include "colormapst.h"
#include "cursorstr.h"
#include "servermd.h"
#include "Xatom.h"
#include "rfb.h"
#include "input.h"
#include "mipointer.h"
#include "sprite.h"
#include "xpra.h"
#include "xpra_wire.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

extern WindowPtr *WindowTable;
extern char *desktopName;

/* the version of xpra whose protocol this is */
#define XPRA_VERSION		"6.5"

#define XPRA_MAX_FIRST_PACKET	(1024 * 1024)	  /* before the hello */
#define XPRA_MAX_PACKET		(32 * 1024 * 1024)
#define XPRA_MAX_BACKLOG	(192 * 1024 * 1024) /* unsent output */
#define XPRA_READ_AT_ONCE	(4 * 1024 * 1024)
#define XPRA_BAND_BYTES		(1024 * 1024)	  /* pixels in one packet */
#define XPRA_DEFER_MS		20	/* from a change to sending it */
#define XPRA_IDLE_MS		250	/* looking for new titles and such */
#define XPRA_QUIET_MS		500	/* after the client resized a window */
#define XPRA_ACK_WAIT_MS	1000	/* then send without waiting for acks */
#define XPRA_MAX_UNACKED	3
#define XPRA_MAX_TEXT		512
#define XPRA_MIN_LZ4		256
#define XPRA_MAX_CURSOR		128	/* pixels a side */

typedef struct _XpraClient {
    struct _XpraClient *next;
    int sock;
    char name[64];
    XwBuf in;			/* received, not yet a whole packet */
    XwBuf out;			/* to be written, from outPos */
    size_t outPos;
    XwBuf chunk[XW_MAX_CHUNKS];	/* raw chunks for the packet to come */
    Bool haveChunk[XW_MAX_CHUNKS];
    int nChunks;
    Bool hello;			/* the hello has been exchanged */
    Bool ui;			/* shows the windows (not a query) */
    Bool closing;		/* close once the output is written */
    Bool dead;			/* to be freed by xpraReap */
    Bool lz4;
    Bool cursors;		/* wants the pointer's shape */
    unsigned long cursorHash;	/* of the shape it was last sent */
    unsigned long cursorSerial;
    unsigned long seq;		/* of the last draw packet */
    int unacked;		/* draw packets not acknowledged yet */
    CARD32 lastAck;
} XpraClient;

typedef struct _XpraWin {
    struct _XpraWin *next;
    int wid;			/* what the client calls it */
    XID xid;			/* the application's window */
    Bool or;			/* override redirect: a menu, a tooltip */
    int x, y, w, h;		/* on the screen */
    Bool seen;			/* by the scan in progress */
    Bool sent;			/* the client has been told of it */
    Bool mapped;		/* the client is showing it */
    Bool refresh;		/* send all of it */
    int cx, cy;			/* where the client has it */
    Bool haveClientPos;
    int cw, ch;			/* the size the client has it at */
    CARD32 quietUntil;		/* no size news to the client before this */
    unsigned long metaHash;
    int parentWid;		/* what a popup was placed against */
} XpraWin;

Bool xpraEnabled = FALSE;

static Bool xpraUnixWanted = FALSE;	/* -xpra or -xprasocket */
static char *xpraSocketArg = NULL;
static int xpraPort = 0;

static char xpraSocketPath[sizeof(((struct sockaddr_un *)0)->sun_path)];
static int xpraUnixSock = -1;
static int xpraTcpSock = -1;
static fd_set xpraFds;
static int xpraMaxFd = -1;
static Bool xpraSocketsDone = FALSE;

static XpraClient *xpraClients = NULL;
static XpraClient *xpraUI = NULL;
static XpraWin *xpraWins = NULL;
static int xpraNextWid = 1;
static int xpraFocusWid = 0;
static int xpraButtonMask = 0;
static time_t xpraStartTime = 0;

static RegionRec xpraDamage;		/* changed since the last flush */
static Bool xpraDamageValid = FALSE;
static OsTimerPtr xpraTimer = NULL;
static Bool xpraTimerSet = FALSE;

/*
 * Set while packets are being handled or sent.  Handling one can come back
 * here: SetInputFocus, for one, calls UpdateCurrentTime, which calls
 * ProcessInputEvents, which calls xpraCheckFds - and a second reader of
 * the same socket would take the packets out from under the first.
 */
static Bool xpraBusy = FALSE;

static XwBuf xpraPkt;			/* the packet being built */
static XwBuf xpraPix;			/* its pixels */
static XwBuf xpraZ;			/* the same, compressed */
static XwBuf xpraPlain;			/* a received packet, decompressed */
static XwArena *xpraArena = NULL;

static Atom atomWM_STATE, atomWM_PROTOCOLS, atomWM_DELETE_WINDOW;
static Atom atomNET_WM_NAME, atomUTF8_STRING, atomNET_WM_WINDOW_TYPE;
static Atom atomNET_WM_PID;

static void xpraSchedule(int ms);
static void xpraCloseClient(XpraClient *c, const char *why);


/****************************************************************************/
/* command line */

int
xpraProcessArgument(int argc, char *argv[], int i)
{
    if (strcmp(argv[i], "-xpra") == 0) {
	xpraUnixWanted = TRUE;
	xpraEnabled = TRUE;
	return 1;
    }
    if (strcmp(argv[i], "-xprasocket") == 0) {	/* -xprasocket path */
	if (i + 1 >= argc)
	    UseMsg();
	xpraSocketArg = argv[i + 1];
	xpraUnixWanted = TRUE;
	xpraEnabled = TRUE;
	return 2;
    }
    if (strcmp(argv[i], "-xpraport") == 0) {	/* -xpraport port */
	if (i + 1 >= argc)
	    UseMsg();
	xpraPort = atoi(argv[i + 1]);
	if (xpraPort < 1 || xpraPort > 65535)
	    UseMsg();
	xpraEnabled = TRUE;
	return 2;
    }
    return 0;
}

void
xpraUseMsg(void)
{
    ErrorF("-xpra                  share the windows with Xpra clients, on a "
	   "socket\n"
	   "                       \"xpra attach :display\" finds "
	   "(~/.xpra/host-display)\n");
    ErrorF("-xprasocket path       the same, on this unix socket\n");
    ErrorF("-xpraport port         the same, on this TCP port of the "
	   "loopback\n"
	   "                       interface (no authentication: tunnel it)\n");
}


/****************************************************************************/
/* output */

static void
xpraWrite(XpraClient *c)
{
    while (!c->dead && c->outPos < c->out.len) {
	int n = send(c->sock, (char *)c->out.data + c->outPos,
		     c->out.len - c->outPos, MSG_NOSIGNAL);

	if (n > 0) {
	    c->outPos += (size_t)n;
	} else if (n < 0 && errno == EINTR) {
	    continue;
	} else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
	    break;
	} else {
	    xpraCloseClient(c, n < 0 ? strerror(errno) : "cannot write");
	    return;
	}
    }
    if (c->outPos >= c->out.len) {
	c->out.len = 0;
	c->outPos = 0;
	if (c->closing)
	    xpraCloseClient(c, NULL);
    } else {
	if (c->outPos > 1024 * 1024) {
	    xwBufConsume(&c->out, c->outPos);
	    c->outPos = 0;
	}
	/* the socket is full: come back to it */
	xpraSchedule(XPRA_DEFER_MS);
    }
}

/* start a packet in xpraPkt */
static void
xpraBegin(const char *type)
{
    xwBufReset(&xpraPkt);
    xwListOpen(&xpraPkt);
    xwPutStr(&xpraPkt, type);
}

/* finish the packet in xpraPkt and queue it */
static void
xpraSend(XpraClient *c)
{
    unsigned char header[XW_HEADER_SIZE];

    xwClose(&xpraPkt);
    if (c->dead || c->closing)
	return;
    if (xpraPkt.failed) {
	xpraCloseClient(c, "out of memory");
	return;
    }
    if (c->out.len - c->outPos + xpraPkt.len > XPRA_MAX_BACKLOG) {
	xpraCloseClient(c, "the client does not read what it is sent");
	return;
    }
    xwPutHeader(header, XW_FLAG_RENCODEPLUS | XW_FLAG_FLUSH, 0, 0,
		(unsigned long)xpraPkt.len);
    xwBufAppend(&c->out, header, XW_HEADER_SIZE);
    xwBufAppend(&c->out, xpraPkt.data, xpraPkt.len);
    if (c->out.failed) {
	c->outPos = 0;
	xpraCloseClient(c, "out of memory");
    }
}

/* tell the client why, then close when that has been written */
static void
xpraDisconnect(XpraClient *c, const char *why)
{
    if (c->dead || c->closing)
	return;
    rfbLog("xpra: disconnecting %s: %s\n", c->name, why);
    xpraBegin("disconnect");
    xwPutStr(&xpraPkt, why);
    xpraSend(c);
    c->closing = TRUE;
    xpraWrite(c);
}


/****************************************************************************/
/* text */

static Bool
xpraValidUtf8(const unsigned char *s, size_t n)
{
    size_t i = 0;

    while (i < n) {
	unsigned char c = s[i];
	int k, j;
	unsigned char lo = 0x80, hi = 0xbf;

	if (c < 0x80) {
	    if (c == 0)
		return FALSE;
	    i++;
	    continue;
	}
	if (c >= 0xc2 && c <= 0xdf) {
	    k = 1;
	} else if (c >= 0xe0 && c <= 0xef) {
	    k = 2;
	    if (c == 0xe0)
		lo = 0xa0;		/* no overlong forms */
	    if (c == 0xed)
		hi = 0x9f;		/* no surrogates */
	} else if (c >= 0xf0 && c <= 0xf4) {
	    k = 3;
	    if (c == 0xf0)
		lo = 0x90;
	    if (c == 0xf4)
		hi = 0x8f;
	} else {
	    return FALSE;
	}
	if (n - i <= (size_t)k)
	    return FALSE;
	if (s[i + 1] < lo || s[i + 1] > hi)
	    return FALSE;
	for (j = 2; j <= k; j++) {
	    if ((s[i + j] & 0xc0) != 0x80)
		return FALSE;
	}
	i += (size_t)k + 1;
    }
    return TRUE;
}

/*
 * A string from a window property.  The client refuses a packet with a
 * string that is not UTF-8, so text that is not valid is taken to be
 * ISO 8859-1, which any bytes are.
 */

static void
xpraPutText(XwBuf *b, const unsigned char *s, size_t n)
{
    unsigned char tmp[2 * XPRA_MAX_TEXT];
    size_t i, len = 0;

    while (n > 0 && s[n - 1] == 0)
	n--;
    if (n > XPRA_MAX_TEXT)
	n = XPRA_MAX_TEXT;
    if (xpraValidUtf8(s, n)) {
	/* the cut may have fallen inside a character */
	xwPutStrN(b, (const char *)s, n);
	return;
    }
    for (i = 0; i < n; i++) {
	unsigned char c = s[i];

	if (c < 0x20 || c == 0x7f || (c >= 0x80 && c < 0xa0))
	    c = ' ';
	if (c < 0x80) {
	    tmp[len++] = c;
	} else {
	    tmp[len++] = (unsigned char)(0xc0 | (c >> 6));
	    tmp[len++] = (unsigned char)(0x80 | (c & 0x3f));
	}
    }
    xwPutStrN(b, (const char *)tmp, len);
}


/****************************************************************************/
/* windows */

static WindowPtr
xpraLookup(XID xid)
{
    return (WindowPtr)LookupIDByType(xid, RT_WINDOW);
}

static PropertyPtr
xpraProp(WindowPtr pWin, Atom name)
{
    PropertyPtr p;

    for (p = wUserProps(pWin); p; p = p->next) {
	if (p->propertyName == name)
	    return p;
    }
    return NULL;
}

/* the items of a property of 32 bit values */
static CARD32 *
xpraProp32(WindowPtr pWin, Atom name, long *count)
{
    PropertyPtr p = xpraProp(pWin, name);

    if (!p || p->format != 32 || p->size < 1 || !p->data) {
	*count = 0;
	return NULL;
    }
    *count = p->size;
    return (CARD32 *)p->data;
}

static XpraWin *
xpraFindWin(int wid)
{
    XpraWin *win;

    for (win = xpraWins; win; win = win->next) {
	if (win->wid == wid)
	    return win;
    }
    return NULL;
}

static XpraWin *
xpraFindXid(XID xid)
{
    XpraWin *win;

    for (win = xpraWins; win; win = win->next) {
	if (win->xid == xid)
	    return win;
    }
    return NULL;
}

/* the child of the root that holds a window */
static WindowPtr
xpraTopLevel(WindowPtr pWin)
{
    while (pWin->parent && pWin->parent->parent)
	pWin = pWin->parent;
    return pWin;
}

/*
 * A window manager puts an application's window inside a frame of its own
 * and marks the application's with WM_STATE; it is the application's
 * window that is shared, not the frame.
 */

static WindowPtr
xpraFindState(WindowPtr pWin, int depth)
{
    WindowPtr pChild, pFound;

    for (pChild = pWin->firstChild; pChild; pChild = pChild->nextSib) {
	if (!pChild->viewable)
	    continue;
	if (xpraProp(pChild, atomWM_STATE))
	    return pChild;
	if (depth > 0 && (pFound = xpraFindState(pChild, depth - 1)))
	    return pFound;
    }
    return NULL;
}

static WindowPtr
xpraAppWindow(WindowPtr pTop)
{
    WindowPtr pApp;

    if (pTop->overrideRedirect || xpraProp(pTop, atomWM_STATE))
	return pTop;
    pApp = xpraFindState(pTop, 3);
    return pApp ? pApp : pTop;
}

/* is a window manager running? */
static ClientPtr
xpraWMClient(void)
{
    WindowPtr pRoot = WindowTable[0];
    OtherClients *oc;

    for (oc = wOtherClients(pRoot); oc; oc = oc->next) {
	if (oc->mask & SubstructureRedirectMask)
	    return clients[CLIENT_ID(oc->resource)];
    }
    return NULL;
}

static unsigned long
xpraHash(unsigned long h, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;

    while (n--)
	h = (h ^ *p++) * 16777619UL;
    return h;
}

/* changes when anything xpraPutMetadata reads does */
static unsigned long
xpraMetaHash(WindowPtr pApp)
{
    static Atom *const names[] = {
	&atomNET_WM_NAME, &atomNET_WM_WINDOW_TYPE, &atomNET_WM_PID
    };
    static const Atom fixed[] = {
	XA_WM_NAME, XA_WM_CLASS, XA_WM_NORMAL_HINTS, XA_WM_TRANSIENT_FOR,
	XA_WM_CLIENT_MACHINE
    };
#define N_FIXED (sizeof(fixed) / sizeof(fixed[0]))
    unsigned long h = 2166136261UL;
    PropertyPtr p;
    unsigned int i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]) + N_FIXED; i++) {
	Atom a = i < N_FIXED ? fixed[i] : *names[i - N_FIXED];

	p = xpraProp(pApp, a);
	if (p && p->data && p->size > 0 && p->size < 65536)
	    h = xpraHash(h, p->data, (size_t)p->size * (p->format / 8));
	h = xpraHash(h, &i, sizeof(i));
    }
    return h;
}

/*
 * What a popup is placed against: the window it names, or the top-most
 * shared window it lies over, or the one with the focus.
 */

static XpraWin *
xpraParentOf(XpraWin *win, WindowPtr pApp)
{
    XpraWin *other, *best = NULL;
    WindowPtr pOther, pSib;
    CARD32 *v;
    long n;

    v = xpraProp32(pApp, XA_WM_TRANSIENT_FOR, &n);
    if (v && (pOther = xpraLookup(v[0]))) {
	for (other = xpraWins; other; other = other->next) {
	    WindowPtr pShared = xpraLookup(other->xid);

	    if (other != win && other->sent && pShared &&
		(pShared == pOther || xpraTopLevel(pShared) == xpraTopLevel(pOther)))
		return other;
	}
    }
    if (!win->or)
	return NULL;

    /* from the top of the stack down: the first one under the popup */
    for (pSib = WindowTable[0]->firstChild; pSib; pSib = pSib->nextSib) {
	if (!pSib->viewable)
	    continue;
	other = xpraFindXid(xpraAppWindow(pSib)->drawable.id);
	if (!other || other == win || !other->sent)
	    continue;
	if (win->x < other->x + other->w && win->x + win->w > other->x &&
	    win->y < other->y + other->h && win->y + win->h > other->y)
	    return other;
	if (!best && other->wid == xpraFocusWid)
	    best = other;
    }
    return best;
}

/* the dictionary describing a window */
static void
xpraPutMetadata(XwBuf *b, XpraWin *win, WindowPtr pApp)
{
    PropertyPtr p;
    CARD32 *v;
    long n, i;
    XpraWin *parent;
    int maxw = rfbScreen.width, maxh = rfbScreen.height;
    Bool typed = FALSE;

    xwDictOpen(b);
    xwKeyInt(b, "xid", (long long)win->xid);

    p = xpraProp(pApp, atomNET_WM_NAME);
    if (!p || p->format != 8)
	p = xpraProp(pApp, XA_WM_NAME);
    xwPutStr(b, "title");
    if (p && p->format == 8 && p->data && p->size > 0)
	xpraPutText(b, (unsigned char *)p->data, (size_t)p->size);
    else
	xwPutStr(b, "");

    p = xpraProp(pApp, XA_WM_CLASS);
    if (p && p->format == 8 && p->data && p->size > 1) {
	const unsigned char *s = (unsigned char *)p->data;
	size_t len = (size_t)p->size, first = 0;

	while (first < len && s[first])
	    first++;
	xwPutStr(b, "class-instance");
	xwListOpen(b);
	xpraPutText(b, s, first);
	if (first + 1 < len)
	    xpraPutText(b, s + first + 1, len - first - 1);
	else
	    xwPutStr(b, "");
	xwClose(b);
    }

    xwPutStr(b, "window-type");
    xwListOpen(b);
    v = xpraProp32(pApp, atomNET_WM_WINDOW_TYPE, &n);
    for (i = 0; v && i < n && i < 8; i++) {
	char *name = NameForAtom(v[i]);

	if (name && strncmp(name, "_NET_WM_WINDOW_TYPE_", 20) == 0 && name[20]) {
	    xwPutStr(b, name + 20);
	    typed = TRUE;
	}
    }
    if (!typed)
	xwPutStr(b, xpraProp(pApp, XA_WM_TRANSIENT_FOR) && !win->or ?
		 "DIALOG" : "NORMAL");
    xwClose(b);

    if (win->or)
	xwKeyBool(b, "override-redirect", 1);

    v = xpraProp32(pApp, atomNET_WM_PID, &n);
    if (v)
	xwKeyInt(b, "pid", (long long)v[0]);

    /* the machine the application runs on: clients put it in the title */
    p = xpraProp(pApp, XA_WM_CLIENT_MACHINE);
    xwPutStr(b, "client-machine");
    if (p && p->format == 8 && p->data && p->size > 0) {
	xpraPutText(b, (unsigned char *)p->data, (size_t)p->size);
    } else {
	char host[256];

	host[0] = '\0';
	gethostname(host, sizeof(host) - 1);
	host[sizeof(host) - 1] = '\0';
	xpraPutText(b, (unsigned char *)host, strlen(host));
    }

    parent = xpraParentOf(win, pApp);
    win->parentWid = parent ? parent->wid : 0;
    if (parent) {
	xwKeyInt(b, "transient-for", parent->wid);
	if (win->or) {
	    /* where it is, measured from the window it belongs to */
	    xwKeyInt(b, "parent", parent->wid);
	    xwKeyPair(b, "relative-position", win->x - parent->x,
		      win->y - parent->y);
	}
    }

    /*
     * WM_NORMAL_HINTS: flags, four obsolete fields, minimum, maximum,
     * increment, two aspect ratios, base, gravity.  A window larger than
     * the screen could not be seen whole, so the screen is the most.
     */
    xwPutStr(b, "size-constraints");
    xwDictOpen(b);
    v = xpraProp32(pApp, XA_WM_NORMAL_HINTS, &n);
    if (v && n >= 15) {
	CARD32 flags = v[0];

	if ((flags & 16) && v[5] < 32768 && v[6] < 32768)
	    xwKeyPair(b, "minimum-size", v[5], v[6]);
	if ((flags & 32) && v[7] > 0 && v[8] > 0) {
	    if ((int)v[7] > 0 && (int)v[7] < maxw)
		maxw = (int)v[7];
	    if ((int)v[8] > 0 && (int)v[8] < maxh)
		maxh = (int)v[8];
	}
	if ((flags & 64) && v[9] > 0 && v[10] > 0 && v[9] < 32768 &&
	    v[10] < 32768)
	    xwKeyPair(b, "increment", v[9], v[10]);
	if ((flags & 256) && n >= 17 && v[15] < 32768 && v[16] < 32768)
	    xwKeyPair(b, "base-size", v[15], v[16]);
	if ((flags & 512) && n >= 18 && v[17] <= 10)
	    xwKeyInt(b, "gravity", v[17]);
    }
    xwKeyPair(b, "maximum-size", maxw, maxh);
    xwClose(b);

    xwClose(b);
}

/* where the client should put a window */
static void
xpraClientPosition(XpraWin *win, int *px, int *py)
{
    XpraWin *parent = win->or ? xpraFindWin(win->parentWid) : NULL;

    *px = win->x;
    *py = win->y;
    if (parent && parent->haveClientPos) {
	*px = parent->cx + (win->x - parent->x);
	*py = parent->cy + (win->y - parent->y);
    }
}

static void
xpraAnnounce(XpraClient *c, XpraWin *win, WindowPtr pApp)
{
    XwBuf meta;
    int x, y;

    /* the metadata first: it finds the window a popup belongs to */
    xwBufInit(&meta);
    xpraPutMetadata(&meta, win, pApp);
    xpraClientPosition(win, &x, &y);

    xpraBegin(win->or ? "new-override-redirect" : "new-window");
    xwPutInt(&xpraPkt, win->wid);
    xwPutInt(&xpraPkt, x);
    xwPutInt(&xpraPkt, y);
    xwPutInt(&xpraPkt, win->w);
    xwPutInt(&xpraPkt, win->h);
    xwBufAppend(&xpraPkt, meta.data, meta.len);
    if (meta.failed)
	xpraPkt.failed = 1;
    xwBufFree(&meta);
    xwDictOpen(&xpraPkt);		/* no client properties */
    xwClose(&xpraPkt);
    xpraSend(c);

    win->sent = TRUE;
    win->refresh = TRUE;
    win->cw = win->w;
    win->ch = win->h;
    win->metaHash = xpraMetaHash(pApp);
    if (win->or) {
	/* the client shows a popup at once, where it was told to */
	win->mapped = TRUE;
	win->cx = x;
	win->cy = y;
	win->haveClientPos = TRUE;
    }
}

static void
xpraForget(XpraWin *win)
{
    XpraWin **prev;

    if (win->sent && xpraUI) {
	xpraBegin("lost-window");
	xwPutInt(&xpraPkt, win->wid);
	xpraSend(xpraUI);
    }
    if (xpraFocusWid == win->wid)
	xpraFocusWid = 0;
    for (prev = &xpraWins; *prev; prev = &(*prev)->next) {
	if (*prev == win) {
	    *prev = win->next;
	    break;
	}
    }
    free(win);
}

static void
xpraForgetAll(void)
{
    while (xpraWins)
	xpraForget(xpraWins);
}

/* the rectangle a top level window takes up, border included */
static void
xpraOuterBox(WindowPtr pTop, BoxPtr box)
{
    int bw = wBorderWidth(pTop);

    box->x1 = pTop->drawable.x - bw;
    box->y1 = pTop->drawable.y - bw;
    box->x2 = pTop->drawable.x + pTop->drawable.width + bw;
    box->y2 = pTop->drawable.y + pTop->drawable.height + bw;
}

/* does a window count for placement: mapped, not a popup, not pSelf */
static Bool
xpraPlaced(WindowPtr pTop, WindowPtr pSelf)
{
    return pTop != pSelf && pTop->viewable && !pTop->overrideRedirect &&
	pTop->drawable.width >= 2 && pTop->drawable.height >= 2;
}

/* is this part of the screen clear of the other windows? */
static Bool
xpraBoxFree(BoxPtr box, WindowPtr pSelf)
{
    WindowPtr pSib;
    BoxRec other;

    if (box->x1 < 0 || box->y1 < 0 || box->x2 > rfbScreen.width ||
	box->y2 > rfbScreen.height)
	return FALSE;
    for (pSib = WindowTable[0]->firstChild; pSib; pSib = pSib->nextSib) {
	if (!xpraPlaced(pSib, pSelf))
	    continue;
	xpraOuterBox(pSib, &other);
	if (box->x1 < other.x2 && box->x2 > other.x1 &&
	    box->y1 < other.y2 && box->y2 > other.y1)
	    return FALSE;
    }
    return TRUE;
}

#define XPRA_PLACE_GAP	4

/*
 * Without a window manager: move a window that lies over another, or
 * partly off the screen, to where it is clear.  The places tried are the
 * top left corner and beside and below each of the other windows; the one
 * nearest the top, then nearest the left, is taken.  If the window fits
 * nowhere it stays where it is.
 */

static void
xpraPlace(WindowPtr pTop)
{
    WindowPtr pSib;
    BoxRec box, other;
    int w, h, i, bestX = -1, bestY = -1;
    XID vlist[2];

    xpraOuterBox(pTop, &box);
    if (xpraBoxFree(&box, pTop))
	return;
    w = box.x2 - box.x1;
    h = box.y2 - box.y1;
    if (w > rfbScreen.width || h > rfbScreen.height)
	return;

    for (pSib = NULL;;) {
	int cand[4][2], n = 0;

	if (!pSib) {
	    cand[n][0] = 0;
	    cand[n++][1] = 0;
	} else if (xpraPlaced(pSib, pTop)) {
	    xpraOuterBox(pSib, &other);
	    cand[n][0] = other.x2 + XPRA_PLACE_GAP;
	    cand[n++][1] = other.y1;
	    cand[n][0] = other.x1;
	    cand[n++][1] = other.y2 + XPRA_PLACE_GAP;
	    cand[n][0] = other.x2 + XPRA_PLACE_GAP;
	    cand[n++][1] = 0;
	    cand[n][0] = 0;
	    cand[n++][1] = other.y2 + XPRA_PLACE_GAP;
	}
	for (i = 0; i < n; i++) {
	    int x = cand[i][0], y = cand[i][1];

	    if (bestX >= 0 && (y > bestY || (y == bestY && x >= bestX)))
		continue;
	    box.x1 = x;
	    box.y1 = y;
	    box.x2 = x + w;
	    box.y2 = y + h;
	    if (xpraBoxFree(&box, pTop)) {
		bestX = x;
		bestY = y;
	    }
	}
	pSib = pSib ? pSib->nextSib : WindowTable[0]->firstChild;
	if (!pSib)
	    break;
    }
    if (bestX < 0)
	return;
    vlist[0] = (XID)bestX;
    vlist[1] = (XID)bestY;
    ConfigureWindow(pTop, CWX | CWY, vlist, serverClient);
}

/*
 * Compare the top level windows with what the client has been told:
 * announce the new ones, take back the ones that have gone, report
 * changes of size, place and title.
 */

static void
xpraScan(void)
{
    XpraClient *c = xpraUI;
    WindowPtr pTop, pApp;
    XpraWin *win, *next;
    CARD32 now = GetTimeInMillis();
    Bool bare = xpraWMClient() == NULL;

    if (!c)
	return;
    for (win = xpraWins; win; win = win->next)
	win->seen = FALSE;

    /* from the bottom up, so that a popup comes after what it is over */
    for (pTop = WindowTable[0]->lastChild; pTop; pTop = pTop->prevSib) {
	int x, y, w, h;

	if (!pTop->viewable)
	    continue;
	pApp = xpraAppWindow(pTop);
	w = pApp->drawable.width;
	h = pApp->drawable.height;
	/* a new window, or a new size: it may be in the way of another */
	if (bare && xpraPlaced(pTop, NULL)) {
	    win = xpraFindXid(pApp->drawable.id);
	    if (!win || !win->sent || w != win->w || h != win->h)
		xpraPlace(pTop);
	}
	x = pApp->drawable.x;
	y = pApp->drawable.y;
	/* not the 1x1 and off-screen windows toolkits keep for themselves */
	if (w < 2 || h < 2 || x >= rfbScreen.width || y >= rfbScreen.height ||
	    x + w <= 0 || y + h <= 0)
	    continue;

	win = xpraFindXid(pApp->drawable.id);
	if (!win) {
	    win = (XpraWin *)calloc(1, sizeof(XpraWin));
	    if (!win)
		continue;
	    win->wid = xpraNextWid++;
	    win->xid = pApp->drawable.id;
	    win->or = pTop->overrideRedirect;
	    win->x = x;
	    win->y = y;
	    win->w = w;
	    win->h = h;
	    win->next = xpraWins;
	    xpraWins = win;
	}
	win->seen = TRUE;

	if (!win->sent) {
	    win->x = x;
	    win->y = y;
	    win->w = w;
	    win->h = h;
	    xpraAnnounce(c, win, pApp);
	    continue;
	}

	if (w != win->w || h != win->h) {
	    win->w = w;
	    win->h = h;
	    win->refresh = TRUE;
	}
	if (win->or) {
	    if (x != win->x || y != win->y || w != win->cw || h != win->ch) {
		int cx, cy;

		win->x = x;
		win->y = y;
		xpraClientPosition(win, &cx, &cy);
		xpraBegin("window-move-resize");
		xwPutInt(&xpraPkt, win->wid);
		xwPutInt(&xpraPkt, cx);
		xwPutInt(&xpraPkt, cy);
		xwPutInt(&xpraPkt, w);
		xwPutInt(&xpraPkt, h);
		xpraSend(c);
		win->cx = cx;
		win->cy = cy;
		win->cw = w;
		win->ch = h;
	    }
	} else {
	    win->x = x;
	    win->y = y;
	    /*
	     * The client asked for one size and the application took
	     * another (or changed size by itself): once things have
	     * settled, the client is told.
	     */
	    if ((w != win->cw || h != win->ch) &&
		(int)(now - win->quietUntil) >= 0) {
		xpraBegin("window-resized");
		xwPutInt(&xpraPkt, win->wid);
		xwPutInt(&xpraPkt, w);
		xwPutInt(&xpraPkt, h);
		xpraSend(c);
		win->cw = w;
		win->ch = h;
	    }
	}

	if (xpraMetaHash(pApp) != win->metaHash) {
	    win->metaHash = xpraMetaHash(pApp);
	    xpraBegin("window-metadata");
	    xwPutInt(&xpraPkt, win->wid);
	    xpraPutMetadata(&xpraPkt, win, pApp);
	    xpraSend(c);
	}
    }

    for (win = xpraWins; win; win = next) {
	next = win->next;
	if (!win->seen)
	    xpraForget(win);
    }
}


/****************************************************************************/
/* pixels */

/* how the client is to read a row of the 32 bit framebuffer */
static const char *
xpraRgbFormat(void)
{
    if (rfbServerFormat.bitsPerPixel == 32 && rfbServerFormat.trueColour &&
	rfbServerFormat.redMax == 255 && rfbServerFormat.greenMax == 255 &&
	rfbServerFormat.blueMax == 255 && rfbServerFormat.greenShift == 8) {
	Bool blueLow = rfbServerFormat.blueShift == 0 &&
	    rfbServerFormat.redShift == 16;
	Bool redLow = rfbServerFormat.redShift == 0 &&
	    rfbServerFormat.blueShift == 16;

	/* shifts count from the low byte: which that is, memory decides */
	if (rfbServerFormat.bigEndian) {
	    if (blueLow)
		return "XRGB";
	    if (redLow)
		return "XBGR";
	} else {
	    if (blueLow)
		return "BGRX";
	    if (redLow)
		return "RGBX";
	}
    }
    return NULL;
}

/* any other depth: one pixel at a time to blue, green, red, unused */
static void
xpraConvertRow(const unsigned char *src, unsigned char *dst, int w)
{
    int bytes = rfbServerFormat.bitsPerPixel / 8;
    int i;

    for (i = 0; i < w; i++, src += bytes, dst += 4) {
	unsigned long p;
	unsigned int r, g, b;

	if (bytes == 1) {
	    p = src[0];
	} else if (bytes == 2) {
	    CARD16 v;

	    memcpy(&v, src, 2);
	    p = v;
	} else {
	    CARD32 v;

	    memcpy(&v, src, 4);
	    p = v;
	}
	if (rfbServerFormat.trueColour) {
	    r = (p >> rfbServerFormat.redShift) & rfbServerFormat.redMax;
	    g = (p >> rfbServerFormat.greenShift) & rfbServerFormat.greenMax;
	    b = (p >> rfbServerFormat.blueShift) & rfbServerFormat.blueMax;
	    r = rfbServerFormat.redMax ? r * 255 / rfbServerFormat.redMax : 0;
	    g = rfbServerFormat.greenMax ? g * 255 / rfbServerFormat.greenMax : 0;
	    b = rfbServerFormat.blueMax ? b * 255 / rfbServerFormat.blueMax : 0;
	} else if (rfbInstalledColormap &&
		   p < (unsigned long)rfbInstalledColormap->pVisual->ColormapEntries) {
	    EntryPtr e = &rfbInstalledColormap->red[p];

	    if (e->fShared) {
		r = e->co.shco.red->color >> 8;
		g = e->co.shco.green->color >> 8;
		b = e->co.shco.blue->color >> 8;
	    } else {
		r = e->co.local.red >> 8;
		g = e->co.local.green >> 8;
		b = e->co.local.blue >> 8;
	    }
	} else {
	    r = g = b = 0;
	}
	dst[0] = (unsigned char)b;
	dst[1] = (unsigned char)g;
	dst[2] = (unsigned char)r;
	dst[3] = 0;
    }
}

/* one rectangle of a window, given in screen coordinates */
static void
xpraSendBox(XpraClient *c, XpraWin *win, WindowPtr pWin, BoxPtr box)
{
    const char *format = xpraRgbFormat();
    int bpp = rfbServerFormat.bitsPerPixel / 8;
    int x1 = box->x1, y1 = box->y1, x2 = box->x2, y2 = box->y2;
    int w, bandH, y;

    if (x1 < 0)
	x1 = 0;
    if (y1 < 0)
	y1 = 0;
    if (x2 > rfbScreen.width)
	x2 = rfbScreen.width;
    if (y2 > rfbScreen.height)
	y2 = rfbScreen.height;
    w = x2 - x1;
    if (w <= 0 || y2 <= y1 || bpp < 1 || bpp > 4 || bpp == 3)
	return;
    bandH = XPRA_BAND_BYTES / (w * 4);
    if (bandH < 1)
	bandH = 1;

    for (y = y1; y < y2 && !c->dead && !c->closing; y += bandH) {
	int h = y2 - y < bandH ? y2 - y : bandH;
	size_t rowBytes = (size_t)w * 4, size = rowBytes * (size_t)h;
	const unsigned char *data;
	size_t len, zlen = 0;
	int row;

	xwBufReset(&xpraPix);
	if (!xwBufReserve(&xpraPix, size))
	    return;
	for (row = 0; row < h; row++) {
	    const unsigned char *src = (unsigned char *)rfbScreen.pfbMemory +
		(size_t)(y + row) * rfbScreen.paddedWidthInBytes +
		(size_t)x1 * bpp;

	    if (format)
		memcpy(xpraPix.data + row * rowBytes, src, rowBytes);
	    else
		xpraConvertRow(src, xpraPix.data + row * rowBytes, w);
	}
	data = xpraPix.data;
	len = size;
	if (c->lz4 && size >= XPRA_MIN_LZ4) {
	    xwBufReset(&xpraZ);
	    if (xwBufReserve(&xpraZ, xwLz4Bound(size)))
		zlen = xwLz4Compress(xpraPix.data, size, xpraZ.data, xpraZ.cap);
	    if (zlen > 0 && zlen < size) {
		data = xpraZ.data;
		len = zlen;
	    } else {
		zlen = 0;
	    }
	}

	xpraBegin("draw");
	xwPutInt(&xpraPkt, win->wid);
	xwPutInt(&xpraPkt, x1 - pWin->drawable.x);
	xwPutInt(&xpraPkt, y - pWin->drawable.y);
	xwPutInt(&xpraPkt, w);
	xwPutInt(&xpraPkt, h);
	xwPutStr(&xpraPkt, "rgb24");
	xwPutBytes(&xpraPkt, data, len);
	xwPutInt(&xpraPkt, (long long)++c->seq);
	xwPutInt(&xpraPkt, (long long)rowBytes);
	xwDictOpen(&xpraPkt);
	xwKeyStr(&xpraPkt, "rgb_format", format ? format : "BGRX");
	if (zlen)
	    xwKeyInt(&xpraPkt, "lz4", 1);
	xwClose(&xpraPkt);
	xpraSend(c);
	if (c->unacked++ == 0)
	    c->lastAck = GetTimeInMillis();
    }
}

/* send what has changed in the windows; TRUE if something is left to send */
static Bool
xpraFlush(void)
{
    XpraClient *c = xpraUI;
    ScreenPtr pScreen = screenInfo.screens[0];
    XpraWin *win;
    Bool pending = FALSE;

    if (!c || c->dead || c->closing || !xpraDamageValid)
	return FALSE;
    for (win = xpraWins; win && !pending; win = win->next)
	pending = win->refresh && win->sent && win->mapped;
    if (!pending && !REGION_NOTEMPTY(pScreen, &xpraDamage))
	return FALSE;

    /*
     * A client that is behind is not sent more: the changes add up here
     * and go out as one when it has caught up.
     */
    if (c->out.len - c->outPos > XPRA_BAND_BYTES)
	return TRUE;
    if (c->unacked > XPRA_MAX_UNACKED) {
	if ((int)(GetTimeInMillis() - c->lastAck) < XPRA_ACK_WAIT_MS)
	    return TRUE;
	c->unacked = 0;
    }

    /* the client draws the pointer itself: not in the pixels it gets */
    if (rfbScreen.cursorIsDrawn)
	rfbSpriteRemoveCursor(pScreen);

    for (win = xpraWins; win; win = win->next) {
	WindowPtr pWin = xpraLookup(win->xid);
	RegionRec visible, send;
	BoxPtr rects;
	int n, i;

	if (!pWin || !pWin->viewable || !win->sent || !win->mapped)
	    continue;
	REGION_INIT(pScreen, &visible, NullBox, 0);
	REGION_INIT(pScreen, &send, NullBox, 0);
	REGION_INTERSECT(pScreen, &visible, &pWin->borderClip, &pWin->winSize);
	if (win->refresh)
	    REGION_COPY(pScreen, &send, &visible);
	else
	    REGION_INTERSECT(pScreen, &send, &xpraDamage, &visible);
	win->refresh = FALSE;

	n = REGION_NUM_RECTS(&send);
	rects = REGION_RECTS(&send);
	if (n > 1) {
	    /* many small pieces cost more than the rectangle around them */
	    BoxRec ext = *REGION_EXTENTS(pScreen, &send);
	    long area = 0, extArea = (long)(ext.x2 - ext.x1) * (ext.y2 - ext.y1);

	    for (i = 0; i < n; i++)
		area += (long)(rects[i].x2 - rects[i].x1) *
		    (rects[i].y2 - rects[i].y1);
	    /* ...when all of it is this window's to show */
	    if ((n > 16 || area * 2 > extArea) &&
		RECT_IN_REGION(pScreen, &visible, &ext) == rgnIN) {
		xpraSendBox(c, win, pWin, &ext);
		n = 0;
	    }
	}
	for (i = 0; i < n; i++)
	    xpraSendBox(c, win, pWin, &rects[i]);
	REGION_UNINIT(pScreen, &visible);
	REGION_UNINIT(pScreen, &send);
	if (c->dead || c->closing)
	    break;
    }
    REGION_EMPTY(pScreen, &xpraDamage);
    if (!c->dead)
	xpraWrite(c);
    return FALSE;
}

/*
 * The pointer's shape, when it is not the one the client was sent last.
 * X cursors here are two bitmaps, the shape and a mask, and two colours:
 * sent as blue, green, red, alpha, opaque where the mask is set.  For the
 * root window's cursor (the X of an X server with nothing under the
 * pointer) the client is told to use its own.
 */

static void
xpraSendCursor(XpraClient *c)
{
    ScreenPtr pScreen = screenInfo.screens[0];
    CursorPtr pCursor = rfbSpriteGetCursorPtr(pScreen);
    CursorBitsPtr bits = pCursor ? pCursor->bits : NULL;
    unsigned long hash = 1;
    int w = 0, h = 0, rowBytes = 0, x, y, px, py;
    Bool lsb = screenInfo.bitmapBitOrder == LSBFirst;
    unsigned char *out;

    if (!c->cursors || c->dead || c->closing)
	return;
    if (pCursor == rootCursor || !bits || !bits->source || !bits->mask ||
	bits->width < 1 || bits->height < 1 ||
	bits->width > XPRA_MAX_CURSOR || bits->height > XPRA_MAX_CURSOR) {
	bits = NULL;
    } else {
	w = bits->width;
	h = bits->height;
	rowBytes = PixmapBytePad(w, 1);
	hash = xpraHash(2166136261UL, &bits->width, 4 * sizeof(bits->width));
	hash = xpraHash(hash, &pCursor->foreRed, 6 * sizeof(pCursor->foreRed));
	hash = xpraHash(hash, bits->source, (size_t)rowBytes * h);
	hash = xpraHash(hash, bits->mask, (size_t)rowBytes * h);
	if (hash == 1)
	    hash = 2;
    }
    if (hash == c->cursorHash)
	return;
    c->cursorHash = hash;

    xpraBegin("cursor");
    if (!bits) {
	xwPutStr(&xpraPkt, "");
	xpraSend(c);
	return;
    }
    xwBufReset(&xpraPix);
    if (!xwBufReserve(&xpraPix, (size_t)w * h * 4))
	return;
    out = xpraPix.data;
    for (y = 0; y < h; y++) {
	for (x = 0; x < w; x++, out += 4) {
	    int byte = y * rowBytes + x / 8;
	    int bit = lsb ? 1 << (x & 7) : 0x80 >> (x & 7);

	    if (!(bits->mask[byte] & bit)) {
		out[0] = out[1] = out[2] = out[3] = 0;
	    } else if (bits->source[byte] & bit) {
		out[0] = (unsigned char)(pCursor->foreBlue >> 8);
		out[1] = (unsigned char)(pCursor->foreGreen >> 8);
		out[2] = (unsigned char)(pCursor->foreRed >> 8);
		out[3] = 255;
	    } else {
		out[0] = (unsigned char)(pCursor->backBlue >> 8);
		out[1] = (unsigned char)(pCursor->backGreen >> 8);
		out[2] = (unsigned char)(pCursor->backRed >> 8);
		out[3] = 255;
	    }
	}
    }
    rfbSpriteGetCursorPos(pScreen, &px, &py);
    xwPutStr(&xpraPkt, "raw");
    xwPutInt(&xpraPkt, px);
    xwPutInt(&xpraPkt, py);
    xwPutInt(&xpraPkt, w);
    xwPutInt(&xpraPkt, h);
    xwPutInt(&xpraPkt, bits->xhot < w ? bits->xhot : w - 1);
    xwPutInt(&xpraPkt, bits->yhot < h ? bits->yhot : h - 1);
    xwPutInt(&xpraPkt, (long long)++c->cursorSerial);
    xwPutBytes(&xpraPkt, xpraPix.data, (size_t)w * h * 4);
    xwPutStr(&xpraPkt, "");
    xpraSend(c);
}

static CARD32
xpraTimerCallback(OsTimerPtr timer, CARD32 now, pointer arg)
{
    XpraClient *c;
    Bool more;

    xpraTimerSet = FALSE;
    if (xpraBusy) {
	/* not from inside the handling of a packet: very soon instead */
	xpraTimerSet = TRUE;
	return 1;
    }
    xpraBusy = TRUE;
    for (c = xpraClients; c; c = c->next) {
	if (!c->dead && c->outPos < c->out.len)
	    xpraWrite(c);
    }
    xpraScan();
    if (xpraUI) {
	xpraSendCursor(xpraUI);
	if (!xpraUI->dead)
	    xpraWrite(xpraUI);
    }
    more = xpraFlush();
    xpraBusy = FALSE;
    if (xpraTimerSet)			/* set again from in there */
	return 0;
    if (!xpraUI)
	return 0;
    xpraTimerSet = TRUE;
    return more ? XPRA_DEFER_MS : XPRA_IDLE_MS;
}

static void
xpraSchedule(int ms)
{
    if (xpraTimerSet && ms >= XPRA_IDLE_MS)
	return;
    /* a timer waiting out the idle period is brought forward */
    xpraTimer = TimerSet(xpraTimer, 0, (CARD32)ms, xpraTimerCallback, NULL);
    xpraTimerSet = TRUE;
}

void
xpraAddDamage(ScreenPtr pScreen, RegionPtr pRegion)
{
    static Bool soon = FALSE;

    if (!xpraUI || !xpraDamageValid)
	return;
    /* the pointer being drawn or removed is not a change to a window */
    if (rfbScreen.dontSendFramebufferUpdate)
	return;
    if (!REGION_NOTEMPTY(pScreen, &xpraDamage))
	soon = FALSE;
    REGION_UNION(pScreen, &xpraDamage, &xpraDamage, pRegion);
    if (!soon || !xpraTimerSet) {
	soon = TRUE;
	xpraSchedule(XPRA_DEFER_MS);
    }
}


/****************************************************************************/
/* what the client asks for */

static void
xpraRaise(WindowPtr pWin)
{
    WindowPtr pTop = xpraTopLevel(pWin), pSib;
    ClientPtr wm = xpraWMClient();
    XID vlist[1];

    /* leave it be if only menus and the like are above it */
    for (pSib = pTop->prevSib; pSib; pSib = pSib->prevSib) {
	if (pSib->viewable && !pSib->overrideRedirect)
	    break;
    }
    if (!pSib)
	return;
    vlist[0] = Above;
    /* as the window manager, or it would be asked instead */
    ConfigureWindow(pTop, CWStackMode, vlist, wm ? wm : serverClient);
}

static void
xpraResize(XpraWin *win, WindowPtr pWin, int w, int h)
{
    XID vlist[2];

    if (win->or)
	return;
    if (w < 1)
	w = 1;
    if (h < 1)
	h = 1;
    if (w > rfbScreen.width)
	w = rfbScreen.width;
    if (h > rfbScreen.height)
	h = rfbScreen.height;
    win->cw = w;
    win->ch = h;
    if (w == (int)pWin->drawable.width && h == (int)pWin->drawable.height)
	return;
    vlist[0] = (XID)w;
    vlist[1] = (XID)h;
    /* a window manager, if there is one, gets to carry this out */
    ConfigureWindow(pWin, CWWidth | CWHeight, vlist, serverClient);
    win->quietUntil = GetTimeInMillis() + XPRA_QUIET_MS;
    xpraSchedule(XPRA_DEFER_MS);
}

/* map-window and configure-window: wid, x, y, w, h, ... */
static void
xpraHandleGeometry(XwValue *p, Bool map)
{
    XpraWin *win = xpraFindWin((int)xwInt(xwItem(p, 1), 0));
    WindowPtr pWin;
    int w, h;

    if (!win || !(pWin = xpraLookup(win->xid)))
	return;
    /* configure-window: "skip geometry" is the ninth item */
    if (!map && xwBool(xwItem(p, 9), 0))
	return;
    win->cx = (int)xwInt(xwItem(p, 2), 0);
    win->cy = (int)xwInt(xwItem(p, 3), 0);
    win->haveClientPos = TRUE;
    w = (int)xwInt(xwItem(p, 4), win->w);
    h = (int)xwInt(xwItem(p, 5), win->h);
    if (map && !win->mapped) {
	win->mapped = TRUE;
	win->refresh = TRUE;
	xpraSchedule(XPRA_DEFER_MS);
    }
    xpraResize(win, pWin, w, h);
}

static void
xpraHandleClose(XwValue *p)
{
    XpraWin *win = xpraFindWin((int)xwInt(xwItem(p, 1), 0));
    WindowPtr pWin;
    ClientPtr owner;
    CARD32 *v;
    long n, i;

    if (!win || !(pWin = xpraLookup(win->xid)))
	return;
    owner = wClient(pWin);
    if (!owner || owner == serverClient || owner->clientGone)
	return;
    v = xpraProp32(pWin, atomWM_PROTOCOLS, &n);
    for (i = 0; v && i < n; i++) {
	if (v[i] == atomWM_DELETE_WINDOW) {
	    xEvent ev;

	    memset(&ev, 0, sizeof(ev));
	    ev.u.u.type = ClientMessage | 0x80;
	    ev.u.u.detail = 32;
	    ev.u.clientMessage.window = win->xid;
	    ev.u.clientMessage.u.l.type = atomWM_PROTOCOLS;
	    ev.u.clientMessage.u.l.longs0 = atomWM_DELETE_WINDOW;
	    ev.u.clientMessage.u.l.longs1 = currentTime.milliseconds;
	    /* no mask and no filter: to the owner, whatever it selected */
	    TryClientEvents(owner, &ev, 1, NoEventMask, NoEventMask, NullGrab);
	    return;
	}
    }
    /* it has no polite way: what a window manager does then */
    rfbLog("xpra: window %d cannot be asked to close, closing its client\n",
	   win->wid);
    CloseDownClient(owner);
}

static void
xpraHandleFocus(XwValue *p)
{
    int wid = (int)xwInt(xwItem(p, 1), 0);
    XpraWin *win = xpraFindWin(wid);
    WindowPtr pWin;

    if (!win || win->or || !(pWin = xpraLookup(win->xid)) || !pWin->viewable) {
	if (wid <= 0) {
	    /* the user has gone elsewhere: no key stays down */
	    xpraFocusWid = 0;
	    KbdReleaseAllKeys();
	}
	return;
    }
    xpraFocusWid = wid;
    xpraRaise(pWin);
    SetInputFocus(serverClient, inputInfo.keyboard, win->xid,
		  RevertToPointerRoot, CurrentTime, FALSE);
}

/*
 * The position is [x, y] on the client's screen and, from clients that
 * know where in the window it is, [x, y, in-window x, in-window y].
 */

static void
xpraHandlePointer(XwValue *widv, XwValue *pos, int button, Bool pressed)
{
    XpraWin *win = xpraFindWin((int)xwInt(widv, 0));
    WindowPtr pWin;
    int x, y;

    if (!win || !(pWin = xpraLookup(win->xid)) || !pWin->viewable)
	return;
    if (!pos || pos->type != XW_LIST || pos->n < 2)
	return;
    if (pos->n >= 4) {
	x = pWin->drawable.x + (int)xwInt(xwItem(pos, 2), 0);
	y = pWin->drawable.y + (int)xwInt(xwItem(pos, 3), 0);
    } else {
	x = (int)xwInt(xwItem(pos, 0), 0);
	y = (int)xwInt(xwItem(pos, 1), 0);
	if (win->haveClientPos) {
	    x += pWin->drawable.x - win->cx;
	    y += pWin->drawable.y - win->cy;
	}
    }
    if (x < 0)
	x = 0;
    if (y < 0)
	y = 0;
    if (x >= rfbScreen.width)
	x = rfbScreen.width - 1;
    if (y >= rfbScreen.height)
	y = rfbScreen.height - 1;

    if (button >= 1 && button <= 5) {
	if (pressed) {
	    if (!win->or)
		xpraRaise(pWin);
	    xpraButtonMask |= 1 << (button - 1);
	} else {
	    xpraButtonMask &= ~(1 << (button - 1));
	}
    }
    PtrAddEvent(xpraButtonMask, x, y, NULL);
    /* it may be over something with another pointer shape now */
    xpraSchedule(XPRA_DEFER_MS);
}

/* key-action: wid, key name, pressed, modifiers, keysym, text, keycode, group */
static void
xpraHandleKey(XwValue *p)
{
    Bool pressed = xwBool(xwItem(p, 3), 0);
    long long keysym = xwInt(xwItem(p, 5), 0);

    if (keysym <= 0 || keysym > 0x1fffffff)
	return;
    KbdAddEvent(pressed, (KeySym)keysym, NULL);
}

static void
xpraSendHello(XpraClient *c)
{
    char host[256], disp[32];
    time_t now = time(NULL);
    int maxw = rfbScreen.width > 8192 ? rfbScreen.width : 8192;
    int maxh = rfbScreen.height > 8192 ? rfbScreen.height : 8192;
    XwBuf *b = &xpraPkt;

    host[0] = '\0';
    gethostname(host, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';
    sprintf(disp, ":%.20s", display);

    xpraBegin("hello");
    xwDictOpen(b);
    xwKeyStr(b, "version", XPRA_VERSION);
    xwKeyStr(b, "tightvnc", XVNCRELEASE);
    xwKeyBool(b, "rencodeplus", 1);
    xwKeyBool(b, "lz4", 1);
    xwKeyBool(b, "none", 1);
    xwPutStr(b, "encoders");
    xwListOpen(b);
    xwPutStr(b, "rencodeplus");
    xwClose(b);
    xwPutStr(b, "compressors");
    xwListOpen(b);
    xwPutStr(b, "lz4");
    xwPutStr(b, "none");
    xwClose(b);
    xwKeyStr(b, "server.mode", "seamless");
    xwKeyStr(b, "session-type", "seamless");
    xwKeyStr(b, "session_name", desktopName);
    xwKeyStr(b, "hostname", host);
    xwKeyStr(b, "display", disp);
    xwKeyInt(b, "start_time", (long long)xpraStartTime);
    xwKeyInt(b, "current_time", (long long)now);
    xwKeyInt(b, "elapsed_time", (long long)(now - xpraStartTime));
    xwKeyBool(b, "windows", 1);
    xwKeyBool(b, "keyboard", 1);
    xwKeyBool(b, "cursors", 1);
    xwKeyBool(b, "bell", 0);
    xwKeyBool(b, "pointer.relative", 1);
    xwKeyBool(b, "readonly", 0);
    xwKeyBool(b, "sharing", 0);
    xwKeyBool(b, "lock", 0);
    xwKeyBool(b, "client-shutdown", 0);
    xwKeyPair(b, "actual_desktop_size", rfbScreen.width, rfbScreen.height);
    xwKeyPair(b, "root_window_size", rfbScreen.width, rfbScreen.height);
    xwKeyPair(b, "desktop_size", rfbScreen.width, rfbScreen.height);
    /*
     * The windows live on the client's desktop, wherever it puts them: a
     * client told its screen is bigger than ours would scale them to fit.
     */
    xwKeyPair(b, "max_desktop_size", maxw, maxh);
    xwKeyStr(b, "encoding", "rgb");
    xwPutStr(b, "encodings");
    xwDictOpen(b);
    xwPutStr(b, "");
    xwListOpen(b);
    xwPutStr(b, "rgb");
    xwClose(b);
    xwPutStr(b, "core");
    xwListOpen(b);
    xwPutStr(b, "rgb24");
    xwPutStr(b, "rgb32");
    xwClose(b);
    xwClose(b);
    xwClose(b);
    xpraSend(c);
}

static void
xpraHandleHello(XpraClient *c, XwValue *p)
{
    XwValue *caps = xwItem(p, 1), *v;
    char request[32];

    if (c->hello || !caps || caps->type != XW_DICT) {
	xpraDisconnect(c, "invalid hello");
	return;
    }
    c->hello = TRUE;
    v = xwGet(caps, "lz4");
    c->lz4 = xwListHas(xwGet(caps, "compressors"), "lz4") ||
	(v && (v->type == XW_DICT || xwBool(v, 0)));
    /* pointer shapes go as plain pixels: every client takes those */
    v = xwGet(xwGet(caps, "cursor"), "encodings");
    c->cursors = xwBool(xwGet(caps, "cursors"), 0) &&
	(!v || xwListHas(v, "raw"));
    if (!xwGet(caps, "rencodeplus") &&
	!xwListHas(xwGet(caps, "encoders"), "rencodeplus")) {
	xpraDisconnect(c, "this server speaks rencodeplus only");
	return;
    }

    xpraSendHello(c);
    xwStr(xwGet(caps, "request"), request, sizeof(request));
    if (request[0]) {
	/* "xpra info", "xpra id", ...: the hello is the answer */
	c->closing = TRUE;
	xpraWrite(c);
	return;
    }

    if (xpraUI && xpraUI != c)
	xpraDisconnect(xpraUI, "a new client has connected");
    xpraForgetAll();
    xpraUI = c;
    c->ui = TRUE;
    rfbLog("xpra: %s attached\n", c->name);
    xpraScan();
    xpraBegin("startup-complete");
    xpraSend(c);
    xpraWrite(c);
    xpraSchedule(XPRA_DEFER_MS);
}

static void
xpraDispatch(XpraClient *c, XwValue *p)
{
    XwValue *type = xwItem(p, 0);

    if (xwIsStr(type, "hello")) {
	xpraHandleHello(c, p);
	return;
    }
    if (xwIsStr(type, "disconnect") || xwIsStr(type, "connection-lost")) {
	xpraCloseClient(c, "the client disconnected");
	return;
    }
    if (xwIsStr(type, "ping")) {
	xpraBegin("ping_echo");
	xwPutInt(&xpraPkt, xwInt(xwItem(p, 1), 0));
	xwPutInt(&xpraPkt, 0);
	xwPutInt(&xpraPkt, 0);
	xwPutInt(&xpraPkt, 0);
	xwPutInt(&xpraPkt, -1);
	xpraSend(c);
	if (!c->dead)
	    xpraWrite(c);
	return;
    }
    /* everything else is for the client showing the windows */
    if (!c->hello || c != xpraUI)
	return;

    if (xwIsStr(type, "damage-sequence")) {
	/* seq, wid, width, height, decode time, message */
	if (c->unacked > 0)
	    c->unacked--;
	c->lastAck = GetTimeInMillis();
	if (xwInt(xwItem(p, 5), 0) < 0) {
	    XpraWin *win = xpraFindWin((int)xwInt(xwItem(p, 2), 0));

	    /* it could not draw that: all of the window again */
	    if (win)
		win->refresh = TRUE;
	}
	xpraSchedule(1);
    } else if (xwIsStr(type, "pointer")) {
	/* device, sequence, wid, position, properties */
	xpraHandlePointer(xwItem(p, 3), xwItem(p, 4), 0, FALSE);
    } else if (xwIsStr(type, "pointer-button")) {
	/* device, sequence, wid, button, pressed, position, properties */
	xpraHandlePointer(xwItem(p, 3), xwItem(p, 6),
			  (int)xwInt(xwItem(p, 4), 0), xwBool(xwItem(p, 5), 0));
    } else if (xwIsStr(type, "pointer-position")) {
	/* wid, position, modifiers, buttons */
	xpraHandlePointer(xwItem(p, 1), xwItem(p, 2), 0, FALSE);
    } else if (xwIsStr(type, "button-action")) {
	/* wid, button, pressed, position, modifiers, buttons */
	xpraHandlePointer(xwItem(p, 1), xwItem(p, 4),
			  (int)xwInt(xwItem(p, 2), 0), xwBool(xwItem(p, 3), 0));
    } else if (xwIsStr(type, "key-action")) {
	xpraHandleKey(p);
    } else if (xwIsStr(type, "focus")) {
	xpraHandleFocus(p);
    } else if (xwIsStr(type, "map-window")) {
	xpraHandleGeometry(p, TRUE);
    } else if (xwIsStr(type, "configure-window")) {
	xpraHandleGeometry(p, FALSE);
    } else if (xwIsStr(type, "unmap-window")) {
	XpraWin *win = xpraFindWin((int)xwInt(xwItem(p, 1), 0));

	if (win && !win->or)
	    win->mapped = FALSE;
    } else if (xwIsStr(type, "close-window")) {
	xpraHandleClose(p);
    } else if (xwIsStr(type, "buffer-refresh")) {
	int wid = (int)xwInt(xwItem(p, 1), -1);
	XpraWin *win;

	for (win = xpraWins; win; win = win->next) {
	    if (wid <= 0 || win->wid == wid)
		win->refresh = TRUE;
	}
	xpraSchedule(XPRA_DEFER_MS);
    }
    /* the rest (keymaps, clipboard, logging...) is not acted upon */
}


/****************************************************************************/
/* input from the sockets */

static void
xpraParse(XpraClient *c)
{
    size_t pos = 0;

    while (!c->dead && !c->closing && c->in.len - pos >= XW_HEADER_SIZE) {
	const unsigned char *p = c->in.data + pos;
	const unsigned char *data;
	size_t len;
	XwHeader h;
	XwValue *v;
	int i;

	if (xwParseHeader(p, &h) < 0) {
	    rfbLog("xpra: %s: not a packet header: %02x %02x %02x %02x %02x "
		   "%02x %02x %02x (at %lu of %lu)\n", c->name, p[0], p[1], p[2],
		   p[3], p[4], p[5], p[6], p[7], (unsigned long)pos,
		   (unsigned long)c->in.len);
	    xpraCloseClient(c, "not an xpra client");
	    return;
	}
	if (h.flags & XW_FLAG_CIPHER) {
	    xpraDisconnect(c, "encryption is not supported");
	    break;
	}
	if (h.size > (c->hello ? XPRA_MAX_PACKET : XPRA_MAX_FIRST_PACKET)) {
	    xpraDisconnect(c, "packet too large");
	    break;
	}
	if (c->in.len - pos - XW_HEADER_SIZE < h.size)
	    break;			/* not all here yet */
	data = p + XW_HEADER_SIZE;
	len = (size_t)h.size;
	pos += XW_HEADER_SIZE + len;

	if (h.level & XW_LEVEL_LZ4) {
	    long size = xwLz4Size(data, len);

	    xwBufReset(&xpraPlain);
	    if (size < 0 || size > XPRA_MAX_PACKET ||
		!xwBufReserve(&xpraPlain, (size_t)size + 1) ||
		xwLz4Decompress(data, len, xpraPlain.data, (size_t)size) != size) {
		xpraDisconnect(c, "invalid compressed packet");
		break;
	    }
	    data = xpraPlain.data;
	    len = (size_t)size;
	} else if (h.level != 0) {
	    xpraDisconnect(c, "compression not supported (lz4 is)");
	    break;
	}

	if (h.index > 0) {
	    /* a raw chunk: an item of the packet that follows */
	    if (c->haveChunk[h.index] || c->nChunks >= 4) {
		xpraDisconnect(c, "invalid raw packet");
		break;
	    }
	    xwBufReset(&c->chunk[h.index]);
	    xwBufAppend(&c->chunk[h.index], data, len);
	    if (c->chunk[h.index].failed) {
		xpraCloseClient(c, "out of memory");
		return;
	    }
	    c->haveChunk[h.index] = TRUE;
	    c->nChunks++;
	    continue;
	}

	if (!(h.flags & XW_FLAG_RENCODEPLUS)) {
	    xpraDisconnect(c, "this server speaks rencodeplus only");
	    break;
	}
	xwArenaReset(xpraArena);
	v = xwDecode(xpraArena, data, len);
	if (!v || v->type != XW_LIST || !v->first ||
	    (v->first->type != XW_STR && v->first->type != XW_BYTES)) {
	    xpraDisconnect(c, "invalid packet");
	    break;
	}
	for (i = 1; i < XW_MAX_CHUNKS; i++) {
	    if (c->haveChunk[i]) {
		XwValue *item = xwItem(v, i);

		if (item) {
		    item->type = XW_BYTES;
		    item->s = c->chunk[i].data;
		    item->n = c->chunk[i].len;
		    item->first = NULL;
		}
	    }
	}
	xpraDispatch(c, v);
	for (i = 1; i < XW_MAX_CHUNKS; i++) {
	    if (c->haveChunk[i]) {
		c->haveChunk[i] = FALSE;
		xwBufFree(&c->chunk[i]);
	    }
	}
	c->nChunks = 0;
    }
    if (!c->dead)
	xwBufConsume(&c->in, pos);
}

static void
xpraRead(XpraClient *c)
{
    size_t got = 0;

    while (!c->dead && got < XPRA_READ_AT_ONCE) {
	int n;

	if (!xwBufReserve(&c->in, 65536)) {
	    xpraCloseClient(c, "out of memory");
	    return;
	}
	n = recv(c->sock, (char *)c->in.data + c->in.len, 65536, 0);
	if (n > 0) {
	    c->in.len += (size_t)n;
	    got += (size_t)n;
	    if (n < 65536)
		break;
	} else if (n == 0) {
	    /* "xpra list" connects and leaves: not worth a line in the log */
	    xpraCloseClient(c, c->ui ? "connection closed" : NULL);
	    return;
	} else if (errno == EINTR) {
	    continue;
	} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
	    break;
	} else {
	    xpraCloseClient(c, strerror(errno));
	    return;
	}
    }
    if (c->closing) {
	/* nothing more is wanted from it */
	c->in.len = 0;
	return;
    }
    xpraParse(c);
}

static void
xpraNewClient(int listener, Bool tcp)
{
    XpraClient *c;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    const int one = 1;
    int sock;

    sock = accept(listener, tcp ? (struct sockaddr *)&addr : NULL,
		  tcp ? &addrlen : NULL);
    if (sock < 0)
	return;
    if (sock >= FD_SETSIZE || fcntl(sock, F_SETFL, O_NONBLOCK) < 0 ||
	!(c = (XpraClient *)calloc(1, sizeof(XpraClient)))) {
	close(sock);
	return;
    }
    if (tcp) {
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one));
	sprintf(c->name, "%.40s:%d", inet_ntoa(addr.sin_addr),
		(int)ntohs(addr.sin_port));
    } else {
	sprintf(c->name, "local client %d", sock);
    }
    c->sock = sock;
    xwBufInit(&c->in);
    xwBufInit(&c->out);
    c->lastAck = GetTimeInMillis();
    c->next = xpraClients;
    xpraClients = c;
    AddEnabledDevice(sock);
    FD_SET(sock, &xpraFds);
    if (sock > xpraMaxFd)
	xpraMaxFd = sock;
}

/* why == NULL: closed by us, as intended */
static void
xpraCloseClient(XpraClient *c, const char *why)
{
    if (c->dead)
	return;
    c->dead = TRUE;
    if (why && (c->ui || !c->hello))
	rfbLog("xpra: %s: %s\n", c->name, why);
    RemoveEnabledDevice(c->sock);
    FD_CLR(c->sock, &xpraFds);
    close(c->sock);
    c->sock = -1;
    if (c == xpraUI) {
	rfbLog("xpra: %s detached\n", c->name);
	xpraUI = NULL;
	xpraForgetAll();
	/* nothing it held down stays down */
	KbdReleaseAllKeys();
	if (xpraButtonMask) {
	    int x, y;

	    xpraButtonMask = 0;
	    GetSpritePosition(&x, &y);
	    PtrAddEvent(0, x, y, NULL);
	}
    }
}

static void
xpraReap(void)
{
    XpraClient **prev = &xpraClients, *c;
    int i;

    while ((c = *prev)) {
	if (!c->dead) {
	    prev = &c->next;
	    continue;
	}
	*prev = c->next;
	xwBufFree(&c->in);
	xwBufFree(&c->out);
	for (i = 0; i < XW_MAX_CHUNKS; i++)
	    xwBufFree(&c->chunk[i]);
	free(c);
    }
}

void
xpraCheckFds(void)
{
    XpraClient *c, *next;
    struct timeval tv;
    fd_set fds;
    int n;

    if (!xpraEnabled || xpraMaxFd < 0 || xpraBusy)
	return;
    memcpy((char *)&fds, (char *)&xpraFds, sizeof(fd_set));
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    n = select(xpraMaxFd + 1, &fds, NULL, NULL, &tv);
    if (n <= 0)
	return;
    xpraBusy = TRUE;
    if (xpraUnixSock >= 0 && FD_ISSET(xpraUnixSock, &fds))
	xpraNewClient(xpraUnixSock, FALSE);
    if (xpraTcpSock >= 0 && FD_ISSET(xpraTcpSock, &fds))
	xpraNewClient(xpraTcpSock, TRUE);
    for (c = xpraClients; c; c = next) {
	next = c->next;
	if (!c->dead && c->sock >= 0 && FD_ISSET(c->sock, &fds))
	    xpraRead(c);
    }
    xpraReap();
    xpraBusy = FALSE;
}


/****************************************************************************/
/* starting and stopping */

static void
xpraListen(int sock, const char *what)
{
    if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0 || listen(sock, 8) < 0) {
	rfbLogPerror("xpra: listen");
	FatalError("Xvnc: cannot set up the Xpra socket\n");
    }
    AddEnabledDevice(sock);
    FD_SET(sock, &xpraFds);
    if (sock > xpraMaxFd)
	xpraMaxFd = sock;
    rfbLog("Listening for Xpra connections on %s\n", what);
}

/* the socket "xpra attach :display" looks for: ~/.xpra/host-display */
static Bool
xpraDefaultSocketPath(char *path, size_t size)
{
    const char *home = getenv("HOME");
    char host[256], dir[512];
    struct passwd *pw;

    if (!home || !home[0]) {
	pw = getpwuid(getuid());
	home = pw ? pw->pw_dir : NULL;
    }
    host[0] = '\0';
    gethostname(host, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';
    if (!home || !host[0] || strlen(home) + 8 > sizeof(dir))
	return FALSE;
    sprintf(dir, "%s/.xpra", home);
    if (mkdir(dir, 0700) < 0 && errno != EEXIST)
	return FALSE;
    if (strlen(dir) + strlen(host) + strlen(display) + 3 > size)
	return FALSE;
    snprintf(path, size, "%s/%s-%s", dir, host, display);
    return TRUE;
}

static void
xpraInitSockets(void)
{
    FD_ZERO(&xpraFds);

    if (xpraUnixWanted) {
	struct sockaddr_un addr;
	mode_t mask;
	int probe;

	if (xpraSocketArg) {
	    if (strlen(xpraSocketArg) >= sizeof(xpraSocketPath)) {
		rfbLog("xpra: socket path too long\n");
		FatalError("Xvnc: cannot set up the Xpra socket\n");
	    }
	    strcpy(xpraSocketPath, xpraSocketArg);
	} else if (!xpraDefaultSocketPath(xpraSocketPath,
					  sizeof(xpraSocketPath))) {
	    rfbLog("xpra: cannot work out a socket path in ~/.xpra, "
		   "use -xprasocket\n");
	    FatalError("Xvnc: cannot set up the Xpra socket\n");
	}
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, xpraSocketPath);

	/* a socket nobody answers is left over from a dead server */
	probe = socket(AF_UNIX, SOCK_STREAM, 0);
	if (probe >= 0) {
	    if (connect(probe, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
		rfbLog("xpra: %s is in use by another server\n",
		       xpraSocketPath);
		FatalError("Xvnc: cannot set up the Xpra socket\n");
	    }
	    close(probe);
	}
	unlink(xpraSocketPath);

	xpraUnixSock = socket(AF_UNIX, SOCK_STREAM, 0);
	mask = umask(0077);		/* ours alone */
	if (xpraUnixSock < 0 || xpraUnixSock >= FD_SETSIZE ||
	    bind(xpraUnixSock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	    rfbLogPerror("xpra: unix socket");
	    xpraSocketPath[0] = '\0';
	    FatalError("Xvnc: cannot set up the Xpra socket\n");
	}
	umask(mask);
	xpraListen(xpraUnixSock, xpraSocketPath);
    }

    if (xpraPort) {
	struct sockaddr_in addr;
	const int one = 1;
	char what[64];

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)xpraPort);
	/* nobody is asked for a password: this machine only */
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	xpraTcpSock = socket(AF_INET, SOCK_STREAM, 0);
	if (xpraTcpSock >= 0)
	    setsockopt(xpraTcpSock, SOL_SOCKET, SO_REUSEADDR, (char *)&one,
		       sizeof(one));
	if (xpraTcpSock < 0 || xpraTcpSock >= FD_SETSIZE ||
	    bind(xpraTcpSock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	    rfbLogPerror("xpra: TCP port");
	    FatalError("Xvnc: cannot set up the Xpra socket\n");
	}
	sprintf(what, "TCP port %d (loopback only)", xpraPort);
	xpraListen(xpraTcpSock, what);
    }
}

#define XPRA_ATOM(name) MakeAtom(name, strlen(name), TRUE)

void
xpraInit(void)
{
    if (!xpraEnabled)
	return;

    if (!xpraSocketsDone) {
	xpraSocketsDone = TRUE;
	xpraStartTime = time(NULL);
	xwBufInit(&xpraPkt);
	xwBufInit(&xpraPix);
	xwBufInit(&xpraZ);
	xwBufInit(&xpraPlain);
	xpraArena = xwArenaNew();
	if (!xpraArena)
	    FatalError("xpraInit: out of memory\n");
	xpraInitSockets();
    }

    /*
     * This is also where a server reset comes through.  Then the windows
     * are gone, so are the atoms and the timers, and the screen's regions
     * have been freed with it.
     */
    xpraForgetAll();
    xpraBusy = FALSE;
    xpraTimer = NULL;
    xpraTimerSet = FALSE;
    xpraButtonMask = 0;
    xpraFocusWid = 0;
    xpraDamageValid = FALSE;

    atomWM_STATE = XPRA_ATOM("WM_STATE");
    atomWM_PROTOCOLS = XPRA_ATOM("WM_PROTOCOLS");
    atomWM_DELETE_WINDOW = XPRA_ATOM("WM_DELETE_WINDOW");
    atomNET_WM_NAME = XPRA_ATOM("_NET_WM_NAME");
    atomUTF8_STRING = XPRA_ATOM("UTF8_STRING");
    atomNET_WM_WINDOW_TYPE = XPRA_ATOM("_NET_WM_WINDOW_TYPE");
    atomNET_WM_PID = XPRA_ATOM("_NET_WM_PID");
}

/* called once the screen exists, see rfbScreenInit */
void
xpraScreenInit(ScreenPtr pScreen)
{
    if (!xpraEnabled)
	return;
    REGION_INIT(pScreen, &xpraDamage, NullBox, 0);
    xpraDamageValid = TRUE;
    if (xpraUI)
	xpraSchedule(XPRA_DEFER_MS);
}

void
xpraShutdown(void)
{
    XpraClient *c;

    if (!xpraEnabled)
	return;
    for (c = xpraClients; c; c = c->next) {
	if (c->sock >= 0)
	    close(c->sock);
    }
    if (xpraSocketPath[0] && xpraUnixSock >= 0)
	unlink(xpraSocketPath);
}
