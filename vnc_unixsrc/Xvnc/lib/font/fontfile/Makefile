SRCS = dirfile.c fontdir.c fontfile.c fileio.c fontscale.c defaults.c bitsource.c register.c renderers.c bufio.c decompress.c filewr.c printerfont.c ffcheck.c gunzip.c
OBJS = $(SRCS:.c=.o)
INCS = -I../../../include/fonts -I../include -I../../../programs/Xserver/include
DEFS += -DX_GZIP_FONT_COMPRESSION -DFONTDIRATTRIB

all: $(OBJS)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -o $@ $<

clean:
	$(RM) $(OBJS)

install:

.PHONY: all clean install
