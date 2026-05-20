SRCS = bitmap/?*.c fontfile/?*.c fc/?*.c Speedo/?*.c Type1/?*.c util/?*.c
OBJS = $(SRCS:.c=.o)
INCS +=
SUBDIRS = $(patsubst %/?*.c,%,$(SRCS))

all: libfont.a

libfont.a: $(SUBDIRS)
	$(RM) $@
	$(AR) cqs $@ $(OBJS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

clean: $(SUBDIRS)
	$(RM) libfont.a

install:

.PHONY: all clean install $(SUBDIRS)
