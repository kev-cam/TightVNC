# The definitions every part of the server is compiled with (_XSERVER64 on
# a 64 bit machine, for one) are exported by Xvnc/Makefile.  Objects built
# without them lay out the server's structures differently, link all the
# same, and the server then crashes as it starts.
ifndef LN
$(error run make in vnc_unixsrc/Xvnc or above, not in this directory)
endif

SRCS = init.c sockets.c kbdptr.c cmap.c draw.c cutpaste.c dispcur.c sprite.c rfbserver.c translate.c httpd.c auth.c rre.c corre.c stats.c hextile.c zlib.c tight.c cursor.c
OBJS = $(SRCS:.c=.o)
INCS = -I. -I../../../../exports/include/X11 -I../../../../include/fonts -I../../cfb -I../../mfb -I../../mi -I../../include -I../../os -I../../../../../include -I../../../../exports/include
DEFS += -DDDXOSINIT -DSERVER_LOCK -DDDXOSFATALERROR -DDDXTIME -DPART_NET

all: libvnc.a

libvnc.a: $(OBJS)
	$(RM) $@
	$(AR) cqs $@ $^

init.o: init.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -Wno-error=format-security $(DEFS) $(INCS) -DXVNCRELEASE=\"TightVNC-1.3.10\" -o $@ $<

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(DEFS) $(INCS) -o $@ $<

clean:
	$(RM) $(OBJS) libvnc.a

install:

.PHONY: all clean install
