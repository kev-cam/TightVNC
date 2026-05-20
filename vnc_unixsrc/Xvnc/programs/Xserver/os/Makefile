SRCS = WaitFor.c access.c connection.c io.c oscolor.c osinit.c utils.c auth.c mitauth.c secauth.c xdmauth.c xdmcp.c decompress.c transport.c xalloc.c
OBJS = $(SRCS:.c=.o)
INCS = -I. -I../include -I../../../exports/include/X11 -I../../../include/extensions -I../../../lib/Xau -I../lbx -I../../../exports/include
DEFS += -DHASXDMAUTH -DDDXOSINIT -DSERVER_LOCK -DDDXOSFATALERROR -DDDXTIME -DPART_NET -DUSE_RGB_TXT -DXDMCP -DXSERV_t -DTRANS_SERVER -DUNIXCONN -DTCPCONN

XTRANS_INCS = -I../include -I../../../include -I../../../lib/xtrans
XTRANS_DEFS =

all: libos.a

libos.a: $(OBJS)
	$(RM) $@
	$(AR) cqs $@ $^

transport.o: transport.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -Wno-error=format-security $(XTRANS_INCS) $(DEFS) $(XTRANS_DEFS) -o $@ $<

transport.c: ../../../lib/xtrans/transport.c
	$(LN) $< $@

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -o $@ $<

clean:
	$(RM) $(OBJS) transport.c libos.a

install:

.PHONY: all clean install
