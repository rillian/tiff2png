# Makefile for tiff2png
# Copyright 1996 Willem van Schaik
# Copyright 2002 Greg Roelofs
# Copyright 2014 Ralph Giles

PACKAGE = tiff2png
VERSION = 0.92-git

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

EXTRA_DIST = README CHANGES Makefile.w32

OBJS = $(SRCS:%.c=%.o)

tiff2png: tiff2png.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

check: all
	./tiff2png -h

clean:
	$(RM) $(OBJS) tiff2png

install: all
	$(COPY) tiff2png $(INSTALL)/bin/

dist: $(PACKAGE)-$(VERSION).tar.gz

$(PACKAGE)-$(VERSION).tar.gz: Makefile $(SRCS) $(EXTRA_DIST)
	-$(RM) -r $(PACKAGE)-$(VERSION)
	mkdir $(PACKAGE)-$(VERSION)
	cp $^ $(PACKAGE)-$(VERSION)/
	tar czf $@ $(PACKAGE)-$(VERSION)/*
	$(RM) -r $(PACKAGE)-$(VERSION)

.PHONY: all clean install dist
