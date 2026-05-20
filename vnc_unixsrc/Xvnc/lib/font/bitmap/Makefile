SRCS = bdfread.c bdfutils.c bitmap.c bitmaputil.c bitscale.c bitmapfunc.c pcfread.c pcfwrite.c snfread.c fontink.c
OBJS = $(SRCS:.c=.o)
INCS = -I../../../include/fonts -I../include
DEFS += -DX_GZIP_FONT_COMPRESSION

all: $(OBJS)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -o $@ $<

clean:
	$(RM) $(OBJS)

install:

.PHONY: all clean install
