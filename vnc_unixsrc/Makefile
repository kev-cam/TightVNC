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

clean: $(SUBDIRS)

# Where "make install" puts what has been built, and under which names:
# the ones Debian gives them, which the vncserver script here runs
# (Xtightvnc, tightvncpasswd).  In /usr/local/bin, which comes before
# /usr/bin on the usual PATH, they take the place of a distribution's
# tightvncserver without touching its files; "make install PREFIX=/usr"
# overwrites those.  DESTDIR is put in front of everything, for packaging.
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MAN1DIR = $(PREFIX)/share/man/man1
INSTALL ?= install

# what is built : the name it is installed under : its manual page
PROGRAMS = \
    Xvnc/programs/Xserver/Xvnc:Xtightvnc:Xvnc/programs/Xserver/Xvnc.man \
    vncserver:tightvncserver:vncserver.man \
    vncpasswd/vncpasswd:tightvncpasswd:vncpasswd/vncpasswd.man \
    vncconnect/vncconnect:tightvncconnect:vncconnect/vncconnect.man \
    vncviewer/vncviewer:xtightvncviewer:vncviewer/vncviewer.man

# Installs what is there and builds nothing (it is often run as root): the
# X server has to have been built, the viewer and the rest go in if they were.
# It stops if a source has changed since it was compiled (after a git pull,
# say), or if the X server's last build did not get as far as linking it:
# what would go in is then the programs from before, not what the tree has.
# Only sources that have an object are looked at, so a file the build does
# not compile cannot stop it.  The vncserver script goes in with the commit
# it is installed from (build-id) written into it, for its -version.
install:
	@if [ ! -x Xvnc/programs/Xserver/Xvnc ]; then \
	    echo "*** The X server has not been built: run make (or make server) first."; \
	    exit 1; \
	fi
	@changed=$$(find Xvnc libvncauth vncviewer vncpasswd vncconnect -name '*.o' 2>/dev/null | \
	    while read o; do \
		c=$${o%.o}.c; \
		if [ -e "$$c" ] && [ "$$c" -nt "$$o" ]; then echo "$$c"; break; fi; \
	    done); \
	if [ -n "$$changed" ]; then \
	    echo "*** $$changed has changed since it was compiled: run make (or make server)"; \
	    echo "*** first, or what goes in is the programs from before the change."; \
	    exit 1; \
	fi
	@if [ -n "$$(find Xvnc -name '*.o' -newer Xvnc/programs/Xserver/Xvnc 2>/dev/null | head -1)" ]; then \
	    echo "*** The X server's last build did not finish: run make (or make server) first."; \
	    exit 1; \
	fi
	@$(INSTALL) -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(MAN1DIR)"
	@id=$$(sh ./build-id 2>/dev/null); [ -n "$$id" ] || id=unknown; \
	for p in $(PROGRAMS); do \
	    file=$${p%%:*}; rest=$${p#*:}; name=$${rest%%:*}; man=$${rest#*:}; \
	    if [ -f "$$file" ]; then \
		if [ "$$file" = vncserver ]; then \
		    sed "s/@BUILD_ID@/$$id/" vncserver > "$(DESTDIR)$(BINDIR)/$$name.new" && \
		    chmod 755 "$(DESTDIR)$(BINDIR)/$$name.new" && \
		    mv -f "$(DESTDIR)$(BINDIR)/$$name.new" "$(DESTDIR)$(BINDIR)/$$name" || exit 1; \
		else \
		    $(INSTALL) -m 755 "$$file" "$(DESTDIR)$(BINDIR)/$$name" || exit 1; \
		fi; \
		$(INSTALL) -m 644 "$$man" "$(DESTDIR)$(MAN1DIR)/$$name.1" || exit 1; \
		echo "installed $(DESTDIR)$(BINDIR)/$$name"; \
	    else \
		echo "not built, not installed: $$name"; \
	    fi; \
	done

# takes away exactly the files "make install" puts there
uninstall:
	@for p in $(PROGRAMS); do \
	    rest=$${p#*:}; name=$${rest%%:*}; \
	    for f in "$(DESTDIR)$(BINDIR)/$$name" "$(DESTDIR)$(MAN1DIR)/$$name.1"; do \
		if [ -f "$$f" ]; then unlink "$$f" && echo "removed $$f"; fi; \
	    done; \
	done

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
	@if [ "$(MAKECMDGOALS)" = clean ]; then :; \
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

# What a machine that runs the server needs: the X server and vncpasswd
# (the vncserver script, which is not built, asks it for the password).
# Needs zlib and libjpeg, none of the X libraries.
server: server-headers
	$(MAKE) -C libvncauth all
	$(MAKE) -C vncpasswd all
	$(MAKE) -C Xvnc all

# the tests of the server that has been built (tests/run-tests)
check:
	$(MAKE) -C tests check

.PHONY: all clean install uninstall server server-headers check list_defines $(SUBDIRS)
