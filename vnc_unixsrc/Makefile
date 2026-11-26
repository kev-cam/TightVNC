MAKECMDGOALS ?= all
SUBDIRS = libvncauth vncviewer vncpasswd vncconnect Xvnc

# This is C of the 1990s: old style definitions, functions never declared.
# gcc 14 and later take neither as anything newer than C90, so unless the
# caller has flags of its own:
CFLAGS ?= -O2 -g -std=gnu90
export CFLAGS

all: list_defines $(SUBDIRS)

clean install: $(SUBDIRS)

list_defines:
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -E -dM /dev/null | sort

vncviewer vncpasswd Xvnc: libvncauth

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

# the X server alone: needs zlib and libjpeg, none of the X toolkit
# libraries the viewer does
server:
	$(MAKE) -C libvncauth all
	$(MAKE) -C Xvnc all

# the tests of the server that has been built (tests/run-tests)
check:
	$(MAKE) -C tests check

.PHONY: all clean install server check list_defines $(SUBDIRS)
