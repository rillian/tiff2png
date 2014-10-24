# Makefile for tiff2png
# Copyright 1996 Willem van Schaik
# Copyright 2002 Greg Roelofs
# Copyright 2014 Ralph Giles

CC ?= gcc
CFLAGS ?= -g -Wall -O3
COPY = /bin/cp -p

PREFIX = /usr/local

# It appears that PHOTOMETRIC_MINISWHITE should always be inverted (which
# makes sense), but if you find a class of TIFFs or a version of libtiff for
# which that is *not* the case, try not defining INVERT_MINISWHITE.

CFLAGS += -DINVERT_MINISWHITE

# DESTDIR_IS_CURDIR will put all converted images into the current directory
# by default or if the -destdir option is given without an argument.

CFLAGS += -DDESTDIR_IS_CURDIR


all: tiff2png

SRCS = tiff2png.c
LIBS = -ltiff -ljpeg -lpng -lz -lm

OBJS = $(SRCS:%.c=%.o)

tiff2png: tiff2png.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) $(OBJS) tiff2png

install: all
	$(COPY) tiff2png $(INSTALL)/bin/

.PHONY: all clean install
