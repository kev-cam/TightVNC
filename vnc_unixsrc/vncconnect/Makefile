SRCS = vncconnect.c
OBJS = $(SRCS:.c=.o)
INCS =
LIBS = -lX11 -lXext

all: vncconnect

vncconnect: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) -o $@ $<

clean:
	$(RM) $(OBJS) vncconnect

install:

.PHONY: all clean install
