MAKECMDGOALS ?= all
SUBDIRS = libvncauth vncviewer vncpasswd vncconnect Xvnc

# This is C of the 1990s: old style definitions, functions never declared.
# gcc 14 and later take neither as anything newer than C90, so unless the
# caller has flags of its own:
CFLAGS ?= -O2 -g -std=gnu90
export CFLAGS

# Does the compiler find these headers?  (have-header says nothing.)
HAVE = CC="$(CC)" CPPFLAGS="$(CPPFLAGS)" sh ./have-header
SERVER_HEADERS = zlib.h jpeglib.h
VIEWER_HEADERS = X11/IntrinsicP.h X11/Xaw/Form.h X11/Xmu/StdSel.h X11/extensions/XShm.h zlib.h jpeglib.h
CONNECT_HEADERS = X11/Xlib.h

all: list_defines $(SUBDIRS)
	@$(HAVE) $(VIEWER_HEADERS) || echo "note: vncviewer was not built, see above"
	@$(HAVE) $(CONNECT_HEADERS) || echo "note: vncconnect was not built, see above"

clean install: $(SUBDIRS)

list_defines:
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -E -dM /dev/null | sort

vncviewer vncpasswd Xvnc: libvncauth

libvncauth vncpasswd:
	$(MAKE) -C $@ $(MAKECMDGOALS)

# Nothing of the X server can be built without zlib and libjpeg: better
# said here than by the compiler, four hundred files in.  A rule of its
# own, on which the X server depends, so that the recipes below stay plain
# $(MAKE) lines.
server-headers:
	@if [ "$(MAKECMDGOALS)" = clean ] || [ "$(MAKECMDGOALS)" = install ]; then :; \
	elif $(HAVE) $(SERVER_HEADERS); then :; \
	else \
	    echo "*** Xvnc needs zlib.h and jpeglib.h, and the compiler does not find them."; \
	    echo "*** Install zlib1g-dev and libjpeg-dev (their Debian names),"; \
	    echo "*** or point CPPFLAGS and LDFLAGS at where they are."; \
	    exit 1; \
	fi

Xvnc: server-headers
	$(MAKE) -C $@ $(MAKECMDGOALS)

# The viewer wants the X toolkit (Xt, Xaw, Xmu) and vncconnect wants Xlib.
# The X server wants neither, and a machine that is only to run the server
# may have the headers of neither: "make" then builds the rest and says
# what it left out, instead of stopping at the viewer.
vncviewer:
	@if [ "$(MAKECMDGOALS)" != all ] || $(HAVE) $(VIEWER_HEADERS); then \
	    $(MAKE) -C $@ $(MAKECMDGOALS); \
	else \
	    echo "*** vncviewer is not built: the compiler does not find the X toolkit headers"; \
	    echo "*** (libxaw7-dev libxmu-dev libxt-dev libxext-dev libjpeg-dev zlib1g-dev)."; \
	    echo "*** The X server does not need them."; \
	fi

vncconnect:
	@if [ "$(MAKECMDGOALS)" != all ] || $(HAVE) $(CONNECT_HEADERS); then \
	    $(MAKE) -C $@ $(MAKECMDGOALS); \
	else \
	    echo "*** vncconnect is not built: the compiler does not find X11/Xlib.h (libx11-dev)."; \
	fi

# the X server alone: needs zlib and libjpeg, none of the X libraries
server: server-headers
	$(MAKE) -C libvncauth all
	$(MAKE) -C Xvnc all

# the tests of the server that has been built (tests/run-tests)
check:
	$(MAKE) -C tests check

.PHONY: all clean install server server-headers check list_defines $(SUBDIRS)
