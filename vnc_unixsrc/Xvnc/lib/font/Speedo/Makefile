SRCS = spfuncs.c spfile.c spinfo.c sperr.c spfont.c spglyph.c spencode.c do_char.c do_trns.c out_bl2d.c out_blk.c out_scrn.c out_util.c reset.c set_spcs.c set_trns.c
OBJS = $(SRCS:.c=.o)
INCS = -I../../../include/fonts -I../include -I../../../programs/Xserver/include
DEFS +=

all: $(OBJS)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -o $@ $<

clean:
	$(RM) $(OBJS)

install:

.PHONY: all clean install
