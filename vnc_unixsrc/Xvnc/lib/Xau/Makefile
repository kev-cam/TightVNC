SRCS = AuDispose.c AuFileName.c AuGetAddr.c AuGetBest.c AuLock.c AuRead.c AuUnlock.c AuWrite.c
OBJS = $(SRCS:.c=.o)
INCS = -I../../exports/include
DEFS +=

all:libXau.a

libXau.a: $(OBJS)
	$(RM) $@
	$(AR) cqs $@ $^

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -o $@ $<

clean:
	$(RM) $(OBJS) libXau.a

install:

.PHONY: all clean install
