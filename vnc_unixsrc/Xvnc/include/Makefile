HDRS = DECkeysym.h HPkeysym.h Sunkeysym.h X.h XWDFile.h Xalloca.h Xatom.h Xfuncproto.h Xfuncs.h Xmd.h Xos.h Xos_r.h Xosdefs.h Xpoll.h Xproto.h Xprotostr.h Xthreads.h ap_keysym.h keysym.h keysymdef.h
X11HDRS = X11/cursorfont.h X11/Xcms.h X11/XKBlib.h X11/Xlib.h X11/Xlibint.h X11/Xlocale.h X11/Xresource.h X11/Xutil.h
XAUHDRS = Xau/Xauth.h
XDMCPHDRS = Xdmcp/Xdmcp.h
XTRANSHDRS = xtrans/Xtrans.h
LIBHDRS = $(X11HDRS) $(XAUHDRS) $(XDMCPHDRS) $(XTRANSHDRS)
SUBDIRS = bitmaps extensions fonts

all: $(SUBDIRS)
	mkdir -p ../exports/include/X11 && \
		cd ../exports/include/X11 && \
		for h in $(HDRS); do $(LN) ../../../include/$$h .; done && \
		for h in $(LIBHDRS); do $(LN) ../../../lib/$$h .; done

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

clean: $(SUBDIRS)
	$(RM) -r ../exports

install:

.PHONY: all clean install $(SUBDIRS)
