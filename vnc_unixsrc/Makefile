MAKECMDGOALS ?= all
SUBDIRS = libvncauth vncviewer vncpasswd vncconnect Xvnc

all: list_defines $(SUBDIRS)

clean install: $(SUBDIRS)

list_defines:
	$(CC) $(CPPFLAGS) $(CFLAGS) -x c -E -dM /dev/null | sort

vncviewer vncpasswd Xvnc: libvncauth

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: all clean install list_defines $(SUBDIRS)
