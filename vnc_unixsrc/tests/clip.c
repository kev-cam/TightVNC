/*
 * clip - own or read an X selection, for the tests of xpra-view.
 *
 *   clip own SELECTION TEXT [CHUNK]   own it with TEXT until another takes it;
 *                                     with CHUNK, texts longer than that go in
 *                                     parts (INCR)
 *   clip own SELECTION @FILE [CHUNK]  the same, with the contents of FILE
 *   clip get SELECTION [TARGET]       print it (UTF8_STRING unless TARGET;
 *                                     TARGETS prints one name a line);
 *                                     exit status 1 if there is none
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: 2026 D. Kevin Cameron
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

static Display *dpy;

static int
wait_event(XEvent *ev, int seconds)
{
    int fd = ConnectionNumber(dpy);

    while (!XPending(dpy)) {
	fd_set fds;
	struct timeval tv = {seconds, 0};

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0)
	    return 0;
    }
    XNextEvent(dpy, ev);
    return 1;
}

/* an INCR transfer under way: one requestor at a time is enough here */
static Window incr_win = None;
static Atom incr_prop, incr_type;
static size_t incr_pos;

static int
own(Atom selection, const char *text, size_t len, size_t chunk)
{
    Window w = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    Atom targets = XInternAtom(dpy, "TARGETS", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    Atom textA = XInternAtom(dpy, "TEXT", False);
    Atom incr = XInternAtom(dpy, "INCR", False);
    XEvent ev;

    XSetSelectionOwner(dpy, selection, w, CurrentTime);
    if (XGetSelectionOwner(dpy, selection) != w) {
	fprintf(stderr, "clip: could not own the selection\n");
	return 1;
    }
    printf("owned\n");
    fflush(stdout);
    for (;;) {
	XNextEvent(dpy, &ev);
	if (ev.type == SelectionClear)
	    return 0;
	if (ev.type == PropertyNotify && ev.xproperty.window == incr_win &&
	    ev.xproperty.atom == incr_prop && ev.xproperty.state == PropertyDelete) {
	    size_t n = len - incr_pos < chunk ? len - incr_pos : chunk;

	    XChangeProperty(dpy, incr_win, incr_prop, incr_type, 8, PropModeReplace,
			    (const unsigned char *)text + incr_pos, (int)n);
	    incr_pos += n;
	    if (n == 0) {
		XSelectInput(dpy, incr_win, NoEventMask);
		incr_win = None;
	    }
	    XFlush(dpy);
	    continue;
	}
	if (ev.type == SelectionRequest) {
	    XSelectionRequestEvent *r = &ev.xselectionrequest;
	    XEvent reply;
	    Atom prop = r->property != None ? r->property : r->target;

	    memset(&reply, 0, sizeof(reply));
	    reply.xselection.type = SelectionNotify;
	    reply.xselection.requestor = r->requestor;
	    reply.xselection.selection = r->selection;
	    reply.xselection.target = r->target;
	    reply.xselection.time = r->time;
	    reply.xselection.property = prop;
	    if (r->target == targets) {
		long list[] = {targets, utf8, XA_STRING, textA};

		XChangeProperty(dpy, r->requestor, prop, XA_ATOM, 32, PropModeReplace,
				(unsigned char *)list, 4);
	    } else if (r->target == utf8 || r->target == XA_STRING || r->target == textA) {
		Atom type = r->target == textA ? utf8 : r->target;

		if (chunk && len > chunk && incr_win == None) {
		    long size = (long)len;

		    incr_win = r->requestor;
		    incr_prop = prop;
		    incr_type = type;
		    incr_pos = 0;
		    XSelectInput(dpy, incr_win, PropertyChangeMask);
		    XChangeProperty(dpy, r->requestor, prop, incr, 32, PropModeReplace,
				    (unsigned char *)&size, 1);
		} else {
		    XChangeProperty(dpy, r->requestor, prop, type, 8, PropModeReplace,
				    (const unsigned char *)text, (int)len);
		}
	    } else {
		reply.xselection.property = None;
	    }
	    XSendEvent(dpy, r->requestor, False, NoEventMask, &reply);
	    XFlush(dpy);
	}
    }
}

static int
get(Atom selection, const char *target_name)
{
    Window w = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    Atom target = XInternAtom(dpy, target_name, False);
    Atom prop = XInternAtom(dpy, "CLIP_TEST", False);
    Atom incr = XInternAtom(dpy, "INCR", False);
    Atom type;
    int format;
    unsigned long n, after;
    unsigned char *data;
    XEvent ev;

    XSelectInput(dpy, w, PropertyChangeMask);
    XConvertSelection(dpy, selection, target, prop, w, CurrentTime);
    do {
	if (!wait_event(&ev, 10))
	    return 2;			/* no answer at all */
    } while (ev.type != SelectionNotify);
    if (ev.xselection.property == None)
	return 1;
    if (XGetWindowProperty(dpy, w, prop, 0, 1 << 24, True, AnyPropertyType, &type, &format,
			   &n, &after, &data) != Success)
	return 1;
    if (type == incr) {
	XFree(data);
	for (;;) {
	    do {
		if (!wait_event(&ev, 10))
		    return 2;
	    } while (ev.type != PropertyNotify || ev.xproperty.atom != prop ||
		     ev.xproperty.state != PropertyNewValue);
	    if (XGetWindowProperty(dpy, w, prop, 0, 1 << 24, True, AnyPropertyType, &type,
				   &format, &n, &after, &data) != Success)
		return 1;
	    if (n == 0) {
		XFree(data);
		return 0;
	    }
	    fwrite(data, 1, n, stdout);
	    XFree(data);
	}
    }
    if (format == 32 && type == XA_ATOM) {
	unsigned long i;

	for (i = 0; i < n; i++) {
	    char *name = XGetAtomName(dpy, ((Atom *)data)[i]);

	    printf("%s\n", name ? name : "?");
	    if (name)
		XFree(name);
	}
    } else if (format == 8) {
	fwrite(data, 1, n, stdout);
    }
    XFree(data);
    return 0;
}

int
main(int argc, char **argv)
{
    Atom selection;

    if (argc < 3 || !(dpy = XOpenDisplay(NULL))) {
	fprintf(stderr, "usage: clip own SELECTION TEXT|@FILE [CHUNK] | clip get SELECTION [TARGET]\n");
	return 2;
    }
    selection = XInternAtom(dpy, argv[2], False);
    if (strcmp(argv[1], "own") == 0 && argc >= 4) {
	const char *text = argv[3];
	size_t len = strlen(text);
	char *buf = NULL;

	if (text[0] == '@') {
	    FILE *f = fopen(text + 1, "rb");
	    long size;

	    if (!f || fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
		perror(text + 1);
		return 2;
	    }
	    buf = malloc((size_t)size + 1);
	    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
		perror(text + 1);
		return 2;
	    }
	    fclose(f);
	    text = buf;
	    len = (size_t)size;
	}
	return own(selection, text, len, argc >= 5 ? (size_t)atol(argv[4]) : 0);
    }
    if (strcmp(argv[1], "get") == 0)
	return get(selection, argc >= 4 ? argv[3] : "UTF8_STRING");
    fprintf(stderr, "clip: unknown command %s\n", argv[1]);
    return 2;
}
