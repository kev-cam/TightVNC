DATABITS64 = $(shell $(CC) -x c -E -dM /dev/null | grep 'SIZE_WIDTH.*64')

export LN = ln -snf
# see ../Makefile
CFLAGS ?= -O2 -g -std=gnu90
export CFLAGS
export DEFS = $(if $(DATABITS64),-D_XSERVER64,) -D_POSIX_C_SOURCE=199309L -D_POSIX_SOURCE -D_XOPEN_SOURCE=500L -D_DEFAULT_SOURCE -D_REENTRANT -DNDEBUG -DFUNCPROTO=15 -DNARROWPROTO

SUBDIRS = include lib programs

all clean install: $(SUBDIRS)

lib: include
programs: include lib

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: all clean install $(SUBDIRS)
