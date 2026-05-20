SRCS = fsconvert.c fserve.c fsio.c transport.c
OBJS = $(SRCS:.c=.o)
INCS = -I../../../include/fonts -I../../../exports/include -I../include
DEFS +=

XTRANS_INCS = -I../../xtrans
XTRANS_DEFS = -DUNIXCONN -DTCPCONN -DFONT_t -DTRANS_CLIENT -DTRANS_SERVER -DTRANS_REOPEN

all: $(OBJS)

transport.o: transport.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -Wno-error=format-security $(XTRANS_INCS) $(DEFS) $(XTRANS_DEFS) -o $@ $<

transport.c: ../../xtrans/transport.c
	$(LN) $< $@

fsio.o: fsio.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) $(XTRANS_DEFS) -o $@ $<

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -o $@ $<

clean:
	$(RM) $(OBJS) transport.c

install:

.PHONY: all clean install
