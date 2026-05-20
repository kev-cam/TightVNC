DEFS += -DSHAPE -DSTATIC_COLOR -DAVOID_GLYPHBLT -DPIXPRIV -DMITMISC -DXTEST -DXSYNC -DXCMISC -DMITSHM -DBIGREQS -DGLX_MODULE # -DGCCUSESGAS # <- Only required for assembler optimization which has been disabled in Makefile.cfb-common
LIBS = dix/libdix.a os/libos.a ../../lib/Xau/libXau.a ../../lib/Xdmcp/libXdmcp.a ../../lib/font/libfont.a  hw/vnc/libvnc.a ../../../libvncauth/libvncauth.a cfb/libcfb.a cfb16/libcfb.a cfb24/libcfb.a cfb32/libcfb.a mfb/libmfb.a dix/libxpstubs.a mi/libmi.a Xext/libext.a -lm -lz -ljpeg
SUBDIRS = dix os mi Xext mfb cfb cfb16 cfb24 cfb32 hw/vnc

all: Xvnc

Xvnc: $(SUBDIRS)
	$(CC) $(LDFLAGS) -o $@ $(LIBS)

cfb16 cfb24 cfb32: cfb

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

clean: $(SUBDIRS)
	$(RM) Xvnc

install:

.PHONY: all clean install $(SUBDIRS)
