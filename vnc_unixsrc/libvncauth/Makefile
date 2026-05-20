SRCS = vncauth.c d3des.c
OBJS = $(SRCS:.c=.o)
INCS = -I. -I../include

all: libvncauth.a

libvncauth.a: $(OBJS)
	$(RM) $@
	$(AR) cqs $@ $^

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) -o $@ $<

clean:
	$(RM) $(OBJS) libvncauth.a

install:

.PHONY: all clean install
