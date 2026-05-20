SRCS = mfbgc.c mfbwindow.c mfbfont.c mfbfillrct.c mfbpntwin.c maskbits.c mfbpixmap.c mfbimage.c mfbline.c mfbbres.c mfbhrzvert.c mfbbresd.c mfbpushpxl.c mfbzerarc.c mfbfillarc.c mfbfillsp.c mfbsetsp.c mfbscrinit.c mfbscrclse.c mfbclip.c mfbbitblt.c mfbgetsp.c mfbpolypnt.c mfbbltC.c mfbbltX.c mfbbltCI.c mfbbltO.c mfbbltG.c mfbcmap.c mfbtileC.c mfbtileG.c mfbmisc.c mfbbstore.c mfbseg.c mfbpgbwht.c mfbpgbblak.c mfbpgbinv.c mfbigbwht.c mfbigbblak.c mfbpawhite.c mfbpablack.c mfbpainv.c mfbtewhite.c mfbteblack.c mfbbltC.c mfbbltX.c mfbbltCI.c mfbbltO.c mfbbltG.c mfbtileC.c mfbtileG.c mfbplywhite.c mfbplyblack.c mfbplyinv.c
OBJS = $(SRCS:.c=.o)
INCS = -I. -I../mi -I../include -I../../../exports/include/X11 -I../../../include/fonts -I../../../exports/include
DEFS +=

all: libmfb.a

libmfb.a: $(OBJS)
	$(RM) $@
	$(AR) cqs $@ $^

mfbseg.c: mfbline.c
	$(LN) $< $@

mfbseg.o: mfbseg.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DPOLYSEGMENT -o $@ $<

mfbpgbwht.c: mfbplygblt.c
	$(LN) $< $@

mfbpgbwht.o: mfbpgbwht.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBPOLYGLYPHBLT=mfbPolyGlyphBltWhite -DOPEQ=MFB_OPEQ_WHITE -o $@ $<

mfbpgbblak.c: mfbplygblt.c
	$(LN) $< $@

mfbpgbblak.o: mfbpgbblak.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBPOLYGLYPHBLT=mfbPolyGlyphBltBlack -DOPEQ=MFB_OPEQ_BLACK -o $@ $<

mfbpgbinv.c: mfbplygblt.c
	$(LN) $< $@

mfbpgbinv.o: mfbpgbinv.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBPOLYGLYPHBLT=mfbPolyGlyphBltInvert -DOPEQ=MFB_OPEQ_INVERT -o $@ $<

mfbigbwht.c: mfbimggblt.c
	$(LN) $< $@

mfbigbwht.o: mfbigbwht.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBIMAGEGLYPHBLT=mfbImageGlyphBltWhite -DOPEQ=MFB_OPEQ_WHITE -o $@ $<

mfbigbblak.c: mfbimggblt.c
	$(LN) $< $@

mfbigbblak.o: mfbigbblak.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBIMAGEGLYPHBLT=mfbImageGlyphBltBlack -DOPEQ=MFB_OPEQ_BLACK -o $@ $<

mfbpawhite.c: mfbpntarea.c
	$(LN) $< $@

mfbpawhite.o: mfbpawhite.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBSOLIDFILLAREA=mfbSolidWhiteArea -DMFBSTIPPLEFILLAREA=mfbStippleWhiteArea -DOPEQ=MFB_OPEQ_WHITE -DEQWHOLEWORD=MFB_EQWHOLEWORD_WHITE -o $@ $<

mfbpablack.c: mfbpntarea.c
	$(LN) $< $@

mfbpablack.o: mfbpablack.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBSOLIDFILLAREA=mfbSolidBlackArea -DMFBSTIPPLEFILLAREA=mfbStippleBlackArea -DOPEQ=MFB_OPEQ_BLACK -DEQWHOLEWORD=MFB_EQWHOLEWORD_BLACK -o $@ $<

mfbpainv.c: mfbpntarea.c
	$(LN) $< $@

mfbpainv.o: mfbpainv.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBSOLIDFILLAREA=mfbSolidInvertArea -DMFBSTIPPLEFILLAREA=mfbStippleInvertArea -DOPEQ=MFB_OPEQ_INVERT -DEQWHOLEWORD=MFB_EQWHOLEWORD_INVERT -o $@ $<

mfbtewhite.c: mfbtegblt.c
	$(LN) $< $@

mfbtewhite.o: mfbtewhite.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBTEGLYPHBLT=mfbTEGlyphBltWhite -DOP=MFB_OP_WHITE -DCLIPTETEXT=mfbImageGlyphBltWhite -o $@ $<

mfbteblack.c: mfbtegblt.c
	$(LN) $< $@

mfbteblack.o: mfbteblack.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBTEGLYPHBLT=mfbTEGlyphBltBlack -DOP=MFB_OP_BLACK -DCLIPTETEXT=mfbImageGlyphBltBlack -o $@ $<

mfbplywhite.c: mfbply1rct.c
	$(LN) $< $@

mfbplywhite.o: mfbplywhite.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBFILLPOLY1RECT=mfbFillPolyWhite -DOPEQ=MFB_OPEQ_WHITE -DEQWHOLEWORD=MFB_EQWHOLEWORD_WHITE -o $@ $<

mfbplyblack.c: mfbply1rct.c
	$(LN) $< $@

mfbplyblack.o: mfbplyblack.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBFILLPOLY1RECT=mfbFillPolyBlack -DOPEQ=MFB_OPEQ_BLACK -DEQWHOLEWORD=MFB_EQWHOLEWORD_BLACK -o $@ $<

mfbplyinv.c: mfbply1rct.c
	$(LN) $< $@

mfbplyinv.o: mfbplyinv.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMFBFILLPOLY1RECT=mfbFillPolyInvert -DOPEQ=MFB_OPEQ_INVERT -DEQWHOLEWORD=MFB_EQWHOLEWORD_INVERT -o $@ $<

mfbbltC.c: mfbblt.c
	$(LN) $< $@

mfbbltC.o: mfbbltC.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMROP=Mcopy -o $@ $<

mfbbltX.c: mfbblt.c
	$(LN) $< $@

mfbbltX.o: mfbbltX.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMROP=Mxor -o $@ $<

mfbbltCI.c: mfbblt.c
	$(LN) $< $@

mfbbltCI.o: mfbbltCI.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMROP=McopyInverted -o $@ $<

mfbbltO.c: mfbblt.c
	$(LN) $< $@

mfbbltO.o: mfbbltO.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMROP=Mor -o $@ $<

mfbbltG.c: mfbblt.c
	$(LN) $< $@

mfbbltG.o: mfbbltG.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMROP=0 -o $@ $<

mfbtileC.c: mfbtile.c
	$(LN) $< $@

mfbtileC.o: mfbtileC.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMROP=Mcopy -o $@ $<

mfbtileG.c: mfbtile.c
	$(LN) $< $@

mfbtileG.o: mfbtileG.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -DMROP=0 -o $@ $<

%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(INCS) $(DEFS) -o $@ $<

clean:
	$(RM) $(OBJS) libmfb.a mfbseg.c mfbpgbwht.c mfbpgbblak.c mfbpgbinv.c mfbigbwht.c mfbigbblak.c mfbpawhite.c mfbpablack.c mfbpainv.c mfbtewhite.c mfbteblack.c mfbplywhite.c mfbplyblack.c mfbplyinv.c mfbbltC.c mfbbltX.c mfbbltCI.c mfbbltO.c mfbbltG.c mfbtileC.c mfbtileG.c

install:

.PHONY: all clean install
