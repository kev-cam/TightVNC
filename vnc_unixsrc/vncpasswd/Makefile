SRCS = vncpasswd.c
OBJS = $(SRCS:.c=.o)

INCS = -I../include

LIBS = ../libvncauth/libvncauth.a

all: vncpasswd

vncpasswd: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) -o $@ $<

clean:
	$(RM) $(OBJS) vncpasswd

install:

.PHONY: all clean install
