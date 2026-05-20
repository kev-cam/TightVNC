SRCS = arith.c curves.c fontfcn.c hints.c lines.c objects.c paths.c regions.c scanfont.c spaces.c t1funcs.c t1info.c t1io.c t1malloc.c t1snap.c t1stub.c token.c type1.c util.c
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
