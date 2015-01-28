# Makefile for tiff2png
# Copyright 1996 Willem van Schaik
# Copyright 2002 Greg Roelofs
# Copyright 2014 Ralph Giles

PACKAGE := tiff2png
VERSION := 0.92

CC ?= gcc
CFLAGS ?= -g -Wall -O3
COPY := /bin/cp -p

PREFIX ?= /usr/local

# It appears that PHOTOMETRIC_MINISWHITE should always be inverted (which
# makes sense), but if you find a class of TIFFs or a version of libtiff for
# which that is *not* the case, try not defining INVERT_MINISWHITE.

CFLAGS += -DINVERT_MINISWHITE

# DESTDIR_IS_CURDIR will put all converted images into the current directory
# by default or if the -destdir option is given without an argument.

CFLAGS += -DDESTDIR_IS_CURDIR

GIT_VERSION := $(shell git describe --always --tags --match 'v*' --dirty)
VERSION := $(or $(patsubst v%,%,$(GIT_VERSION)),$(VERSION),unknown)

CFLAGS += -DVERSION=\"$(VERSION)\"

all: tiff2png

SRCS := tiff2png.c
LIBS := -ltiff -ljpeg -lpng -lz -lm

EXTRA_DIST := README CHANGES Makefile.w32

OBJS := $(SRCS:%.c=%.o)

tiff2png: tiff2png.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

check: all
	./tiff2png -h

clean:
	$(RM) $(OBJS) tiff2png

BINDIR := $(PREFIX)/bin

install: all
	mkdir -p $(BINDIR)
	$(COPY) tiff2png $(BINDIR)/


DISTDIR := $(PACKAGE)-$(VERSION)

dist: $(DISTDIR).tar.gz

$(DISTDIR).tar.gz: Makefile $(SRCS) $(EXTRA_DIST)
	-$(RM) -r $(DISTDIR)
	mkdir $(DISTDIR)
	cp $^ $(DISTDIR)/
	tar czf $@ $(DISTDIR)/*
	$(RM) -r $(DISTDIR)

distcheck: dist
	tar xf $(DISTDIR).tar.gz
	cd $(DISTDIR) && make check && make dist
	$(RM) -r $(DISTDIR)
	@echo $(DISTDIR).tar.gz is ready to distribute

.PHONY: all clean install dist distcheck
