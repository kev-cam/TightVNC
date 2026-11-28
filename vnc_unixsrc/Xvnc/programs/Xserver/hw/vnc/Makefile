# The definitions every part of the server is compiled with (_XSERVER64 on
# a 64 bit machine, for one) are exported by Xvnc/Makefile.  Objects built
# without them lay out the server's structures differently, link all the
# same, and the server then crashes as it starts.
ifndef LN
$(error run make in vnc_unixsrc/Xvnc or above, not in this directory)
endif

SRCS = init.c sockets.c kbdptr.c cmap.c draw.c cutpaste.c dispcur.c sprite.c rfbserver.c translate.c httpd.c auth.c rre.c corre.c stats.c hextile.c zlib.c tight.c cursor.c xpra.c xpra_wire.c
OBJS = $(SRCS:.c=.o)
INCS = -I. -I../../../../exports/include/X11 -I../../../../include/fonts -I../../cfb -I../../mfb -I../../mi -I../../include -I../../os -I../../../../../include -I../../../../exports/include
DEFS += -DDDXOSINIT -DSERVER_LOCK -DDDXOSFATALERROR -DDDXTIME -DPART_NET

# The release, and the commit the server is built from (vnc_unixsrc/build-id):
# -version, the log and the Xpra hello say both.  buildid.h is written
# again only when one of them changes, so that a build with nothing new
# compiles nothing.
RELEASE = TightVNC-1.3.11

all: libvnc.a

libvnc.a: $(OBJS)
	$(RM) $@
	$(AR) cqs $@ $^

buildid.h: FORCE
	@id=`sh ../../../../../build-id 2>/dev/null`; [ -n "$$id" ] || id=unknown; \
	printf '#define XVNCRELEASE "%s"\n#define XVNCBUILD "%s"\n' '$(RELEASE)' "$$id" > buildid.h.new; \
	if cmp -s buildid.h.new buildid.h; then rm -f buildid.h.new; else mv -f buildid.h.new buildid.h; fi

init.o: init.c buildid.h
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -Wno-error=format-security $(DEFS) $(INCS) -o $@ $<

xpra.o: xpra.c buildid.h
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(DEFS) $(INCS) -o $@ $<

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(DEFS) $(INCS) -o $@ $<

clean:
	$(RM) $(OBJS) libvnc.a buildid.h buildid.h.new

install:

# A command, not an empty rule: smak drops an empty rule's target as a
# prerequisite in a sub-make, and then never remakes buildid.h.  Phony, or
# "make -t" would make a file of that name, and buildid.h never again.
FORCE:
	@:

.PHONY: all clean install FORCE
