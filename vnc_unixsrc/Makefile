MAKECMDGOALS ?= all
SUBDIRS = libvncauth vncviewer vncpasswd vncconnect Xvnc

all: list_defines $(SUBDIRS)

clean install: $(SUBDIRS)

list_defines:
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -E -dM /dev/null | sort

vncviewer vncpasswd Xvnc: libvncauth

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

# the tests of the server that has been built (tests/run-tests)
check:
	$(MAKE) -C tests check

.PHONY: all clean install check list_defines $(SUBDIRS)
