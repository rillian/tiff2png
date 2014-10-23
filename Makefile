# Makefile for tiff2png
# Copyright (C) 1996 Willem van Schaik

#CC=cc
CC=gcc
#COPY=cp
COPY=/bin/cp -p
DEL=/bin/rm -f

# TAKE CARE: If you use libtiff version v3.4, please use -DNEW_LIBTIFF, for
#            the libtiff that comes with netpbm, which is v2.4, you must
#            change this to -DOLD_LIBTIFF. For other versions I don't know.
#            Reason is that v3.4 does automatic flipping when MINISWHITE.
# GRR 19990713:  This does not seem to be true for 3.4beta028; it still
#            requires OLD_LIBTIFF to be defined or else fax pages (1-bit)
#            come out mostly black.
TIFF_VERSION = -DOLD_LIBTIFF
#TIFF_VERSION = -DNEW_LIBTIFF

# change to match your directories (you see the ./ and ../ ?!?!)
#LIBTIFF=./libtiff
LIBTIFF=/work/graphics/libgr2/tiff/libtiff
#LIBTIFF=../netpbm/libtiff
# newer libtiffs (can) use libjpeg, too
LIBJPEG=/work/graphics/libgr2/jpeg

#LIBPNG=../libpng
LIBPNG=/usr/lib
#ZLIB=../zlib
ZLIB=/usr/lib

INSTALL=/usr/local

# GRR 19990713:  FAXPECT is a custom conversion option for stretched faxes
CFLAGS=$(TIFF_VERSION) -DFAXPECT -L. \
	-I$(LIBTIFF) \
	-I$(LIBPNG) \
	-I$(ZLIB)
LDFLAGS=-L. \
	-L$(LIBTIFF)/ \
	-L$(LIBPNG)/ \
	-L$(ZLIB)/ \
	-lpng -lz -ltiff -lm
SLDFLAGS=-L. \
	$(LIBTIFF)/libtiff.a $(LIBJPEG)/libjpeg.a \
	$(LIBPNG)/libpng.a \
	$(ZLIB)/libz.a \
	-lm

OBJS = tiff2png.o

all: tiff2png tiff2png-static

tiff2png: tiff2png.o
	$(CC) -o tiff2png tiff2png.o $(LDFLAGS)

tiff2png-static: tiff2png.o
	$(CC) -o tiff2png-static tiff2png.o $(SLDFLAGS)

install: all
	$(COPY) tiff2png $(INSTALL)/bin
	$(COPY) tiff2png.1 $(INSTALL)/man/man1

clean:
	$(DEL) *.o tiff2png

# leave this line empty

$(OBJS):
