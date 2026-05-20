SRCS = mivaltree.c mipolyseg.c mipolyrect.c mipoly.c mipolycon.c mipolygen.c mipolyutil.c mifillrct.c miwideline.c mispans.c miarc.c mizerarc.c mifillarc.c miwindow.c micursor.c miregion.c mipolytext.c mibitblt.c mipolypnt.c mipushpxl.c miexpose.c miglblt.c mizerline.c mifpolycon.c midash.c mibstore.c miinitext.c mieq.c mipointer.c misprite.c midispcur.c miscrinit.c miclipn.c migc.c
OBJS = $(SRCS:.c=.o)
INCS = -I. -I../include -I../../../include/fonts -I../../../exports/include/X11 -I../../../exports/include
DEFS +=

CBRTSRCS = cbrt.c
CBRTOBJS = $(CBRTSRCS:.c=.o)

all: libmi.a libcbrt.a

libmi.a: $(OBJS)
	$(RM) $@
	$(AR) cqs $@ $^

libcbrt.a: $(CBRTOBJS)
	$(RM) $@
	$(AR) cqs $@ $^

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -o $@ $<

clean:
	$(RM) $(OBJS) libmi.a $(CBRTOBJS) libcbrt.a

install:

.PHONY: all clean install
