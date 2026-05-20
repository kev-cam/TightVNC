SRCS = utilbitmap.c fontnames.c fontutil.c fontxlfd.c format.c fontaccel.c atom.c miscutil.c private.c patcache.c
OBJS = $(SRCS:.c=.o)
INCS = -I../../../include/fonts -I../include
DEFS +=

all: $(OBJS)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -o $@ $<

clean:
	$(RM) $(OBJS)

install:

.PHONY: all clean install
