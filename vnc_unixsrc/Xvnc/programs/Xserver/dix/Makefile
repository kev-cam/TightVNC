SRCS = atom.c colormap.c cursor.c devices.c dispatch.c dixutils.c events.c extension.c gc.c globals.c glyphcurs.c grabs.c main.c property.c resource.c swaprep.c swapreq.c tables.c window.c initatoms.c dixfonts.c privates.c pixmap.c
OBJS = $(SRCS:.c=.o)
INCS = -I../include -I../../../exports/include/X11 -I../../../include/fonts -I../../../include/extensions
DEFS +=

XPSRCS = xpstubs.c
XPOBJS = $(XPSRCS:.c=.o)

all: libdix.a libxpstubs.a

libdix.a: $(OBJS)
	$(RM) $@
	$(AR) cqs $@ $^

libxpstubs.a: $(XPOBJS)
	$(RM) $@
	$(AR) cqs $@ $^

globals.o: globals.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DCOMPILEDDEFAULTFONTPATH=\"/usr/share/fonts/X11/misc/,/usr/share/fonts/X11/Speedo/,/usr/share/fonts/X11/Type1/,/usr/share/fonts/X11/75dpi/,/usr/share/fonts/X11/100dpi/\" -DRGB_DB=\"/usr/share/X11/rgb\" -o $@ $<

main.o: main.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DVENDOR_STRING="\"AT&T Laboratories Cambridge\"" -DVENDOR_RELEASE=3332 -o $@ $<

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -o $@ $<

clean:
	$(RM) $(OBJS) libdix.a $(XPOBJS) libxpstubs.a

install:

.PHONY: all clean install
