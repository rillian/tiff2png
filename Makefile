# Makefile for tiff2png
# Copyright (C) 1996 Willem van Schaik

#CC=cc
CC=gcc
OPTIMFLAGS = -O2 
DEBUGFLAGS = -g -Wall -W
#COPY=cp
COPY=/bin/cp -p
DEL=/bin/rm -f

# TAKE CARE:  If you use the (very old) libtiff that comes with netpbm, which
#             is v2.4, you may need to change this to -DOLD_LIBTIFF.  (The
#             only difference is whether tiffcomp.h is included; it is not
#             installed by default in newer versions of libtiff, but it may
#             have been required for older versions.)
#TIFF_VERSION = -DOLD_LIBTIFF
TIFF_VERSION =

# It appears that PHOTOMETRIC_MINISWHITE should always be inverted (which
# makes sense), but if you find a class of TIFFs or a version of libtiff for
# which that is *not* the case, try not defining INVERT_MINISWHITE:
#
#MIN_INVERT =
MIN_INVERT = -DINVERT_MINISWHITE  


# change to match your directories (you see the ./ and ../ ?!?!)
#LIBTIFF=/usr/lib
#LIBTIFF=/usr/local/lib
LIBTIFF=../libtiff/libtiff
#LIBJPEG=../libgr2/tiff/libtiff
#LIBTIFF=../netpbm/libtiff

# newer libtiffs (can) use libjpeg, too
#LIBJPEG=/usr/lib
LIBJPEG=/usr/local/lib
#LIBJPEG=../libjpeg
#LIBJPEG=../libgr2/jpeg

#LIBPNG=/usr/lib
#LIBPNG=/usr/local/lib
LIBPNG=../libpng

#ZLIB=/usr/lib
#ZLIB=/usr/local/lib
ZLIB=../zlib

INSTALL=/usr/local

# GRR 19990713:  FAXPECT is a custom conversion option for stretched faxes
CFLAGS=$(TIFF_VERSION) -DFAXPECT $(MIN_INVERT) $(OPTIMFLAGS) $(DEBUGFLAGS) \
	-I$(LIBTIFF) \
	-I$(LIBPNG) \
	-I$(ZLIB)
LDFLAGS=-L. \
	-L$(LIBTIFF)/ \
	-L$(LIBPNG)/ \
	-L$(ZLIB)/ \
	-lpng -lz -ltiff -ljpeg -lm
SLDFLAGS=-L. \
	$(LIBTIFF)/libtiff.a $(LIBJPEG)/libjpeg.a \
	$(LIBPNG)/libpng.a \
	$(ZLIB)/libz.a \
	-lm

OBJS = tiff2png.o

# default is dynamic only (or mixed dynamic/static, depending on installed libs)
default: tiff2png

# it's nice to have a choice, though
all: tiff2png tiff2png-static

tiff2png: tiff2png.o
	$(CC) -o tiff2png tiff2png.o $(LDFLAGS)

tiff2png-static: tiff2png.o
	$(CC) -o tiff2png-static tiff2png.o $(SLDFLAGS)

install: all
	$(COPY) tiff2png $(INSTALL)/bin
	$(COPY) tiff2png.1 $(INSTALL)/man/man1

clean:
	$(DEL) *.o tiff2png tiff2png-static

# leave this line empty

$(OBJS):
