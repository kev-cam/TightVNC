SUBDIRS = font Xau Xdmcp

all clean install: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: all clean install $(SUBDIRS)
