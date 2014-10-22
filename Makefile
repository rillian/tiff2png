# Makefile for tiff2png
# Copyright (C) 1996 Willem van Schaik

CC=cc
COPY=cp
DEL=rm

# TAKE CARE: If you use libtiff version v3.4, please use -DNEW_LIBTIFF, for
#            the libtiff that comes with netpbm, which is v2.4, you must
#            change this to -DOLD_LIBTIFF. For other versions I don't know.
#            Reason is that v3.4 does automatic flipping when MINISWHITE.

# change to match your directories (you see the ./ and ../ ?!?!)
LIBTIFF=./libtiff
#LIBTIFF=../netpbm/libtiff
LIBPNG=../libpng
ZLIB=../zlib
INSTALL=/usr/local

CFLAGS=-DOLD_LIBTIFF -L. \
	-I$(LIBTIFF) \
	-I$(LIBPNG) \
	-I$(ZLIB)
LDFLAGS=-L. \
	-L$(LIBTIFF)/ \
	-L$(LIBPNG)/ \
	-L$(ZLIB)/ \
	-lpng -lz -ltiff -lm

OBJS = tiff2png.o

all: tiff2png

tiff2png: tiff2png.o
	$(CC) -o tiff2png tiff2png.o $(LDFLAGS)

install: all
	$(COPY) tiff2png $(INSTALL)/bin
	$(COPY) tiff2png.1 $(INSTALL)/man/man1

clean:
	$(DEL) -f *.o tiff2png

# leave this line empty

$(OBJS):
