HDRS = Xext.h multibuf.h shape.h XShm.h MITMisc.h XI.h XInput.h XIproto.h    XTest.h xtestext1.h XIE.h XIEproto.h XIEprotost.h sync.h    XKB.h XKBgeom.h XKBproto.h XKBstr.h XKBsrv.h Xdbe.h Xdbeproto.h     Xag.h Xagstr.h Xagsrv.h     record.h recordstr.h         lbxbuf.h lbxbufstr.h lbxdeltastr.h lbximage.h         lbxopts.h lbxstr.h lbxzlib.h     security.h securstr.h   Print.h Printstr.h

all:
	mkdir -p ../../exports/include/X11/extensions && \
		cd ../../exports/include/X11/extensions && \
		for h in $(HDRS); do $(LN) ../../../../include/extensions/$$h .; done

clean:
	$(RM) -r ../../exports/include/X11/extensions

install:

.PHONY: all clean install
