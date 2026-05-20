HDRS = FS.h FSproto.h fsmasks.h

all:
	mkdir -p ../../exports/include/X11/fonts && \
		cd ../../exports/include/X11/fonts && \
		for h in $(HDRS); do $(LN) ../../../../include/fonts/$$h .; done

clean:
	$(RM) -r ../../exports/include/X11/fonts

install:

.PHONY: all clean install
