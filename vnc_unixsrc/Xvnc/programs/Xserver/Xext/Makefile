SRCS = shape.c shm.c mbuf.c mitmisc.c xtest.c xtest1di.c xtest1dd.c sleepuntil.c bigreq.c sync.c xcmisc.c xprint.c
OBJS = $(SRCS:.c=.o)
INCS = -I../include -I../../../exports/include/X11 -I../../../include/extensions -I../hw/xfree86/common -I../hw/xfree86/os-support -I../../../exports/include
DEFS +=

all: libext.a

libext.a: $(OBJS)
	$(RM) $@
	$(AR) cqs $@ $^

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -o $@ $<

clean:
	$(RM) $(OBJS) libext.a

install:

.PHONY: all clean install
