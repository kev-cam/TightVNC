PIXELSIZE = 32

include ../Makefile.cfb-common

$(foreach SRC, $(SRCS), $(eval $(call link_source_file,$(SRC))))

clean:
	$(RM) $(SRCS) $(OBJS) libcfb.a
