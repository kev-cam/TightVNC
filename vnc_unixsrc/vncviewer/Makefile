SRCS = argsresources.c caps.c colour.c cursor.c desktop.c dialogs.c fullscreen.c listen.c misc.c popup.c rfbproto.c selection.c shm.c sockets.c tunnel.c vncviewer.c
OBJS = $(SRCS:.c=.o)
INCS = -I. -I../include
LIBS = -lXaw -lXmu -lXt -lSM -lICE -lXpm -lX11 -lXext ../libvncauth/libvncauth.a -lz -ljpeg

all: vncviewer

vncviewer: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) -o $@ $<

clean:
	$(RM) $(OBJS) vncviewer

install:

.PHONY: all clean install
